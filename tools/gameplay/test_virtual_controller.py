"""Host safety tests with a fake polling peer. No game or UI is launched."""
import importlib.util
import json
import os
from pathlib import Path
import stat
import struct
import tempfile
import threading
import time
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("virtual_controller", Path(__file__).with_name("virtual-controller.py"))
vc = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vc)
TOKEN = "a" * 32
EXE = r"C:\Games\Example\Example.exe"


class HostTests(unittest.TestCase):
    def setUp(self):
        # /tmp and /var can be symlinks on macOS. Use the actual temp parent.
        self.temporary = tempfile.TemporaryDirectory(dir=os.path.realpath(tempfile.gettempdir()))
        self.path = Path(self.temporary.name) / "controller"
        self.config = vc.initialize(self.path, EXE, r"Z:\private\tmp\controller\state.bin")
        self.token = self.config["environment"]["VIBEWINE_XINPUT_TEST_TOKEN"]

    def tearDown(self):
        self.temporary.cleanup()

    def write_ack(self, seq, polls, pid=123, status="READY", token=None, enabled=1):
        payload = f"VXIA0001 {token or self.token} {pid} {seq} {polls} {enabled} {status}\n"
        temporary = self.path / "peer.tmp"
        temporary.write_text(payload)
        os.replace(temporary, self.path / vc.ACK_FILE)

    def fake_peer(self, expected_flags, status, *, pid=123, polls=1, token=None, minimum_seq=1):
        errors = []
        def peer():
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                try:
                    packet = (self.path / vc.STATE_FILE).read_bytes()
                    if len(packet) == 72:
                        values = vc.SNAPSHOT.unpack(packet)
                        if values[-1] == expected_flags and values[2] >= minimum_seq:
                            self.write_ack(values[2], polls, pid, status, token)
                            return
                except FileNotFoundError:
                    pass
                time.sleep(0.002)
            errors.append("peer did not receive the expected snapshot")
        thread = threading.Thread(target=peer)
        thread.start()
        def finish_peer():
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertEqual(errors, [])
        self.addCleanup(finish_peer)
        return thread

    def bind(self):
        self.fake_peer(vc.ARM, "READY")
        return vc.command(self.path, vc.ARM, timeout_ms=200)

    def test_layout_limits_and_neutral_control_frames(self):
        raw = vc.encode_snapshot(TOKEN, 0x0102030405060708, 1000,
                                 buttons=0xf7ff, lt=255, rt=254,
                                 lx=-32768, ly=32767, rx=-1, ry=1, pid=123)
        self.assertEqual(len(raw), 72)
        self.assertEqual(raw[:8], b"VXIP0001")
        self.assertEqual(raw[8:40], TOKEN.encode())
        self.assertEqual(raw[40:48], bytes.fromhex("0807060504030201"))
        self.assertEqual(struct.unpack_from("<IHBBhhhhII", raw, 48),
                         (1000, 0xf7ff, 255, 254, -32768, 32767, -1, 1, 123, 0))
        for changes in ({"seq": 0}, {"lease_ms": 0}, {"lease_ms": 1001},
                        {"buttons": 65536}, {"buttons": 0x0800}, {"lt": -1}, {"ry": 32768},
                        {"pid": 0}, {"flags": vc.ARM, "lx": 1},
                        {"token": "A" * 32}, {"seq": True}):
            args = dict(token=TOKEN, seq=1, lease_ms=1, pid=123)
            args.update(changes)
            with self.subTest(changes=changes), self.assertRaises(vc.ControllerError):
                vc.encode_snapshot(**args)

    def test_init_is_private_and_never_overwrites(self):
        self.assertEqual(stat.S_IMODE(self.path.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE((self.path / vc.MANIFEST).stat().st_mode), 0o600)
        self.assertRegex(self.token, "^[0-9a-f]{32}$")
        self.assertFalse((self.path / vc.STATE_FILE).exists())
        with self.assertRaises(FileExistsError):
            vc.initialize(self.path, EXE, r"Z:\state.bin")
        self.assertEqual(vc.status(self.path)["sequence"], 0)

    def test_arm_binds_actual_pid_and_state_receipt(self):
        result = self.bind()
        self.assertEqual(result["receipt"]["pid"], 123)
        self.fake_peer(vc.STATE, "STATE", polls=2)
        result = vc.command(self.path, vc.STATE, buttons=0x0002, timeout_ms=200)
        self.assertEqual(result["receipt"]["seq"], 2)
        raw = (self.path / vc.STATE_FILE).read_bytes()
        self.assertEqual(vc.SNAPSHOT.unpack(raw)[-2:], (123, vc.STATE))
        self.assertEqual(stat.S_IMODE((self.path / vc.STATE_FILE).stat().st_mode), 0o600)

    def test_state_requires_acknowledged_arm_and_does_not_create_snapshot(self):
        with self.assertRaisesRegex(vc.ControllerError, "ARM"):
            vc.command(self.path, vc.STATE, buttons=1)
        self.assertFalse((self.path / vc.STATE_FILE).exists())

    def test_foreign_identity_never_establishes_binding(self):
        for token in ("b" * 32,):
            self.fake_peer(vc.ARM, "READY", token=token)
            with self.assertRaisesRegex(vc.ControllerError, "unacknowledged"):
                vc.command(self.path, vc.ARM, timeout_ms=30)
            result = vc.status(self.path)
            self.assertEqual(result["pid"], 0)
            self.assertFalse(result["armed"])

    def test_stale_receipt_and_wrong_pid_do_not_count_as_delivery(self):
        self.bind()
        # Matching future sequence with an old poll counter is still not a new poll.
        self.fake_peer(vc.STATE, "STATE", polls=1)
        with self.assertRaisesRegex(vc.ControllerError, "unacknowledged"):
            vc.command(self.path, vc.STATE, buttons=1, timeout_ms=30)
        packet = vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())
        self.assertEqual(packet[-1], vc.RELEASE)
        self.assertEqual(packet[2], 3)
        self.assertEqual(packet[3], 1)
        self.assertEqual(packet[4:11], (0, 0, 0, 0, 0, 0, 0))
        self.assertFalse(vc.status(self.path)["armed"])

    def test_wrong_bound_pid_and_disabled_receipt_fail_closed(self):
        self.bind()
        self.write_ack(2, 2, pid=456, status="STATE")
        with vc.locked_session(self.path) as (directory, manifest):
            with self.assertRaisesRegex(vc.ControllerError, "PID"):
                vc.receipt_for(directory, manifest, seq=2)
        self.fake_peer(vc.STATE, "DISABLED", polls=2)
        with self.assertRaisesRegex(vc.ControllerError, "DISABLED"):
            vc.command(self.path, vc.STATE, buttons=1, timeout_ms=200)
        with self.assertRaisesRegex(vc.ControllerError, "ARM"):
            vc.command(self.path, vc.STATE, timeout_ms=10)

    def test_release_is_neutral_and_requires_new_arm(self):
        self.bind()
        self.fake_peer(vc.RELEASE, "RELEASE", polls=2)
        result = vc.command(self.path, vc.RELEASE, timeout_ms=200)
        self.assertEqual(result["receipt"]["status"], "RELEASE")
        with self.assertRaisesRegex(vc.ControllerError, "ARM"):
            vc.command(self.path, vc.STATE, timeout_ms=10)
        self.fake_peer(vc.ARM, "READY", polls=3)
        vc.command(self.path, vc.ARM, timeout_ms=200)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-2], 123)

    def test_late_first_arm_receipt_recovers_pid_without_reusing_sequence(self):
        with self.assertRaises(vc.ControllerError):
            vc.command(self.path, vc.ARM, timeout_ms=10)
        self.write_ack(1, 1, status="EXPIRED")
        self.fake_peer(vc.ARM, "READY", polls=2, minimum_seq=2)
        result = vc.command(self.path, vc.ARM, timeout_ms=200)
        self.assertEqual(result["receipt"]["seq"], 2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-2], 123)

    def test_invalid_receipt_cannot_acknowledge_arm(self):
        self.fake_peer(vc.ARM, "INVALID", polls=1)
        with self.assertRaisesRegex(vc.ControllerError, "INVALID"):
            vc.command(self.path, vc.ARM, timeout_ms=200)
        self.assertFalse(vc.status(self.path)["last_command_acknowledged"])

    def test_status_is_historical_and_rejects_stale_ack(self):
        self.bind()
        result = vc.status(self.path)
        self.assertTrue(result["historical_only"])
        self.assertNotIn("token", result["receipt"])
        self.write_ack(9, 9)
        result = vc.status(self.path)
        self.assertIsNone(result["receipt"])
        self.assertFalse(result["armed"])

    def test_no_automatic_lease_renewal_after_writer_returns(self):
        self.bind()
        self.fake_peer(vc.STATE, "STATE", polls=2)
        vc.command(self.path, vc.STATE, lx=12345, lease_ms=10, timeout_ms=200)
        initial = (self.path / vc.STATE_FILE).read_bytes()
        time.sleep(0.025)
        self.assertEqual((self.path / vc.STATE_FILE).read_bytes(), initial)
        self.assertEqual(vc.SNAPSHOT.unpack(initial)[3], 10)
        # Reader must independently expire this unchanged packet. This test does
        # not claim to exercise Wine's reader or prove game release behavior.

    def test_symlinks_hardlinks_and_nonprivate_files_rejected(self):
        link = self.path.parent / "linked"
        link.symlink_to(self.path, target_is_directory=True)
        with self.assertRaises(OSError):
            vc.status(link)
        outside = self.path.parent / "outside"
        outside.write_bytes(b"unchanged")
        (self.path / vc.STATE_FILE).symlink_to(outside)
        with self.assertRaises(OSError):
            vc.command(self.path, vc.ARM, timeout_ms=10)
        self.assertEqual(outside.read_bytes(), b"unchanged")
        (self.path / vc.STATE_FILE).unlink()
        os.link(outside, self.path / vc.STATE_FILE)
        with self.assertRaises(vc.ControllerError):
            vc.command(self.path, vc.ARM, timeout_ms=10)
        (self.path / vc.STATE_FILE).unlink()
        (self.path / vc.STATE_FILE).write_bytes(b"old")
        os.chmod(self.path / vc.STATE_FILE, 0o644)
        with self.assertRaises(vc.ControllerError):
            vc.command(self.path, vc.ARM, timeout_ms=10)

    def test_receipt_symlink_is_not_followed(self):
        other = self.path.parent / "ack"
        other.write_text(f"VXIA0001 {self.token} 123 1 1 1 READY\n")
        (self.path / vc.ACK_FILE).symlink_to(other)
        with self.assertRaises(vc.ControllerError):
            vc.command(self.path, vc.ARM, timeout_ms=20)
        self.assertEqual(vc.status(self.path)["pid"], 0)

    def test_concurrent_writer_is_rejected_without_queueing(self):
        with vc.locked_session(self.path):
            with self.assertRaisesRegex(vc.ControllerError, "another command"):
                vc.command(self.path, vc.ARM, timeout_ms=20)
        self.assertFalse((self.path / vc.STATE_FILE).exists())

    def test_atomic_replace_leaves_old_or_complete_new_packet(self):
        with vc.locked_session(self.path) as (directory, manifest):
            original = vc.encode_snapshot(self.token, 1, 100, flags=vc.ARM)
            vc.atomic_write(directory, vc.STATE_FILE, original)
            replacement = vc.encode_snapshot(self.token, 2, 100, flags=vc.ARM)
            real_replace = os.replace
            def inspect_then_replace(*args, **kwargs):
                self.assertEqual((self.path / vc.STATE_FILE).read_bytes(), original)
                real_replace(*args, **kwargs)
                self.assertEqual((self.path / vc.STATE_FILE).read_bytes(), replacement)
            with mock.patch.object(vc.os, "replace", side_effect=inspect_then_replace):
                vc.atomic_write(directory, vc.STATE_FILE, replacement)
        self.assertEqual(list(self.path.glob(".write-*")), [])

    def pulse_peers(self, state_status="STATE", release_status="RELEASE"):
        self.fake_peer(vc.ARM, "READY", polls=1)
        self.fake_peer(vc.STATE, state_status, polls=2)
        self.fake_peer(vc.RELEASE, release_status, polls=3)

    def test_pulse_single_lock_arm_state_hold_release(self):
        self.pulse_peers()
        original_publish = vc.publish
        observed = []
        def publish(directory, manifest, flags, *args, **kwargs):
            if flags == vc.STATE:
                with self.assertRaisesRegex(vc.ControllerError, "another command"):
                    vc.command(self.path, vc.ARM, timeout_ms=1)
            seq = original_publish(directory, manifest, flags, *args, **kwargs)
            observed.append(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes()))
            return seq
        with mock.patch.object(vc, "publish", side_effect=publish):
            result = vc.pulse(self.path, hold_ms=60, buttons=2)
        self.assertEqual([packet[-1] for packet in observed], [vc.ARM, vc.STATE, vc.RELEASE])
        self.assertEqual([packet[2] for packet in observed], [1, 2, 3])
        self.assertEqual(observed[0][3], 1000)
        self.assertEqual(observed[1][3], 60)
        self.assertEqual(observed[1][4], 2)
        self.assertEqual(observed[2][4:11], (0, 0, 0, 0, 0, 0, 0))
        self.assertEqual(set(result["receipts"]), {"arm", "state", "release"})
        self.assertFalse(vc.status(self.path)["last_command_acknowledged"])

    def test_pulse_invalid_arguments_do_not_publish_arm(self):
        for values in ({"hold_ms": 0}, {"hold_ms": 501}, {"buttons": 0x0800},
                       {"lx": 32768}, {"timeout_ms": 0}, {"timeout_ms": 5001}):
            with self.subTest(values=values), self.assertRaises(vc.ControllerError):
                vc.pulse(self.path, **values)
        self.assertFalse((self.path / vc.STATE_FILE).exists())
        self.assertEqual(vc.status(self.path)["sequence"], 0)

    def test_pulse_reserves_cleanup_sequences_before_arm(self):
        with vc.locked_session(self.path) as (directory, manifest):
            manifest["seq"] = vc.MAX_SEQ - 3
            vc.save_manifest(directory, manifest)
        with self.assertRaisesRegex(vc.ControllerError, "insufficient sequences"):
            vc.pulse(self.path, buttons=2)
        self.assertFalse((self.path / vc.STATE_FILE).exists())

    def test_pulse_state_rejection_still_releases_and_reports_failure(self):
        self.pulse_peers(state_status="INVALID")
        with self.assertRaisesRegex(vc.ControllerError, "pulse failed.*INVALID"):
            vc.pulse(self.path, hold_ms=60, lx=20000)
        packet = vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())
        self.assertEqual(packet[-1], vc.RELEASE)
        self.assertEqual(packet[4:11], (0, 0, 0, 0, 0, 0, 0))
        self.assertFalse(vc.status(self.path)["last_command_acknowledged"])

    def test_pulse_state_timeout_still_releases(self):
        self.fake_peer(vc.ARM, "READY", polls=1)
        self.fake_peer(vc.RELEASE, "RELEASE", polls=3)
        with self.assertRaisesRegex(vc.ControllerError, "pulse failed"):
            vc.pulse(self.path, hold_ms=30, buttons=2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-1], vc.RELEASE)

    def test_pulse_publication_error_after_state_write_still_releases(self):
        self.fake_peer(vc.ARM, "READY", polls=1)
        self.fake_peer(vc.RELEASE, "RELEASE", polls=2)
        original_write = vc.atomic_write
        def fail_after_write(directory, name, raw):
            original_write(directory, name, raw)
            if name == vc.STATE_FILE and vc.SNAPSHOT.unpack(raw)[-1] == vc.STATE:
                raise OSError("state publication failed after replace")
        with mock.patch.object(vc, "atomic_write", side_effect=fail_after_write):
            with self.assertRaisesRegex(vc.ControllerError, "state publication failed after replace"):
                vc.pulse(self.path, hold_ms=120, buttons=2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-1], vc.RELEASE)
        self.assertFalse(vc.status(self.path)["last_command_acknowledged"])

    def test_pulse_reports_primary_and_release_failures(self):
        self.pulse_peers(state_status="INVALID", release_status="INVALID")
        with self.assertRaisesRegex(vc.ControllerError, "pulse failed:.*INVALID.*release unacknowledged"):
            vc.pulse(self.path, hold_ms=60, buttons=2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-1], vc.RELEASE)

    def test_pulse_release_timeout_is_not_success(self):
        self.fake_peer(vc.ARM, "READY", polls=1)
        self.fake_peer(vc.STATE, "STATE", polls=2)
        with self.assertRaisesRegex(vc.ControllerError, "release unacknowledged"):
            vc.pulse(self.path, hold_ms=30, timeout_ms=250, buttons=2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-1], vc.RELEASE)

    def test_pulse_interrupt_during_hold_releases_without_hiding_interrupt(self):
        self.pulse_peers()
        original_sleep = time.sleep
        def interrupted_hold(seconds):
            if seconds > 0.02:
                raise KeyboardInterrupt("test interruption")
            original_sleep(seconds)
        with mock.patch.object(vc.time, "sleep", side_effect=interrupted_hold):
            with self.assertRaisesRegex(KeyboardInterrupt, "test interruption"):
                vc.pulse(self.path, hold_ms=120, buttons=2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-1], vc.RELEASE)
        self.assertFalse(vc.status(self.path)["last_command_acknowledged"])

    def test_pulse_arm_lease_guard_prevents_late_active_state(self):
        self.fake_peer(vc.ARM, "READY", polls=1)
        self.fake_peer(vc.RELEASE, "RELEASE", polls=2)
        original_command = vc._command_locked
        def delayed_arm(*args, **kwargs):
            result, started = original_command(*args, **kwargs)
            if args[2] == vc.ARM:
                started = time.monotonic() - 1
            return result, started
        with mock.patch.object(vc, "_command_locked", side_effect=delayed_arm):
            with self.assertRaisesRegex(vc.ControllerError, "ARM lease guard"):
                vc.pulse(self.path, hold_ms=60, buttons=2)
        self.assertEqual(vc.status(self.path)["sequence"], 2)
        self.assertEqual(vc.SNAPSHOT.unpack((self.path / vc.STATE_FILE).read_bytes())[-1], vc.RELEASE)

    def test_pulse_short_whole_deadline_prevents_active_state(self):
        self.fake_peer(vc.ARM, "READY", polls=1)
        self.fake_peer(vc.RELEASE, "RELEASE", polls=2)
        with self.assertRaisesRegex(vc.ControllerError, "insufficient pulse deadline"):
            vc.pulse(self.path, hold_ms=120, timeout_ms=150, buttons=2)
        self.assertEqual(vc.status(self.path)["sequence"], 2)

    def test_invalid_receipt_formats(self):
        valid = f"VXIA0001 {TOKEN} 1 1 1 1 STATE".encode()
        self.assertEqual(vc.parse_ack(valid)["status"], "STATE")
        for raw in (valid + b" extra", valid.replace(b"STATE", b"UNKNOWN"),
                    valid.replace(b" 1 1 1 1 ", b" 0 1 1 1 "),
                    valid.replace(b" 1 1 1 1 ", b" 1 1 0 1 "),
                    valid.replace(b" 1 1 1 1 ", b" 1 1 1 2 "), b"\xff"):
            with self.subTest(raw=raw), self.assertRaises(vc.ControllerError):
                vc.parse_ack(raw)


if __name__ == "__main__":
    unittest.main()

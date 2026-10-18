#!/usr/bin/env python3
"""Offline CLI contract tests; actual producer races live in tests_protocol.cpp.

This suite supplies static, explicitly constructed on-disk protocol states to
exercise JSON/exit status, version, path, and identity controls. It does not run
Wine or assert that a real producer was started.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

MAGIC = 0x4353325452414332
OLD_MAGIC = 0x4353325452414345
CAPACITY = 131072
BYTES = 4096 + CAPACITY * 64
OPEN = 1 << 30
CLOSING = 2 << 30
MARKER = b"CS2TRACE_SESSION_V2\n"
HEADER = struct.Struct("<7Q")
EVENT = struct.Struct("<QQQIIIIQQQ")
CONTROLLER = Path(__file__).with_name("cs2trace")


class CLITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cs2trace-cli-", dir="/private/tmp")
        self.root = Path(self.temp.name)
        result = self.call("new-session", self.root)
        self.assertEqual(result.returncode, 0, result.stderr)
        row = self.one(result)
        self.assertIs(row["runtime_started"], False)
        self.session = Path(row["session"])
        self.assertEqual(self.session.parent, self.root)
        self.assertRegex(self.session.name, r"^dxmt-trace-session-[0-9a-f]{32}$")
        self.assertEqual(self.session.stat().st_mode & 0o777, 0o700)
        self.assertEqual((self.session / ".cs2trace-session").read_bytes(), MARKER)
        self.path = self.session / "trace.bin"
        self.create_trace(self.path)
        self.identity = self.one(self.call_ok("inspect", self.path))["identity"]
        self.addCleanup(self.temp.cleanup)

    @staticmethod
    def create_trace(path):
        fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            os.ftruncate(fd, BYTES)
            os.pwrite(fd, HEADER.pack(MAGIC, 2, CAPACITY, 0, 0, 0, 0), 0)
        finally:
            os.close(fd)

    def call(self, *args):
        return subprocess.run([str(CONTROLLER), *map(str, args)], capture_output=True,
                              text=True, timeout=5)

    def call_ok(self, *args):
        result = self.call(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def call_bad(self, *args, code=1):
        result = self.call(*args)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        self.assertTrue(result.stderr.strip(), "failure must explain rejection")
        return result

    def rows(self, result):
        return [json.loads(line) for line in result.stdout.splitlines()]

    def one(self, result):
        rows = self.rows(result)
        self.assertEqual(len(rows), 1, result.stdout)
        return rows[0]

    def summary(self, result):
        rows = self.rows(result)
        self.assertTrue(rows, "decode omitted summary")
        self.assertEqual(rows[-1]["kind"], "summary")
        return rows[-1]

    def patch(self, offset, *values):
        with self.path.open("r+b") as handle:
            os.pwrite(handle.fileno(), struct.pack("<" + "Q" * len(values), *values), offset)

    def header(self):
        with self.path.open("rb") as handle:
            return HEADER.unpack(handle.read(HEADER.size))

    def arm(self, gen=1, planned=1024, reserve=128):
        return self.one(self.call_ok("arm", self.path, gen, self.identity, planned, reserve))

    def stop(self, gen=1):
        return self.one(self.call_ok("stop", self.path, gen, self.identity))

    def publish_fixture(self, gen=1, commit=1):
        with self.path.open("r+b") as handle:
            data = EVENT.pack(100, 180, 0x123, 1, 77, 0, gen, 9, 10, commit)
            self.assertEqual(os.pwrite(handle.fileno(), data, 4096), EVENT.size)
        self.patch(24, 1)

    def test_closed_capture_workflow_and_truthful_json(self):
        inspected = self.one(self.call_ok("inspect", self.path))
        self.assertEqual(inspected["protocol_version"], 2)
        self.assertIs(inspected["live_pid_verified"], False)
        armed = self.arm()
        self.assertEqual(armed["capture"], 1)
        self.assertIs(armed["producer_quiescence_verified"], False)
        self.assertIs(armed["structurally_complete"], False)
        self.assertEqual(armed["admitted_writers"], 0)
        self.call_bad("decode", self.path, 1, self.identity)
        self.publish_fixture()
        stopped = self.stop()
        self.assertEqual(stopped["operation"], "stop")
        self.assertEqual(stopped["capture"], 0)
        self.assertIs(stopped["producer_quiescence_verified"], True)
        self.assertIs(stopped["structurally_complete"], True)
        decoded = self.call_ok("decode", self.path, 1, self.identity)
        rows = self.rows(decoded)
        self.assertEqual(len(rows), 2)
        event, summary = rows
        self.assertEqual(event["kind"], "event")
        self.assertEqual(event["generation"], 1)
        self.assertEqual(event["duration_ns"], 80)
        self.assertEqual(event["clock_domain"], "cpu_steady_ns")
        self.assertEqual(summary["events"], 1)
        self.assertIs(summary["structurally_complete"], True)
        self.assertIs(summary["producer_quiescence_verified"], True)
        self.assertIs(summary["inconclusive_window"], False)
        self.assertIs(summary["all_operations_complete"], False)
        self.assertIs(summary["live_pid_verified"], False)
        self.assertIs(summary["bridge_verified"], False)
        self.assertEqual(summary["boundary_rejected_total"], 0)

    def test_timeout_before_reservation_has_stable_counters_but_is_incomplete(self):
        self.arm()
        self.patch(32, (1 << 32) | OPEN | 1)
        result = self.call("stop", self.path, 1, self.identity)
        self.assertEqual(result.returncode, 3, result.stderr)
        stopped = self.one(result)
        self.assertIs(stopped["producer_quiescence_verified"], False)
        self.assertIs(stopped["structurally_complete"], False)
        self.assertEqual(stopped["admitted_writers"], 1)
        self.assertEqual(stopped["capture"], 0)
        self.assertEqual(self.header()[4], (1 << 32) | CLOSING | 1)
        decoded = self.call("decode", self.path, 1, self.identity)
        self.assertEqual(decoded.returncode, 3, decoded.stderr)
        summary = self.summary(decoded)
        self.assertEqual(summary["next_before_reserved"], 0)
        self.assertEqual(summary["next_after_reserved"], 0)
        self.assertEqual(summary["uncommitted_reserved"], 0)
        self.assertIs(summary["counters_changed"], False)
        self.assertIs(summary["structurally_complete"], False)
        self.assertIs(summary["producer_quiescence_verified"], False)
        self.assertIs(summary["inconclusive_window"], True)
        before = self.path.read_bytes()[:56]
        self.call_bad("arm", self.path, 2, self.identity, 1024, 128)
        self.call_bad("stop", self.path, 1, self.identity)
        self.assertEqual(self.path.read_bytes()[:56], before)

    def test_closing_reserved_slot_stays_incomplete(self):
        self.arm()
        self.patch(24, 1, (1 << 32) | CLOSING | 1)
        decoded = self.call("decode", self.path, 1, self.identity)
        self.assertEqual(decoded.returncode, 3, decoded.stderr)
        summary = self.summary(decoded)
        self.assertEqual(summary["uncommitted_reserved"], 1)
        self.assertIs(summary["structurally_complete"], False)
        self.assertIs(summary["inconclusive_window"], True)

    def test_closed_reservation_hole_is_structurally_complete_but_lossy(self):
        self.arm()
        self.stop()
        self.patch(24, 1)
        decoded = self.call("decode", self.path, 1, self.identity)
        self.assertEqual(decoded.returncode, 3, decoded.stderr)
        summary = self.summary(decoded)
        self.assertIs(summary["structurally_complete"], True)
        self.assertIs(summary["producer_quiescence_verified"], True)
        self.assertIs(summary["inconclusive_window"], True)
        self.assertEqual(summary["uncommitted_reserved"], 1)

    def test_overflow_and_boundary_rejection_report_separately(self):
        self.arm()
        self.stop()
        self.patch(24, CAPACITY + 1)
        self.patch(40, 1, 4)
        decoded = self.call("decode", self.path, 1, self.identity)
        self.assertEqual(decoded.returncode, 3, decoded.stderr)
        summary = self.summary(decoded)
        self.assertIs(summary["structurally_complete"], True)
        self.assertIs(summary["inconclusive_window"], True)
        self.assertEqual(summary["dropped_before"], 1)
        self.assertEqual(summary["dropped_after"], 1)
        self.assertEqual(summary["boundary_rejected_total"], 4)
        self.assertIs(summary["all_operations_complete"], False)

    def test_generation_controls_and_old_events_not_reattributed(self):
        self.arm()
        self.publish_fixture()
        before = self.header()
        self.call_bad("stop", self.path, 2, self.identity)
        self.assertEqual(self.header(), before)
        self.stop()
        self.call_bad("arm", self.path, 1, self.identity, 1024, 128)
        self.call_bad("decode", self.path, 2, self.identity)
        self.arm(2)
        self.stop(2)
        summary = self.summary(self.call_ok("decode", self.path, 2, self.identity))
        self.assertEqual(summary["events"], 0)
        self.assertEqual(summary["wrong_generation"], 1)
        self.assertEqual(summary["committed_all_generations"], 1)
        self.assertIs(summary["inconclusive_window"], False)

    def test_empty_generation_cannot_be_reused(self):
        self.arm(0xFFFFFFFF)
        self.stop(0xFFFFFFFF)
        self.call_bad("arm", self.path, 0xFFFFFFFF, self.identity, 1024, 128)
        self.arm(1)
        self.stop(1)

    def test_generation_parser_and_usage(self):
        before = self.header()
        for gen in ("", "0", "-1", "+1", "1x", "4294967296", "99999999999999999999999999"):
            with self.subTest(generation=gen):
                self.call_bad("arm", self.path, gen, self.identity, 1024, 128)
                self.assertEqual(self.header(), before)
        self.call_bad("arm", self.path, 1, code=2)
        self.call_bad("unknown", code=2)

    def test_header_versions_and_publication_marker_rejected(self):
        self.patch(0, OLD_MAGIC, 1)
        self.call_bad("inspect", self.path)
        self.patch(0, MAGIC, 1)
        self.call_bad("inspect", self.path)
        self.patch(0, 0, 2)
        self.call_bad("inspect", self.path)
        self.patch(0, MAGIC, 2)
        self.arm()
        self.publish_fixture(commit=2)
        self.stop()
        failed = self.call_bad("decode", self.path, 1, self.identity)
        self.assertEqual(failed.stdout, "", "corrupt event must not yield success summary")

    def test_explicit_identity_and_replacement_guard(self):
        self.call_bad("arm", self.path, 1, "0:0", 1024, 128)
        self.call_bad("arm", self.path, 1, "", 1024, 128)
        self.assertEqual(self.header()[4], 0)
        old = self.path.with_name("trace.old")
        self.path.rename(old)
        self.create_trace(self.path)
        self.call_bad("arm", self.path, 1, self.identity, 1024, 128)
        self.assertEqual(self.header()[4], 0)
        with old.open("rb") as handle:
            self.assertEqual(HEADER.unpack(handle.read(56))[4], 0)

    def test_path_file_and_marker_controls(self):
        self.call_bad("inspect", "trace.bin")
        self.call_bad("new-session", ".")
        self.call_bad("inspect", self.session / ".." / self.session.name / "trace.bin")
        alias = self.session / "alias.bin"
        alias.symlink_to(self.path)
        self.call_bad("inspect", alias)
        linked = self.session / "hardlink.bin"
        os.link(self.path, linked)
        self.call_bad("inspect", self.path)
        linked.unlink()
        os.truncate(self.path, BYTES - 1)
        self.call_bad("inspect", self.path)
        os.truncate(self.path, BYTES)
        marker = self.session / ".cs2trace-session"
        marker.write_bytes(b"CS2TRACE_SESSION_V1\n")
        self.call_bad("inspect", self.path)
        marker.write_bytes(MARKER)
        os.chmod(self.session, 0o755)
        self.call_bad("inspect", self.path)

    def test_session_lock_prevents_concurrent_controller(self):
        marker = self.session / ".cs2trace-session"
        with marker.open("r+b") as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.call_bad("inspect", self.path)
            self.call_bad("arm", self.path, 1, self.identity, 1024, 128)
        self.arm()
        self.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--controller", type=Path, default=CONTROLLER)
    options, rest = parser.parse_known_args()
    CONTROLLER = options.controller.resolve()
    if not CONTROLLER.is_file():
        parser.error("controller binary missing: " + str(CONTROLLER))
    unittest.main(argv=[sys.argv[0], *rest])

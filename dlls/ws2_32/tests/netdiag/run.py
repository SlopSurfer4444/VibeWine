#!/usr/bin/env python3
"""Build/run native logger and controller regressions in one disposable directory.

No Wine installation, game process, external packets or machine changes.
LGPL-2.1-or-later.
"""
import json
import fcntl
import os
from pathlib import Path
import select
import shlex
import shutil
import stat
import struct
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
EXE = r"C:\fixture\netdiag.exe"
TOKEN = "0123456789abcdef" * 2
CAPACITY = 16384
SIZE = 16384 + CAPACITY * 128


def run(argv, ok=True, **kwargs):
    p = subprocess.run([str(a) for a in argv], text=True, capture_output=True, timeout=30, **kwargs)
    assert (p.returncode == 0) == ok, (argv, p.returncode, p.stdout, p.stderr)
    return p


def response(p):
    assert select.select([p.stdout], [], [], 15)[0], "fixture response timeout"
    line = p.stdout.readline()
    assert line, ("fixture exited", p.poll(), p.stderr.read())
    return json.loads(line)


class Fixture:
    def __init__(self, binary, path=None, token=TOKEN, claimed=True):
        env = os.environ.copy()
        env.pop("VIBEWINE_NETDIAG_FILE", None)
        env.pop("VIBEWINE_NETDIAG_TOKEN", None)
        if path is not None:
            env.update(VIBEWINE_NETDIAG_FILE=str(path), VIBEWINE_NETDIAG_TOKEN=token)
        self.p = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, env=env)
        assert response(self.p) == {"ready": True, "claimed": claimed}

    def command(self, command):
        self.p.stdin.write(command + "\n")
        self.p.stdin.flush()
        result = response(self.p)
        assert result["ok"]
        return result

    def close(self):
        self.p.stdin.write("quit\n")
        self.p.stdin.flush()
        out, err = self.p.communicate(timeout=15)
        assert self.p.returncode == 0, (out, err)


def main():
    checks = []
    with tempfile.TemporaryDirectory(prefix="vibewine-netdiag-") as temp:
        tmp = Path(temp)
        tool, native = tmp / "netdiag-tool", tmp / "logger-fixture"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        assert shutil.which(compiler[0]), "C compiler unavailable"
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include")]
        run(compiler + flags + [HERE / "netdiag-tool.c", "-o", tool])
        run(compiler + flags + ["-pthread", HERE / "logger-fixture.c", "-o", native])
        checks.append("strict C11 controller and native pthread fixture builds")
        sequence = 0

        def init(exe=EXE):
            nonlocal sequence
            sequence += 1
            directory = tmp / f"capture-{sequence}"
            run([tool, "init", directory, exe, TOKEN])
            path = directory / "netdiag.bin"
            assert stat.S_IMODE(directory.stat().st_mode) == 0o700
            assert stat.S_IMODE(path.stat().st_mode) == 0o600
            data = path.read_bytes()
            assert len(data) == SIZE
            assert struct.unpack_from("<QIIII", data) == (0x31474454454E5756, 1, 128, CAPACITY, 0)
            assert data[24:88] == bytes(64)
            assert data[88:120] == TOKEN.encode()
            assert data[120:120 + len(exe)] == exe.encode()
            assert not any(data[120 + len(exe):])
            return path

        def patch(path, offset, value):
            with path.open("r+b") as f:
                f.seek(offset)
                f.write(value)

        def dump(path):
            output = run([tool, "dump", path]).stdout
            assert TOKEN not in output and EXE not in output and '"handle"' not in output
            rows = [json.loads(s) for s in output.splitlines()]
            h, records = rows[0], rows[1:]
            assert h["count"] == len(records) <= h["reserved"] <= CAPACITY
            seqs = [e["sequence"] for e in records]
            assert seqs == sorted(set(seqs))
            for e in records:
                assert e["operation_id"] > 0 and e["host_pid"] > 0 and e["thread_id"] == 77
                if e["api"] in (11, 12):
                    assert e["address_hex"] is None
                    assert e["family"] == 0 and e["flags"] & 4
                    assert e["wait_capture_limit"] == 16
                    if e["flags"] & 128:
                        assert e["wait_metadata_valid"] and 0 <= e["wait_captured"] <= 16
                    else:
                        assert not e["wait_metadata_valid"]
                        assert e["wait_timeout_kind"] == "unknown"
                        assert e["wait_timeout_100ns"] is None and e["wait_total"] is None and e["wait_captured"] is None
                else:
                    assert len(e["address_hex"]) == 32
                if e["phase"] == 4 and e["api"] == 4 and e["flags"] & 1:
                    assert e["returned"] == -e["requested"]
            return h, records

        path = init()
        before = path.read_bytes()
        result = run([tool, "verify-fresh", path, EXE, TOKEN])
        assert json.loads(result.stdout) == {"fresh": True, "version": 1, "record_size": 128,
                                            "capacity": CAPACITY, "state": 0, "reserved": 0,
                                            "dropped": 0, "host_pid": 0, "windows_pid": 0}
        assert TOKEN not in result.stdout and EXE not in result.stdout
        run([tool, "verify-fresh", path, EXE, "f" * 32], ok=False)
        run([tool, "verify-fresh", path, EXE + "other", TOKEN], ok=False)
        assert path.read_bytes() == before
        run([tool, "init", path.parent, EXE, TOKEN], ok=False)
        assert path.read_bytes() == before
        run([tool, "arm", path, "10"], ok=False)
        f = Fixture(native, path)
        run([tool, "verify-fresh", path, EXE, TOKEN], ok=False)
        # A held controller lock must fail immediately, rather than queueing and
        # arming/stopping after another controller releases the session.
        with path.open("r+b") as locked:
            fcntl.flock(locked, fcntl.LOCK_EX | fcntl.LOCK_NB)
            for command in [["arm", path, "45000"], ["stop", path]]:
                started = time.monotonic()
                rejected = subprocess.run([str(tool)] + [str(a) for a in command],
                                          text=True, capture_output=True, timeout=2)
                assert rejected.returncode != 0 and time.monotonic() - started < 2
                assert "busy" in rejected.stderr
            fcntl.flock(locked, fcntl.LOCK_UN)
        assert dump(path)[0]["state"] == 0
        for value in ["0", "45001", "-1", "x", "1x", "", " 1", "+1", "9999999999999999999999"]:
            run([tool, "arm", path, value], ok=False)
        assert dump(path)[0]["state"] == 0
        run([tool, "arm", path, "45000"])
        run([tool, "verify-fresh", path, EXE, TOKEN], ok=False)
        run([tool, "arm", path, "1"], ok=False)
        f.command("lifetimes")
        f.command("socket")
        f.command("errno")
        h, events = dump(path)
        assert h["state"] == 1 and h["enabled"] and h["windows_pid"] == 4242
        assert any(e["native_errno"] != 0 and e["returned"] == -1 for e in events)
        run([tool, "stop", path])
        run([tool, "verify-fresh", path, EXE, TOKEN], ok=False)
        stopped = dump(path)[0]["reserved"]
        f.command("socket")
        f.command("emit 4")
        f.command("errno")
        assert dump(path)[0]["reserved"] == stopped
        run([tool, "stop", path])
        run([tool, "arm", path, "1"], ok=False)
        f.close()
        checks.append("private zero-prefaulted initialization, no overwrite, exact claim, duration bounds and one-shot stop")
        checks.append("observed/open/close/reused lifetimes, stale close guard, local socket error and errno preservation")
        checks.append("sanitized read-only fresh verification, exact binding and nonblocking arm/stop controller locking")

        # A byte in each control field and representative lifetime, executable
        # tail, header padding and record regions makes a file non-fresh.
        path = init()
        original = path.read_bytes()
        for offset in [20, 24, 32, 40, 48, 56, 64, 72, 76, 80, 84,
                       120 + len(EXE) + 1, 632, 640, 8816, 16383, 16384, SIZE - 1]:
            patch(path, offset, b"\x01")
            rejected = run([tool, "verify-fresh", path, EXE, TOKEN], ok=False)
            assert not rejected.stdout
            assert path.read_bytes()[offset] == 1  # verifier did not repair it
            patch(path, offset, original[offset:offset + 1])
        assert json.loads(run([tool, "verify-fresh", path, EXE, TOKEN]).stdout)["fresh"]
        assert path.read_bytes() == original
        checks.append("fresh verification rejects every control field, lifetime storage, executable tail, padding and record bytes")

        path = init()
        f = Fixture(native, path)
        run([tool, "arm", path, "45000"])
        f.command("metadata")
        h, records = dump(path)
        assert h["version"] == 1 and h["record_size"] == 128 and h["capacity"] == CAPACITY
        sockets = [e for e in records if e["api"] == 6 and e["phase"] == 7]
        assert [(e["family"], e["socket_type"], e["socket_protocol"]) for e in sockets] == [(2, 1, 6), (23, -1, -2)]
        assert all(e["flags"] & 64 and e["lifetime"] > 0 for e in sockets)
        groups = {}
        for e in records:
            if e["api"] in (11, 12): groups.setdefault(e["operation_id"], []).append(e)
        assert len(groups) == 5
        unknown = list(groups.values())[-1]
        assert [e["phase"] for e in unknown] == [1, 4]
        assert all(not e["wait_metadata_valid"] for e in unknown)
        assert (unknown[-1]["returned"], unknown[-1]["status"], unknown[-1]["error"]) == (-1, 0xc0000005, 10014)
        for group, expected in zip(groups.values(), [(11, -123450, "relative", 2, 2, False),
                                                      (12, 2**63 - 1, "infinite", 1, 1, False),
                                                      (11, 0, "immediate", 19, 16, False),
                                                      (12, -10000, "relative", 1, 1, True)]):
            api, timeout, kind, total, captured, invalid = expected
            summaries = [e for e in group if e["wait_record"] == "summary"]
            members = [e for e in group if e["wait_record"] == "member"]
            assert [e["phase"] for e in summaries] == [1, 4]
            assert [e["requested"] for e in summaries] == [total, total]
            assert summaries[-1]["returned"] == 1
            assert all(e["wait_input_mask"] is None and e["wait_output_mask"] is None and
                       not e["wait_input_mask_known"] and not e["wait_output_mask_known"] for e in summaries)
            assert len(members) == captured * 2
            for e in group:
                assert e["api"] == api and e["wait_timeout_100ns"] == timeout and e["wait_timeout_kind"] == kind
                assert (e["wait_total"], e["wait_captured"]) == (total, captured)
                assert e["wait_truncated"] == bool(e["flags"] & 32) == (captured < total)
            for before, after in zip(members[::2], members[1::2]):
                assert (before["phase"], after["phase"]) == (2, 5)
                assert before["lifetime"] == after["lifetime"]
                assert (before["lifetime"] > 0) == (not invalid)
                assert before["wait_mask_domain"] == after["wait_mask_domain"] == ("afd" if api == 11 else "windows_poll")
                assert before["wait_input_mask"] == after["wait_input_mask"] == (None if invalid else 5 if api == 11 else 0x110)
                assert before["wait_input_mask_known"] == after["wait_input_mask_known"] == (not invalid)
                if invalid:
                    assert before["requested"] == after["requested"] == 0
                    assert after["wait_output_mask"] == 4
                assert not before["wait_output_mask_known"] and before["wait_output_mask"] is None
                assert after["wait_output_mask_known"] == (after["status"] == 0)
                assert after["wait_output_mask"] == (after["returned"] if after["status"] == 0 else None)
                assert not before["wait_afd_status_known"] and before["wait_afd_status"] is None
                assert before["wait_wsa_error"] is None
                afd_known = after["wait_output_mask_known"] and not invalid and after["returned"] != 0
                assert after["wait_afd_status_known"] == afd_known
                assert after["wait_afd_status"] == (after["status"] if afd_known else None)
                assert after["wait_wsa_error"] == after["error"] == 0
        run([tool, "stop", path])
        f.close()
        checks.append("encoded socket type/protocol and wait summary/member decoding: signed native timeout, infinite, truncation, mask domains and lifetimes")

        # A record encoded using the original v1 layout must still dump unchanged
        # generic values; no new socket/wait annotation is inferred for it.
        path = init()
        legacy = struct.pack("<6QqQ4I2i4I16s2I", 1, 123, 1, 2, 987654321, 25, 17, 99,
                             4, 4, 0, 0, -1, 0, 2, 443, 0, 0,
                             bytes([127, 0, 0, 1]) + bytes(12), 9999, 77)
        assert len(legacy) == 128
        patch(path, 16384, legacy)
        patch(path, 40, struct.pack("<Q", 1))
        h, records = dump(path)
        assert len(records) == 1
        e = records[0]
        assert (e["api"], e["phase"], e["requested"], e["returned"], e["family"], e["port"]) == (4, 4, 25, 17, 2, 443)
        assert e["address_hex"] == "7f000001" + "00" * 12
        assert "socket_type" not in e and "wait_record" not in e
        checks.append("original v1 binary record and numeric serializer remain compatible at 128 bytes and 16384 capacity")

        path = init()
        f = Fixture(native, path)
        run([tool, "arm", path, "1"])
        time.sleep(0.03)
        assert not f.command("socket")["active"]
        f.command("errno")
        assert dump(path)[0]["state"] == 3 and dump(path)[0]["count"] == 0
        run([tool, "arm", path, "100"], ok=False)
        f.close()
        checks.append("monotonic 1ms expiry, permanent no-rearm, stop/expiry leave existing socketpair usable")

        path = init()
        run([tool, "stop", path])
        f = Fixture(native, path)
        run([tool, "arm", path, "100"], ok=False)
        f.command("emit 2")
        assert dump(path)[0]["count"] == 0
        f.close()

        f = Fixture(native, claimed=False)
        f.command("errno")
        f.command("socket")
        f.close()
        path = init()
        for token in ["f" * 32, "F" * 32, "short"]:
            f = Fixture(native, path, token=token, claimed=False)
            f.command("errno")
            f.close()
        f = Fixture(native, str(path).removeprefix("/"), claimed=False)
        f.close()
        wrong = init(r"C:\fixture\other.exe")
        f = Fixture(native, wrong, claimed=False)
        f.close()
        # Cached initialization must not become opt-in later; every rejection uses a new process.
        assert dump(path)[0]["host_pid"] == 0
        checks.append("fresh-process no-opt-in, wrong executable/token, uppercase/short token and relative path refusal")

        corruptions = [(0, bytes(8)), (8, struct.pack("<I", 2)), (12, struct.pack("<I", 64)),
                       (16, struct.pack("<I", CAPACITY - 1)), (120, b"x" * 512)]
        for offset, value in corruptions:
            bad = init()
            patch(bad, offset, value)
            f = Fixture(native, bad, claimed=False)
            f.command("errno")
            f.close()
            run([tool, "dump", bad], ok=False)
        for mutation in ["filemode", "dirmode", "short", "hardlink", "symlink", "dirlink"]:
            bad = init()
            if mutation == "filemode": bad.chmod(0o644)
            elif mutation == "dirmode": bad.parent.chmod(0o755)
            elif mutation == "short":
                with bad.open("r+b") as stream: stream.truncate(128)
            elif mutation == "hardlink": os.link(bad, bad.parent / "duplicate")
            elif mutation == "symlink":
                original = bad.with_name("original")
                bad.rename(original)
                bad.symlink_to(original)
            else:
                alias = tmp / f"alias-{sequence}"
                alias.symlink_to(bad.parent, target_is_directory=True)
                bad = alias / "netdiag.bin"
            f = Fixture(native, bad, claimed=False)
            f.command("socket")
            f.close()
            run([tool, "dump", bad], ok=False)
        checks.append("malformed ABI, unterminated executable, file size/permissions, hardlinks and final symlinks refused")

        path = init()
        f = Fixture(native, path)
        rival = Fixture(native, path, claimed=False)
        rival.command("emit 1")
        rival.close()
        run([tool, "arm", path, "45000"])
        f.p.stdin.write("concurrent\n")
        f.p.stdin.flush()
        dump(path)  # A snapshot while writers may be active must be self-consistent.
        assert response(f.p)["ok"]
        h, events = dump(path)
        assert h["count"] > 0 and h["reserved"] + h["dropped"] == 4000
        f.command("emit 9000")
        h, events = dump(path)
        assert h["reserved"] == CAPACITY and h["count"] == CAPACITY
        assert h["dropped"] >= 4000 + 18000 - CAPACITY
        run([tool, "stop", path])
        f.close()
        checks.append("exclusive process claim, concurrent commit snapshots, bounded saturation/drop accounting")

        # Dump skips a reserved slot without its release-published sequence.
        path = init()
        patch(path, 40, struct.pack("<Q", 1))
        assert dump(path)[0]["count"] == 0
        checks.append("uncommitted reserved slot is never exposed")
    print(json.dumps({"passed": checks, "scope": "native headers/controller only; no Wine or gameplay acceptance"}, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Experimental, focus-free host writer for the VibeWine virtual XInput protocol.

This program neither launches a game nor synthesizes native keyboard/mouse input.
A successful command requires a matching receipt produced by the game's poll.
"""
from __future__ import annotations

import argparse
import contextlib
import fcntl
import json
import os
from pathlib import Path
import re
import secrets
import stat
import struct
import sys
import time

MAGIC = b"VXIP0001"
ACK_MAGIC = "VXIA0001"
SNAPSHOT = struct.Struct("<8s32sQIHBBhhhhII")
ARM, RELEASE, STATE = 1, 2, 0
BUTTON_MASK = 0xf7ff  # Defined XInput buttons, including GUIDE; 0x0800 is reserved.
MANIFEST = "session.json"
STATE_FILE = "state.bin"
ACK_FILE = STATE_FILE + ".ack"
MAX_SEQ = (1 << 64) - 1
STATUSES = {"READY", "STATE", "RELEASE", "EXPIRED", "INVALID", "DISABLED"}
TOKEN_RE = re.compile(r"[0-9a-f]{32}\Z")
UINT_RE = re.compile(r"(?:0|[1-9][0-9]*)\Z")
WINDOWS_PATH = re.compile(r"(?:[A-Za-z]:\\|\\\\[^\\]+\\[^\\]+\\).+")


class ControllerError(Exception):
    pass


def integer(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise ControllerError(f"{field} must be an integer in {low}..{high}")
    return value


def windows_path(value, field):
    if not isinstance(value, str) or not WINDOWS_PATH.fullmatch(value) or any(ord(c) < 32 for c in value):
        raise ControllerError(f"{field} must be an absolute Win32 path without control characters")
    return value


def encode_snapshot(token, seq, lease_ms, buttons=0, lt=0, rt=0,
                    lx=0, ly=0, rx=0, ry=0, pid=0, flags=STATE):
    if not isinstance(token, str) or not TOKEN_RE.fullmatch(token):
        raise ControllerError("token must contain exactly 32 lowercase hexadecimal characters")
    integer(seq, 1, MAX_SEQ, "sequence")
    integer(lease_ms, 1, 1000, "lease_ms")
    integer(buttons, 0, 65535, "buttons")
    if buttons & ~BUTTON_MASK:
        raise ControllerError("buttons contains reserved XInput bits")
    integer(lt, 0, 255, "lt")
    integer(rt, 0, 255, "rt")
    for name, value in (("lx", lx), ("ly", ly), ("rx", rx), ("ry", ry)):
        integer(value, -32768, 32767, name)
    integer(pid, 0, 0xffffffff, "pid")
    integer(flags, 0, 2, "flags")
    if flags in (ARM, RELEASE) and any((buttons, lt, rt, lx, ly, rx, ry)):
        raise ControllerError("ARM and RELEASE must have a neutral gamepad")
    if flags != ARM and pid == 0:
        raise ControllerError("STATE and RELEASE require a bound nonzero Windows PID")
    return SNAPSHOT.pack(MAGIC, token.encode("ascii"), seq, lease_ms,
                         buttons, lt, rt, lx, ly, rx, ry, pid, flags)


def parse_ack(raw):
    try:
        fields = raw.decode("ascii").split()
    except UnicodeError as exc:
        raise ControllerError("receipt is not ASCII") from exc
    if len(fields) != 7 or fields[0] != ACK_MAGIC or not TOKEN_RE.fullmatch(fields[1]):
        raise ControllerError("malformed receipt header")
    if not all(UINT_RE.fullmatch(x) for x in fields[2:6]):
        raise ControllerError("malformed receipt integers")
    pid, seq, polls, enabled = map(int, fields[2:6])
    integer(pid, 1, 0xffffffff, "receipt PID")
    integer(seq, 1, MAX_SEQ, "receipt sequence")
    integer(polls, 1, MAX_SEQ, "receipt poll count")
    integer(enabled, 0, 1, "receipt enabled")
    if fields[6] not in STATUSES:
        raise ControllerError("unknown receipt status")
    return dict(token=fields[1], pid=pid, seq=seq, polls=polls,
                enabled=bool(enabled), status=fields[6])


def open_directory(path):
    """Walk every component without following symlinks, including parents."""
    absolute = os.path.abspath(os.fspath(path))
    parts = Path(absolute).parts
    fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in parts[1:]:
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = next_fd
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
            raise ControllerError("session directory must be owned by this user with mode 0700")
        return fd
    except BaseException:
        os.close(fd)
        raise


def check_file(fd, name, private=False):
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
        raise ControllerError(f"{name} must be a regular, singly linked file owned by this user")
    if private and stat.S_IMODE(info.st_mode) != 0o600:
        raise ControllerError(f"{name} must have mode 0600")
    return info


def read_file(directory, name, limit=8192, private=False):
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    try:
        info = check_file(fd, name, private)
        if info.st_size > limit:
            raise ControllerError(f"{name} exceeds its size limit")
        raw = os.read(fd, limit + 1)
        if len(raw) > limit:
            raise ControllerError(f"{name} exceeds its size limit")
        return raw
    finally:
        os.close(fd)


def atomic_write(directory, name, raw):
    # Reject existing links/non-private files; never open a replaced target to write.
    try:
        existing = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
    except FileNotFoundError:
        pass
    else:
        try:
            check_file(existing, name, private=True)
        finally:
            os.close(existing)
    temporary = ".write-" + secrets.token_hex(16)
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                 0o600, dir_fd=directory)
    try:
        os.fchmod(fd, 0o600)
        stream = os.fdopen(fd, "wb")
        fd = None  # stream now owns it, including on write failure.
        with stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, name, src_dir_fd=directory, dst_dir_fd=directory)
        os.fsync(directory)
    finally:
        if fd is not None:
            # fdopen owns the descriptor once entered, including on write failure.
            with contextlib.suppress(OSError):
                os.close(fd)
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary, dir_fd=directory)


def save_manifest(directory, manifest):
    atomic_write(directory, MANIFEST,
                 (json.dumps(manifest, sort_keys=True) + "\n").encode("utf-8"))


def load_manifest(directory):
    try:
        manifest = json.loads(read_file(directory, MANIFEST, private=True))
    except (ValueError, UnicodeError) as exc:
        raise ControllerError("invalid session manifest") from exc
    if not isinstance(manifest, dict) or manifest.get("version") != 1:
        raise ControllerError("unsupported session manifest")
    if not isinstance(manifest.get("token"), str) or not TOKEN_RE.fullmatch(manifest["token"]):
        raise ControllerError("invalid manifest token")
    windows_path(manifest.get("exe"), "manifest executable")
    windows_path(manifest.get("wine_file"), "manifest snapshot")
    integer(manifest.get("seq"), 0, MAX_SEQ, "manifest sequence")
    integer(manifest.get("pid"), 0, 0xffffffff, "manifest PID")
    integer(manifest.get("polls"), 0, MAX_SEQ, "manifest poll count")
    if type(manifest.get("armed")) is not bool:
        raise ControllerError("invalid manifest armed state")
    return manifest


@contextlib.contextmanager
def locked_session(path):
    directory = open_directory(path)
    lock = None
    try:
        lock = os.open(".lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK,
                       0o600, dir_fd=directory)
        check_file(lock, ".lock", private=True)
        # Do not queue commands behind another writer: an old command must not run late.
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise ControllerError("another command owns this session") from exc
        yield directory, load_manifest(directory)
    finally:
        if lock is not None:
            os.close(lock)
        os.close(directory)


def initialize(path, exe, wine_file):
    windows_path(exe, "executable")
    windows_path(wine_file, "snapshot")
    absolute = os.path.abspath(os.fspath(path))
    parent = Path(absolute).parent
    # Validate parent components without imposing session-specific ownership/mode.
    parent_fd = os.open("/", os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in parent.parts[1:]:
            next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent_fd)
            os.close(parent_fd)
            parent_fd = next_fd
        os.mkdir(Path(absolute).name, mode=0o700, dir_fd=parent_fd)
    finally:
        os.close(parent_fd)
    directory = open_directory(absolute)
    try:
        manifest = dict(version=1, token=secrets.token_hex(16), exe=exe,
                        wine_file=wine_file, seq=0, pid=0, polls=0, armed=False)
        save_manifest(directory, manifest)
    finally:
        os.close(directory)
    return dict(directory=absolute, snapshot=os.path.join(absolute, STATE_FILE),
                environment={"VIBEWINE_XINPUT_TEST_FILE": wine_file,
                             "VIBEWINE_XINPUT_TEST_TOKEN": manifest["token"],
                             "VIBEWINE_XINPUT_TEST_EXE": exe})


def receipt_for(directory, manifest, seq=None, minimum_polls=None):
    ack = parse_ack(read_file(directory, ACK_FILE, limit=512))
    if ack["token"] != manifest["token"]:
        raise ControllerError("receipt belongs to another token")
    if manifest["pid"] and ack["pid"] != manifest["pid"]:
        raise ControllerError("receipt belongs to another Windows PID")
    if seq is not None and ack["seq"] != seq:
        raise ControllerError("receipt sequence does not match this command")
    if minimum_polls is not None and ack["polls"] <= minimum_polls:
        raise ControllerError("receipt poll count did not advance")
    return ack


def publish(directory, manifest, flags, lease_ms=500, **gamepad):
    if manifest["seq"] == MAX_SEQ:
        raise ControllerError("sequence exhausted; initialize a new session")
    seq = manifest["seq"] + 1
    data = encode_snapshot(manifest["token"], seq, lease_ms,
                           pid=manifest["pid"], flags=flags, **gamepad)
    # Persist the sequence before publication, so a crash cannot reuse it.
    manifest["seq"] = seq
    manifest["armed"] = False
    save_manifest(directory, manifest)
    atomic_write(directory, STATE_FILE, data)
    return seq


def _command_locked(directory, manifest, flags, lease_ms=500, timeout_ms=1500,
                    *, not_after=None, fresh_lease=False, cleanup_on_failure=True, **gamepad):
    if flags == STATE and not manifest["armed"]:
        raise ControllerError("a fresh acknowledged neutral ARM is required")
    if flags == RELEASE and not manifest["pid"]:
        raise ControllerError("no acknowledged Windows PID is bound")
    baseline = manifest["polls"]
    # A timed-out first ARM may have bound the producer after our deadline.
    # Recover only its identity from a receipt for exactly our last sequence;
    # a new command still requires a newer sequence and an advancing poll.
    if flags == ARM and not manifest["pid"] and manifest["seq"]:
        try:
            previous_arm = receipt_for(directory, manifest, seq=manifest["seq"])
        except (ControllerError, OSError):
            pass
        else:
            manifest["pid"] = previous_arm["pid"]
            baseline = max(baseline, previous_arm["polls"])
    # Receipt count can advance between CLI invocations; observe its latest value.
    try:
        previous = receipt_for(directory, manifest)
        baseline = max(baseline, previous["polls"])
    except (ControllerError, OSError):
        pass
    started = time.monotonic()
    if not_after is not None and started >= not_after:
        raise ControllerError("command deadline elapsed before publication")
    seq = publish(directory, manifest, flags, lease_ms, **gamepad)
    deadline = started + timeout_ms / 1000
    if not_after is not None:
        deadline = min(deadline, not_after)
    if fresh_lease:
        deadline = min(deadline, started + lease_ms / 1000)
    expected = {ARM: "READY", STATE: "STATE", RELEASE: "RELEASE"}[flags]
    error = "no matching poll receipt"
    while time.monotonic() < deadline:
        try:
            ack = receipt_for(directory, manifest, seq, baseline)
            if ack["status"] != expected or (flags != RELEASE and not ack["enabled"]):
                error = f"game poll reported {ack['status']} (enabled={ack['enabled']})"
                break
            if time.monotonic() >= deadline:
                error = "matching receipt arrived after the command deadline"
                break
            manifest.update(pid=ack["pid"], polls=ack["polls"], armed=(flags != RELEASE))
            save_manifest(directory, manifest)
            return (dict(receipt=ack, lease_ms=lease_ms,
                         note="Poll receipt only; inspect the game image to establish an action."), started)
        except (ControllerError, OSError) as exc:
            error = str(exc)
        time.sleep(min(0.01, max(0, deadline - time.monotonic())))
    # Neutral ARM itself is safe. Failed active/release commands get a newer
    # neutral RELEASE when bound; independent producer expiry remains mandatory.
    if cleanup_on_failure and flags != ARM and manifest["pid"]:
        publish(directory, manifest, RELEASE, lease_ms=1)
    raise ControllerError(f"command unacknowledged: {error}; session disarmed, neutral ARM required")


def command(path, flags, lease_ms=500, timeout_ms=1500, **gamepad):
    integer(timeout_ms, 1, 5000, "timeout_ms")
    with locked_session(path) as (directory, manifest):
        result, _ = _command_locked(directory, manifest, flags, lease_ms, timeout_ms, **gamepad)
        return result


def pulse(path, hold_ms=120, timeout_ms=2000, **gamepad):
    """ARM, one bounded state, then neutral RELEASE under one writer lock.

    The active state lease is the hold budget itself, capped at 500 ms. The
    budget starts before publishing, so receipt latency never adds a full hold
    interval. The reader must independently expire input if this writer dies.
    """
    integer(hold_ms, 1, 500, "hold_ms")
    integer(timeout_ms, 1, 5000, "timeout_ms")
    # Reject invalid control arguments before even publishing a neutral ARM.
    encode_snapshot("0" * 32, 1, hold_ms, pid=1, flags=STATE, **gamepad)
    deadline = time.monotonic() + timeout_ms / 1000
    receipts = {}
    failure = release_failure = None
    with locked_session(path) as (directory, manifest):
        if manifest["seq"] > MAX_SEQ - 4:
            raise ControllerError("insufficient sequences for pulse and neutral cleanup; initialize a new session")
        try:
            receipts["arm"], arm_started = _command_locked(
                directory, manifest, ARM, 1000, 500,
                not_after=deadline, fresh_lease=True, cleanup_on_failure=False)
            if time.monotonic() >= arm_started + 0.9:
                raise ControllerError("ARM lease guard elapsed before active state")
            if time.monotonic() + hold_ms / 1000 + 0.05 >= deadline:
                raise ControllerError("insufficient pulse deadline for state hold and release receipt")
            receipts["state"], state_started = _command_locked(
                directory, manifest, STATE, hold_ms, hold_ms,
                not_after=min(deadline, arm_started + 0.9), fresh_lease=True,
                cleanup_on_failure=False, **gamepad)
            hold_until = min(state_started + hold_ms / 1000, deadline)
            remaining = hold_until - time.monotonic()
            if remaining <= 0:
                raise ControllerError("state hold budget elapsed before its receipt completed")
            time.sleep(remaining)
        except BaseException as exc:
            failure = exc
        finally:
            if manifest["pid"]:
                try:
                    if time.monotonic() >= deadline:
                        # Even after the wait budget expires, publish one neutral
                        # release. Never report it as acknowledged without a poll.
                        publish(directory, manifest, RELEASE, lease_ms=1)
                        raise ControllerError("deadline elapsed; neutral RELEASE published without waiting for receipt")
                    receipts["release"], _ = _command_locked(
                        directory, manifest, RELEASE, 1, 500,
                        not_after=deadline, cleanup_on_failure=True)
                except BaseException as exc:
                    release_failure = exc
            else:
                release_failure = ControllerError("no acknowledged PID for RELEASE; ARM was neutral")
    if failure is not None:
        if not isinstance(failure, (ControllerError, OSError)):
            raise failure
        detail = f"pulse failed: {failure}"
        if release_failure is not None:
            detail += f"; release unacknowledged: {release_failure}"
        raise ControllerError(detail) from failure
    if release_failure is not None:
        if not isinstance(release_failure, (ControllerError, OSError)):
            raise release_failure
        raise ControllerError(f"pulse release unacknowledged: {release_failure}") from release_failure
    return dict(receipts=receipts, hold_ms=hold_ms,
                note="Poll receipts only; inspect the game image to establish an action.")


def status(path):
    with locked_session(path) as (directory, manifest):
        result = dict(pid=manifest["pid"], sequence=manifest["seq"], armed=False,
                      last_command_acknowledged=manifest["armed"], live_lease_unknown=True)
        try:
            ack = receipt_for(directory, manifest, seq=manifest["seq"])
        except (ControllerError, OSError) as exc:
            result.update(receipt=None, receipt_error=str(exc), last_command_acknowledged=False)
        else:
            # A historical receipt cannot prove the producer's lease is still live.
            result.update(receipt=ack, historical_only=True)
            result["receipt"].pop("token")
            if ack["status"] in {"RELEASE", "EXPIRED", "INVALID", "DISABLED"} or not ack["enabled"]:
                result["last_command_acknowledged"] = False
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True, help="private session directory; symlinks rejected")
    sub = parser.add_subparsers(dest="action", required=True)
    init = sub.add_parser("init", help="create a new private session; does not launch anything")
    init.add_argument("--exe", required=True, help="exact absolute Windows executable path")
    init.add_argument("--wine-file", required=True, help="absolute Win32 path to this session's state.bin")
    sub.add_parser("status", help="read identity-bound historical receipt; sends no input")
    for name in ("arm", "state", "release", "pulse"):
        child = sub.add_parser(name)
        if name == "pulse":
            child.add_argument("--hold-ms", type=int, default=120, help="active state lease/hold budget, 1..500 ms")
            child.add_argument("--timeout-ms", type=int, default=2000, help="whole pulse wait budget, 1..5000 ms")
        else:
            child.add_argument("--lease-ms", type=int, default=500)
            child.add_argument("--timeout-ms", type=int, default=1500)
        if name in ("state", "pulse"):
            child.add_argument("--buttons", type=lambda value: int(value, 0), default=0)
            for axis in ("lt", "rt", "lx", "ly", "rx", "ry"):
                child.add_argument("--" + axis, type=int, default=0)
    args = vars(parser.parse_args(argv))
    action, directory = args.pop("action"), args.pop("directory")
    try:
        if action == "init":
            result = initialize(directory, **args)
        elif action == "status":
            result = status(directory)
        elif action == "pulse":
            result = pulse(directory, **args)
        else:
            result = command(directory, {"arm": ARM, "state": STATE, "release": RELEASE}[action], **args)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (ControllerError, OSError) as exc:
        print(json.dumps(dict(error=str(exc))), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

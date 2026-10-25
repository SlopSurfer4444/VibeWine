#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile actual Wine address-wait and Unix alert functions at native seams."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def function(source, name):
    # Locate definitions, not declarations or calls. These source functions do
    # not contain brace literals; fail closed if their signatures move.
    import re
    match = re.search(r"^(?:NTSTATUS WINAPI|static LONGLONG) " + name + r"\([^;]*?\)\n\{", source, re.M)
    if not match:
        raise RuntimeError(f"cannot find production function {name}")
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def check_result(result, backend, expect_regression):
    lines = [
        "controls mismatch/timeout sizes=1,2,4,8 invalid-size empty-wake raw-alert PASS",
        "controls alert coalescing one retrieval then timeout PASS",
        "ordinary single wake success, no second infinite wait PASS",
        "ordinary all wake success, no second infinite wait PASS",
    ]
    for scenario in ("deferred", "pending", "cleanup-lock-race"):
        for wake in ("single", "all"):
            if expect_regression:
                lines.append(f"{wake}/{scenario} first=0x102 unrelated=0 pending=0 drain_syscalls=0 REGRESSION")
            else:
                drains = int(scenario != "pending" or backend == "kqueue")
                lines.append(f"{wake}/{scenario} first=0 unrelated=0x102 pending=0 drain_syscalls={drains} PASS")
    if expect_regression:
        expected_status, expected_error = 42, "ADDRESS_WAIT_REGRESSION cases=6\n"
    else:
        lines.append("drain backend failure propagates STATUS_INVALID_CID PASS")
        if backend == "kqueue":
            lines.append("drain native kevent failure propagates STATUS_INVALID_HANDLE PASS")
        lines.append("ADDRESS_WAIT_PASS")
        expected_status, expected_error = 0, ""
    # Require the entire diagnostic, not a substring: recoverable UBSan errors,
    # ASan reports, extra failures or unexpected stdout must never be admitted
    # as a successful expected-baseline regression (or as a current-source pass).
    if (result.returncode != expected_status or result.stderr != expected_error
            or result.stdout != "\n".join(lines) + "\n"):
        raise SystemExit(f"unexpected {backend} fixture result: exit={result.returncode}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read both sync.c files from this component Git revision")
    parser.add_argument("--expect-regression", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--arch", choices=("arm64", "x86_64"), help="Darwin compiler architecture")
    parser.add_argument("--backend", choices=("futex", "kqueue", "all"),
                        default="all" if sys.platform == "darwin" else "futex",
                        help="Darwin defaults to both production branches; kqueue uses native syscalls")
    args = parser.parse_args()
    if args.backend in ("kqueue", "all") and sys.platform != "darwin":
        parser.error("native kqueue fixture requires Darwin; select --backend futex")
    backends = ("futex", "kqueue") if args.backend == "all" else (args.backend,)
    here = Path(__file__).resolve().parent
    root = here.parents[2]

    def read(path):
        if args.revision:
            return subprocess.check_output(["git", "show", f"{args.revision}:{path}"], cwd=root, text=True)
        return (root / path).read_text()

    address_source = read("dlls/ntdll/sync.c")
    unix_source = read("dlls/ntdll/unix/sync.c")
    start = address_source.index("struct futex_entry\n{")
    end = address_source.index("/*************************************************************************\n *           RtlInitializeSListHead", start)
    address = address_source[start:end]
    unix = "\n\n".join(function(unix_source, name) for name in (
        "NtAlertThreadByThreadId", "NtAlertMultipleThreadByThreadId",
        "get_absolute_timeout", "update_timeout", "NtWaitForAlertByThreadId"))
    fixture = (here / "fixture.c.in").read_text()
    fixture = fixture.replace("/* PRODUCTION_UNIX */", unix).replace("/* PRODUCTION_ADDRESS */", address)
    identity = hashlib.sha256((address + unix).encode()).hexdigest()
    print(f"production SHA256={identity} source={args.revision or 'working-tree'}", flush=True)
    with tempfile.TemporaryDirectory(prefix="vibewine-address-wait-") as scratch:
        source = Path(scratch) / "fixture.c"
        source.write_text(fixture)
        for backend in backends:
            binary = Path(scratch) / f"fixture-{backend}"
            command = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread", "-O2",
                       "-I", str(root / "include"), str(source), "-o", str(binary)]
            if args.arch:
                command += ["-arch", args.arch]
            if backend == "kqueue":
                command.append("-DTEST_KQUEUE")
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, check=True, timeout=60)
            print(f"backend={backend}", flush=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            print(result.stdout, end="")
            print(result.stderr, end="")
            check_result(result, backend, args.expect_regression)
            if args.expect_regression:
                print("EXPECTED_BASELINE_REGRESSION_CONFIRMED")


if __name__ == "__main__":
    main()

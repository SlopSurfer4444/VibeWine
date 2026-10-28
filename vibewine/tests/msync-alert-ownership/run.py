#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile production alert acquisition and terminal paths against real POSIX fds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name, last=False):
    matches = list(re.finditer(r"(?m)^(?:static[^;\n]*|void|int)\b[^;\n]*\b"
                              + re.escape(name) + r"\s*\(", source))
    if not matches:
        raise RuntimeError("missing production function: " + name)
    match = matches[-1] if last else matches[0]
    start = source.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read production source from Git without changing checkout")
    parser.add_argument("--expect-regression", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--arch", choices=("arm64", "x86_64"))
    parser.add_argument("--non-apple", action="store_true", help="use actual non-Apple do_msync stub")
    parser.add_argument("--repetitions", type=int, default=4)
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 32:
        parser.error("--repetitions must be 1..32")
    if args.expect_regression and not args.revision:
        parser.error("--expect-regression requires --revision")
    here = Path(__file__).resolve().parent
    root = here.parents[2]

    def read(path):
        if args.revision:
            return subprocess.check_output(["git", "show", f"{args.revision}:{path}"],
                                           cwd=root, text=True, timeout=15)
        return (root / path).read_text()

    paths = ("dlls/ntdll/unix/thread.c", "dlls/ntdll/unix/sync.c", "dlls/ntdll/unix/msync.c")
    thread, sync, msync = [read(path) for path in paths]
    generated = here.joinpath("fixture.c.in").read_text()
    for name in ("get_unix_exit_code", "block_exit_signals", "pthread_exit_wrapper", "abort_thread",
                 "abort_process", "exit_process", "exit_thread"):
        generated = generated.replace("@" + name.upper() + "@", function(thread, name))
    generated = generated.replace("@GET_INPROC_ALERT_FD@", function(sync, "get_inproc_alert_fd"))
    generated = generated.replace("@DO_MSYNC@", function(msync, "do_msync", last=args.non_apple))
    # Native pthreads have no Wine GS/TEB; all fd syscalls and ownership branches are unchanged.
    generated = generated.replace("#if defined(__APPLE__) && defined(__x86_64__)", "#if 0 /* native pthread has no Wine TEB */")
    if re.search(r"@[A-Z_]+@", generated):
        raise RuntimeError("unexpanded production body")
    failures, runs = [], []
    with tempfile.TemporaryDirectory(prefix="vibewine-msync-alert-") as directory:
        source, binary = Path(directory) / "fixture.c", Path(directory) / "fixture"
        source.write_text(generated)
        command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread",
                   str(source), "-o", str(binary)]
        if args.arch:
            command += ["-arch", args.arch]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        for backend in (("fd",) if args.non_apple else ("msync", "fd")):
            for kind in ("normal", "abort"):
                for case in ("matching", "nonmatching", "unused", "failed-request"):
                    for repetition in range(args.repetitions):
                        env = dict(os.environ, WINEMSYNC="1" if backend == "msync" else "0")
                        result = subprocess.run([str(binary), kind, case], capture_output=True, text=True,
                                                env=env, timeout=5)
                        expected = int(args.expect_regression and backend == "msync" and case == "matching")
                        okay = result.returncode == expected and not result.stderr and len(result.stdout) < 1024
                        okay &= bool(re.fullmatch(r"backend=" + backend + r" kind=" + kind + r" case=" + case
                                                 + r" alert=-?\d+ sentinel_valid=" + str(1 - expected) + r"\n",
                                                 result.stdout))
                        observation = {"backend": backend, "kind": kind, "case": case, "repetition": repetition,
                                       "exit": result.returncode, "observation": result.stdout.strip()}
                        runs.append(observation)
                        if not okay:
                            failures.append(dict(observation, stderr=result.stderr[:4096]))
    print(json.dumps({"source": args.revision or "working-tree", "arch": args.arch or "native",
                      "backend_implementation": "non-Apple stub" if args.non_apple else "Darwin",
                      "sanitized": args.sanitize,
                      "source_sha256": {path: hashlib.sha256(body.encode()).hexdigest()
                                        for path, body in zip(paths, (thread, sync, msync))},
                      "runs": runs, "failures": failures}, indent=2))
    if failures:
        raise SystemExit("MSYNC_ALERT_OWNERSHIP_FAIL")
    print("MSYNC_ALERT_OWNERSHIP_" + ("REGRESSION_REPRODUCED" if args.expect_regression else "PASS"))


if __name__ == "__main__":
    main()

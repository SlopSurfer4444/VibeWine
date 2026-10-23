#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile both production terminal thread paths and deliver real POSIX SIGQUIT."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def function(text, name):
    match = re.search(r"(?m)^(?:static[^;\n]*|void)\b[^;\n]*\b" + re.escape(name) + r"\s*\(", text)
    if not match:
        raise RuntimeError("missing production function: " + name)
    start = text.index("{", match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read production files from this component Git revision")
    parser.add_argument("--expect-regression", action="store_true")
    parser.add_argument("--kind", choices=("abort", "normal", "both"), default="both")
    parser.add_argument("--repetitions", type=int, default=8)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--cc", default="clang")
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 32:
        parser.error("--repetitions must be 1..32")
    if args.expect_regression and not args.revision:
        parser.error("--expect-regression requires --revision")
    root = Path(__file__).resolve().parents[3]

    def read(path):
        if args.revision:
            return subprocess.check_output(["git", "show", f"{args.revision}:{path}"],
                                           cwd=root, text=True, timeout=15)
        return (root / path).read_text()

    thread = read("dlls/ntdll/unix/thread.c")
    server = read("dlls/ntdll/unix/server.c")
    signal = read("dlls/ntdll/unix/signal_x86_64.c")
    # Assert the independently inspected signal edge still exists. Its machine
    # context setup is not executed by this host-native POSIX fixture.
    assert "abort_thread( 0 );" in function(signal, "quit_handler")
    mask = re.search(r"    sigemptyset\( &server_block_set \);\n(?:    sigaddset\( &server_block_set, \w+ \);\n)+", server)
    if not mask:
        raise RuntimeError("production server signal mask missing")
    generated = (Path(__file__).with_name("fixture.c.in").read_text()
                 .replace("/* PRODUCTION_UNIX_EXIT_CODE */", function(thread, "get_unix_exit_code"))
                 .replace("/* PRODUCTION_PTHREAD_EXIT_WRAPPER */", function(thread, "pthread_exit_wrapper"))
                 .replace("/* PRODUCTION_ABORT_THREAD */", function(thread, "abort_thread"))
                 .replace("/* PRODUCTION_ABORT_PROCESS */", function(thread, "abort_process"))
                 .replace("/* PRODUCTION_EXIT_PROCESS */", function(thread, "exit_process"))
                 .replace("/* PRODUCTION_EXIT_THREAD */", function(thread, "exit_thread"))
                 .replace("/* PRODUCTION_BLOCK_EXIT_SIGNALS */", function(thread, "block_exit_signals")
                          if re.search(r"static void block_exit_signals\s*\(", thread) else "")
                 .replace("/* PRODUCTION_READ_REPLY_DATA */", function(server, "read_reply_data"))
                 .replace("/* PRODUCTION_SERVER_BLOCK_SET */", mask.group(0)))
    # Windows x86_64's GS/TEB cleanup cannot run in a native POSIX host process.
    # The exact source keeps this preprocessor branch; the fixture disables only
    # that platform intrinsic, never the mask, decrement, close or pthread exit.
    generated = generated.replace("#if defined(__APPLE__) && defined(__x86_64__)", "#if 0 /* no Wine GS/TEB on native host */")
    failures, runs = [], []
    with tempfile.TemporaryDirectory(prefix="vibewine-thread-exit-") as directory:
        scratch = Path(directory)
        source, binary = scratch / "fixture.c", scratch / "fixture"
        source.write_text(generated)
        command = [args.cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-pthread", str(source), "-o", str(binary)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        built = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if built.returncode:
            raise SystemExit(built.stderr)
        kinds = ("abort", "normal") if args.kind == "both" else (args.kind,)
        for kind, injection in [(kind, injection) for kind in kinds for injection in range(7)]:
            for repetition in range(args.repetitions):
                result = subprocess.run([str(binary), str(injection), kind], capture_output=True, timeout=2)
                if len(result.stdout) % 16 or len(result.stdout) > 4096 or result.stderr:
                    raise SystemExit(f"invalid fixture output {injection}: {result.stderr!r}")
                events = list(struct.iter_unpack("=IIiI", result.stdout))
                decrements = [event for event in events if event[0] == 1]
                handlers = [event for event in events if event[0] == 3]
                closes = [event for event in events if event[0] == 2]
                expected_exit = 0 if args.expect_regression and 1 <= injection <= 5 else 20
                expected_decrements = [(1, 1, 1, 0), (1, 1, 0, 0)] if expected_exit == 0 else [(1, 1, 1, 0), (1, 0, 0, 0)]
                expected_handlers = int(expected_exit == 0 or injection == 6)
                expected_quit_mask = int(not args.expect_regression or injection == 6)
                okay = (result.returncode == expected_exit and decrements == expected_decrements
                        and len(handlers) == expected_handlers
                        and len(closes) == (injection if expected_exit == 0 else 5)
                        and (7, 1, expected_quit_mask, 0) in events)
                if expected_exit == 20:
                    okay &= ((6, 0, 0, 5) in events and (4, 0, 20, 0) in events
                             and (8, 0, 0, 0) in events)
                if not okay:
                    failures.append({"kind": kind, "injection": injection, "repetition": repetition,
                                     "returncode": result.returncode, "events": events})
                runs.append({"kind": kind, "close_boundary": injection, "repetition": repetition,
                             "exit": result.returncode, "signal_handlers": len(handlers)})
    print(json.dumps({"source": args.revision or "working-tree",
                      "thread_sha256": hashlib.sha256(thread.encode()).hexdigest(),
                      "server_sha256": hashlib.sha256(server.encode()).hexdigest(),
                      "signal_sha256": hashlib.sha256(signal.encode()).hexdigest(),
                      "sanitized": args.sanitize, "runs": runs, "failures": failures}, indent=2))
    if failures:
        raise SystemExit("THREAD_EXIT_FAIL")
    print("THREAD_EXIT_" + ("REGRESSION_REPRODUCED" if args.expect_regression else "PASS"))


if __name__ == "__main__":
    main()

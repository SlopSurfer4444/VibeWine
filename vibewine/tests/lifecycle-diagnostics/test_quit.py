#!/usr/bin/env python3
"""Exercise current production Quit functions natively; never launch Wine."""
from __future__ import annotations

import argparse
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FLAGS = 0x80000002


def fragment(source: str, first: str, last: str) -> str:
    assert source.count(first) == 1, first
    assert source.count(last) == 1, last
    start = source.index(first)
    end = source.index(last, start)
    return source[start:end]


def build(directory: Path, args: argparse.Namespace) -> tuple[Path, str]:
    source = (ROOT / "dlls/winemac.drv/dllmain.c").read_text()
    template = (HERE / "quit_fixture.c.in").read_text()
    pieces = {
        "QUOTA": fragment(source, "#define LIFECYCLE_EVENT_LIMIT", '#include "ax_provider.inc"'),
        "ENUM": fragment(source, "struct quit_info {", "#pragma pack(push,1)"),
        "METHODS": fragment(source, "static void quit_reply(int reply)",
                            "/***********************************************************************\n *              get_first_resource"),
    }
    for name, text in pieces.items():
        template = template.replace(f"@PRODUCTION_{name}@", text)
    assert not re.search(r"@PRODUCTION_\w+@", template)
    compiled_source = directory / "quit.c"
    executable = directory / "quit"
    compiled_source.write_text(template)
    compiler = shutil.which(args.cc)
    assert compiler, f"Compiler not found: {args.cc}"
    architecture_flags = ["-arch", args.arch] if args.arch else []
    macros = subprocess.run(
        [compiler, *architecture_flags, "-dM", "-E", "-x", "c", "-"],
        input="", check=True, capture_output=True, text=True, timeout=60,
    ).stdout
    if re.search(r"^#define (__aarch64__|__arm64__) 1$", macros, re.MULTILINE):
        effective_architecture = "arm64"
    elif re.search(r"^#define __x86_64__ 1$", macros, re.MULTILINE):
        effective_architecture = "x86_64"
    else:
        raise AssertionError("Compiler target is neither arm64 nor x86_64")
    if args.arch:
        assert effective_architecture == args.arch, (args.arch, effective_architecture)
    sanitizer_flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
    subprocess.run(
        [compiler, *architecture_flags, "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", *sanitizer_flags, "-g",
         str(compiled_source), "-o", str(executable)],
        check=True, capture_output=True, text=True, timeout=60,
    )
    return executable, effective_architecture


def execute(executable: Path, case: tuple[int, ...], enabled: int):
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    completed = subprocess.run(
        [str(executable), *map(str, case), str(enabled), str(FLAGS)],
        check=True, capture_output=True, text=True, timeout=10, env=environment,
    )
    assert not completed.stderr, completed.stderr
    lines = completed.stdout.splitlines()
    summary = json.loads(lines[-1])
    traces = []
    for line in lines[:-1]:
        assert line.startswith("TRACE "), line
        traces.append(dict(re.findall(r"(\w+)=([^ ]+)", line[6:])))
    assert summary["status"] == 0, (case, summary)
    assert summary["replies"] == 1, (case, summary)
    assert summary["allocations"] == 0, (case, summary)
    assert summary["foreign_messages"] == 0, (case, summary)
    assert summary["trace_lines"] == len(traces)
    if not enabled:
        assert not traces and summary["counter"] == 0, (case, summary)
    else:
        assert summary["counter"] == len(traces), (case, summary)
    return summary, traces


def validate(case: tuple[int, ...], summary: dict, traces: list[dict]):
    count, veto, _query_mode, _end_mode, send_fault, alloc_fault, enum_fault, _reverse = case
    failed = bool(alloc_fault or enum_fault)
    accepted = not failed and not veto and send_fault != 2
    assert summary["reply_value"] == int(accepted), (case, summary)
    assert summary["terminations"] == int(accepted), (case, summary)
    expected_count = 0 if failed else count
    assert summary["queries"] == summary["ends"] == expected_count, (case, summary)
    assert traces[0]["phase"] == "begin", (case, traces)
    assert sum(item["phase"] == "reply" for item in traces) == 1, (case, traces)
    for item in traces:
        assert item["event"] == "app_quit", (case, item)
        assert int(item["pid"], 16) == 0x1234 and int(item["tid"], 16) == 0x5678, item
        phase = item["phase"]
        assert phase in {"begin", "windows", "reply", "query_send", "query_callback",
                         "end_send", "end_callback"}, item
        message = int(item["msg"], 16)
        flags = int(item["flags"], 16)
        if phase == "reply":
            assert message == 0 and int(item["count"]) == int(item["done"]) == 0, item
            assert int(item["result"]) == int(accepted) and flags == 0, item
        else:
            assert flags == FLAGS, item
        if phase in {"query_send", "query_callback", "end_send", "end_callback"}:
            assert int(item["count"]) == count, item
            assert message == (0x11 if phase.startswith("query") else 0x16), item
            assert 1 <= int(item["hwnd"], 16) <= count, item
            assert 0 <= int(item["done"]) <= count, item
            if phase.endswith("callback"):
                assert int(item["done"]) >= 1, item
            if phase == "query_send" or phase == "end_callback":
                assert int(item["result"]) == 0, item
            if phase == "end_send":
                assert int(item["result"]) == int(accepted), item
            if phase == "query_callback":
                window = int(item["hwnd"], 16)
                expected = window != veto
                if window == count and send_fault in {1, 2}:
                    expected = send_fault == 1
                assert int(item["result"]) == int(expected), (case, item)
        elif phase in {"begin", "windows"}:
            assert message == 0 and int(item["result"]) == 0, item
            assert int(item["count"]) == (count if phase == "windows" else 0), item
    phases = [item["phase"] for item in traces]
    assert phases.count("windows") == int(not failed), (case, traces)
    for phase in ("query_send", "query_callback", "end_send", "end_callback"):
        assert phases.count(phase) == expected_count, (case, traces)
        events = [item for item in traces if item["phase"] == phase]
        assert sorted(int(item["hwnd"], 16) for item in events) == list(range(1, expected_count + 1))
        if phase.endswith("callback"):
            assert [int(item["done"]) for item in events] == list(range(1, expected_count + 1))
    for window in range(1, expected_count + 1):
        positions = {
            phase: next(index for index, item in enumerate(traces)
                        if item["phase"] == phase and int(item["hwnd"], 16) == window)
            for phase in ("query_send", "query_callback", "end_send", "end_callback")
        }
        assert positions["query_send"] < positions["query_callback"] < positions["end_send"]
        assert positions["end_send"] < positions["end_callback"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="/usr/bin/clang")
    parser.add_argument("--arch", choices=("arm64", "x86_64"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    cases = [(0, 0, 0, 0, 0, 0, 0, 0)]
    for query_mode, end_mode, reverse, veto in itertools.product(range(3), range(3), range(2), (0, 2)):
        cases.append((3, veto, query_mode, end_mode, 0, 0, 0, reverse))
    for send_fault, mode, reverse in itertools.product((1, 2, 3), range(3), range(2)):
        cases.append((3, 0, mode, mode, send_fault, 0, 0, reverse))
    # First/second allocation, window-list growth, and enumeration failures.
    cases.extend([(3, 0, 1, 1, 0, 1, 0, 0), (3, 0, 1, 1, 0, 2, 0, 0),
                  (33, 0, 1, 1, 0, 3, 0, 0), (3, 0, 1, 1, 0, 0, 1, 0),
                  (33, 0, 2, 2, 0, 0, 0, 1)])
    with tempfile.TemporaryDirectory(prefix="vw-lifecycle-quit-") as temporary:
        executable, architecture = build(Path(temporary), args)
        for case in cases:
            on, traces = execute(executable, case, 1)
            validate(case, on, traces)
            off, _ = execute(executable, case, 0)
            assert {key: value for key, value in on.items() if key not in {"counter", "trace_lines"}} == {
                key: value for key, value in off.items() if key not in {"counter", "trace_lines"}
            }, (case, on, off)
    print(json.dumps({"suite": "quit", "cases": len(cases), "channel_runs": len(cases) * 2,
                      "architecture": architecture, "compiler": args.cc,
                      "sanitizers": "address,undefined" if args.sanitize else "none", "result": "PASS"}))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as failure:
        raise AssertionError(f"Native fixture failed: {failure.cmd}\n{failure.stdout}\n{failure.stderr}") from failure

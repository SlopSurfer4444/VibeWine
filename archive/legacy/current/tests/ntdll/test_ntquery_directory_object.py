#!/usr/bin/env python3
"""Compile and exercise the exact extracted baseline and candidate Wine function."""

from __future__ import annotations

import hashlib
import pathlib
import subprocess
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASELINE = ROOT / "ntdll/wave7/source/unix/sync.c"
CANDIDATE = ROOT / "current/ntdll/sync.c"
PREAMBLE = (HERE / "harness_preamble.h").read_text()
SUFFIX = (HERE / "harness_suffix.c").read_text()
SIGNATURE = "NTSTATUS WINAPI NtQueryDirectoryObject("


def extract_function(path: pathlib.Path) -> str:
    source = path.read_text()
    start = source.find(SIGNATURE)
    if start < 0:
        raise RuntimeError(f"function signature missing in {path}")
    opening = source.find("{", start)
    if opening < 0:
        raise RuntimeError(f"function body missing in {path}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise RuntimeError(f"unclosed function body in {path}")


def build_and_run(label: str, source_path: pathlib.Path, work: pathlib.Path) -> subprocess.CompletedProcess[str]:
    function = extract_function(source_path)
    function_hash = hashlib.sha256(function.encode()).hexdigest()
    generated = work / f"{label}.c"
    binary = work / label
    generated.write_text(PREAMBLE + "\n" + function + "\n" + SUFFIX)
    command = [
        "clang", "-target", "x86_64-apple-darwin", "-O2", "-std=c11",
        "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(binary),
    ]
    compiled = subprocess.run(command, text=True, capture_output=True)
    if compiled.returncode:
        raise RuntimeError(f"{label} compile failed:\n{compiled.stdout}{compiled.stderr}")
    print(f"{label} extracted-function sha256={function_hash}")
    return subprocess.run([str(binary)], text=True, capture_output=True)


def main() -> int:
    if not BASELINE.is_file() or not CANDIDATE.is_file():
        raise SystemExit("baseline or candidate sync.c is missing")
    with tempfile.TemporaryDirectory(prefix="ntquery-boolean-regression-") as temporary:
        work = pathlib.Path(temporary)
        baseline = build_and_run("baseline", BASELINE, work)
        if baseline.returncode == 0:
            raise SystemExit("baseline unexpectedly passed poisoned-R8 regression")
        if "last_index == 1" not in baseline.stderr:
            raise SystemExit(
                "baseline failed for an unexpected reason; expected the poisoned-R8 index assertion:\n"
                + baseline.stdout + baseline.stderr
            )
        candidate = build_and_run("candidate", CANDIDATE, work)
        if candidate.returncode:
            raise SystemExit("candidate failed:\n" + candidate.stdout + candidate.stderr)
        print("baseline: expected failure at poisoned-R8 progress assertion")
        print(candidate.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

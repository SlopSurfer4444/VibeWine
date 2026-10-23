#!/usr/bin/env python3
"""Fault-inject complete shipping XInput backend; no Wine or real I/O.

The pinned predecessor must fail expiry-crossing cases while the current header
passes. Both execute identical tests against unchanged header snapshots. Its
small, exact historical header is bundled only as a regression fixture.
LGPL-2.1-or-later.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
HEADER = ROOT / "dlls/xinput1_3/test_backend.h"
BASE = "10557ac06d458d48cc874799adc655050108e5a1"
BASE_SHA256 = "92c0f82952b38b5da2507ade04d36de3858d20a5f0d6ca2d0f6019e3567f15ef"
CASES = ["ordinary", "enabling"]
CASES += [f"{kind}-{seam}" for kind in ("existing", "incoming", "arm") for seam in (1, 2, 3)]
CASES += [f"handoff-{seam}" for seam in (4, 5, 6, 7)]
CASES += [f"accepted-ack-{seam}" for seam in (5, 7)]
BASE_FAIL = set(CASES) - {"ordinary", "enabling"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--predecessor-header", type=Path, default=HERE / "predecessor-10557ac.h",
                        help="exact header from pinned predecessor for negative controls")
    args = parser.parse_args()
    before = HEADER.read_bytes()
    fixture = (HERE / "native-expiry-fixture.c").read_bytes()
    runner = Path(__file__).read_bytes()
    old = args.predecessor_header.read_bytes()
    if digest(old) != BASE_SHA256:
        parser.error("predecessor does not match the pinned header SHA-256")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    assert compiler and shutil.which(compiler[0]), "native C compiler unavailable"
    flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
    failures, results = [], {}
    with tempfile.TemporaryDirectory(prefix="xinput-native-expiry-") as name:
        tmp = Path(name)
        for label, content in (("predecessor", old), ("candidate", before)):
            header = tmp / (label + "-test_backend.h")
            header.write_bytes(content)
            binary = tmp / label
            diagnostic_flags = ["-DTEST_HAS_DIAGNOSTICS=1"] if b"test_diagnostics_snapshot" in content else []
            subprocess.run(compiler + flags + diagnostic_flags + ['-DTEST_BACKEND_HEADER="' + str(header) + '"',
                                              str(HERE / "native-expiry-fixture.c"), "-o", str(binary)],
                           check=True, timeout=30)
            rows = []
            diagnostic_cases = [f"diagnostic-{seam}" for seam in (8, 9, 10, 11)]
            diagnostic_cases += ["diag-throttle", "diag-accepted", "diag-worst-json", "diag-short-write", "diag-ack-short-write", "diag-io-2", "diag-io-5"]
            diagnostic_cases += [f"diag-fail-{seam}" for seam in (8, 9, 11)]
            cases = CASES + (diagnostic_cases if diagnostic_flags and label == "candidate" else [])
            for case in cases:
                run = subprocess.run([str(binary), case], text=True, capture_output=True, timeout=5)
                expected = 1 if label == "predecessor" and case in BASE_FAIL else 0
                rows.append({"case": case, "returncode": run.returncode,
                             "expected_returncode": expected, "passed_expectation": run.returncode == expected,
                             "stdout": run.stdout, "stderr": run.stderr})
                if run.returncode != expected:
                    failures.append({"generation": label, **rows[-1]})
            results[label] = rows
    assert HEADER.read_bytes() == before, "candidate header changed during verification"
    assert (HERE / "native-expiry-fixture.c").read_bytes() == fixture, "fixture changed during verification"
    assert Path(__file__).read_bytes() == runner, "runner changed during verification"
    print(json.dumps({"scope": "entire unchanged shipping header with deterministic Win32 I/O and monotonic clock faults; not Wine integration",
                      "linearization": "last expiry guard before state copy; no guarantee after copy or scheduling",
                      "compiler_flags": flags,
                      "shipping_only_warning_exceptions": ["-Wunused-parameter", "-Wsign-compare"],
                      "wire": "original VXIP0001 72-byte command; original ACK/diagnostic formats reach host vsnprintf with only DWORD %lu normalized to %u; all I/O mocked, no wire changes",
                      "source_sha256": {"predecessor": digest(old), "candidate": digest(before), "fixture": digest(fixture), "runner": digest(runner)},
                      "limitations": ["Win32 APIs and atomics are deterministic single-thread seams; no Windows scheduling or filesystem validation", "GetKeystroke main.c callsite is source-reviewed separately; this runner executes only header poll(FALSE)", "No guarantee that ACK or snapshot data implies caller-visible input delivery"],
                      "pinned_predecessor": BASE,
                      "negative_controls": "executed",
                      "results": results, "failures": failures}, indent=2))
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

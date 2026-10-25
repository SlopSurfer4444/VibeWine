#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise actual Wine address-wait queue publication with bounded schedules."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="extract the production address bodies from this Git revision")
    parser.add_argument("--expect-regression", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--arch", choices=("arm64", "x86_64"), help="Darwin compiler architecture")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    if args.revision:
        production = subprocess.check_output(["git", "show", f"{args.revision}:dlls/ntdll/sync.c"],
                                             cwd=root, text=True)
    else:
        production = (root / "dlls/ntdll/sync.c").read_text()
    start = production.index("struct futex_entry\n{")
    end = production.index("/*************************************************************************\n *           RtlInitializeSListHead", start)
    body = production[start:end]
    fixture = (here / "publication.c.in").read_text().replace("/* PRODUCTION_ADDRESS */", body)
    print(f"production SHA256={hashlib.sha256(body.encode()).hexdigest()} source={args.revision or 'working-tree'}", flush=True)
    with tempfile.TemporaryDirectory(prefix="vibewine-address-publication-") as scratch:
        source = Path(scratch) / "publication.c"
        binary = Path(scratch) / "publication"
        source.write_text(fixture)
        command = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread", "-O2",
                   "-I", str(root / "include"), str(source), "-o", str(binary)]
        if args.arch:
            command += ["-arch", args.arch]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if args.expect_regression:
            stdout = ("raw-alert/single return_before_detach=1 REGRESSION\n"
                      "raw-alert/all return_before_detach=1 REGRESSION\n")
            stderr, status = "ADDRESS_PUBLICATION_REGRESSION cases=2\n", 42
        else:
            stdout = ("raw-alert/single return_before_detach=0 PASS\n"
                      "raw-alert/all return_before_detach=0 PASS\n"
                      "all/batch257 captured tids survive publication, flush256+1, collision retained PASS\n"
                      "ADDRESS_PUBLICATION_PASS\n")
            stderr, status = "", 0
        if (result.stdout, result.stderr, result.returncode) != (stdout, stderr, status):
            raise SystemExit(f"unexpected publication result: exit={result.returncode}")
        if args.expect_regression:
            print("EXPECTED_PUBLICATION_REGRESSION_CONFIRMED")


if __name__ == "__main__":
    main()

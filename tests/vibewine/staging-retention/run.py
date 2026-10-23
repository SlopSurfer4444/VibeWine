#!/usr/bin/env python3
"""Compile exact production staging methods with real Rc and counted Metal-free mocks."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, help="DXMT root (defaults to this checkout)")
    parser.add_argument("--expect-retained-baseline", action="store_true")
    parser.add_argument("--arch", choices=("native", "x86_64"), default="native")
    parser.add_argument("--sanitize", action="store_true", help="Enable AddressSanitizer and UBSan")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = (args.source_dir or tests.parents[2]).resolve()
    files = [source / "src/dxmt/dxmt_staging.hpp", source / "src/dxmt/dxmt_staging.cpp"]
    contents = [path.read_text() for path in files]
    # Keep method/class bodies verbatim; only replace their dependency boundary.
    generated = "\n".join("\n".join(line for line in text.splitlines()
                         if not line.startswith("#include") and line != "#pragma once")
                         for text in contents)
    compiler = shutil.which("clang++") or shutil.which("c++")
    if not compiler:
        parser.error("C++ compiler unavailable")
    with tempfile.TemporaryDirectory(prefix="dxmt-staging-test-") as directory:
        scratch = Path(directory)
        (scratch / "staging-under-test.hpp").write_text(generated)
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-O1", "-g", "-pthread",
                   "-I", str(tests), "-I", str(scratch), "-I", str(source / "src/util"),
                   str(tests / "fixture.cpp"), "-o", str(scratch / "fixture")]
        if args.arch == "x86_64":
            command += ["-arch", "x86_64"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True, timeout=60)
        run = [str(scratch / "fixture")]
        if args.expect_retained_baseline:
            run += ["baseline"]
        if args.arch == "x86_64" and platform.machine() == "arm64":
            run = ["/usr/bin/arch", "-x86_64"] + run
        subprocess.run(run, check=True, timeout=15)
        print(json.dumps({"source": str(source), "arch": args.arch,
                          "sanitizers": args.sanitize, "baseline": args.expect_retained_baseline,
                          "source_sha256": {str(path.relative_to(source)): hashlib.sha256(text.encode()).hexdigest()
                                            for path, text in zip(files, contents)}}, sort_keys=True))


if __name__ == "__main__":
    main()

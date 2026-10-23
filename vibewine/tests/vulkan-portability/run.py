#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise Wine's complete instance-create conversion at native host seams."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


def definition(source, pattern):
    match = re.search(pattern, source, re.M)
    if not match:
        raise RuntimeError(f"production definition not found: {pattern}")
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", help="read Vulkan production code from this component Git revision")
    parser.add_argument("--expect-regression", action="store_true")
    parser.add_argument("--arch", choices=("arm64", "x86_64"), help="optional Darwin native/translated target")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    source = (subprocess.check_output(["git", "show", f"{args.revision}:dlls/win32u/vulkan.c"],
                                     cwd=root, text=True, timeout=30) if args.revision else
              (root / "dlls/win32u/vulkan.c").read_text())
    parts = [source[:source.index("#if 0")],
             definition(source, r"^struct instance\n\{" ) + ";",
             definition(source, r"^static const void \*find_next_struct\([^;]+?\)\n\{"),
             definition(source, r"^static int vulkan_object_compare\([^;]+?\)\n\{"),
             definition(source, r"^static VkResult convert_instance_create_info\([^;]+?\)\n\{")]
    production = "\n\n".join(parts)
    fixture = (here / "fixture.c.in").read_text().replace("/* PRODUCTION */", production)
    print(f"production SHA256={hashlib.sha256(production.encode()).hexdigest()} source={args.revision or 'working-tree'}", flush=True)
    with tempfile.TemporaryDirectory(prefix="vibewine-vulkan-portability-") as scratch:
        path = Path(scratch) / "fixture.c"
        binary = Path(scratch) / "fixture"
        path.write_text(fixture)
        command = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                   "-D_DARWIN_C_SOURCE", "-D__WINESRC__", "-DWINE_UNIX_LIB", "-fms-extensions",
                   "-I", str(root / "include"), "-pthread", "-O1", "-g",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(path), "-o", str(binary)]
        if args.arch:
            command += ["-arch", args.arch]
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
        print(result.stdout, end="")
        expected = "VULKAN_PORTABILITY_PASS cases=8\n"
        error, status = "", 0
        if args.expect_regression:
            expected = "VULKAN_PORTABILITY_REGRESSION failures=6 cases=8\n"
            error = "".join(f"FAIL {case}/{feature}\n" for case in ("supported", "existing-flags")
                            for feature in ("extension", "flag", "portability-device"))
            status = 42
        # Exact status/stdout/stderr prevent a crash or sanitizer report from
        # being misclassified as a successful expected-baseline regression.
        if result.returncode != status or result.stdout != expected or result.stderr != error:
            print(result.stderr, end="")
            raise SystemExit(f"unexpected portability fixture result: exit={result.returncode}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Execute production switch arms with real Metal.hpp/FenceSet and CPU stubs.

No native Metal/Wine library, device, game or window is used. Temporary generated
sources/binaries are removed after each run; there is no persistent collector.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]


def switch_arm(source, name, following):
    start = source.index(f"    case EncoderType::{name}: {{", source.index("ArgumentEncodingContext::flushCommands("))
    end = source.index(f"    case EncoderType::{following}: {{", start)
    return source[start:end].replace(f"case EncoderType::{name}:", "case 0:", 1)


def main():
    path = REPO / "src/dxmt/dxmt_context.cpp"
    source = path.read_text()
    fixture = (HERE / "context_fixture.cpp.in").read_text()
    for name, next_name in (("Render", "Compute"), ("Compute", "Blit"), ("Present", "Clear")):
        marker = f"/* {name.upper()}_ARM */"
        assert fixture.count(marker) == 1
        fixture = fixture.replace(marker, switch_arm(source, name, next_name))
    with tempfile.TemporaryDirectory(prefix="encoder-context-") as tmp:
        cpp = Path(tmp) / "fixture.cpp"
        binary = Path(tmp) / "fixture"
        cpp.write_text(fixture)
        subprocess.run([
            "xcrun", "clang++", "-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I" + str(REPO / "src/winemetal"), "-I" + str(REPO / "src/dxmt"),
            "-I" + str(REPO / "src/util"), "-I" + str(REPO / "src/airconv"),
            "-I" + str(REPO / "include"),
            str(cpp), "-o", str(binary),
        ], check=True, timeout=90)
        subprocess.run([str(binary)], check=True, timeout=30,
                       env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
    print("source_sha256=" + hashlib.sha256(path.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()

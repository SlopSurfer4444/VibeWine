#!/usr/bin/env python3
"""CPU-only whole-production Presenter::encodeCommands recording fixture.

Uses actual Metal.hpp wrappers and command ABI, with synchronous stub thunks.
No Metal library/device, Wine runtime, window, displayed frame, or GPU claim.
Generated source/binary are private temporary files removed after the run.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def extract_block(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[start:cursor]


def main():
    source = (REPO / "src/dxmt/dxmt_presenter.cpp").read_text()
    header = (REPO / "src/dxmt/dxmt_presenter.hpp").read_text()
    body = extract_block(source, "WMT::MetalDrawable\nPresenter::encodeCommands(")
    metadata = extract_block(header, "struct DXMTPresentMetadata {") + ";"
    configuration = extract_block(header, "struct PresentationConfiguration {") + ";"
    program = (HERE / "present_fixture.cpp.in").read_text()
    for marker, value in [("@METADATA@", metadata), ("@CONFIGURATION@", configuration), ("@FUNCTION@", body)]:
        assert program.count(marker) == 1, marker
        program = program.replace(marker, value)
    with tempfile.TemporaryDirectory(prefix="dxmt-present-") as scratch:
        cpp = Path(scratch) / "present.cpp"
        binary = Path(scratch) / "present"
        cpp.write_text(program)
        subprocess.run(["xcrun", "clang++", "-std=c++20", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(REPO / "src/winemetal"), str(cpp), "-o", str(binary)],
                       check=True, text=True, timeout=60)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=20)
    print(json.dumps({"passed": True,
                      "scope": "CPU synchronous stub-thunk interpretation; no Metal link/device/GPU/Wine/window",
                      "sanitizers": ["address", "undefined"],
                      "whole_function_sha256": hashlib.sha256(body.encode()).hexdigest(),
                      "cases": result.stdout.splitlines()}, indent=2))


if __name__ == "__main__":
    main()

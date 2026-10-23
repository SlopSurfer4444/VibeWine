#!/usr/bin/env python3
"""CPU-only whole-production Presenter::encodeCommands recording fixture.

Uses actual Metal.hpp wrappers and command ABI, with synchronous stub thunks.
Runs the default, explicit optimized, and historical-wrapper reference modes.
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
    variants = [
        ("undefined_default", []),
        ("explicit_optimized", ["-DDXMT_PERF_REF_W4_BATCHING=0"]),
        ("historical_wrapper_reference", ["-DDXMT_PERF_REF_W4_BATCHING=1"]),
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix="dxmt-present-") as scratch:
        cpp = Path(scratch) / "present.cpp"
        cpp.write_text(program)
        for name, defines in variants:
            binary = Path(scratch) / name
            subprocess.run(["xcrun", "clang++", "-std=c++20", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *defines,
                            "-I", str(REPO / "src/winemetal"), str(cpp), "-o", str(binary)],
                           check=True, text=True, timeout=60)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if result.returncode:
                raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
            results.append({"variant": name, "compile_defines": defines, "cases": result.stdout.splitlines()})
    print(json.dumps({"passed": True,
                      "scope": "CPU synchronous stub-thunk interpretation; no Metal link/device/GPU/Wine/window",
                      "sanitizers": ["address", "undefined"],
                      "whole_function_sha256": hashlib.sha256(body.encode()).hexdigest(),
                      "results": results}, indent=2))


if __name__ == "__main__":
    main()

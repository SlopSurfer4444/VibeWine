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
import platform
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
    controls = []
    with tempfile.TemporaryDirectory(prefix="dxmt-present-") as scratch:
        cpp = Path(scratch) / "present.cpp"
        command = ["xcrun", "clang++", "-std=c++20", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                   "-I", str(REPO / "src/winemetal"), "-I", str(REPO / "src/dxmt")]
        def compile_run(candidate, name, defines):
            cpp.write_text(candidate)
            binary = Path(scratch) / name
            subprocess.run([*command, *defines, str(cpp), "-o", str(binary)],
                           check=True, text=True, timeout=60)
            return subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        for name, defines in variants:
            result = compile_run(program, name, defines)
            if result.returncode:
                raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
            results.append({"variant": name, "compile_defines": defines, "cases": result.stdout.splitlines()})
        # Compile the identical whole production function for the other supported
        # Darwin architecture; execution remains on the host architecture.
        other = "x86_64" if platform.machine() == "arm64" else "arm64"
        cpp.write_text(program)
        for name, defines in variants:
            subprocess.run([*command, "-arch", other, *defines, "-c", str(cpp),
                            "-o", str(Path(scratch) / (name + ".o"))], check=True, timeout=60)
        mutations = [
            ("offset_viewport_wrong_absolute_blit", "!has_bars && source_width == (uint64_t)width &&\n                                  source_height == (uint64_t)height",
             "source_width == (uint64_t)viewport.width && source_height == (uint64_t)viewport.height"),
            ("bars_not_cleared", "has_bars ? WMTLoadActionClear : WMTLoadActionDontCare", "WMTLoadActionDontCare"),
            ("wrong_viewport_tail", "viewport_command.viewports.set(&viewport);", "viewport.originX += 1; viewport_command.viewports.set(&viewport);"),
            ("repeated_source_dimension_thunks", "!has_bars && source_width == (uint64_t)width &&\n                                  source_height == (uint64_t)height",
             "!has_bars && backbuffer.width() == (uint64_t)width &&\n                                  backbuffer.height() == (uint64_t)height"),
        ]
        for mutation, before, after in mutations:
            assert body.count(before) == 1, mutation
            changed = program.replace(before, after)
            # The ABI-tail mutation targets the optimized list specifically.
            groups = variants[:2] if mutation == "wrong_viewport_tail" else variants
            for name, defines in groups:
                result = compile_run(changed, mutation + "_" + name, defines)
                if result.returncode != 1 or ": " not in result.stderr or "sanitizer" in result.stderr.lower() or "runtime error:" in result.stderr:
                    raise RuntimeError(f"mutation not rejected cleanly: {mutation}/{name}\n{result.stdout}\n{result.stderr}")
                controls.append({"mutation": mutation, "variant": name, "rejected": True,
                                 "failure": result.stderr.strip()})
    print(json.dumps({"passed": True,
                      "scope": "CPU synchronous stub-thunk interpretation; no Metal link/device/GPU/Wine/window",
                      "sanitizers": ["address", "undefined"],
                      "whole_function_sha256": hashlib.sha256(body.encode()).hexdigest(),
                      "results": results, "other_architecture_compile": other, "mutation_controls": controls}, indent=2))


if __name__ == "__main__":
    main()

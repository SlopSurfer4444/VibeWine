#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Configure only a tiny temporary Meson fixture using the real PERF block.

Checks host/native C/C++ compiler arguments for every option without building
DXMT, invoking Ninja, executing PE output or touching a shared build directory.
Meson's normal compiler-sanity checks are CPU-only.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--meson", type=Path, required=True)
    parser.add_argument("--cross-file", type=Path, required=True)
    parser.add_argument("--ninja", type=Path, help="reviewed Ninja for configure discovery only; default adjacent to Meson")
    args = parser.parse_args()
    for path in (args.meson, args.cross_file, args.ninja or args.meson.with_name("ninja")):
        assert path.is_absolute() and path.is_file(), path
    options = (REPO / "meson.options").read_text()
    option_start = options.index("option('vibewine_perf_reference'")
    option_end = options.index("\n", options.index("description :", option_start))
    option = options[option_start:option_end] + "\n"
    build = (REPO / "meson.build").read_text()
    start = build.index("# Corrected performance controls")
    end = build.index("add_project_arguments('-DDXMT_PAGE_SIZE", start)
    block = build[start:end]
    results = []
    with tempfile.TemporaryDirectory(prefix="w4-reference-meson-") as tmp:
        root = Path(tmp)
        source = root / "source"
        source.mkdir()
        (source / "meson.options").write_text(option)
        (source / "probe.c").write_text("int main(void) { return 0; }\n")
        (source / "probe.cpp").write_text("int main() { return 0; }\n")
        targets = "\n".join(
            "executable('" + kind + "-" + language + "', 'probe." + extension + "', native: " + native + ")"
            for kind, native in (("host", "false"), ("native", "true"))
            for language, extension in (("c", "c"), ("cpp", "cpp")))
        (source / "meson.build").write_text(
            "project('w4-reference-wiring', ['c', 'cpp'])\ndxmt_crossbuild = meson.is_cross_build()\n" + block + targets + "\n")
        environment = dict(os.environ, NINJA=str(args.ninja or args.meson.with_name("ninja")))
        for mode, expected in ((None, (0, 0)), ("none", (0, 0)), ("w4", (1, 1)),
                               ("getters", (1, 0)), ("batching", (0, 1))):
            destination = root / (mode or "default")
            command = [str(args.meson), "setup", str(destination), str(source), "--cross-file", str(args.cross_file)]
            if mode is not None:
                command.append("-Dvibewine_perf_reference=" + mode)
            configured = subprocess.run(command, capture_output=True, text=True, timeout=60, env=environment)
            assert configured.returncode == 0, (mode, configured.stdout, configured.stderr)
            entries = json.loads((destination / "compile_commands.json").read_text())
            assert len(entries) == 4, (mode, entries)
            for entry in entries:
                parameters = entry.get("arguments") or shlex.split(entry["command"])
                for name, value in zip(("GETTERS", "BATCHING"), expected):
                    prefix = "-DDXMT_PERF_REF_W4_" + name + "="
                    assert [p for p in parameters if p.startswith(prefix)] == [prefix + str(value)], (mode, entry)
            results.append({"mode": mode or "undefined_default", "getters": expected[0], "batching": expected[1],
                            "host_native_c_cpp_commands": len(entries)})
    print(json.dumps({"passed": True, "scope": "temporary configure/CPU compiler sanity only; no DXMT build/PE/GPU/runtime",
                      "perf_block_sha256": hashlib.sha256(block.encode()).hexdigest(), "modes": results}, indent=2))


if __name__ == "__main__":
    main()

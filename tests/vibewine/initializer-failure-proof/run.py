#!/usr/bin/env python3
"""Opt-in CPU diagnosis of known ResourceInitializer allocation defects; NOT a passing regression test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import tempfile


PRODUCTION = (
    "src/dxmt/dxmt_allocation.hpp",
    "src/dxmt/dxmt_allocation.cpp",
    "src/dxmt/dxmt_ring_bump_allocator.hpp",
    "src/dxmt/dxmt_resource_initializer.hpp",
    "src/dxmt/dxmt_resource_initializer.cpp",
)
DEPENDENCIES = (
    "src/winemetal/Metal.hpp",
    "src/winemetal/winemetal.h",
    "src/winemetal/wave7_layer_apply.h",
    "include/ftl.hpp",
    "src/util/util_math.hpp",
    "src/util/util_likely.hpp",
    "src/dxmt/dxmt_context.hpp",
)
FIXTURES = ("fixture-prelude.hpp", "fixture-resources.hpp", "fixture-main.hpp")


def strip_dependencies(text):
    # Production method bodies are unchanged. Only dependency includes and
    # pragma once are removed before compiling the complete classes together.
    return "\n".join(line for line in text.splitlines()
                     if not line.startswith("#include") and line != "#pragma once")


def limit_capture():
    # Bound each diagnostic stdout/stderr file; all files are private temporary
    # artifacts and are removed by TemporaryDirectory, including on failure.
    resource.setrlimit(resource.RLIMIT_FSIZE, (4 * 1024 * 1024, 4 * 1024 * 1024))


def execute(command, scratch, name, timeout, env=None):
    stdout = scratch / (name + ".stdout")
    stderr = scratch / (name + ".stderr")
    try:
        with stdout.open("wb") as out, stderr.open("wb") as err:
            result = subprocess.run(command, stdout=out, stderr=err, timeout=timeout,
                                    check=False, env=env, preexec_fn=limit_capture)
        for path in (stdout, stderr):
            print(path.read_text(errors="replace"), end="")
        if result.returncode:
            raise RuntimeError(f"{name} exited with status {result.returncode}")
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"{name} exceeded its {timeout}-second deadline") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expect-known-defects", action="store_true",
                        help="Explicitly request reproduction of known bugs; success does not mean correctness")
    parser.add_argument("--source-dir", type=Path,
                        help="DXMT source root; defaults to the checkout containing this diagnostic")
    parser.add_argument("--source-ref", help="Read an immutable historical Git revision, e.g. 66bae06c")
    args = parser.parse_args()
    if not args.expect_known_defects:
        parser.error("diagnostic mode only: pass --expect-known-defects to reproduce known bugs; "
                     "this must not be added as a green correctness/CI gate")
    if platform.system() != "Darwin":
        parser.error("this bounded diagnostic requires macOS sandbox-exec; no unsandboxed fallback")
    compiler = shutil.which("clang++")
    sandbox = shutil.which("sandbox-exec")
    if not compiler or not sandbox:
        parser.error("clang++ and sandbox-exec are required")

    tests = Path(__file__).resolve().parent
    source = (args.source_dir or tests.parents[2]).resolve()
    # Snapshot every input once. Both the receipt and compiler consume these
    # bytes, so concurrent changes to the checkout cannot misbind the receipt.
    revision = None
    if args.source_ref:
        revision = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "--verify", "--end-of-options", args.source_ref + "^{commit}"],
            text=True, timeout=10).strip()
        source_bytes = {name: subprocess.check_output(
            ["git", "-C", str(source), "show", revision + ":" + name], timeout=10)
            for name in PRODUCTION + DEPENDENCIES}
    else:
        source_bytes = {name: (source / name).read_bytes() for name in PRODUCTION + DEPENDENCIES}
    if b"struct InitResult" in source_bytes["src/dxmt/dxmt_resource_initializer.hpp"]:
        parser.error("this diagnostic expects the historical faulty API; use --source-ref 66bae06c "
                     "or run tests/vibewine/initializer-transaction/run.py for repaired behavior")
    fixture_bytes = {name: (tests / name).read_bytes() for name in FIXTURES + ("run.py",)}
    contents = {name: data.decode("utf-8") for name, data in source_bytes.items()}
    context = contents["src/dxmt/dxmt_context.hpp"]
    begin = context.index("inline std::size_t\nalign_forward_adjustment")
    end = context.index("\nenum class PipelineStage", begin)
    generated = "\n".join((
        '#include "fixture-prelude.hpp"',
        "namespace dxmt {", context[begin:end], "}",
        strip_dependencies(contents[PRODUCTION[0]]),
        strip_dependencies(contents[PRODUCTION[1]]),
        '#include "fixture-resources.hpp"',
        strip_dependencies(contents[PRODUCTION[2]]),
        "#define private public",
        strip_dependencies(contents[PRODUCTION[3]]),
        "#undef private",
        strip_dependencies(contents[PRODUCTION[4]]),
        '#include "fixture-main.hpp"',
    ))
    head = subprocess.run(["git", "-C", str(source), "rev-parse", "HEAD"],
                          text=True, capture_output=True, check=False, timeout=10)
    receipt = {
        "mode": "expect-known-defects",
        "meaning": "successful diagnosis reproduces bugs; NOT repair acceptance",
        "source_head": head.stdout.strip() if head.returncode == 0 else None,
        "historical_source_revision": revision,
        "source_sha256": {name: hashlib.sha256(source_bytes[name]).hexdigest()
                          for name in PRODUCTION + DEPENDENCIES},
        "fixture_sha256": {name: hashlib.sha256(fixture_bytes[name]).hexdigest()
                           for name in FIXTURES + ("run.py",)},
        "generated_sha256": hashlib.sha256(generated.encode()).hexdigest(),
        "observation_count": 16,
        "sanitizers": ["address", "undefined"],
        "exceptions": "disabled for this fixture",
        "execution": "native CPU with mocked Metal C API; no GPU or installed runtime",
    }
    print(json.dumps(receipt, sort_keys=True), flush=True)
    with tempfile.TemporaryDirectory(prefix="dxmt-initializer-proof-") as directory:
        scratch = Path(directory).resolve()
        for name, data in source_bytes.items():
            snapshot = scratch / name
            snapshot.parent.mkdir(parents=True, exist_ok=True)
            snapshot.write_bytes(data)
        fixture_dir = scratch / "fixtures"
        fixture_dir.mkdir()
        for name, data in fixture_bytes.items():
            (fixture_dir / name).write_bytes(data)
        cpp = scratch / "production.cpp"
        executable = scratch / "fixture"
        cpp.write_text(generated)
        command = [compiler, "-std=c++20", "-O1", "-g", "-fno-exceptions",
                   "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                   "-I", str(fixture_dir), "-I", str(scratch / "src/winemetal"),
                   "-I", str(scratch / "src/util"), "-I", str(scratch / "include"),
                   str(cpp), "-o", str(executable)]
        execute(command, scratch, "compile", 60)
        # Quote the dynamically allocated path for the sandbox profile's string
        # literal, not for a shell. No shell or HOME override is used.
        scratch_literal = json.dumps(str(scratch))
        profile = '(version 1)(allow default)(deny network*)(deny file-write*)' + \
                  '(allow file-write* (subpath ' + scratch_literal + '))'
        execute([sandbox, "-p", profile, str(executable)], scratch, "fixture", 15,
                {**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})


if __name__ == "__main__":
    main()

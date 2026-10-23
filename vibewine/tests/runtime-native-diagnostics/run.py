#!/usr/bin/env python3
"""Offline native diagnostic fixtures; no AppKit, Metal, Wine or GUI runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "dlls/winemac.drv"
GATE = "VIBEWINE_DRAWABLE_PHASE_DIAG"
VALUES = (None, "", "0", "01", "1x", "true", " 1", "1 ", "1")
SOURCES = ("drawable_phase_diag.h", "visibility_guard.h", "visibility_snapshot.inc",
           "visibility_startup.inc", "d3dmetal_objc.m", "cocoa_event.m")
OLD_VISIBILITY_SHA256 = "daf3feb2ec3de6ebc800ce86f5da121d00376a18ff3eb5f22491dc083dae39f3"
FAILURE_MODES = ("startup-json-nil", "startup-json-exception", "startup-json-oversized")
SANITIZER = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:")
RECORD_FIELDS = set("v site record pid tid layer queue surface drawable event_type "
                    "count_before count_after start_ns end_ns wall_ns cpu_valid cpu_ns "
                    "phase0_ns phase1_ns phase2_ns phase3_ns".split())


def require(condition, details):
    if not condition:
        raise RuntimeError(str(details))


def records(stderr):
    result = []
    for line in stderr.splitlines():
        require(line.startswith("W7PD "), ("unexpected diagnostic", line))
        fields = line.split()[1:]
        record = dict(field.split("=", 1) for field in fields)
        require(len(record) == len(fields) and set(record) == RECORD_FIELDS,
                ("unexpected metadata fields", record))
        require(record["v"] == "1" and 0 < int(record["record"]) <= 128, record)
        require(int(record["pid"]) > 0 and int(record["tid"]) > 0, record)
        require(sum(int(record[f"phase{i}_ns"]) for i in range(4)) == int(record["wall_ns"]), record)
        result.append(record)
    return result


def retained_visibility_baseline(source):
    """Reconstruct the exact retained include, never use an external checkout."""
    current_comment = (" * objects. A nil payload reports collection/serialization failure so the caller\n"
                       " * can release request-owned resources. Exactly one admitted request per host\n"
                       " * process. */")
    old_comment = " * objects. Exactly one admitted request per host process. */"
    current_dispatch = """            /* Every executed admitted request must complete, including failed
             * serialization. The startup owner closes its directory fd there;
             * suppressing completion on failure would strand that ownership. */
            if (data.length > 32768) data = nil;
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                @autoreleasepool { completion(data); }
            });"""
    old_dispatch = """            if (data && data.length <= 32768)
                dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                    @autoreleasepool { completion(data); }
                });"""
    require(source.count(current_comment) == source.count(current_dispatch) == 1,
            "Retained baseline reconstruction seams changed")
    baseline = source.replace(current_comment, old_comment).replace(current_dispatch, old_dispatch)
    require(hashlib.sha256(baseline.encode()).hexdigest() == OLD_VISIBILITY_SHA256,
            "Reconstructed retained visibility include hash changed")
    return baseline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("arm64", "x86_64"), help="compile/run architecture")
    parser.add_argument("--no-sanitizers", action="store_true", help="omit C ASan/UBSan checks")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("macOS clang and Foundation SDK are required")
    evidence = []
    env = dict(os.environ)
    env.pop(GATE, None)
    env.pop("VIBEWINE_DS3_VISIBILITY_DIR", None)
    env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    def execute(argv, label, extra_env=None, expected_code=0):
        completed = subprocess.run([str(arg) for arg in argv], env=extra_env or env,
                                   text=True, capture_output=True, timeout=60)
        require(completed.returncode == expected_code,
                (label, completed.returncode, completed.stdout, completed.stderr))
        require(not SANITIZER.search(completed.stdout + completed.stderr), label)
        evidence.append(label)
        return completed

    def gated(value):
        current = dict(env)
        if value is not None:
            current[GATE] = value
        return current

    # Canonicalize macOS /var -> /private/var before the production no-symlink
    # directory guard sees it. The guard still enforces all actual permissions.
    with tempfile.TemporaryDirectory(prefix="vibewine-native-diag-",
                                     dir=Path(tempfile.gettempdir()).resolve()) as directory:
        temp = Path(directory)
        arch = ["-arch", args.arch] if args.arch else []
        common = ["/usr/bin/clang", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                  "-pthread", "-I", SOURCE, *arch]
        variants = [("normal", [])]
        if not args.no_sanitizers:
            variants.append(("asan-ubsan", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]))
        for variant, flags in variants:
            policy = temp / ("policy-" + variant)
            execute([*common, *flags, HERE / "policy.c", HERE / "second.c", "-o", policy],
                    "compile policy " + variant)
            for value in VALUES:
                execute([policy, "enabled" if value == "1" else "disabled"],
                        f"policy {variant} gate={value!r}", gated(value))
            guard = temp / ("guards-" + variant)
            execute([*common, *flags, HERE / "guards.c", "-o", guard], "compile guards " + variant)
            execute([guard, temp], "private directory/image guards " + variant)

        raw = temp / "raw"
        execute([*common, HERE / "raw.c", "-o", raw], "compile raw metadata")
        emitted = records(execute([raw], "raw metadata", gated("1")).stderr)
        require(len(emitted) == 2, emitted)
        for index, record in enumerate(emitted):
            require(record["site"] == "drawable" and int(record["wall_ns"]) == 100000000, record)
            require(int(record["record"]) == index + 1 and int(record["cpu_valid"]) == 1 - index, record)
            require(int(record["cpu_ns"]) == (0 if index else 8), record)
            require([record[key] for key in ("layer", "queue", "surface", "drawable")]
                    == ["0x11", "0x22", "0x33", "0x44"], record)

        source = (SOURCE / "d3dmetal_objc.m").read_text()
        start = source.index("@implementation WineMetalLayer")
        end = source.index("@end", start) + len("@end")
        extracted = (HERE / "override.m").read_text().replace(
            "/* EXTRACTED_IMPLEMENTATION */", source[start:end]).replace(
            "/* DIAGNOSTIC_HEADER */", '#include "drawable_phase_diag.h"')
        fixture = temp / "override.m"
        fixture.write_text(extracted)
        objc = ["/usr/bin/clang", "-std=gnu11", "-fblocks", "-fno-objc-arc", "-Werror",
                "-I", SOURCE, "-framework", "Foundation", *arch]
        override = temp / "override"
        execute([*objc, fixture, "-o", override], "compile exact nextDrawable against Foundation mocks")
        for mode in ("post", "no-post"):
            posting = mode == "post"
            for value in VALUES:
                completed = execute([override, mode], f"nextDrawable {mode} gate={value!r}", gated(value))
                counts = json.loads(completed.stdout)
                expected = dict(delegate=2 if posting else 1, superview=2 if posting else 0,
                                window=3 if posting else 0, queue=int(posting), startup=1,
                                create=int(posting), post=int(posting), release=int(posting),
                                super=1, same_drawable=True,
                                raw_calls=(5 if posting else 3) if value == "1" else 0,
                                cpu_calls=(5 if posting else 3) if value == "1" else 0)
                require(counts == expected, (mode, value, counts, expected))
                emitted = records(completed.stderr)
                require(len(emitted) == int(value == "1"), (mode, value, emitted))
                if emitted:
                    record = emitted[0]
                    require(record["site"] == "drawable" and record["cpu_valid"] == "1", record)
                    require(int(record["wall_ns"]) == (400000000 if posting else 200000000), record)
                    if not posting:
                        require(record["phase1_ns"] == record["phase2_ns"] == "0", record)
                        require(record["phase0_ns"] == record["phase3_ns"] == "100000000", record)
                        require(record["queue"] == record["surface"] == "0x0", record)

        lifecycle = temp / "lifecycle"
        execute([*objc, HERE / "lifecycle.m", "-o", lifecycle], "compile exact visibility includes against Foundation mocks")
        lifecycle_modes = ["disabled", "scope-steam", "scope-wrong-fullpath", "sampled",
                     "invalid-json", "expired-main", "startup-write", "startup-retain",
                     "startup-expired", "startup-bad-dir", *FAILURE_MODES]
        for mode in lifecycle_modes:
            output = temp / mode
            output.mkdir(mode=0o755 if mode == "startup-bad-dir" else 0o700)
            current = dict(env, VIBEWINE_DS3_VISIBILITY_DIR=str(output))
            execute([lifecycle, mode, output], "visibility " + mode, current)
            files = list(output.iterdir())
            publishes = mode in ("startup-write", "startup-retain")
            require(len(files) == int(publishes), (mode, files))
            if publishes:
                result = files[0]
                require(result.name == "startup-visibility.json" and result.stat().st_size <= 32768,
                        (mode, result))
                require(result.stat().st_mode & 0o7777 == 0o600, (mode, oct(result.stat().st_mode)))
                require(json.loads(result.read_text())["status"] == "sampled", mode)

        baseline_directory = temp / "baseline"
        baseline_directory.mkdir(mode=0o700)
        baseline_source = retained_visibility_baseline((SOURCE / "visibility_snapshot.inc").read_text())
        (baseline_directory / "visibility_snapshot.inc").write_text(baseline_source)
        baseline = temp / "lifecycle-baseline"
        # -I search order must select the temporary retained include before SOURCE.
        execute([*objc[:1], "-I", baseline_directory, *objc[1:], HERE / "lifecycle.m", "-o", baseline],
                "compile exact retained serialization baseline")
        for index, mode in enumerate(FAILURE_MODES, 1):
            output = temp / ("baseline-" + mode)
            output.mkdir(mode=0o700)
            current = dict(env, VIBEWINE_DS3_VISIBILITY_DIR=str(output))
            completed = execute([baseline, mode, output], "retained baseline detects " + mode,
                                current, expected_code=2)
            expected = f"FAIL admitted serialization failure cleanup: mode={index} close=0 publish=0\n"
            require(completed.stderr == expected and not completed.stdout, (mode, completed.stderr))
            require(not list(output.iterdir()), (mode, "unexpected baseline publication"))

    print(json.dumps({"status": "passed", "checks": len(evidence), "cases": evidence,
                      "source_sha256": {name: hashlib.sha256((SOURCE / name).read_bytes()).hexdigest()
                                        for name in SOURCES},
                      "retained_visibility_baseline_sha256": OLD_VISIBILITY_SHA256,
                      "scope": "Offline C and Foundation mocks; no AppKit/Metal objects, Wine runtime, GPU, GUI, installation or game execution",
                      "limitations": "No native queue-post method extraction, dispatch timing, real retain graph, window visibility, displayed frames or FPS evidence"}, indent=2))


if __name__ == "__main__":
    main()

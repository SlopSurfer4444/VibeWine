#!/usr/bin/env python3
"""Compile and execute exact enumeration code against deterministic CF metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

FUNCTIONS = (
    "static BOOL display_mode_is_supported(",
    "static void display_mode_to_devmode_fields(",
    "static BOOL display_mode_matches_descriptor(",
    "void check_retina_status(",
    "static void display_get_current_mode(",
    "UINT macdrv_UpdateDisplayDevices(",
)


def extract(source, marker):
    start = source.index(marker)
    body = source.index("{", start)
    level = 1
    pos = body + 1
    while level:
        if source[pos] == "{":
            level += 1
        elif source[pos] == "}":
            level -= 1
        pos += 1
    return source[start:pos]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    raw = args.source.read_bytes()
    source = raw.decode()
    fixture = Path(__file__).with_name("retina_fixture.c").read_text()
    generated = fixture.replace("/* PRODUCTION_FUNCTIONS */", "\n\n".join(extract(source, f) for f in FUNCTIONS))
    header_path = args.source.parent / "macdrv_cocoa.h"
    header_raw = header_path.read_bytes()
    header = header_raw.decode()
    generated = generated.replace("/* EXACT_GEOMETRY_HELPER */", extract(header, "static inline CGRect cgrect_win_from_mac("))
    generated_path = args.output / "retina_fixture.c"
    binary = args.output / "retina_fixture"
    generated_path.write_text(generated)
    command = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
               "-fsanitize=address,undefined", "-g", "-O1", str(generated_path), "-framework", "CoreFoundation", "-o", str(binary)]
    build = subprocess.run(command, capture_output=True, text=True)
    (args.output / "build.log").write_text(build.stdout + build.stderr)
    result = {"sourceSha256": hashlib.sha256(raw).hexdigest(),
              "testRunnerSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "testTemplateSha256": hashlib.sha256(Path(__file__).with_name("retina_fixture.c").read_bytes()).hexdigest(),
              "generatedFixtureSha256": hashlib.sha256(generated_path.read_bytes()).hexdigest(), "functionsExtractedVerbatim": len(FUNCTIONS), "extractedFunctionMarkers": FUNCTIONS,
              "geometryHelperExtractedVerbatim": "cgrect_win_from_mac",
              "geometryHeaderPath": str(header_path), "geometryHeaderSha256": hashlib.sha256(header_raw).hexdigest(),
              "command": command, "buildExitCode": build.returncode, "scope": "exact Retina state and device rebuild code with fake CG/monitor providers"}
    if build.returncode == 0:
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        (args.output / "run.log").write_text(run.stdout + run.stderr)
        result.update(runExitCode=run.returncode, stdout=run.stdout, stderr=run.stderr)
        unknown_cases = {}
        for case in ("mode-on", "mode-off", "descriptor-on", "descriptor-off"):
            unknown = subprocess.run([str(binary), case], capture_output=True, text=True)
            (args.output / (case + ".log")).write_text(unknown.stdout + unknown.stderr)
            unknown_cases[case] = {"exitCode": unknown.returncode, "stdout": unknown.stdout,
                                   "stderr": unknown.stderr}
        result["unknownStateCases"] = unknown_cases
        result["allCasesPassed"] = run.returncode == 0 and all(x["exitCode"] == 0 for x in unknown_cases.values())
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result.get("allCasesPassed", False) else 1)

if __name__ == "__main__":
    main()

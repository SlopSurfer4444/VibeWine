#!/usr/bin/env python3
"""Exercise the private checked native layer bridge without a GPU or installation."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

HERE = Path(__file__).resolve().parent
BRIDGE = HERE.parent / "source/bridge/src/winemetal"
OLD_BRIDGE = HERE.parents[2] / "history/wave7-candidate1/native/bridge/src/winemetal"
WINE = HERE.parent / "source/winemac.drv"
all_modes = ["validation", "old-bridge", "prepare-failure", "executor-reject", "success", "exception-retry", "reentry", "worker", "worker-copy", "queued-shutdown", "active-shutdown", "colorspace-format-matrix"]
parser = argparse.ArgumentParser()
parser.add_argument("--scenario", choices=all_modes)
args = parser.parse_args()


def extract(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    at, depth = opening + 1, 1
    while depth:
        if text[at] == "{":
            depth += 1
        elif text[at] == "}":
            depth -= 1
        at += 1
    return text[start:at]


app = (WINE / "cocoa_app.m").read_text()
native = (BRIDGE / "unix/winemetal_ds3.c").read_text()
legacy = (OLD_BRIDGE / "unix/winemetal_ds3.c").read_text()
property_marker = "static NTSTATUS\n_MTLCommandBuffer_property(void *obj)"
old_property = extract(legacy, property_marker).replace(
    "_MTLCommandBuffer_property", "legacy_MTLCommandBuffer_property")
new_property = extract(native, property_marker)
prefix = (HERE / "fixture_prefix.m").read_text()
source = prefix + "\n" + extract(app, "static void PerformRequest(void *info)\n{")
source += "\n" + extract(app, "void OnMainThreadAsync(dispatch_block_t block)\n{")
source += '\n#include "' + str(WINE / "wave7_main_executor.inc") + '"\n'
source += r'''
static bool execute_on_main_checked(dispatch_block_t block) {
  executor_calls++;
  return !reject_executor && macdrv_run_on_main_v1(block)!=0;
}
'''
source += extract(native, "CFStringRef\nGetColorSpaceName(enum WMTColorSpace colorspace)")
source += '\n#include "' + str(BRIDGE / "unix/wave7_layer_apply.inc") + '"\n'
source += r'''
/* Feedback dispatch is unrelated to the checked layer property case. */
#define W7FB_PROPERTY UINT64_C(0x573746420001)
struct W7FBRequest;
static uint64_t w7fb_dispatch(struct W7FBRequest *request) { (void)request; abort(); }
'''
source += old_property + "\n" + new_property + "\n"
source += (HERE / "fixture_body.m").read_text()
(HERE / "generated_test.m").write_text(source)
modes = [args.scenario] if args.scenario else all_modes
for sanitizer in [False, True]:
    binary = HERE / ("test_asan" if sanitizer else "test_native")
    command = ["clang", "-fblocks", "-fno-objc-arc", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-I", str(BRIDGE), "-I", str(BRIDGE.parents[1]), str(HERE / "generated_test.m"),
               "-framework", "Foundation", "-framework", "QuartzCore", "-framework", "CoreGraphics", "-framework", "Metal", "-o", str(binary)]
    if sanitizer:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, check=True, timeout=120)
    for mode in modes:
        subprocess.run([str(binary), mode], check=True, timeout=20)
paths = [BRIDGE.parents[1] / "wave7_layer_apply.h", BRIDGE / "unix/wave7_layer_apply.inc", BRIDGE / "unix/winemetal_ds3.c",
         BRIDGE / "winemetal.h", BRIDGE / "winemetal_thunks.h", OLD_BRIDGE / "unix/winemetal_ds3.c",
         WINE / "wave7_main_executor.inc", WINE / "cocoa_app.m", HERE / "run.py",
         HERE / "fixture_prefix.m", HERE / "fixture_body.m"]
result = {"passed": True, "builds": ["regular", "address+undefined"], "scenarios": modes,
          "scope": "Source-connected native bridge with mock layer; real Foundation Wine request source/executor; no GPU, installation, app, or live game.",
          "source_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}}
receipt = HERE / (f"result-{args.scenario}.json" if args.scenario else "result.json")
receipt.write_text(json.dumps(result, indent=2) + "\n")
print("PASS native checked apply fixture: regular and ASan/UBSan")

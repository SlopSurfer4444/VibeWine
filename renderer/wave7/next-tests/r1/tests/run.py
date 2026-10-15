#!/usr/bin/env python3
"""Compile only extracted exact checked apply/executor source; no Wine/GPU launch."""
from pathlib import Path
import hashlib
import itertools
import json
import resource
import subprocess

HERE = Path(__file__).resolve().parent
GEN = HERE.parents[4]
BRIDGE = GEN / "native/wave7/source/bridge/src/winemetal"
WINE = GEN / "native/wave7/source/winemac.drv"
PATCHED = BRIDGE / "unix/wave7_layer_apply.inc"
BUILD = HERE / "build"
BUILD.mkdir(exist_ok=True)

def extract(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    at, depth = opening + 1, 1
    while depth:
        depth += (text[at] == "{") - (text[at] == "}")
        at += 1
    return text[start:at]

prefix_path = HERE / "fixture_prefix.m"
prefix = prefix_path.read_text()
prefix = prefix.replace("static void w7ds_refresh_layer_main(FixtureMetalLayer *layer) { (void)layer; refreshes++; }",
    'static bool throw_refresh;\nstatic void w7ds_refresh_layer_main(FixtureMetalLayer *layer) { (void)layer; assert([NSThread isMainThread]); refreshes++; if(throw_refresh) [NSException raise:@"RefreshFailure" format:@"expected"]; }')
app = (WINE / "cocoa_app.m").read_text()
native = (BRIDGE / "unix/winemetal_ds3.c").read_text()
source = prefix + "\n" + extract(app, "static void PerformRequest(void *info)\n{")
source += "\n" + extract(app, "void OnMainThreadAsync(dispatch_block_t block)\n{")
source += '\n#include "' + str(WINE / "wave7_main_executor.inc") + '"\n'
old_body_path = HERE / "fixture_body.m"
old_body = old_body_path.read_text()
source += '\nstatic const CFStringRef querymode=CFSTR("WineAppWaitQueryResponseMode");\nstatic atomic_int done;\n'
source += extract(old_body, "static void setup(void)") + "\n"
source += extract(old_body, "static void teardown(void)") + "\n"
# Only symbol lookup is controlled; dispatch_once, executor branch selection,
# inline/GCD dispatch and actual Wine executor are exact production source.
source += '''
#include <dlfcn.h>
static bool fixture_use_wine;
static int resolution_calls;
static void *fixture_dlsym(void *handle, const char *name) {
  assert(handle==RTLD_DEFAULT && !strcmp(name,"macdrv_run_on_main_v1"));
  resolution_calls++;
  return fixture_use_wine ? (void *)macdrv_run_on_main_v1 : NULL;
}
#define dlsym fixture_dlsym
'''
source += extract(native, "static bool\nexecute_on_main_checked(dispatch_block_t block)")
source += "\n#undef dlsym\n"
source += extract(native, "CFStringRef\nGetColorSpaceName(enum WMTColorSpace colorspace)")
body = (HERE / "body.m").read_text()
cases = list(itertools.product(["fallback", "wine"], ["inline", "worker"], ["normal"], ["none", "setter", "refresh"]))
cases += [("wine", where, "reject", "none") for where in ["inline", "worker"]]
patched = PATCHED.read_text()
variants = {"candidate": patched}
results = []
def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
for name, apply in variants.items():
    generated = BUILD / (name + ".m")
    generated.write_text(source + "\n" + apply + "\n" + body)
    for sanitize in ([False, True] if name == "candidate" else [False]):
        binary = BUILD / (name + ("-asan" if sanitize else ""))
        cmd = ["clang", "-fblocks", "-fno-objc-arc", "-g", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable", "-I", str(BRIDGE), "-I", str(BRIDGE.parents[1]), str(generated), "-framework", "Foundation", "-framework", "QuartzCore", "-framework", "CoreGraphics", "-framework", "Metal", "-o", str(binary)]
        if sanitize:
            cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        compile_result = subprocess.run(cmd, timeout=45, capture_output=True, text=True)
        assert compile_result.returncode == 0, compile_result.stderr
        selected = cases if name == "candidate" else [("fallback", "worker", "normal", "refresh" if name == "early-completed" else "setter")]
        for case in selected:
            result = subprocess.run([str(binary)] + list(case), timeout=6, capture_output=True, text=True, preexec_fn=no_core)
            expected = result.returncode == 0 if name == "candidate" else result.returncode != 0
            if name == "baseline":
                expected = result.returncode < 0 and "uncaught exception 'FixtureSetterFailure'" in result.stderr
            elif name != "candidate":
                expected = result.returncode < 0 and "Assertion failed: (request.status==W7LA_FAILED)" in result.stderr
            row = {"variant": name, "sanitized": sanitize, "case": case, "returncode": result.returncode, "expectedOutcome": expected, "stdout": result.stdout[-1500:], "stderr": result.stderr[-2000:]}
            results.append(row)
            assert expected, row
inputs = [PATCHED, BRIDGE / "unix/winemetal_ds3.c", WINE / "cocoa_app.m", WINE / "wave7_main_executor.inc", prefix_path, old_body_path, HERE / "body.m", HERE / "run.py"]
(HERE / "result.json").write_text(json.dumps({"passed": True, "candidateCases": len(cases), "builds": ["regular", "ASan+UBSan"], "negativeControls": 0, "negativeControlEvidence": "Original owner result retained separately; not rerun", "results": results, "inputs": {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, "limits": "Mock layer/refresh and controlled symbol lookup; real Foundation, GCD and exact Wine executor. No GPU, presenter, native runtime or game."}, indent=2) + "\n")
print("PASS 14 cases x regular/ASan+UBSan; prior negative controls reused, not rerun")

# Checking and building Wave7 sources

On macOS with Python 3 and Xcode Command Line Tools, run from this repository:

```sh
python3 tests/run_wave7.py
```

The runner copies source into a temporary directory and runs small source-connected
C/C++/Objective-C fixtures and synthetic trace/evidence tests. It does not build a
full runtime, install anything, start Wine or launch a game. `--mutation-check`
adds WSI negative controls; `--include-history` separately runs retained Wave6/
Wave5 checks. Generated binaries and results remain in the disposable copy.

Full component compilation needs matching external Wine configuration, generated
headers, dependency libraries, SDK and compiler. See
[build-recipes/README.md](build-recipes/README.md) for explicit inputs and commands.
Recipes print by default and require `--execute` to compile. The recorded native
build used Apple clang 21.0.0; a clean-room or bit-identical public rebuild has not
been demonstrated. Original unsigned and selected signed hashes are distinct.

The renderer build uses LLVM_MINGW, DXMT_RELEASE and WINEBUILD environment inputs;
see renderer/wave7/build.py. Run it from a writable disposable copy. The optional
GPU smoke fixture is compile-only here: set LLVM_MINGW to its bin directory, run
renderer/wave7/smoke/build.py and verify_imports.py. It is not in offline defaults.
The ntdll path probe requires an explicitly supplied disposable engine and is also
excluded from offline defaults. Never use full build recipes as an install script.

Source notices and licenses are retained. Root LICENSE, upstream notices and the
renderer component license govern their respective material. No license grant
for proprietary donor binaries is implied; those binaries are not distributed.

## Checked graphics successor

Final sources replace the Wave7 production trees; Wave6 remains historical. The
bridge recipe now requires BASELINE_NATIVE_DIR/winemac.so with the exact unsigned
baseline-v6 hash in WAVE7_PROVENANCE.json; the selected runtime uses the mask-fixed
driver. NATIVE_DEPENDENCY_DIR/ntdll.so retains the recorded donor hash. This
preserves actual link inputs; it does not claim a mask-linked rebuild occurred.
The native recipe compiles the full current tree, whereas the recorded private
mask build reused 23 unchanged objects and recompiled two.

Run new fixtures only in a disposable copy: renderer/wave7/presenter-tests/run.py,
native/wave7/checked-tests/run.py, native/wave7/mask-tests/test_mask_coordinates.py,
renderer/wave7/verify_consumer.py, and renderer/wave7/feedback-tests/{run_writer_tests.py,native-close/run.sh}.
Presenter accepts --sanitizer address or thread and explicit --mutation controls.
Mask tests use unittest; native/feedback fixture scripts generate local outputs.
Historical aggregate scripts remain available; this export did not rerun them.
Sanitized receipts are in evidence/checked-mask; original and derivative hashes
are recorded in WAVE7_ADAPTATIONS.json. They are historical execution evidence,
not a claim that public path adapters or clean-room builds were executed.

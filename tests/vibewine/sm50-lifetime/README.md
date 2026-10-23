# Public SM50 ownership regression

This CPU-only fixture compiles the actual `src/airconv/dxbc_converter.cpp`
translation unit and links an existing x86_64 LLVM 14 support build. The static
archive supplies unchanged converter/parser utilities, not the public-API TU
under test. It launches no renderer, game, Metal compiler, or installed runtime.

The port adapts [Feifan He's upstream commit 39625fa9](https://github.com/3Shain/dxmt/commit/39625fa98c9830ffb035d51a37e3590e2b8d4d9e)
(CodeWeavers, LGPL-2.1-or-later; see the component `LICENSE` and `COPYING.LIB`).
Only error/shader ownership is carried over. The newer IOHelper/VertexPuller
architecture and unrelated comment changes are excluded. Shader lowering,
AIRCONV API version 26, and cache epoch 2 are unchanged.

From the component root:

```sh
python3 tests/vibewine/sm50-lifetime/run.py \
  --llvm /path/to/llvm-darwin \
  --support-build /path/to/existing/build
python3 tests/vibewine/sm50-lifetime/run.py \
  --llvm /path/to/llvm-darwin \
  --support-build /path/to/existing/build \
  --baseline-ref 284f84ccbc3ec61fb5ea25744a951d3f1eabdeaa
```

The 22 cases cover null shader output, invalid DXBC, all five compilation
wrappers' null bitcode output, successful initialization and real compute-shader
compilation/destruction, a conversion error, and hull initialization rejected
for an oversized threadgroup after allocating the shader/control-flow graph.
Every case runs with both a supplied and omitted error output. Error strings and
published success objects are checked through the public API. C++ allocations
are counted across five repetitions after three identical warmup calls, so
process-lifetime LLVM registrations do not conceal per-call object leaks.

The baseline check compiles the same actual TU from the given pre-fix Git ref.
It requires the old success-path and partial-shader leaks and null-error-output
failures, while preserving passing explicit-error ownership controls. Null-output failure
requires UBSan exit status 86 and the exact null-store diagnostic and matching
summary location in `dxbc_converter.cpp`; arbitrary signals or unrelated
sanitizer findings are rejected. Success and leak controls require exact output
with no additional diagnostics. The fixed
implementation must pass every case with zero net outstanding C++ allocations.
UBSan also rejects undefined behavior. `run.py` creates one automatically removed
temporary build directory; no retained captures/logs or data producer is added.

Verified 2026-10-23 against the existing full-bundle support build and project
LLVM 14 dependency: all 22 fixed cases and all 22 baseline expectations passed.
This verifies the exercised public CPU API paths. It does not verify successful
tessellation/geometry pipeline conversion, every shader resource rejection,
GPU behavior, installed runtime behavior, or whole-project packaging.

# Narrow DXMT upstream backports, 2026-10-23

The integration branch starts from public component `e7b02196a31dc633ac802c9c27b88dbdd8f40dca`.
It selects two standalone upstream correctness fixes, preserving original author
attribution, LGPL/MIT notices and cherry-pick provenance. It does not merge the
concurrent DXIL/D3D12 feature work or alter the installed renderer/runtime.

- `ce001dfbedd7116aa9beb2e816c6ba44e599c15d`: decode hull-shader relative
  `OUTPUT_CONTROL_POINT_ID` indices and lower them to invocation id plus offset.
  The four affected files had no local changes against the upstream base.
  A formerly missing return on an unsupported relative register now returns zero
  in release builds. This fallback does not validate arbitrary malformed shaders.
- `a71b60ecc506484b964cba9ebd3f9fb2b2537e04`: acquire the existing device mutex
  in `WaitUntilGPUIdle`, `TemporalUpscale`, `BeginUAVOverlap` and `EndUAVOverlap`.
  Locking remains conditional on multithread protection; deferred contexts retain
  their no-op mutex. Existing recursive ownership permits already-locked callers.

Compiler output compatibility moves from local epoch 1 to 2, while upstream
`AIRCONV_VERSION` remains 26. See `compiler-cache-compatibility.md`. Existing
cache data is preserved; no producer, retention policy or cleanup is added.
Staging, dynamic-buffer and ring-retention fixes and native DS3 behavior remain
outside these selected source edits.

## Reproduce focused checks

From the component root:

```sh
python3 tests/vibewine/compile_regressions.py
python3 tests/vibewine/context-locks/run.py --sanitize
python3 tests/vibewine/context-locks/run.py --arch x86_64 --sanitize
python3 tests/vibewine/context-locks/run.py --source-ref e7b02196a31dc633ac802c9c27b88dbdd8f40dca --expect-unlocked-baseline --sanitize
python3 tests/vibewine/control-point-index/run.py --llvm /absolute/llvm14-prefix --support-build /absolute/existing-matched-build
python3 tests/vibewine/control-point-index/run.py --llvm /absolute/llvm14-prefix --support-build /absolute/existing-matched-build --baseline-ref e7b02196a31dc633ac802c9c27b88dbdd8f40dca
```

The control-point fixture compiles the actual changed parser and converter
translation units with release assertions disabled and UBSan enabled. The fixture
keeps its own checks enabled and inspects the resulting LLVM index and input-patch
GEP for offsets 0, 3 and 17. It also checks immediate, temporary, indexable temporary
and unsupported-relative decoding and verifies the LLVM module. The same fixture
compiles against the baseline sources and must fail specifically at the former
missing return. An existing x86_64 LLVM14 build supplies unchanged support objects
and static libraries; neither changed translation unit is taken from its archive.
This is a focused compiler test, not a freshly rebuilt matched renderer bundle.

The lock fixture extracts the full production entry bodies, temporal cache class,
recursive mutex implementation and no-op mutex. CPU resource/queue doubles assert
protected ownership before access. Contention is synchronized with an observed
spin-loop attempt, and cases cover recursive ownership, disabled protection,
cache hit/miss, temporal early return and deferred behavior. The baseline must
expose all four unlocked entries. ASan/UBSan check the fixture; it does not model
Metal scheduling or prove absence of every real-world lock cycle.

Focused checks and syntax-only compilation of the immediate and deferred D3D11
translation units passed on this Mac. A full newly matched compiler, PE libraries,
native bridge and Metal helper build remains a separate integration gate. No game,
GPU rendering, FPS, installed activation or compatibility claim follows from
these CPU/source checks.

## Follow-up: SM50 public API ownership

From component `284f84ccbc3ec61fb5ea25744a951d3f1eabdeaa`, adapt the ownership
part of Feifan He's [upstream `39625fa9`](https://github.com/3Shain/dxmt/commit/39625fa98c9830ffb035d51a37e3590e2b8d4d9e)
(`feifan@codeweavers.com`, original Signed-off-by retained as provenance).
The unrelated IOHelper/VertexPuller refactors in that upstream revision are not
transplanted into this older compiler architecture.

Error objects and partially initialized shaders now use `unique_ptr`; public
ownership transfers only on the documented output boundary. Successful calls no
longer leak their unused error object, error paths tolerate an omitted `ppError`,
and failed initialization releases a partial shader. All five compile wrappers
share this error-ownership rule. Public API 26 and compiler epoch 2 remain
unchanged: successful shader lowering/output has not changed in this follow-up.

```sh
python3 tests/vibewine/sm50-lifetime/run.py --llvm /absolute/llvm14-prefix --support-build /absolute/existing-matched-build
python3 tests/vibewine/sm50-lifetime/run.py --llvm /absolute/llvm14-prefix --support-build /absolute/existing-matched-build --baseline-ref 284f84ccbc3ec61fb5ea25744a951d3f1eabdeaa
```

The fixture compiles the complete changed converter translation unit with UBSan
and checks 22 public-API cases: real compute initialization/compilation/destruction,
partial hull rejection, conversion errors, null output and optional error output.
After warmup, allocation counters return to zero across repeated calls. The same
baseline compiles and must produce the exact expected leaks, null-output UBSan
diagnostics and unaffected ownership controls; arbitrary crashes cannot pass.
Retained LLVM14/static objects supply unchanged support code. Successful specialized
tessellation/geometry pipelines, a new full renderer build, real GPU rendering,
installed behavior and numerical FPS impact remain unverified.


## Neighbor follow-up, 27 October 2026

The donor scan moved from upstream `a148782d` to
[`3b252590`](https://github.com/3Shain/dxmt/commit/3b252590cb0290e36fca6f088b5255b871d2d95a),
26 commits ahead. The following are narrow adaptations, not a wholesale merge
of upstream D3D12/IOHelper architecture. Feifan He / CodeWeavers' original
LGPL-2.1-or-later notices are retained.

- [`9839e55f`](https://github.com/3Shain/dxmt/commit/9839e55f98d4aca103957ee4132d6395a325a9ed):
  snapshot a call continuation before replacing its owning variant. VibeWine
  additionally fixes conditional-call bookkeeping: record the actual constructed
  call in the true branch, preserving the condition and false continuation.
  [Actual-TU tests](../tests/vibewine/call-return-lifetime/README.md) parse nine
  DXBC programs, check owned CFG execution, and verify produced LLVM modules.
  The second build injects an overwrite before replacement, with unchanged type
  definitions; it is a fault-injection oracle, not a sanitizer diagnosis of
  variant destruction. The ordinary baseline independently rejects CALLC.
- [`d162ea44`](https://github.com/3Shain/dxmt/commit/d162ea44a31fbf9089ea09977337f0beb30bd082):
  POINTLIST geometry emits point-size metadata and size1, and counts emitted
  vertices as point primitives. [GS tests](../tests/vibewine/gs-point-output/README.md)
  rebuild the actual converter TU and verify LLVM before/after optimization;
  three baseline point oracles fail while line/triangle controls pass.
- [`a741a3fa`](https://github.com/3Shain/dxmt/commit/a741a3fad14a4bc5fc899f85bfa35de8f933e74b):
  release copied ColorSync tag data. VibeWine extends cleanup to the donor's
  missed invalid-signature exit. Surrounding profile/fallback ownership is
  already covered by our display snapshot and is not imported twice.
  [CoreFoundation tests](../tests/vibewine/chromaticity-lifetime/README.md)
  exercise real CFData ownership, malformed tags and unchanged outputs. They
  extract the complete reader; full native-TU coverage belongs to the matched
  release build, not to this fixture alone.

Shader-output compatibility epochs advance from2/3 to4/5 (ordinary/cleanup),
keeping AIRCONV API26 unchanged. The shader database and PSO archive share this
boundary; previously compiled output is never silently reused across it. No
installed cache is deleted or migrated. There is no new title check, runtime
flag, collector or background producer in these adaptations. CPU/IR verification
and complete build checks do not establish GPU behavior, DS3 compatibility or FPS.

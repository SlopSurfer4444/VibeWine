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

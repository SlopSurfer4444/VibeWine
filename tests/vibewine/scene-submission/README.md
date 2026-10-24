# Admission-preserving scene submission experiment

`-Dvibewine_scene_submission=true` enables a build-only D3D11 experiment.
It defaults to **false** and defines `DXMT_SCENE_SUBMISSION=0/1` consistently
for PE and native compilation. It does not alter ABI, compiler/cache versions,
queue depth, retention settings or game configuration.

After valid presentation configuration, `Present1` obtains its existing
`SyncFrame` admission once. When enabled, it then invokes the existing
dirty/promote-gated `Flush`, committing ready rendering before a potentially
blocking drawable acquisition. It reacquires `CurrentChunk` afterward. Both
plain and MetalFX paths move the admission token into the final presentation
lambda; only that chunk gets the frame-latency fence. Logical frame advancement
and `PresentBoundary` still occur once. With the option disabled, the original
single-chunk submission grouping remains.

This uses the existing ordered queue, resource tracking, cross-chunk event/fence
machinery, readback publication and CPU-coherence retirement. The scene chunk
does not release final frame admission. The token stays with final presentation
through encode and retirement, including missing-drawable handling. Invalid
configuration, invalid arguments, test Present and occluded paths retain their
existing admission/submission behavior. Resize and Present continue under the
same device/buffer locks. No new thread or deferred ownership subsystem exists.

Both submissions are attributed to the existing `Present` commit reason, rather
than falsely identifying the internal scene submission as an application's
explicit Flush. The scene admission/commit precedes event 25's final-frame
binding; the final chunk follows it. Consumers must retain chunk sequence IDs
and the shared logical frame identity rather than assuming one chunk per frame.
Capture boundaries remain logical frame boundaries.

## CPU source regression

```sh
python3 -B tests/vibewine/scene-submission/run.py
python3 -B tests/vibewine/scene-submission/run.py --arch x86_64
```

The runner extracts **whole production methods verbatim**: `Present1`,
`SyncFrame`, `SyncFrameState`, `Flush`, `Flush1`, `PrepareFlush` and `Commit`.
Observable queue/window/Metal adapters make submission order and move-only
token lifetime checkable. The adapters do not establish Metal execution,
hazard correctness, native presentation, actual capture completeness, real
resize/exit behavior or performance.

Each accepted variant runs 56 scenarios across both constexpr presentation
paths, dirty/promote/no-work cases, valid/invalid configuration, null backbuffer,
occlusion, test Present, non-waitable admission and invalid SyncInterval.
Default-undefined, explicit-off and enabled variants must pass. The historical
source `236089329156243f6c3777b4114be38b50bb4301` with the enabled define must
fail the split contract; `--baseline-ref` selects another retained equivalent.
Four connected production mutations must also fail: dropping the final token,
flushing before admission, unconditional empty submission and tagging the scene
chunk with final frame completion. Failures must be behavioral assertions,
not compile or sanitizer failures. ASan/UBSan cover every executable variant.

Generated sources and binaries live in one private temporary directory removed
on exit. Compilation has a 60-second timeout per variant and execution 20 seconds.
No Wine/game/GPU/window is launched, and no installed runtime is touched.
The output includes production source hashes and exact negative results.

## Remaining acceptance

The separate GPU fixture and matched build must exercise render-to-present
dependencies with untracked resources, readback/query visibility, failure paths
and retained ownership. Real-game adoption additionally requires valid warm
fixed-scene baseline/candidate/reversal captures, observer-overhead controls,
actual displayed intervals and latency/memory checks. Extra chunks change
sequence-based allocator aging and may add submission overhead. Long drawable
waits alone establish neither exposed GPU idle nor an FPS improvement.
`compile-waits` excludes the necessary frame/drawable/GPU events.

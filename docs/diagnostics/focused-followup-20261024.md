# Exit and frame-hitch follow-up, 24 October 2026

Wine remains `e20af58312ef059e54f8d16767ee7b74cb8358e5`. The new DXMT
build source is `356cc60d500f11d59dd2cb215250b2a08b07e8ba`, following the
previous `828429ca` matched graphics build. These are component/build identities;
they do not claim that every Wine feature was rebuilt or every game was tested.

## General native cleanup repair

During process shutdown, `NSObject_release` now checks the actual NTDLL
`RtlDllShutdownInProgress` flag before crossing into Objective-C cleanup.
Sibling threads may already have stopped while owning host locks. The kernel
reclaims those terminal process resources; ordinary release and explicit DLL
unload still perform their native ownership work. This changes no game executable,
exception policy, exit status or production timeout. Destructor-hidden terminal
side effects are skipped, so required persistence must precede process shutdown.

The native production-thunk regression reproduced the old lock failure and
verified the candidate on arm64 and x86_64/Rosetta. A separate PE lifecycle
probe used real NTDLL shutdown state: normal/unload paths entered the bridge,
terminal detach omitted it, and natural exit status 20 was preserved. The old
terminal control failed the ownership assertion. Exact real-header compilation
also catches the missing MinGW declaration that the initial fixture seam hid.

The default release build passed draw, MRT/depth, compute, MSAA and later-chunk
query tests with actual candidate module mappings; three deliberately broken
GPU controls were rejected. These establish covered correctness, not a game
freeze cure. Actual installed DS3 exit/relaunch remains a separate gate.

## Installation checkpoint

The selected 22-file Wine/graphics closure was promoted through the existing
matched-runtime transaction, with prior recovery retained. Fresh installed
hash readback matched, and repeating the operation made no changes. The ordinary
launcher uses that installed closure. The public DXMT equivalent is
`14e3a6a88288b8b69ff84b0b8c69c8c2af663385`; the
[tree-equality map](../provenance/focused-publication-20261024.json) binds it to
the actual build source. No real-game exit/relaunch result on this follow-up is
claimed yet. Packaging did not replace game files, saves or caches.

## Diagnostic changes

- `DXMT_CS2_TRACE_PROFILE=compile-waits` selects the existing compiler, readiness,
  archive and bounded persistence probes without the high-rate draw/Map stream.
  Event 95 explicitly identifies coverage; invalid profiles fail closed. Default
  tracing, v2 layout, writer quiescence and lifetime caps remain. See the
  [telemetry contract](../../tools/telemetry/README.md).
- [Phase annotations](../../tools/telemetry/SESSION_PHASES.md) bind actual
  observations to process birth and native Metal clocks. Unknown scenes stay
  unknown; aggregate intervals crossing boundaries are not fractionally invented.
  Memory summaries distinguish process counters from host paging.
- The temporary collection adapter disables automatic stack sampling by default
  and timestamps copied log-byte ranges. A nominal one-second native sample in
  the previous run lived for over eight seconds and overlapped late hitches.
  Source/adapter tests and activation are reported separately; the revised
  collection adapter is packaged but has not yet been activated in a new game
  session.

The installed launcher does not yet expose `compile-waits`; setting an external
shell variable does not activate it through the launcher’s fixed environment.
The producer/decoder feature and supported live activation are separate gates.

The maintained collector/analyzer passed 178 offline checks including the new
profile and phase cases. They do not establish live labels or instrumentation
overhead. The previous roughly four-minute, unlabeled 1080p frame stream is an
uncontrolled observation, not a clean gameplay benchmark or numerical FPS gain.
Active host paging and CPU-side latency are leads; compilation and a renderer
memory leak have not been established as the cause. No speculative cache eviction
or compiler-concurrency default is promoted by this follow-up.

## Ownership and scope

These engine repairs and diagnostic policies have no DS3 image check. DS3 startup
and resolution policy stays in its existing profile. The private delivery adapter
also corrects current-helper versus historical-native endpoint validation while
retaining every historical image edge; it is an installation lifecycle repair,
not a graphics optimization. Runtime installation is verified separately from
actual application launch,
real game exit/relaunch and labeled cold/warm comparison.

No new permanent service, cache purge or cleaner is introduced. Capture scopes
reuse seven-day raw retention, a sampled 256 MiB capture cap and 30-day/4 MiB
curated summaries, with active, analysis and recovery holds. Profile filtering
does not expand these bounds. Public notes omit private bindings and raw traces.

## Next FPS direction: admission-preserving early scene submission

Selected on 24 October after an independent local source review and a parallel
ChatGPT Pro consultation against workspace `14a04675`, Wine `e20af583` and DXMT
`23608932`. The source/build experiment below was subsequently implemented;
game performance acceptance and the earlier follow-up remain open.

### Implementation and verification checkpoint, 25 October

DXMT `91add796` adds the build option `vibewine_scene_submission`, default
**false**. It preserves the existing frame admission before the optional
dirty/promote-gated Flush, reacquires the final chunk afterward, and moves the
admission token into either final presentation callback. The bounded ring,
logical frame advancement, final latency fence and retirement machinery are
unchanged. Both chunks use the existing Present reason; the scene commit
precedes event 25's final-frame binding. This is a general D3D11 experiment,
without an executable-name or game-profile check.

- Whole-production-method extraction passes 336 accepted scenarios across
  arm64 and x86_64/Rosetta with ASan/UBSan; the old source and four connected
  admission/ownership/grouping mutations are rejected on both architectures.
- The separate native Metal discriminator passes eight cases per variant.
  With its injected acquisition gate held, reference scene data stays untouched;
  the split variant completes actual GPU work before the gate opens. Final
  admission is retained through scene retirement. Immediate acquisition and
  extracted nil-fallback fence operations also pass. No-Flush and omitted-fence
  source mutations are independently rejected with their expected natural exits.
- Both release PE/native builds use source `91add796`, the same dependencies
  and `vibewine_perf_reference=none`; actual Meson options differ only in the
  scene-submission boolean. Both matched closures pass all five offscreen
  D3D11 GPU stages with independently observed module mappings and natural exit.

The native discriminator has queue/presenter boundary adapters. It does not
execute the production queue worker, actual CAMetalDrawable acquisition, scaler,
hazard optimizer or query retirement. The offscreen D3D11 guest establishes
covered rendering/closure compatibility, not the new swapchain path. Neither
fixture proves exposed idle or an FPS gain in a game. See the
[CPU regression](../../components/dxmt/tests/vibewine/scene-submission/README.md)
and [native discriminator](../../components/dxmt/tests/vibewine/scene-submission/gpu/README.md).

A baseline attempt used the unchanged installed runtime with the bounded
per-frame observer armed before launch and automatic stack sampling disabled.
DS3 launched and accepted visually verified title/menu commands, but repeatedly
displayed connection-loss dialogs and returned to the title screen. Some subsequent
poll-acknowledged commands did not visibly advance the intended menu. No stable
offline world or gameplay benchmark was accepted; the stream must not be
reported as gameplay or as a before/after comparison. macOS Quit ended the
observed game process, then the owned test prefix and observer were stopped.
No experimental game runtime was installed or enabled by this checkpoint.

The switch stays off until a matched, labeled game comparison satisfies the
causality, latency, memory and stop criteria below. The current playable build,
game files, saves, shader caches, VPN and unrelated sessions are preserved.

The current queue commits a Metal command buffer only after `chunk.encode()`
returns (`dxmt_command_queue.cpp:180–187`). Encoding reaches the synchronous
`nextDrawable()` inside `Presenter::encodeCommands()` (`dxmt_presenter.cpp:360–363`).
`PrepareFlush()` closes the pass without submitting; Present then joins the same
chunk before `Commit()` (`d3d11_context_imm.cpp:535–537`,
`d3d11_swapchain.cpp:985,1012–1045`). Thus drawable acquisition can delay scene
commands that could otherwise be submitted. Exposed GPU idle has not been proven.

The first implementation slice should be an opt-in split using the existing
queue: admit one logical frame, submit its ready scene, then encode/submit its
final presentation. Keep `SyncFrame()` admission before the early submission,
move the existing `SyncFrameState` RAII token into the final presentation
chunk, advance the logical frame once and retain that chunk's latency and
completion boundary. Both chunks share the same logical frame/capture identity. A naked additional Flush before admission is not the
candidate: it can change frames in flight and manufacture a misleading gain.
Preserve command/resource ordering, retained backbuffer/configuration identities,
query/readback-before-coherence, missing-drawable completion, resize and shutdown.
The existing bounded chunk ring must remain bounded. Submit at most one extra
nonempty scene chunk per successful Present; retain dirty/promote gating and
keep MetalFX disabled for the first comparison. A chunk-count cap is not a strict
byte cap, so resource footprint remains an independent check.

A discriminating baseline needs a valid clock join showing ready-but-uncommitted
scene work overlapping exposed GPU idle during drawable acquisition. Long
`nextDrawable()` alone can be healthy display backpressure. Use the existing
chunk/drawable/GPU/presentation events in short bounded captures, splitting replay
from native flush if needed. **Do not use `compile-waits`: it excludes these
events and is not yet exposed by the installed launcher's fixed environment.**
No new monitoring platform or unlimited trace is required.

Compare alternating baseline/candidate/reversal runs in a visible, verified warm
scene at identical render/output resolution, camera, settings, presentation policy,
cache treatment, power and background load. Control observer overhead. Require a
repeatable improvement in displayed frame intervals/p95/p99 or long-frame counts
beyond run variation, without increased input latency, logical queue depth or
memory. Stop if GPU was already busy, the wait merely moves, or extra submissions
cost more. Additional chunks also change sequence-based allocator aging: inspect
allocation churn and peak bytes rather than attributing all effects to overlap.

Useful checked precedents are [Apple's drawable guidance](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/Drawables.html)
and [DXVK's flush-before-image-acquisition design](https://github.com/doitsujin/dxvk/blob/master/src/d3d11/d3d11_swapchain.cpp).
These inform the design; Vulkan code is not a drop-in Metal patch.
The next-ranked CPU candidate is precise binding invalidation after
WRITE_DISCARD, starting with constant-buffer stage/slot dependencies. Current
immediate/deferred paths broadly mark stages dirty; resource-table materialization
must remain separate from mandatory access, residency and UAV-counter tracking.
A stable view pointer does not prove an unchanged counter allocation. This is a
fallback if submission overlap is falsified, not another part of the first patch.
Existing [Metal residency-set primitives](https://developer.apple.com/documentation/metal/mtlresidencyset)
are another reusable foundation if residency CPU cost is measured, but do not
replace hazard tracking or justify indefinite pinning.

Before promotion, exercise delayed/nil drawable, cross-chunk query and Map
coherence, resource replacement, resize/occlusion, failure and normal exit/relaunch.
A controlled native GPU fixture should distinguish actual scene completion
while acquisition is delayed from merely recording an early CPU submit call.

Low-render/fixed-output spatial MetalFX remains a separate quality/performance
experiment for a demonstrated GPU-bound case, not a native-resolution gain.

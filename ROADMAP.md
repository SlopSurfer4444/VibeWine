# VibeWine roadmap

This is a direction for contributions, not a release promise. Start with the
[component source](README.md#get-the-source) and [contribution guide](CONTRIBUTING.md).

## Active integrated wave — 23 October 2026

### Focused follow-up — 24 October 2026

The owner accepted a focused exit, frame-hitch and scene-metrics pass after a
play session that felt usable under background load. Keep the current playable
runtime as the comparison/recovery candidate; saves, caches, VPN and unrelated
applications remain outside the change. Root owns integration, Git indexes and
live installation. The older W8 checkpoint remains open.

| ID | Required result | Current evidence / next acceptance |
| --- | --- | --- |
| F1 | Terminal native cleanup avoids the observed host-lock path while preserving ordinary release and library unload | Source/build/install accepted: process-detach-aware Metal release passes native old-fail/new-pass, real NTDLL normal/unload/terminal controls and matched GPU regressions; selected closure installed with hash readback/no-op; real game exit/relaunch remains pending |
| F2 | Genuine menu/loading/gameplay/exit labels and complete, low-perturbation recording | Source accepted: PID/birth-bound phase CLI and bounded offline joins pass 26 focused checks and genuine saved-stream replay; collector default sampling disabled with 19 local adapter tests. New live phase labels remain pending |
| F3 | Attribute CPU/wait/paging hitches before changing compiler/cache policy | Source/build accepted: opt-in compile/wait profile excludes high-rate draw/Map events, names its coverage and retains writer/generation bounds. 178 telemetry checks pass. Host paging/CPU latency observed; labeled cold/warm attribution and numerical gain remain pending |

Accept this follow-up only against the changed source, focused negative controls,
matched build and applicable runtime checks. No game-only workaround or FPS gain
is implied by source tests. Keep any unmet live comparison visibly pending.

### Selected next FPS experiment — 24 October 2026

Prioritize **admission-preserving early scene submission**: use the existing
bounded queue to submit ready scene work before a blocking drawable acquisition,
while retaining one logical frame admission and final presentation completion.
Independent local review and ChatGPT Pro converged on this source-backed
hypothesis. The opt-in implementation is committed as DXMT `91add796`, default
off. Production-method admission/lifetime regressions, the bounded native Metal
held-acquisition discriminator and both full matched builds pass; both matched
PE/native variants pass all five offscreen GPU stages. The discriminator proves
GPU work can complete during its injected acquisition gate; its queue boundary
is an adapter, not the production worker or a displayed-FPS test. Real-game
causal capture and controlled improvement remain pending. Require exposed GPU
idle/late submission evidence and repeatable displayed-frame improvement without
queue-depth, input-latency or memory regression. Extra chunks change allocation
aging, which must be measured. See the [source path, controls and stop criteria](docs/diagnostics/focused-followup-20261024.md#next-fps-direction-admission-preserving-early-scene-submission).
The first live baseline did not reach a stable offline world: connection-loss
UI and inconsistent menu transitions prevented an accepted gameplay interval.
The unchanged game ended via macOS Quit; the task-owned prefix/observer stopped.
No experimental runtime was installed, no FPS improvement is claimed, and the
current playable closure remains selected. Source implementation is complete;
game performance acceptance remains open.
This direction does not close F1–F3 or make the optional experiments mandatory.

The owner requested maximum useful macOS integration, frame-time improvement and
autonomous DS3 testing in one end-to-end wave. This section is the current shared
plan, authorized by the owner's subsequent go-ahead. Activation is not an
installation receipt. The
research checkpoint is hub `8a76bfb5`, Wine `bee453b09460e0c3eb49b7c491a6d04e65cc38a5`
and DXMT `fab325e0adcd639d822dbfac87a1b840ccb2d4bf`. The
[independent findings](docs/integrations/independent-performance-review-20261023.md)
and the completed external architecture consultation informed selection. External
advice is not a test result or permission to mutate a working runtime.

**Terminal checkpoint:** one identified, source-matched candidate passes rendering
and lifetime regressions, launches through the ordinary application route,
reaches a visually verified offline world, completes a bounded benchmark,
changes supported resolution/window modes, exits normally and launches again
with Steam left open. Publish measured effects and their limits, exact patch
provenance and a recovery path. Preserve working installation, saves, VPN,
RustDesk and unrelated sessions while developing it. Installation and gameplay
remain separate acceptance phases; source success does not satisfy them.

One wave can contain several independently reviewable patches and controlled
variants. Shared rendering/ABI, persistence ownership and native lifecycle have
one integration owner. Tests on the same GPU run sequentially; background builds
and analysis stop during timed comparisons. An experimental branch is not an
implicit prerequisite for the verified core candidate.

| ID | Deliverable and consumer | Prerequisites | State / admission |
| --- | --- | --- | --- |
| W0 | Bind source, dependencies, overlay provenance, bundle identity and a reproducible reference; integration owner | Current source/provenance inventory | Source/build checkpoint accepted: Wine e20af583, terminal SIGQUIT race covered, scoped overlays reconciled, affected x64/i386 modules and server linked; full production-feature Wine build not claimed |
| W1 | Discriminating bounded telemetry and displayed-observer admission; performance integrator | W0 for live acceptance | Source accepted: bounded detailed events and actual SQLite status; 144 combined telemetry tests pass; live displayed-observer admission pending |
| W2 | Query publication and teardown correctness; renderer integrator | Current source; W0/W1 for runtime acceptance | Source/GPU accepted for covered cases: publication and empty-chunk completion repaired, Windows retirement drains before destruction; ASan/TSAN controls and real empty-chunk query pass; game lifecycle pending |
| W3 | Shader readiness, bounded persistence and backlog-aware compiler scheduling; renderer integrator | Shared lifetime/shutdown contract; W1/W2 before final acceptance | Source/build accepted: bounded persistence, real cache status and opt-in backlog growth; ownership/failure fixtures, native SQLite and full linking pass; game/performance acceptance pending |
| W4 | Descriptor getter hoisting, render/compute setup and final-pass batching; renderer integrator | Existing synchronous command-list ABI; W1 for performance attribution | Source/GPU accepted for covered cases: draw/MRT-depth/compute/MSAA pass with all matched mappings; displayed Present performance remains pending |
| W5 | Native geometry, focus/cursor, fullscreen/exit/relaunch regressions and repairs; native integrator | W0 and agreed view/swapchain ownership; W2/W4 for combined rendering tests | Source/build accepted: HDR validation and shared fullscreen capture generations; real mode/focus/cursor/exit checks pending |
| W6 | Autonomous load/scene/control/benchmark/exit adapter; test integrator | Existing CUA guards and explicitly admitted process-local input; W0/W1/W5 for live acceptance | Source accepted: finite lifecycle, guarded Quit and actual timed Node/Python/native collector glue; 50 JS,30 controller,13 executor tests pass; live scene/display evidence pending |
| W7 | Bounded MSync registration-ACK repair; Wine integrator | Paired client/server protocol and cleanup ownership | Source/build accepted: paired ACK park/wake, source/native controls and isolated paired runtime progress pass; matched server delivery transaction under implementation |
| W8 | Matched build, native GPU regression, controlled DS3 comparisons, ordinary-launch promotion and publication; integration owner | W0–W7 accepted, and no material unresolved failures | Running: matched fresh build and all five offscreen GPU stages/three negative controls pass; root owns schema12 transaction, ordinary live acceptance, measurements and publication |

### Core implementation scope

- **W1:** add enqueue/start/dependency-ready/persistence events, task links,
  runnable depth/worker count, cache lock/write results, and completion-to-readback
  and coherence intervals. Reuse current identities, clocks, generation gates and
  record caps. Distinguish game scene, menus/loading, thermal/power state and
  memory pressure. Compare observer disabled/unarmed/armed with an independent
  observer; screenshots and AX polling stay outside timed sections. Missing or
  invalid presented timestamps remain unknown rather than CPU-derived FPS.
- **W2:** synchronize all visibility-query state accessed by encoder, finish and
  API threads, and publish timestamp results safely. Cover pending/ready, reuse,
  overlapping queries and concurrent polling. Do not introduce finish-thread
  acquisition of a device mutex that a waiting API caller may hold. Exercise
  device release with queued callbacks before claiming the teardown hypothesis
  fixed. Keep required GPU readbacks ahead of CPU-coherence publication.
- **W3:** create functions/pipelines and publish readiness before optional cache
  persistence. Reuse existing detached ownership, with owned keys/data and no
  dangling shader/pipeline references. A bounded persistence worker must not
  consume compiler capacity. Preserve one SQLite writer, cache compatibility
  keys, existing entries and miss/failure fallback. Handle busy/corrupt/full-queue
  cases without blocking rendering. Backlog-aware worker growth must consider
  bursts and continuations; thread-cap tuning remains separately switchable.
- **W4:** hoist repeated attachment getters and batch existing command nodes,
  without a new transport or ABI. Preserve all setters, fences and stage masks,
  stack-payload lifetime, null-drawable completion, immutable queued gamma/config,
  actual acquired drawable size and resource ownership. Cover MRT, depth/stencil,
  MSAA/resolve and geometry/tessellation/stream-output bindings. Large descriptor
  caching or shader-compiler refactors are not implied by these local changes.
- **W5:** test Win32 size, Cocoa points, backing pixels, render resolution and
  drawable output independently, with symmetric input/clip transforms. Exercise
  supported resolution changes, windowed/borderless/native fullscreen, resize,
  Mission Control and focus loss, controller reconnect, normal Quit, crash and
  repeat launch. Reproduce Steam popup-dismissal/quit/session-ownership failures
  where still present. Keep game quirks in profiles; no global false-foreground
  policy. The current unconditional `SetHDRMetaData()` success deserves a separate
  compatibility check: do not equate silent success with functional HDR.
- **W6:** extend `tools/gameplay` and `profiles/ds3`, rather than create another
  automation platform. Reconcile the exact process/window; classify notices and
  online errors; load offline; verify HUD/world and a safe action; warm up; run a
  stationary capture first, then a verified finite out-and-back route; verify end
  scene and neutral input; exit through the game's menu and relaunch. Add bounded
  cancellation, input expiry and phase deadlines. Do not create/delete characters,
  spend items/souls, overwrite/restore saves or loop blind confirmations. Moving a
  game on another Space and observing displayed cadence are separate acceptance
  cases, not interchangeable evidence.
- **W7:** adapt bounded ACK spinning plus server-assisted parking together,
  retaining current early-unregister fixes. Test acknowledgement-before-park,
  park-before-ack, delayed server, deadlines, interrupts, cancellation and process
  death. No speculative direct-object spin defaults or wholesale protocol merge.

Any new queue, cache writer or collector must declare owner, exact namespace,
item/byte bounds, TTL, overflow behavior, shutdown/drain and cleanup/hold policy
in its existing component contract before activation. Retain the current archive
disk/entry limits unless a separately justified change is accepted. Opaque Metal
object counts are not an exact memory-byte bound. Do not purge an owner's caches
or reset a live trace to create a clean result.

### Optional experiments within the same wave

These have explicit deliverables/admission gates; none is automatically enabled
in W8 or blocks shipping the accepted core.

| ID | Experiment | Admission and useful outcome |
| --- | --- | --- |
| X1 | Fixed-target spatial MetalFX: native, lower-resolution ordinary scale and lower-resolution MetalFX | Actual GPU encoding, image/geometry/color correctness and scaler cost at the same final output. An opt-in quality/performance mode, separate from native-resolution gains |
| X2 | Native arm64 wineserver built from our matching source | Guest-architecture reporting, per-client Rosetta detection, identical protocol, dependency slices and isolated x64-client stress. Compare server CPU/latency; no claim that the x64 game becomes ARM-native |
| X3 | Atomic server-owned WaitAll and required grant-publication repairs | Coupled shared-state/protocol proof, no phantom grants, timeout/APC/abandoned-mutex/death tests. Correctness and contention experiment, not a default speed toggle |
| X4 | Chunk submission or completed-resource retirement tuning | Admit a change only after W1 identifies a critical-path cost. Preserve Flush/query progress, required readbacks, bounded memory and input latency; existing allocator-retention fixes stay intact |
| X5 | Network failure localization using existing metadata diagnostics | Reconcile overlay source; capture socket identity, DNS/connect/readiness/error transitions without payload. Separate offline FPS runs; no VPN/SIP change and no guaranteed server-side repair |
| X6 | Cross-process top-level GPU surface prototype for a real compatibility case | First establish a target failure; public sharing APIs, bounded slots, attachment acknowledgment and peer-death/resize lifetime. No CPU-readback transport or mandatory private-API presenter |

Temporal reconstruction/frame generation requires a separate source of correct
motion/depth/jitter/history and image-quality acceptance. It is not satisfied by
an existing color-only swapchain hook. Wholesale donor presenters, private cursor
APIs and a rewrite to a different language are not default requirements for this
wave.

### End-to-end acceptance and attribution

1. Accept source regressions first, including old-fail/new-pass controls,
   sanitizers, cache failure/queue saturation and shutdown/lifetime cases.
2. Build one matched candidate and a corrected reference from the same source
   base. Retain exact compiler/PE/native/helper and loaded-module identities.
   Include already accepted development backports once; do not credit them again
   as new findings or mix old/new bridge bytes.
3. Exercise native GPU correctness and window/input lifecycle, then the complete
   ordinary-launch offline DS3 cycle twice with Steam left open between runs.
   Unexpected scene, unavailable input/window or overflow invalidates the run.
4. Compare independent performance switches in the same candidate/base, followed
   by the combined result. Alternate repeated reference/candidate runs at fixed
   scene/camera, settings, render/output size, cache treatment and power/thermal
   conditions. Separate cold and warm caches and verify instrumentation overhead.
   Report covered intervals, p95/p99, long-frame counts, CPU/GPU work and memory;
   support every claimed gain with the appropriate observation.
5. Promote only accepted options into the ordinary launch route. Retain a known
   working rollback, document unresolved limitations, and publish source pins,
   patch authors/licenses, build instructions and measurements. A docs commit,
   successful build or launch receipt alone cannot close this plan.

## Make the project easier to build and review

- Establish an independently verifiable upstream baseline and dependency pins.
  The retained Wine archive identity does not substitute for a verified Git revision.
- Validate clean-room component builds and combined runtime assembly from the pinned workspace.
- Preserve component licenses and attribution; keep private deployment tooling out.
- Add CI only for fixtures that can genuinely run in its supported environment.
  Current local test results are not CI coverage.

## Native macOS behavior

- Reproduce and fix cursor confinement across Mission Control and focus changes.
- Exercise fullscreen, resolution changes, normal exit, crash recovery and relaunch.
- Expand accessibility from isolated standard-control fixtures to stable real-app
  discovery and input. Keep visual interaction distinct from semantic AX support.
- Check Retina and child-window geometry on additional applications and displays.

## Performance with evidence

- Compare identical scenes, resolution, settings and cold/warm cache state.
- Separate shader compilation, CPU work, GPU waits, frame presentation and thermal
  effects before changing code. Report frame-time distribution, not only average FPS.
- Validate purported general improvements on a second application where feasible.

## Compatibility profiles

- Keep title detection and compatibility policy outside general algorithms.
- Document why each exception exists, how it is selected/disabled, and which
  versions were tested. Test both profile-enabled and default behavior.
- Remove exceptions when the underlying defect is fixed or the title no longer
  requires them. Avoid silently expanding a workaround to every application.

DS3 is the initial test case, not the project's API contract. No claim is made
that the existing profile is a finished generic profile-management system.

### Matched native checkpoint, 2026-10-23

Wine e20af583 / DXMT 828429ca selected 22-file closure built with FreeType restored
and the installed relative library paths preserved. Native font positive/blank
controls, 32 PE shutdown runs and all five GPU stages plus three negative controls
passed with actual mapping/hash checks. The shutdown reentry follow-up has
old-fail/new-pass real-signal coverage in both terminal thread paths. W8 is still
open: schema12 source transaction is reviewed/committed separately; canonical
installation, real game lifecycle, fixed-window presented measurements and
publication remain required. Installed epoch1 remains untouched at this point.

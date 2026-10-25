# Presentation and compiler follow-up — 25 October 2026

DXMT source `96940d73` extends `e1772a42`; Wine remains `5d4b43e4`.
This separates inexpensive general work reduction, opt-in performance experiments
and diagnostic evidence. None is a DS3 executable-specific patch. The installed
ten-switch renderer/MetalFX launcher is unchanged. No game was launched here.

## Implemented changes

| Change | Contract | Evidence and limits |
| --- | --- | --- |
| Reuse source dimensions in Present | Read each immutable backbuffer extent once; retain actual acquired drawable geometry, viewport and pipeline decisions | Saves up to two native property calls per successful Present. Whole production method/actual wrappers preserve prior commands, failures, bars and gamma. Small work reduction, not measured FPS |
| Compiler concurrency budget | `d3d11.maxCompilerThreads=0` preserves legacy behavior; positive values cap startup/growth per pipeline cache, including serial compilation | Actual scheduler saturation/dependencies/continuations/shutdown tested in both growth/scheduling modes, sanitizers and x86_64. No profile enables it. Cache writers/other threads are outside the cap; lower concurrency may increase needed-shader latency |
| Bounded pass-optimizer search | Default-off `vibewine_bounded_encoder_optimizer`; at most 64 physical successor positions per eligible encoder, including Null slots | At 1,024 independent renders, relation calls fall from 523,776 to 63,456. Historical/default/off receipts match; partial folds, resolves, fences and barriers covered. Fewer optional merges may increase GPU cost; no GPU/game gain follows |
| Sampled native visibility | Only active `presentation-waits`; existing property transport, asynchronous main-owned snapshots, 500 ms cadence | Window/display/layer-lifetime identities, active Space, occlusion/hidden flags, age and caller revision. Unknown/stale/missing/loss/conflicting identities stay unusable. No skip-policy or displayed-frame semantics change |

See [compiler](../../components/dxmt/tests/vibewine/compiler-budget/README.md),
[optimizer](../../components/dxmt/tests/vibewine/encoder-optimizer-lookahead/README.md),
[presentation](../../components/dxmt/tests/vibewine/encoder-marshalling/README.md)
and [visibility](../../components/dxmt/docs/presentation-visibility-20261025.md)
contracts and test commands.

## Checks and provenance

Original bounded adaptations of existing code; no donor implementation/license
import. An independent ChatGPT Pro source audit of hub `e921dadc` / DXMT
`e1772a42` confirmed existing batching and identified optional quadratic search,
compiler contention and possible ordinary-pass setup/draw joining. Local source
inspection/tests determined implementation; that report is a hypothesis source,
not test evidence. Setup/draw joining remains deferred: GS/TS marshalling and
barriers split those streams, and real workload multiplicity/cost is unmeasured.
No query-driven submission change or cache-lock removal was made.

Integrated telemetry passes 197 offline tests. Native visibility/occlusion pass
actual production-code fixtures with controlled Foundation/main-ownership seams.
The production Present method passes default/optimized/reference variants,
ASan/UBSan, x86_64 compilation and 11 hostile controls. Compiler saturation passes
ASan/UBSan, TSan and x86_64. Optimizer checks cover historical/default/off/on and
combined dead-store/residency modes plus three connected wrong-action controls.
Independent review caught and repaired contradictory repeated visibility
identities: both decoders now reject immutable-payload disagreements while
permitting different age/status/caller revisions.

These are source/CPU/native-seam checks, not live WindowServer behavior,
encoder emission/readbacks or sustained game timing. Both fresh complete
161-target matched release builds pass, with eleven experiment switches all
off/on. PE/native definitions match; compiler, PE graphics, native bridge and
Metal helper were built together using retained LLVM 15.0.7 and the configured
Wine scaffold. This is not a complete new Wine distribution. Exact source/tree
and unsigned artifact hashes are in the [build record](../provenance/presentation-followup-20261025.json).
Both new variants also pass five offscreen D3D11 GPU stages (basic draw,
MRT/depth, compute, four-sample MSAA and queries across chunks), 172 checks each.
A connected pixel corruption control on the enabled variant is rejected with
exit 20. Each run verifies the actual process birth, seven loaded PE/native
libraries, unchanged candidate hashes and isolated-server cleanup. The sandbox
denies game/account/home and network access; the lab was restored to its exact
prior hashes. These small workloads do not validate long lookahead cutoffs,
actual displayed Present/MetalFX, visibility or game performance.

## Next measurement

First bind the visible game window to the measured layer and demonstrate usable
native presented-time coverage. Visibility snapshots never supply missing
presented frames. All-skipped records remain missing displayed-FPS coverage.
The repaired private live watcher and public `session_phases.py` are separate
paths: the latter still rejects aggregates over five seconds; it was not fixed
here.

Alternate equal-scene A/A and A/B runs with fixed render/output resolution,
power/load and cache state. Compare compiler caps independently from optimizer
selection; separate cold compilation from warm gameplay. Count actual passes,
relation checks, worker occupancy and critical-path waits, then compare frame
tails, latency, memory and GPU cost. Reject a smaller CPU counter if extra passes
or later demanded shaders worsen the frame. Equal-output MetalFX comparison
needs native 1440p versus lower render resolution upscaled to 1440p.

Retain the playable reference/recovery. This checkpoint does not install or
enable the experiments. Existing root performance owner retains raw checks seven
days, curated summaries 30 days / 4 MiB and build products under the existing
40 GB / 20 GiB free-reserve contract. Source/minimal regressions are durable Git
content. Visibility reuses the fixed 131072-record trace cap and held-session
cleanup; no collector, service or image recording is introduced.

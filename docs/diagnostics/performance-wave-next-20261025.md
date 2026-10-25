# Demand, discard and presentation wave — 25 October 2026

Source checkpoint, not a measured FPS release. DXMT `e1772a42` extends the
previous `c7e4ab45` neighbor repairs; Wine remains `5d4b43e4`. The original
source checkpoint did not install or launch a game. The later owner-selected
installation is recorded below; the previous endpoint remains recoverable.
All mechanisms below are general D3D11/runtime behavior, with no
DS3 executable-name check or replacement title policy.

## Changes and checked reuse

| Mechanism | Result and control | Origin |
| --- | --- | --- |
| Demand-aware compiler admission | Default-off `vibewine_demand_scheduler`; queued speculation can be promoted, inherited dependency urgency and bounded lower-class fairness; no extra workers, priority increase, substituted pipeline or skipped draw | Independent adaptation of DXVK urgency queues to DXMT ownership/ready publication |
| Explicit color discard | Default-off `vibewine_discard_hints`; only the immediately preceding exact private standalone base-2D color Store can become DontCare; no future load rewrite or content-validity registry | DXVK API intent informed the design; reuses local ordered commands/Metal attachment descriptors |
| Presentation diagnostics | Process-lifetime `presentation-waits` profile, identity-rich queue/admission/drawable waits and explicit native-to-queue frame bridge; stricter decoder/analyzer coverage gate | Existing producer, native feedback and bounded session protocol |

See the component [scheduler](../../components/dxmt/docs/demand-scheduler-20261025.md)
and [discard](../../components/dxmt/docs/discard-hints-20261025.md) notes for
pinned donors, licenses, failure controls and exact exclusions. No Vulkan executor
or donor async path returning an empty pipeline was imported. Both experiment
booleans default false and are emitted identically into PE/native compiler flags.
Old full/compile-waits profile meanings, capture generation rules and lifetime
capacity remain unchanged. New events 96–99 are profile-only; counters from
separate layers are joined by actual receipts, never assumed equal.

The [short capture recipe](../../tools/telemetry/PRESENTATION_WAITS.md) includes
phase evidence, event-rate calibration, immutable identity and loss checks. Its
illustrative rate/duration is not a live measurement or a ten-minute guarantee.
A compiled trace producer or CPU Present counter cannot establish displayed FPS.

## Verified CPU/source behavior

- At equal eight-worker saturation, releasing one active compiler slot selected
  queued speculative index 0 on the original FIFO path and demanded index 4 on
  the candidate. Both backlog-growth settings completed each task once at the
  same capacity. Actual extracted shader lookup overloads promote the same
  variant rather than compiling a duplicate. Native and Rosetta ASan/UBSan and
  TSan passed, along with prior compiler/persistence controls. This demonstrates
  admission order, not native compiler speed or a frame-time gain.
- Production discard API/helper/state/merge adapters passed 101 disabled and
  126 enabled assertions under ASan/UBSan. Historical no-op and six connected
  wrong-action mutations were rejected. Prior pass and failed-PSO guards remain.
- The integrated telemetry suite passed 184 tests, including compiled production
  producer, binary decoder and wrong/incomplete/duplicate identity controls.
  The previous saved compile-waits stream still decodes/analyzes under its own
  profile. Independent review repaired false coverage acceptance before freeze.
- The queue-shutdown fixture now represents the prior capture ownership seam.
  It checks joined workers before cleanup, no stop for unowned capture, and one
  stop across repeated/concurrent/destructor shutdown; four ASan/UBSan suites and
  connected missing/early/unowned-cleanup controls passed.

Independent integrated source review found no material S0/S1/S2 interaction
blocker. D3D12 retains a pre-existing incompatible Presenter caller; this
checkpoint and the configured build do not claim D3D12 acceptance.

## Parsed shader IR decision

NerRobDog's exact IR series was examined at `ad95248b`, `a3b44706`, `41a90a00`
and `82858922`, retaining its LGPL attribution. It is **not transplanted**:
failed rematerialization can reach null compilation, never-run tasks retain
constructor counts, and eager eviction leaves arguments under conditional free.

A fresh production parser/converter CPU harness used 13 existing Wine shader
fixtures spanning VS/PS/GS/HS/DS/CS, including a 12.7 KiB high-resource PS.
39 runs measured 1,152–140,448 bytes of live parsed IR per shader. Retaining
original DXBC instead leaves an allocator opportunity of 1,024–127,648 bytes
before lease/mutex overhead; these inputs are not a DS3 shader distribution.
Usable C++ allocations plus actual PackedVariantList backing were counted;
allocator-zone residuals were not treated as live owned IR. Counted C++ bytes
balanced after destruction. Process footprint did not decrease, so no RSS or
memory-pressure benefit is asserted.

Uninstrumented parse P50 medians were 1.38–329 microseconds under Rosetta/current
load. Seven ordinary outputs across three runs and four paired-stage outputs
remained byte-identical after destroy/reparse; three initialization failure
modes passed. This used the retained LLVM 14 CPU harness configuration, separate
from the matched LLVM 15.0.7 renderer build. No native Metal/game equivalence or
concurrent eviction was tested. Reparse may trade memory for additional CPU cost.

A future implementation must use fallible execution-scoped leases only after
an unusable binary cache result, cover both partners of all paired stages,
retain independent immutable DXBC/reflection/arguments, and unwind partial
acquisition. Cache-hit, allocation failure, concurrent variants, debug dump and
queued/blocked shutdown controls are required before enabling eviction.
The assessment closes this wave's decision; eviction is not shipped or enabled.

## Build/GPU integration

Both complete 161-target release builds passed, with all ten experiment
booleans false/true and identical PE/native definitions verified. The native
bridge, PE libraries, compiler and Metal helper were built together using retained
LLVM 15.0.7 and the configured retained Wine scaffold. The build source was local
`036aaae9`; public `e1772a42` has the identical source tree, recorded in the
[public equivalence record](../provenance/performance-wave-next-20261025.json).
The original local branch remains preserved. Only commit metadata/description
differs. This is not a complete new Wine distribution.

Both variants passed five offscreen D3D11 stages (draw, MRT/depth, compute, MSAA,
query across chunks). Both also passed all eight dedicated discard case groups
and 25 defined pixel images, including deferred replay and unaffected mip/slice
controls. The enabled variant rejected two actual wrong GPU-command controls
and three generic pixel/compute/query controls, each by the intended exit 20.
Every run bound the live process birth and seven required PE/native library
mappings, verified unchanged hashes and completed its isolated server cleanup.
The authless lab denied home/game/account access and IP network traffic. No game
or installed prefix was used; the installed manifest/helper hashes stayed equal
to the pre-wave baseline. Source review and tests found no material blocker.

These offscreen image checks do not prove a native Store omission occurred or
measure bandwidth, shader contention in a real game, production drawable waits,
input/native window behavior, memory improvement or FPS. Actual native discard
action admission/cost and matched gameplay comparisons remain pending.

## Retention and next acceptance

### Owner installation follow-up — 25 October 2026

The experimental ten-switch renderer is now installed through the existing
matched-runtime transaction. The five DXMT artifacts changed; all 17 retained
Wine artifacts stayed byte-identical. This does not mean that every change in
the current Wine source was rebuilt or installed. Final signed delivery bytes
were tested again in the isolated GPU lab: five generic stages and 25 discard
images passed in each variant; a connected overwrite-omission control failed as
expected. Installation readback and an identical no-op application passed.

A separate helper-only transaction made the ordinary DS3 icon request the
existing fixed-output MetalFX spatial path: current 1920x1080 render input,
2560x1440 requested output. It preserves graphics settings and all 22 runtime
files. The per-title launch selection is local deployment policy, separate from
the general renderer implementation. A cold Steam launch is needed to deliver
the environment; warm-launch metadata explicitly leaves activation unverified.
The helper's 72 affected checks and actual production-config-parser check pass.
Four legacy helper-refresh size-limit failures reproduce on the exact previous
source and remain unresolved; the selected transactional delivery route passed.

An isolated native MetalFX probe completed a real 1080p-to-1440p GPU encode and
readback: all 3,686,400 pixels matched within one channel value. A changed-input
negative control failed the fixed oracle. This proves native API/device support
for that SDR case, not the Wine bridge or the game's scaler activation.
The ordinary launcher then requested a cold profiled launch; the DS3 process and
its native window were observed. Screenshot retrieval initially timed out; a
later CUA observation showed the loaded world and short movement from the
bonfire toward the fountain. This is not a controlled route, combat test,
per-frame MetalFX activation proof, image-quality acceptance or FPS improvement.
No collector was armed before the initial owner play session.
The owner subsequently reported playing and finding the upscaled mode heavier.
No recording was armed for that session: no retrospective FPS or causal
comparison is available. This is an adverse usability observation, not an
accepted performance improvement. A comparison must arm and verify the collector
before opening either game variant, and distinguish native 1080p cost from
native 1440p image/output comparisons.

### Later recording and diagnostic repair

Three later attachments bound the same live game birth, without restarting it.
The first saved 56 accepted native intervals before a valid 5.67689-second
aggregate triggered the watcher's old five-second parser ceiling. It also ran
intrusive stack samples; those periods are unsuitable for clean FPS comparison.
The second, with stack sampling disabled, ended normally at 180.53 seconds with
119 fresh accepted intervals. The third used the reviewed parser repair and
ended normally at 90.61 seconds with 56 accepted intervals and no parser/cleanup
error. These are separate captures with explicit gaps, not one continuous run.

The repaired watcher fully validates coherent long aggregates, marks them as
dropped coverage and resets stall continuity while continuing collection. It
retains freshness, PID, replay, finite-number, alignment and byte limits; these
aggregates do not become valid frame-time samples. The real failing record,
following normal recovery and actual Monitor seam passed: 29 tests passed, one
unrelated unavailable historical fixture was skipped. Reviewed source was
published to the existing local diagnostic owner with predecessor preservation
and exact readback; no game runtime binary changed for this repair.

The three-minute raw stream describes a 2560x1440 BGRA8Unorm Metal layer, but
all its frame observations are tagged skipped, with no presented/on-glass
coverage. Visible CUA movement does not license relabeling them. Their work
duration averages/maxima are not display cadence or FPS. The host also performed
substantial swapping; that is a confounder, not a proven cause. The current
result is bounded useful diagnostics plus a repaired collection failure;
displayed-FPS coverage and a matched native/upscaled comparison remain open.

Existing performance owner retains private raw diagnostics/test receipts seven
days; curated summaries 30 days / 4 MiB. Source, decisions and minimal regressions
are durable Git content. Private IR scratch is capped at 64 MiB, build products
at the existing 40 GB with 20 GiB free reserve. Active work, unresolved analysis
and recovery remain held. Exact-path manual cleanup follows preservation checks;
no cleaner, background collector or installed producer is introduced.

Next live gate: the installed matched candidate, fixed scene/settings,
controlled warm/cold state and background load, phase-marked short presentation
trace plus independent displayed-frame observation. First demonstrate whether
this workload uses the new paths. Compare frame tails, latency and memory as well
as average FPS; retain the playable reference. Installation is accepted separately;
no controlled game result or numerical performance improvement is established.

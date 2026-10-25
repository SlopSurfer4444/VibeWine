# Deeper FPS hypotheses, 25 October 2026

Read-only source investigation against hub `fb8967fd`, Wine
`e20af58312ef059e54f8d16767ee7b74cb8358e5` and DXMT
`e11476e33117b454bac60b997532f9823e34855b`. Three independent branches
examined binding costs, GPU/residency behavior, and synchronization/memory;
the integration owner cross-checked the leading source paths and investigated
shader lowering and existing GPU-capture support. No game was launched, runtime
changed, or performance benchmark run. These are causal hypotheses with rejection
criteria, not accepted fixes or numerical FPS gains.

The [early scene-submission experiment](../diagnostics/focused-followup-20261024.md#next-fps-direction-admission-preserving-early-scene-submission)
remains the selected implemented, default-off experiment. Its native held-acquire
fixture is not a gameplay result. This review adds alternatives and discriminators;
it does not replace the pending controlled comparison or require every experiment.

## Decision order

| Direction | Established source behavior | Admission to an implementation experiment |
| --- | --- | --- |
| Precise constant-buffer invalidation | DISCARD broadly dirties stages, including unrelated bindings | Material avoidable table rebuilding on the CPU critical path |
| Overwritten attachment stores | Clear folding can leave a predecessor Store whose contents are immediately destroyed | Frequent eligible final stores and critical GPU attachment traffic |
| Residency declaration deduplication after pass merging | Logical-pass dedup precedes command-list concatenation | Substantial covered duplicates and native CPU cost in final physical passes |
| Bounded D3D11 residency sets | D3D12 in the same tree provides reusable primitives; D3D11 does not use them | Residual cross-pass residency cost after the narrower candidate |
| Completed-idle allocation pools | Existing reclamation is lazy on that object's next use | Owned idle bytes materially sustain observed paging/pressure |
| MSync send backpressure / WaitAll rollback | Deadline coverage and rollback ownership have remaining source concerns | Deterministic reproduction; frame-critical use required for FPS priority |
| Shader lowering / fixed-output upscaling | Compiler and scaler paths provide separate GPU experiments | Actual GPU-bound passes, with correctness and image-quality checks |

Priority is conditional on the bottleneck. An optimization outside the limiting
CPU/GPU/presentation dependency may lower work or power without changing frame
intervals. Savings from different lanes cannot simply be added. Frame generation,
a deeper queue and a higher CPU Present count are not simulation-FPS evidence.

## 1. Rebuild bindings only for affected resources

DXMT `src/d3d11/d3d11_context_imm.cpp:158–170` and the deferred Map counterpart
dirty all constant-buffer or SRV stages after WRITE_DISCARD. In
`d3d11_context_impl.cpp:3301–3337`, reflection filters active slots, but a dirty
constant-buffer stage still allocates and reconstructs its complete referenced
table. `dxmt_context.cpp:210` then resolves current backing, retains/tracks it,
checks residency and writes binding data.

Start with constant buffers: scan existing bound entries and dirty only matching
resource slots/stages. Reuse `BindingSet` iteration rather than first creating a
reverse-dependency registry. This avoids unrelated stage tables; it does **not**
make a required table partially updated. Maps before a draw already coalesce,
and new encoders/shaders legitimately invalidate bindings. A scan can cost more
than broad bitset assignments when most tables really need rebuilding.

Measure DISCARD count, genuinely affected stage masks, avoidable table rebuilds,
entries/bytes encoded and API/encoding CPU time as frame aggregates. Check unbound
resources, one resource in several slots/stages, offsets, renaming/suballocation,
shader changes and repeated deferred replay. Compare resulting table bytes and
mandatory access/residency/lifetime records, then rendered old/new-data sentinels.
Reject if almost every rebuilt table is necessary or dependency scanning loses.

A separate, broader candidate exists at `d3d11_context_impl.cpp:3314,3327`:
referenced bound UAVs force mixed argument-table materialization for each relevant
draw/dispatch. Reusing immutable table bytes requires separating them from access,
hazard, retention and counter work in `dxmt_context.cpp:305`. A stable view pointer
does not imply a stable UAV counter allocation (`dxmt_counter.cpp:94`); resetting
an initial counter can matter even with unchanged bindings. Replacing `uav_bound`
with a dirty check is unsafe. Admit this larger design only if unchanged tables
dominate measured cost after mandatory tracking is separated.

Bounded reuse checks found the broad DISCARD behavior in
[upstream DXMT a148782d](https://github.com/3Shain/dxmt/blob/a148782d431249405acdb1531dbb0c7e737bbece/src/d3d11/d3d11_context_imm.cpp)
and inspected phonzammi, fayep and jostmoritz fork sources. No ready donor patch
was identified. [DXVK invalidation](https://github.com/doitsujin/dxvk/blob/master/src/dxvk/dxvk_context.cpp)
is a design precedent, not a Metal implementation to transplant.

## 2. Avoid storing pixels a full clear immediately destroys

Ordinary render attachments default to Load/Store
(`d3d11_context_impl.cpp:4535–4556`). The backward optimizer
(`dxmt_context.cpp:885–895`) folds Clear into the next Render as load=Clear,
removing the clear node (`1378–1416`). A preceding Render cannot merge with that
load=Clear successor (`1574,1587,1602`). There is no equivalent dead-store rule
for that folded pattern. A surviving Render→Clear has a depth/stencil DontCare
rule (`1421–1430`), but no color counterpart.

Thus Render(A)→Clear(A)→Render(A) can retain a predecessor Store although the
next full clear destroys its contents. [Apple's attachment guidance](https://developer.apple.com/videos/play/wwdc2021/10153/)
explains why unnecessary stores can consume bandwidth. The driver may already
avoid some traffic; nominal dimensions × bytes/pixel are not measured bus bytes.

First count eligible stores **after optimization**. The smallest experiment is
directly adjacent, same native allocation/view and full extent, single-sample 2D,
ordinary Store followed by a verified full clear. Exclude resolves, aliases,
partial coverage, arrays/3D planes, depth/stencil ambiguity and intervening
readers/copies. Preserve all draws, fences, queries and other attachments.
Compare image goldens plus actual pass/store duration and bandwidth. Reject if
the pattern is rare, traffic is already avoided, or reduced traffic does not
improve the limiting GPU interval. Do not globally replace Store with DontCare.

## 3. Residency: a narrow duplicate pass before the larger architecture

`CheckResourceResidency` (`dxmt_residency.hpp:35–53`) already deduplicates resource
usage within a logical encoder and preserves usage/stage upgrades. Render-pass
merging later concatenates lists (`dxmt_context.cpp:1474–1478`). The native
interpreter executes each remaining declaration (`winemetal_unix.c:941–945`).
Two logical passes merged into one physical encoder can therefore retain
duplicate native declarations.

Count covered duplicates in final physical command lists and native declaration
CPU time. A bounded experiment suppresses only a later declaration whose exact
native resource and both requested masks are covered by one earlier declaration,
preserving order and upgrades. Do not independently union usage and stage masks:
vertex-read plus fragment-write does not establish vertex-write coverage.
All earlier `access()`, fences, allocation retention and counter work
remain. Reject if duplicates are scarce or bookkeeping exceeds saved calls.
This is distinct from completed W4 getter hoisting/command batching.

For larger cross-pass savings, D3D11 can reuse the same tree's D3D12 residency
wrappers and membership pattern (`d3d12_device.cpp:1203–1244`,
`d3d12_command_queue.cpp:140`); this is not a missing bridge from scratch.
[Apple documents](https://developer.apple.com/documentation/metal/simplifying-gpu-resource-management-with-residency-sets)
lower declaration overhead and earlier residency preparation. Our wrappers expose
membership/commit and queue attachment, not requestResidency/endResidency; explicit
early preparation needs additional bridge work. Start with a
stable, byte/count-bounded subset, serialized membership changes, supported-OS
fallback and completion-safe removal. Preserve untracked-resource hazards,
fences/events and ownership. Heap membership can make an entire heap resident.
Do not copy an all-live-resources policy indiscriminately. Reject if churn or
paging increases, CPU cost is negligible, or the critical interval does not shrink.

## 4. Memory pressure: identify the owner before reclaiming more

The [dynamic](../diagnostics/dynamic-retention-20261023.md) and
[staging](../diagnostics/staging-retention-20261023.md) fixes explicitly retain
lazy cleanup. `dxmt_dynamic.cpp:41–55,110–124` and `dxmt_staging.cpp:44–48,65–67`
trim only on that object's next allocation/Map. A surviving inactive object can
retain a completed rename burst. This is a documented remaining boundary, not a
rediscovery of the fixed active-resource high-water defects.

Existing observations of compression/swapping during long pauses justify the
lead, not attribution. Correlate process footprint, host VM activity and owned
completed-idle bytes through labeled scene transitions. Add aggregate live,
current/inflight, completed-idle and allocation/release counters only if existing
counters cannot distinguish the owner. Reject if owned idle bytes are small or
stalls persist without pressure/fault activity. A safe sweep needs real resource
ownership and must protect current, incomplete and command-list-held allocations.
Sequence age is neither a global byte cap nor elapsed time; extra scene chunks
change it and must be controlled in comparisons. No speculative cache purge.

## 5. Synchronization: distinguish blocked send, ACK and actual object wait

The repaired MSync ACK loop begins after `server_register_wait` returns
(`Wine dlls/ntdll/unix/msync.c:397–407`). Register, unregister and signal sends
(`249–250,294–295,732–733`) omit MACH_SEND_TIMEOUT. The server defaults to a
50-message queue (`server/msync.c:653–659`) drained by one pump. A full queue can
block before the bounded ACK loop. [Apple XNU's send path](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/ipc/ipc_mqueue.c#L441)
distinguishes timeout-enabled full-queue waits.

Use an isolated delayed-receiver fixture, then separate send, ACK, object-wait
and unregister durations on critical game threads. A naive timeout can strand
waiter bookkeeping; a larger queue can merely postpone saturation. Reject this
FPS lead when sends are consistently short or waits follow genuinely unavailable
producer work. Native arm64 wineserver is lower priority until actual critical
server service CPU, rather than scheduling/dependency, is material. Unalertable
single waits already bypass the server through ulock (`msync.c:1003–1008`).

WaitAll has a separate correctness lead: `msync.c:1107–1112` skips a mutex already
owned by the caller, while rollback `1166–1177` clears preceding mutex owners
without acquisition ownership tracking. Semaphore/event restoration at
`1180–1190` also lacks the normal release wake. Reproduce recursive ownership,
rollback wake and timeout races in a production-body fixture first.
[Dormison 9d6a6ed5](https://github.com/kageroumado/dormison/commit/9d6a6ed55572957275f17a1d0b5d300b46227937)
has relevant work but depends on earlier acquisition tracking and itself introduces
paired client/server reset-generation changes; it is not a standalone cherry-pick
or atomic WaitAll. Correctness can justify a
repair independently, but FPS priority requires representative critical WaitAll use.

Compiler, encode and finish threads already request TIME_CRITICAL. Blanket boosts
are not a supported direction. `server/mach.c:160` also follows an infinite receive
loop that returns on empty receive, making its intended priority reapplication
unreachable. Actual requested/effective policy readback and a small lifecycle
fixture are needed; later explicit calls may compensate.

## 6. GPU-bound shaders and output scaling

DXBC-to-AIR `airconv_context.cpp:78–155` intentionally selects a limited LLVM pass
pipeline, balancing compilation cost against shader quality. More optimization
passes are not automatically better: compare final GPU cost, compilation latency,
code size and image correctness on hot shader hashes. Use a frame capture to
locate expensive passes and [occupancy/limiter counters](https://developer.apple.com/documentation/xcode/finding-your-metal-apps-gpu-occupancy)
to distinguish register pressure, ALU, texture and bandwidth limits. Occupancy
alone is not an optimization target or proof of a bottleneck.

Respect DXBC precise/refactoring contracts: `dxbc_instructions.cpp:560–565` and
`nt/dxbc_converter_base.cpp:2934–2955` already choose math flags with stage-specific
restrictions. The commented SGV MinPrecision reads in `dxbc_signature.cpp` do not
establish that DS3 has useful minimum-precision arithmetic. [Microsoft permits
full precision for minimum-precision types](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/using-hlsl-minimum-precision);
a safe FP16 experiment requires actual compatible bytecode/IR dataflow, not
blanket demotion. Conversions and lost precision can erase savings or corrupt
depth, animation and effects. Reject if no expensive eligible shaders exist.

Low-render/fixed-output spatial MetalFX is a different quality/performance mode.
The [existing donor](https://github.com/slavazhulanov/dxmt/commit/a642d7045a52a731ae5447cd01c58e5d48a18778)
separates render from desktop output dimensions; our current multiplier alone
does not lower game rendering cost. Admit only a resolution-sensitive GPU-bound
case. Compare native render, lower render with ordinary scaling, and lower render
with MetalFX at the same output, including scaler/presentation cost, UI, gamma/HDR
and geometry. Color-only swapping is not universal temporal reconstruction.

## Evidence route and stop conditions

Reuse existing trace identities, phase labels, CPU/GPU clocks and bounded capture
contracts. The installed trace profile's coverage and launch-environment limits
remain documented; `compile-waits` cannot attribute draw/Map/residency/store costs.
Prefer aggregate counters for high-rate paths over logging every resource.

Native GPU capture already exists (`dxmt_capture.cpp`, command queue `150–169`,
WineMetal capture wrappers). [Apple's supported capture API](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically)
can expose pass/shader costs without inventing a new recorder. The current native
start wrapper ignores NSError and capture-start success (`winemetal_unix.c:1395–1409`),
so a capture request is not proof of a usable trace: validate support, real start
status, completed artifact and readable workload before relying on it. Existing
logical-frame/chunk boundaries also need checking with split submission. These
are diagnostic prerequisites, not a frame-capture repair performed by this review.

Keep diagnostic capture separate from minimally instrumented timing: capture/replay
changes execution, and serialized profiler mode is not normal concurrent GPU
performance. First establish one visible, labeled stable world with unchanged
power/settings/render/output/cache/background conditions. Then compare one switch
at a time, alternating baseline/candidate/reversal. Report displayed intervals,
p95/p99 and long-frame counts plus critical CPU/GPU time, latency and memory.
Reject an optimization when benefit stays within A/A variation, another wait
merely grows, queue depth rises, unexpected rendering differences or failed quality
criteria appear, or memory/latency regresses.

No producer, permanent service, extra raw-data scope or retention policy is
introduced here. Proposed measurement must use the existing owner and retention
contracts before activation. Checked source behavior is the present result;
deterministic reproductions and controlled gameplay remain future acceptance.

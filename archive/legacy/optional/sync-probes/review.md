# Independent synchronization attribution review

Owner: wave5_renderer/reentry_review. Consumer: wave5_renderer/root.
Disposition: the revised source is accepted as a bounded offline diagnostic
addition under the distinct-operation boundary contract below. No live capture,
renderer activation, game compatibility or measured performance is established.

## Findings and revisions

The first draft independently sampled current capture in `commitReason` and
`SyncScope`, skipped ReasonScope installation when entry generation was0, and
let fallback Flush/PassPromotion replace an active same-queue reason after a
capture restart. A Map/GPUIdle operation begun under G1 (or unarmed) could
therefore produce attribution under G2. I reported these concrete generation
counterexamples and reviewed the implementation owner's repairs.

The revised ReasonState includes `active`, including entry generation0.
Same-queue nested scopes inherit the parent's generation; specific scopes may
change the reason while retaining that generation. A fallback retains the entire
active parent state regardless of current capture. All exits restore the previous
state. Cross-queue scopes own their own entry generation and restore the resumed
parent; thread-local state prevents one thread's reason leaking to another.

Types30–32 explicitly initialize SyncScope generation from
`operationGeneration(queue)` rather than resampling after a flush. These events
retain their operation's entry generation and are discarded by the existing
record check when the capture changes. An operation started unarmed remains
unarmed across its nested waits. The default SyncScope member initializer still
samples current generation; future operation-owned uses must preserve the
explicit `.gen=operationGeneration(...)` pattern used by every current callsite.

Type33 moved to the chunk publication seam and receives the exact frozen
`chunk.trace_generation`, queue, sequence and frame. If an active same-queue
ReasonScope belongs to a different generation, commitReason emits nothing.
It never replaces an older operation's reason with a new-generation reason.

## Distinct operation boundary contract

The implementation owner deliberately retains the diagnostic base's chunk
publication generation sampling. Chunk publication is a distinct traced
operation: a G1 Map/GPUIdle operation spanning stop/arm may publish a chunk
under G2, while its types30–32 and reason33 are missing from G2. Existing
types20–24 continue to describe that chunk's G2 publication/processing.
This is not whole-Map/GetData/GPUIdle all-or-none capture semantics. Consumers
must treat absent33 as boundary-unknown/unattributed, never as a classified
Unknown reason0, and exclude missing/duplicate/conflicting reason joins from
reason-specific aggregates. A real bare commit without an active same-queue
reason can still emit an explicit Unknown0 marker. The distinction is material.

Reason attribution is scoped to the currently executing context operation;
it is not a global causality proof. The TLS helper retains only its current
state plus each RAII scope's saved parent. Cross-queue scopes restore parents
on return; an unrelated queue commit while another queue's scope is topmost
has no matching active reason and remains unknown. Nested/reentrant multi-device
flows need this qualification rather than a claim of complete cross-queue cause
reconstruction.

## Template and rendering behavior

Both ContextInternalState and DeferredContextInternalState have `cmd_queue`.
Thus `requires { ctx_state.cmd_queue; }` is true for both current template
instantiations; it is not an immediate-context discriminator. The initial
prepare.py comment claiming otherwise was corrected. Deferred `Commit` remains
its existing no-op. Both branches invoke Commit exactly once at the existing
promotion condition; diagnostic RAII does not add a deferred submission.
Immediate Commit retains its command/state updates, and the new publication
marker uses the pre-existing release/acquire chunk handoff. No mutex policy,
renderer command, GPU wait, fence target, retry loop or native ABI is changed.

Type30 times the existing staging CPU-fence wait, type31 times the not-ready
GetData-triggered Flush call (which may produce no chunk), and type32 times the
existing GPUIdle CPU-fence wait. Type33 labels the trigger of chunk submission,
not the complete chunk's contents or dominant cost. In particular FenceWait
labels the flush before the GPU event wait is encoded into a later chunk; it
does not measure a CPU fence wait or prove the flushed chunk contains that
event wait. PassPromotion/ExplicitFlush fallback preserves a more specific
enclosing operation reason. Present reason covers the final Commit seam, not
every earlier step of Present or occluded flush-only calls.

## Independent checks and artifact binding

I verified the entire private source delta against diagnostic/source: only
`cs2_trace.hpp`, `d3d11_context_impl.cpp`, `d3d11_swapchain.cpp`,
`d3d11_context_imm.cpp`, and `dxmt_command_queue.cpp` differ; no additional
source paths exist. All before/after hashes match source-binding.json, and the
regenerated ordered diff matches sync-probes.patch exactly. prepare.py patches
unique expected anchors and copies from the retained diagnostic base. Its
successful current output is verified; no generator execution was performed
by this reviewer, and an interrupted/failed regeneration must be rebound before
using an earlier build receipt.

At the owner's authorization I created `tests-generation.cpp` from the exact
generated helper bytes and compiled/ran it in a private temporary directory
with native clang++, C++20, ASan and UBSan. All 12 checks passed without sanitizer
diagnostics: correct reason/keys, restart suppression, unarmed-entry suppression,
same-queue fallback/specific inheritance, cross-queue restoration, valid and
boundary SyncScopes, bare Unknown reason, thread-local isolation and exception
unwind. Capture/clock/event sinks are mocks; this validates helper control flow,
not Wine mapping atomics, queue scheduling or physical GPU/game behavior.

Reviewed SHA256:

- prepare.py: `399c018bf2f8080a62cb423fd67143af770fbc83efd8a41e09e0dc10b471b0b5`
- cs2_trace.hpp: `04a3305192eeaeafde61d8f4d41ac90e7edfec8ac792994a890b7f10eaaa3567`
- tests-generation.cpp: `5d860fb8c0fc1e97f44dceeab5765b9a72957d0871d0248719be416dc9fbe4b3`

The retained full PE build log and metadata record 69 source units and successful
compilation/link, including both context implementations. I recomputed the DLL
SHA256 and matched build/build.json:
`27100bd36f25fcc79f4b3dba6d900a4d58841047d5f72cdad98cd52cfd76d791`.
This is offline build/artifact evidence; imports/helper/native runtime checks
and types30–33 controller/analyzer integration belong to their owners. Disabled
paths still add TLS/generation bookkeeping and branches; matched probe-overhead
measurement remains required. No runtime, display, profile or installed-library
mutation was performed by this review.

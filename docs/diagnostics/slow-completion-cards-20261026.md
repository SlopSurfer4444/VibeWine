# Identify slow completion before changing scheduling

The native1440 stall recording contains a 3.037-second command-buffer completion
wait, a downstream 3.333-second frame-latency wait, and shorter shader and reset
delays. Those overlapping observations cannot be added together or interpreted
as GPU execution time. Host paging was substantial, but its owner and causal
relationship remain unknown. No scene-matched FPS improvement is claimed.

This checkpoint changes source, tests and build artifacts only. It does not
select a new installed runtime or launch a game. These are general renderer
changes; there is no executable-name or DS3-specific condition.

## Sparse completion cards

The existing `stall-waits` profile can publish six linked records after an
observed completion call of at least50ms. It requires the existing explicit
`DXMT_CS2_TRACE_DETAIL_LIMIT` and the chunk's original capture generation.
Other profiles do not enable these probes. No new mapping or native bridge ABI
is introduced.

All six rows have the same CPU-steady interval, queue birth serial and thread.
They retain chunk sequence, zero-based queue frame, optional one-based final
frame target, initializer target and its observed value immediately before the
finish-thread wait, sampled queue frontiers, before/after native status, and
optional thread CPU delta in `GetThreadTimes`100ns units. Native GPU duration is
calculated only from a valid completed command buffer's own start/end values.

The matched decoder and analyzer require the complete111–116 group. Missing or
duplicate parts, conflicting queue/chunk/frame/thread identities, invalid flags,
reserved payload, or inconsistent observed chronology are rejected. A partial
card at the shared detail cap remains incomplete evidence; no missing field is
filled with zero. One admitted writer spans a card so capture stop drains the
admitted bundle. Cap, stale generation and capture-boundary accounting remain
part of acceptance.

The card is emitted after the wait returns. **Waits still outstanding at capture
stop are not observed by this mechanism.** It does not claim a start breadcrumb
or a complete queue history. A same-buffer native duration and CPU wait may be
compared as separate observations; subtracting their absolute epochs, or naming
their difference paging/notification time, is invalid without additional proof.
Thread CPU delta separates on-thread CPU from wall time, not GPU blocking from
runnable starvation.

The existing reset110 is supplemented by thresholded117–119 scopes for command
capture destruction, allocation-reference clearing, and native-buffer release.
They preserve readback publication, final-frame signaling, CPU coherence and
chunk recycling order. Default/unpublished chunks never read uninitialized
sequence/frame fields. Nested reset scopes are not additive CPU-busy costs.

## Deliver idle diagnostics through Wine

The previous idle formatter wrote to PE CRT `stderr`, which can be invalid in a
GUI game launched by Steam even while the Unix launch log works. The new bounded
`Logger::diagnostic` endpoint uses the existing Wine debug-output route, with
the existing fallback/file configuration. Explicit diagnostic output is
independent of ordinary severity filtering; ordinary log filtering is retained.
Its boolean acknowledges acceptance into the output route, not durable storage.

With `DXMT_IDLE_ATTRIBUTION=1`, each queue emits one `VIBE_IDLE_START` enablement
acknowledgement followed by at most63 partial-scan summaries, no more often than
once per10seconds. The acknowledgement is not a pool sample. Pool references
and released-reference bytes are not RSS or unique physical memory. An absent
record never proves zero held memory or zero release cost.

## Upload reuse is a separate experiment

`vibewine_initializer_inclusive_reuse` is default-off. It translates only the
initializer's proven inclusive completed-upload frontier into a reuse boundary;
the generic checked ring's strict contract and ordinary allocation path remain
unchanged. See the component's
[candidate and tests](../../components/dxmt/docs/initializer-inclusive-reuse-20261026.md).

An isolated production-CPU fixture reproduces two sequential completed32MiB
upload reservations using two buffer allocation calls without the option and
one with it. It also tests allocation denial, incomplete uploads, stale cached
completion, two-plane identity and transactional rollback. These native endpoints
are mocks: their counts do not establish actual saved game memory or FPS.
The offscreen Metal fixture exercises delayed real reads and destination
readbacks; it is narrower than full production initializer/Wine-bridge gameplay
acceptance. The option stays default-off and is not silently enabled for users.

## Verification scope

Focused fixtures exercise the actual producer, complete finish/reset methods,
strict decoder/analyzer, production logger routes and initializer transactions.
Mutation controls reject early resource reuse and incorrect operation order.
Native GUI-PE logging uses a disposable test prefix, not an installed game
profile. Full renderer configurations with experiments disabled/enabled are
build checks; live DS3 performance and installed activation remain separate.

Before a follow-up gameplay run, bind the actual installed libraries and matched
collector/analyzer, prepare collection before launch, and retain explicit scene,
power and background-load observations. Use the same scene and resolution for
candidate/control. A short thread/VM observation at an actual long wait remains
necessary to distinguish scheduling, paging and dependency execution.

## Owner and retention

The existing diagnostic/session integrator owns these opt-in producers and
their derived reports. Completion cards and reset details share the existing
finite detailed-record budget and131072-slot process-lifetime mapping. The
[stall recipe](../../tools/telemetry/STALL_WAITS.md) retains its7-day/64MiB combined
raw-and-decoded experiment cap and30-day/4MiB compact-result cap. Active mappings,
active analysis and unresolved acceptance/recovery holds require reviewed expiry.

Idle output is owned by the existing finish thread, enabled explicitly, at most
64 records of at most1019 adjusted bytes per queue:65216bytes total. Its inherited
launch-log TTL/rotation and active evidence exclusions remain authoritative.
There is no new file, timer, worker, retry loop, collection service or cleaner.
The session owner stops collection after acceptance; cleanup uses the existing
bounded inventory/preservation procedure and never includes unrelated data.

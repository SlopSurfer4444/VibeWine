# Late-stall capture repair, 26 October 2026

The preceding native1440 diagnostic session retained 104,020 detailed records
over only27.919 seconds before its count stop. Independent Metal aggregates
continued for258.055 seconds and contained a966.663ms on-glass interval maximum.
The trace therefore cannot identify the owner of the later pause. Scene labels
were absent; no native-versus-upscale FPS comparison is accepted.

## Implemented

The opt-in [stall-waits profile](../../tools/telemetry/STALL_WAITS.md) extends the
existing producer, immutable decoder and analyzer. It retains selected CPU wall
scopes of at least50ms and optionally capped task/cache context. Existing full,
compiler and presentation profiles retain their behavior; unknown profile names
still fail closed. There is no automatic profile selection or installed launcher
change.

Three source probes close gaps in the finishing thread:

| Type | Scope | Identity |
| --- | --- | --- |
|108|Completed-idle resource sweep, excluding diagnostic output|queue birth / chunk / saved frame|
|109|The three ring allocator reclamation calls together|same|
|110|Command chunk reset, after query readback and before CPU coherence|same|

These probes are inactive outside stall-waits. They preserve every operation,
fence, notification, resource-lifetime rule and feature gate. Missing phases may
have taken less than50ms; their absence is not zero cost. Duplicate phases,
conflicting frame/thread identities and impossible observed phase chronology
are rejected independently by the decoder and analyzer. Raw observations remain
available after rejection.

An offline type/duration replay of the old104,020-record snapshot selects13
records: eleven long CPU Present-start intervals, one fullscreen call and one
completion wait. This demonstrates the volume problem on that early window;
it does not simulate the new probes, late gameplay, producer overhead or a
guaranteed recording duration. CPU Present intervals are not displayed FPS.

## Source-backed memory leads, not a diagnosed cause

The preceding session's host swap usage rose14.641→17.638GiB. It is host-wide
evidence, not an allocation attribution. Its temperature and clock frequencies
were not captured. Startup/loading and gameplay were not separated.

- The idle-retirement owner deliberately refuses allocations over its64MiB
  per-pass release cap. An old oversized FIFO head can retain excess references
  while an owner is inactive. Existing tests explicitly preserve this tradeoff;
  admitting oversized releases would change the contract. It is not silently
  treated as a leak or patched by exceeding the budget.
- The idle sweep runs on the finishing thread before chunk-capacity notification.
  Native destruction or paging there could delay later producer progress.
- Ring reclamation follows the old event92 and can scan/erase a completed prefix
  under its mutex. Dynamic/staging foreground trimming can also destroy an aged
  burst. Neither path has been established as the late-stall cause.
- The immutable residency policy retains at most256 entries /64MiB. It cannot
  by itself account for multi-GiB retained allocations. Allocation sizes are
  not physical GPU residency.

The next bounded run should correlate these slow CPU scopes, compiler/fence
waits, independently observed display gaps and host paging. Keep scene, build,
power and load fixed; preserve unknown transitions. The new profile deliberately
omits native GPU and presented-frame streams, so the independent frame observer
remains necessary. It also retains the existing uncalibrated CPU-clock domain:
do not subtract its absolute epoch from Metal/native presentation clocks.

## Verification and delivery boundary

Both retained complete release configurations rebuilt successfully with all18
experiment switches off/on, against the unchanged matched engine dependencies.
The native/PE/compiler/helper build completes; no installed library was replaced.
Component source is `cd7350da`. All240 offline telemetry tests and13 workspace
fixture tests pass. The actual producer sanitizer fixture and complete
production finishing-method fixture cover filtering, shared detail quota,
generation boundaries and operation ordering. The latter runs on arm64 and
Rosetta with ASan/UBSan and rejects nine relevant mutation executions per arch.
Its native endpoints and scope observation are mocks, not GPU/game evidence.

No DS3 launch, collector activation, installation or claimed FPS improvement is
part of this checkpoint. The exact component pin is recorded by the integrating
commit; private source/build receipts retain hashes of the three production
files across both builds. Hosted CI is configured but has not run for this local
checkpoint.

## Ownership and retention

The existing performance/session integrator owns this opt-in diagnostic mode.
It adds metadata-only records to the same process-lifetime mapping: no new
service, file layout, capacity increase, counter reset or cleaner. The recipe
uses a stricter64MiB combined raw/decoded experiment cap within the existing
telemetry contract,7-day TTL; compact results/minimal failure evidence30days/
4MiB. Active mappings, pending analysis and required recovery holds remain
excluded from manual cleanup. Existing originals are retained unchanged; no
cleanup was performed.

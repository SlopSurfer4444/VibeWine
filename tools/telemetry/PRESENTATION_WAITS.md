# Short presentation/wait capture recipe

Status: source-only recipe, not executed. Owner: existing performance/session
integrator. Consumer: the next controlled DS3 presentation-tail experiment.
The producer/decoder/analyzer must come from the same integrated source revision
and their exact file/binary hashes must be recorded in the private run summary.
The caller must pass `PresentData.frame_id` into `Presenter::encodeCommands`;
a missing caller integration yields98 frame0 and fails acceptance. Source tests
cannot establish which runtime binary a game actually loads.

1. Bind the actual process PID/birth, runtime/component source revisions and
   built-library hashes through the existing launcher/session ownership route.
   Before that producer starts, opt in to
   `DXMT_CS2_TRACE_PROFILE=presentation-waits` and the normal private
   `DXMT_CS2_TRACE_DIR`. Set a finite `DXMT_CS2_TRACE_DETAIL_LIMIT`, e.g.12000,
   only if optional90–92 finish attribution is needed. Unset/zero disables those
   probes; no setting may exceed32768. Do not modify the installed launcher or
   selection just to run this recipe. Verify the loaded matched candidate
   separately before attributing a trace to it. Do not join another process's
   events by PID alone or reuse a historical file identity/generation.
2. Reuse the existing independent native observer and phase markers described
   in [SESSION_PHASES](SESSION_PHASES.md). Mark observed menu/loading/gameplay
   entry and exit, explicitly describing the route/combat/camera actions and
   evidence receipt. Use `unknown` around unobserved transitions. Take the
   minimal scene/configuration receipt outside the timed route. Bind the
   observer PID/birth and wall/native-clock coverage separately; phase markers
   do not calibrate CPU-steady timestamps to native71 host time. A supplied
   phase is an observation claim, not automatic proof of continuous gameplay.
3. Inspect the exact mapping and retain its device/inode identity, capacity,
   remaining lifetime slots and current counters. Use a fresh unused generation
   and explicit admission budget. A short **separate calibration capture** in
   the same scene can estimate event rate; its generation also permanently
   spends lifetime slots. Stop/decode it and record reserved-slot delta, elapsed
   source time, drops and maximum observed rate. Marker95 must identify profile3.
   Do not infer a safe duration from the previous compile-waits trace's16events.
4. Calculate `planned = 1 + ceil(route_seconds * conservative_records_per_second)`
   plus a separate tail reserve. Use observed rates, multiple chunk counts,
   native counter74 bursts and optional90–92 in that estimate; slow/censored
   legacy scopes can increase volume during the very hitch being investigated.
   For scale only, **6000 records/s ×8s +1 =48001** records plus **16384** tail
   reserve needs64385 unused slots after calibration. This is an illustrative
   admission estimate, not a measured workload rate or automatic duration
   guarantee. If the calibrated rate or remaining capacity cannot support the
   route, shorten it or use a fresh producer through its normal lifecycle;
   never reset counters, recycle a generation, enlarge capacity or raise detail
   quota. One file is8,392,704bytes; raw plus decoded output must fit the shared
   64MiB experiment cap before recording.
5. Mark the actual observed route boundary outside timing, arm immediately
   before the short repeated route, and retain the arm receipt:

   ```text
   cs2trace inspect ABS_FILE
   cs2trace arm ABS_FILE UNUSED_GENERATION DEV:INODE PLANNED_RECORDS TAIL_RESERVE
   ```

   Keep settings, actual drawable dimensions/revision, power/background load,
   warm/cold state and independent observer fixed. Reserve a brief observed tail
   for asynchronous completions/feedback; stop before lifetime capacity is
   exhausted. Native feedback arrives on later Present/teardown, so a boundary
   can remain incomplete even when the producer is quiescent. Mark route end and
   `unknown`/observed exit; do not silently label later unobserved runtime as play.
6. Stop once, retain the exact final receipt, then decode an immutable,
   identity-bound snapshot through the existing session snapshot procedure:

   ```text
   cs2trace stop ABS_FILE GENERATION DEV:INODE
   cs2trace decode ABS_IMMUTABLE_FILE GENERATION SNAPSHOT_DEV:INODE
   python3 tools/telemetry/analyze.py ABS_DECODED --output ABS_REPORT --expected-profile presentation-waits --expected-width WIDTH --expected-height HEIGHT --expected-revision REVISION
   ```

   Stop must report structural completion and admitted-writer quiescence, stable
   counters and zero admitted writers. Confirm no dropped/uncommitted/boundary
   rejected records, exact generation/file identities and profile95 flags3.
   An immutable copy gets its own device/inode; record hashes linking it to the
   quiescent source. Never decode a changed or unbound live file as a snapshot.
   Decoder exit0 establishes structure/profile policy; analyzer exit0 with the
   expected profile establishes its stricter coverage gate. Check both,
   `presentation_capture.failures`, strict native triplets, native losses and
   observed CLOSE ticket/discard accounting. Missing/duplicate/stale identities
   or tail records fail complete-capture acceptance; retain partial observations
   with that status rather than filling missing cost with zeros or selectively
   hiding bad companions. Historical records from other generations remain
   excluded and are not stale current-generation evidence.
7. Retain only a source-bound private result with exact capture coverage and
   uncertainty. Compare queue admission, drawable wait, commit/completion and
   native consecutive intervals by their respective identities/clocks. 91→92 is
   post-reset signal/finish observation including any optional retirement work,
   not isolated signal cost. Keep CPU Present, actual native drawable cadence,
   per-second observer aggregates and phase evidence distinct. No baseline or
   performance improvement follows from this diagnostic capture alone. Apply
   the7-day/64MiB raw cap and active/analysis/recovery holds from README; stop all
   task-owned collection at acceptance through its existing owner.

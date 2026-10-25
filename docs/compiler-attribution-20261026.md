# Bounded compiler attribution

Verification date: 2026-10-26. Owner: VibeWine DXMT; consumer: the existing
immutable telemetry decoder/analyzer. This adds scopes to the existing opt-in
v2 trace producer, with no service, session, background writer, new ring or
capacity increase. Absence of events is not evidence of zero work.

All intervals below use the producer's **CPU steady-clock nanoseconds**. They
measure elapsed observed scopes, including any blocking inside a native call;
they are not thread CPU time, backend internal queue residence, total scheduler
cost, critical-path cost or an FPS result. Native GPU epochs are never subtracted.

| Type | Scope | object | flags | detail0 | detail1 |
| --- | --- | --- | --- | --- | --- |
| 104 | cache reader mutex acquisition | cache birth serial | 1 acquired, 3 exception before acquisition | parent work birth serial, or 0 if unavailable | read operation serial |
| 105 | AIR conversion proc | parent work birth serial | 1 returned success, 2 returned failure, 3 exception | unique stage operation serial | 0 (cold conversion) |
| 106 | native library creation | parent work birth serial | same result flags | unique stage operation serial | 0 cold, 1 cached data |
| 107 | native function creation | parent work birth serial | same result flags | unique stage operation serial | 0 cold, 1 cached data |

104 starts before attempting the actual mutex and stops immediately after its
acquisition. It publishes after the mutex is released, including exception
unwinding. Native get exception after acquisition still has a successful 104;
no checked-get result is invented. Existing event 93 keeps its original meaning:
time in checked native cache get after lock acquisition, object=cache birth,
detail0=read operation serial, detail1=0, flags=raw native SQLite status.
93 also retains its original generation admission after acquiring the lock.
If capture changes during the wait, 93 may remain standalone; the old wait
cannot be relabeled into the new generation. For an unchanged capture,
104.detail1 joins 93.detail0 using exact generation/process/cache identity;
104.detail0 links to a task only when its exact generation is available.

The shader lexical scope copies only immutable work serial and submission
generation from `ThreadpoolWork`. Nested calls and exception unwinding restore
the prior thread-local context. Context never retains a task pointer. Each
native/AIR operation gets a fresh serial; rejected cached library/function may
be followed by cold conversion and creation under the same parent work.
Results describe the existing caller checks and do not repair or reinterpret
native failure handling. Existing PSO, wait, scheduler and persistence events
retain their prior meanings.

Full/default and `compile-waits` admit these optional details. They require the
existing `DXMT_CS2_TRACE_DETAIL_LIMIT` (default off, maximum 32,768 records),
share its generation budget and fail-closed stale/budget markers, and are
excluded from `presentation-waits` and invalid profiles before recording.
The selection and budget configuration remain immutable per producer process.
Unarmed/profile-excluded stage scopes and stage contexts belonging to a
noncurrent generation perform no timestamp reads or operation serial allocation.
They produce no stage event. A capture race after this admission check still
passes through the existing fail-closed publication gate; stale reader wait
publication also retains that gate. A task captured in an old generation cannot
be relabeled as work in a later generation. Exhausting the detailed budget stops
ordinary record publication, but armed stage attempts can still allocate an
operation serial and read clocks before the existing budget gate rejects them;
this patch claims no zero overhead after budget exhaustion.

Data contract: numeric identities, result/route codes and monotonic timestamps
only; no shader bytes, keys, filenames, SQL text or personal content. Storage
and ownership remain the existing process-lifetime trace: 131,072 fixed 64-byte
slots plus 4,096-byte header. The source is bounded, append-only and never grows
or rotates in this patch. Capture STOP, immutable export, retention/holds and
cleanup use the existing telemetry session owner and the VibeWine hub
`tools/telemetry/README.md` Producer/retention contract: raw captures and decoded
derivatives expire after 7 days with an 80 MiB aggregate cap per experiment;
minimal results/failure evidence retain at most 30 days / 4 MiB. Cleanup is manual
through that owner, after checking exact identity/references/holds and preserving
required originals; no automatic purge is installed. No standalone
collector or alternate retention mechanism is installed. This patch creates
no additional retained capture copy; temporary tests have a 64 MiB byte cap,
20-second process bounds, and automatic cleanup on exit. Active/held captures
and required audit/recovery originals remain under their existing owner.

Focused verification: `tests/vibewine/compiler-attribution/run.py` compiles the
actual helper/admission/profile headers plus production-extracted task base,
shader compile task and cache read method. Only OS/WMT/compiler endpoints and
clock-call counting are shims. It covers cold/warm success, cached failure and
fallback, AIR/library/function failure/exception, unavailable reader, actual
contended mutex, no tracing while disabled/profile-excluded/closed, nested and
thread-isolated context, stale work, arm/rearm during a real contended reader acquisition, and budget exhaustion. Source mutations
that remove parent binding or start the timer after acquisition must fail.
The existing profile producer fixture checks additive profile policy.
Full matched PE/native build and real application captures remain separate
acceptance gates; these CPU fixtures establish no FPS improvement.

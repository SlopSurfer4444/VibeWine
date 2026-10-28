# Opt-in thread and Quit lifecycle evidence

This instrumentation helps diagnose a future occurrence. It does not repair or
classify a Steam hang. An earlier Steam birth had a held critical section whose
recorded owner was absent; a later fresh native Dock Quit completed normally.
The earlier owner's exit cause and its timing relative to Quit remain unknown.

The dedicated Wine channel is `vw_lifecycle`. Wine's current channel-name ABI
allows at most 14 characters; the longer descriptive name cannot be registered
without changing that ABI. Enabling this channel is an explicit diagnostic
launch choice. Ordinary play must keep its existing debug environment. The
runtime/control owner supplies and supervises the opt-in launch; these source
changes do not enable the channel or start a collector.

Four translation units contain small local admission functions. The disabled
path checks `TRACE_ON(vw_lifecycle)` before reading or changing its counter or
performing ownership queries. Each enabled counter uses compare/exchange and
saturates. The last slot emits one quota marker and excludes that triggering
payload; further attempts emit nothing. Limits apply per translation unit in
each loaded module image:

| Source | Total lines, including one marker | Observations |
|---|---:|---|
| `dlls/ntdll/thread.c` | 4096 | `RtlExitUserThread` entry and requested status |
| `dlls/ntdll/unix/thread.c` | 4096 | Successful thread creation; termination request/result |
| `dlls/ntdll/sync.c` | 64 | Existing critical-section timeout and owner query |
| `dlls/winemac.drv/dllmain.c` | 512 | Quit entry/window count, query/end sends and callbacks, reply |

Creation records contain caller and target **Windows** PID/TID, start address,
returned handle, creation flags and local/remote route. They occur only after
final attribute processing succeeds. The new local record uses the already
captured server TID, without adding a dereference of a child TEB after its thread
can exit. A remotely created thread can produce both a target-side local record
and a caller-side remote record. A fast child can exit before its creator emits
the successful-return record. Event order is not a guaranteed execution order.

`exit_user_begin` records intent before DLL/FLS teardown; it does not assert
that teardown completed. Termination records distinguish request and server
result, including caller IDs, handle, requested exit status, result and `self`.
The admitted request record also captures the target's basic information before
termination. A current-thread pseudo-handle uses the caller's Windows IDs
directly; any other handle gets one `ThreadBasicInformation` query. Attempt,
status and known flags distinguish unavailable identities, and failed-query
fields are cleared. This does not close or change a foreign handle. A target
can exit between that snapshot and the termination request; the record is not
an atomic identity guarantee across those operations.
There is no new logging in `abort_thread`, `exit_thread`, or signal handlers.
Server/native termination outside these entry points may remain unobserved.
Handles and IDs can be reused: bind observations to the same process birth.

The critical-section event runs only at an existing timeout. It records the
section pointer, owner before/after, lock/recursion counts, raw open/query status,
attempt flags, queried identity and exit status. It requests only limited query
access and closes each successfully opened handle. Wine's current `NtOpenThread`
uses TID alone, so the returned PID and TID are explicitly compared with the
expected identity. Failure to open a TID, including `STATUS_INVALID_CID`, is an
observation and not proof of a dead owner. Unattempted/failed query fields are
unavailable. Equal owners before/after do not exclude ABA/TID reuse; the fields
are not an atomic consistent snapshot. No lock is acquired/released or repaired,
and no timeout, retry, termination or admission policy changes.

Quit events preserve the current generic protocol and its reference ownership.
Payloads are emitted before their referenced request can be released. Reply
records describe the PE request to the native side, not confirmed AppKit receipt
or process exit. Native Cocoa reason/queue/reply instrumentation is omitted:
that file currently exposes a different logging interface, and adding a new
bridge was outside this first candidate's agreed scope. No callback ABI changes.

## Focused verification

```sh
python3 vibewine/tests/lifecycle-diagnostics/run.py --sanitize
python3 vibewine/tests/lifecycle-diagnostics/run.py --sanitize --arch x86_64
```

The native fixtures compile the current admission functions and event emitters
verbatim. Eight host threads race each actual counter beyond its quota; the
runner requires exactly the ordinary admissions plus one marker, a saturated
counter, and no counter changes while disabled. Native NT API seams exercise
existing/missing/changed/mismatched owners, query failure and absent owner,
including successful-open handle closure and no queries while disabled or
saturated. Termination target cases cover valid, denied and invalid queries,
self pseudo-handles and no queries while disabled or saturated. Thread payload
tests check the actual emitted values. Source binding
checks keep these emitters attached to success, termination and timeout sites.

The Quit fixture extracts the current eligible-window selection, admission,
trace helper, reply, request and callback functions. It checks real emitted
phases/fields against the responsive, veto, zero-window, synchronous/deferred,
allocation/enumeration/send-failure and realloc cases. Channel-on/off runs must
preserve reply, termination, allocation and ownership behavior. This is native
source-level evidence with API seams, not a full-module or live-Wine test.
Root owns full PE/Unix builds, signed package closure, opt-in launch and live
admission. No gameplay or performance claim follows from these checks.

The test runner bounds compiler calls to 60 seconds, native processes to 10
seconds and temporary output below 64 MiB; `TemporaryDirectory` removes ordinary
scratch on exit. It starts no background producer. A crash leftover is private
test scratch with a seven-day manual-review TTL and 64 MiB aggregate cap; retain
active runs and evidence holds. Durable fixtures/docs are project source.

For the current investigation, root owns private diagnostic output under
`outputs/integrated-wave-20261023/steam-quit-recurrence-20261028`; its existing
`capture-contract.json` governs the 64 MiB aggregate evidence cap, 16 MiB trace
cap and 90-day manual-review TTL, with unresolved/recovery holds excluded from
cleanup. Root must bind and stop each opt-in collector at its declared deadline;
per-module event quotas do not enforce file size, elapsed time or vendor data
retention. No cleanup service, vendor retention change or automatic purge is
introduced.

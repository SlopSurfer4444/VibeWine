# Integrated wave source checkpoint — 23 October 2026

This development checkpoint ports the admitted installed-era experiments onto
`bee453b09460e0c3eb49b7c491a6d04e65cc38a5`, retaining its reviewed backports.
It is not a binary-build, installation, gameplay or FPS receipt.

- Process-local XInput comes from `fbe0e3de434aa611b352aae661eacf87c4ee2f85`.
  Its image/token/PID/sequence admission and reader-side expiry remain mandatory.
- Native AX Raise, visibility and drawable-phase diagnostics come from
  `4e6b235421bf3ded37b08ca12928e1998285c390`. Passive getters remain passive.
  Visibility serialization nil/exception/oversize now always worker-completes
  with nil, permitting the startup callback to close its directory descriptor.
  Retained collector SHA256 `daf3feb2ec3de6ebc800ce86f5da121d00376a18ff3eb5f22491dc083dae39f3`
  changes to `17135acf127b54e529fc5aaa20a18c195fc3031f0d024bc51ec0d77a46f195dd`.
- Metadata-only networking comes from `0b4261775ea2016db16d7a77aa1e3586c808073f`.
  Both shared headers are registered in `include/Makefile.in` for fresh makedep.
  Source instrumentation is not a network-connectivity repair.
- Paired MSync ACK parking adapts Dormison
  `c9966373a268a1fdafe398634c19be653c674ed0`, preserving prior unregister fixes.
  Bounded spin, deadline-aware park, paired wake and dead-send-right checks are
  local adaptations; direct-object spinning and atomic WaitAll are not imported.
  The client/server must be built/deployed together. Wire sizes are unchanged.

Original authors, Wine notices and LGPL terms remain intact. Existing source
histories have different bases; these are scoped semantic ports, not a blind
whole-branch merge. `source-overlay.json` binds every changed/new file.

## Focused acceptance and limits

XInput native expiry checks, exact predecessor controls, passive AX/action
fixtures, network logger/hook/readiness fixtures, C/Foundation diagnostic mocks,
actual client/pump ACK fixtures and real Darwin ulock/Mach-right tests passed.
The integration owner independently reran the candidate ACK/native controls,
native diagnostic suite and network logger. Relevant complete source syntax
checks are separate from a newly linked whole runtime. See each test README.

The MSync tests cover ACK-before-park/park-before-ACK, delayed server, interrupt,
error/unregister paths and infinite-wait slice reset. Blocking Mach send and
whole Wine process lifecycle remain outside their acceptance. Netdiag stop/expiry
is not writer quiescence; a previously in-flight record may commit after stop.
Queued native startup work requires its main request source to execute.

## Optional producer data contract

All these experiments default off. Their owner is the explicitly created private
session/test runner, not a permanent system collector. No installer enables them
from source presence alone.

| Producer | Exact scope/data | Source bound | Retention/cleanup |
| --- | --- | --- | --- |
| Network diagnostics | One admitted image/process/session; scalar socket identities, endpoints, durations, results; no payload, DNS names or credentials | One-shot at most45s;16384 records;2113536-byte mapping | Raw private capture7days, pinned failure holds excluded; runner/manual exact-session cleanup after writer exit; stop is not quiescence |
| XInput diagnostics | One admitted producer session, bounded status/ack replacement files | Diagnostic sidecar below2048bytes; ACK buffer180bytes, unchanged-poll heartbeat100ms, command/state transitions may acknowledge immediately | Session lifetime;7day failed-session evidence hold; disarm/verify producer exit before exact-owned cleanup |
| Native visibility | Opt-in exact DS3 startup scope; restricted geometry/lifecycle JSON | One startup-visibility.json at most32768bytes per process | Private test-session7days unless pinned unresolved evidence; close/exclusive publication guards; no traversal cleanup |
| Drawable phase diagnostic | Opt-in process stderr scalar records, no images |128 attempted records per each of2translation units | Capturing runner bounds total stderr;7day raw TTL unless pinned; source installs no unbounded log sink |

These are policy limits with explicit source caps; no automatic deletion service
is installed. Active producers, user data, saves, caches, credentials and unknown
objects are excluded from cleanup. Summary/source evidence is retained with its
issue while material findings remain unresolved.

## Terminal-thread shutdown follow-up

Both `abort_thread` and normal `exit_thread` now block SIGQUIT locally before
their one terminal thread-count decrement. The global server/RPC signal mask is
unchanged. A real pipe EOF or normal worker retirement followed by a real
thread-directed SIGQUIT at each of the five actual descriptor-close boundaries
previously reentered `abort_thread`, decremented twice and could terminate the
host with zero while a live main thread intended status 20. The exact production
bodies are exercised by `vibewine/tests/thread-exit/run.py`; the native fixture
keeps the server mask, atomics, descriptor cleanup and pthread exit, substituting
only Wine-specific GS/TEB access and host platform dependencies.

Baseline: 40 injected regressions plus 16 expected controls. Candidate: 224 native
and 112 ASAN/UBSAN cases passed, including main-thread mask preservation, all
five FD closes, normal worker status and legitimate pre-retirement SIGQUIT. The
integration owner independently repeated 28 candidate cases. These controlled
results establish the race, not the cause of an earlier GPU-guest status mismatch
or a gameplay/FPS improvement. Matched Wine PE/native execution remains a separate
acceptance step.

# Independent bounded review: borderless watchdog successor

Reviewed source SHA-256: `d64b57d5d387489028ff17a72bcff02479ad4290b3c292b23da182be48dd0d30`.

Verdict: no blocking finding in the reviewed watchdog-generation, sticky-mutation, target-context and wait-target delta. This is source/offline-fixture acceptance, not live Wine validation or installation approval.

- Generation/deadline comparison and expiry claim are serialized with arm/disarm. An observation predating disarm/rearm cannot expire the successor operation.
- No watchdog state lock spans window or registry operations. After expiry claim, subsequent phase transitions park instead of initiating another write.
- Both actual production write wrappers set sticky mutation before the Win32 mutation call. This means mutation attempted/may have begun, not confirmed successful. Sticky session state intentionally remains true after verified rollback or later read-only follow cycles.
- Prior P2 diagnostic issue addressed: explicit watch_target replaces/clears PID and HWND; startup selection clears the prior candidate, new enumeration clears the prior HWND, and apply/restore validation has its own phase.
- wait_target preserves the existing bounded discovery behavior and distinguishes its wait horizon from an armed synchronous-operation timeout.

## Independent checks

Ran `tests/borderless-watchdog/run.py` against the successor source, output `/private/tmp/watchdog-review-offline-successor`:

- 32 assertions passed.
- Clang C11, Wall/Wextra/Werror, AddressSanitizer and UndefinedBehaviorSanitizer.
- Extracted actual production watchdog methods, wait_target and mutation wrappers; deterministic fake-clock transitions cover stale disarm/rearm, one-shot claims, pre-write sticky mutation, prevention of the subsequent write after expiry, target-context clearing and delayed discovery.

## Limits

- Fixture uses deterministic interleavings, not competing native main/watchdog threads or Wine SRW-lock implementation.
- stdout reporting/flush before ExitProcess remains potentially blocking; this patch does not guarantee termination if diagnostic output stalls. This is an existing architectural limitation, not repaired here.
- Phases are coarse operation context, not a precise per-API trace. A true mutation flag is conservative and cannot prove a successful change.
- No live Wine, application control, game execution, installed runtime, canonical source or manifest mutation performed by this reviewer.

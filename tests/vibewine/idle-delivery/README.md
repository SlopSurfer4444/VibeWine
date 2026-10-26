# Idle diagnostic delivery

The opt-in idle producer uses `Logger::diagnostic(const char *)`, which shares
normal logger initialization, Wine `ntdll!__wine_dbg_output`, fallback stderr,
mutex and optional `DXMT_LOG_PATH` file. Only this explicitly selected entrypoint
bypasses `DXMT_LOG_LEVEL`; ordinary logger filtering and file naming are unchanged.
Its return value acknowledges route acceptance, not durable delivery: Wine itself
does not return the underlying Unix `write(2)` result. A caller must not retry
indefinitely or treat `true` as a capture receipt.

The sink accepts a nonempty single line, with an optional trailing LF, at most
1011 payload bytes. It rejects CR, embedded LF and overlong input before output.
The `info:  ` prefix and LF keep each call below Wine's 1020-byte debug buffer
limit (1019 bytes at most). Callers supply accessible NUL-terminated input.

`IdleRetirementDiagnostics` acknowledges `DXMT_IDLE_ATTRIBUTION=1` on its first
record with `VIBE_IDLE_START pid=... queue=... enabled=1 time_ns=...`. That line
establishes enablement and the route, and is not a pool sample. It consumes one
of the existing 64 records per queue. Up to 63 samples follow at intervals of at
least ten seconds. An absent/zero flag produces neither acknowledgement nor
samples. Missing output cannot by itself distinguish a disabled producer from
an unavailable route; the fixture uses an independent Win32 receipt for that.
The counters remain partial pool-reference observations, not physical memory or
process RSS measurements.

The finish thread owns this producer. Scope is an explicitly enabled queue;
data is numeric PID/queue/time and partial pool counters; output is at most
65,216 bytes/queue (64 x 1019) over its lifetime. It adds no timer, worker,
collector or retry loop. Existing launch-log storage/rotation/TTL and active
capture/recovery holds own retention; this code creates no separate diagnostic
file unless the existing `DXMT_LOG_PATH` configuration requests one.

Run the host fallback/validation contract with ASan and UBSan:

```sh
python3 tests/vibewine/idle-delivery/native.py
python3 tests/vibewine/idle-retirement/diagnostics.py
```

The separate actual GUI-subsystem PE fixture compiles the **production** logger,
environment and string implementation with an explicit existing LLVM-MinGW
compiler. Supply an existing trusted local Wine engine and a fresh project output
directory:

```sh
python3 tests/vibewine/idle-delivery/run.py \
  --cxx /absolute/llvm-mingw/bin/x86_64-w64-mingw32-clang++ \
  --wine /absolute/trusted/engine/bin/wine \
  --output /absolute/project/fresh-idle-delivery-run
```

It creates and initializes an isolated Wine prefix and exact inode-bound Unix
socket directory. A sandbox permits writes only there and to the fresh output,
disables IP networking and Launch Services, and disables the macOS Wine display
driver and menu builder. The fixture creates no windows and loads no game.
It waits for the private server to settle after initialization; registry files
are not necessarily persisted when `wineboot` itself returns. After all cases
(or a failure) it stops and waits only that prefix's server, then removes the
exact newly-created prefix. Installed prefixes, runtimes and applications are
never written. Tool arguments are explicit; this script installs nothing.

Cases verify absent/zero opt-in; old `fputs` with an invalid Windows stderr;
the patched sink with invalid and usable Windows stderr; old `fputs` with usable
stderr; and ordinary normal-log behavior at `none` and `info`. Each guest writes
a separate Win32 receipt with PID, effective enablement, record attempts, old
stdio failures, handle validity and exit code. Positive cases require exactly
one enable acknowledgement plus 63 samples captured from Unix fd 2, with pool
unknown-tail data and correct prefixes, while the old invalid-handle candidate
must fail all 64 stdio attempts and capture no records. The old usable-handle
candidate must write 64 records to its owned Windows file. The formatter test
separately exercises rate, clock regression/zero epoch, count and numeric-size
bounds; native coverage tests fallback failure and configured log-file delivery.

Retained fixture artifacts are source-bound receipts, bounded logs, sandbox
profile and executable; they belong to the requesting task's evidence retention
contract. The private prefix is transient test data, removed after the terminal
server wait; active/unknown execution must be reconciled before cleanup. These
checks prove fixture delivery only and make no installed-game, FPS, stall-cause,
physical-residency or RSS claim.

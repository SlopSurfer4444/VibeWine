# Process-local XInput experiment

This off-by-default test backend supplies slot 0 only inside one opted-in process.
It creates no host input events, system controller, foreground request, or global
Wine device. While opted in, other indices are disconnected and the HID backend
is not started. Missing opt-in, malformed configuration, or a different executable
preserves the ordinary Wine path. This experiment does not make an inactive game
poll input or resume its simulation.

Set all three environment variables before loading the builtin DLL:

- `VIBEWINE_XINPUT_TEST_FILE`: absolute ordinary Win32 file path, e.g.
  `Z:\private\test-session\input.bin`; no device, UNC, or pipe paths.
- `VIBEWINE_XINPUT_TEST_TOKEN`: fresh 32-character lowercase hex session token.
- `VIBEWINE_XINPUT_TEST_EXE`: exact full Win32 executable path, compared without
  case sensitivity against `GetModuleFileNameW(NULL)`.

Remove these variables and restart the process to deactivate. Neither registry
nor runtime installation is part of the backend or its tests. Use a private
session directory and never reuse tokens. The target path scopes the opt-in to an
executable; the first PID-zero ARM can be claimed by any concurrent instance of
that executable. Start only the intended instance and bind the returned Windows
PID before sending further commands. This is a trusted local test transport,
not an authentication boundary against another process running as the same user.

## Wire protocol version 1

Publish a complete 72-byte file by atomic replacement. All numbers are little
endian. Partial files, files larger than 72 bytes, unsupported flags/buttons,
wrong session/PID, invalid lease, and older sequences neutralize and disarm.
Rejected records do not advance the last accepted sequence.

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 8 | ASCII `VXIP0001` |
| 8 | 32 | lowercase hex token, no terminator |
| 40 | 8 | sequence, unsigned and strictly increasing, starting above zero |
| 48 | 4 | lease in milliseconds, 1 through 1000 |
| 52 | 2 | XInput button mask (standard buttons and Guide only) |
| 54 | 1 | left trigger |
| 55 | 1 | right trigger |
| 56 | 8 | four signed 16-bit axes: LX, LY, RX, RY |
| 64 | 4 | Windows PID; zero permitted only for the first neutral ARM |
| 68 | 4 | flags: 1 ARM, 0 STATE, 2 RELEASE |

ARM and RELEASE require a completely neutral gamepad. ARM makes the backend
ready; STATE requires a live ARM. RELEASE unarms and returns neutral. Once an ARM
has been accepted, all later records including ARM require that Windows PID.
The lease uses a fresh `GetTickCount64()` value after command file I/O and
validation when a new sequence is accepted;
repeated reads never refresh it. A command may be observed after the writer has
stopped, so each hold is limited to at most its lease **after receipt**, not after
host publication. Read operations are rate limited to once per 4 ms; expiry is
checked on every query, again after command I/O before accepting a new sequence,
and after receipt/diagnostic I/O immediately before the state copy or keystroke
processing. An expired ARM cannot authorize a held STATE that was being read.
A fresh neutral ARM accepted after slow I/O receives its lease from the fresh
acceptance clock. There is no bound on scheduling after the final handoff.
Expiry, malformed/missing input, replay, release, or
`XInputEnable(FALSE)` returns neutral and requires a new neutral ARM before held
STATE. Enabling never replays a held state. Repeating Enable(TRUE) is harmless.
A disabled backend continues returning a connected, neutral controller.

`GetState` masks Guide; ordinal 100 `GetStateEx` preserves it. Packet numbers
change when the gamepad changes, including neutralization, and stay stable on
repeated polls. Capabilities report a gamepad without physical force feedback;
SetState succeeds for slot 0 without any hardware transmission. GetKeystroke uses
the same gamepad transition code as the ordinary provider. A previously observed
keystroke DOWN is followed by UP when queried after disable/neutralization; edges
during intervals with no keystroke queries are not queued. Battery/audio queries
return unsupported for the synthetic slot.

## Poll receipts and limits

The reader atomically replaces `<input file>.ack` with one ASCII line:

```
VXIA0001 token pid sequence pollcount enabled status
```

`sequence` is the last accepted record; `pollcount` counts actual slot-zero
GetState/GetStateEx calls. Status is READY, STATE, RELEASE, EXPIRED, INVALID, or
DISABLED. ACK output is throttled to 100 ms, with accepted commands and enable
transitions requesting the next immediate receipt after an actual state poll.
GetKeystroke may update the backend but cannot publish a poll receipt; the next actual GetState poll does.
Enable transitions do not publish receipts; re-enabling reports INVALID until a
fresh ARM.
Consumers must validate token, PID, requested accepted sequence/status, and a
poll count higher than their pre-command baseline. A receipt proves an actual
DLL state poll and acceptance of the reported sequence at the ACK boundary, not game simulation, screen changes, or gameplay. ACK file
I/O can itself cross a lease: the final handoff then returns neutral even if the
earlier ACK says STATE. An ACK is therefore not proof that a held value reached
the caller or that the caller subsequently resumed execution.

Each XInput DLL compiled from this source owns independent backend state. When
multiple versions are loaded in one process, each must see ARM, and their shared
ACK path is last-writer-wins. This version therefore supports trustworthy receipt
attribution only when the process uses one proven provider. It does not claim
cross-module synchronization or combined poll counts. xinput9_1_0 delegates to
xinput1_4 and therefore uses that module's state. Native overrides/hot patches can
bypass this builtin path and must be verified separately.

## Optional diagnostic snapshot

`VIBEWINE_XINPUT_TEST_DIAGNOSTICS=1` additionally enables observations only after
all ordinary path/token/executable opt-in checks pass. Other values or an absent
flag create no diagnostic output. It adds no input transport, timer, worker,
foreground request, or synthetic GetState poll. Use a fresh private session for
each run; an old sidecar does not establish current activity.

Actual valid slot-zero state, keystroke, or enable calls may atomically replace
`<input file>.diag` via `<input file>.diag.tmp.<WindowsPID>`. It is a single JSON
object with LF, version1, below2048bytes; write attempts start at most once per
100ms. This bounds storage/frequency, **not synchronous filesystem latency**.
Compare paired diagnostics-off/on behavior before attributing a timing change to
the game. Diagnostic I/O runs before the final expiry guard and preserves the
underlying LastError. It cannot extend a lease.

The schema contains:

- `token`, `pid`, `module`: session token, Windows PID and sanitized DLL basename,
  never a filesystem path. Same sole-provider admission as the ACK is required.
- `snapshot_tick`, `sequence`, `status`, `enabled`, `armed`, `deadline`: captured
  backend state before that snapshot's own I/O and final handoff. It may already
  be stale when read. Tick/deadline fields are process GetTickCount64 milliseconds,
  not wall time or host monotonic-clock values.
- `accepted_sequence`, `accepted_via`, `accepted_tick`: last newly accepted
  command; via1 means GetState/GetStateEx, via2 means GetKeystroke,0 means none.
  Key-only consumption never advances ACK GetState pollcount or writes an ACK.
- `state_entries/exits`, `key_entries/exits`, `enable_entries/exits`, each with
  `*_enter_tick`, `*_exit_tick`, and `*_thread`: atomic call counts/timestamps
  and last-entry **Windows** thread ID after opt-in and argument validation.
  Entries are recorded before acquiring the backend lock. Exits denote the
  final pre-copy/pre-keystroke-processing/pre-return handoff while holding that
  lock; they do not prove caller resumption. The current call's exit appears
  only in a subsequent snapshot. Counter/timestamp/thread fields are sampled
  separately: this is not an atomic multi-field sample. Timestamps are bounded
  best-effort atomic maxima and may omit an update under repeated contention;
  thread IDs are not paired atomically with ticks and are not native Mach IDs.
- `handoff_tick`, `handoff_sequence`, `handoff_api`, `handoff_status`: last final
  expiry-checked handoff retained in memory. API1=state,2=key,3=enable;0/NONE means
  no prior handoff. A later snapshot exposes the previous handoff, not its own.
- `enable_transitions`, `enable_tick`: actual boolean transitions, including
  disable; repeated Enable(TRUE) does not count as a transition or rearm.
- `io_entries/exits/failures`, `io_stage`, `io_enter_tick`, `io_exit_tick`,
  `io_error_stage`, `io_error`, `io_error_tick`: command/ACK operation boundaries
  and cumulative failures with the last failure's Windows error. Stages1..8 are
  command open/type/read/close and ACK open/write/close/rename. A short successful
  ACK write records synthetic ERROR_WRITE_FAULT; failed Win32 calls retain their
  immediate error. The cleanup DeleteFile calls are not separately instrumented.
- `snapshot_failures`, `snapshot_error`: optional sidecar failures, visible only
  if a subsequent snapshot succeeds. Successful partial snapshot writes are
  likewise ERROR_WRITE_FAULT. These fields exclude ordinary command validation
  failures, which are represented by backend INVALID status.

Missing output, unchanged mtime, no new counter, or a difference between entries
and exits cannot independently prove whether no API was called, initialization
or lock acquisition blocked, synchronous I/O blocked, the game paused, or the
snapshot writer failed. Memory entry/I/O boundaries cannot publish themselves
while the same call is blocked. Use the retained Windows thread IDs as a clue
with an independently identity-bound OS stack sample, rather than treating the
sidecar as a live watchdog. It does not observe Metal nextDrawable, native Space
membership, Windows activation messages, or game simulation.

Run `python3 dlls/xinput1_3/tests/process-local/run_native_expiry.py` for native
fault injection using the entire unchanged shipping header and Win32 clock/I/O
mocks. The pinned predecessor must fail the new expiry-crossing cases and the
candidate must pass. This executes no Wine and proves no real filesystem timing
or background-game behavior. The executable fixture additionally supports mode
`diagnostics` (same arguments as `enabled`) for the full DLL API regression plus
bounded JSON binding, key-only acceptance, and unchanged ACK checks. Root must
run actual default/enabled/diagnostics console modes in the private quarantine
before any game candidate is admitted.

## Build and executable regression fixture

`build.py --configured-build PATH --toolchain PATH --output FRESH_PATH` reads a
configured Wine Makefile and compile database, recompiles the complete source,
and links private x64 xinput1_3/xinput1_4 DLLs using existing Wine tools/imports.
It also links the same compiled objects with unique internal DLL names for fixtures.
It also builds `fixture.exe` with LLVM-MinGW. Inputs remain read-only; build logs,
commands, source hashes, output hashes, and PE export/import inspection are
written under FRESH_PATH. The configured build's generated headers, original
header tree, resources, and import libraries must match this Wine base. No
installation, prefix creation, Wine execution, signing, or game launch occurs.

Run the console executable using a disposable Wine runtime/prefix. Use the
`vibewine_test_xinput1_3.dll` or `vibewine_test_xinput1_4.dll` fixture artifact
with default load order. These link the exact same compiled objects as the
normal-name artifacts but set a unique **internal export name** using Winebuild
`--filename`. Merely renaming a file is insufficient: Wine's builtin search uses
the PE internal export name and can map the runtime's existing xinput1_3 while
still reporting the requested path. A native-only (`=n`) override also rejects
the builtin marker with error 126. The fixture checks and prints the actual
module path; retain that evidence, the unique internal-name inspection, and the
candidate hash. This avoids modifying an engine or installed provider.

```
fixture.exe enabled ABSOLUTE_DLL_PATH ABSOLUTE_TRANSPORT_PATH
fixture.exe default ABSOLUTE_DLL_PATH ABSOLUTE_TRANSPORT_PATH
fixture.exe wrongprocess ABSOLUTE_DLL_PATH ABSOLUTE_TRANSPORT_PATH
```

Each invocation uses a fresh process and its own transport path. Valid commands
are polled until the DLL acknowledges their exact sequence (bounded at 250 ms);
invalid commands are polled until neutral. This accommodates the coarse Windows
GetTickCount64 clock without assuming that an 8 ms host sleep advances it. `enabled` tests
real exported DLL calls: ARM, buttons, GetStateEx, packets, capabilities, keystrokes,
release, lease expiry, no stale reactivation, disable/enable/rearm, malformed data,
wrong PID, wrong token, replay, rejected high-sequence recovery, deletion, and
oversized lease. `default` and `wrongprocess` exercise ordinary slot 0 and require
that no ACK is created; an actual physical controller may be connected or disconnected. Their
return code is recorded rather than assuming the host has no controller. No
visible window or foreground call is used.

For an actual host companion integration, launch:

```
fixture.exe listen ABSOLUTE_UNIQUE_DLL_PATH DURATION_MS
```

`listen` preserves the three inherited opt-in variables, creates no snapshots,
and polls slot-zero GetState every 4 ms for a bounded duration (1 to 45000 ms).
It prints only changed returned states plus a final poll count and Windows PID.
The host can ARM/STATE/RELEASE and observe expiry against the real DLL and ACK
transport. A successful poll/receipt remains distinct from a game action.

The regression modes create and replace their supplied transport and ACK files; give them a
disposable directory. Do not point it at the game's live transport. Run against
both DLLs separately. Fixtures and build success are separate from installation
and real background-game acceptance.

## Validation recorded 2026-10-18

The complete x64 xinput1_3 and xinput1_4 sources compiled and linked, including
normal and unique-internal-name fixture DLLs. PE imports/exports and the internal
export names were inspected. The executable fixture compiled with
`-Wall -Wextra -Werror`. In an isolated Wine runtime with a fresh private prefix,
the unique-name xinput1_3 DLL passed enabled mode (415 assertions), default mode
(11 assertions), and wrong-process mode (11 assertions), all with zero failures.
The actual loaded fixture path was checked. Prefix wineserver cleanup completed
with exit code zero. The unique DLL tested had SHA-256
`db2fe1dba35dac3de6a885ab16bfe3287f70b192bfdd4c68fe335a0e7b16c823`.

This is narrow exported-API/transport regression acceptance. xinput1_4 was built
and inspected, not executed in that regression. No production provider was
installed, and background DS3 behavior or FPS was not tested by these checks.
The implementation and fixtures were AI-assisted; source review, compile logs,
PE metadata, real exported-API results, and the staged source diff were checked.

## Expiry/diagnostic candidate verification (2026-10-19)

The entire shipping header passes31 deterministic native cases. The pinned
10557ac predecessor fails all15 deadline-crossing faults and passes2 ordinary
controls. Native diagnostics cover blocking I/O at each snapshot handoff,
100ms throttling, separate key consumption, prior-handoff observations, short
writes and LastError preservation; worst-value serialized JSON is1480bytes.
These are single-thread mocks, not filesystem or game scheduling evidence.

The private build-v4 compiled full x64 xinput1_3/xinput1_4 sources, uniquely named
fixture DLLs and the console executable using the existing configured Wine
build. All15 compile/link/inspection commands passed, and export tables matched
the prior fixture build. No installed module was replaced. Root then ran the
unique xinput1_3 DLL in a fresh isolated Wine prefix with networking denied:
enabled419/default11/wrongprocess11/diagnostics483 assertions, all0 failures;
scoped wineserver cleanup exited0. The diagnostics mode checked actual JSON
binding and GetKeystroke acceptance without a GetState ACK. Retained private
evidence is background-xinput-expiry-live-v4/result.json. This clears only the
console candidate; inactive/offscreen DS3 progress, cursor behavior, Space
handling and renderer behavior still require separate evidence.

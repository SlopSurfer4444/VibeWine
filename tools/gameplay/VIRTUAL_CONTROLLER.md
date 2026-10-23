# Experimental process-local controller writer

This host tool writes virtual XInput controller snapshots for an **optional,
experimental Wine producer**. It does not launch games, install DLLs, change
Wine overrides, move windows, activate applications, switch Spaces, or send
keyboard/mouse events. It is separate from the existing CUA session helper.
No successful background game control is claimed by these sources or host tests.

The intended boundary is one explicitly admitted game process. The producer
checks the exact Windows executable path, a random session token, a bound
Windows PID and a strictly increasing sequence. Windows controller polling,
rather than macOS focus, consumes the state. A game may stop polling while
inactive or disable XInput. That case must fail closed and remains a live
compatibility question. No game-memory reads/writes or gameplay hooks are used
by this host writer.

## Commands

Python 3 and the standard library suffice on macOS. Use a new absolute directory
whose existing parents contain no symlinks. On macOS use `/private/tmp`, since
`/tmp` is a symlink. `init` creates the final directory with mode `0700`; it
refuses an existing directory. The example paths and executable are placeholders.
The Windows snapshot path must map to **that exact directory's `state.bin`** in
the intended Wine prefix. The tool cannot verify drive mappings.

```sh
python3 tools/gameplay/virtual-controller.py --directory /private/tmp/vxinput-session init \
  --exe 'C:\Games\Example\Example.exe' \
  --wine-file 'Z:\private\tmp\vxinput-session\state.bin'
```

The JSON response provides the producer environment values:
`VIBEWINE_XINPUT_TEST_FILE`, `VIBEWINE_XINPUT_TEST_TOKEN`, and
`VIBEWINE_XINPUT_TEST_EXE`. These only affect a producer deliberately launched
with those values; the writer does not apply them or start a game. Keep the
session directory and token private. Configure and admit the isolated Wine
candidate separately before attempting input.

With the admitted producer already running and the game visually observed at a
safe menu, an automation client can issue the following commands. An ARM lease
expires quickly, so issue a desired bounded state promptly after ARM succeeds.
Do not use an unobserved script of repeated actions.

```sh
python3 tools/gameplay/virtual-controller.py --directory /private/tmp/vxinput-session arm --lease-ms 1000
python3 tools/gameplay/virtual-controller.py --directory /private/tmp/vxinput-session state --buttons 0x0002 --lease-ms 120
python3 tools/gameplay/virtual-controller.py --directory /private/tmp/vxinput-session release
python3 tools/gameplay/virtual-controller.py --directory /private/tmp/vxinput-session status
```

For one visually chosen short action, `pulse` keeps ARM, STATE, a bounded hold,
and RELEASE inside one process and one session lock. This avoids expiring ARM
while an automation client makes separate command/tool round trips:

```sh
python3 tools/gameplay/virtual-controller.py --directory /private/tmp/vxinput-session pulse \
  --buttons 0x0002 --hold-ms 120 --timeout-ms 2000
```

`--hold-ms` is 1..500 ms, default 120. The STATE lease equals that budget, and the
budget starts before state publication: receipt latency consumes the budget
rather than adding another full hold interval. ARM has a 1000 ms lease; the
writer rejects a late active state and reserves time for release. `--timeout-ms`
is the whole pulse wait budget, 1..5000 ms (default 2000), rather than a timeout
repeated independently for each phase. Invalid controls are rejected before ARM.
There is no automatic repeat or renewal.

A pulse succeeds only after the expected ARM, STATE and RELEASE poll receipts.
Failure or an interrupt during the state/hold phase still attempts neutral
RELEASE in `finally`; the original failure is preserved, and a release timeout
is reported as unacknowledged. If the whole wait budget is exhausted, the writer
still publishes neutral RELEASE when a PID is bound, but does not call it
acknowledged. Before the first PID receipt, ARM is neutral and release cannot be
addressed to an unknown PID. Reader-side expiry remains the independent fallback
if the writer is killed. Pulse receipts do not prove a game action or preservation
of the active macOS Space. This convenience command has only host mock coverage
in this change; admit its exact source separately before any live trial.

`0x0002` is XInput D-pad Down, not a guarantee of a safe action in the current
game state. The state command also supports `--lt`, `--rt` (0..255) and
`--lx`, `--ly`, `--rx`, `--ry` (-32768..32767). Unspecified controls are neutral.
Reserved XInput button bit `0x0800` is rejected.
All command leases are 1..1000 ms; the default is 500 ms. Each command has a
bounded `--timeout-ms` of 1..5000 ms, default 1500 ms. Command success does not
extend the lease. No command automatically renews it.

ARM is always neutral. The first accepted ARM may target PID 0; its poll receipt
binds the real Windows PID. Subsequent packets, including ARM, use that bound
PID. `state` requires a previously acknowledged ARM; expiry, disablement, invalid
input and RELEASE require a new neutral ARM in the producer. If the first ARM
was received after the writer's timeout, a subsequent ARM can recover only its
PID from the exact previous receipt and still requires a new poll receipt.
A new game process requires a new session and token, not rebinding an old PID.
If the first ARM has no receipt, repeated retries can remain unrecoverable: a
reader may have bound a preceding PID-zero packet whose receipt is unavailable.
Stop retries and prepare a new admitted session/producer launch rather than
assuming PID recovery always works.

On a failed active command the host disarms its local session and publishes a
new neutral RELEASE where possible. The producer must independently expire an
unchanged snapshot within its requested lease, even if the writer is killed or
cannot publish a release. No host process, daemon or timer remains running after
the command returns. Removal of the host writer cannot enforce producer expiry;
that obligation belongs to the Wine implementation and its tests.

`status` sends no input. It reports identity-bound **historical** receipts and
sets `live_lease_unknown: true`, `armed: false`: an old acknowledgment cannot
prove a currently live lease. `last_command_acknowledged` is historical local
state, not proof of current readiness. Fresh command success requires matching
token, bound PID, exact sequence, advancing poll count, expected status and
an enabled controller (except neutral RELEASE).

## Binary protocol v1

The atomic `state.bin` snapshot is exactly 72 bytes, little endian:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | ASCII `VXIP0001` |
| 8 | 32 | ASCII random lowercase hexadecimal token |
| 40 | 8 | Nonzero uint64 sequence, strictly increasing |
| 48 | 4 | uint32 lease in milliseconds, 1..1000 |
| 52 | 2 | uint16 XInput button mask |
| 54 | 1 | uint8 left trigger |
| 55 | 1 | uint8 right trigger |
| 56 | 8 | Four int16 axes: LX, LY, RX, RY |
| 64 | 4 | uint32 Windows PID |
| 68 | 4 | uint32 flag: 1 ARM, 2 RELEASE, 0 STATE |

`state.bin.ack` is bounded ASCII:

```text
VXIA0001 TOKEN PID SEQUENCE POLLCOUNT ENABLED STATUS
```

Statuses are `READY`, `STATE`, `RELEASE`, `EXPIRED`, `INVALID`, and `DISABLED`.
Re-enabling XInput requires a new ARM; it cannot create a READY receipt by itself.
`ENABLED` is 0 or 1. Poll count increases only when slot-zero
`XInputGetState`/`XInputGetStateEx` is exercised. Capabilities or a host file write
are not delivery evidence. The accepted sequence in a receipt stays at the last
accepted packet when a malformed/replayed packet is rejected.

Manifest sequences are saved before snapshot publication, so an interrupted
writer never deliberately reuses a sequence. Writes use private temporary files,
fsync and atomic replace. Existing symlink/hardlink/non-private targets are
rejected, and every directory component is opened without following symlinks.
A nonblocking per-session lock rejects concurrent commands; commands are never
queued to run after their intended observation. The threat boundary is other
users and accidental path confusion, not a malicious process already running as
the same user with access to the session directory.

## Reuse choice and verification

Wine's `dlls/xinput1_3/main.c` (LGPL-2.1-or-later) provides the controller API
seam. The producer experiment is component commit
`10557ac06d458d48cc874799adc655050108e5a1`, on
`experiment/process-local-xinput-20261018`; it is not enabled by installing this
host script. [InputFusion](https://github.com/xan105/InputFusion)
was checked as an alternative: its LGPL-3.0 SDL3/Detours implementation translates
controller APIs but adds dependencies and lacks this bounded host protocol.
[SoulsGym's input module](https://github.com/amacati/SoulsGym/blob/main/soulsgym/core/game_input.py)
uses Windows SendInput or Linux XTEST and has no macOS backend; the full framework
modifies game memory. The obscure [xinputd](https://github.com/hurricane-src/xinputd)
shared-memory design is a relevant precedent, but its old Wine integration and
macOS gaps make it a larger adaptation. No code from those projects is copied
into this host writer.

Run host checks from this repository:

```sh
python3 -m unittest discover -s tools/gameplay -p 'test_virtual_controller.py' -v
```

The fake polling peer covers serialization bounds, token/PID/sequence/poll
identity, release on acknowledgment failure, disabled-state rejection, private
atomic files, link rejection and concurrent-writer rejection. Pulse checks also
exercise state rejection/timeout, release timeout, interruption during hold,
ARM-lease and whole-deadline guards, and one lock across the whole pulse. Host tests do not
exercise Wine, prove independent producer expiry, prove input release in a game,
or demonstrate macOS Space preservation. Live acceptance additionally requires
actual game poll receipts, visible menu movement, neutral release/expiry and
unchanged native frontmost application/Space; use the game's actual images.
The current receipt has no DLL/provider identity. This experiment therefore also
requires verifying that one admitted XInput provider owns this session in the
game process. Independently loaded XInput DLL variants must not share a session:
their poll counters and backend states are independent and their receipts could
otherwise be confused. The host cannot establish that prerequisite itself.

### Isolated execution evidence, 2026-10-18

The built x64 XInput 1.3 producer was executed in a separate Wine prefix:
415 enabled-mode assertions, 11 default-mode assertions, and 11 wrong-process
assertions passed. The fixture checked the loaded module path. Its DLL also used
a unique internal PE export name: merely renaming a Wine builtin DLL can still
resolve to the original builtin and is insufficient candidate binding.

A second, 20-second console fixture exercised this host writer against the real
DLL. The consumer observed A plus left-stick X=12000, neutral state after a
250-ms lease expired, B, and explicit neutral release. The consumer made 3,948
polls and exited successfully; the isolated Wine server settled successfully.
No native keyboard/mouse events or focus actions were used for this exchange.

These runs establish the transport and consumer API behavior, including
independent expiry. They do not establish DS3 background polling, visible game
actions, unchanged Spaces, or performance overhead. Production DS3 was not
modified by these fixture runs; a separately bound game trial is still required.

### DS3 background-menu trial, 2026-10-18

A separate temporary runtime using Wine source commit
`10557ac06d458d48cc874799adc655050108e5a1` loaded the patched XInput 1.3 DLL.
Process text mappings identified that exact private DLL; the stricter native
region scanner exhausted its region budget, so its result was not counted as
positive evidence. No second patched XInput provider was installed.

Actual game images confirmed virtual A/B and D-pad actions: title-screen
confirmation, warning/news dismissal, main-menu selection changes and Continue.
A native observer recorded the same other foreground application before and
after a confirmation action, with DS3 inactive. A separate public WindowServer
observer found the exact DS3 window in the complete window list but absent from
both onscreen samples. These observations establish inactive/offscreen menu
control; they do not identify a specific macOS Space.

The game continued polling, acknowledged state and release packets, and reported
EXPIRED after a bounded neutral ARM lease. Separate CLI invocations initially
failed acknowledgment, consistent with ARM-lease expiry between processes;
the single-process `pulse` command avoids that inter-process timing gap
and was exercised against the live game. Its 28 host tests also passed.

Character loading repeatedly encountered DS3-server or Steam-connection errors.
No world movement, combat, FPS gain, background-rendering equivalence or
continuous unattended gameplay is claimed. The game was closed through the
native application menu (not the in-game normal-exit path), its owned Steam was
shut down, and the exact ordinary launch settings were restored and verified.
The game may consequently show its improper-exit notice on the next start.
This remains an opt-in experiment, not a production replacement.

### Host fixture timing boundary

Rejection/interrupt propagation fixtures use a deterministic polling peer that
reads the actually published binary snapshot before writing its receipt, plus a
controlled test clock. This keeps thread scheduling and fsync latency from
preventing the semantic phase under test. Separate asynchronous peer tests still
exercise the real command/lease deadlines. A late-state-receipt fixture proves
that elapsed receipt latency consumes the original hold budget, sends no extra
hold and still publishes neutral RELEASE. Product deadlines are unchanged.

During the W6 integration check, the old 28-test suite first had two timing-phase
failures: the interruption case expired before reaching hold, and the combined
INVALID case timed out before the expected state rejection. A relaxed-load
reproduction then passed all 28. The repaired 29-test suite passed normally and
under two bounded CPU workers; these remain host flow checks, with no game or
actual producer exercised by the fake peer.


The local timed executor additionally opts into publication deadline fencing:
sequence persistence is followed by remaining-lease recomputation, and a final
check rejects expired STATE replacement after temporary-file fsync. It records
actual atomic replacement bounds; a replacement that overruns the admitted
schedule tolerance invalidates the interval and invokes neutral cleanup. These
optional host guards change no binary protocol and no ordinary `pulse` deadline.
Consumer-relative lease expiry and actual game consumption remain separate from
host publication timing.

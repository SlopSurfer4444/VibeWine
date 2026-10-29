# Optional foreground cursor warps

`VIBEWINE_FOREGROUND_CURSOR_WARP=1` opts one process into refusing native cursor
warps when its native application is inactive or its key window is missing,
non-Wine, closing, hidden, minimized or off the current Space. It is read once at
controller initialization. Unset, empty, `0`, `true`, `01` and any other value keep
the previous behavior. This is an explicit desktop-interference policy:
[Microsoft's SetCursorPos contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursorpos)
describes a shared cursor and access/input-desktop requirements; it does not
require foreground application status.

The guard covers both the direct CoreGraphics warp and delegated event-tap
handler path after native clipping has been released. A refusal returns `FALSE`
from the native controller before the cursor primitive or queue discard/reset.
If stale clipping exists, its existing release runs first: successful release
updates the existing stale-mouse-event timestamp, while failed release does not
permit a warp. The unchanged internal C signature now returns `1` for success,
`0` for genuine native failure and `-1` for an explicit successful policy veto.
Its sole driver consumer maps only positive values to Win32 `TRUE`. A policy
veto returns `FALSE` without per-call logging; actual warp or release failure
keeps `FALSE` and the existing error report. No successful movement is invented. The existing permission-dialog exception and internal key-window
transfer policy are preserved. An explicit eligible request can warp again;
focus return alone does not replay a warp. It does not disable process-local
controller/XInput delivery or physical gamepads.

## Offline verification

```sh
python3 vibewine/tests/native-cursor-warp/run.py
python3 vibewine/tests/native-cursor-warp/run.py --translated
```

The runner extracts actual production warp, eligibility, release and window
update methods, both Objective-C selectors, the C bridge and actual mouse-driver
wrapper plus the environment parser. It binds the production init
assignment, then supplies application/window state, cursor primitives, clipping
handler and event queues as seams. It creates no NSApplication, native window,
cursor effect, Wine process or game. The native arm64 and translated x86_64
variants run under `-Werror`, ASan and UBSan. Each tests 36 assertions in six
environment modes; exact base `271e5cda` fails 23 linked assertions only for the
opt-in mode, and passes compatibility modes. These counts are source behavior
assertions, not independent end-to-end workflows. Existing native-cursor-focus
checks remain separately runnable.

## Owned-window live acceptance: integration owner only

`owned-window.c` is a standard Win32 window for the actual matched Wine runtime.
It is source preparation; no build or launch is implied by this README. The
integration owner supplies an already authorized PE compiler/runtime and owns
all native UI, game, focus, cursor and capture activity. It needs no new native
app package, installation, accessibility permissions or global input injector.
Build with the existing Wine PE toolchain and user32 import library, then bind
the executable hash, runtime/source identities, process PID/birth and opt-in
value before launch. Keep a disposable owned prefix separate from Steam/saves.

The window starts unarmed. Its real standard buttons offer clip+warps,
warps-only, release and normal Quit. Every 20ms an armed timer deliberately
continues native requests after application focus loss; its target alternates
15 pixels either side of the centre so server deduplication cannot eliminate
all repeated requests. The clip rectangle is 90×90 pixels. Escape releases
while that window receives keyboard input. Normal close releases and exits;
a 90-second message-loop deadline also releases/closes. That deadline depends
on the process/message thread making progress and is not a hang watchdog.

Run a short opt-in session with actual physical cursor observations:

1. Arm **warps only** in the unique `VibeWine T8 Owned Cursor Fixture` window.
   Confirm actual foreground warping and growing calls. Cause genuine native
   key loss to another owned window. Move the physical cursor outside the old
   centre, and verify it remains free while `armed=1` and calls keep growing.
   Repeat with clip+warps and actual Space change/minimization. Record actual
   native callback observations separately if the integration owner has them.
2. Return eligible focus without clicking Arm; the next explicit timer request
   should restore warping/clipping. Release, then repeat. Test an intentional
   internal Wine key-window transfer and the existing explicit permission-dialog
   exception with an appropriate owned native dialog, when available.
3. Enter Mission Control using the actual gesture. Record cursor behavior and
   whether native active/key/Space state changed. If all retained their eligible
   values, this policy may still allow warps: classify that observation honestly.
4. Release and choose **Normal Quit**. Check a terminal process/exit receipt and
   `T8 NORMAL_EXIT`, then relaunch the same bound executable and repeat a short
   foreground/release/normal-close sequence. A forced kill does not satisfy
   normal exit/relaunch.

The Win32 `SetCursorPos` BOOL is **only an API acknowledgement**. In this Wine
source, `NtUserSetCursorPos` updates server state and ignores the driver's return
(`dlls/win32u/input.c`); `api_ok` therefore does not prove native movement or
native refusal. Likewise WM_ACTIVATEAPP is a Windows observation, not an AppKit
key/Space receipt. Native physical cursor observations are required. Expected policy vetoes produce no per-call log. Genuine native errors retain
the existing error reporting; keep the integration owner's logs bounded.

[Apple's active-Space property](https://developer.apple.com/documentation/appkit/nswindow/isonactivespace)
and [Spaces notification](https://developer.apple.com/documentation/appkit/nsworkspace/activespacedidchangenotification)
only describe Space membership/changes. Neither examined public contract supplies
a Mission Control overview entry/exit signal. This patch adds no private APIs or
last-event-target heuristic. Mission Control retained-state behavior and actual
game input, normal game exit/relaunch remain separate live gates. The game
scenario must bind the controller's actual rendered-state progress and ordinary
exit; successful input ACKs alone cannot establish it.

## Data ownership and retention

The offline runner owns and removes its temporary source/binaries on exit.
The live fixture produces only stdout transition/one-second summaries during its
90-second lifetime, no files or installed producer. Root owns redirected logs:
7 days/32MiB aggregate cap, results/decisions 30 days/8MiB, build artifacts
256MiB; active analysis and recovery holds excluded. Redirected stdout needs the
root's bounded writer; this fixture does not implement a disk quota. The root
coordinates exact-owned cleanup after process exit and hold checks; no cleaner
is installed. Root must keep the campaign's 40GiB aggregate/20GiB free reserve.

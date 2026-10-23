# Native inactive topmost-window regression

The production change is the exact `cocoa_app.m` hunk from
[kageroumado/dormison `3e77a1125ef897378f3f869ff5b2eaf0c44a58aa`](https://github.com/kageroumado/dormison/commit/3e77a1125ef897378f3f869ff5b2eaf0c44a58aa),
“An inactive program's topmost windows stay below the Dock”, authored by
**kageroumado**, with original **Co-Authored-By: Claude Fable 5.1
<noreply@anthropic.com>** retained as provenance. The donor file preserves Ken
Thomases / CodeWeavers' LGPL-2.1-or-later notice. The fixtures use that license;
the generated test sources retain the original production file notices.

`minimumLevelForActive:NO` already suppresses fullscreen/capture elevation, and
`applicationWillResignActive:` already calls `adjustWindowLevels:NO` explicitly.
The unconditional topmost floor nevertheless raised visible inactive floating
windows to Dock+1. The donor makes that floor depend on the supplied activation
state. Active behavior retains Dock+1 and the higher fullscreen/capture levels.

From the Wine component root, in a logged-in macOS GUI session:

```sh
python3 vibewine/tests/native-window-levels/check.py
```

A sandbox without WindowServer access reports `NATIVE_WINDOW_SERVER_UNAVAILABLE`
and fails; it is not a successful native check. Run this reviewed command with
scoped GUI-session access if required. It does not require Accessibility or
screen-recording permission and does not capture window images.

The runner extracts these complete, unmodified production methods separately
from baseline `d2237a9a82d9960360ce8400242ddeae6646c347` and the working source:

- `adjustWindowLevels:`, its native inactive wrapper, and
  `applicationWillResignActive:` / `applicationDidBecomeActive:`;
- `minimumLevelForActive:` from the matching `cocoa_window.m` generation.

The real native AppKit operations include transparent, one-pixel `NSPanel`
windows, window-number ordering, level changes, child relationships and
`orderFront:`/`orderBack:`. CoreGraphics independently reads only the fixture's
own window levels after a bounded 20ms run-loop flush. The test app prohibits
activation; its panels cannot become key/main, ignore mouse events and close
before exit. The fixture checks initial/final inactive, key/main and frontmost
application state. It never targets or changes another application's windows.
The panel used as a Dock-level ordering sentinel belongs to the test itself.

The 2026-10-23 check passes **35 assertions** for the candidate. The baseline
fails exactly eight named assertions: six inactive-below-Dock cases (windowed,
normal/floating fullscreen state, captured state, Dock-level sentinel ordering,
and a restored fullscreen seam) and two mixed floating-parent/normal-child
ordering cases. Those mixed-child differences depend on the preceding inactive
level history; they are not a separate active-path repair. The active production
calculation is unchanged. Unexpected failures, build errors and crashes fail the runner.
Both generations preserve unrelated fixture-window levels, hidden guards,
floating toggles, normal-child ordering and inactive wrapper behavior. Native
CoreGraphics observes active floating level **21** and corrected inactive
floating level **3** on this host; these values are checked against platform
constants rather than hardcoded in the fixture.

The activation/fullscreen/capture/minimized/other-space states are controlled
seams. Activation callbacks run in full, but their display, fullscreen-update,
unminimize and event dependencies are inert. No real application activation,
fullscreen transition, display capture, minimization, Space change or click on
the user's Dock occurs. The minimized restore case clears an injected minimized
flag and orders its own panel; it does not run `windowDidDeminiaturize:`.

Two existing policy limitations are deliberately visible: the common floating
floor still overrides `TOPMOST_FLOAT_INACTIVE_NONE` and the fullscreen exception
of `NONFULLSCREEN`; the minimized pass still requests
`minimumLevelForActive:YES`. A minimized fullscreen window therefore retains its
existing active minimum until it is restored and levels are recalculated. The
narrow donor patch does not repair those policies.

The complete changed `cocoa_app.m` also passed x86_64 syntax-only compilation
with the retained baseline configured headers and Makefile flags (six existing
macOS API deprecation warnings). This is not a fresh configure or linked build.

This check proves source-method behavior with native AppKit window-server
operations. It does not prove a complete Wine build, installed behavior, actual
OS lifecycle integration, gameplay, Dock click delivery or FPS improvement.
No Wine/Steam/game process is started, no runtime is installed or switched, and
no private SkyLight cursor subsystem is imported.

The runner owns its short-lived generated sources/binaries in an exact private
`TemporaryDirectory` and removes them on exit. Native panels live only within
the test process and are closed/released before normal exit; process exit also
retires its window-server resources. Source, provenance and the test remain in
Git. There is no persistent capture/log producer or retention-policy change.

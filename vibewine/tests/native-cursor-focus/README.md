# Native cursor focus and Space ownership

The application already releases native cursor clipping on application
deactivation, sleep and accepted termination. This follow-up handles the narrower
case where a Wine window loses key status while its application stays active,
and stale queued clipping requests after native focus or Space eligibility is
lost. The native release precedes the Windows focus-loss event; a blocked Windows
message thread cannot delay that release.

New clipping requires an active application and its visible, non-minimized,
non-closing key Wine window on the active Space. The Space callback releases
existing clipping when those conditions cease to hold. It does not activate,
order, restore or select a window. A new eligible clipping request can reacquire;
the callback alone does not restore an old rectangle.

The confinement handler uses that current key window, rather than an all-Spaces
front-window scan. Its same-rectangle shortcut also checks native owner identity,
so an internal key transfer can rebind the unchanged rectangle. The event-tap
handler keeps its existing same-rectangle behavior. The existing explicit native
permission-dialog exception retains its prior front-window selection, including
changed rectangles with a nil or native-dialog key window. Intentional internal
key-switch focus-loss suppression remains intact.

Run from the Wine component root on macOS:

```sh
python3 vibewine/tests/native-cursor-focus/run.py
python3 vibewine/tests/native-cursor-focus/run.py --translated
```

The runner compiles the complete production controller admission/start/stop and
Space callback methods, key gain/loss callbacks, and confinement owner/start/stop
methods. Foundation application/window fakes expose the public callback boundary;
the native confinement setter records ownership. Rectangle/window intersection
conversion is a seam, not a geometry test. The event-tap's CoreGraphics machinery
is not exercised. Neither runner creates an NSApplication, native window,
cursor operation, Wine process or game. Both ASan/UBSan variants use temporary
directories removed on exit. `--baseline-ref` selects the unmodified Git source
for the required exact regression control. A compiler error, unrelated crash or
unexpected failure set fails the runner.

The checks cover release before posting focus loss; blocked/queued reacquisition;
active, key, class, closing, visibility, minimization and Space eligibility;
explicit reacquisition after return; unchanged-rectangle owner identity;
non-activating Space callbacks; idempotent release; internal key switches;
and unchanged/changed rectangles during the permission exception. Results record
the production-file hashes and generated fixture hash. These are source/callback
tests; application/window state is injected rather than delivered by AppKit.

The reviewed candidate passes 28 assertions on native arm64 and translated
x86_64 with `-Werror`, ASan and UBSan. The bound unmodified base fails exactly
19 expected checks in each variant. This count includes linked assertions along
the release/reacquire sequence, not 28 independent native workflows.

## Mission Control boundary and live acceptance

Apple documents
[Space changes](https://developer.apple.com/documentation/appkit/nsworkspace/activespacedidchangenotification),
[Space membership](https://developer.apple.com/documentation/appkit/nswindow/isonactivespace),
[key-window loss](https://developer.apple.com/documentation/appkit/nswindowdelegate/windowdidresignkey(_:))
and [occlusion transitions](https://developer.apple.com/documentation/appkit/nswindow/didchangeocclusionstatenotification).
These contracts do not establish a public Mission Control overview enter/exit
signal. The existing event-tap source itself notes that Mission Control can keep
the app active. Occlusion is not used as a focus heuristic here.

A four-finger overview gesture that preserves key status and Space membership
may exercise none of these callbacks. Direct `SetCursorPos` remains able to warp
outside this clipping path. Therefore this change does not establish a repaired
Mission Control gesture, cursor-warp suppression, normal exit/relaunch, or game
input restoration. Those require coordinated live acceptance with a disposable
owned fixture or the requested game.

For live acceptance, the integration owner must first bind the built runtime and
own all concurrent UI/capture work. Observe a uniquely titled owned window:
clip, cause genuine key loss, verify the cursor can leave its old rectangle,
return key focus, request clipping again, and verify reacquisition. Repeat with
an actual Space change and with Mission Control overview, recording which public
callbacks occurred separately from physical cursor behavior. Confirm intentional
internal key transfer and explicit permission-dialog exceptions, then normal
close/relaunch. An overview with no authoritative callback remains unsupported
by this repair; a synthetic callback pass cannot replace that observation.

## Artifact retention

The fixture produces no persistent application telemetry. Its generated source
and binaries are private temporary files removed on runner exit. The coordinating
task owns any redirected output/evidence: raw logs seven days, 32 MiB cap;
results/decisions thirty days, 8 MiB cap; active analysis/recovery holds exclude
required evidence from cleanup. Cleanup is by that owner after exact path and
hold checks; this fixture installs no cleaner or quota enforcement.

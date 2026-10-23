# DS3 visual test scenario

Status: partial live control observed; no autonomous gameplay benchmark accepted.
Use the generic `tools/gameplay` CUA helper for bounded actions. This profile
contains DS3 knowledge; the shared helper must not recognize game executables,
inspect saves, or synthesize a game accessibility tree.

## Preconditions

Use an already accepted installed runtime and the existing diagnostic launcher.
Keep the user's graphics settings, saves and other running applications intact.
Bind the game's real native application only after its process/window appears:
a Steam launch receipt is not a game-ready signal. Obtain a fresh screenshot.
Do not launch a second copy to recover a missing window before reconciling the
first process. Do not run route experiments during online multiplayer.

## Visual checkpoints owned by the agent

Use the game's bindings rather than generic dialog conventions. The official
[Windows manual](https://www.fromsoftware.jp/manual/darksouls3/win/operation_appendix.html)
lists **E / left click** for menu confirmation, **Q** for cancellation, and arrow
keys for navigation. Return is not a default confirmation key; its non-response
alone is not evidence of failed keyboard transport. Observe customized bindings
if these defaults do not produce the expected visible result. Configure the
generic helper with `createSession(gameApp, {additionalKeys: ['e', 'q']})`.

1. Title: identify the title and any-key prompt. Raise the observed window and
   click only a visible safe location. Read the next screenshot.
2. Notices: distinguish improper-exit notice, information screen, server error
   and menu. Dismiss visible OK/Close controls individually. Never blindly send
   repeated confirmation keys: they could select a different state.
3. Main menu: identify Continue and select it. Await loading completion using
   screenshots with bounded attempts. Stop on an error, character selector or
   unexpected state; do not create or delete a character.
4. World: prove the character and HUD are visible. Inspect surroundings before
   movement. Do not assume the saved position is safe. No combat, consumables,
   purchases, progression or soul-spending are needed for this scenario.
5. First useful benchmark: a fixed safe scene and camera, short bounded capture.
   Record scene/resolution/configuration before and after. Start with a static
   observation before validating a repeatable short out-and-back route.
6. Exit: open the actual game menu, visually select the game's normal exit and
   verify return to title, then desktop. Reconcile process state before rerun.

The agent must classify every checkpoint from actual images. These instructions
are not an implemented vision classifier or a claim that a complete route works.
Identical key durations do not guarantee identical scenes. If health changes,
combat/invasion/loading starts, focus is lost, or the image is ambiguous, stop
input and mark the performance sample invalid.

## Current live evidence (2026-10-18)

Supported CUA attachment and screenshots worked once the delayed Steam launch
created the real DS3 window. Mouse clicks dismissed the improper-exit notice and
information screen. A bounded Down key changed selection from Continue to Load
game. Return/Escape did not visibly confirm those dialogs in the observed
attempts. Before world loading was verified the window became unavailable and
Steam logged the game process ending. The user confirmed this was not a manual
exit. Wine logged a null write at `DarkSoulsIII.exe+0xf14c52`; inspection of the
matching executable identifies a deliberate `0xdead` fatal-stop instruction.
The caller and triggering condition are unknown: this is evidence of a crash,
not evidence that CUA, DXGI, or a specific patch caused it. A full caller stack
is still required.

A second run remained alive at the title screen. AX enumeration, screenshots,
and the exposed Raise action succeeded, but a coordinate click failed with CUA
`noWindowsAvailable` (`-10005`). Return delivery had no visible effect. The
helper stopped input on the failure as designed. AX visibility must therefore
not be used as proof of native input routing. Keep this routing defect separate
from the earlier game crash; their relationship has not been established.

No gameplay FPS, movement, clean exit or complete autonomous cycle is claimed.

### Subsequent focus and mapping isolation

In a later single live run, the window on the active Space acquired native
application/key-window focus. Mouse clicks dismissed the title and warning;
E confirmed a server retry, dismissed information and selected Continue. The
menu displayed online status, but character-network initialization failed twice;
world loading was not established. Retrying a visible completed network error
must remain distinct from blind repeated input.

Later in that same process, the window was off the active Space and coordinate
input again returned `noWindowsAvailable`. The diagnostic showed Windows
accepting the foreground transition and calling the native focus path with
activation disabled; native application/key-window focus remained false. This
narrows the observed routing failure to cross-Space/native activation in this
scenario. It does not establish a universal Wine cause or validate a fix.
Temporary focus instrumentation is not a production or FPS acceptance result.

## Capture discipline

Use `tools/telemetry` and its explicit record budget. Keep GUI observations
outside a stationary timed capture, retain raw records and reject overflow or
configuration changes. Do not report title/menu cadence as gameplay FPS. The
independent observer overhead comparison remains a separate required experiment.

## Implemented lifecycle policy (source only)

[`lifecycle-policy.mjs`](lifecycle-policy.mjs) supplies DS3's allowed visual
intents for the [finite lifecycle API](../../tools/gameplay/LIFECYCLE.md). Actual
screenshot classification, exact process/native window binding, ordinary launcher,
fresh neutral receipts and a local telemetry/controller executor remain explicit
caller capabilities. The source workflow and CPU tests do not complete the live
scenario described above. There is no default route from an unknown saved spot.

The classifier must first verify offline mode before `continue-offline`. It may
choose one visually safe WASD pulse of at most 250 ms and must then observe its
actual response. The route admits only bounded left-stick movement, after a
successful stationary capture and separate visual corridor/endpoint admission.
Unexpected online/network/character states fail the runner rather than receive
blind confirmations. Normal exit must visibly return to title before the final
observed desktop Quit; process absence and a new generation are then checked.

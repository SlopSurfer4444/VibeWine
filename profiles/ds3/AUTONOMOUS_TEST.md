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
Steam logged the game process ending; the cause remains unresolved. No gameplay
FPS, movement, clean exit or complete autonomous cycle is claimed.

## Capture discipline

Use `tools/telemetry` and its explicit record budget. Keep GUI observations
outside a stationary timed capture, retain raw records and reject overflow or
configuration changes. Do not report title/menu cadence as gameplay FPS. The
independent observer overhead comparison remains a separate required experiment.

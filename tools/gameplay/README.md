# Bounded computer-use game sessions

This small module reuses an already-bound CUA App. It adds one-use observation
handles and stops the session on input or observation errors. It is not a second
input injector, a vision model, or an unattended benchmark daemon. The agent
still inspects each returned screenshot and chooses the next action.

```js
const { createSession } = await import('/absolute/checkout/tools/gameplay/session.mjs');
const session = createSession(gameApp); // already bound using supported CUA APIs
let observation = await session.observe({width: screenshotWidth, height: screenshotHeight});
// Inspect observation.screenshot before choosing a visible, safe action.
observation = await session.act(observation.token,
  {type: 'key', key: 'Down', durationMs: 120},
  {width: screenshotWidth, height: screenshotHeight});
```

Dimensions must come from the real screenshot and match CUA's coordinate mapping.
The helper only bounds values against the caller's supplied dimensions; it does
not decode images. A token prevents reuse of an old local observation, but does
not prove the user or game has not changed state since capture. Use fresh visual
checks and do not overlap manual control with automated input.

Each action is one bounded key press (at most1000ms), one left click, or an AX
secondary action actually exposed in the latest tree. There is no blind retry,
raw key-down API, persistent key hold, process launch, focus stealing loop or
automatic App rebinding. CUA owns delivery and key release; if its call fails,
the helper cannot independently prove that the OS released input. Stop and
reconcile the actual window before starting a new session.

The default keys cover navigation and WASD only, not item use/combat. An allowed
key is not inherently safe in every game state. Keep game-specific visual
checkpoints under `profiles/`; see the partial [DS3 scenario](../../profiles/ds3/AUTONOMOUS_TEST.md).

For a verified game-specific menu mapping, opt in to extra letters at session
creation: `createSession(gameApp, {additionalKeys: ['e', 'q']})`. The option accepts
at most 52 single ASCII letters with exact case; it cannot enable chords, named
keys, raw holds, or a longer duration. Letters can have gameplay effects outside
menus, so inspect the screenshot before each action. Defaults remain unchanged,
and every added key still consumes one fresh observation token and requires the
next screenshot and full AX observation. Configuration is copied at creation;
invalid options throw before the App is accessed.

Run the mock contract tests with `node --test tools/gameplay/session.test.mjs`.
These prove control-flow guards, not focus, key timing, movement, or gameplay.
Live acceptance still requires loading the world, observing movement, a valid
capture and normal exit without user input. Do not capture screenshots during a
stationary timed benchmark: first establish the scene, perform the separate
bounded telemetry capture, then observe its end state.

The existing CUA path was chosen over another CGEvent injector: inspected prior
PyAutoGUI backend findings showed that injection alone cannot prove Wine
RawInput/DirectInput delivery or target focus. This helper changes neither Wine
nor the installed game runtime.

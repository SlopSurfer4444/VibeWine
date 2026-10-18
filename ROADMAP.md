# VibeWine roadmap

This is a direction for contributions, not a release promise. Start with the
[component source](README.md#start-with-the-source) and [contribution guide](CONTRIBUTING.md).

## Make the project easier to build and review

- Establish an independently verifiable upstream baseline and dependency pins.
  The retained Wine archive identity does not substitute for a verified Git revision.
- Validate clean-room component builds and combined runtime assembly from the pinned workspace.
- Preserve component licenses and attribution; keep private deployment tooling out.
- Add CI only for fixtures that can genuinely run in its supported environment.
  Current local test results are not CI coverage.

## Native macOS behavior

- Reproduce and fix cursor confinement across Mission Control and focus changes.
- Exercise fullscreen, resolution changes, normal exit, crash recovery and relaunch.
- Expand accessibility from isolated standard-control fixtures to stable real-app
  discovery and input. Keep visual interaction distinct from semantic AX support.
- Check Retina and child-window geometry on additional applications and displays.

## Performance with evidence

- Compare identical scenes, resolution, settings and cold/warm cache state.
- Separate shader compilation, CPU work, GPU waits, frame presentation and thermal
  effects before changing code. Report frame-time distribution, not only average FPS.
- Validate purported general improvements on a second application where feasible.

## Compatibility profiles

- Keep title detection and compatibility policy outside general algorithms.
- Document why each exception exists, how it is selected/disabled, and which
  versions were tested. Test both profile-enabled and default behavior.
- Remove exceptions when the underlying defect is fixed or the title no longer
  requires them. Avoid silently expanding a workaround to every application.

DS3 is the initial test case, not the project's API contract. No claim is made
that the existing profile is a finished generic profile-management system.

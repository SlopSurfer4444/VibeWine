# Experiments

Instrumentation and unproven alternatives must default off, have bounded resource
cost, and document removal. The existing [optional experiments](../optional/README.md)
remain historical material; they are not automatically enabled by this workspace.

FPS comparisons must record source pins, hardware, resolution, scene, settings,
cache state, warm-up, background work and measurement overhead. CPU Present calls
alone do not prove displayed frames. Report frame-time distributions and stalls,
not just averages. Source tests do not prove gameplay or performance.

[Native Metal presentation experiment](native-presentation/README.md) separates
window visibility, drawable presentation timestamps and GPU completion with a
bounded native control; it does not require Wine or launch a game.

[Early scene submission](../components/dxmt/tests/vibewine/scene-submission/README.md)
is a default-off D3D11 build experiment (`-Dvibewine_scene_submission=true`). It
submits dirty scene work after frame admission and before the final presentation
chunk. Its [native Metal discriminator](../components/dxmt/tests/vibewine/scene-submission/gpu/README.md)
proves work completes during a fixture-held acquisition gate; it does not prove
game speed. [Current acceptance](../docs/diagnostics/focused-followup-20261024.md#implementation-and-verification-checkpoint-25-october)
keeps production Present, displayed FPS, latency and memory checks explicit.

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

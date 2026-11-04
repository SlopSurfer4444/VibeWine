# Roadmap

VibeWine aims to make Windows applications and games behave more naturally on
macOS, while improving performance where measurements identify avoidable work.
This roadmap describes engineering directions, not promised speedups or release
dates. See [status](docs/STATUS.md) for the evidence behind current checkpoints.

## Native macOS behavior

- Reliable window geometry across Retina scaling, resizing and display changes.
- Fullscreen transitions, ordinary exit and repeat launch without retained capture
  or stale application state.
- Focus, cursor release and supported accessibility actions that work with
  automation as well as manual use.
- Clearly identified applications and sessions, with services kept distinct from
  the game they support.

## Renderer and compiler efficiency

- Reduce redundant state, resource-binding and allocation work while preserving
  ordering and ownership.
- Improve upload reuse and retirement without recycling in-flight resources.
- Investigate shader conversion, compilation demand and cache reuse with matched
  source/options and correct failure handling.
- Reduce GPU traffic and unnecessary passes only when image and hazard controls
  establish equivalent output.
- Distinguish rendering cost, presentation waits and background scheduling before
  selecting an optimization.

## Reproducible measurements

- Repeatable workloads, with collectors ready before measured activity starts.
- Comparable resolution, scene, cache state, power conditions and observer cost.
- Frame-time distributions and stalls, alongside validated presentation evidence.
- Clear rejection of incomplete traces and negative experimental results.

## Compatibility and contributions

- Review useful upstream and fork changes, preserving authorship and licensing.
- Keep reusable engine repairs separate from title-specific policies.
- Document actual activation and deactivation of each experimental path.
- Improve build instructions and test coverage without treating a source check as
  proof of an installed runtime or game result.

Earlier implementations and iteration plans remain available in Git history.
Dated technical reports in `docs/` record their original source pins and limits.

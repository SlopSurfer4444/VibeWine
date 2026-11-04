# Source and validation status

## Main branch

The public workspace pins these independently versioned components:

| Component | Revision |
| --- | --- |
| Wine | `ffd567b1eee7d773bcff0eae4099da4778885574` |
| DXMT | `84761a313189d88570fb057630003dfd180e74dd` |

The Gitlinks are authoritative. Component branch tips can advance independently;
a workspace update requires a reviewed pin change. See [build instructions](BUILDING.md)
and [patch origins](PATCHES.md).

The [29 October source report](diagnostics/sustained-wave-20261029.md) records
binding/upload/clear economy, queue-failure handling, MSync repairs and opt-in
cursor/compiler work. Scoped Wine builds, matched graphics builds and focused
regressions are reported there. Subsequent publication and fixture repairs are
recorded in the [publication map](provenance/publication-20261029.json) and Git
history. These checks do not establish universal game coverage or an FPS gain.

## Newer experimental checkpoint

The [verified local-build branch](https://github.com/SlopSurfer4444/VibeWine/tree/optimization/verified-local-build-20261104)
contains newer benchmark controls and component pins. Its
[4 November comparison](https://github.com/SlopSurfer4444/VibeWine/blob/e2777eb31837d94d7be404bc9bf4b35fc35091ac/docs/diagnostics/current-build-loop-comparison-20261104.md)
records fresh Wine/graphics builds and eight rendered benchmark runs.
The shader-loop experiment produced matching control images but **no demonstrated
performance gain**, and remains off. That branch is not silently substituted for
main's pins or an installed runtime.

## Interpreting results

| Evidence | What it establishes |
| --- | --- |
| Source checks | Covered code contracts and regression behavior |
| Builds | The stated targets compile with the recorded inputs |
| Installed readback | The selected files match the intended delivery |
| Game observations | The reported workflow worked in the particular session |
| Comparable traces | Only the measured effects supported by that capture |

VibeWine currently distributes source. Historical installed test bundles combined
selected builds with retained runtime dependencies; they are not a clean-room
Wine distribution or a general installation promise. Private launchers, saves,
credentials and game assets are not part of this repository.

Dated installation and gameplay observations remain in the
[historical status ledger](../archive/reports/status-through-20261029.md).
They do not certify the state of another user's machine. Earlier iteration
names are retained for provenance, not used as product release names.

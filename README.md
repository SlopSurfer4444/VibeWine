# VibeWine 🍷

**Vibe-code the fix. Prove it works. Share it.**

An AI-friendly project for improving **Wine and DXMT on macOS**: native window
behavior, accessibility, game compatibility and measurable performance.
Human-written and AI-assisted contributions are welcome.

[Build](docs/BUILDING.md) · [Contribute](CONTRIBUTING.md) ·
[Patch origins](docs/PATCHES.md) · [Status](docs/STATUS.md) · [Roadmap](ROADMAP.md)

## What we work on

- **macOS integration:** window and Retina geometry, application lifecycle,
  accessibility, focus and cursor behavior.
- **Graphics and performance:** resource lifetime, state and upload overhead,
  shader compilation, queue correctness and frame presentation.
- **Repeatable testing:** bounded telemetry, input-control tools and regression
  fixtures that distinguish a code change from an observed improvement.

General fixes belong in the engine. Title-specific workarounds belong behind
explicit game gates. Experiments stay opt-in until their results justify wider
use. An accessibility bridge can expose supported controls; it does not turn a
rendered game interface into an accessibility tree.

## Get the source

```sh
git clone --recurse-submodules https://github.com/SlopSurfer4444/VibeWine.git
cd VibeWine
python3 scripts/workspace.py check
```

This is a **source project**, not a ready-to-install Wine distribution. GitHub's
automatic ZIP omits submodules; use a recursive clone. Wine and DXMT are pinned
independently, with their original licenses and attribution.

## Find your way

| Location | Purpose |
| --- | --- |
| [`components/wine`](components/wine) | Complete Wine component; native integration and compatibility fixes |
| [`components/dxmt`](components/dxmt) | Complete DXMT component; Direct3D-to-Metal renderer and shader tooling |
| [`profiles/`](profiles/README.md) | Title-specific gates, activation and test policies |
| [`tools/telemetry/`](tools/telemetry/README.md) | Capture and analysis tools |
| [`tools/gameplay/`](tools/gameplay/README.md) | Bounded input control and test protocols |
| [`experiments/`](experiments/README.md) | Opt-in investigations with explicit limits |
| [`docs/`](docs/README.md) | Build guide, provenance, validation and technical reports |
| [`patches/`](patches/runtime-2026-10-23/README.md) | Retained runtime source deltas and reconstruction notes |
| [`archive/`](archive/README.md) | Earlier snapshots and development records |

The [status page](docs/STATUS.md) identifies the main-branch pins and newer
experimental checkpoints. Builds, installation, game observations and measured
performance are reported separately. We do not claim a universal FPS increase.

## Contribute

Bring a reproduction, a focused change and the checks you actually ran.
[The contribution guide](CONTRIBUTING.md) explains component branches, game
profiles, validation and source attribution. Both human and AI-assisted work
receive the same review.

VibeWine is independent of Wine, DXMT and CodeWeavers. Existing copyright
notices and per-file licenses remain intact; AI assistance does not remove
contributor responsibility for review or licensing.

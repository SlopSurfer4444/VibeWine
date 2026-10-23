# VibeWine 🍷

**Vibe-code the fix. Prove it works. Share it.**

An AI-friendly contributor workspace for **Wine on macOS and DXMT**: native
windows, Retina, accessibility, game compatibility and performance you can
measure. Human-written and AI-assisted contributions are welcome. Bring a
reproduction, a focused patch and evidence of what you checked.

**Current checkpoint: 23 October 2026.** A matched DXMT/Metal bundle has passed
native GPU correctness checks and private installation/launch checks. An owner
reported a good DS3 gameplay session on the matching trial. Controlled FPS gains,
sustained freeze elimination and compatibility with other games remain open.
This repository distributes source, not a ready-to-install Wine runtime.

[Patch origins](docs/PATCHES.md) · [Current status](docs/STATUS.md) ·
[Build guide](docs/BUILDING.md) · [Contribute](CONTRIBUTING.md)

## Get the source

```sh
git clone --recurse-submodules https://github.com/SlopSurfer4444/VibeWine.git
cd VibeWine
python3 scripts/workspace.py check
```

| Source | What it contains | Where to contribute |
| --- | --- | --- |
| [Wine](https://github.com/SlopSurfer4444/VibeWine/tree/component/wine), `components/wine` | Complete vendor-derived Wine tree and reviewed native/AX fixes | `component/wine` |
| [DXMT](https://github.com/SlopSurfer4444/VibeWine/tree/component/dxmt), `components/dxmt` | Complete renderer, compiler and matching native Metal bridge | `component/dxmt` |
| [Installed Wine source deltas](patches/runtime-2026-10-23/README.md) | Exact native-driver/MSync patches; links to separate XInput/network source branches | See each patch's base and provenance |
| [Game profiles](profiles/README.md) | Explicit title-specific compatibility | `main`, or the component owning the gate |
| [Telemetry](tools/telemetry/README.md) and [gameplay tools](tools/gameplay/README.md) | Analysis, bounded test control and reproducible test protocols | `main` |
| [Experiments](experiments/README.md) | Opt-in instrumentation and native presentation probes | `main` |

Components are pinned submodules hosted on branches in this same repository.
Updating a component branch does not silently update the workspace pin. GitHub's
main-branch ZIP omits submodule contents; use a recursive clone or the component
links above. The installed test runtime combines several source revisions; it is
not a full clean rebuild of the pinned Wine branch. [Status](docs/STATUS.md)
records that boundary explicitly.

## Keep the scope clear

| Scope | Examples | Expected contribution |
| --- | --- | --- |
| **Core** | Graphics allocation lifetime, DXGI correctness, native window/AX behavior | General fix with an unaffected-path regression |
| **Game profile** | DS3 mode-list limit, narrowly selected aspect/borderless behavior | Named gate, activation/deactivation and reason |
| **Experiment** | Virtual XInput, network metadata, presentation diagnostics | Off by default, bounded and reversible |
| **Tooling / docs** | Build helpers, fixtures, provenance, analysis | Reproducible commands and truthful limits |

[The patch map](docs/PATCHES.md) separates upstream work, adapted code and local
changes. A game observation is useful feedback; a performance claim also needs
comparable scenes, settings, cache state and frame-time measurements. Computer
use seeing a window does not establish reliable game input.

Start with a small issue or PR. Explain the failure, identify the source/base,
keep unrelated changes separate and report the checks you actually ran. See the
[contribution guide](CONTRIBUTING.md) and [roadmap](ROADMAP.md).

## History and attribution

VibeWine is independent of Wine, DXMT and CodeWeavers. Upstream authors,
copyright notices and per-file licenses remain intact. Wine-derived code retains
LGPL terms; DXMT and its dependencies have their own notices, including LGPL
Metal source. AI assistance does not remove contributor responsibility for
provenance, review or licensing.

`current/`, `native/`, `renderer/`, `ntdll/`, `optional/` and retained evidence
folders describe earlier source snapshots. They remain available for comparison;
new component work starts in `components/`. Snapshot manifests certify only their
original scope. [Upstream and publication history](docs/UPSTREAM.md) explains the
known bases, unknown ancestry and public equivalents of locally built commits.

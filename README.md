# VibeWine 🍷

**Vibe-code the fix. Prove it works. Share it.**

An AI-friendly contributor workspace for **Wine on macOS and DXMT**: native
windows, Retina, accessibility, game compatibility and performance you can
measure. Human-written and AI-assisted contributions are welcome. Bring a
reproduction, a focused patch and evidence of what you checked.

**Current checkpoint: 29 October 2026.** The
[sustained-performance continuation](docs/diagnostics/sustained-wave-20261029.md)
reduces redundant bindings, completed-upload allocations and eligible adjacent
clears, and completes terminal queue-failure handling. It also adds bounded
scene capture, MSync pump repair and explicit native compiler/cursor experiments.
Focused controls, seven affected Wine targets and both complete matched graphics
builds pass. Installed-game behavior and measured FPS remain separate.
[Current status](docs/STATUS.md) records those boundaries.

The [first source drop](docs/releases/2026-10-25-source-preview.md),
[neighbor wave](docs/integrations/neighbor-wave-20261025.md),
[performance wave](docs/diagnostics/performance-wave-next-20261025.md) and
[presentation follow-up](docs/diagnostics/presentation-followup-20261025.md)
remain dated evidence for their respective source/build/install checkpoints.

The owner selected the preceding combined experiment
for diagnostic DS3 play; the previous playable endpoint remains recoverable. Source
adds seven independently gated renderer experiments, MSync rollback repair and
bounded attribution, alongside early scene submission. Both complete matched
builds and covered native/offscreen GPU checks pass. The experimental closure is installed with verified no-op convergence; controlled
game FPS, latency and memory comparisons remain pending.
[Changes, checks and limits](docs/diagnostics/fps-wave-20261025.md).

This repository distributes source, not a ready-to-install Wine runtime.
[First experimental source drop](docs/releases/2026-10-25-source-preview.md)
includes a recursive source snapshot; GitHub's automatic ZIP omits submodules.
[Selected upstream/fork backports](docs/integrations/fork-review-20261023.md),
[the integrated checkpoint](docs/diagnostics/integrated-wave-20261023.md) and
[status](docs/STATUS.md) distinguish source, builds, installation and game results.

[Patch origins](docs/PATCHES.md) · [Current status](docs/STATUS.md) ·
[Build guide](docs/BUILDING.md) · [Contribute](CONTRIBUTING.md)

## Start here

| You want to… | Authoritative place |
| --- | --- |
| Change Wine or DXMT | `components/wine` or `components/dxmt`, pinned below |
| Find a general fix and its original author | [Patch origins](docs/PATCHES.md) |
| Change behavior for one game | [Game-policy map](profiles/README.md); it names the actual engine gates |
| Run measurements or test control | [Telemetry](tools/telemetry/README.md) / [gameplay tools](tools/gameplay/README.md) |
| See what is built, installed or actually tested | [Status](docs/STATUS.md) |

The top-level `current/`, `native/`, `renderer/`, `ntdll/`, `baseline/` and
`WAVE*` material is retained history. It is not a second active engine tree.
New contributions start in the component trees; historical paths remain stable
for source attribution and old reproductions.

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

The workspace pins Wine `5d4b43e4` and the DXMT hot-path source checkpoint
listed in [current status](docs/STATUS.md). Components are pinned
submodules hosted on branches in this same repository.
Updating a component branch does not silently update the workspace pin. GitHub's
main-branch ZIP omits submodule contents; use a recursive clone or the component
links above. The selected candidate is rebuilt from these pins over a retained
engine/dependency base. The previous installed runtime is composite; neither is
a full reproducible Wine distribution. [Status](docs/STATUS.md) records these
boundaries explicitly.

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

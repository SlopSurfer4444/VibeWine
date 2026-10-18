# VibeWine 🍷

**Vibe-code the fix. Prove it works. Share it.**

An AI-friendly home for improving Wine on macOS: native window behavior,
Retina rendering, accessibility for automation, compatibility, and measurable
performance work. Human-written and AI-assisted pull requests are welcome.
Bring a reproduction, explain your change, and show what you checked.

**Early developer project:** this repository currently contains source snapshots,
patches and regression fixtures. It is not a complete Wine checkout, a downloadable
runtime, or a one-click replacement for CrossOver. The complete installed runtime
is not yet reproducible from this repository alone.

[Contribute](CONTRIBUTING.md) · [Current source and evidence](current/README.md) ·
[Roadmap](ROADMAP.md) · [Source map](SOURCE_INDEX.md) · [Build boundaries](BUILDING_WAVE7.md)

## Fix classes of bugs, keep game workarounds separate

| Track | What belongs here | Default behavior |
| --- | --- | --- |
| Core | General Wine/macOS and graphics-layer corrections | Correct the demonstrated defect; preserve unrelated behavior |
| Game profiles | Compatibility workarounds for a named title | Narrowly selected by an explicit application profile; no global override |
| Experiments | Instrumentation and unproven alternatives | Off by default, bounded and removable |

A bug found in Dark Souls III can lead to a general fix. A Dark Souls III
workaround does not automatically belong in every application's code path.
Keep the mechanism reusable and the exception in the profile. These tracks are
contribution rules and source classifications, not a claim of a new plugin framework.

## What is here today

- **General Wine fixes:** a BOOLEAN ABI correction affecting directory enumeration;
  Retina client-surface sizing; child-surface coordinate correction; handling
  failed surface creation and bridge allocations.
- **General DXGI correction:** checked errors and actual counts when finding a
  display mode, with bounded retries when the list changes.
- **Title-specific compatibility:** DS3's distinct-resolution cap is enabled by
  its executable profile, not imposed on all applications. See
  [the current classification](current/README.md).
- **Accessibility work:** macOS accessibility discovery and standard-control
  fixtures. Reliable end-to-end computer use in real games remains unfinished;
  a game-rendered UI does not automatically become a semantic accessibility tree.
- **Performance research:** opt-in diagnostics and regression work. No controlled,
  general FPS improvement is claimed yet.

The latest generic fixes passed source fixtures and local build/installation
checks. Those checks do not establish every game's compatibility. Earlier DS3
sessions demonstrated resolution changes, fullscreen exit and repeat launch;
latest-patch gameplay, Mission Control cursor release and sustained performance
still need separate testing. See [current evidence](current/README.md).

## Join in

Pick a reproducible bug, improve a fixture, test a second application, or help
make the build reproducible. Small, understandable PRs are a great starting point.
AI assistance is welcome; submitting a patch means taking responsibility for
reviewing it, explaining its source and reporting honest validation.

Use [the contribution guide](CONTRIBUTING.md) and the PR/issue templates. We review
behavior, maintainability, provenance and evidence. Generated code does not get
a free pass, and an AI tool's confident explanation is not a test result.

## Build on the ecosystem

VibeWine builds on the work of Wine, DXMT and their contributors. It is independent
of those projects and is not an official Wine release. Useful general patches
should remain reviewable and reusable by other projects; this is not an
anti-upstream campaign.

Related macOS projects include [Sikarugir](https://github.com/Sikarugir-App/Sikarugir),
[the frankea Whisky fork](https://github.com/frankea/Whisky), and
[Uncork](https://github.com/tonianev/uncork). Their scopes overlap with ours;
we make no claim to be the first or only project working on these problems.

Wine-derived code retains LGPL-2.1-or-later notices; DXMT trees retain their MIT
and third-party notices. Read each component's license. No game binaries, saves,
credentials or private machine-management code are included.

Older checkpoints remain for comparison. Their historical status is not the
current release status: start with `current/`, then consult
[the retained history](history/PRE_VIBEWINE_README.md) when needed.

# VibeWine 🍷

**Vibe-code the fix. Prove it works. Share it.**

An AI-friendly contributor workspace for Wine on macOS and DXMT: native window
behavior, Retina, accessibility, compatibility and measurable performance.
Human-written and AI-assisted contributions are equally welcome.

## Start with the source

```sh
git clone --recurse-submodules https://github.com/SlopSurfer4444/VibeWine.git
cd VibeWine
python3 scripts/workspace.py check
```

| Source | Purpose | Development |
| --- | --- | --- |
| [Wine source](https://github.com/SlopSurfer4444/VibeWine/tree/component/wine) (`components/wine`) | Complete Wine source and component history | `component/wine` branch |
| [DXMT source](https://github.com/SlopSurfer4444/VibeWine/tree/component/dxmt) (`components/dxmt`) | Complete DXMT source and component history | `component/dxmt` branch |
| [profiles](profiles/README.md) | Explicit title-specific compatibility | Opt-in, separate from core |
| [Telemetry tools](tools/telemetry/README.md) | Offline collector/analyzer and measurement protocol | `main` branch |
| [experiments](experiments/README.md) | Instrumentation and exploratory work | Off by default |

The components are pinned Git submodules hosted as separate branches in this
same repository. GitHub’s main-branch ZIP does **not** bundle submodule contents;
use the recursive clone above or browse the component links. Main pins an exact pair; updating a branch does not silently
change a checkout. Open component PRs against their component branch, workspace
PRs against main. See [contributing](CONTRIBUTING.md).

**Developer source workspace, not a runtime release.** Complete source trees do
not establish that their combined runtime is buildable, installed or gameplay
validated. See [build instructions and limitations](docs/BUILDING.md) and
[component provenance](docs/UPSTREAM.md). Workspace CI checks metadata and source
pins; it does not certify Wine builds, Metal behavior or FPS gains.

## What we work on

- General fixes for display modes, window surfaces, cursor release and lifetime.
- macOS accessibility and reliable automation; game-rendered controls still need
  visual feedback rather than an invented accessibility tree.
- Repeatable frame-time measurements and controlled performance experiments.
- Clear separation between reusable fixes and named-game exceptions.

[Roadmap](ROADMAP.md) · [Contribution guide](CONTRIBUTING.md) ·
[Historical installed-source evidence](current/README.md) ·
[Snapshot-era source map](SOURCE_INDEX.md)

The retained `current/`, `native/`, `renderer/`, `ntdll/`, `optional/` and evidence
folders describe earlier snapshots and experiments, not the source entrypoint
for new component work. Their contents and history are preserved for comparison.
They are not a claim that the new component pair has passed those old checks.
The retained `FILE_MANIFEST` and provenance records describe their original
snapshot scope; they do not inventory or certify the new component trees.

## Built with the ecosystem

VibeWine is independent of Wine, DXMT and CodeWeavers. Preserve upstream authors,
licenses and history. Wine-derived source retains LGPL notices; DXMT and its
third-party components retain their own licenses. This workspace does not
relicense them. No proprietary game files, saves or credentials belong here.

AI assistance is welcome; responsibility for provenance, review and evidence
stays with the contributor. No general FPS improvement is claimed without
controlled measurements.

# Development archive

This directory retains earlier source snapshots, build recipes, audits and
iteration records. The active development entry points are the complete
[`Wine`](../components/wine) and [`DXMT`](../components/dxmt) components;
start with the [project guide](../README.md).

| Material | Location |
| --- | --- |
| Earlier source snapshots and their fixtures | [`legacy/`](legacy/) |
| Original development index | [`legacy/SOURCE_INDEX.md`](legacy/SOURCE_INDEX.md) |
| Original iteration roadmap | [`legacy/ROADMAP.md`](legacy/ROADMAP.md) |
| Status ledger through 29 October | [`reports/status-through-20261029.md`](reports/status-through-20261029.md) |

Names such as `wave6`, `wave7` and `current` identify **historical checkpoints**.
They are neither product versions nor alternative current source trees. Old
“current”, “installed” and “pending” statements describe their original date.
No command in an old report is an instruction to change an installed runtime.

The public-layout change relocates the old trees together, preserving every
non-Markdown file byte for byte. Markdown navigation is adjusted for the new
locations. Historical manifests and build receipts retain their original paths
and hashes; they describe their original export, not a checksum of this new
layout. For original path/hash interpretation, use the
[pre-layout tree](https://github.com/SlopSurfer4444/VibeWine/tree/1d42b291769267e7f5a618f137d91fbc59cd8b43).
Git history and original author/license information are preserved.

A few maintained telemetry regression tests reuse archived fixture inputs.
That narrow dependency is explicit; it does not make these snapshots production
edit targets. Run historical fixtures from the relocated `legacy/` root in a
disposable checkout, following their own prerequisites.

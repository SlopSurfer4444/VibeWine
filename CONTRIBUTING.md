# Contributing to VibeWine

Small fixes, reproducible bug reports, documentation, and AI-assisted contributions
are welcome. You are responsible for understanding, reviewing, and testing what
you submit, including code suggested by an AI tool. Mention substantial AI
assistance and the parts you personally checked; no transcript is required.

## Choose the right scope

- **core:** general Wine, macOS, or renderer fixes. Preserve existing behavior by
  default outside the demonstrated bug, and cover relevant unaffected cases.
- **game-profile:** title-specific workarounds. Keep them separate from general
  fixes, explicitly opt-in, and narrowly selected by the intended title/profile.
  Document activation, deactivation, and why the general path is insufficient.
- **experiment:** exploratory changes and instrumentation. Keep them off by
  default, bounded, and easy to disable.
- **tooling / docs:** build helpers, fixtures, evidence tools, or documentation.

These are scope descriptions, not a promise that matching GitHub labels exist.
Avoid mixing unrelated fixes and title workarounds in one patch. Discuss larger
changes in an issue before investing in a broad rewrite.

## Prepare a patch

Start with [the README](README.md), [build instructions](docs/BUILDING.md) and
[upstream provenance](docs/UPSTREAM.md). Edit complete source in `components/`.
Use the [patch map](docs/PATCHES.md) to identify an existing fix or experimental
base before duplicating it. Update the relevant provenance entry when importing
or adapting code; mark unknown origins explicitly.
Commit inside the component and open its PR against `component/wine` or
`component/dxmt`, not main. Once accepted, a separate main PR updates the gitlink
and records integration evidence. Never use `submodule update --remote` as a
substitute for a reviewed pin update. Historical snapshot folders are evidence,
not duplicate production edit targets.

For a component checkout, create a branch before editing (submodules initially
use detached HEAD): `git -C components/dxmt switch -c my-fix`. Push that branch to
your fork and target the matching component branch in VibeWine.

Explain the failure, the intended behavior, and the smallest useful reproduction.
Add or update a regression fixture where it meaningfully catches the bug. Report
the commands you ran and their results, including failures and checks you could
not run. Run fixtures that generate outputs in a disposable copy as described in
[current/tests/README.md](current/tests/README.md).

Source fixtures, a successful build, installation, and observed game behavior are
different evidence. State which you verified. For title profiles and experiments,
check the default path with the feature disabled as well as the enabled path.
For performance claims, compare the same scene, resolution, graphics settings,
and cache state; include hardware, runtime versions, measurement method, and
before/after results. A perceived improvement alone is useful feedback, not a
measured FPS gain.

## Sources and licenses

Preserve the existing component's license, copyright notices, and attribution.
Wine-derived and DXMT-derived trees have different licenses; read the notices in
the tree you modify. For imported or adapted code, identify its source URL,
revision when available, license, and your adaptations. Mark unknown provenance
as unknown rather than inventing a revision or source history. Do not submit code
you lack permission to contribute.

For retained runtime source patches, follow their named bases and reconstruction
instructions. A patch exported from a private build is not automatically a safe
merge into a newer component or proof of a full reproducible binary build.

Keep generated binaries, proprietary game/runtime files, credentials, saves,
personal paths, and private logs out of patches. Review logs and screenshots for
private information before attaching them. Include only evidence needed to
understand and reproduce the change.

Use the pull request template for a short explanation and honest validation
record. Documentation-only changes need a link/content review, not game testing.

# Source history and upstream updates

The workspace uses pinned submodules because Wine and DXMT have independent
histories, licenses and release cycles. Both are hosted in VibeWine component
branches; no synthetic history or force rewrite is needed to combine them.

- Wine upstream: https://gitlab.winehq.org/wine/wine
- DXMT upstream: https://github.com/3Shain/dxmt

Read `components/wine/vibewine/README.md` and each component's provenance notes
for its actual base and local deviations. The Wine branch begins with a complete
vendor corresponding-source archive import, not a fabricated Wine upstream
parent. Its exact upstream Git ancestor remains unknown; review backports instead
of merging unrelated histories blindly. DXMT can retain its identified upstream
history.
Wine upstream identity is not necessarily the identity of a historical installed
CrossOver-derived runtime. Do not infer exact ancestry from a version string.

## Current provenance map

[Patch origins](PATCHES.md) links each change to its actual source and scope.
[DXMT provenance](provenance/dxmt.md) records the identified upstream ancestor,
local changes and the matched graphics build. [Wine provenance](provenance/wine.md)
separates the stable component, experimental XInput/network branches and exact
[native-driver/MSync source deltas](../patches/runtime-2026-10-23/README.md).
The installed test mixture is not a new Wine release or a clean rebuild of the
stable component pin.

[The 2026-10-23 fork review](integrations/fork-review-20261023.md) records later
development backports, including small personal forks, rejected duplicates and
platform/ABI mismatches. These source changes are distinct from the prior matched
bundle and preserve original upstream author and license attribution.

Six previously unpublished DXMT commits used machine-local email metadata.
Their public equivalents preserve exact source trees, original author
names/dates/messages, upstream ancestry and licenses while using the public
account email. [The map](../components/dxmt/docs/PUBLICATION.json) binds original
build-source identities to public equivalents. Original local commits and
historical build records are retained. A subsequent documentation-only commit
updates public baseline references; production/test code remains identical.
No published history is force-rewritten and no new runtime build is inferred.

## Update flow

Fetch upstream in an isolated component branch; inspect changes and
resolve conflicts by behavior; run component checks; merge the component PR; then
update main's gitlink with integration evidence. Preserve local corrections only
where still needed. No game-specific override becomes a global default merely
because a merge succeeds.

Previous source snapshots remain in this repository and its Git history. The
snapshot-era main checkpoint is `92feab216314f4b13f71e280fdd0afa7a89592c6`.

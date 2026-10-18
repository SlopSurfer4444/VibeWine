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

Update flow: fetch upstream in an isolated component branch; inspect changes and
resolve conflicts by behavior; run component checks; merge the component PR; then
update main's gitlink with integration evidence. Preserve local corrections only
where still needed. No game-specific override becomes a global default merely
because a merge succeeds.

Previous source snapshots remain in this repository and its Git history. The
snapshot-era main checkpoint is `92feab216314f4b13f71e280fdd0afa7a89592c6`.

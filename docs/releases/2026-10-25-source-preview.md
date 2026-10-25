# VibeWine 0.1 experimental source drop

Tag: `v0.1.0-experimental.20261025`. This is a developer source prerelease,
not a ready-to-install Wine distribution or a stable performance release.

VibeWine welcomes human-written and AI-assisted contributions with a
reproduction, attribution, review and meaningful checks. General mechanisms,
title-specific policies and diagnostic experiments remain separate. This
project does not speak for upstream contribution policies.

## Included

- Complete pinned Wine `85864be47d483d4518f729baef48cbf52ea1e5cf` and DXMT
  `25eff5e13003d5e7f6b7cf6e1c76e9e6f3363e27` source trees, plus their pinned
  dependencies and original licenses/notices.
- Attributed Wine/fork fixes, native window/accessibility integration, separate
  DS3 compatibility policies and bounded diagnostic tools.
- Seven new default-off renderer experiments, independent early scene submission,
  and MSync WaitAll rollback/attribution work. See the
  [mechanism and evidence map](../diagnostics/fps-wave-20261025.md).

Both matched DXMT variants and scoped native/offscreen GPU checks passed in the
documented environment. A combined experiment was privately installed with
verified afterimages and recovery retained. The owner subsequently reported
smoother play under background load. That is useful uncontrolled feedback;
there is no controlled before/after FPS, latency or memory improvement claim.
See [current status](../STATUS.md) and [patch origins](../PATCHES.md).

## Download or clone

Use the release's attached `VibeWine-v0.1.0-experimental.20261025-source.tar.gz`
and verify it against `SHA256SUMS`. The separate source manifest records each
Git tree, payload hashes and package scope. The archive recursively includes
component/dependency contents; GitHub's automatically generated ZIP/tar does not.
No private installer, proprietary Apple/CodeWeavers runtime, SDK, game data,
prefix, saves or private diagnostic captures are distributed.

The pinned NVAPI dependency contains its upstream MIT-licensed import libraries
and `License.txt`. This is a source/dependency snapshot, not a claim that every
tracked dependency input is textual source. DXMT's current LGPL notices and
historical MIT notices are retained without relicensing the whole component.

For normal development, keep Git metadata:

```sh
git clone --recurse-submodules --branch v0.1.0-experimental.20261025 https://github.com/SlopSurfer4444/VibeWine.git
cd VibeWine
python3 scripts/workspace.py check
```

The archive has no `.git` metadata. Archive users should read
[BUILDING](../BUILDING.md); `workspace.py check/init` is for Git checkouts and
will not validate an extracted snapshot. Building still requires matching
toolchains, SDKs and explicitly supplied dependencies. A complete reproducible
Wine runtime is not established by this release.

## Next wave

Refresh neighboring upstreams, inspect exact code and licenses, reject duplicate
or unsuitable imports, and adapt selected general fixes with failing-baseline
controls. The research converges on frame-time tails and presentation/queue
latency. New source candidates do not silently replace the playable runtime.
No benchmark claim is inferred from the size of the patch set.

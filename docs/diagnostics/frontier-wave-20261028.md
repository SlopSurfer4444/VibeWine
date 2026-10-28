# Frontier implementation wave — 28 October 2026

This is a local source/build checkpoint, separate from the installed playable
runtime. DXMT `8d0cef8dbcc8d1efbd0db97d4a0aee9e86d592d0` extends `8cf59a01`; Wine
`bb2c891987c9551329fc7dfbb7c7f41abfceb582` extends `5ed4c56b`. No game data, saves,
network settings, installed runtime selection or ordinary game icon is changed.
No DS3 session is part of this checkpoint and no FPS improvement is claimed.

## What changed

The [DXMT component contract](../../components/dxmt/docs/frontier-wave-20261028.md)
describes staging payloads, same-buffer range reset, query no-write/ready reuse,
ordinary PSO/pixel-key normalization, per-key shader parsing, row depth/stencil
uploads, exact opt-in VB/SRV dependencies and tile-helper failure handling.
These changes address general classes of D3D11 behavior; none adds a DS3 gate.

Wine now distinguishes an MSync object index from an operating-system file
descriptor during thread cleanup. Real-fd backends retain their close behavior.
A stable segmented shared-page directory replaces moving client/server arrays:
readers acquire both publication levels, writer locks publish new slots, and
published segments/maps live until process exit. Allocation/map failure stops
before invalid pointers are published or dereferenced. This is a deliberate
failure policy, not proof that other clients make bounded progress after server
death. The pump uses the new lookup with its prior transport behavior.

The larger Mach RPC/rights candidate remains held. Its native matrix passed,
but Rosetta had three aggregate right-count failures, two still unclassified.
Those checks were preserved as failures and that subpart was excluded from the
accepted source. Blocking transport, post-ACK server-death liveness and malformed
pump message validation remain separate work.

The explicit `combined-bounded` metric profile records compiler, presentation,
stall and helper spans together. The consumer keeps ambiguous or incomplete
coverage unknown; existing profile/report compatibility is retained. It is
source tooling, not an installed always-on collector. Capture data lifecycle
and limits remain governed by the existing telemetry contract.

## Checks and experiments

Integration ran thirty new DXMT fixture commands and all fifty-one existing
DXMT workflow commands. Two old fixture seams needed updating for the new pixel
key/visibility gate; their original assertions were retained and affected
commands passed after repair. Telemetry passed 272 tests, Python gameplay 48,
Node gameplay 52. Local Node was the existing v24.19.0; CI targets v22.
Wine source verification covers 11,194 vendor/ordered-overlay files. Table
growth/fatal controls pass native/Rosetta ASan/UBSan, including four reproduced
old moving-array UAF controls. Alert ownership includes genuine fd sentinels
and non-MSync cleanup controls. Independent integrated source review found no
new blocking defect; documented GPU/API/liveness limits remain.

Two private standalone prototypes established real architecture seams:

- Metal4 public compiler creates ordinary PSOs usable by the classic command
  queue/encoder. Tiny MSL and a translated known Wine VS/PS pair passed eight
  GPU draws and 2,048 pixel observations. Invalid function handling requires
  prevalidation; descriptor NSError behavior is not universally established.
- Native ARM airconv and x64 produced identical AIR/reflection across seventeen
  recipes. Thirty-nine protocol/lifecycle observations passed. ARM child was
  faster than x64 child on this corpus, but the one-shot route was slower than
  direct x64. It is not enabled or accepted as a performance improvement.

Those experiments are not a Metal4 engine migration or a production compiler
service. Full recipe ABI, scheduler/cache ownership, controlled compile-demand
cost, native bridge integration and representative rendered output remain gates.

Both fresh 161-target matched release builds pass, with all nineteen existing
experiment switches false or true. They use the same freshly configured Wine,
compiler, PE/native bridge and Metal helper; all seven bundle artifacts per
variant and seven affected Wine outputs were hashed. The first renderer build
failed on an incorrect helper-trace include path; the explicit relative include
repair was reviewed and affected tests passed before the fresh successful pair.
The Wine build's broad before/after snapshot also recorded one concurrent DXMT
documentation addition: Wine and DXMT compiled bytes were unchanged. The
original failure/drift receipt and its narrow adjudication are retained; build success alone cannot promote either variant to the
installed runtime or establish better gameplay.

## Follow-up admitted from the full audit

All sixty full audit cards and five ancillary findings were reconciled against
the existing research and current source; the external CPU probe ZIP was read
as data, never executed. Its small probes add no acceptance beyond the stronger
production-bound fixtures above. Further source-bound surfaces include buffer
view prefix/memoization, nil queue/ring allocation and GPU-error publication,
MSync free-cursor/deadline/liveness details and mapped-state bookkeeping. They
remain hypotheses or held repairs with their own required controls, not silently
implemented or attributed to DS3.

Private evidence owner is the root frontier integration campaign. Raw logs have
seven-day TTL and 32 MiB per-branch cap; curated summaries thirty days / 8 MiB;
build logs 16 MiB, products 40 GiB with 20 GiB free reserve. Active integration,
unresolved defects, rollback and explicit holds are excluded. Manual exact-path
cleanup rechecks ownership/holds; no collector or automatic cleaner is installed.
Source tests/docs remain versioned; private raw logs, audit conversations,
proprietary inputs and station paths are not exported.

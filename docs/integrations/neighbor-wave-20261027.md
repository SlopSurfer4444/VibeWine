# Neighbor refresh and source adaptations — 27 October2026

This wave reviews fresh primary repository histories and carries four bounded
source repairs. The installed DS3 runtime remains on its preceding `bbf22e6a`
installation (`e7410fc`); no game launch, cache deletion, runtime replacement or
FPS improvement is claimed here. Source and build acceptance remain separate
from later native/game acceptance.

## Actual fresh coverage

| Source | Observed head | Result |
| --- | --- | --- |
| [DXMT](https://github.com/3Shain/dxmt/commit/3b252590cb0290e36fca6f088b5255b871d2d95a) | `3b252590` |26 commits after previous `a148782d`; three applicable corrections below |
| [Wine mirror](https://github.com/wine-mirror/wine/commit/f343ca6085bfac807c33e9c1a8a43105071f1416) | `f343ca60` |36 commits beyond previous audit; SDL signal ownership is a follow-up candidate |
| [DXVK](https://github.com/doitsujin/dxvk/commit/bd395bf055e69943e74f869581301ed24cf16b53) | `bd395bf0` |fresh memory-pressure and relocation scheduling designs; no Vulkan transplant |
| [Highball](https://github.com/gauthierpiarrette/highball-engine/commit/e497c41d16a1ebfd22d8fdd307ea73439c6df7ee) | `e497c41d` |new .local loader search; held for missing Windows manifest/DevOverrideEnable semantics |
| Sikarugir, WineAndAqua, Dormison, gzimbric, Gcenx, ossover |same default pins as25October |new push timestamps do not prove new implementation; old late-icon candidate now adapted |
| [g-cqd](https://github.com/g-cqd/dxmt/commit/6a197c73f85e45e0f33ed6fac483d983beab170b), [NerRobDog](https://github.com/NerRobDog/dxmt/commit/8a9f4b749b3dce1f0340b7f92b4ce5d300d22421) |unchanged inspected default histories |NSError/failed-PSO imports are not counted again; parsed-IR release remains a distinct design candidate |

Also checked bounded recent fork inventories (overlapping newest50 and30 DXMT
fork listings), sampled histories and selected option files, plus small-project
repository searches. These are not exhaustive all-branch or all-fork searches.
Original Whisky is archived; launcher metadata is separate from engine code.
CrossOver source archive was not revalidated in this scan. Existing vendor-base
ancestry remains an explicit limitation.

## Integrated repairs and extensions

| Donor mechanism | Local adaptation | Verification boundary |
| --- | --- | --- |
| [DXMT call lifetime9839e55f](https://github.com/3Shain/dxmt/commit/9839e55f98d4aca103957ee4132d6395a325a9ed) |capture continuation before variant replacement; also repair independently found CALLC wrong-variant bookkeeping |actual parser/CFG TU, nine executable graph scenarios, production LLVM verification, ordinary and fault-injected baseline oracles |
| [GS points d162ea44](https://github.com/3Shain/dxmt/commit/d162ea44a31fbf9089ea09977337f0beb30bd082) |correct metadata, emitted point size and primitive count |actual GS TU, three distinct failing baseline point oracles, five point scenarios and16 strip controls, LLVM/UBSan |
| [ColorSync tags a741a3fa](https://github.com/3Shain/dxmt/commit/a741a3fad14a4bc5fc899f85bfa35de8f933e74b) |release all acquired tag exits, including invalid signature missed by donor |real CFData objects, seven cases repeated128times, exact six baseline leaking paths, unchanged output semantics |
| [Late Dock icon8df085af](https://github.com/gzimbric/wine-arm64-macos/commit/8df085af744d252b81be8a7d96cf1f01086c156b) |common existing request callback covers CGImage and CrossOver URL paths, only when already regular |real AppKit/CoreGraphics objects, native/Rosetta36 assertions and five exact baseline failures; GCD substitutes actual CFRunLoop request transport; no real Dock observation |

Donor authors, pinned licenses and original component notices are preserved in
[DXMT provenance](../../components/dxmt/docs/upstream-backports-20261023.md)
and [Wine provenance](../../components/wine/vibewine/UPSTREAM_BACKPORTS.md).
Shader-output epochs4/5 replace2/3 without changing AIRCONV API26. No title-specific
workaround or automatic settings change is introduced. Test-only continuation
fault injection keeps header/type definitions identical to the support archive.
The first discarded destructor-sentinel fixture had a mixed-definition ODR
risk; it was repaired before acceptance, not treated as valid failure evidence.

## Are other forks using VibeWine patches?

No affirmative evidence found within the checked scope. The public repository
reported zero forks. Exact distinctive identifier/code and commit searches,
and five selected external option files, did not establish reuse. Those negative
searches are weak evidence: GitHub's index also missed our public nondefault
component branch. Similarity to NSError/failed-PSO fixes is not our invention:
we had imported/adapted those donors ourselves.

At observation, public heads were main `c9af34b3`, component/dxmt `99e1a1c3`,
component/wine `5d4b43e4`; local diagnostic/reuse commits through `bbf22e6a`
were not yet present in that published renderer branch. Do not attribute copying
from chronology, stars, push dates or a similar design alone.

## Next performance work, grounded in actual code

1. **Unified-memory pressure and fair retirement.** DXVK's
   [actual-free-chunk reservation9c51dd8b](https://github.com/doitsujin/dxvk/commit/9c51dd8b5d67ee3236b7839e6e8827b1f78075c9),
   [rotating relocation cursor e197a8a5](https://github.com/doitsujin/dxvk/commit/e197a8a576de2748e40dcdbfd7a4a81a326115ce),
   and [deduplicated reversible promotions f4c74339](https://github.com/doitsujin/dxvk/commit/f4c7433927b2923034974e54129e8b71e224bac4)
   suggest bounded accounting and starvation controls. DXVK skips dedicated-VRAM
   eviction on integrated GPUs: its eviction policy is not a Metal/unified-memory
   recipe. First correlate our completion cards, upload bytes, retained objects,
   GPU duration and host pressure; preserve pending resource lifetimes. Then
   reproduce a specific over-retention or starvation case before changing policy.
2. **Shader IR retention with rematerialization.** NerRobDog's older direction
   remains worth a distinct resource-lifetime trial: saved parsed-IR bytes must
   exceed retained DXBC plus reparsing cost, including late shader variants,
   compilation cancellation, reflection and paired shader lifetimes. Not a new
   patch today and not an assumed cure for our host-wide swap observations.
3. **Windows resource usage correctness for launchers.** The new small-project
   [Overpass GetDXGIUsage patch](https://github.com/McKack/overpass-runtime/blob/b90803d4186642ee79eb9bedbc91c555a9c86db8/overpass/patches/0002-d3d11-report-DXGI-usage-from-bind-flags-and-CPU-acce.patch)
   targets ANGLE/CEF shared textures; our GetDXGIUsage still returns0. Validate
   DXGI CPU flags and all resource classes against the API contract before
   importing. This is compatibility work, not a direct FPS claim.

Held: Mac-Neutron drawable prefetch depends on VM-host WSI, Highball .local
changes omit manifest policy, D3D12/IOHelper rewrites carry broader dependencies,
and TPIO pointer-list membership still does not solve allocator-reuse ABA.
SDL no-signal-handlers needs a real linked-child signal-owner regression. No
architecture experiment is silently bundled with the four narrow fixes.

## Evidence and retention

Focused tests include expected old failures; native full-TU syntax and matched
release compile results are recorded in the wave's final receipt. Existing
workspace checks validate source pins and package metadata, not gameplay.
New dependency-heavy compiler fixtures are documented local checks, not claimed
hosted CI. No telemetry protocol/producer changed. No remote CI pass is inferred.

Private source API extracts, logs and scratch builds are under the existing
neighbor-refresh-20261027 output owner: raw7days, notes30days, per-branch caps,
active review/recovery holds and manual exact-path cleanup. Curated source,
provenance and decisions in Git are durable results. No memory file, system
service, cleanup policy or installed-game setting is changed.

# Neighbor review and focused convergence — 25 October 2026

The [first experimental source drop](../releases/2026-10-25-source-preview.md)
freezes the preceding playable-source checkpoint. This follow-up adapts three
general correctness mechanisms. They have no DS3 title check and do not replace
the installed runtime. A newer source commit is not a new installed build or a
measured FPS gain.

## Fresh source coverage

Primary GitHub branch/commit/file APIs were checked on 25 October. Dates are Git
commit dates, not release claims. Discovery included the newest 50 DXMT forks,
sampled default histories, renamed projects and smaller personal Wine projects.
This is bounded research, not an exhaustive inventory of every fork.

| Source | Observed pin/date | Result |
| --- | --- | --- |
| [DXMT main](https://github.com/3Shain/dxmt/commit/a148782d431249405acdb1531dbb0c7e737bbece) | `a148782d`, 23 October | No newly observed main delta after the previous audit; inspect neighbor ideas rather than manufacture an upstream merge |
| [Wine mirror](https://github.com/wine-mirror/wine/commit/e3c3d348676128bc745c40aa6397b51951ef1cd6) | `e3c3d348`, 24 October | Review individual portable corrections against the vendor-derived local base; unknown exact vendor ancestry remains unknown |
| [DXVK](https://github.com/doitsujin/dxvk/commit/cbd2206d72d3d9dd9941d5d868bb03a858e85f9b) | `cbd2206d`, 24 October | Demand-aware compiler scheduling and explicit discard intent are useful designs; Vulkan implementations are not direct Metal transplants |
| [Sikarugir](https://github.com/Sikarugir-App/wine/commit/840a939ee526137639665ff5f587224d6a36403f), [WineAndAqua](https://github.com/WineAndAqua/wine/commit/87f8b21babfba1ad921096673a66bcb06377e8dc) | 19 / 20 October | Already-covered native changes and broader vendor hacks; no wholesale queue import |
| [Dormison](https://github.com/kageroumado/dormison/commit/b4329c188072c356e878b57a8b9d5a161d35f648), [gzimbric](https://github.com/gzimbric/wine-arm64-macos/commit/21c45a2c3bb32e5b2a1ed92d72be6f2e6e3e72f6) | 22 / 9 October | Previous topmost/Metal lifetime adaptations remain distinct; GL aliases and native app identity are follow-up compatibility candidates |
| [Gcenx](https://github.com/Gcenx/macports-wine/commit/850f5330ed500a87bdcf8441763ad6cf4109aedc), Highball/ossover, GE/Valve/staging | Exact relevant changes inspected | Keep prior portability/sync imports; review remaining lifetime and completion ownership independently |
| [Soju](https://github.com/BCD1210/soju/commit/43ed224336cdffdba9dc4b47aa858a5273eb0838), [SWGL-on-Mac](https://github.com/jdrly/swgl-on-mac/commit/1c248cd46cae078e74b54a74db656de236488a01) | 21 October | Input/IME ideas found; source-license and ABI/thread-lifecycle boundaries prevent a blind copy |

Recent push timestamps alone were rejected as evidence of new implementation.
Whisky/launcher releases, CrossOver public source, Sikarugir recipes and actual
renderer sources remain separate layers, as in the [previous review](fork-review-20261023.md).

## Adapted source repairs

| Mechanism and donor | Local extension | Checks actually exercised |
| --- | --- | --- |
| [Borrowed NSError adoption, g-cqd `cbf3c18c`](https://github.com/g-cqd/dxmt/commit/cbf3c18cee85195b1519d33e4980001b854f1beb) | All 14 local error-producing wrappers propagate owning references. Retain before replacing, including the same object; null success avoids an extra retain call. Archive/retry and residency callers preserve ownership. | Actual production header/native/PE ownership seams with real Foundation objects; arm64 and Rosetta ASan/UBSan; baseline, omitted-retain and donor-style same-object replacement controls; caller API compilation |
| [Render](https://github.com/g-cqd/dxmt/commit/cb535b372e41a7dc8f6b39606d9da22c2d8a09f9) / [compute](https://github.com/g-cqd/dxmt/commit/82b4f16e0bee910779a6434fe68eede89aa5198d) failed-PSO cancellation | Guard all 14 draw and two dispatch work lambdas with execution-encoder state. Success resets failure. Preserve uploads needed by unchanged-shader recovery; GS/TS metadata changes only after success. No automatic retry or silent compiler-success claim. | Extracted production scheduling calls with real CommandList; stale-PSO baseline; success/failure/success, replay/pass switches and internal helpers; three connected mutations; existing encoder-marshalling modes |
| [Address queue publication, Paul Gofman/Wine `81e52628`](https://github.com/wine-mirror/wine/commit/81e52628b25ac173d04796e8a9b9cda637d3e233) | Snapshot thread ID and detach stack entry before release-publishing NULL; acquire on unlocked waiter precheck. Preserve every timeout's queue lock, previously adapted promised-alert drain and backend errors. | Actual production bodies on native/Rosetta under ASan/UBSan; two precise old-source publication failures; 257-entry batching/collision controls; existing native-kqueue/futex timeout/error checks; full changed PE translation unit compiled for i386/x86_64 |

The g-cqd donor `LICENSE` at all three exact commits is MIT, copyright 2023
Feifan He; its notice is retained in DXMT `LICENSE.OLD`, alongside current LGPL
component notices. This differs from reading the donor's current default branch
license. Wine's donor retains LGPL-2.1-or-later attribution. Adaptations are
identified; imported work is not relabeled as invented by VibeWine.

The integration owner reran the focused positive, failing-baseline and mutation
checks against the combined production tree. Wine's ordered source verifier
accepts 11,179 files. The native ownership runner intentionally requires the
documented local Foundation/Rosetta environment; it is not silently treated as
portable CI coverage. The workflow adds the portable publication/cancellation
controls; declared CI steps do not themselves prove a remote CI pass.

Full matched renderer build results and exact component pins belong in
[STATUS](../STATUS.md). These CPU/source checks do not establish GPU error-path
recovery, a linked complete Wine distribution, installation or gameplay.
Successful shader lowering/API/cache epochs are unchanged by these repairs.

## Converged performance directions

1. **Displayed-frame tails and presentation latency.** First collect a short,
   explicitly phase-marked repeatable route with a capacity-calibrated full trace:
   queue admission, frame-latency wait, drawable acquisition, submission,
   completion and native presented feedback. The compile-waits profile excludes
   the presentation scopes and cannot attribute those delays. Distinguish
   displayed intervals from CPU Present counts, skipped-only observations,
   per-second aggregates and actual per-frame distributions. Do not label
   aggregate percentiles as per-frame P99/1% lows. Compare queue depth/input
   latency/memory as well as cadence before changing admission policy.
2. **Demand-aware compiler capacity.** Inspired by DXVK's urgency queues, test a
   demanded PSO behind speculative pixel variants using the current scheduler.
   Require dependency-priority propagation, race-safe promotion, unchanged worker
   count and no starvation or abandoned tasks. Only add a default-off policy
   after a controlled saturation fixture demonstrates the scheduling delay;
   validate cold and warm actual compilation separately afterward.
3. **Explicit discard intent and IR retention.** Check whether the workload calls
   DiscardResource/DiscardView before implementing narrow full-subresource color
   discard boundaries. Do not confuse this with existing adjacent-clear dead-store
   elimination. Separately evaluate NerRobDog's parsed-IR release/rematerialization
   with retained DXBC accounting, paired shader lifetimes, task cancellation and
   reflection/output equivalence; memory saved must exceed new backing/reparse cost.

Concrete rejecting controls matter more than gathering every available patch.
Lowering precision, suppressing integer writes, or returning an empty pipeline
while compilation runs are not accepted substitutes for rendering correctly.

## Held and deferred work

- Highball's TPIO direct-pointer completion list needs an allocator-reuse/identity
  discriminator before transfer; list membership alone does not settle ABA.
- Broad Vulkan GPL pipelines, D3D12/DXIL migration, GCMouse thread/ABI rewrites,
  all-live residency registries and cross-process IOSurface presentation remain
  separate architecture work. No demonstrated generic FPS benefit follows.
- Wine semaphore-error/process-exit ordering and late native Dock icon/promotion
  patches remain named follow-up candidates; they are not included in these three
  repairs or silently counted as finished.

Private raw audit/source extracts, test outputs and captures retain their existing
bounded owner/TTL/hold contracts. Curated source, provenance and decisions are
durable Git results. No new collector, cache producer, installed service or
cleanup mechanism is introduced by this source wave.

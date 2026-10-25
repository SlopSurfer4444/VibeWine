# Failed application pipeline work cancellation, 25 October 2026

Source base: `25eff5e13003d5e7f6b7cf6e1c76e9e6f3363e27`.
This is a general correctness repair, independent of the optimization switches.
A null asynchronously compiled application PSO previously skipped only its queued
set-PSO command. Later application draw/dispatch commands could still execute
with a preceding successful PSO and incompatible argument bindings.

## Checked reuse and license

Adapted the scoped cancellation design from g-cqd/dxmt:

- [Render cancellation, cb535b372e41a7dc8f6b39606d9da22c2d8a09f9](https://github.com/g-cqd/dxmt/commit/cb535b372e41a7dc8f6b39606d9da22c2d8a09f9).
- [Compute cancellation, 82b4f16e0bee910779a6434fe68eede89aa5198d](https://github.com/g-cqd/dxmt/commit/82b4f16e0bee910779a6434fe68eede89aa5198d).
- Donor author: g-cqd. Donor [LICENSE](https://github.com/g-cqd/dxmt/blob/cb535b372e41a7dc8f6b39606d9da22c2d8a09f9/LICENSE)
  at these exact donor commits is MIT (copyright 2023 Feifan He), whose notice
  is retained in `LICENSE.OLD`. The current component's LGPL-2.1-or-later notices
  remain intact. This is an adaptation, not a wholesale donor commit transplant.

The current local queue, encoder state and deferred replay already supply the
needed lifetimes. Reusing those plus the small donor guard costs less than a
separate cancellation registry, reference-counted flag per command or retry
subsystem. Donor warning telemetry, shader names, compiler retries and GPU test
program are not incorporated here.

## Scope and invariants

Each render/compute encoder owns one default-initialized failure bit. Every
application PSO receipt overwrites it, including a successful receipt after a
failure. Fresh passes have fresh state; encoder data lives on the existing queue
encoding heap, with no cross-thread capture or additional owner allocation.
Deferred lists apply the receipt and dependent work to the active encoder on each
replay. Physical pass merging happens after these lambdas encode their commands,
so a merged native pass cannot revive omitted work. The recording-side ready state
may continue to reference the failed cached pipeline; repeated work remains
suppressed until another PSO is resolved successfully. This repair adds no retry.

All fourteen render work lambdas and both compute work lambdas check at entry.
Rejected work performs no indirect argument access, temporary dispatch allocation,
GS/TS marshal append, visibility advance, barrier resolution or draw/dispatch
command emission. The tessellation guard also precedes division by the successful
pipeline's threads-per-patch value. Unsupported DrawAuto remains an explicit
compatibility observation and does not encode a draw.

Vertex, stream-output and shader resource binding uploads still run. Their
recording-side dirty bits have already been cleared; suppressing uploads loses
updates needed by a later successful PSO when shaders/bindings remain unchanged.
They use reflection and binding state rather than the failed native PSO. The
repair gates application work, leaving independent clear/copy/scale/barrier and
query completion commands on their existing paths.

Tessellation metadata and pass-wide geometry/tessellation use flags update only
after a successful PSO receipt. Failure must not add a stage or clear an earlier
successful draw's pass-wide requirements. `last_pso` retains the last successful
PSO for existing pass machinery; failure state forbids application work from
using that stale handle.

## Acceptance boundaries

The focused CPU fixture is in `tests/vibewine/failed-pipeline/`; its README records
production extraction seams, baseline/mutation controls and remaining gaps.
The candidate and all three named mutation controls passed with ASan/UBSan on
25 October 2026. The exact base-source control exposed both stale render and
compute work, as expected. The existing encoder-marshalling context fixture also
passed in default, optimized and reference modes. Commands are in the focused
README; tests used implementation SHA-256
`24ab3c93ff3293a0539d187cca4a2724773645fa01be14a0640202e73b3e31c0` and
header SHA-256
`eec91f4e356efb9638394469802e59bc7e9b250c82126d7777f4d2cac18574df`.
A fixture pass demonstrates command cancellation and state recovery with injected
receipts. It does not demonstrate a real Metal compiler failure, rendered pixels,
matched PE/native build success, installed behavior, game compatibility or FPS.
The integration owner retains matched-build and disposable offscreen GPU gates.

No persistent producer, log or cache is added. Curated source/tests/this note are
durable project artifacts. Test-generated source and binaries belong to the
focused temporary runner and are removed on exit; task scratch is bounded to
64 MiB, expires after seven days, and preserves active/analysis holds. No automatic
cleanup or authority over unrelated data is added.

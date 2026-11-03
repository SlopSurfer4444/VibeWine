# VSync-zero benchmark findings, 3 November 2026

Three ordered repeats of all six scenes completed, 500 measured frames each,
1280x720, SyncInterval0, one-second warmup, five-second duration ceiling.
All 18 cases passed original structural, loss, module-binding, completion-probe
and CPU/GPU observation checks. Original raw traces remain retained under the
benchmark result retention contract. This is a descriptive source investigation,
not an A/B improvement or full native presentation acceptance.

## First investigation target

Drawable availability and the frame-latency backpressure it propagates take
priority over speculative shader rewriting. On the cube, three traces contain
500 actual Present API records each with SyncInterval0 and 1,002 successful
completed GPU chunks each.

| Cube observation | Repeat 1 | Repeat 2 | Repeat 3 |
| --- | ---: | ---: | ---: |
| Guest elapsed seconds, 500 frames | 4.131 | 4.134 | 4.136 |
| nextDrawable mean wall time, ms | 7.948 | 7.993 | 8.010 |
| PresentBoundary fence wait mean, ms | 8.096 | 8.133 | 8.039 |
| Individual GPU-buffer median, ms | 0.156 | 0.226 | 0.130 |

Type98 directly wraps `layer_.nextDrawable()` in `src/dxmt/dxmt_presenter.cpp`.
Type96 directly wraps the frame-latency fence wait in
`src/dxmt/dxmt_command_queue.hpp`. These observations run on different pipeline
threads; their similar means support a backpressure hypothesis, not proven
causation, and must not be added together. Short individual GPU buffers do not
establish full-frame GPU time or GPU utilization. CPU medians shift substantially
with the distribution of waits despite nearly unchanged elapsed throughput.

The source requests `display_sync_enabled=false`. PresentationDuration defaults
to zero for SyncInterval0 unless a preferred maximum rate is set. Neither setting
guarantees an immediately available drawable. Native availability, bounded queue
depth, compositor scheduling and drawable release remain possible explanations.
The first concrete question is what releases drawable availability and how that
interacts with frame-latency fence signaling. Do not remove queue bounds or change
drawable counts until lifetime, pacing and failure semantics are checked.

## Other useful discriminators

All cases reached the 500-frame cap in approximately 4.1–4.5 seconds, rather than
the five-second ceiling. Median CPU loop time outside Present was 0.072–0.125 ms
for the cube/instanced/textured cases, 0.334–0.407 ms for drawstress, and
0.905–0.970 ms for dynamicupload. The last scene generates 36,864 vertices with
trigonometric functions on CPU, so this does not isolate Wine upload overhead.
Pixelheavy's individual GPU-buffer median varied 2.683–6.170 ms across repeats;
it remains useful for GPU work but is not a stable universal shader bottleneck.

The machine was on battery throughout. Cache was reused without purging; scene
order was forward, rotated and reversed. Power/background conditions were not
experimentally controlled. Warmup, trace serialization, synthetic workload,
capture envelope tails and observer cost constrain comparisons. Physical display
FPS, DS3 gains and an avoidable eight-millisecond delay remain unproven.

## Delivery

The runner now accepts a bounded `--frames` option (1..10000, default10000),
passes it to the existing guest, and records suite settings. Its original live
record-budget and refusal checks remain. The installed app defaults and game
runtime are unchanged. Host tests pass; no renderer optimization is activated
by this diagnostic change.

## Native presentation control

A subsequent native-only fixture completed four cases with 30 warmup and 180
measured frames each. It uses clear-only GPU work and waits for each command
buffer after presenting, without Wine, the DXMT queue or feedback handlers.
Layer settings were read back before and after each case. Compilation passed
with `-O2 -Wall -Wextra -Werror`; execution used network denial, a 25-second
own-process watchdog and a separate bounded host supervisor.

| Native configuration | nextDrawable mean, ms | waitUntilCompleted mean, ms | GPU-buffer median, ms |
| --- | ---: | ---: | ---: |
| Sync off, two drawables | 7.251 | 0.581 | 0.088 |
| Sync off, three drawables | 0.462 | 16.058 | 0.089 |
| Sync on, two drawables | 15.769 | 0.776 | 0.089 |
| Sync on, three drawables | 15.595 | 0.929 | 0.179 |

Native presentation can therefore involve substantial waiting without Wine.
Reducing acquisition time alone can displace blocking into command completion;
the three-drawable case is not evidence of a speedup. This is a single fixed-order
control with serialized submissions, not a renderer A/B test. The first case's
occlusion snapshot changed from false to true; visibility during all intermediate
frames is unknown. Configured display refresh and screen maximum both reported
60 Hz, so the earlier possible 120 Hz compositor explanation is not established.

A source ownership audit found no concrete drawable retention defect: a fresh
autorelease pool surrounds each encoded/committed chunk; borrowed drawable and
texture wrappers are not retained across frames; feedback blocks retain their
ticket/session rather than the drawable; command buffers retire after verified
completion. This is a scoped source review, not proof of every native lifetime.
No renderer patch or runtime setting change is justified by this control alone.
The remaining question is how the separate scene/presentation submissions and
completion frontier affect useful overlap under a GPU-heavy workload.

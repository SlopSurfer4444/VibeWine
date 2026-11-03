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

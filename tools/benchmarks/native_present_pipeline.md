# Native present pipeline preservation experiment

This original, native-only diagnostic compares one coupled CPU encoding worker
with separate render producer and present consumer CPU workers. It is source for
a bounded experiment, not a DXMT integration or a physical scanout measurement.
No existing DXMT, Wine, display, VPN, or service configuration is changed.

## Fixed comparison

Eight cases run in this fixed order: light coupled, light decoupled, light
decoupled, light coupled, then the same four cases with the heavy workload.
Every case has 90 warmup frames, fully drained before 90 measured frames begin.
Light uses 1 integer recurrence iteration per pixel; heavy uses a fixed 4096.
Frame identities are unique within each case (warmup 1–90, measured 91–180),
and reset for the next case. A fresh event per phase makes reuse safe; paired
C/D cases therefore have identical expected image content. No frame dropping,
coalescing, retry, or extra present request is allowed.

All cases reuse the same two GPU queues, one shared private BGRA8 render target,
three private BGRA8 snapshot textures, and three shared CPU readback buffers.
The drawable is 1280x720 BGRA8Unorm. Maximum drawable count is 3, display sync is
disabled, framebuffer-only is disabled, transactions are disabled, and drawable
timeouts are enabled. Actual layer settings and window visibility/occlusion are
read back on the AppKit thread before and after each case. Initial window
visibility may settle for up to two seconds before any case starts; no failed
benchmark case is retried. These are evidence at
two instants, not continuous proof of visibility or presentation.

Both modes execute the identical GPU sequence for every frame:

1. Reserve the exact modulo-three slot before any write to its snapshot.
2. Producer queue computes into the single shared render target, then blits the
   result into the reserved snapshot texture. Producer queue ordering protects
   the shared render target from the following frame's write.
3. Signal a fresh-per-phase `MTLSharedEvent` at `frame_identity + 1` after that
   snapshot copy; only the producer queue signals it, in strictly increasing
   order. Publish the frame to a bounded capacity-three SPSC queue.
4. Consumer acquires a drawable without holding the queue/slot mutex, then its
   GPU command buffer waits for the event value, copies the snapshot to the
   drawable, and copies that drawable to the slot's shared readback buffer.
5. Issue exactly one `presentDrawable:` request and commit. The command buffer
   completion handler checks status, GPU timestamps, the exact completion/slot
   identity and the entire image before releasing that slot's lease.

The coupled worker completes render encoding, drawable acquisition and present
encoding for frame N before encoding frame N+1. It still submits GPU work
asynchronously and permits three reserved slots; it never waits per frame for
GPU completion. In decoupled mode the render worker can encode N+1 while the
consumer waits for drawable N. Both modes use the same queue, slot, verification,
autorelease-pool and callback code paths. Each phase creates one or two CPU
workers; thread creation/join is included in that phase's elapsed measurement.

The extra shared-render-target-to-snapshot copy is intentional: this experiment
includes the preservation cost of taking a snapshot before a subsequent frame
overwrites the shared target. It does not assume free backbuffer renaming.
This still establishes no DXMT application compatibility or integration benefit.
Native x86_64 execution on Apple silicon may use translation and remains a native
Metal API experiment rather than a Wine/Windows present measurement.

## Pixel oracle and accounting

The original MSL kernel uses wrapping unsigned 32-bit integer arithmetic. Its
frame-specific seed includes `(x mod 8, y mod 8)` and the frame identity. A repeated
8x8 tile controls RGB; alpha is 255. The CPU independently calculates the 64
expected tile pixels using the same specified integer recurrence, expands eight
rows, and compares all 720 readback rows, including every one of the 921600
pixels. Shader float conversion uses exact target byte values divided by 255;
any unexpected BGRA8 quantization is an oracle failure, not a tolerance.

Requests must have sequential identities. Completion handlers may run out of
order: each completion checks its own expected identity, unique sample record,
and exact slot owner; no callback serialization is assumed. A slot is reusable
only after its own present completion and verification. A successful phase
requires all offered, render-committed, render-completed, ordered present,
present-completed and full-image-verified counts to equal its requested count,
with no queued or reserved slot left over. Readback proves the image submitted
for each present request, not an image physically scanned out by the display.

`--corrupt-frame` is the sole optional argument. It flips exactly one bit in the
first measured frame's completed readback in the first case, before verification.
The ordinary oracle must reject it as `golden_image_mismatch`, stop further
submission, drain committed work and return exit status 1. It never converts the
expected failure into a successful benchmark. Other arguments fail immediately.

## Bounds, cancellation and lifetime

Queue/slot waits have a three-second deadline and poll the window-close flag.
Cancellation wakes CPU waiters. Both CPU threads join, then every committed
render and present callback is drained with a three-second bound, including
failure paths. The final counter update occurs under the mutex as the callback's
last access to phase state. Phase state and GPU resources remain alive until all
such accesses have ended. No mutex is held across `nextDrawable`, GPU encoding,
commit, or full-image verification.

If a callback drain cannot finish, the program emits a failure and immediately
exits without destructing the referenced context or resources. It never tears
down state that an outstanding callback can access. An independent own-process
45-second watchdog covers a stuck drawable acquisition, GPU event dependency,
thread join, or AppKit call and exits 124. Run it under an additional 60-second
external process bound. No other process is signaled by the program.

Only stdout JSON is emitted (under 64 KiB), with no raw images, output files or
network activity. Successful cases contain warmup and measured counters and
separate distributions for render encoding wall/thread CPU, drawable acquisition
wall/thread CPU, present encoding wall/thread CPU, render/present GPU command
buffer duration and full-image verification wall time. Total measured elapsed
begins after warmup fully drains and ends after all measured callbacks drain.
GPU durations are per-command-buffer intervals; their sum is not a GPU critical
path or physical frame-rate metric. Verification and readback overhead is the
same in both modes and deliberately remains in the measured elapsed interval.

## Build and verification ownership

The integration owner reviews the source, compiles, runs bounded native
positive/negative controls, and interprets the results.
Example build command from the repository root (output directory must exist):

```sh
clang++ -arch x86_64 -fno-objc-arc -fblocks -std=c++17 -O2 -Wall -Wextra -Werror \
  tools/benchmarks/native_present_pipeline.mm -o work/native_present_pipeline \
  -framework AppKit -framework Metal -framework QuartzCore -framework CoreGraphics
```

Expected positive evidence is exit 0, `status=complete`, all eight cases, 90
verified warmups and exactly 90 ordered requests and verified images per measured
case, matching before/after settings. Expected negative evidence is exit 1 and
`golden_image_mismatch` with one corrupted frame reported. Preserve failures and
the original fixed settings before any workload calibration. This file and source
alone do not claim that compilation, native execution or performance acceptance
has happened.

## Recorded result, 3–4 November 2026

The initial 1024-step/15-warmup configuration completed every image and request
but remained limited near 0.75 seconds for 90 measured requests in both modes.
A 4096-step/15-warmup follow-up showed a slower first coupled heavy case; its
last coupled case matched the decoupled cases. This was not admitted as a gain.
Both original configurations and their results were retained.

The final fixed 4096-step/90-warmup configuration was built as x86_64 with
`-O2 -Wall -Wextra -Werror` and run under network denial with a separate 55-second
host bound. All eight cases completed 90 measured requests, 90 render and present
completions and 90 full verified images each; no slot bound exceeded three.
Its one-bit negative control exited 1 with `golden_image_mismatch` after draining
committed work. This validates this test's oracle and execution, not all possible
driver failures or an integration into Wine.

| Final case | Coupled elapsed, ms | Decoupled elapsed, ms |
| --- | --- | --- |
| Light, two repeats each | 749.169 / 750.725 | 733.552 / 750.493 |
| Heavy, two repeats each | 1572.388 / 1568.062 | 1635.754 / 1634.094 |

The heavy decoupled mean was approximately 4.12% higher. There is no demonstrated
benefit from promoting this specific CPU-worker split; the prototype stays a
diagnostic experiment, with no game/runtime activation. A single machine and
fixed ABBA order do not establish a universal regression percentage. Both modes
already use two GPU queues, so this result also does not compare one versus two
GPU queues or rule out every other presentation architecture.

Retention owner: benchmark integration owner. Exact redirected-output, executable
and build-receipt scope is the task-local `work/present-pipeline-20261103/` folder,
bounded to 4 MiB with a 30-day TTL. Cleanup is manual after exact path/identity,
hash, reference and inactivity checks; unresolved, pinned or required evidence
stays held. Reproducible executables may be retired after source and result
preservation. The fixture installs no background producer or automatic cleaner.

## API reference and provenance

The AppKit window/layer and own-process watchdog shell follows the repository's
original `native_drawable_control.m` diagnostic. The compute shader, CPU oracle,
queueing and preservation experiment are original code. No external implementation
or sample source was imported. Apple's public documentation describes event
synchronization across GPU queues on one device:
[Synchronizing events within a single device](https://developer.apple.com/documentation/metal/synchronizing-events-within-a-single-device).
The existing WMT event/command-buffer surface in `Metal.hpp` supplies the relevant
shared-event and GPU wait/signal concepts for a possible later integration;
this diagnostic does not modify or validate that integration.

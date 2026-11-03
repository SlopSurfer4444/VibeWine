# Native drawable control

Self-written, bounded macOS diagnostic; no renderer changes, shaders, third-party
code, file writes or network requests. Requires macOS 12+ and a logged-in graphical
session. It creates one own 1280×720-point client window and explicitly requests
a 1280×720-pixel drawable. It closes that window on ordinary completion.

Compile from the repository root into task-local scratch:

```sh
mkdir -p work/native-drawable-control
xcrun clang -fno-objc-arc -fblocks -mmacosx-version-min=12.0 \
  -framework AppKit -framework Metal -framework QuartzCore -framework CoreGraphics \
  tools/benchmarks/native_drawable_control.m \
  -o work/native-drawable-control/native_drawable_control
work/native-drawable-control/native_drawable_control
```

Run once; no autonomous retry or background installation. Its own 25-second
watchdog emits a small failure JSON and exits 124 if AppKit/GPU calls stall.
Ordinary failure exits 1; four completed cases exit 0. Each case requests sync
off/on × drawable count two/three, checks main-thread layer readback, and performs
30 warmup plus 180 measured frames. Feedback handlers are deliberately absent.
Each frame has its own autorelease pool and clears a Metal render target, queues
immediate presentation, commits, then waits for command completion. Nil drawable,
command failure, setting mismatch or invalid timestamps fail without fallback.

Stdout is one JSON document below 64 KiB. It reports nextDrawable wall time and
worker-thread CPU time, whole frame worker-thread CPU time, and Metal command
GPU duration and waitUntilCompleted wall time independently (mean, median, p95,
milliseconds). Frame CPU excludes
the following status validation, GPU timestamp sampling and autorelease-pool
drain. It is sampled immediately after command completion. There are no
displayed/physical FPS claims.
The per-frame wait explicitly serializes GPU submission and is a limitation of
this minimal control, not a reproduction of the Wine renderer's queue behavior.
Visibility, occlusion, key window, application activity, settings and refresh
metadata are sampled on the main thread before and after each case; intermediate
visibility changes are not continuously monitored. Configured display refresh
comes from the current CGDisplayMode, whose zero value is reported as unavailable;
NSScreen's maximumFramesPerSecond is separate metadata, not measured cadence.

The finite drawable pool can make nextDrawable wait even with display sync off.
Compare this control with the renderer as descriptive attribution evidence only:
its clear-only GPU work and serialized submissions differ from a cube workload.
No nextDrawable timing result by itself proves physical scanout or renderer blame.

API basis: Apple's [CAMetalLayer.nextDrawable documentation](https://developer.apple.com/documentation/quartzcore/cametallayer/nextdrawable())
describes drawable-pool availability waits. This fixture is original code using
public APIs, not a copied Apple or external sample. Build and execution are
integration-owner checks; adding this source does not claim live acceptance.

Retention: the executable is task-owned scratch and may be retired after the
integration owner verifies no run is active. The fixture itself writes no files.
If the owner redirects stdout, retain that small JSON with the experiment's
evidence rather than starting a new capture/cleanup service.
The integration-owner experiment scope has a 1 MiB cap and 30-day TTL; preserve
active, unresolved or pinned evidence. Retire an inactive, hash-matching
reproducible executable after source/result preservation; no recursive cleanup.

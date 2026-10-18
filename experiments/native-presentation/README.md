# Native Metal presentation and visibility experiment

A small native-only macOS control experiment: one ordinary NSWindow owns a
CAMetalLayer, clearing changing colors through real Metal command buffers.
It distinguishes native presentation timestamps from GPU completion and CPU
callback receipt. No Wine, game, fullscreen, remote-control settings, input
injection or other-application control is involved. Local experimental tooling
is MIT licensed; no third-party implementation was copied.

## Build and run

Requires macOS, Xcode Command Line Tools, Metal, and existing Rosetta support to
execute the x86_64 build on Apple Silicon. Both architectures use the same source.

```sh
python3 -B build.py
python3 -B run.py --arch arm64
python3 -B run.py --arch x86_64
```

Build compiles with warnings as errors, signs and verifies disposable `.app`
bundles, checks architecture, and runs offline self-tests. Tests do not create an
NSApplication or Metal device. The default runner validates hashes and launches
nothing. Generated builds, manifests, checks and session evidence are ignored.

An explicit GUI trial is:

```sh
python3 -B run.py --arch x86_64 --run
# Or wait for manual confirmation of native visibility before starting phases:
python3 -B run.py --arch x86_64 --wait-for-start --run
```

The runner emits its retained child PID, app and new session directory. A normal
main runloop settles for at least 0.5 seconds before PREFLIGHT_SETTLED. In manual
mode, the title becomes **Native Metal READY - press Start**, and the real
**Start presentation phases** button enables (AX identifier
`native-present-start`). READY means input-ready; verify actual window/Space
visibility before clicking. This experiment contains no automatic input sender.
No click within 30 seconds ends with failure exit 70.

## Phases and records

After automatic or manual Start, the phase clock runs for approximately 12 seconds:

1. Visible, 0–4 seconds.
2. Only its own window is ordered out, 4–8 seconds.
3. Its own window is ordered visible again, 8–12 seconds.

An order request is not evidence that the window entered the active Space.
Transition snapshots and settled snapshots around 2/6/10 seconds record the
actual own window/view/layer relationship, native window ID, visibility,
miniaturization, active Space, occlusion bits, key/main/app activation, screen,
view bounds, layer hidden/opacity/scale/drawable dimensions, and own CG all/onscreen
window lists. Missing optional values remain null. Coordinate systems are not
silently equated.

Each render attempt follows nextDrawable → encode clear → addPresentedHandler →
commandBuffer.presentDrawable → commit. The presented callback records the frame
and registered/callback drawable IDs, raw IEEE754 presentedTime bits, finite
seconds and validity. Callback receipt is separately labeled CPU monotonic-raw
nanoseconds. It is never a fallback presentation clock. GPU-completion callbacks
record command-buffer status and native GPU start/end/duration in seconds.

Only finite positive native presentation timestamps qualify. A callback count is
not a displayed-frame count. Apple documents zero as not presented or dropped;
zero alone does not identify the cause or count GPU drops. Submission phase may
differ from visibility when a callback executes. Correlate receipt timestamps,
exclude transition margins and unresolved boundary callbacks.

## Bounds and acceptance

The experiment allows at most 160 frame attempts and 2048 event records plus a
summary. Overflow is explicit. nextDrawable may block the main thread, so the
12-second phase schedule is a soft deadline. The supervisor waits at most
25 seconds normally or 55 seconds in manual-start mode, then kills only its
retained child and allows a 2-second reap. Sandbox rules deny networking and
writes outside the new session, except `/dev/null`. Process exit closes its own
window; nothing is installed or registered.

Only normal finish flushes the bounded in-memory records. A timeout, missing or
truncated log is inconclusive. Shutdown waits up to 2 seconds for GPU callbacks;
native presentation callbacks are not fully drained, and the summary explicitly
marks this. Missing callbacks cannot be treated as zero timestamps.

Exit 0 requires phases started, all settled phase bits and no prior failure.
The runner also checks completion separately. Setup/encode errors and an
unstarted or incomplete trial fail. Exit status is preserved with a shared final
flush/explicit process-exit path: AppKit `terminate:` would otherwise exit 0 and
bypass `main`'s intended failure code. Offline actual-binary tests exercise this
shared flush/exit path for no-start exit 70 and complete-state exit 0 on both
architectures; these are not live presentation tests.

Presentation acceptance still requires inspection of actual phase visibility,
valid native times, no overflow and successful GPU statuses. Do not infer FPS
improvement, GPU utilization, complete-frame GPU cost or display latency.

## Observed diagnostic result

One controlled x86_64/Rosetta run produced:

| Submission phase | Callbacks | Valid positive timestamps | Settled native observation |
| --- | ---: | ---: | --- |
| Initial visible | 41 | 40 | Visible, active Space, active app, onscreen window; occlusion bits 8194 |
| Ordered out | 40 | 2 | Not visible, onscreen list empty; occlusion bits 8192 |
| Ordered visible again | 40 | 0 | Visible flag true, **off active Space**, inactive app, onscreen list empty; occlusion bits 8192 |

This demonstrates that x86_64/Rosetta can return valid presentedTime values in
this environment. It does not support a blanket architecture failure. The last
phase shows an order-visible request did not restore onscreen/active-Space
placement. Transition callbacks explain why submission-phase totals are not pure
visibility-state counts; no frame-rate or causality claim follows. Different
trials and game runs must not be merged into a single visibility/presentation
causal observation.

A separate earlier manual trial never started its phases. Its child incorrectly
exited 0 because AppKit terminated the process before `main` returned its failure
code. That evidence is preserved privately; the explicit flush/exit and runner
completion checks above repair failure reporting. No additional live GUI run is
claimed for this source revision.

The implementation was independently reviewed and built/self-tested on both
architectures. Live acceptance is limited to the observed historical run above;
future machines, GUI sessions and source revisions require fresh evidence.

## References and reuse

Local native-window/feedback fixtures supplied the lifecycle and observation
pattern. Existing mock Metal tests cannot establish actual onscreen presentation.
Flutter's custom drawable returns zero and does not implement these callbacks;
large custom graphics backends were unsuitable as this independent baseline.
The direct sequence follows Apple's public API documentation:

- [CAMetalLayer](https://developer.apple.com/documentation/quartzcore/cametallayer)
- [presentedTime](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime)
- [nextDrawable and timeout](https://developer.apple.com/documentation/quartzcore/cametallayer/1478172-nextdrawable)

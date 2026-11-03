# Finite D3D11 benchmark suite

This suite runs six procedural scenes through an explicitly selected Wine/DXMT
runtime without Steam, game menus, saves or game input. It reuses local DX11
scene foundations and the maintained native trace controller/analyzers. Source
implementation and CPU fixtures do not establish installed or live behavior.

## Local app installation

`install.py` packages the guest, collector, analyzer closure, source/license
material and one-click `VibeWine Bench.app`. It requires an already reviewed
isolated runtime wrapper and private prefix; it does not download or install
Wine, accept an arbitrary game prefix, or execute a benchmark during install.
Use `python3 tools/benchmarks/install.py --help` for explicit input paths. The
default operation is a plan. `--apply --runtime-quiescent` is for the owner
after independently settling that exact prefix. Existing unknown targets or
changed installed bytes are refused.

The installed app selects all six scenes, 1280x720, vsync 1, one second warmup
and up to ten seconds measurement per scene, with `presentation-waits` capture.
It opens an HTML report after automatic exit. The low-level runner defaults
below are intentionally different and are always recorded. Double-launches
are locked; an unsuccessful or uncertain cleanup retains a hold for owner
reconciliation before another launch. No background benchmark or cleaner is
installed. The app is a local benchmark integration, not a portable Wine
distribution or a replacement for representative game compatibility tests.

The integration owner supplies a reviewed matched Wine/DXMT runtime, disposable
prefix, private HOME/TMP/cache, network/write isolation and its required loaded
module paths. The runner has no installed-runtime fallback and does not call
`games-play`, `ds3-runtime` or a global wineserver shutdown. A prefix argument is
not itself an isolation boundary; do not pass the game prefix.

Build the guest through `build.py`, then run with absolute paths (replace each
placeholder with reviewed actual paths):

```sh
python3 tools/benchmarks/run.py \
  --wine /absolute/isolated/engine/bin/wine \
  --prefix /absolute/private/prefix \
  --guest /absolute/build/dx11-benchmark.exe \
  --trace-controller /absolute/matched/cs2trace \
  --module /absolute/engine/lib/wine/x86_64-windows/d3d11.dll \
  --module /absolute/engine/lib/wine/x86_64-windows/dxgi.dll \
  --module /absolute/engine/lib/wine/x86_64-windows/winemetal.dll \
  --module /absolute/engine/lib/wine/x86_64-unix/winemetal.so \
  --env WINEDLLOVERRIDES=d3d11,dxgi,winemetal=n,b \
  --output /absolute/new/run-directory --duration 2 --warmup .5 --vsync 0 \
  --profile presentation-waits
```

`--module` requires four to six distinct paths, pinned before launch and checked again after mapping: each supplied file must be actually mapped by the
exact native producer incarnation and its bytes are hashed. Parent admission
should include the PE and native components it requires. The guest's module
list remains separately visible in `guestReady`; declarations alone are not
accepted as mapped-module proof. Use `--env NAME=VALUE` for reviewed runtime
settings. Inherited host environment remains an owner responsibility.

The default `--profile presentation-waits` retains CPU Present, native feedback,
GPU command-buffer and queue/wait observations while excluding per-Map and
compiler/scheduler detail. This avoids the repeated Map diagnostic storm in
drawstress. The explicit `--profile combined-bounded --duration .1` route is a
short diagnostic capture, not a long-run default. Lifetime capacity and loss
gates remain unchanged; even a requested duration is not a guarantee that it
fits the event budget. A budget failure preserves evidence, stops the suite,
and never selects a lighter profile or retries automatically.

Use a short absolute result root. Before creating Wine, the runner checks the
guest's 220-byte output option limit and the nested Win32 trace-file path budget,
including ten decimal PID digits. A path that fits the guest option can still
make the trace leaf exceed MAX_PATH; such a run is refused before measurement.

Default scenes: `cube`, `drawstress`, `instanced`, `dynamicupload`, `pixelheavy`,
`textured`. `--scenes` selects up to six unique names; unknown scene failure is
fatal. One process per scene gives each producer a fresh 131072-slot trace.
Duration is bounded to 0.1..10 seconds and measured frames to 10000; the guest
stops at the first limit, so actual elapsed duration must remain visible. Use
an explicit `--frames 500 --duration 5 --vsync 0` for a capped investigation;
`--frames` accepts 1..10000 and defaults to 10000. Suite settings record both
limits. A frame cap does not guarantee fitting the trace budget; existing
live capacity checks and refusal remain required. Compare actual frame counts
and durations, and never describe a shortened frame-capped run as five seconds.
Vsync is explicit: zero requests unsynchronized submission but can still wait
for drawable availability and queue admission; one also includes
presentation pacing. Compare identical settings and actual durations.

## Evidence and launch order

The runner creates a native-controller session, checks actual executable bytes
and obtains a real native clock receipt before guest creation. Its
`observer-ready.json` explicitly says the future producer is not yet attached;
this readiness proves collector availability, not recording.

After ordinary guest startup/warmup, `ready.json` supplies run UUID, Windows PID,
scene and actual device/module data. The runner independently identifies the
native process mapping its exact trace file, requires that process to belong to
the newly launched process group, records native PID/birth and hashes required
mapped modules. A fresh idle trace is armed once at generation 1. Both probe and measured
decodes must show exactly the requested profile and explicit metadata. The
selection is recorded before launch and in run/suite receipts. Offline analysis
uses that recorded selection; it cannot silently override it. For historical
receipts predating the declaration, pass `analyze.py --profile combined-bounded`
explicitly. Missing, legacy or mismatching profile metadata fails admission.

Only then does the runner atomically publish `probe.json`; the guest makes three
calibration Presents, completes a bounded GPU EVENT query and a staging
readback of the checked integer sentinel, then publishes `probe-done.json`
and waits again. The host requires exact boolean `gpuEventComplete:true`,
`cpuReadbackComplete:true` and integer `readbackValue:1685613940` before normal
probe STOP. Missing, false, truthy substitutes or a wrong/type-changed sentinel
refuse admission and trigger bounded cleanup. GPU EVENT completion alone does
not establish CPU trace publication; the checked staging Map supplies the
source-bound CPU-coherent completion boundary outside measured timing. The runner
stops generation 1 and decodes it while the guest waits: structurally complete,
nonempty actual Present API records are required before measurement. Metadata
and reservation counts alone cannot release the barrier. A failed probe retains
its complete computed `probeAnalysis` in `run.json` before admission is refused;
the error names the failing gates and actual rejected-record families. No
measured generation is armed after a probe refusal. The probe remains
`probe.ndjson`; native clock observations bracket both phases. The runner then
requires at least 120000 remaining lifetime slots and arms fresh generation 2
without resetting the producer, before publishing `go.json`. Probe/go messages
bind schema, UUID and Windows PID. Measured STOP/decode also uses exact generation
2. Late native callbacks and boundary-rejected counters remain visible.

The runner monitors trace capacity, child output and deadlines. Natural exit
zero plus matching complete guest result and CSV are required. STOP verifies
exact identity/generation and admitted-writer quiescence, then decode uses that
same binding. Overflow, empty decoded events, invalid profile, inconsistent
summary, failed HRESULT/guest status, timeout or uncertain cleanup fails the
run; the suite stops at its first failed scene. Timeout termination targets only
the session/process group created by this launch. Detached private-server/GPU
work remains the integration owner's reconciliation responsibility.

A successful suite produces `suite.json`, plus per-scene `run.json`, original
binary trace, decoded `trace.ndjson`, `perframe.csv`, guest results and bounded
logs. Analyze again with `python3 tools/benchmarks/analyze.py /absolute/scene-dir`.

## Meaning of metrics

`--present-every N` is an explicitly different diagnostic workload, bounded to
1..64 and defaulting to 1. Each iteration emits the same scene rendering; for
N>1, intermediate iterations Flush and only every Nth iteration calls Present.
Three calibration Presents remain mandatory regardless of N. New guests declare
`presentEvery` in readiness/results and `measuredPresentCalls` in results; the
host checks both against the request, records the request before launch, and
refuses mismatches. N=1 omits the new guest argument for compatibility with old
installed guests. CPU CSV columns remain unchanged, with presentCallMs=0 on
iterations without Present. Completed prefixes can end before the next Present.
Repeated renders may be overwritten before display, native chunk grouping and
render-pass behavior can change, and Flush is not proof of completion. Neither
render iteration rate nor the difference between N1 and N16 is displayed FPS
or a game speedup. Use it to investigate presentation coupling, not to bypass
presentation in games. Installed app defaults remain unchanged.

For a separate native-only drawable availability control, see
[`native_drawable_control.md`](native_drawable_control.md). It does not run Wine
and is not a matched renderer performance comparison.
The separate [`native_present_pipeline.md`](native_present_pipeline.md) compares
coupled and split CPU encoding workers with every present request and complete
image verification retained. Its tested split is not enabled in the renderer.

CPU `cpuFrameMs` is guest loop entry through the return from Present;
`presentCallMs` is its Present-call subset. Their mean/median/p95/p99/max describe
CPU wall time, including waits. They are not GPU execution time or displayed
FPS. Startup-to-ready is reported separately and includes requested warmup; no
cold-driver-cache claim is made, and no shader caches are purged.

Native presented cadence is recomputed by the maintained telemetry analyzer
from actual feedback triplets, unchanged dimensions/revision and consecutive
frame identities. Missing/invalid feedback stays inconclusive. The native measured scope
is the generation-2 envelope after calibration, including setup/shutdown tails, and must not be
relabeled the exact guest CPU window. It is drawable feedback, not physical
scanout. Full presentation coverage gates remain visible even when an observed
partial cadence exists. No zeros are invented for missing families.

For `presentation-waits`, the adapter's separate `observationUsable` means only
structurally valid, policy-valid observations of actual CPU Present calls and
completed GPU command buffers, with no decoder losses, rejected records or
observed contradictions. The maintained `producerProfile.capture_usable` and
strict native `presentationCoverage.capture_usable` remain unchanged; either
can still be false for missing presented times or successful pending-handler
CLOSE tails. Per-family native coverage is never promoted by observation
admission. Combined diagnostic captures retain their existing strict profile
gate. The maintained presentation report now exposes `observed_contradictions`
for both supported profiles; its historical combined-only `coverage` field and
all full native capture gates remain unchanged.

For improvement comparisons, run unchanged A/A repeats first, retain every
attempt and compare identical source/module hashes, scene, geometry, vsync,
cache treatment, thermal/power/background state and observer overhead. Current
suite output is per-run descriptive evidence; it does not implement a causal
A/B verdict or establish DS3/game compatibility/FPS. The existing strict
stationary fixed-window/repeatability analyzers remain available for separately
admitted exact presentation windows; these animated workloads are not silently
admitted as stationary gameplay.

## Retention and bounds

Owner: benchmark integration owner; consumer: source/build/installation reviewer.
Exact raw scope: one newly created suite output directory with at most six scene
subdirectories and their exact trace sessions. Each original trace is 8392704
bytes; measured decoded output is capped at 24 MiB and probe decode at 1 MiB, CSV at 4 MiB, guest stdout/stderr at
64 KiB each. Probe admission uses 110000 records plus 21072 reserve; measured admission uses
100000 plus 20000 reserve and both stop at sampled lifetime count 110000;
this is not a hard producer quota, while protocol capacity remains 131072.
Outputs over caps fail and preserve evidence. Raw aggregate allowance is 224 MiB
per suite including derivatives, TTL seven days. Curated run/suite reports share
4 MiB per suite and TTL 30 days. Source is durable repository content. Parent
must reserve this space and bound the number of suites before launch.

No background producer/TTL cleaner is installed. Cleanup is manual by the owner
only after exact ownership/reference/identity checks, no active writers, and
required preservation. Active work, pending analysis, unique failure evidence,
recovery and explicit holds exclude retirement. Skip symlinks, unknown/changing
objects and unrelated runtime/prefix/game data. Detached processes must be
reconciled before deleting their private prefix. This contract grants no cleanup
of installed games or existing experiments.

## Offline checks

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/benchmarks -p 'test_*.py' -v
```

Fixtures exercise real bounded Python child output/timeout cleanup, protocol
identity/replay refusal, atomic publication, ordering before failed launch,
collector drift, missing/invalid CPU data and empty trace rejection. They launch
no Wine, GPU, game, native collector or external code. Live acceptance belongs
to the integration owner and must include actual source/runtime module binding,
nonempty decoded events, strict guest failure handling and autonomous exit.

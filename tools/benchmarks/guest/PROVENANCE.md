# Finite D3D11 guest provenance and contract

Owner: `tools/benchmarks` host runner. This guest is a private, finite test application.
Its window/device/buffer setup pattern and exact cube vertex/index topology derive
from the checked-in DXMT `components/dxmt/tests/dx11/dx11_cube.cpp`; the supplied
`shader_cube.hlsl` informed the vertex-position/color convention. The root DXMT
license is LGPL-2.1-or-later, copyright 2023-2026 Feifan He for CodeWeavers. This
derivative uses the same license; retain that notice and DXMT LICENSE/COPYING.LIB
when transferring source/binary. All additional scene shaders and procedural
texture/geometry here are newly written for this guest. No AIO source, Dolphin
assets, model files, images, fonts, Vulkan shell, external shaders, or executable
code are incorporated. The build receipt hashes the inspected foundation files
and actual guest source. Public-source license checks are not a broader legal
opinion about any dependency or installed runtime.

Six workload families: cube (one indexed draw), textured (generated checker SRV),
instanced (1024 indexed instances), drawstress (1024 separate constant updates and
draws), dynamicupload (432 KiB vertex write-discard plus CPU generation per frame),
pixelheavy (96 fixed fragment iterations over a procedural distance field).
These are diagnostic workloads, not a weighted game-performance score. No compute,
tessellation, geometry shader, scene assets or interactive input is required.

Client size defaults to 1920x1080, configurable within bounded limits. The window
is fixed-size and windowed, with one sample per pixel, RGBA8 backbuffer, D24S8 depth,
hardware D3D11 feature level 11.0 required, and explicit `--vsync 0|1` (default 0).
There is no fullscreen or HDR claim. Every HRESULT-returning resource setup, Map,
Present and device-removal check is handled without dialogs or silent fallback.
Void D3D11 draw/state methods are validated by Present or Flush/device status; rendering
correctness and successful compositor display still require external observation.

`--present-every N` accepts digits only, bounded to 1..64, default 1. Every
rendered iteration retains the identical scene commands and deterministic scene
index. Iterations where `(frameIndex + 1) % N == 0` perform checked Present;
all other iterations explicitly Flush the immediate context and check device
status. Flush submits work but does not wait for GPU completion. Warmup follows
the same cadence; the three barrier probe iterations always perform real Present
regardless of N, and their GPU EVENT plus verified CPU staging readback is
unchanged. The normal final ClearState/Flush teardown also submits a remainder.
This diagnostic separates render submission from every-iteration drawable pacing;
it does not establish GPU throughput or display FPS, including at default N=1.

Scene time is `(frameIndex modulo 3600)/60`, without randomness or input; warmup,
probe and measurement each restart the sequence. A duration run consumes a
throughput-dependent prefix. `--frames N` additionally caps the prefix, but duration
remains a hard measurement cap. Compare identical settings and record actual frame
counts; this is not a claim of bit-identical GPU output across drivers.

Protocol schema 1: after warmup, `ready.json` contains runId, Windows pid, scene,
actual validated dimensions, presentEvery, metricScope, featureLevel and loaded
module names/paths. Warmup need not reach a Present when N exceeds its iterations.
With `--barrier`, wait at most 30 seconds for `probe.json` matching exact schema,
runId and pid, render three calibration Presents, publish `probe-done.json`, then
wait at most 30 seconds for identically bound `go.json`. No input or menus are read.
The runner must establish and independently verify native trace binding/progress
before writing GO. Every successful rendered iteration writes `perframe.csv` with
`frame,cpuFrameMs,presentCallMs`; these are CPU wall-call durations, not GPU execution
or compositor display times. The legacy three-column CSV is unchanged: frame is
the rendered iteration index, cpuFrameMs includes full iteration work plus either
Present or Flush, and presentCallMs is exactly zero on non-Present iterations.
File serialization lies outside cpuFrameMs and inside
actual durationSeconds. `results.json` is authoritative guest status, identity,
counts and settings; frames counts rendered iterations, measuredPresentCalls counts
successful measured Present calls (excluding warmup and probe), and presentEvery
and metricScope explicitly identify this CPU rendered-iteration metric. A failed
iteration or CSV write can leave counts unequal; only complete results are valid
for comparisons. Success is published only after normal renderer teardown.

Guest heartbeat guard terminates only its own PID after 15 seconds without progress,
including blocked initialization/Present/teardown, with watchdog.json and exit 124.
Barriers service messages and heartbeat; their independent 30-second deadlines apply.
Host runner must enforce its own absolute deadline and interpret missing results,
watchdog status, or empty native trace as failure. Guest exit zero alone cannot
establish visible presentation, native trace completion, or FPS validity.

Retention: owner is host runner. Exact per-run scope comprises ready/probe-done/results
JSON, perframe CSV, phases JSONL, watchdog JSON and `.guest-owner` marker. Raw records
are bounded to one million frames and one minute measurement plus thirty seconds
warmup; perframe CSV hard bound is below 128 MiB with fixed numeric formatting.
Host per-run byte cap and retention policy in the benchmark README additionally
apply. Preserve current/held acceptance receipts and source; remove expendable guest
raw runs through the host's explicit dry-run retention workflow, never through this
guest. The guest deletes no earlier run or unrelated file and refuses reused artifacts.

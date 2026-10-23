# Independent performance research, 2026-10-23

This investigation was completed without consulting the pending ChatGPT Pro
answer. It examines hub `c02486ad06e96152f5800b62a98d320205844838`, Wine
`bee453b09460e0c3eb49b7c491a6d04e65cc38a5` and DXMT
`fab325e0adcd639d822dbfac87a1b840ccb2d4bf`. Findings below are not installed
patches. No game was launched, working runtime replaced, cache changed or
desktop focus moved. CPU fixtures and one small native MetalFX property query
were run. No FPS improvement or explanation of previous DS3 freezes is proven.

## Priority and evidence

| Priority | Finding | What is established | Expected scope |
| --- | --- | --- | --- |
| 1 | Shader persistence remains on the ready path; compiler-worker occupancy can delay runnable work | Source ordering plus an exact-source scheduler fixture | Shader/PSO compilation tails, especially new variants; warm GPU-bound FPS may be unchanged |
| 2 | GPU query results are published without synchronization with API reads | Three ThreadSanitizer race reports in the exact query classes | General timestamp/occlusion correctness; game symptoms are not established |
| 3 | Repeated native descriptor getters and separate encoder setup calls | Optimized assembly and a command-equivalence CPU fixture | CPU encoding overhead proportional to render-pass count |
| 4 | MSync registration acknowledgement can spin indefinitely | Current source and a coupled donor implementation | Synchronization CPU contention and timeout behavior; requires native paired tests |
| 5 | Native arm64 wineserver is feasible but needs guest/host architecture separation | Concrete donor implementation and mismatches in our source | Server CPU overhead only; game and x64 clients remain translated |

## Compiler readiness and persistence

In `components/dxmt/src/d3d11/d3d11_shader.cpp`, a cold shader calls
`update_cached_variant()` before creating its `MTLFunction` and before scheduler
ready publication. `CachedSM50Shader::update_cached_variant()` obtains the
singleton shader-cache writer lock. `src/winemetal/unix/cache.c` performs one
synchronous SQLite insertion per entry. Thus unrelated completions can wait for
one writer while consumers still consider their shaders unfinished.

The graphics archive's existing post-completion path is a different mechanism:
it publishes readiness first, but persistence still occupies a compiler worker.
The scheduler decrements `running` only after post-work returns. This archive
feature is executable-gated; the general shader database is not.

`src/dxmt/dxmt_tasks.hpp` grows workers only during external `submit()`, when
all currently counted workers are busy. A queued burst before workers enter can
therefore drain on two workers even if the allowed maximum is eight. Dependency
continuations do not trigger growth. The current dependency registration/ready
locking was reviewed; no new dependency lost-wakeup defect was reproduced.

An exact-source CPU fixture, substituting only platform thread/lock/priority
wrappers and fixing hardware concurrency at four, demonstrated:

- Eight tasks admitted before worker startup: two created workers, peak two.
  Each task slept 10 ms; the parent rerun drained in 50.06 ms. This is a
  scheduling demonstration, not a realistic compiler benchmark.
- Admission paced after workers became busy: eight created workers and eight
  simultaneous task entries. This does not establish an optimal worker count.
- Two completed tasks blocked in detached post-work: both were ready, but a
  third runnable task remained unexecuted after 20 ms with both workers occupied.

The useful experiment is compile-first scheduling and bounded optional
persistence, with owned immutable payloads and explicit shutdown/drain behavior.
Moving a write after `ready` alone can shift the stall to another queued task.
Do not simply maximize thread count or deepen the command ring.

Missing trace discriminators are runnable-queue delay, dependency-ready links,
worker count/occupancy, separate cache-lock/read/write durations and SQLite result
codes, and GPU-completion-to-readback/coherence duration. Existing compile totals
blend several of these. Measure them before choosing a concurrency policy.

## Query publication races

`src/dxmt/dxmt_occlusion_query.hpp` stores `TimestampQuery::cached_value_` and
`VisibilityResultQuery::{seq_id_issued,accumulated_value_}` as ordinary fields.
Readback destruction writes them on the finish thread. `GetData()` reads them
on the API thread. Its device mutex does not synchronize with the finish thread;
that thread calls `chunk.reset()` before publishing the CPU-coherence fence.
The query-read path does not acquire that fence first.

The exact two classes were extracted without algorithm changes. A single writer
issued each result once, while a reader polled `getValue()` under a separate
device mutex, retaining observable results. Both the investigator and integration
owner built with `clang++ -std=c++20 -O0 -g -fsanitize=thread -pthread` and ran
with TSAN exit code 66. Both observed three read/write race warnings matching
these fields. Source-extraction equality was checked against the pinned header.

This establishes a CPU publication defect, not the cause of a particular game
freeze. A repair must cover visibility accumulation, completion publication and
query reuse/ownership; changing one field to atomic is not a complete design.
Keep coherence publication after all required readback work.

## Render-pass marshalling

`src/winemetal/unix/winemetal_unix.c` creates a fresh render-pass descriptor and
sets eleven properties for each of eight color slots. Each expression repeats
the attachment-array and indexed getters. At `-O2`, the x86_64 assembly still
contains 33 Objective-C calls per loop iteration: 264 over eight slots.

Hoisting the array once and each attachment once preserves the 88 setters and
reduces that portion to 97 dispatches: **167 redundant getter sends per render
pass removed**. Baseline and prototype compiled for arm64 and x86_64; the parent
independently recompiled all four and counted the x86_64 loop. No timing or FPS
number follows from this call count. Descriptor caching and skipping unset slots
are separate changes with stale-resource/default-value risks.

Draw lists already batch. However, the ordinary render-pass prologue in
`src/dxmt/dxmt_context.cpp` makes five separate fixed buffer-binding calls before
its existing list. The existing node ABI can batch them, saving four bridge
crossings per basic pass while retaining fence order. Final Present has six
separate setup/draw commands that can become one list, saving five crossings
per frame. A CPU fixture using the actual `Metal.hpp` wrappers confirmed equal
normalized records, including null gamma texture and twelve metadata bytes,
for six wrappers versus one list. It used a stub interpreter, not the real GPU.

These are in-process Wine ABI transitions, not IPC and not full-frame copies.
Merging DLLs or rewriting the entire stack does not automatically eliminate
them. The larger opportunity is work repeated across many passes. Preserve
synchronous list consumption, stack-payload lifetimes, drawable dimensions,
configuration revisions and failure-path fence completion.

## Larger Wine candidates

Our MSync registration-ACK loop spins while its token remains pending, without
checking the wait deadline inside that loop. Dormison
[`c9966373`](https://github.com/kageroumado/dormison/commit/c9966373a268a1fdafe398634c19be653c674ed0)
bounds spinning, parks with `ulock_wait`, and adds the matching server wake.
Client and server changes are inseparable. Test acknowledgement-before-park,
park-before-ack, delayed pump, timeout and unregister ownership before promotion.
The donor's separate direct-object spinning defaults to zero; no blanket spin
enablement is proposed.

A native arm64 server has a real donor implementation:
[`3b55f4ea`](https://github.com/kageroumado/dormison/commit/3b55f4eacb90a2a524c06522351d48ddd45f427f),
[`90bd24fa`](https://github.com/kageroumado/dormison/commit/90bd24fa0a0f27a393d1172a58bf1049919ac77a),
[`bf1890d2`](https://github.com/kageroumado/dormison/commit/bf1890d281a26380416c36f0e6cd11cba4f07a3e).
Our `server/registry.c` currently derives guest architecture from compiler
architecture, and `server/mach.c` detects translation of the server itself.
A matching native server needs x64 guest reporting, per-client Rosetta detection,
preserved WoW64-prefix behavior, identical generated protocol and matching native
dependencies. Build from our source; do not substitute a donor binary.

Dormison's server-owned atomic WaitAll is a larger correctness project, not an
automatic speedup. Its
[`c957abf2`](https://github.com/kageroumado/dormison/commit/c957abf27f0136c44b0b8eef9450b6c1763714b5)
requires publication repair
[`305aec30`](https://github.com/kageroumado/dormison/commit/305aec30fef36878f18a355cfd4a7d0a376dde75).
It changes shared-state atomics, generations, registration messages and every
relevant mutation path. Keep it separate from the bounded ACK repair.

## Upscaling and rejected bug hypotheses

Spatial MetalFX already exists behind `DXMT_METALFX_SPATIAL_SWAPCHAIN`.
It multiplies game render dimensions by a configured factor, default two; it
does not automatically lower internal render resolution. Upscaling 1440p to
2880p before another compositor resize can spend extra work. A useful comparison
must keep final output size fixed and include native rendering, lower resolution
with ordinary scaling, and lower resolution with MetalFX.

The native bridge does not explicitly set input-content dimensions or color
mode. A source-extracted constructor probe on the local M5 created a 64×64 to
128×128 BGRA8 scaler. Defaults were full 64×64 content and perceptual mode;
required input/output usage was 1/5, compatible with the existing shader-read
plus render-target usage (5). Therefore omission of content dimensions and a
missing shader-write usage bit are **not demonstrated defects on this path**.
The sandbox had no Metal device; the bounded native property query succeeded
outside it. No texture was encoded or displayed by this probe.

Linear/HDR mode selection remains an integration contract to examine. Spatial
scaling is not temporal reconstruction or frame generation. Apple's
[MetalFX integration guidance](https://developer.apple.com/videos/play/wwdc2022/10103/)
requires additional motion/depth/history inputs for the temporal route; a generic
swapchain does not provide those automatically. Do not promise a universal
DLSS-like switch from the existing final color buffer.

## Next acceptance boundary

First implement and test the independent CPU correctness/performance candidates;
keep native server and atomic WaitAll in separate experiments. Add only the trace
discriminators needed to distinguish their effects. Then build a matched bundle
and compare a fixed, visible gameplay scene with equal resolution, settings,
cache state and background load. Track displayed frame intervals, CPU encoding,
compile/persistence waits and GPU intervals separately. Background CPU Present
counts and generated frames do not establish game-simulation FPS or latency.

An additional source-derived device-teardown concern remains unproven: pipeline
cache destruction may precede command-queue shutdown while callbacks hold raw
pipeline pointers. It requires an ownership/drain fixture before being called
a reproduced hang or use-after-free. No such runtime failure is claimed here.

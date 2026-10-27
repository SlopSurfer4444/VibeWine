# Memory and queue follow-up — 28 October 2026

This source wave develops the previous [neighbor directions](neighbor-wave-20261027.md)
using the existing DXMT owners. It does not install the new renderer, launch
DS3, erase caches, change telemetry protocols or claim a game FPS improvement.
The installed `bbf22e6a` / `e7410fc` endpoint remains separate.

## Implemented mechanisms

| General mechanism | Concrete change | Verified result |
| --- | --- | --- |
|Completed host tracking storage|Add missing reftracker ring maintenance to existing finish cleanup, after tracker clear/detach and coherence|Actual finish/Retain/reset/tracker/ring fixture: ten completed 4 KiB blocks shrink to one; pending storage, external references, all 32 slots and error completions remain valid; old source fails three exact predicates|
|Retained instruction slack|Best-effort compact after all CALL/CALLC cloning, before publishing parsed shader; keep256-byte / 25% threshold and preserve state on failed realloc|13 fixed shader inputs: actual usable packed backing138,240 → 120,432 bytes default and139,264 → 121,456 bytes growth-on;17,808 bytes saved;21 fresh compiler/parser TUs,48 bit-identical artifacts and9 CALL/CALLC/LLVM paths per configuration|
|Resource usage reporting|Map only generic SRV/RTV/UAV bindings into GetDXGIUsage|Actual query/forwarding bodies with real headers:42 cases / 1,792 assertions per architecture; baseline 36 capability failures and6 unchanged controls; CPU/shared/swapchain behavior not inferred|

All changes are general mechanisms without game-title checks or new switches.
The existing 19-switch default/experimental maps are unchanged. AIRCONV API 26 and
compiler-output epochs 4/5 remain unchanged: the compacted representation produces
the same checked outputs; it does not add lowering or change the ABI.

## Donor ideas and independent development

DXVK [actual-free-chunk reservation](https://github.com/doitsujin/dxvk/commit/9c51dd8b5d67ee3236b7839e6e8827b1f78075c9)
and [rotating relocation cursor](https://github.com/doitsujin/dxvk/commit/e197a8a576de2748e40dcdbfd7a4a81a326115ce)
were checked. Its dedicated-VRAM eviction policy excludes integrated GPUs. Our
missing maintenance consumer is a narrower defect than a new Vulkan/Metal
relocation system; no DXVK allocator code was transplanted and its fairness
fix is not claimed as newly implemented here.

NerRobDog's [IR-release sequence](https://github.com/NerRobDog/dxmt/commit/41a90a00caddedf40d561d008b663b9398b33499)
and [follow-up](https://github.com/NerRobDog/dxmt/commit/8285892231f9956e9479004091bca7118a80df5a)
were rechecked against the prior measured rejection. Rematerialization requires
new fallible leases, retained DXBC, cancellation/reflection/paired-stage contracts.
This wave develops the smaller local packed representation instead: no active
IR eviction, reparse, extra DXBC retention, lease protocol or imported donor code.
[Design and corpus limits](../../components/dxmt/docs/ir-compaction-20261028.md).

The small [Overpass GetUsage patch](https://github.com/McKack/overpass-runtime/blob/b90803d4186642ee79eb9bedbc91c555a9c86db8/overpass/patches/0002-d3d11-report-DXGI-usage-from-bind-flags-and-CPU-acce.patch)
motivated a real compatibility correction. We rejected its vertex/constant-buffer
shader-input policy and CPU mapping after inspecting Microsoft semantics and
Wine's resource-test contract. The [usage decision](../../components/dxmt/docs/dxgi-usage-20261028.md)
records exact licenses, primary sources and remaining parity gaps. It is not
proof of real ANGLE/CEF recovery or a direct FPS optimization.

## Acceptance actually exercised

Native and Rosetta sanitizer fixtures cover completed vs pending node storage,
strict frontier lag, held external resources, terminal errors,32chunk slots and
near-limit nonsentinel arithmetic. Packed-list fixtures cover moving and failed
realloc, unchanged payload/order/alignment/any, thresholds, zero/repeated state
and append after compaction in both growth policies. The full corpus rebuilds
all 21 converter/parser TUs consistently, avoiding mixed class definitions, and
compares reflection, arguments, ordinary and paired GS/tessellation output,
including repeated later compilation from retained IR.

The complete finish-order fixture was updated to include the new owner, with an
explicit missing-cleanup mutation. Its first integration compile rejected the
old fixture's absent allocator member; this was repaired rather than omitting
the production call.254 telemetry checks subsequently passed. New local corpus
checks require retained public Wine fixture extracts/configured toolchain inputs;
these are not declared hosted-CI coverage. CI additions cover the supported
small native fixtures, and are not evidence of an actual remote CI pass.

Both complete matched renderer configurations were built in fresh directories,
all 19 experiment flags false/true, reference mode none, using existing pinned
LLVM/SDK/toolchain and retained Wine headers. Full D3D11 buffer/texture translation
units consume the changed usage header. No complete new Wine distribution,
GPU execution or installed behavior follows from these source/build checks.

## Measured scope and unresolved limits

The 17.8 KiB reduction is 12.9% of measured default packed backing on 13 fixtures,
not 12.9% less total game memory or more FPS. Six inputs improved, seven did not;
large input backing stayed unchanged. Allocator usable bytes are not RSS or
physical-memory release. Small-input parse medians showed submicrosecond added
cost in one sequential diagnostic, not a controlled game speedup.

Reftracker reclamation keeps one warm block and requires further completions.
It inherits linear mutex-held scans; no absolute memory cap, fully-idle progress
or per-pass time limit is promised. No evidence yet attributes the previous
multi-second DS3 waits or host-wide swap to this particular memory owner.

A controlled future game trial must bind loaded binaries and settings, mark
scene/foreground intervals, compare pressure and long-frame tails, and measure
instrumentation overhead. No claims about GPU ownership or native clock domains
are changed by this wave.

## Ownership and retention

The [existing runtime ownership contract](../../components/dxmt/docs/fps-wave-20261025.md#memory-owner-follow-up--28-october-2026)
records exact process-memory owners, lifetime/grace, absent aggregate caps,
normal cleanup and pending/current exclusions. No data producer/collector or
installed lifecycle contract changes. Source, tests and decisions in Git are
durable. Private wave evidence is raw seven days / results thirty days with per-branch caps
(usage 3 MiB, IR/retirement 5 MiB each,review 1 MiB); build intermediates 1 GiB and logs 8 MiB,
seven days. Active analysis/recovery/explicit holds exclude cleanup. Only manual exact
owner/reference/hold-checked cleanup is supported; no cleaner installed.

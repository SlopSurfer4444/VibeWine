# Default-off pass experiments, 25 October 2026

`DXMT_DEAD_ATTACHMENT_STORES` and `DXMT_PHYSICAL_RESIDENCY_DEDUP` independently
enable the experiments in `src/dxmt/dxmt_context.cpp`. Undefined or zero selects
the existing behavior. They are general renderer experiments without executable
or game-profile checks. No runtime toggle or installed-runtime change is implied.
The integration owner supplies both PE/native build-option wiring and matched
build acceptance. No FPS gain follows from CPU or offscreen correctness tests.

## A full clear can make one preceding color store unnecessary

`DiscardAdjacentColorStores` examines only the original strictly adjacent
Render→Clear pair before the existing optimizer runs. It changes one predecessor
Store to DontCare; subsequent Clear→Render folding cannot erase the explicit
proof used to make that decision. It does not infer overwrite from a load action,
scan past another encoder, or change the optimizer's fences or node ownership.
If the clear survives, the same store decision applies.

The admitted case has exactly one color target at slot zero and no depth/stencil
attachment. It requires the same retained base-view object/native texture and
allocation; ordinary Store with no resolve; a private nonshared nonheap allocation
without mapped memory; a 2D, single-mip, single-layer, single-sample texture of
depth one; level/slice/depth-plane zero; and nonzero identical full render/clear
extents. Native attachment width/height must also match the producer descriptor.
Those two getters cost native bridge work for otherwise eligible pairs, so they
must be included in performance comparisons. Current linear/buffer-backed texture
factories never mark allocations GpuPrivate; that source invariant is part of
this deliberately narrow admission rule, not a claim that null mappedMemory alone
proves independent texture storage.

Arrays, multisampling/resolves, depth/stencil aspects, reinterpretation/subresource
views, shared/placement-heap aliases, partial extents and intervening operations
are rejected. Preserving all draws and queries is necessary even if their final
color contents are discarded. Existing unrelated optimizer behavior is unchanged.

## Suppress covered declarations after physical pass merging

Logical-encoder residency deduplication remains in place. The optional final
render-list walk runs after the existing pass-merging optimizer, immediately
before the recorded list is consumed by the native render encoder. It changes a
later UseResource node into Nop only if one earlier actual declaration of that
exact native resource covers both its usage and stage masks. Usage and stage
masks are never independently unioned. Zero resource/usage/stage declarations
remain intact. Upgrades, other commands and their relative order remain intact.

A 128-entry direct-mapped stack cache bounds memory and makes traversal linear.
Collisions/evictions merely miss savings; each cache entry records one real
declaration. Nops preserve linked-list topology and the existing tail. This is
render-list only: it does not compact compute lists or the separate GS/TS marshal
setup declarations. It does not remove access tracking, fences, allocation
retention, UAV counters, resource bindings or actual work. It introduces no
residency set or global pinning policy.

## Evidence and remaining acceptance

The CPU fixture in `tests/vibewine/pass-optimizations` extracts the production
encoder structs, optimizer and relationship/signature methods and uses the real
WineMetal command layouts and fence sets. Native-object and texture ownership
adapters make metadata/commands observable without a Metal device; they do not
execute the complete D3D11 or native Metal renderer. Default/off and independent
enabled variants plus the historical source and connected negative mutations
check the behavior rather than trusting a helper return value alone. The fixture
README records actual runs and limitations.

On 25 October, native arm64 and x86_64/Rosetta runs with ASan/UBSan each accepted
five flag configurations with 544 assertions per configuration. The historical
context and nine connected source mutations compiled and failed their behavioral
assertions with natural exit 1; sanitizer/setup failures were not admitted as
negative controls. These runs bind helper SHA-256
`8c4fdedfd1989f5f95ed44588035fca2c1f5b75f5e43f81a83895e71856ed537`.

The separate headless GPU guest prepares five full image readbacks covering folded
and standalone clears, an intervening reader, and mergeable logical passes. Its
negative control first verifies the correct image and then rejects a changed
golden. Source preparation and successful compilation are distinct from an actual
matched-runtime GPU run; the integration owner owns sequential disposable-prefix
execution, module/hash readback and natural-exit acceptance.

The prepared guest also compiled with the reviewed LLVM-MinGW toolchain under
`-O2 -Wall -Wextra -Werror` without diagnostics, and import inspection passed.
The source hash is `80b407e342fee2b137ea7598501afd750c52996d2016a037eb41fcbdbc3d8696`;
the PE was 535040 bytes. Temporary compilation outputs were removed. This was
compile/import validation only; no GPU execution is claimed here. Offline report
checks accepted two complete reports and rejected thirteen invalid variants.

Before performance promotion, measure eligible stores and native declarations
on the real CPU/GPU critical path, compare one switch at a time at fixed workload
and output, and reject savings within A/A variation or regressions in images,
latency or memory. Count stores/declarations after physical optimization. Nominal
attachment bytes are not measured bus traffic. These experiments add no telemetry
producer, capture scope, cache, service or retention behavior. Test scratch is
temporary; retained integration artifacts reuse their owner's existing bounded
retention/active-hold contract.

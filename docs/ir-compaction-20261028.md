# Compact retained shader instructions — 28 October 2026

Parsed IR eviction remains rejected as a direct donor import. The preceding
[measurement and rejection](../../../docs/diagnostics/performance-wave-next-20261025.md#parsed-shader-ir-decision)
identified real allocator opportunities, but no RSS improvement, and donor
rematerialization/cancellation/reflection hazards. This change takes a smaller
resource-lifetime optimization: trim unused packed instruction capacity once
while the shader is still parser-local. Parsed instructions remain available for
late variants, paired stages and cache misses without reparsing or retaining DXBC.

## Sources checked and mechanism choice

The exact NerRobDog sequence was checked again at
[41a90a00](https://github.com/NerRobDog/dxmt/commit/41a90a00caddedf40d561d008b663b9398b33499)
and [82858922](https://github.com/NerRobDog/dxmt/commit/8285892231f9956e9479004091bca7118a80df5a),
and current source at
[8a9f4b74](https://github.com/NerRobDog/dxmt/blob/8a9f4b749b3dce1f0340b7f92b4ce5d300d22421/src/d3d11/d3d11_pipeline_cache.cpp).
Eager default IR release retains DXBC and reparses after a binary cache miss.
Constructor pending-user counts are not released by never-run task destruction;
failed `ir_ensure()` still permits null compilation; argument free remains
conditional on a nonnull shader. Its mechanism is not copied or enabled here.

[DXVK bd395bf0](https://github.com/doitsujin/dxvk/blob/bd395bf055e69943e74f869581301ed24cf16b53/src/dxvk/dxvk_shader_ir.cpp)
keeps serialized IR and materializes a temporary builder. Its representation
cannot be transplanted into DXMT's callback/CFG objects without a serializer and
new ownership contract. Local `PackedVariantList` already has the compact packed
representation and allocator; its unused allocation capacity is the smaller
opportunity. No donor code or dependency is imported by this change.

## Production lifetime and acceptance

`read_control_flow` first parses instructions, then finishes CALL/CALLC cloning.
Cloning copies instruction values. Its work stacks and return/branch targets hold
`BasicBlock` pointers or target-variant pointers, never addresses inside packed
instruction storage. No instruction visitor reference escapes that final phase.
The finalizer moves only packed backing; BasicBlock addresses, branch targets,
metadata and captured signature handlers remain unchanged. The shader has not yet
been published to `CachedSM50Shader` or compilation workers.

`compact()` shrinks only when unused requested capacity is at least 256 bytes and
one quarter of capacity. The subtraction/division avoid threshold overflow.
Empty backing is freed; failed shrink leaves original state intact; successful
shrink preserves packed offsets/tags/values and sets capacity to occupied bytes.
No new lock, task counter, IR lease, runtime ABI or option is needed. Shader cache
keys/output epochs remain unchanged because output artifacts matched exactly.

Fresh CPU results on 13 existing Wine fixtures (VS/PS/GS/HS/DS/CS):

| Existing growth policy | Baseline packed usable bytes | Compacted usable bytes | Saved |
| --- | ---: | ---: | ---: |
| Default 1024-byte steps | 138,240 | 120,432 | 17,808 (12.9%) |
| Optional 1.5x growth | 139,264 | 121,456 | 17,808 (12.8%) |

Six inputs reduced actual usable backing; seven were unchanged. Requested
capacity reduction need not lower actual allocator backing: `realloc` can retain
its allocation bin. No claim of process footprint/RSS release follows. These
inputs are not a DS3 shader distribution; largest-input backing remained unchanged.
The bounded single-process timing diagnostic showed up to about 0.5 microseconds
additional median parse time among small eligible inputs, with run/load variation;
no frame-time or net workload speedup is established.

The [focused runner](../tests/vibewine/packed-ir-compaction/README.md) records exact
limits and scope. Actual fresh converter/parser TUs produced 48 identical
shader/metadata artifacts in both growth configurations. Ordinary and paired
outputs were regenerated using the same retained IR. Template sanitizer checks
passed on arm64 and x86_64 in both growth modes, including forced relocation,
failed allocation, zero size, repeat/no-op thresholds and append afterward.

## Data ownership

No persistent producer or runtime retention policy is introduced. The existing
pipeline-cache owner still retains shaders and parsed IR until cache destruction;
current/queued/running users and scheduler drain rules are unchanged. The only
scope change is best-effort reduction of unused packed instruction allocation
capacity, bounded by each existing allocation; no independently retained bytes,
TTL or new cleanup queue is added. Active IR is never evicted.

Fixed tests/source/decisions are durable Git results. Private receipts are under
`outputs/integrated-wave-20261023/memory-queue-wave-next/ir`, owned by this wave:
raw seven days/5 MiB, notes 30 days; active review, unresolved analysis and recovery
holds exclude cleanup. Test binaries/objects are private bounded temporaries
removed on completion/error; no cleaner, game launch, runtime installation or
GPU measurement was performed.

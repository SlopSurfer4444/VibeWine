# Frontier source checkpoint — 28 October 2026

Local changes on DXMT `8cf59a015b88361ed6f6320eb8a781c68b3b36c1`.
The audit supplied leads; every admitted change was independently bound to the
production source and checked. No external probe implementation was imported.
Existing component and file licenses remain in force.

## General fixes and reductions

| Path | Result | Verification boundary |
| --- | --- | --- |
| Staging initial data | Copy logical row payloads for each mip/slice, including 1D; do not copy source padding | Actual creation/layout bodies; guarded inputs, native/Rosetta ASan/UBSan; old 1D and padded-copy failures reproduced |
| Same constant buffer, whole view | Null range arguments reset prior range even when buffer identity is unchanged | All shader stages, dirty/replay/getter cases, native/Rosetta and connected negative controls |
| Immediate GetData | DataSize zero never writes caller memory across all query kinds; ready polling reuses CPU wrapper | Production method/query extraction, concurrent publication, lifetime/flush/generation controls and TSAN; this is not native Metal query allocation measurement |
| Ordinary graphics PSO key | Ignore index format only when GS, HS, DS and SO are absent | Live draw descriptor unchanged; ordinary/GS/TS/SO keys and forced hash collisions tested |
| Pixel variants | Remove UNORM bits for unused outputs before both variant lookup and compile argument/digest construction | Reflection-based output mask; dual-source case kept conservative; native/Rosetta controls |
| Shader parsing | Concurrent same-SHA calls share one synchronous parse; unrelated keys parse outside global map lock | Actual CreateShader and cache helper; success, failure/retry, allocation, exception, cancellation and lifetime controls; no raw bytecode retained by waiters |
| Depth/stencil initialization | Two row updates per row instead of two updates per texel; same depth/stencil bits and two blits | Transactional scratch allocation, conversion/stride/overflow tests; native offscreen Metal plane readback and corruption control |
| Tile helper failure | Preserve pending barriers and omit dependent draw if helper PSO cannot be created; retry remains possible | Actual dispatch/resolve and fourteen draw call sites; four behavioral negative controls and existing failed-pipeline cases |

VB/SRV backing invalidation is refined only under the existing default-off
`vibewine_precise_bindings` option. Immediate and deferred dynamic DISCARD paths
use exact affected membership. Texture/off paths retain their existing behavior.
The other repairs above do not inspect DS3 or any application name. No shader
cache epoch change is needed for the normalization: the canonical compile recipe
already had that meaning; now in-memory lookup agrees with it.

## Bounded measurement

`combined-bounded` is an explicit new trace profile. It retains compiler,
presentation and stall coverage together and adds helper event120 spans for
tessellation lock/native creation, ClearUAV, tile and RTV helpers. Immutable
serials and same-queue frame observations support conservative joins. Missing
closure/families, loss, contradictory sessions and exhausted limits remain
unknown or ineligible. The new profile does not infer gameplay, scene labels,
displayed FPS or stall causality. Old profile coverage and report shape are
preserved; helper clocks/serials are gated away from old profiles.

Run the portable fixtures from the component root, for example:

```sh
python3 tests/vibewine/staging-initial-data/run.py --arch arm64
python3 tests/vibewine/cb-range-reset/run.py
python3 tests/vibewine/precise-bindings/run.py
python3 tests/vibewine/query-publication/run.py --sanitize address
python3 tests/vibewine/query-publication/run.py --sanitize thread
python3 tests/vibewine/pso-normalization/run.py --arch arm64
python3 tests/vibewine/pso-normalization/run-createshader.py --arch arm64 --sanitize address
python3 tests/vibewine/pso-normalization/run-singleflight.py --arch arm64 --sanitize thread
python3 tests/vibewine/bulk-depth-stencil/run.py
python3 tests/vibewine/bulk-depth-stencil/run.py --gpu
python3 tests/vibewine/tile-barrier/run.py --arch arm64
python3 tests/vibewine/combined_trace_test.py
```

The GPU fixture needs a supported local Metal device and is separate from CPU
CI. Tests use private temporary scratch; their READMEs define exact extraction,
mutations and exclusions. Native/Rosetta fixture success is not full API or game
acceptance. The hub records combined build/check results.

## Remaining limits

- Full GPU enforcement of constant-buffer NumConstants out-of-range zero reads
  is not implemented by the range-reset repair.
- The existing unsupported fragment-to-preraster barrier branch still warns
  and clears its bits; the tile repair does not establish all barrier ordering.
- Existing successful GS/TS marshal hoisting is not redesigned here.
- Tile failure has CPU command-path coverage, not native GPU failure injection.
- The row fixture exercises actual native Metal but not the complete Wine/PE
  bridge; PSO normalization still needs a broader rendered shader corpus.
- ARM converter and Metal4 compiler-only prototypes are independent experiments,
  not runtime features. They must pass complete recipe/bridge/lifetime/GPU gates
  before engine integration. No FPS gain or installed state follows from this
  source checkpoint.

# Resolution sweep and guarded compiler experiment — 4 November 2026

The preceding CPU hot-path ABBA did not demonstrate a reproducible speedup.
This follow-up varies render dimensions while keeping the actual window client
at 960×540. The guest checks both GetClientRect and the actual backbuffer texture
description; host readiness and completion require matching integer geometry.
Existing guests remain supported only in the default coupled-size mode.

## Observed resolution sensitivity

Eight finite runs used the same private matched renderer, vsync 0, two seconds
warmup, 240 measured iterations/Present calls and a ten-second ceiling. All
completed the native process/module binding, three-Present calibration,
readback, trace observation and exact private wineserver settlement gates.

| Order | Scene | Render size | Actual elapsed seconds |
| --- | --- | --- | --- |
| 1 | pixelheavy | 1280×720 | 1.97586 |
| 2 | pixelheavy | 1920×1080 | 2.64762 |
| 3 | pixelheavy | 2560×1440 | 4.63021 |
| 4 | cube | 2560×1440 | 1.96953 |
| 5 | pixelheavy | 2560×1440 | 4.91099 |
| 6 | pixelheavy | 1920×1080 | 2.62029 |
| 7 | pixelheavy | 1280×720 | 1.97590 |
| 8 | cube | 1280×720 | 1.96868 |

Outside-Present CPU wall medians in pixelheavy remained approximately
0.144–0.159 ms. This supports investigating resolution-dependent GPU work;
it does not distinguish ALU, memory or scheduling. Full native presentation
coverage remained false, so neither displayed FPS nor game performance is
established. Selected command-buffer GPU start/end intervals overlap and can
include waits; they are not per-frame shader execution time. Scope-14 CPU
duration records below 100 μs are censored and are not an unconditional sample.

Private evidence: `work/hotpath-economy-20261104/gpu-window-manifest.json` and
`gpu-window-summary.json`, independently recomputed from eight cases and forty
evidence files. Summary SHA-256:
`a7ff767a72c9bf2a90e55f7268dcad016b34355e4ebf2df3b9ff79a0dbf6e1a3`.
An earlier coupled-window 1080p attempt failed client geometry before readiness;
it is not performance evidence and is excluded from this matrix.

## Concrete compiler candidate

The actual benchmark HLSL was compiled through D3DCompile optimization level 3
and the matched DXBC→AIR converter. Its 96-iteration pixel loop retained two
constant-address-space loads, time multiplication and scalar cosine/sine inside
the loop. This is produced IR evidence, rather than an inference from HLSL.

DXMT commit `a49a51c5` adds default-off `vibewine_constant_loop_motion`. It admits
only fragment functions with a conservative whole-function instruction and
pure AIR-math whitelist, then uses LLVM LoopSimplify, LCSSA and MemorySSA LICM.
It changes no floating-point flags or function attributes. Unsafe memory,
stage, call and scalable-vector cases remain byte-identical. The guarded
zero-trip case retains its loads and calls inside the loop.

Actual heavy-loop AS2 loads decrease 2→0 and math calls 10→8. Production-pass
and full-context off/on fixtures pass ASan/UBSan with 23 rejected-module
controls, unchanged declaration/FP flags and source-connected negative controls.
The compiler cache has distinct identities for all cleanup/loop-motion option
combinations. The optional ARM-worker builder now explicitly binds both flags,
its source/options identity, SDK selection and tool environment.

Final fixture receipt SHA-256:
`3041f128688d2923a18879d6e25910b27ec59e2e744131ccdd19398dbe804550`.
Actual heavy DXBC SHA-256:
`cce2074ce9bb9937641e172ff2eccd3abcb48e13083c05b03ff14368f3c76e10`.

This experiment is **not installed or FPS-accepted**. Apple’s final compiler
may already eliminate the repeated work. Acceptance still needs complete
matched off/on builds, real rendered-image equivalence and warm GPU comparison,
with the selected converter worker built from the same source/options. Existing
benchmark sentinel readback proves calibration completion, not shader-image
equivalence. No game or global default activation follows from these fixtures.

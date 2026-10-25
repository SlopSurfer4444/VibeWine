# Experimental late integer halfword cleanup

`DXMT_SHADER_CLEANUP=1` adds `LateHalfwordCSEPass` after the unchanged
Lower16BitTexRead → implicit barrier sequence. The build default is zero.
This is an independently selectable compiler experiment, not an FPS result.

The source-derived missed work is concrete: Lower16BitTexRead separately expands
live low/high i16 extracts of one i32 lane. It runs after the last generic CSE
and InstCombine, so both new i32 `extractelement` instructions survive. The
baseline fixture reproduces eight extracts over four lanes; the candidate has
four. The pass only deduplicates exact same-basic-block instructions for:

- a constant in-range i32/i64-indexed lane of `<4 x i32>`;
- scalar i32 `lshr` by 16;
- scalar i32 to i16 truncation.

It uses LLVM's `Instruction::isIdenticalTo` (including instruction flags), keeps
the earlier dominating definition, and never rewrites floating point, memory,
atomics, calls, barriers or control flow. This does not add FP16 arithmetic,
fast-math, contraction, reassociation or precision conversion. Pure instruction
liveness can still affect register allocation, so fewer AIR instructions do not
establish lower GPU cost.

Reused implementations checked: the existing
[DXMT lower16 transform](https://github.com/3Shain/dxmt/blob/main/src/airconv/transforms/lower_16bit_texread.cpp)
provides the missed pattern; LLVM's
[EarlyCSE](https://github.com/llvm/llvm-project/blob/llvmorg-15.0.7/llvm/lib/Transforms/Scalar/EarlyCSE.cpp)
already provides wider CSE but also forwards loads, deletes stores and merges
attributed calls. A second whole-function EarlyCSE was rejected for this narrow
experiment because the implicit AIR synchronization contract is outside ordinary
LLVM memory semantics. The small restricted pass reuses LLVM exact identity
without introducing another memory optimizer or new dependency.

## Run

```sh
python3 tests/vibewine/shader-cleanup/run.py \
  --llvm /absolute/llvm15-prefix \
  --support-build /absolute/existing-matched-build \
  --mutations
```

The existing local support build selects LLVM **15.0.7**, x86_64. Older test
comments elsewhere calling that installation LLVM14 are not evidence of its
version. The runner compiles current `airconv_context.cpp` separately for switch
off/on, links the existing parser/converter/AIRBuilder archive and generated AIR
headers, and executes the production pipeline. It does not rebuild all retained
converter translation units; a full matched component build remains a separate
integration gate.

Checks cover all four halfword pairs, 1,056 deterministic bit mapping cases before
and after the full pipeline, LLVM verification, real AIRBuilder declarations for
texture writes/atomics/discard, and the inserted convergent threadgroup barrier
with its original operands and surviving volatile store/load/store sequence.
The exact pass also has direct contract checks for differently typed indices,
excluded exotic-width/out-of-range indices, exact versus nonexact shift flags,
and branch-local isolation. These checks are appended to a temporary production
TU copy and introduce no runtime test entry point. Precise floating multiply/add
retain zero fast-math flags. Baseline and candidate
sideeffect/precision function IR and all remaining module IR (including
declarations and attributes) must be byte-identical. A generated valid SM5
compute DXBC goes through public SM50Initialize → SM50Compile → serialized AIR
bitcode parsing and verification in both modes. This minimal public shader is a
converter integration check, not a captured game shader or the halfword fixture.

Mutation controls remove the production CSE insertion (effect assertion must
fail) and the production barrier insertion (post-barrier load assertion must
fail). Temporary sources, objects, binaries and IR live in the runner's private
temporary directory and are removed on exit. There is no runtime installation,
game/GPU launch, service, log producer or retained shader corpus.

## Acceptance boundary

This is evidence of one missed IR pattern and its conservative elimination.
No captured hot-shader corpus was available in the bounded existing metrics tree.
No cold shader compile latency, Metal compiler duration, final native GPU code,
register pressure, image correctness, warm GPU time, or gameplay FPS has been
measured. Shipping/default enablement requires representative hot shaders,
matched build, image validation, cold compile versus warm GPU costs and reversal
under the existing evidence contract. Reject the performance lead if the pattern
is absent, compiled Metal already removes it, compile cost or GPU behavior
regresses, or benefit remains inside baseline variation.

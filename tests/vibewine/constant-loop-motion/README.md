# Opt-in constant-memory fragment loop motion

`vibewine_constant_loop_motion=false` is the default. Enabling it defines
`DXMT_CONSTANT_LOOP_MOTION=1` identically for PE/native Meson targets and adds
`ConstantLoopMotionPass` after the existing lower16, implicit-barrier and optional
halfword cleanup sequence. It introduces no runtime setting or app profile.

The production pass first rejects the **whole function** unless `air.fragment`
metadata names that exact function and the module contains no kernel, vertex,
mesh or object stage metadata. Every block is checked, including unreachable
code. Only nonatomic, nonvolatile AS2 loads, explicitly enumerated pure IR
operations and exact typed AIR math declarations are admitted. Math calls must
be direct, nonvariadic, nonconvergent, non-strictfp, non-noduplicate, readnone,
nosync, nounwind and willreturn, with no operand bundles or musttail. Signatures
are f32 or fixed 2/3/4-lane f32 unary/min/max/FMA and dot overloads; name alone is
insufficient. Unknown/indirect calls, constrained FP, derivatives, interpolation,
texture access, discard, barriers, writes, atomics, volatile/device/shared memory
and address-space casts are excluded.

Rejected functions return `PreservedAnalyses::all()` before even canonicalization.
An admitted function with loops runs standard LLVM LoopSimplify, LCSSA and
MemorySSA-enabled LICM, with no loop rotation or custom instruction movement.
No attributes or FP flags are added. LLVM still decides aliasing, guaranteed
execution, dereferenceability and safe speculation; the admission filter never
forces a move. Keeping the experiment after the unchanged implicit-barrier
detector and excluding its kernel/AS3 domain preserves that detector's input.

## CPU regression

From the component root, using existing LLVM 15 and a matched support build:

```sh
python3 tests/vibewine/constant-loop-motion/run.py \
  --llvm /absolute/llvm15-prefix \
  --support-build /absolute/existing-matched-build \
  --output-dir /absolute/task-local/fresh-evidence \
  --sanitize --mutations
```

`heavy.ll` is the actual preceding CPU probe's optimized AIR, preserved byte for
byte (SHA-256 `36548ba5a5b2dc091165ad6eb3b1de927cdc8b4ed3a1c579a6a3e70bfda6cb6d`).
Its source was the unchanged embedded `ps_heavy` benchmark shader, D3DCompile
level 3, flags2=0, ps_5_0. The fixture verifies modules before/after and compiles
the actual production transform plus current `airconv_context.cpp`; unchanged
converter dependencies/generated helper headers come from the support archive.

Direct production pass checks establish loop loads **2 -> 0** and calls
**10 -> 8**, the entire time/trig chain in entry, preserved declaration bytes and
FP flags (both original zero fast-math flags and an explicit contract/reassoc/nsz
control). The zero-trip counter variant keeps both loads and all ten calls in
the body under the nonrotating standard pipeline.

Twenty-three independently parsed, LLVM-verified rejection modules must remain
byte-identical under the actual pass: AS1/AS3 loads, volatile/atomic load, store,
atomic RMW, convergent/noduplicate/strictfp callee, unknown call, wrong mangled
signature, a scalable-vector signature under the scalar `air.sin.f32` name,
derivative, barrier, discard, missing readnone/nosync/willreturn,
vertex/missing/mixed-kernel stage metadata, strictfp entry and constrained FP.
An additional metadata-identity case keeps a non-entry helper unchanged.
These are direct gate/pass checks; they do not claim that the other generic
optimization passes preserve an unsafe input unchanged.

The full production optimization pipeline is compiled with the new define
absent, explicitly zero and one. Default/off full outputs must match exactly;
only on hoists the heavy chain. Source-connected negative controls bypass
admission (must change the rejected AS1 module) and omit LICM (must lose the
heavy work-saving result). A third control restores the old scalar-type check;
it must change the scalable-vector rejection module specifically. Compilation errors, unrelated assertions and
sanitizer failures cannot satisfy either expected negative.

The runner uses an x86_64 LLVM 15 support build; optional ASan/UBSan instrument
the fixture and rebuilt production TUs, not the prebuilt LLVM/archive internals.
LeakSanitizer is disabled because macOS does not support it. Commands, binary,
source, fixture and output hashes are retained in the receipt. Compilation/run
limits are 60/30 seconds and 256 KiB per command log. One temporary binary exists
at a time, peak output is capped at 128 MiB, and generated binaries/objects are
removed on exit. Evidence is retained 30 days under the enclosing task contract;
active integration/recovery holds take precedence.

## Matching and acceptance boundary

Compiler output changes require a distinct compiler-cache compatibility value;
the shared database/PSO archive epoch is 6 for loop motion alone and 7 when
combined with shader cleanup. Existing off configurations retain epochs 4/5.
Portable compile controls check these combinations. Do not reuse
old persisted AIR as evidence that the new pass executed.

The standalone native ARM worker builder reads `airconv_src` and hashes its source
files automatically, so this new transform joins its source identity. That alone
does not select the option: standalone compiler flags must explicitly carry the
same `DXMT_CONSTANT_LOOP_MOTION` **and** `DXMT_SHADER_CLEANUP` values as the native
converter/bridge configuration. They must be part of its hashed converter flags,
then a newly built worker's reported build ID must match the bridge's
`vibewine_arm_worker_build_id`. Meson's `native: true` definitions alone do not
configure a standalone builder. No ARM worker was built or activated here.
The canonical builder now exposes `--constant-loop-motion` and `--shader-cleanup`
for that explicit matching; both default off and participate in its build ID.

CPU IR verification and sanitizer checks are not complete AIR semantic proof,
native Metal acceptance, displayed-image equivalence or GPU/FPS improvement.
Full matched PE/native/worker compilation, cache separation, effects/precision
corpus, native compilation/image checks and measured cold-compile/warm-GPU A/B
remain independent integration gates. Apple's downstream compiler may already
perform this motion, and register pressure or compilation cost may erase benefit.

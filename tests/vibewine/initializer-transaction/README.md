# Resource initializer transaction regression

Run on macOS from the source root:

```sh
python3 tests/vibewine/initializer-transaction/run.py
```

This correction-oriented fixture is separate from the immutable
`initializer-failure-proof` diagnostic. It compiles snapshots of the actual
initializer, ring allocator, allocation tracker, and Metal C++ bridge with
ASan and UBSan. Only dependency includes are removed when composing the
translation unit. Resource stand-ins provide dimensions and native handles;
the C API shim injects native allocation/submission failures and records
commands only when their command buffer commits. A malloc macro around the
initializer implementation alone provides its constructor-allocation failure
seam. Exceptions remain enabled to check `flushToWait` error propagation.

The 31 cases cover exactly 18,454 natural buffer fills, all six initializer
paths across CPU capacity, reference-storage rollover, valid A / failed B /
valid C, no reuse of failed native buffers, depth/stencil command-pair
atomicity and disjoint spans, first/second upload-allocation failures, zero
cache failure and growth, persistent finite failure, initial malloc/queue/event
failure, nil command buffer/blit/render encoder with preserved pending work,
all-path success, an exhausted empty heap, checked size overflow, and retained
allocation cleanup, actual initializer reuse of two completed 32 MiB blocks
for a 32 MiB depth plus 8 MiB stencil upload under native allocation failure,
source-pitch overflow, sequence exhaustion, and D24 planar payload bytes.
The reuse fixture allocates its bounded 64 MiB source only in process memory.
The sibling `ring-checked` tests independently check
atomic reuse of two completed blocks under native allocation failure.

The runner snapshots every input before compilation and prints SHA-256 hashes
of sources, fixtures, and generated translation unit. Compilation has a
60-second deadline and execution a 30-second deadline; stdout and stderr are
capped at 4 MiB each. Execution denies network access and writes outside its
private temporary directory. All temporary sources, binaries, and captures
are removed on exit; the caller owns any explicitly saved receipt. There is
no persistent collector, automatic archive, build/deploy step, or installed
runtime change.

These tests establish CPU transaction behavior against the real bridge
wrapper and a mocked native C API. They do not establish GPU execution,
resource completion/lifetime on real Metal, game correctness, performance,
or installed-runtime behavior.

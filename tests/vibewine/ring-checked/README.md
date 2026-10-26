# Checked initializer ring reservations

`run.py` compiles the complete production ring header, including the GPU,
placed staging and host allocator bodies. It replaces only platform dependency
includes with a counted native-buffer seam. This exercises the real checked
reservation code and the real `std::list` ownership/metadata allocations.

```sh
python3 tests/vibewine/ring-checked/run.py --sanitize
python3 tests/vibewine/ring-checked/run.py --arch x86_64 --sanitize
python3 tests/vibewine/ring-retention/run.py --sanitize
python3 tests/vibewine/ring-retention/run.py --arch x86_64 --sanitize
```

`RingBumpState::allocateChecked(seq, coherent, sizes, alignment, out)` accepts
one or two nonzero sizes and an equally sized output span. Alignment must be a
nonzero power of two. `Reservation` contains `Block *block` and `uint64_t offset`.
Failure leaves outputs, FIFO ordering, block identity, offsets, sequence IDs and
TTL values unchanged. Success commits all reservations using nonallocating list
splices. Completed standard blocks remain eligible only when their last-use
sequence is strictly less than `coherent`; in-flight blocks cannot be reset.

The fixture verifies:

- Failure after planning a current-tail span, a recycled span, or a new span;
  snapshots compare every existing FIFO entry and metadata field.
- First/second native allocation failures, thrown `bad_alloc`, and first/second
  production list-node allocation failures. Temporary blocks are released once.
- Two completed 32MiB blocks serve a 32MiB + 8MiB batch with native allocation
  forbidden and list-node allocation forbidden. Their existing `Block` references
  survive reuse; the warm survivor remains valid after retirement of its neighbor.
- Shared-block paired spans, aligned offsets, zero sizes, count/output bounds,
  invalid alignments, extent/alignment overflow, strict coherence and protected
  in-flight blocks. Oversized ad-hoc blocks are not recycled as standard blocks.
- Nil native buffers, valid native handles with zero optional GPU addresses,
  missing placed CPU memory, host malloc
  failure on either plane, and an invalid block from the legacy unchecked API.
  Nonplaced staging remains valid with a native buffer and no CPU mapping.

The legacy unchecked allocation entry point remains available. The existing
ring-retention suite independently covers its retention and concurrent cleanup.
These CPU fixtures use mocked native buffers and tiny placeholder host storage;
they establish no GPU execution, installed runtime, gameplay or performance
result. The runner reports the exact production header SHA-256 and removes all
task-owned generated source/binaries through `TemporaryDirectory` on exit,
including failure. There is no persistent data producer or raw capture archive.

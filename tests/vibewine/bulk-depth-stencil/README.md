# Bulk depth/stencil initializer rows

Run from the source root on macOS:

```sh
python3 tests/vibewine/bulk-depth-stencil/run.py
python3 tests/vibewine/bulk-depth-stencil/run.py --arch x86_64
python3 tests/vibewine/bulk-depth-stencil/run.py --gpu
```

The production initializer now packs an owned depth row and stencil row in one
fallible `calloc(1, 5*width)` allocation. It allocates before acquiring the mutex
or changing pending state, then keeps the existing reserve-both, retain, fill,
publish-pair transaction. The original D24 division expression remains exact;
D32 depth uses byte copies. Only logical stencil bytes cross the bridge, so its
rounded upload row padding remains untouched. Calls fall from `2*width*height`
to `2*height`; bytes remain `5*width*height` and GPU copy commands remain two.
There is no ABI, SIMD, storage policy, or persistent scratch-cache change.

The runner snapshots the complete production initializer, ring allocator,
allocation tracker, actual Metal.hpp/wire definitions, supporting headers and
fixtures before compiling. Dependency includes alone are stripped when composing
the translation unit; production method bodies are unchanged. A macro intercepts
actual production calloc/free calls, independently of constructor malloc.
The prelude extends the existing initializer-transaction C API fixture; resource
stand-ins are snapshotted from that fixture without modifying its sources.

The 24 CPU cases check both formats, widths 1/2/3/4/5/7/19, padded and unaligned
sources, distinct stencil/X24 bytes, D24 endpoints/adjacent midpoint values, D32
NaNs/infinities/±0/subnormals as raw bits, nonzero suballocation offsets, mips and
slices, all allocator payload/padding/sentinel bytes, unchanged source guards,
all plane metadata/destination/options, two-command publication, failed encoder
retry, checked source arithmetic, first/second plane reservation failure, and
actual calloc failure with a queued A/full CPU heap/failed B/recovered C.
Caller references are dropped while the initializer is live to prove A's pending
hold and B's absence of a hold. Scratch free counts cover success and reservation
failure. All runs use ASan+UBSan. LeakSanitizer is unsupported on this platform;
explicit scratch and destination-hold checks cover the changed ownership.

Historical `e3845197978e3fde46a07b584073ba499fd0b25b` must pass the same data and
transaction checks in legacy count mode (23 cases); the unchanged historical
body must fail the candidate target specifically at the bulk-row call count.
A normal failure/crash/sanitizer report cannot satisfy that negative control.

`--gpu` additionally binds the test buffer-update and copy endpoints to real
native Metal resources. The unchanged production initializer creates the planar
payloads; the adapter uploads both planes to a private Depth32Float_Stencil8 array
texture and reads them back at the encoded slice/mip, comparing every meaningful
byte against the independently validated fixture data. Twenty cases execute
forty real plane blits plus forty readback blits; both formats, odd dimensions,
source padding, NaN depth bits, mips and slices participate. Actual row updates and their
managed didModifyRange calls are counted separately from fixture setup. A second process corrupts a native
stencil source byte and must exit 20 only after real readback detects corruption.
The adapter substitutes Wine/native endpoints and queue plumbing, so this is
native Metal readback evidence rather than matched PE/Wine runtime acceptance.
No game, installed runtime, window, swapchain or full project build is used.

Each compile has a 60-second deadline and process has a 45-second deadline,
with 4 MiB stdout/stderr file caps, denied network and writes restricted to its
private temporary directory. Each GPU completion poll has a five-second
cooperative deadline; process timeout is failure, never GPU-cancellation proof.
Temporary snapshots, focused binaries and captures are removed on exit. No
persistent producer is installed. The owning root consumes stdout receipts.

Evidence retained outside Git belongs to the wave upload worker/root consumer:
`outputs/frontier-wave-20261028/upload`. Raw logs/JSON receipts: 7 days, aggregate
32 MiB cap. Result/verification notes: 30 days, aggregate 8 MiB cap. Active review,
unresolved failures and recovery holds are excluded from expiry. Cleanup is
manual and exact-path only after owner verification of holds, identity and
retained results; this document installs no cleaner and authorizes no purge.

Integration must also adapt the older initializer-transaction fixture's two
legacy call-count/sample-layout assertions. That pre-existing test directory and
shared build/test registration belong to the root integrator.

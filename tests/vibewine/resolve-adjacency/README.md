# Preserve live encoder order when folding a resolve

The production optimizer previously allowed Render R → independent live X →
Resolve Q(S→D) to absorb Q into R before checking Q's dependencies. If X read
or wrote D, the real tracker supplies Q's wait on X. The fold moved the resolve
before X and copied that future wait into earlier R. This is a demonstrated
CPU dependency/order transformation defect. Its actual Metal manifestation is
not established by these tests.

The correctness fix applies to both unbounded/default and optional bounded
search. A per-search live-node marker allows Render→Resolve folding only when
R and Q are adjacent live encoders. Existing Null positions and successfully
absorbed resolves stay transparent. A surviving unrelated/mismatched encoder
blocks the backward fold conservatively. The existing dependency stop still
runs for a suppressed resolve. Other relation kinds and the physical 64-slot
lookahead bound retain their existing paths. No public API, new option, object
ownership or command emission changes are added. Existing adjacent fence
transfer behavior is intentionally preserved.

```sh
python3 -B tests/vibewine/resolve-adjacency/run.py
python3 -B tests/vibewine/resolve-adjacency/run.py --arch x86_64
python3 -B tests/vibewine/encoder-optimizer-lookahead/run.py
```

The focused runner extracts the actual optimizer driver and complete production
relation/signature/dependency methods. It reuses existing production-layout
adapters and links `dxmt_deptrack.cpp` unchanged. Texture access calls use the
same source-Read/destination-Write pattern as `resolveTexture`; fences come
from actual tracker accesses rather than hand-populated dependency sets.
The texture/native adapters do not issue Metal operations.

54 scenarios cover compute and blit destination writes/readers, unrelated live
work, distances 2/3/63/64/65 with inert Null slots, previous completed adjacent
folds, adjacent Null-only gaps, multi-attachment resolve chains, an unmatched
live Resolve, event/timestamp stops, retained dependency stops and unchanged
ordinary render merging across an independent unmatched Resolve. Live IDs
advance with physical positions; the intermediate destination access is next
to Q so its tracker wait stays within the real 64-ID dependency lane. Original
encoder array identity, destination attachment/store changes, future waits,
completion sets and optimization counts are checked. The inherited pass helper
and residency cases also run.

Current default/off/bounded/combined-experiment selections must pass.
`96940d73ac28f57fe12df832d246d3e84965f2cd` must fail in default and bounded
selection on the real destination-reader dependency. Six connected driver
mutations must fail behavioral assertions: disabling the live guard, treating
Nulls as live, treating absorbed resolves as live, bypassing the guard at
position 64, dropping the suppressed-resolve dependency stop, and extending the
guard to unrelated relation kinds. Sanitizer/compiler failures do not count as
negative-control acceptance. ASan/UBSan cover every executable; scratch is
removed at exit, with 90/30-second compile/run timeouts. There is no producer,
retention change or installed runtime mutation.

The separate `gpu/guest.cpp` is source preparation for the root's isolated
matched-build check. GPU execution, actual candidate mappings and readback
results remain the root's responsibility. A native R(S red MSAA) → X(D green) →
Resolve(S→D) sequence must produce red, including compute/blit routes; source
and CPU tests alone cannot claim that result, FPS or deadlock.

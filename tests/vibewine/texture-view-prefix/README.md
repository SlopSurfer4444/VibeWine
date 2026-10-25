# Requested-prefix texture views

Base: `a21579fb2a03aef5fbe93d7bcba1ea882e63a192`.

Texture::view previously prepared every registered descriptor when an
allocation's view epoch differed from the descriptor epoch. It now asks
prepareAllocationViews for the requested index, materializing only the contiguous
prefix through that index. This is a default source change with no new switch,
sparse cache, replacement view or resource eviction.

For N registered descriptors and first requested index K, old work was N
TextureView wrappers / N-1 native newTextureView calls; new work is K+1 wrappers /
K native calls. Slot zero still wraps the original native texture. The fixture's
N=9, K=2 case measures 9/8 -> 3/2; K=8 remains 9/8. A later higher request appends
only its missing prefix. Registering an unused tail descriptor no longer forces
that view to exist when an earlier key is used.

## Owner and consumer contract

- allocation.version_ equals cached_view_.size(): both count the successfully
  materialized contiguous prefix. Advance the count after each successful push.
  A wrapper or small_vector allocation failure leaves earlier appended views
  valid, and retry resumes the first missing descriptor rather than duplicating
  them. The predecessor's final-only epoch assignment did not preserve that
  invariant after a partial exception.
- Every existing TextureView heap object, allocation pointer, key, GPU resource
  ID and residency state survives future appends. Rc handles can move when the
  vector grows; no vector-element address stability is promised.
- Texture::createView, checkViewUseArray and checkViewUseFormat retain descriptor
  append/dedup behavior. The descriptor shared lock and encode-thread-only
  ownership of mutable allocation state remain controlling. This does not add
  general concurrent lookup support.
- Shader binding consumes gpuResourceID (`dxmt_context.cpp:359-363,399-402`);
  makeResident consumes the same view's residency (`dxmt_context.hpp:603-610`).
  Clear/attachment matching consumes TextureView identities/keys; resolve can
  explicitly request a view on an older allocation (`dxmt_context.cpp:1744`).
  No other production consumer directly reads these private cache/version fields.
- Native newTextureView returning nil keeps its existing nonnull cached wrapper
  containing nil texture/GPU ID zero and is not retried. This is NOT a contract
  for a failed Wine Unix call: that thunk's output fields are not initialized.
  No bridge-error policy was changed.
- Native views are still owned through TextureViewRef/Rc by their allocation and
  released with it. There is no new data producer, log, persistent cache or
  retention mechanism.

## Verification and limits

The source fixture extracts actual TextureViewDescriptor/Key, TextureView and
TextureViewRef classes, actual view constructors/refcount methods, and Texture
prepare/create/lookup/alias/rename methods. It compiles actual small_vector,
intrusive Rc, Metal wrappers and CheckResourceResidency, intercepting native
newTextureView/retain/release. Texture/TextureAllocation setup and the Windows
mutex wrapper are substituted; their relevant real declarations were separately
reviewed. No complete production translation unit, Metal driver or Wine runtime
is exercised by this fixture.

Both runs passed ASan/UBSan:

```
python3 tests/vibewine/texture-view-prefix/run.py
python3 tests/vibewine/texture-view-prefix/run.py --arch x86_64
```

Candidate checks include low/high/lower/repeat request order; highest-first and
single-descriptor negative controls; changed descriptor epoch; rename/new
allocation and explicit old-allocation lookups; format/array alias deduplication;
exact native parent/format/type/mip/slice/swizzle arguments and GPU IDs; heap view
identity and residency across actual small_vector growth; descriptor lock
exclusion; native nil caching; native reference balance; and real C++ wrapper /
vector-reserve allocation failure, temporary native release and resumable prefix.

The immutable predecessor passes its previous count expectations and is rejected
by the unchanged low-index work target. Baseline mode intentionally excludes the
new partial-exception-prefix contract; only the candidate exercises those failure
checks. Seven connected production-code mutations are rejected by behavioral
assertions without sanitizer faults: eager tail, reset prefix, missing descriptor
lock, wrong native format/parent/GPU ID, and publishing the epoch only after all
appends (caught after partial reserve failure).

Independent read-only review found no blocker in the implementation, consumer
contracts or fixtures. Full matched PE/native build and real workload acceptance
remain with the integration owner. Frequency of unused tail descriptors in DS3
and any FPS effect are unknown; reduced call counts are not a measured FPS gain.

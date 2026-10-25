# Staging texture copy command coalescing

Bound to base `a21579fb2a03aef5fbe93d7bcba1ea882e63a192`.

The staging-to-staging branch of `CopyTextureBitcast` emitted one buffer blit per
row. For identical-format, identical-byte-width, full tightly packed rows in
distinct native buffers, it now emits one buffer blit per copied image. The same
existing encoder, resource-use calls, deferred Rc captures, two whole-buffer
accesses, allocation offsets, image strides and flush promotion remain in place.
There is no new copy engine/helper: the existing command writer emits a longer
contiguous span. BC handling and all other copy branches are unchanged.

Eligibility requires source/destination X=0, copied row bytes equal both row
pitches, and each image stride large enough for the copied rows. Padding,
partial rows, different formats/texel widths and same native handles keep the
original row commands. Image gaps are never copied. The Metal blit payload is
unchanged: this reduces command construction/encoding work, not copied bytes.

Source eligibility is broader than a ring-buffer concern: each StagingResource
constructs its own Buffer (`dxmt_staging.cpp:16`), then allocates an ordinary
BufferAllocation (`:129-133`); this is not the shared RingBump allocator. CPU
placed buffers on i386 also own separately allocated memory. Different staging
subresources are independently constructed in `d3d11_resource_staging.cpp:123`.
Row pitch is Metal-aligned (`d3d11_resource_helper.cpp:627-635,672-680`), so only
widths with no resulting row padding qualify. Actual staging-to-staging call
frequency and eligible fraction in DS3 remain unknown; no FPS gain is claimed.
The distinct-buffer premise must be revisited if staging adopts shared/aliased
backing allocation.

## Verification

`run.py` extracts the FULL production `CopyTextureBitcast` method, compiles it
with the actual wire command structures, intrusive Rc and alignment helper, and
substitutes the native/context boundary. The fixture executes its deferred
staging branch, checks exact wire commands/access ordering/offsets/ownership and
flush promotion, and applies emitted copies to sentinel data. Its independent
row-by-row reference checks every payload and padding byte. Explicit per-case
eligibility is an oracle, not a duplicated production predicate.

Both commands passed with ASan/UBSan:

```
python3 tests/vibewine/staging-copy-coalescing/run.py
python3 tests/vibewine/staging-copy-coalescing/run.py --arch x86_64
```

Each architecture covers 18 scenarios: H=12/D=1 produces 12 -> 1 commands;
H=5/D=3 produces 15 -> 3. Cases include unequal image strides, nonzero Y/Z and
allocation offsets, H=1, source/destination row padding, partial X, same backing,
format/byte-width mismatch, unsupported BC, packed depth/stencil raw copies and
empty defensive dimensions. X-at-exact-pitch and undersized-image-stride cases
are synthesized defensive inputs to exercise guard rejection; the real
TextureCopyCommand constructor normally rejects those incompatible dimensions.

The immutable predecessor passes its old work counts and fails the unchanged
coalescing target. Eleven connected source controls are rejected by behavioral
assertions: disable grouping, ignore distinct backing, ignore row pitches,
ignore format, ignore texel width, ignore X origin, ignore image extent, emit
only one row's length, omit UseCopySource, omit flush promotion, and omit source
suballocation offset. Negative controls must fail before any sanitizer fault.

The source fixture does not execute Metal/Wine, the real dependency tracker,
or full D3D TextureCopyCommand construction. Full matched build and controlled
native/game validation remain integration-owner work. No runtime/install/game
cache changes were made.

## Related deep dispositions

**Depth/stencil initialization:** `initDepthStencilWithData` (`dxmt_resource_initializer.cpp:311-384`)
performs two updateContents calls per texel: 2WH bridge calls and (managed upload
storage) 2WH didModifyRange notifications, while emitting two GPU blits. This is
initialization/loading work; steady-state FPS relevance is unmeasured. After the
initializer transaction repair, two reusable row scratch arrays can reduce this
to 2H updates with about 5W scratch bytes. Preserve D24's existing float division,
D32 depth bits including NaNs/signed zero, stencil byte positions, source pitch,
destination offsets/pitches, W meaningful stencil bytes versus aligned padding,
blit options, dimensions/mip/slice and sequence/retention. Do not mutate this
surface alongside the transaction owner. Both command nodes are linked before
GPU allocation/type initialization in the base; failure at second-node/second-
buffer acquisition must be resolved first. The existing general row upload
(`:282-295`) is the simpler reuse pattern; the GPU DepthStencilBlitContext depends
on the main rendering context and is not an initializer-queue drop-in.

**Eager texture views:** `Texture::view` (`dxmt_texture.cpp:256-260`) currently
materializes every registered descriptor at the first allocation lookup. Slot 0
wraps the original texture; each remaining slot allocates a TextureView and
crosses native newTextureView. N registered descriptors cost N wrappers and N-1
native views, even if only an early index K is requested. Requested-prefix
materialization through K would cost K+1 wrappers and K native views while
preserving contiguous indexing and existing version-prefix semantics. It must
preserve each existing TextureView object/residency/key/GPU ID, encode-thread
ownership, descriptor locks, and old-allocation lookup. A vector element address
is not promised stable across vector growth. Highest-index requests save no work;
low-after-high and new-descriptor-epoch controls are required. Sparse holes/cache
replacement are outside this bounded proposal.

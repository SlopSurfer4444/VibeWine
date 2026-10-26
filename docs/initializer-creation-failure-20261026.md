# D3D11 resource creation and checked initializer results

The buffer and device-texture factories admit a resource only after all required
initialization calls succeed. `InitStatus::OutOfMemory` returns `E_OUTOFMEMORY`;
other rejected initialization returns `E_FAIL`. Failures leave the caller output
null. Host `std::bad_alloc` inside these factories also returns `E_OUTOFMEMORY`.
The existing device-level `MTLD3DError` mapping remains unchanged.

## Ownership and compatibility

Buffer construction is separated from `Initialize()`. A local `Com<D3D11Buffer>`
owns the object while allocation, initial-data upload or zero initialization,
rename and dynamic-buffer setup run. Native buffer validity is checked before
upload or zero initialization. On success, the local COM reference transfers to
the output without changing its public reference count.

The texture factory checks both the allocation wrapper and its native texture
before rename or initialization. It visits subresources in the existing order,
passing the same pointer, pitches and format flags as before, and stops at the
first rejection. Normal, renamable depth/stencil and shared paths retain their
existing allocation flags and selection criteria.

Earlier accepted subresources may remain queued when a later one fails. The
initializer owns those allocation references through its existing reference
tracker until encoding/retirement; returning failure does not cancel or replay
them. The temporary texture descriptor and COM wrapper can be released because
queued commands contain native handles and copied dimensions. Neither command
encoding nor allocation destruction dereferences `TextureAllocation::descriptor`.
A strong allocation-to-descriptor link would introduce a cycle with
`Texture::current_` and is deliberately unnecessary here.

Shared textures initialize completely before creating the keyed mutex,
D3DKMT resource or bootstrap registration. The COM owner is allocated before
`D3DKMTCreateAllocation2`; successful handles are immediately adopted by a
`noexcept` setter. Bootstrap registration occurs last. If that registration
fails, the existing `DeviceTexture` destructor destroys the D3DKMT resource.
There is no subsequent host allocation or initialization after successful
texture-name registration and before output publication. A requested keyed
mutex that cannot be created now fails the factory rather than returning a
resource with no mutex.

A scoped guard owns the fresh `allocation->machPort` on the shared-creation
path until successful bootstrap registration and COM output publication. Every
earlier return or host exception releases that send right once through the
matched `WMTMachPortDeallocate` bridge. It does not alter borrowed/imported port
ownership or successful shared-resource retention. Pending commands retain the
native texture independently of the Mach send right, so releasing an unpublished
right does not invalidate a queued initialization prefix.

## Evidence boundary

The focused `tests/vibewine/initializer-creation` fixture extracts production
creation methods and the full texture factory. Allocation/native/platform
endpoints and COM base classes are mocks. Its runner documents the executed
scenarios and source bindings. This establishes caller control flow and mocked
ownership accounting; it does not establish natural native/GPU OOM recovery,
full PE/native ABI integration, installed behavior or gameplay performance.

The caller source requires the matching `WMTMachPortDeallocate` PE/native bridge
provided by the repair integration owner. These mocked checks do not prove real
Mach-right release; root owns that native check and matched build. The existing
bootstrap API has no explicit unregister operation. Successful shared-resource
bootstrap-name lifetime and internal failure/exception cleanup in
`KeyedMutex::create` remain outside this caller patch.

The guard begins only after `Texture::allocate` returns its allocation wrapper.
Native shared allocation creates the right before constructing that wrapper, so
an exception in `TextureAllocation` construction is an earlier ownership edge
that this caller guard cannot observe. It is reported to the integration owner
for allocation-level exception cleanup; no caller-only claim covers that seam.

## Artifact ownership and retention

The initializer-repair owner consumes these sources and focused tests. The
fixture creates only private temporary build output and removes its exact owned
directory on exit. Temporary raw evidence is bounded by the campaign's existing
7-day / 64 MiB cap with active and unresolved-evidence holds; source, this note,
test inputs and compact verification receipts remain reviewable results. No
collector or cleanup service is installed. Root owns canonical integration,
matched builds, GPU checks, Git staging and commit.

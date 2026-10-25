# Exact-repeat D3D11 setter guards

Base: `a21579fb2a03aef5fbe93d7bcba1ea882e63a192`. Three local guards remove
repeated state work using existing D3D11 state under its existing lock:

- `IASetInputLayout` compares the converted internal interface pointer before
  assignment and pipeline invalidation, including null/null. It still performs
  conversion; a failed conversion clears an existing layout as before.
- `SetShader` returns for null/null only when `NumClassInstances == 0`.
  Nonzero unsupported class-instance diagnostics remain. Nonnull exact repeats
  already had an identity guard and retain that path.
- In the existing nonreplacement VB branch, update effective stride/offset and
  emit `bindVertexBufferOffset` only if either changed. Hazard validation and
  BindingSet identity resolution still run first. Existing dirty bits remain
  untouched; null unbind, resource replacement and changed metadata still work.

No additional pointer cache, feature switch, state owner or retention producer
is introduced.

## Observed operation counts

The unchanged production methods first reproduced these failures against the
immutable base before editing production source. The same fixture then passes
with the guards. Counts are CPU state transitions/state-command emissions, not
Metal calls, shader compilation counts or FPS:

| Production-bound scenario | Base | Guarded |
|---|---:|---:|
| 100 identical input layouts, ready pipeline before each call: ready→active transitions | 100 | 0 |
| 100 null pixel shaders, ready pipeline before each call: ready→active transitions | 100 | 0 |
| 100 identical VB calls: emitted offset/stride state commands | 100 | 0 |
| Three VB slots with only the middle stride changed: emitted offset/stride commands | 3 | 1 |

`InvalidateRenderPipeline` already does nothing outside ready pipeline states.
Repeated invalidations without intervening draws therefore coalesce naturally.
After an invalidation, production finalization can revisit descriptor creation,
pipeline-cache lookup and state emission (`context_impl.cpp:4898-4939`). Lower
`TrackRenderPipelineState` can independently suppress a duplicate native PSO
command. The table does not claim every saved transition previously caused a
native PSO change.

## Lifecycle and source-bound checks

The native fixture compiles exact extracted production setter bodies,
`InvalidateRenderPipeline`, `InvalidateComputePipeline`, `InvalidateCurrentPass`,
`UpdateVertexBuffer`, `ClearState`, both reset helpers, full immediate
`ExecuteCommandList`, and full deferred `FinishCommandList`. It includes actual
BindingSet, Com and Rc implementations and the production VB declaration/trait.
It also compiles production encoding-context VB setters, buffer-allocation
`access`, and full `encodeVertexBuffers` body. Counter wrappers only surround the
native VB setter bodies. Native allocation endpoints, COM objects, device,
command-list storage and residency tracking are counted fixture substitutes.

Seven groups exercise exact repeats; changed layout/shader/stride/offset;
nonnull shader coalescing; null unbind and repeated null; failed layout
conversion; unsupported class diagnostics; same-buffer hazard rejection; mixed
slots; dirty preservation; and the following lifecycle boundaries:

- **ClearState:** actual reset helpers end the pass, enqueue encoding clear, and
  reset D3D state. A new VB bind remains a full bind; repeated null layout does
  not cancel an already queued clear.
- **Deferred callers:** actual immediate execute and deferred finish method
  bodies run with both restore flags. Fixture command lists change/clear encoding
  state; the exact production VB restore block restores buffer/offset/stride.
  Finish records restoration in the newly created list. Repeat guards preserve
  the explicit restoration commands. Non-VB resource restoration bodies are
  outside this focused fixture and omitted.
- **Encoder first use/reopen:** the exact `SwitchToRenderEncoder` preamble,
  through its independent resource/VB dirty marks, runs before the unchanged
  production upload method. Exact repeats leave these dirty marks live and both
  initial/reopened uploads occur. Native render-pass creation and full PSO
  recreation are outside the fixture; ready states are selected by the adapter.
- **DISCARD:** the actual immediate DISCARD case and `MapDynamicBuffer` helper
  dirty VBs and enqueue allocation rename/suballocation updates. Subsequent exact
  VB repeats add no offset command, and actual `access`/`encodeVertexBuffers`
  observe changed GPU addresses and suballocation offsets on all three uploads.
  Deferred DISCARD's independent dirty mark and rename path were source-inspected
  (`context_def.cpp:110-142,163-186`); its map helper is not executed here.

`ArgumentEncodingContext::bindVertexBufferOffset` only writes offset and stride
(`dxmt_context.hpp:504-507`); it neither refreshes allocations nor establishes
residency/synchronization. Stored Buffer identity observes its current allocation
through production `access` (`:359-363`). Thus DISCARD does not rely on the removed
exact-repeat command. Neither immediate nor deferred `EmitST` marks a GPU
operation; their state append operations are separate from immediate `EmitOP`'s
operation flag (`context_imm.cpp:28-39`, `context_def.cpp:34-42`).

## Hypothesis disposition

| Candidate | Disposition |
|---|---|
| Repeated input-layout assignment invalidates PSO | Implemented with converted interface identity and null/change controls. |
| Repeated null shader invalidates PSO | Implemented only without class instances; diagnostic path retained. |
| Repeated nonnull shader invalidates PSO | Already coalesced; no new guard. Never compare ManagedShader alone: distinct SO wrappers may return null. |
| Same VB identity always allows skipping call | Rejected. Validation, stride/offset changes and null/replacement paths matter. |
| Exact VB stride/offset emits a redundant command | Implemented inside existing nonreplacement branch, after validation. |
| Dirty VB implies repeated offset command is required | Rejected. Independently set dirty state drives the actual upload; offset command has no allocation side effect. |
| Topology, SRV, sampler and constant-buffer repeats | Existing identity/value checks already coalesce exact repeats; retain changed-range/offset handling. |
| Blend setter repeats | Pipeline invalidation is already conditional; blend-factor dirtying remains. No tested extension in this change. |
| Rasterizer state repeats | Nonnull identity already guarded; null still dirties state. Held without expanding lifecycle scope. |
| Depth/stencil or index-buffer repeats | Held: no production-bound proof for their full effective-state/encoding contract in this fixture. |

## Verification

From component root:

```sh
python3 tests/vibewine/exact-setters/run.py --sanitize
python3 tests/vibewine/exact-setters/run.py --arch x86_64 --sanitize
python3 tests/vibewine/exact-setters/run.py --baseline-ref a21579fb2a03aef5fbe93d7bcba1ea882e63a192 --sanitize
```

2026-10-26: native arm64 and x86_64 ASan/UBSan each pass 7/7 groups. Immutable
baseline requires six exact repeat-related assertion failures and passes the
independent changed/null/mixed-slot control group. Unrelated failures and
sanitizer crashes are rejected. `git diff --check` passes. Temporary generated
sources/executables are private and automatically removed on runner exit; source
hashes and operation counts are printed.

Candidate `context_impl.cpp` SHA-256:
`52534f9046d33d0b1c31373a1d5cd7ab140aaec2b16fff3f7a99873912301379`.
Baseline SHA-256:
`4459931bb0f9d1c5be9d287078ac9bd917cdebcf1dcef634d0e6214ff91d16ea`.

This is a source/CPU correctness and operation-count result. No matched
production build, native device/encoder, GPU buffer readback, game session or
FPS comparison was run. Root owns canonical integration, index, production
build, GPU acceptance and publication. All task changes remain unstaged and
uncommitted in the isolated source worktree.

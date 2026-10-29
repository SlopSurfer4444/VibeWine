# Exact-repeat D3D11 setter guards

Base: `a21579fb2a03aef5fbe93d7bcba1ea882e63a192`. Four local guards remove
repeated state work using existing D3D11 state under its existing lock:

- `IASetInputLayout` compares the converted internal interface pointer before
  assignment and pipeline invalidation, including null/null. It still performs
  conversion; a failed conversion clears an existing layout as before.
- `SetShader` returns for null/null only when `NumClassInstances == 0`.
  Nonzero unsupported class-instance diagnostics remain. Nonnull exact repeats
  already had an identity guard and retain that path.
- `IASetIndexBuffer` compares effective resource identity after hazard validation;
  format/offset are always updated. See the dated index-buffer evidence below.
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

2026-10-26 candidate `context_impl.cpp` SHA-256:
`52534f9046d33d0b1c31373a1d5cd7ab140aaec2b16fff3f7a99873912301379`.
Baseline SHA-256:
`4459931bb0f9d1c5be9d287078ac9bd917cdebcf1dcef634d0e6214ff91d16ea`.

This is a source/CPU correctness and operation-count result. No matched
production build, native device/encoder, GPU buffer readback, game session or
FPS comparison was run. Root owns canonical integration, index, production
build, GPU acceptance and publication. All task changes remain unstaged and
uncommitted in the isolated source worktree.

## 29 October: index-buffer identity economy

`IASetIndexBuffer` now validates the requested resource and normalizes an invalid
binding to null before comparing effective resource identity. A changed binding
still emits `bindIndexBuffer`; an unchanged binding skips the COM assignment,
Buffer capture and state command. Format and offset are stored unconditionally,
including null and rejected resources. No cache or new state is added.

The production encoding setter only assigns `ibuf_`. The production indexed draw
captures format/offset separately and obtains the current allocation/suballocation
through `currentIndexBuffer` and `access` at execution. Existing explicit index
restoration remains in `RestoreEncodingContextState`. Skipping the redundant
setter therefore does not replace DISCARD, tracking or deferred restoration.

The fixture now extracts those production methods, `IAGetIndexBuffer`, ordinary
`DrawIndexed`, the index restoration block, and both actual `forward_rc`
implementations. Command-list execution uses deferred copying so captured Buffer
references survive replay. Its synthetic deferred recording boundary starts
with ClearState; it does not assume that an immediate binding exists in a new
command list.

With 100 identical setters interleaved with 100 executed indexed draws, immutable
base `61aca2a3621386f1c6777972327149e48675883e` emits 100 state commands and performs
100 COM AddRef/Release pairs and 100 Buffer Rc retain/release pairs. Candidate
performs zero of each. Both execute all 100 draws, track all 100 accesses, and
perform 101 hazard validations including the initial bind. Final COM/Rc counts
are equal. This is a deterministic operation-count result, not FPS or measured
production CPU time.

Four index groups check changed format/offset, getter state, null/nonindexable
resources, same-resource hazard rejection, exact draw packets and 11 decoded
index/vertex values from separate backing bytes, ordered allocation rename,
same-allocation suballocation change, the actual immediate MapDynamicBuffer and
DISCARD path, ClearState, both command-list restore flags, and replay twice.
Native arm64 and x86_64 ASan/UBSan pass all eleven groups. The current immutable
base fails only the two index work-saving assertions, after covered data checks.
Seven source-connected mutations are rejected: unconditional emission, omitted
hazard validation, omitted format, omitted offset, missing null unbind, missing
index restoration, and omitted allocation suboffset.

```sh
python3 tests/vibewine/exact-setters/run.py --sanitize
python3 tests/vibewine/exact-setters/run.py --arch x86_64 --sanitize
python3 tests/vibewine/exact-setters/run.py --index-only --baseline-ref 61aca2a3621386f1c6777972327149e48675883e --sanitize
python3 tests/vibewine/exact-setters/run.py --mutation drop-offset --sanitize
```

Each other mutation is named by `--help`; `--index-only` selects the four new
index groups. The runner binds the extracted fixture, test sources and included
production headers by SHA-256 and halts on UBSan errors. Baseline failures must
match exact assertions; mutations must reach their connected target case and
fail through an assertion, with sanitizer crashes rejected.

Limits: native GPU objects, full D3D context/device, hazard-overlap implementation,
PreDraw, GS/TS/indirect consumers and deferred MapDynamicBuffer are substituted or
source-inspected. Ordinary draw packet/data parity is CPU evidence; no native
GPU/game/FPS or full production-build acceptance is claimed. Root owns those
integration/runtime checks. No production retention behavior changes; temporary
fixture sources/executables are automatically removed. Private branch logs are
seven days/32MiB and curated results thirty days/8MiB, with active integration and
required recovery holds; cleanup belongs to the root integration owner.

### Combined queue-failure interface compatibility

The combined source `5847d276c13e77c11c740884bde115190fb714c5` adds terminal
queue guards to methods already extracted by this fixture. Its old vector-backed
`AllocateCommandData` substitute could not represent the new nullable result;
it also lacked the queue alias and `CommandQueueFailed` API. This fixture repair
changes only test interfaces and preserves every production guard.

The runner now extracts the actual `moveonly_list`, queue failure enum/status
method and, when present in the selected implementation, `CommandQueueFailed`.
Fixture-owned raw storage outlives the move-only list and its element destruction;
boxed callables allow the existing `std::function` endpoint to retain move-only
captures. The queue scheduler/Fail endpoint remains a first-error interface
substitute. No native queue implementation is claimed by this fixture.

All twelve default groups pass on native arm64 and x86_64 ASan/UBSan, including
the original 100→0 command/COM/Rc counts, 100 draws/accesses, data/getter/hazard,
DISCARD, restore flags and replay checks. The new group executes full production
`ExecuteCommandList` with a healthy nonempty visibility-query list, then checks
terminal admission, null-list allocation and query-clone `bad_alloc` rejection.
It also exercises the actual failed render-reopen guard and the extracted DISCARD
case's refusal to publish pointer/pitches after injected recording failure.
Four additional source mutations remove admission, clone-failure publication,
render-failure or Map-publication guards; each must fail its connected case.

The original immutable `61aca2a3` baseline and seven index mutations remain valid.
Baseline selection replaces only `context_impl.cpp`; current immediate/deferred
bodies and queue/list interfaces remain bound in reported source hashes. Its
`--index-only` mode runs the four historical index groups and does not assert
queue-failure behavior absent from that older implementation. No source guards
are erased to compile either selection.

Native queue scheduling, actual host-allocation OOM, queue wake/publication
semantics and real native destruction remain the queue owner's separate tests.
The fixture's queue state, allocation failure and command-recording failure are
explicit substitutes. These tests establish context-consumer compatibility and
failure branching, not native GPU/game/runtime acceptance.

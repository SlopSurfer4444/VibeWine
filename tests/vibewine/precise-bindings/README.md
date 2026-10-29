# Precise buffer-binding invalidation after DISCARD

`DXMT_PRECISE_BINDINGS=1` makes immediate/deferred dynamic-buffer
`WRITE_DISCARD` invalidate only currently bound constant-buffer, vertex-buffer
and buffer-SRV slots referring to that buffer's canonical `Buffer` identity. The helper scans existing bound
entries. It has no dependency registry, persistent state, allocations or retained
references. Undefined/zero selection retains the historical broad invalidation.
The enclosing Meson option is `vibewine_precise_bindings`, default `false`.
Texture SRVs do not match a buffer DISCARD. Dynamic texture invalidation,
renaming, suballocation, shader/encoder invalidation and resource-table encoding
are unchanged. There is no application-name check.

This only avoids rebuilding unrelated stage tables. A referenced changed CB
still rebuilds its complete stage table. The dependency scan itself has a cost;
this is an opt-in source experiment, without an established game FPS benefit.

## CPU production regression

```sh
python3 tests/vibewine/precise-bindings/run.py
python3 tests/vibewine/precise-bindings/run.py --arch x86_64
```

The runner copies these production bodies verbatim into a temporary fixture:

- Both complete dynamic-buffer `Map` branches, including DISCARD error handling
  and `NO_OVERWRITE`; both complete `MapDynamicBuffer` methods.
- All three precise helpers, actual CB/VB/SRV representations and redundancy traits.
- Full `encodeVertexBuffers`, `bindVertexBuffer`, buffer-SRV `bindBuffer`, the
  complete production SRV serializer switch case and its command-emission tail.
  Sampler/UAV cases are outside this extraction; texture access endpoints are
  rejection adapters because these scenarios use raw and typed buffer SRVs.
- `DynamicBuffer` class and constructor/destructor, allocation, rename and
  suballocation methods, including its completed-allocation trimming helper.
- `SetConstantBuffer`, argument preallocation, shader-stage upload, CB binding,
  CB offset binding and `encodeConstantBuffers`.
- The immediate execution loop that publishes deferred buffers' latest names.

It includes actual production `BindingSet`, bitsets, intrusive `Rc` and shader
reflection declarations. Platform/COM objects, buffer storage, queue, deferred
list container and Metal command/access/residency boundary are CPU adapters.
The immediate adapter consumes captures once; the deferred adapter keeps them
for replay, matching the two production `forward_rc` contracts.

Fifty-one scenarios cover every shader stage on immediate/deferred paths;
bound A, unrelated B and unbound C; distinct resource wrappers sharing one
canonical buffer; same-stage and cross-stage aliases; changing constant offsets;
six successive DISCARDs across suballocations and allocation renames; repeated
deferred replay; interleaved immediate/deferred use of the same resource; null
unbind/dummy/rebind; bound but unreflected slot 13; preservation of existing
unrelated dirty bits; failed Maps; and exact buffer SRV/VB invalidation.
Twenty-four added scenarios use raw and typed buffer SRVs at slots 0/1/64/127,
distinct views/wrappers of one Buffer,
unrelated resources, a texture-view placeholder, unused and unbound aliases,
both context paths and all stages. They exercise actual SRV upload selection,
raw addresses, typed allocation IDs/suballocation metadata and full production
VB serialization through successive DISCARDs. Observations
include exact dirty bits, stage-table counts, retained data read through encoded
addresses, and access/residency call counts for every materialized table.

Default/explicit-off/enabled variants must pass under ASan/UBSan at `-O0`
(the fixture measures correctness, not speed). Historical
source `e11476e33117b454bac60b997532f9823e34855b`, compiled with the enabled define,
must fail the precise contract. Connected mutations must also fail naturally:
missing/wrong dependencies, first-alias-only invalidation, dirtying a complete
slot set, missing immediate/deferred calls, omitted rename/suballocation
operations, and omitted residency. The 28 October source `e3845197` separately
must fail
the new exact SRV/VB contract. Connected new negatives remove VB/SRV dependence,
broaden their dirty sets, stop after the first SRV alias, drop upper-qword SRVs,
remove either immediate/deferred resource call site, or remove raw SRV residency.
A sanitizer error or compilation failure
cannot count as an accepted negative control.

The runner prints source, fixture and runner hashes with every outcome. One
private temporary directory owns all generated sources/binaries and is removed
on exit. Compile/run limits are 60/20 seconds per variant. No permanent producer,
capture, cache, game or runtime state is created. Caller-retained JSON output
belongs to the enclosing experiment's existing evidence/retention policy.

## Limits and integration gates

Texture/staging portions of `Map`, production context mutex ownership,
`FinishCommandList`/`ExecuteCommandList` restore boundaries, production queue,
hazard optimizer, real residency and actual GPU execution are outside this CPU
fixture. The unrelated idle-retirement experiment is disabled in this fixture;
its integration with this option requires the matched combined build/tests.
CPU data-address checks establish the covered ordering, not displayed frames or
full D3D11 behavior. VB upload selection in the added scenarios is a CPU adapter;
actual `UpdateVertexBuffer` and command-list lifecycle continue to be checked by
`../exact-setters/run.py`. The added SRV path uses actual shader upload selection,
SRV-case serialization and emission; sampler/UAV cases, real native typed-view
creation, texture serialization, hazard validation and GPU execution are excluded.
The same-buffer whole-range reset has its own `../cb-range-reset/run.py` regression;
none of these CPU fixtures proves shader NumConstants/OOB-zero enforcement.
Simultaneous multi-stage SRV aliases and unrelated resource B data readback are
not exercised by the added SRV scenarios; their per-stage dirty masks are checked. Real PE compilation with production headers and both gate
values is a separate check; the integration owner owns matched builds and GPU
regressions. Installation and game performance acceptance remain separate.

A later bounded trace should count DISCARDs, actual dependent stage masks,
unrelated table rebuilds avoided, CB entries traversed, argument bytes and CPU
encoding time. Stop promoting this direction if almost every rebuild is needed,
encoder/shader changes dominate, or the dependency scan costs more than it saves.
Keep general UAV table caching separate: encoding also performs required access,
residency, retention and counter-allocation work, so replacing `uav_bound` with a
dirty-bit condition is unsafe.

## Combined queue-interface fixture compatibility

The publication source `35d1f00403efa35482420722dbd4165e3cc9b967` adds
`CommandQueueFailed` to argument reservation and reads queue failure after the
immediate dynamic-map helper. The CPU Queue adapter now models healthy status and
first-error retention; the runner extracts the actual `CommandQueueFailed` helper
when present. Production argument/Map guards remain in generated candidate code.
Historical sources without that helper keep their historical bodies unchanged.

One additional scenario checks healthy argument reservation, rejection after a
terminal queue error without advancing argument storage, first-error retention,
and unchanged mapped pointer/pitches when injected command-recording failure is
observed after the actual map helper. Two generated source mutations remove only
the argument-admission or post-map-publication guard and must fail connected
assertions. Existing defaults, explicit gate choices, historical baselines,
CB/VB/SRV data and dirty-mask controls and all existing mutation expectations are retained.
The current positive variants run 52 scenarios; original failure contracts remain
unchanged. The CB range-reset fixture imports these repaired adapters and retains
its original setter/getter/reset/replay assertions and negative expectations.

Queue failure/recording endpoints are explicit CPU substitutes. This does not
exercise native queue allocation/OOM, wake/cancellation or GPU execution; those
remain the queue owner's tests. No production source, installed runtime, game,
metrics or Git-index state is changed by this fixture repair. Generated scratch
still uses the existing TemporaryDirectory lifetime and enclosing retention rules.

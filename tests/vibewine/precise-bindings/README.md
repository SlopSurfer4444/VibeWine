# Precise constant-buffer invalidation after DISCARD

`DXMT_PRECISE_BINDINGS=1` makes immediate/deferred dynamic-buffer
`WRITE_DISCARD` invalidate only currently bound constant-buffer slots referring
to that buffer's canonical `Buffer` identity. The helper scans existing bound
entries. It has no dependency registry, persistent state, allocations or retained
references. Undefined/zero selection retains the historical broad invalidation.
The enclosing Meson option is `vibewine_precise_bindings`, default `false`.
SRV/VB invalidation, renaming, suballocation, shader/encoder invalidation and
resource-table encoding are unchanged. There is no application-name check.

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
- The precise helper, `CONSTANT_BUFFER_B` representation and redundancy trait.
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

Twenty-seven scenarios cover every shader stage on immediate/deferred paths;
bound A, unrelated B and unbound C; distinct resource wrappers sharing one
canonical buffer; same-stage and cross-stage aliases; changing constant offsets;
six successive DISCARDs across suballocations and allocation renames; repeated
deferred replay; interleaved immediate/deferred use of the same resource; null
unbind/dummy/rebind; bound but unreflected slot 13; preservation of existing
unrelated dirty bits; failed Maps; and unchanged SRV/VB invalidation. Observations
include exact dirty bits, stage-table counts, retained data read through encoded
addresses, and access/residency call counts for every materialized table.

Default/explicit-off/enabled variants must pass under ASan/UBSan at `-O0`
(the fixture measures correctness, not speed). Historical
source `e11476e33117b454bac60b997532f9823e34855b`, compiled with the enabled define,
must fail the precise contract. Connected mutations must also fail naturally:
missing/wrong dependencies, first-alias-only invalidation, dirtying a complete
slot set, missing immediate/deferred calls, omitted rename/suballocation
operations, and omitted residency. A sanitizer error or compilation failure
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
full D3D11 behavior. Real PE compilation with production headers and both gate
values is a separate check; the integration owner owns matched builds and GPU
regressions. Installation and game performance acceptance remain separate.

A later bounded trace should count DISCARDs, actual dependent stage masks,
unrelated table rebuilds avoided, CB entries traversed, argument bytes and CPU
encoding time. Stop promoting this direction if almost every rebuild is needed,
encoder/shader changes dominate, or the dependency scan costs more than it saves.
Keep general UAV table caching separate: encoding also performs required access,
residency, retention and counter-allocation work, so replacing `uav_bound` with a
dirty-bit condition is unsafe.

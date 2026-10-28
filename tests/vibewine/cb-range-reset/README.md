# Same-buffer constant-buffer range reset

The CPU fixture checks that a valid `FirstConstant=16, NumConstants=16` view of
a 512-byte CB becomes `FirstConstant=0, NumConstants=32` when the same resource
is rebound through either plain `*SetConstantBuffers` or explicit
`*SetConstantBuffers1(..., nullptr, nullptr)`. It verifies getter metadata,
exact dirty bits/state-command counts and data read through the actual encoded
CB address at offset zero.

```sh
python3 tests/vibewine/cb-range-reset/run.py
python3 tests/vibewine/cb-range-reset/run.py --arch x86_64
```

The runner reuses `precise-bindings/run.py` extraction and CPU adapters without
editing them. Actual production `SetConstantBuffer`, `GetConstantBuffer`, all
six stages' plain and `*1` setter/getter wrappers, CB representation/BindingSet,
CB offset/full binding, upload and table encoding execute. Both immediate and
replayable deferred capture ownership contracts are exercised. The actual
Execute/Finish restore branch snippets and actual CB restore loop execute with
both restore flags; encoding clear and D3D state reset are CPU boundary adapters.
Full Execute/Finish methods, command-list rollover, queue/COM/native Metal,
mutex integration, shader changes and real GPU execution are outside this test.

Forty-eight scenarios cover every shader stage, both contexts, both reset API
forms and both restore flags. They also cover exact repeats, preservation of
unrelated dirty bits, count-only changes/reset, resource replacement, null
unbind/repeated null/rebind and repeated deferred replay. An additional isolated
count-only scenario connects the count update/dirty negative controls. `NumConstants` is
checked through actual getter/state changes; the current encoder consumes only
the CB address/offset. This fixture does not prove shader out-of-bounds range
enforcement or add such enforcement.

Default, explicit-off and enabled precise-binding variants must pass with
ASan/UBSan. Immutable `8cf59a015b88361ed6f6320eb8a781c68b3b36c1` context
source must compile and fail specifically at reset metadata readback; compilation
or sanitizer errors are not accepted negative controls. Connected mutations
must also fail behaviorally: dropping the encoded offset reset retains stale
data; dropping the nonreplacement count update or its dirty mark fails the
isolated count-only scenario. Source/runner/fixture
and reused-adapter hashes are emitted. Generated sources/binaries live in one
private temporary directory removed on exit; compile/run timeouts are 60/20
seconds per variant. No installed runtime or game state is changed. Any retained
output belongs to the enclosing experiment's evidence policy.

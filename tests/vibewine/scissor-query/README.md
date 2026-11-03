# Scissor query on dirty serialization only

`PreDraw` resolves the effective rasterizer state and calls `IsScissorEnabled`
only inside its existing `DirtyState::Scissors` branch. The getter returns the
state object's immutable `ScissorEnable` field. No cache or invalidation rule
changes. Stable draws avoid a virtual getter when no scissor packet is needed.

From the component root:

```sh
python3 tests/vibewine/scissor-query/run.py
python3 tests/vibewine/scissor-query/run.py --arch x86_64
```

The runner compiles the **full actual production `PreDraw` method**, actual
`RSSetState`, `RSSetViewports`, `RSSetScissorRects`, the exact encoder reopen
dirty-state statement, production `Flags`, and real WineMetal wire structures.
An immutable `a422a9324e6c655e43cfeb53423497e3ca390a0e` PreDraw executes beside
the candidate. The capture receiver compares every packet byte and pointed-to
viewport/scissor payload byte after normalizing process-local payload pointers.
Packet reserved bytes are initialized by the CPU allocator.

Four groups cover:

- 1024 stable draws: exact packet/payload, update/upload-call parity and getter
  calls **1024 -> 1**. This counts executed getters, not production CPU time.
- Explicit rectangle goldens: enabled clipping, inverted/outside rectangles,
  missing scissors, disabled full-target rectangles and viewport-size fallback.
- Actual setter transitions: enable/disable/default reset, repeated nonnull/null
  state, multiple viewports and rects, disabled viewport resizing. The CPU model
  also requests actual production dirty flags for target resize/reopen, a state
  reset/reopen, and ordinary/geometry/tessellation resource-upload selection.
- Queue/pipeline admission refusal, command-data allocation failure with dirty
  state retained, recovery, and indexed draw admission without an index buffer.

The runner first executes the old method as the primary path. All packet and
rectangle controls must pass before it fails exactly `stable getter economy`.
It also compiles a connected mutation forcing scissor enablement false, which
must fail exactly the `enabled scissors` golden. Compilation failures, sanitizer
errors and unrelated assertions cannot count as expected negative results.
Every binary uses ASan/UBSan with recovery disabled; LeakSanitizer is disabled
because it is unsupported on macOS. Compiler/run timeouts are 60/20 seconds.
Source, fixture, runner and baseline SHA-256 hashes are printed.

## Limits

COM conversion, mutex type, state object methods, command-data allocation,
command capture storage, pipeline finalization, vertex/SO updates, resource
uploads and queue failure are CPU adapters. State/scissor/viewport setters and
PreDraw execute from production source, but full ClearState, OM target binding,
render-pass creation, PSO creation and native queue lifetime are not executed.
The reset/target scenarios explicitly supply their boundary state and execute
the extracted production reopen dirty statement; they do not establish full
command-list restoration or GPU lifecycle behavior. State commands are queued
then consumed once; replayable deferred command lists are not exercised.

Real wire layout and CPU behavior do not prove native Metal/GPU output, matched
PE/native build acceptance, installed runtime behavior, game behavior or FPS.
Those remain the integration owner's checks. Generated source/binaries belong
to one private temporary directory removed on exit. No runtime, game, persistent
producer or retained cache is created.

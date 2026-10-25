# Compute setup bridge fusion

`-Dvibewine_compute_pass_fusion=true` enables a default-off experiment separate
from render-pass fusion. It uses the existing synchronous opcode **37** and
command-node ABI. It joins the two stack buffer bindings directly to the recorded
compute head, saving one native crossing per eligible pass. Eligibility requires
nonnil encoder and argument-buffer handles. Undefined/off, nil handles and W4
batching-reference builds retain the original emission path.

Fence waits remain before setup and fence updates after the recorded stream.
There is no intervening compute work or dedup operation to move. Only the stack
setup tail changes; recorded topology stays intact. The native interpreter
initializes `threadgroup_size` for each call, and its `SetBuffer` handler leaves
that state untouched. The buffer-only prefix therefore preserves the original
recorded call's initial state and its subsequent PSO/dispatch transitions.

```sh
python3 -B tests/vibewine/compute-pass-fusion/run.py
python3 -B tests/vibewine/compute-pass-fusion/run.py --arch x86_64
```

The test extracts the whole production emission sequence from fence waits
through endEncoding and the exact PE thunk, and compiles the real Metal.hpp,
wire layouts and binding indices. A synchronous opcode-37 observer serializes
complete command payloads in execution order and models the native interpreter's
per-call threadgroup state. 32 scenarios cover nil handles, fence/no-fence sets,
empty/Nop/direct/full streams, multiple PSOs, all dispatch types, residency,
buffer offsets, byte payload contents, textures, barriers and indirect command
execution. Recorded topology is checked after every traversal.

Default/off/on, combined render+compute and W4 reference selections must retain
identical command sequences. Enabled eligible cases must save exactly one
crossing; excluded cases must retain their count. Connected drop-stream,
double-setup, reversed-stream and bypass mutations must fail the command/count
contract. The bypass preserves commands but loses the crossing reduction.
Both whole compute and whole render emission fixtures are compiled and executed
with both flags set; the render output must equal its render-on/compute-off
output. ASan/UBSan cover every executable.

Scratch lives in one private temporary directory removed on exit; compile/run
timeouts are 60/20 seconds. No producer, persistent data, ABI, allocator, resource
ownership or thread is added. This is CPU observation through an injected native
consumer. It does not execute the real Metal interpreter, GPU or Wine. Matched
build, Metal validation and controlled gameplay measurement remain necessary
before adoption; the crossing reduction does not establish an FPS improvement.

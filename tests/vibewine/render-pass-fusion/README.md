# Ordinary render pass bridge fusion

`-Dvibewine_render_pass_fusion=true` enables a default-off build experiment.
It reuses the existing linked render-command ABI and synchronous opcode 38.
No bridge export, list ownership, allocator, retained storage or thread changes.

For an ordinary render pass with valid encoder and argument buffer, no GS/TS,
no emulated stream output, and no GS/TS argument-marshalling tasks, the five
stack setup bindings are linked directly to the existing recorded command head.
This replaces two `encodeCommands` crossings with one. Fence waits precede the
combined stream; fence updates follow it. Physical residency dedup still runs
on the original recorded list before the temporary link is installed. Only the
stack tail points to that list; the recorded head, tail and links are unchanged.
The native traversal completes before stack storage leaves scope.

GS/TS/SO, marshal tasks, null handles and W4 batching-reference builds retain
their established emission path. In particular, setup must remain before
argument-marshalling operations and their barrier. The default/undefined/off
selection retains the existing two streams.

Run the bounded CPU check:

```sh
python3 -B tests/vibewine/render-pass-fusion/run.py
python3 -B tests/vibewine/render-pass-fusion/run.py --arch x86_64
python3 -B tests/vibewine/pass-optimizations/run.py
```

The runner extracts the whole production render-emission sequence, from fence
waits through `endEncoding`, plus the exact PE thunk and production residency
dedup function. It compiles the real `Metal.hpp`, wire layouts and binding
indices. The counted synchronous native-dispatch adapter observes opcode 38
and serializes every exercised command payload in execution order. It compares
full sequences, not just hashes, across default/off/on and W4 reference builds.
256 cases cover all GS/TS/SO flag combinations, each marshal-task combination
including tasks without flags, null encoder/buffer, empty and nonempty recorded
streams, two fence stages, duplicate residency nodes and a recorded barrier.
Both dedup selections run. Eligible cases save exactly one crossing; exclusions
retain identical crossing counts. The maximal setup uses all 14 slots.

Four connected mutations must fail the command/count contract: dropping the
stream, emitting setup twice, allowing fusion around argument-marshalling work,
and bypassing the optimization while preserving the complete command sequence.
Recorded topology is checked after each synchronous traversal. ASan/UBSan
cover every variant. Scratch source/binaries are private temporary files removed
on exit; compilation and execution have 60/20-second timeouts. No data producer
or retention policy is added.

This verifies CPU command ordering, payloads, boundary counts and stack
lifetimes through an injected native consumer. It does not execute the Metal
interpreter, a GPU, Wine or a game. Matched PE/native build, real Metal command
validation and controlled baseline/candidate/reversal gameplay measurements
remain required before adoption. Fewer crossings do not establish FPS gains.

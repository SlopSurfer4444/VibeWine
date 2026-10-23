# Completed Dynamic allocation retention

Historical verification checkpoint. Original local build/source identities below
remain unchanged; [DXMT provenance](../provenance/dxmt.md) maps them to
code-equivalent public commits. For the latest installation state, see
[current status](../STATUS.md).

`DynamicBuffer` and `DynamicLinearTexture` kept their retired backing-allocation
FIFO at its historical highwater until destruction. A counted source fixture
reproduces 33 live allocations after a 32-rename burst and 1000 fully coherent
normal-use cycles. This is a general DXMT resource-lifetime issue, without a
game-name check or application-profile exception.

The fix reserves the first coherent allocation for reuse and releases only excess
FIFO references at least 300 completed sequence IDs old. It introduces no waits.
Current names, incomplete entries and command-list-owned allocations are protected;
independent references held by recorded commands remain valid. Reclamation is lazy
on subsequent allocation. Unused objects can retain their pool, and later bursts
can pay allocation/release costs again. Sequence age is not a wall-clock or frame
count, nor a global memory cap.

## Verification

From `components/dxmt`:

```sh
python3 tests/vibewine/dynamic-retention/run.py --sanitize
python3 tests/vibewine/dynamic-retention/run.py --arch x86_64 --sanitize
```

Native arm64 and Rosetta ASan/UBSan passed. The fixture compiles the production methods and actual intrusive Rc with
counted mock allocations. Old source reproduces the defect and fails the corrected
assertions. Cases cover age boundaries, current/inflight/deferred ownership,
external references, recurring bursts, extreme sequence values, conservative
out-of-order FIFO retirement and destruction.

A clean 69-translation-unit D3D11 candidate also includes the earlier staging
retention fix. Its import/export names and ordinals and embedded Metal helper
remain unchanged. The headless guest in the Dynamic test's `gpu/` directory passed
in a copied authless runtime/prefix with network and user-file isolation: 512
byte-exact snapshots, 3824 checks and 1309 completed event queries, then natural
exit zero. It checks immediate/deferred buffer and linear-texture renaming, both
FinishCommandList and ExecuteCommandList restore flags, retained-list replay and
two 384-query GPU-progress windows. The snapshots measure data correctness; actual
pool reclamation counts are established only by the source fixture. The private
server was cleaned up and the existing Steam/server identities stayed unchanged.

## Relation to the game freeze

A subsequent baseline-game capture, without either memory candidate installed,
held a displayed frame for 59.216 seconds. A native stack inside the interval
shows the Metal submission path blocked in an IOGPU trap and the DXMT finishing
thread waiting for completion. System compression and swapping rose concurrently.
These observations do not identify which resource caused memory pressure or prove
that Dynamic/staging retention caused this stall. The full CPU profile was not
usable after oversized finalization; the independent native Metal history and
text stack samples remain available.

The candidate was subsequently activated in a separate private g23 trial. Its
runtime preserves all six previous Wine/input/native/network changes and replaces
only D3D11. A fresh process-incarnation check and physical mapped-file observation
confirmed the candidate DLL in DS3; the title screen appeared and native Quit
closed the game. This is not a canonical renderer installation or combat/FPS
acceptance. No game FPS gain or freeze repair has been established. A controlled
same-scene comparison remains required before promoting it as a gameplay repair.

# Dynamic allocation retention regression

Both `DynamicBuffer` and `DynamicLinearTexture` retain retired allocations in an `Rc` FIFO. A one-time rename burst previously kept its full backing allocation highwater until resource destruction, including after every retirement sequence completed. Repeated normal use reused that highwater without shrinking it.

The fixture compiles the checked production class/method bodies and production `Rc`; it replaces the Metal allocation boundary with counted 64-byte allocations. It checks the 300 completed-sequence reuse window, completed versus in-flight entries, current name safety, independent references such as pending rename closures, deferred command-list ownership/recycle, recurring burst reuse, UINT64_MAX age arithmetic, conservative out-of-order retirement, and destruction. It is not a GPU or D3D11 command-list integration test.

Run from the repository root:

```sh
python3 tests/vibewine/dynamic-retention/run.py --sanitize
python3 tests/vibewine/dynamic-retention/run.py --arch x86_64 --sanitize
python3 tests/vibewine/dynamic-retention/run.py --source-ref 68ef6024 --expect-retained-baseline --sanitize
python3 tests/vibewine/dynamic-retention/run.py --source-ref 68ef6024 --expect-retained-baseline --arch x86_64 --sanitize
```

The baseline check asserts that 33 allocations remain after 1000 completed rename cycles. Running the corrected suite against that old source must fail:

```sh
python3 tests/vibewine/dynamic-retention/run.py --source-ref 68ef6024 --sanitize
```

Reclamation is lazy on a subsequent `allocate`, keeps one coherent allocation reserved for the caller, and drops only excess FIFO references aged at least 300 completed sequences. Current and command-list-owned names are outside that FIFO. Surviving external `Rc` references continue to hold their allocations. The change introduces no GPU waits and does not free incomplete entries. Idle resources without further allocation calls retain their pool until destruction; this fixture does not establish a DS3 memory magnitude or freeze repair.

Aging excess copies trades memory for allocation cost: a new large burst after the 300 completed-sequence window may recreate allocations that the old permanent highwater would have kept. The window counts command sequences, not frames or seconds. Trimming and allocation-reference destruction run under the existing Dynamic mutex; this fixture does not measure their Metal-driver release cost or frame-time impact.

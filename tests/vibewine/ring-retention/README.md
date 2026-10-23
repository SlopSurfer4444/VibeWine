# Completed ring-block retention regression

The runner compiles the exact production `RingBumpState` template and constants
against counted move-only blocks, omitting only unrelated block allocators and
platform includes. It does not load Metal, launch Wine, allocate GPU storage, or
change a runtime. Native and translated runs support ASan/UBSan.

```sh
python3 tests/vibewine/ring-retention/run.py --sanitize
python3 tests/vibewine/ring-retention/run.py --arch x86_64 --sanitize
python3 tests/vibewine/ring-retention/run.py --source-ref a5ad261e --expect-retained-baseline --sanitize
```

Baseline refs above use the public equivalents recorded in
[`docs/PUBLICATION.json`](../../../docs/PUBLICATION.json). Their source trees
match the audited local predecessors; unpublished local revision IDs are not
required in a fresh public clone.

Without `--expect-retained-baseline`, the old ref must fail the new bound.
Baseline mode establishes ten completed standard blocks still retained after
301 idle cleanup calls and nine after call 302. The candidate preserves that
initial grace, then retires the excess together and retains the newest completed
standard block as a warm spare. Cleanup can remove completed ad-hoc blocks behind
that spare, and preserves every block beyond the coherent sequence. The existing
`UINT64_MAX` sentinel remains unconditional teardown and must only be used after
the owner has stopped all users.

The fixture checks fixed and advancing coherence, empty and sentinel cleanup,
oversized blocks, front reuse and partial-tail grace reset, reference stability,
move/destruction ownership, and a producer writing an in-flight block while a
collector reclaims completed blocks. Every cleanup mutation uses the production
mutex. A `null_mutex` specialization still requires the owner's external
serialization, as before.

All scratch source and binaries live in a temporary directory removed by the
runner, including on failure. There is no persistent data producer. These tests
establish the allocator policy and ownership invariants; they do not demonstrate
that this policy change cures any DS3 frame stall.

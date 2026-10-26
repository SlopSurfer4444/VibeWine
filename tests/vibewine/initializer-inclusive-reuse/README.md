# Initializer inclusive upload reuse candidate

`python3 tests/vibewine/initializer-inclusive-reuse/run.py` runs bounded native
CPU regressions with ASan/UBSan, exceptions enabled and mocked Metal C endpoints.
It compiles complete snapshots of the production initializer, ring, allocation
tracker and actual Metal C++ wrapper in three modes: define absent, explicitly0,
and explicitly1. Every mode runs the original31 transaction cases unchanged.
`--source-dir PATH` selects another DXMT checkout. No Wine/game/runtime execution,
network, installed-prefix mutation or persistent collector is involved.

The production change is an initializer-only adapter: the generic checked ring
still accepts an exclusive reuse frontier and retains strict last-use<frontier.
The initializer's eventN is inclusive completion after every GPU source read in
submissionN. With `-Dvibewine_initializer_inclusive_reuse=true` the adapter supplies
completed+1 only when0<current<UINT64_MAX and completed<current. It never advances
past the pending current generation, and impossible/exhausted inputs return
boundary0. Existing public initializer preflight still rejects sequence exhaustion
before publishing commands. Both reservation attempts use this adapter. Define
absent/0 passes the original completed value unchanged. Ordinary ring allocate,
checked allocation and cleanup implementation and the old strict-boundary test
are unchanged.

The new cases exercise:

- Two sequential full32MiB initializer uploads after event1: default/off allocate
  two native identities; enabled mode reuses the original identity with one call.
  Denying a second native allocation makes default/off return OutOfMemory while
  enabled mode publishes both copies with the original source identity.
- Stale completion and pending current-generation storage remain protected.
  A separate case starts with stale cached completion, observes newer event1 and
  requires the second reservation attempt to reuse equality only when enabled.
- The complete production initializer uses the exact two original distinct backing
  objects for paired32MiB+8MiB uploads at equality, with zero offsets and no new
  allocation. The disabled case fails before updating source data or commands.
- The complete generic ring plus exact production adapter preserves its full FIFO
  snapshot and caller sentinels on second-plane native nil, native bad_alloc and
  production std::list-node allocation failure. A prepared first-plane reuse
  remains uncommitted. Pair reuse checks original object identities and protects
  both now-pending allocations. Stale/future completion and sequence limits are
  exercised independently.

The32MiB allocation-count cases use the existing identity/capacity-only native
mock: it does not allocate32MiB host/GPU backing or dereference the declared32MiB
source unless capture_updates is enabled. These checks prove CPU allocation and
transaction behavior. Real Metal payload/lifetime evidence is separately scoped
in [gpu/README.md](gpu/README.md). Full production initializer/native-bridge GPU
validation and a matched complete build remain integration gates; this switch
stays off by default. No memory, FPS or game-performance claim follows.

Run the existing contract independently:

```
python3 tests/vibewine/ring-checked/run.py --sanitize
python3 tests/vibewine/initializer-transaction/run.py
```

Owner: initializer candidate worker; integration/index/build/admission owner: root.
Source snapshots, binaries and bounded diagnostic stdout/stderr are private
TemporaryDirectory data removed on exit. Explicitly retained receipts inherit the
existing initializer-repair seven-day TTL and combined64MiB raw cap, active/recovery
holds and manual exact-owner cleanup. No new retained application-data producer
or cleanup mechanism is added.

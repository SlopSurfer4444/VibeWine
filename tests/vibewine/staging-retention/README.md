# Staging retained allocation regressions

Run from the DXMT component root:

```sh
python3 tests/vibewine/staging-retention/run.py --sanitize
python3 tests/vibewine/staging-retention/run.py --arch x86_64 --sanitize
```

`run.py` reads the actual staging header and implementation from this checkout, removes only include directives/`#pragma once`, and compiles their unchanged bodies. Tests use DXMT's actual intrusive `Rc` template. The Metal device, buffer allocation and mutex dependency boundary is replaced with counted in-memory allocations. Generated sources and executables live in an automatically removed temporary directory.

The suite covers aged highwater retention: 33 buffers stay reusable on completion and through age 299, then reduce to current plus one spare at age 300 completed sequences. It checks completion immediately before/equal to retirement, younger in-flight IDs surviving reclamation of older entries, pending immediate rename closures, reuse without growing numeric slot space or allocating new buffers across 100 recurring bursts, preserved copied bytes, independent external `Rc` ownership of actually trimmed slots, an allocation reserved before immediate-name publication, and concurrent numeric-ID lookup while the pool grows. The deferred lifetime case models the source's strong resource capture and execution-time current-buffer resolution; it does not compile the whole D3D11 context or establish GPU/deferred-context integration correctness.

The allocation fault seam denies every C++ allocation during aged map cleanup, verifies freelist capacity covers all pool slots, and injects failure at each allocation in a pool-growth operation. It checks pool/current/FIFO identities and live allocation counts survive failures, including a pool-vector growth failure after freelist reserve succeeded. The seam uses a malloc-backed global `operator new` only in the test executable; production allocation behavior is not replaced.

To reproduce predecessor behavior without changing the checkout, provide an unpatched DXMT source tree:

```sh
python3 tests/vibewine/staging-retention/run.py --source-dir ../../current/dxgi-source --expect-retained-baseline
```

Baseline mode expects 33 allocations to remain after coherent catchup, the age threshold and 10000 maps, followed by complete destruction when the resource dies. Candidate mode must fail on that predecessor. No Metal, installed Wine runtime, game, or user data is used. Lazy cleanup requires another map/allocation after the reuse window; completely untouched resources and in-flight burst memory are outside this change's guarantee. ASan/UBSan do not establish freedom from all data races.

# Completed staging allocation retention

The general DXMT fix is component commit
`44800cafedf849aa6f03e05a01698e9016cb6e22`. It is independent of any game profile.

Staging renames previously retained every backing allocation until the resource
died. A source fixture reproduces a 32-rename burst retaining 33 full allocations
after completion and 10000 subsequent maps. The fix preserves recent copies for
reuse, but ages excess completed copies out after 300 completed sequence IDs,
leaving the current allocation and one warm spare. Cleanup happens on the next
map/allocation; untouched resources and still-in-flight memory are not reclaimed.
Bursts recurring after that reuse window may need new allocations.

Numeric IDs remain stable. Only coherent retired IDs are released, and freelist
storage is reserved before publishing new slots so cleanup does not allocate.
Queued rename closures and GPU completion precede coherence publication; external
strong references remain valid after the pool relinquishes its ownership.

## Verification

From `components/dxmt`:

```sh
python3 tests/vibewine/staging-retention/run.py --sanitize
python3 tests/vibewine/staging-retention/run.py --arch x86_64 --sanitize
python3 tests/vibewine/staging-retention/run.py \
  --source-dir ../../current/dxgi-source --expect-retained-baseline --sanitize
```

Native arm64 and Rosetta x86_64 ASan/UBSan checks passed, covering aging boundaries,
in-flight/current/reserved IDs, slot reuse, recent recurring bursts, external
references, copied bytes, concurrent lookup and four allocation-failure points.
The fixture compiles the production class/method bodies with the real `Rc` type
and substitutes counted allocations at the Metal boundary. It is not a real GPU
or complete deferred-context integration test.

A separate candidate based on the installed renderer's recorded source was clean
rebuilt across all 69 translation units. Its Metal helper content and PE import/
export names and ordinals were unchanged. The candidate is not installed, and no
game was launched. The historical `current/` snapshot remains unchanged.

## Relationship to the reported freeze

The saved Metal timeline confirms one displayed frame held for 42.066 seconds.
A native snapshot inside that interval shows the Metal submission thread in an
IOGPU submission trap and a DXMT finishing thread waiting for command completion.
System memory decompression/swap activity increased at the same time. Those
observations do not establish which component caused the pause, or that this
resource pool was active in the affected workload.

The CPU trace contains samples only after 134.613 seconds, outside that pause.
Stored counters alone must not be used to attribute it. An offline temporal
coverage validator now distinguishes sample presence from coverage of a reported
interval. No game FPS gain or freeze repair has been established by this change.

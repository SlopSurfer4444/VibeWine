# Adjacent full-color clear contraction

The existing default-off `DXMT_DEAD_ATTACHMENT_STORES` selection now also
contracts **disjoint original adjacent** Clear→Clear pairs. A narrow full-clear
proof admits only the exact same private, nonshared, nonheap, unmapped native
base texture: 2D, one sample/mip/layer/depth and matching nonzero descriptor,
recorded and actual native extents. The later value survives. Earlier external
waits and both completion identities move into the survivor; only the earlier
internal completion edge is removed. Unrelated encoder ordering is unchanged.

This removes a physical render-clear encoder and its store boundary. It does
not remove a submitted API call, claim shader savings, or establish a game gain.
There is no new option, ABI, retained data producer or cache epoch.

Disjoint pairing is a correctness boundary. `FenceSet` aliases IDs modulo256;
unlimited forward aggregation can collect every update bit and subsequently
erase a real external dependency. The prepass skips the next original pair
when one is contracted. The helper also rejects predecessor wait/update overlap
to fail closed on that ambiguity in already-transformed nodes.

Run `python3 -B tests/vibewine/adjacent-clear/run.py` (or `--arch x86_64`). The
fixture reuses `pass-optimizations/run.py` and adapters; full production prepass,
reverse optimizer, relation methods, structures and helper are extracted.
Actual `FenceSet` and native command wire layouts are included. Texture storage
and extent getters are CPU adapters. This covers default/off/on, exact base
`b3e92edfb749eac59d90dcf2932dfd4cef24bbe5` old-fail, connected helper/driver
mutations, both completion and external wait edges, 257-clear slot wrapping,
subsequent existing render folding, and identity/storage/extent/gap exclusions.
The existing dead-store/residency cases run too. Expected negative controls must
fail a behavioral assertion with exit1; build, sanitizer or signal failure never
counts. Scratch sources/binaries are bounded temporary files removed on exit.
`--prepare-only` only generates/mutates sources and hashes, without a compiler.

Native image/cost source preparation:

```
python3 -B tests/vibewine/adjacent-clear/gpu/prepare.py --output /absolute/new/private/directory
```

The prepared receipt gives exact compile/run argument arrays; preparation never
compiles or executes. Root must grant compiler/GPU slots before those actions.
The native fixture compiles the exact extracted optimizer with the feature on;
its baseline skips optimization. The selected surviving nodes drive native
Metal color-clear encoders/fences on untracked private textures. WMT width/height
getters forward to actual Metal objects. A real prior reader must retain blue,
the adjacent-clear target must retain the final green, a gap reader must retain
red, and distinct-target clears must remain separate. Every readback pixel is
checked; `--wrong-golden` first verifies the baseline then changes one golden
byte and must exit30 at `FAIL pixel golden` on candidate readback.

The native Metal adapter is deliberately smaller than Wine/PE/native integration:
it does not invoke the production WMT bridge or its entire flush emission.
Matched full builds and renderer-level images remain root's integration gates.
Timing uses complete command-buffer GPU intervals for128→64 full512×512 clears,
three warmups and15 rounds with rotating baseline/candidate order. It includes
fence and driver work; fast-clear metadata and driver coalescing can dominate.
Report all rows and measured negative/neutral outcomes. No FPS claim follows.
The parent owns an external deadline and reconciliation of GPU work.

Task evidence belongs to root's sustained-wave contract: raw logs7days/32MiB,
curated results30days/8MiB, build products256MiB, aggregate40GiB with20GiB free
reserve. Active/unresolved/recovery/explicit holds exclude retirement. Cleanup
is manual, exact-owned-path and writer-coordinated; no cleaner is installed.

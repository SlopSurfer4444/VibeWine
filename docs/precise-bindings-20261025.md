# Opt-in precise constant-buffer invalidation

The `vibewine_precise_bindings` Meson option defaults to **false** and defines
`DXMT_PRECISE_BINDINGS`. Enabled immediate/deferred dynamic-buffer
`WRITE_DISCARD` scans existing bound CB entries and dirties every slot that
references the mapped buffer's canonical `Buffer` identity. Unrelated slots and
stages retain their previous state. Existing dirty bits are preserved.

The old behavior dirties all CB bindings in every stage. A later draw/dispatch
uses reflection masks and rebuilds any affected stage's entire CB argument
table. Precision can therefore avoid unrelated stage tables; it does not make
updates within one affected table partial. The scan has a CPU cost and needs a
controlled comparison before performance promotion. No FPS benefit is claimed.

The helper stores no references or registry and allocates no memory. Allocation
renaming/suballocation, SRV/VB invalidation, shader/encoder invalidation,
argument encoding, access/hazard tracking, residency, query handling and queue
admission keep their existing paths. This is general D3D11 behavior without an
executable-name gate. Undefined/zero compilation retains the original CB loop.

The [production regression](../tests/vibewine/precise-bindings/README.md) binds
the helper and actual immediate/deferred buffer-Map branches to production CB
binding/upload/encoding and rename methods. Its CPU storage/command adapters
cover unrelated/unbound resources, multiple aliases and offsets, suballocation
and allocation changes, repeated replay, shared-resource context interleaving,
null/dummy bindings, unused reflected slots, errors and access/residency counts.
Baseline and connected mutation controls must be rejected. The runner removes
its generated scratch directory; no producer or retention policy is introduced.

On 25 October, both native arm64 and x86_64/Rosetta matrices passed ASan/UBSan:
27 scenarios each for default, explicit-off and enabled selections. Per
architecture, broad modes passed 38,600 checks and enabled mode passed 37,240.
The historical baseline plus nine connected mutations were naturally rejected
with exit 1, rather than compiler/sanitizer failures. Both complete context
translation units also passed real-header x86_64 PE syntax checks with the option
zero and one, using the existing build's production compiler flags at `-O3`.
These were syntax checks, without object generation or linking.

Whole-context restore boundaries, combined experiment flags, actual Metal
resource tracking and GPU rendering remain integration gates. Installation and
game acceptance are separate. A useful bounded comparison needs actual
dependent-stage masks, avoided table counts/bytes and CPU encoding time. Stop
promotion if almost every table rebuild is necessary or dependency scanning
costs more than it removes. UAV table caching remains a separate design problem
because unchanged view identity does not guarantee unchanged counter allocation
or eliminate access/hazard/residency obligations.

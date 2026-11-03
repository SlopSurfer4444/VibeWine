# D3D11 CPU hot-path economy, 4 November 2026

DXMT source `3986417fee45ce1e66d6480b9b41c06e97fff3ea` removes two
unnecessary operations without changing queue depth, presentation, resource
tracking or application profiles. This continues T3 in the canonical roadmap.
Neither change is specific to DS3.

| Path | Removed work | Preserved behavior |
| --- | --- | --- |
| `ArgumentEncodingContext::encodeConstantBuffers` | A temporary owning `Rc<Buffer>` copy for every nonnull reflected CB entry | The binding continues to own the Buffer. Synchronous access retains the current allocation before publishing its GPU address. Access, offsets, residency and command emission still execute. |
| `PreDraw` | Resolve/query of scissor enablement when scissors are clean | The existing dirty-scissors branch performs the same query and emits the same packets. Rasterizer/viewport/encoder invalidation rules are unchanged; there is no new cache. |

## Verification

The extended precise-bindings fixture extracts the complete actual CB encoder
and uses production intrusive `Rc`. Six additional scenarios cover all shader
stages, two aliases, an offset and a null binding. They observe no transient
Buffer retain/release during encoding, identical addresses and access/residency
counts, and live allocation data after unbinding and destroying the Buffer.
The connected `cb-copy` mutation restores the old local copy and fails precisely
the transient-ownership contract. Existing rename/suballocation, offset,
immediate/deferred replay and dependency controls remain active.

All 24 precise-bindings variants meet their expected outcomes under ASan/UBSan
on native arm64 and x86_64. The enabled candidate passes 57 scenarios and
183,718 checks. The fixture's healthy-queue adapters were updated for the
previously introduced queue-failure interfaces; failed queues are outside this
fixture's scope.

The new scissor-query fixture executes full extracted production `PreDraw`,
actual rasterizer/scissor/viewport setters and real WineMetal packet layouts.
Across 1,024 stable draws the getter count changes from 1,024 to one. Packet,
payload, status and resource-path observations match the immutable `a422a932`
baseline. Independent rectangle goldens, state transitions, encoder reopen and
admission/allocation failures are checked. The baseline fails only the economy
assertion; a forced-disabled-scissors mutation fails the rectangle golden.
Both architectures pass the four scenario groups under ASan/UBSan.

The fixture's COM, queue, allocation-retention, native residency, pipeline and
Metal endpoints are CPU adapters. Source audit independently confirms the
production allocation-retention path is unchanged. These checks establish
covered operation removal and command equivalence, not GPU pixels or FPS.

## Build and delivery

A fresh full Meson release build with the existing experimental feature options
completed. All seven selected compiler/PE/native artifacts are present: airconv,
libairconv, WineMetal PE/native bridge, D3D11, DXGI and D3D10Core. The successful
retry completed 154 remaining steps after the initial attempt stopped at a
missing Metal compiler. Production source hashes agree before and after the
build and with the subsequently committed source tree; commit metadata itself
was not the build's original version identifier.

The vendor-owned Xcode Metal Toolchain `27B47` was installed through
`xcodebuild -downloadComponent MetalToolchain`; `metal --version` reports
`32023.922`. No global developer-directory selection or game configuration was
changed. No benchmark or DS3 run was launched in this pass. The installed game
engine and benchmark application remain on their previously pinned bytes.

Private build/test evidence and the reusable build cache occupy approximately
151 MiB in this pass's task-local `work/` directory. Retain for 30 days or until
an accepted successor has consumed the required evidence; no background producer
or automatic deletion was installed. These paths and raw logs are not published.

## Performance acceptance still required

The drawstress source binds the same constant buffer to vertex and pixel stages
and performs 1,024 updates/draws. If both reflected entries are encoded on each
iteration, the borrow removes 2,048 retain/release pairs per frame. This is a
source-derived count, not an observed renderer time saving.

Compare the baseline and candidate through the same matched compiler/native
closure, scene, settings and warmup before assigning CPU or displayed-FPS gains.
The earlier native presentation-worker split remains rejected: its warmed heavy
case was about 4.1% slower. These two CPU changes do not revive that experiment
or establish that drawable pacing has been fixed.

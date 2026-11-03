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
changed. The initial source/build pass did not launch DS3 or a benchmark. The
subsequent isolated comparison below used its own engine copies. The installed
game engine and benchmark application remain on their previously pinned bytes.

The initial build cache occupied approximately 151 MiB. The later matched
baseline build and independent prefixes add scratch described below. No background
producer or automatic deletion was installed. Private paths/logs are not published.

## Performance acceptance still required

The drawstress source binds the same constant buffer to vertex and pixel stages
and performs 1,024 updates/draws. If both reflected entries are encoded on each
iteration, the borrow removes 2,048 retain/release pairs per frame. This is a
source-derived count, not an observed renderer time saving.

### Controlled comparison

Both revisions were built with the same current compiler versions and release
options; their production delta is exactly the two consumer files above. Separate
task-owned prefixes, engine copies, HOME/cache and sandbox roots were used.
Mapped-module paths/hashes bind each guest to its selected build. The unchanged
2,443 engine/framework rows match the pinned installation; five graphics artifacts
come from the corresponding matched build.

Order A–B–B–A, A=`a422a932`, B=`3986417f`. Each suite ran drawstress then cube:
1,280×720, VSync0, two seconds warmup, 500 measured iterations with an actual
Present each iteration, five-second ceiling. All eight cases completed with
native identity, calibration/readback and observation admission; each private
wineserver was subsequently observed settled.

| Run | Source | Drawstress elapsed, seconds | Median loop outside Present, ms | Median recorded encode scope without contained drawable acquire, ms |
| --- | --- | ---: | ---: | ---: |
| 1 | A | 4.13550 | 0.33355 | 0.7031 |
| 2 | B | 4.13558 | 0.33165 | 0.6906 |
| 3 | B | 4.13577 | 0.33705 | 0.7112 |
| 4 | A | 4.13611 | 0.33150 | 0.6920 |

**This comparison does not demonstrate a reproducible speedup.** Removed source
operations do not establish CPU-duration or frame-throughput improvement. Cube
also gives no consistent gain: elapsed A=4.16837/4.14255 seconds,
B=4.14265/4.14325 seconds.

Outside-Present values subtract observations within each CSV row before taking
a median. Encode discrimination uses type14 intervals containing no same-thread
type98 interval, all in the CPU-steady clock. Each drawstress trace has 501 such
scopes and 500 acquire-containing scopes. These remain wall times and can include
other waits. Production Scope omits intervals below 100 µs; cube's varying
retained-scope counts make its scope medians unsuitable for an unqualified
encoding-cost comparison. The trace envelope includes completion/drain boundaries.
Battery fell 40%→39%; temperature, background load and power policy were uncontrolled.

Full native presentation identity/feedback coverage remains false in every case.
These observations establish neither physical displayed FPS nor GPU saturation
or DS3 performance. The micro-optimizations are not promoted to the installed
game as an FPS improvement. A genuinely GPU-bound scene/resolution sweep is the
next useful discriminator, rather than more speculation about this small CPU cost.

### Capture reliability and retirement

Two refused attempts produced no admitted performance data: an output exceeding
the guest's 220-byte bound, then a nested trace leaf exceeding Win32 MAX_PATH.
Shorter task roots resolved both. The maintained runner now checks both budgets
before Wine creation, reserving ten PID digits for the trace leaf. All 33 host
tests pass, including refusal before process creation. The experiment driver's
independent session hold spans runner completion through private-server wait;
failed holds were reconciled against exact server settlement.

Independent recomputation verified all eight observations and 48 evidence-file
hashes. After analysis, 24 inactive raw binary/NDJSON payloads were retired
(104,871,959 bytes). CSVs, calibration/run results, summaries and input hashes
remain; raw-event joins require a fresh capture. Another 1,216,003,656 bytes of
pinned regular engine/framework files were retired from superseded long-path
copies. Symlinks and prefix state were preserved. About 3.2 GiB of matched builds,
reusable test runtimes, prefixes and evidence remain under the combined 6 GiB
cap, with 30-day cache/report retention. No installed payload was removed.

The earlier native presentation-worker split remains rejected: its warmed heavy
case was about 4.1% slower. These two CPU changes do not revive that experiment
or establish that drawable pacing has been fixed.

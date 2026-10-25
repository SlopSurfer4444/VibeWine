# Sampled presentation visibility diagnostics

The `presentation-waits` trace profile pulls cached native layer visibility at
most twice per second per Presenter and active trace generation. Full/default,
compile-waits and unarmed traces do not call the visibility transport. The
optional 96-byte `W7VSRequest` uses the existing command-buffer property thunk;
old native bridges return unsupported without changing presentation behavior.
The feedback152-byte and trace64-byte ABIs are unchanged.

Only the asynchronous main refresh reads AppKit. At most one refresh is queued
per actual associated layer-state lifetime. There is no timer, background
collector, service, extra notification registration, or synchronous main wait.
An already queued single refresh may finish after capture stop; it cannot
publish PE trace events. Diagnostic state never enables or changes
`DXMT_SKIP_OCCLUDED_DRAWABLES`, its bootstrap or its admission decisions.

A 500ms refresh threshold and 1s expiry produce explicit UNKNOWN, STALE or fresh
status. Fresh state without a hosting window or usable window ID remains missing
host evidence. A unique layer identity is tied to associated-state lifetime;
reconstruction at the same raw pointer gets a different identity. Window IDs,
active Space membership, native occlusion bits, layer/view hidden flags and
native observation age identify missing visibility coverage. These are sampled
observations, never a continuous-visibility or gameplay assertion.

The four events share generation, swapchain, encoding thread and CPU receipt.
All start/end are CPU steady-clock receipt instants. Native MONOTONIC_RAW times
are payload only and must never be subtracted from QPC or CA-host timestamps.

| Event | flags | detail0 | detail1 |
| --- | --- | --- | --- |
| 100 | W7VSStatus | lifetime layer identity | observation sequence |
| 101 | W7VSFlags | native observation ns | native age ns |
| 102 | zero | native window number | display ID |
| 103 | zero | raw native occlusion bits | configuration revision |

Raw values remain visible for unknown/stale cases. Missing/partial/duplicate,
expired-as-fresh, malformed, or loss-affected groups cannot supply valid
visibility evidence. Old traces remain decodable; absent100–103 means no
visibility coverage. PresentedTime events71–73 retain their exact existing
semantics and clocks. No skipped Metal count, callback count, CPU/GPU duration or
visibility flag becomes displayed FPS.

Owner: existing presentation trace controller/caller. Scope: actual requested
layer while an explicit profile3 generation is active. Data class: scalar
process-local diagnostic identities/flags/timestamps. Retention/cleanup: reuse
the existing session contract and owner cleanup; no additional files or TTL.
Source cap: existing append-only131072-record trace; this adds at most four
records per500ms request (eight/second/Presenter); normal capacity/boundary loss
checks apply. Active/held trace originals remain excluded from cleanup.

Focused checks: native production syntax in Wine/native modes; actual native
transport/cache through main-ownership Foundation fakes on arm64/x86_64 and
ASan/UBSan; verbatim producer method against clock/transport seams; exact
producer profile allowlist; actual binary decoder plus hostile analyzer groups.
These checks do not verify live DS3 visibility or display cadence.

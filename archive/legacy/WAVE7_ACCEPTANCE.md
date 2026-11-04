# Current Wave7 graphics evidence and limits

This source checkpoint includes exception-safe native layer application (R1), clear-pipeline format/sample cache keys and success-only caching (B1/B2), detached persistent prewarm ownership (B3), event 77 accounting repair, and native AX root/window identity seams. Exact artifact identities are in WAVE7_PROVENANCE.json. Native admission delivery code, installed policy, profiles and identity metadata are excluded.

| Evidence | What it establishes |
|---|---|
| Production builds | 69 renderer translation units, native bridge compile/link, and all 25 driver translation units plus ABI compile/link succeeded. Exact source, binary and signature bindings were independently reviewed. |
| Focused source fixtures | Event 77 and persistence checks passed regular, address and thread sanitizer runs; R1 passed 14 cases in regular and ASan/UBSan runs; B1/B2 passed four cases in each mode. Native AX source and focused fixtures were reviewed separately. Original receipts describe private source runs, not execution of public adapters. |
| ABI/signatures | Renderer imports/exports and bridge Unix tables stayed unchanged; static driver GDI/Unix layout checks passed; selected native libraries passed strict signature verification. These are bounded compatibility checks. |
| Isolated actual AppKit and GPU fixtures | Owner reports final AppKit fixture and exact selected renderer/bridge/driver virtual-mode and queued-close GPU smokes passed. Procedural fixtures do not establish game behavior. |
| Installation | Owner reports the reviewed graphics package installed through commit 487b4065. This source export performs no installation or activation. |
| Actual DS3 trial | Owner observed the game launch. AX root returned one window and two children, compared with zero earlier. CUA click still failed with noWindowsAvailable; the window reported AXWindow, unknown subrole, inactive native state and CG level 27. More AX nodes do not establish actionable game control. |
| Recovery | A timed restoration returned the desktop to normal. This is not acceptance of a normal in-game exit. |
| Still pending | Actual DS3 virtual fullscreen/resolution, reliable CUA input, normal exit, relaunch and matched performance acceptance. No FPS gain is measured. |

These are separate source, build, installation and observed-behavior statements. The pointer-offset explanation remains a hypothesis. No game compatibility claim follows from the focused checks. Native AX root eligibility does not enable the optional PE accessibility bridge or prove complete child-provider behavior.

Pipeline preflight does not refund an application-consumed external waitable token. A later native encode failure cannot change an already returned Present HRESULT. Feedback events 77/78 are CPU instants, not displayed-frame durations or evidence of GPU drops; capture-boundary/last-frame finalization remains limited. B3 detaches lifetime ownership without establishing arbitrary concurrent caller safety. Historical evidence is retained under history/wave7-checked-mask and earlier trees.

No raw game logs, crash reports, authentication, profile/configuration data, installed snapshots or private admission metadata are distributed. No aggregate or full build was rerun simply to export these sources. Portable recipes and adapters have explicit external inputs; no bit-identical or clean-room runtime reproduction is claimed.

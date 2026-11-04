# Wave7 candidate1: evidence and remaining acceptance

This source snapshot identifies the frozen candidate1 combination. It is a source
review/export checkpoint, not a completed Dark Souls III compatibility result.
Observation entries below are owner-reported from the integration session; this
export does not include private logs, profiles, recordings or configuration.
This documentation update preserves the reviewed renderer/native/ntdll source,
fixtures and build recipes. It includes no new admission candidate code.

| Scenario | Status and limit |
|---|---|
| Candidate installation | Candidate1 installed; owner reports no-op/status checks passed. |
| Procedural GPU fixture | Owner reports test fixture passed for the installed combination. This is not game acceptance. |
| Actual CUA keyboard fixtures | All three live fixtures (window messages, Raw Input and DirectInput) received Return, Down and Escape, including key-down/key-up or corresponding buffered/state observations. Cleanup was verified; DirectInput self-timed out after receiving the inputs. |
| Actual Dark Souls III input | Native CUA menu mouse interaction observed; DS3 keyboard input remains unconfirmed. Pointer offset is a hypothesis only. Fixture keyboard results do not establish game keyboard acceptance. |
| Normal game exit | Menu exit through native CUA mouse interaction observed: the game native process disappeared and Steam remained. A normal process exit code was not established. |
| Relaunch/admission | Native identity admission remains unresolved. The current replacement admission candidate is NOT INSTALLABLE until a replacement ownership proof is reviewed. It is excluded from this export. |
| Requested virtual resolution/fullscreen | Actual DS3 acceptance remains pending. Trials failed startup identity admission and configurations were restored. Source refusal of a nonzero mode is not a user-facing resolution fix. |
| Performance | No FPS gain measured. Source timing semantics and disabled release clocks do not establish faster gameplay. |
| Tray candidate | Not installed; excluded from this source snapshot. |
| Public recipes | Explicit-input derivatives of recorded build commands; clean-room and bit-identical rebuild not established. |

The virtual trial identified as v2 failed DOS-form process-name recognition. The
following trial, v3, failed the `lineage_before` ownership check. These are trial
revisions, not renderer source revisions. Wine's `spawn_process` uses a double
fork, so the proposed requirement for Unix ancestry back to Steam is invalid for
this launch path. The observed DOS-form process name establishes a mismatch in
that identity assumption; it does not establish the cause of the original Steam
launch hang. A new ownership proof must be reviewed before the admission
candidate can be installed or a further trial can be accepted.

Both trial records report restored configuration. Neither trial establishes the
actual game's virtual fullscreen geometry, requested resolution or gameplay.
Offline fixtures exercise source logic and mocks. They do not establish real
AppKit scheduling, game input delivery, displayed frames, refresh behavior,
capture release, or performance. Keep installed, built, observed and accepted
states separate. A later code fix needs a new identity and fresh evidence.


## Historical material retained below

# Current pending acceptance — Wave6

- Real-game CUA discovery, attachment and observable actions remain unresolved, despite fixture/shallow AX success.
- DS3 full-display borderless/controller confirmed in one session. Actual normal exit and exclusive-fullscreen lifecycle still need verification; no dangerous modeset test authorized by this document.
- In-game resolution changes appear ineffective per user. Source path discards ResizeTarget dimensions then retains previous dimensions on zero ResizeBuffers; actual DS3 sequence/effective allocation remains unobserved.
- No matched displayed-FPS uplift; background-app closure improved perceived smoothness but cause is unproven.
- HDR fidelity, Metal view ownership and broader cross-game validation remain open; optional XInput uninstalled.
- Management launcher/source, native donor binaries and live profiles excluded; this source snapshot is not a complete distributable runtime.

## Retained historical notes

# Acceptance boundaries and pending hooks

## Included optional, not installed

- `optional/borderless`: independently reviewed helper successor and exact-source tests. No actual Wine API liveness or real-game fullscreen acceptance yet.
- `optional/sync-probes`: independently reviewed diagnostic producer, controller/analyzer and tests. Candidate hash 27100bd36f25fcc79f4b3dba6d900a4d58841047d5f72cdad98cd52cfd76d791. This is diagnostics, not an FPS fix; installed diagnostic remains be8769bd7cbc63d129e36bad367a041a4cbdf76a5b74af04c3ba4b946efdbf8a.

## Not included

New Wine discovery source is not yet accepted for live use and is excluded. A real CreateProcess-created target and helper appeared with distinct application identities in inventory. The fixture supervisor bounded total lifetime to 180.82 seconds. The getApp tool returned a timeout after 667.9 seconds, after the fixture was already stopped; this is not a 180-second cleanup-duration measurement. Inventory visibility did not establish successful computer-use control.

An exit-crash investigation raised a possible `_purecall` path. This remains a hypothesis rather than a proven origin, root cause or fix. No proprietary game code, binary or disassembly is included.

Future inclusion requires source identity, review, applicable isolated tests, privacy/license scan and regenerated manifest. Parent owns publication. An experiment's directory or successful build does not establish installed or gameplay acceptance.

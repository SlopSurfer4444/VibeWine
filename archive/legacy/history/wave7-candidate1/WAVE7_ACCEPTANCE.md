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

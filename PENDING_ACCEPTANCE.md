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

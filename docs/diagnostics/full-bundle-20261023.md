# Matched graphics bundle, 2026-10-23

Historical verification checkpoint. Original local build/source identities below
remain unchanged; [DXMT provenance](../provenance/dxmt.md) maps them to
code-equivalent public commits. For the latest installation state, see
[current status](../STATUS.md).

The DXMT component pin `5cf1662beee80e428810a49f0d5564569884b1bb`
was built as one matched group: D3D11, DXGI, D3D10Core, WineMetal PE,
and the native WineMetal bridge with statically linked AIRCONV. New PE code
must not be paired with the older native bridge: their Unix-call interface
and command layout differ.

This pin includes completed-ring-block aging, ports the retained bridge's
checked main-thread execution and optional acquisition diagnostics, preserves
the exact DS3 aspect-fit gates, and guards zero/overflow arithmetic in the
current Metal task layout. A shared compiler compatibility key isolates
persisted output from the changed compiler implementation without deleting
old shader caches. AIRCONV's public version remains 26; the local epoch is 1.

## Checks and observations

- A complete 161-step PE/native/helper build succeeded. The compiled Metal
  helper occurs once in D3D11. The native bridge does not depend on the old
  donor AIRCONV shim or a build-tree LLVM library.
- An isolated hardware copy guest passed 3,826 checks, 1,309 queries and 512
  byte-exact snapshots, including deferred replay. A separate shader guest
  passed 27 golden pixels at three sizes; its deliberately corrupted golden
  failed with the expected status.
- Actual mapped-file observations bound the new D3D11, DXGI, WineMetal PE
  and native bridge to the same private game run. D3D10Core was verified on
  disk; this game did not load it.
- The owner reported that the subsequent DS3 session played well. This is
  a useful gameplay observation, not a controlled FPS comparison or proof
  that the previously recorded long stalls can no longer occur.

The private trial retained the preceding Wine driver, NTDLL, network and
controller candidates. Those files were not rebuilt by this graphics change.
Promotion into the ordinary launcher was subsequently completed through the
private installer's supported transaction. Readback matched all ten selected
runtime files, four graphics prefix pairs and the installed lifecycle. Repeating
that installation was a no-op. The ordinary application launched DS3 with schema4
native confirmation; same-birth mapped-file observations verified the four
loaded graphics artifacts and the retained runtime XInput/WS2 pair. A visible
title/information screen was also observed. A repeated ordinary launcher request
returned already-running without creating another game session.

This verifies installation and initial ordinary launch, not sustained gameplay
on that permanent route. The earlier positive owner session used the matching
private trial. Normal game-menu exit followed by relaunch with Steam left open
remains unverified in this follow-up: automation could inspect the window but
did not establish reliable keyboard control. The title session also displayed
the previously reported server-disconnection message. No network repair or
controlled FPS improvement is claimed.

Private build receipts, profiles, session identifiers and raw game observations
are deliberately absent from this repository. Optional diagnostics and cache
retention remain governed by the component contracts; no collector is enabled
by updating this pin.

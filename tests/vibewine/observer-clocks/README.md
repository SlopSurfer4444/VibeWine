# Early profile gates for legacy scope clocks

Run `python3 tests/vibewine/observer-clocks/run.py`. For an isolated DXMT
worktree set `DXMT_TRACE_TEST_HUB` to the VibeWine hub containing the existing
`tools/telemetry/test_detail_producer.py` portable Windows mapping seam.

The actual producer header, immutable profile policy, admission, boundary
rejection and record code execute. Only the clock implementation is replaced
by a counted deterministic steady-clock seam; OS mapping is the existing shim.
Native operation callbacks execute through the actual `ApiScope::invoke` and
inside real `Scope`/`SyncScope` lifetimes. No runtime, cache, GPU or game is used.

Per 1,000 repetitions of Map-like API42, frame scope6 and fence scope30:

| Profile | Optional scope clock reads | Native operation calls |
| --- | ---: | ---: |
| default/full | 6,000 | 3,000 |
| compile-waits | 0 | 3,000 |
| presentation-waits | 2,000 | 3,000 |
| invalid/fail-closed | 0 | 3,000 |

Nondefault profiles retain one additional metadata clock/read event95 on first
observed generation, outside these repeated-scope counts. Thus this proves
specific timestamp-call avoidance, not zero whole-producer overhead or FPS.

The generation field and original `generation()` call remain unchanged, even
for excluded events, preserving initial profile metadata and caller identity.
Only an `active` bit gates each optional start/end timestamp. The booleans fit
the tested64-bit host object padding; size/offset assertions compare all three
previous layouts. Target builds remain an integration check.
Default/full allowed output, result payloads, 100us legacy Scope censoring and
same-generation STOP rejection counters retain their existing behavior.
Allowed scopes already in flight (including an explicit old generation) can
still read clocks; old tokens cannot emit in a new generation. This patch does
not promise zero work for stale allowed scopes, change boundary semantics, or
upgrade a zero/old token. CompileTimer/Present clocks remain outside this change.

All 15 production `Scope` constructions and all three `SyncScope` constructions
at base a21579fb explicitly initialize `.type` in their aggregate initializer;
no source aliases or default-construct-then-type-assignment uses were found.
Existing field order and designated initializer ordering stay compatible.
`ApiScope`'s constructor initializes type before the new bit. Future aggregate
callers must continue initializing type before the scope begins.

Checks cover disabled/unarmed/filtered scopes, all four profiles, immutable
environment selection, exact clock/native counts, zero/old tokens, close/rearm,
profile metadata, serialized result/identity/payload fields and preserved compiler
scopes3/5/17/19. Source mutants restoring
late timing and skipping excluded wrapped API operations must both fail.
ASan/UBSan and warnings-as-errors are enabled. Each test process has a20-second
bound, scratch is checked against64MiB and automatically removed on exit.
This test owns only its private temporary fixture data; no producer, retained
capture, service or cleanup policy is installed or changed. Full matched build
and measured application overhead remain root-owned integration gates.

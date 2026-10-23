# Exit and frame-hitch follow-up, 24 October 2026

Wine remains `e20af58312ef059e54f8d16767ee7b74cb8358e5`. The new DXMT
candidate is `356cc60d500f11d59dd2cb215250b2a08b07e8ba`, following the
previous `828429ca` matched graphics build. These are component/build identities;
they do not claim that every Wine feature was rebuilt or every game was tested.

## General native cleanup repair

During process shutdown, `NSObject_release` now checks the actual NTDLL
`RtlDllShutdownInProgress` flag before crossing into Objective-C cleanup.
Sibling threads may already have stopped while owning host locks. The kernel
reclaims those terminal process resources; ordinary release and explicit DLL
unload still perform their native ownership work. This changes no game executable,
exception policy, exit status or production timeout. Destructor-hidden terminal
side effects are skipped, so required persistence must precede process shutdown.

The native production-thunk regression reproduced the old lock failure and
verified the candidate on arm64 and x86_64/Rosetta. A separate PE lifecycle
probe used real NTDLL shutdown state: normal/unload paths entered the bridge,
terminal detach omitted it, and natural exit status 20 was preserved. The old
terminal control failed the ownership assertion. Exact real-header compilation
also catches the missing MinGW declaration that the initial fixture seam hid.

The default release build passed draw, MRT/depth, compute, MSAA and later-chunk
query tests with actual candidate module mappings; three deliberately broken
GPU controls were rejected. These establish covered correctness, not a game
freeze cure. Actual installed DS3 exit/relaunch remains a separate gate.

## Diagnostic changes

- `DXMT_CS2_TRACE_PROFILE=compile-waits` selects the existing compiler, readiness,
  archive and bounded persistence probes without the high-rate draw/Map stream.
  Event 95 explicitly identifies coverage; invalid profiles fail closed. Default
  tracing, v2 layout, writer quiescence and lifetime caps remain. See the
  [telemetry contract](../../tools/telemetry/README.md).
- [Phase annotations](../../tools/telemetry/SESSION_PHASES.md) bind actual
  observations to process birth and native Metal clocks. Unknown scenes stay
  unknown; aggregate intervals crossing boundaries are not fractionally invented.
  Memory summaries distinguish process counters from host paging.
- The temporary collection adapter disables automatic stack sampling by default
  and timestamps copied log-byte ranges. A nominal one-second native sample in
  the previous run lived for over eight seconds and overlapped late hitches.
  Source/adapter tests and activation are reported separately.

The maintained collector/analyzer passed 178 offline checks including the new
profile and phase cases. They do not establish live labels or instrumentation
overhead. The previous roughly four-minute, unlabeled 1080p frame stream is an
uncontrolled observation, not a clean gameplay benchmark or numerical FPS gain.
Active host paging and CPU-side latency are leads; compilation and a renderer
memory leak have not been established as the cause. No speculative cache eviction
or compiler-concurrency default is promoted by this follow-up.

## Ownership and scope

These engine repairs and diagnostic policies have no DS3 image check. DS3 startup
and resolution policy stays in its existing profile. The private delivery adapter
also corrects current-helper versus historical-native endpoint validation while
retaining every historical image edge; it is an installation lifecycle repair,
not a graphics optimization. Runtime installation, ordinary application launch,
real game exit/relaunch and labeled cold/warm comparison remain explicit evidence.

No new permanent service, cache purge or cleaner is introduced. Capture scopes
reuse seven-day raw retention, a sampled 256 MiB capture cap and 30-day/4 MiB
curated summaries, with active, analysis and recovery holds. Profile filtering
does not expand these bounds. Public notes omit private bindings and raw traces.

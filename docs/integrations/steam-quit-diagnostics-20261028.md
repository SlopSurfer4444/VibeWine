# Bounded Steam Quit lifecycle diagnostics

Status: diagnostics prepared and compiled; the recurring hang is not repaired.

One stuck Steam main thread waited for an application critical section whose
recorded owner was absent. A new process had the corresponding lock free and
exited through the client shutdown command and an owner-performed Dock Quit.
The successful Dock click occurred after the bounded capture ended; it is not
a captured quit-phase comparison or proof that the old thread-exit cause is fixed.

The optional Wine channel `vw_lifecycle` records thread creation, user exit,
termination target snapshots/results, critical-section timeout ownership and
Windows-side Quit phases. It adds no new timeout, automatic lock release,
termination policy, callbacks ABI or gameplay/default logging. Native Cocoa
queue/reason instrumentation is outside this candidate.

The counters saturate independently per loaded translation unit: 4096 PE thread,
4096 native thread, 64 critical-timeout and 512 PE Quit records including each
quota marker. Channel-off paths do not mutate counters or query owners. IDs and
handles can be reused; bind data to one process birth and retain unavailable
query statuses rather than inferring a dead owner.

Validation: native arm64 and translated x86_64 ASan/UBSan fixtures exercise the
actual admission/emitters, owner/target-query failure cases and 60 Quit schedules
with logging on/off. The complete seven-target Wine build succeeded with the
existing build configuration; production-input hashes matched reviewed source.
Wine source verification passed 11,187 files. Control launch-profile tests pass;
these observations establish diagnostic preparation, not live repair or FPS.

The DS3 control owner must preserve all five current graphics binaries, the
current 1080p native display setting and the prior rollback generation. Runtime
installation and live event emission are separate acceptance steps. Private
process samples, debugger output and raw replays remain outside this repository.

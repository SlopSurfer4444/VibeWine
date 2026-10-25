# Native observer profile gates

Run `python3 tests/vibewine/observer-gates/run.py`. In an isolated DXMT worktree,
set `DXMT_TRACE_TEST_HUB` to the VibeWine hub that contains the existing
`tools/telemetry/test_detail_producer.py` portable Windows mapping seam.

The fixture compiles the exact current producer/profile header, production
`observePresentation`, `observeVisibility`, feedback request/transport,
drain/close/destructor methods, the queue completion-observation block, and
actual Metal GPU timestamp getter wrappers. Native endpoints and drawable
objects are counted fakes; no GPU, game, database or installed runtime is used.

Full/default and presentation profiles retain QUERY/OPEN/POLL/STATUS/REGISTER,
capability/error results, feedback records, dimensions and GPU property reads.
Fresh compile-waits and invalid processes perform none of those feedback
operations, no feedback geometry reads, and no GPU timestamp getters. An
injected preexisting native owner is deliberately different: existing bounded
drain/STATUS/CLOSE cleanup still runs once before returning, then later calls
perform no new observation work. Native sessions close on generation changes,
capture stop, unsupported registration and destruction. The real profile is
immutable: changing environment after first selection cannot switch it.
Generation/profile metadata and compiler/dependency event selection are checked.

Removing either production gate causes its native-call counter assertion to
fail. These negative controls distinguish avoided observer work from an empty
filtered output stream. ASan/UBSan are enabled, compilation uses warnings as
errors, each process is bounded to 20 seconds, and private temporary scratch is
checked against 64 MiB and removed on exit. The test owns only this temporary
fixture data; no collector, retained capture or cleanup service is added.

This patch gates new optional feedback work and GPU timestamp property reads.
Mandatory completion/status/error handling, fence signalling, readbacks,
command-buffer completion and actual Present remain in their existing paths.
The fixture compiles the observation block rather than the whole worker; full
matched PE/native builds and live behavior remain integration gates. Other
legacy Scope/ApiScope clocks still need separate evidence before claiming low
overhead for the whole producer. No FPS or timing improvement is established.

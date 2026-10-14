# Requested adversarial architectural audit

Read README, PROVENANCE, SOURCE_INDEX and FILE_MANIFEST first. Distinguish installed code, fixture-only evidence and pending work. Follow source call chains; cite repository-relative paths and line numbers. Do not treat the management harness as the product: it is deliberately excluded.

Prioritize:
1. Wine/AppKit window discovery and AX lifetime: why a standard-control fixture works while real Steam/DS3 discovery fails; process/window/session identity, hidden/system windows, coordinate/DPI mapping, provider lease readiness and false-success semantics.
2. Fullscreen and exit: capture/mode ownership, nested lock reentry, asynchronous Cocoa teardown, cursor release, RestoreDisplayMode ordering, desktop-return crash and borderless timeout. Propose isolated reproductions and safe recovery acceptance before a real exclusive-fullscreen test.
3. Renderer throughput and pacing: shader/pipeline compile scheduling, cache retention, GPU completion waits, queue synchronization, CPU allocation/locking and present pacing. Identify plausible hot paths, what measurement would falsify each hypothesis, and low-risk patches. No promised FPS gain without data.
4. Diagnostic correctness: races, record publication/commit, ABI, PID reuse, session ownership, clock domains, completed-status GPU validity, ring overflow and overhead when disabled/enabled. Distinguish missing measurements from measured absence.
5. Memory/lifetime/concurrency regressions in startup, monitor allocations, display-link coalescing and AX publication. Audit failure cleanup as closely as success.
6. Cross-game design: which changes belong in Wine driver versus DXMT versus app/session wrapper; avoid per-game hardcoded behavior unless evidence requires it.

For each finding provide severity, exact evidence, trigger, user effect, smallest defensible patch, tests and rollback boundary. Separate confirmed bug, plausible hypothesis and rejected lead. Produce a dependency-ordered next-wave plan, a small set of measurements with highest information value, and a clear list of unsupported conclusions. Audit all included source freely, but never imply excluded runtime/build dependencies were inspected.

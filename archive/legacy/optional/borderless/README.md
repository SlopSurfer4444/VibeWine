# Optional borderless watchdog successor — not installed

This is original standalone Windows helper source plus bounded extracted-source tests. No deployment harness, launcher policy, game data or binary is included. Independent source review accepted this candidate; hardware/Wine API liveness and gameplay acceptance remain pending.

The helper gives watchdog arms unique generations, rejects stale expiry after disarm/rearm, records phase and sticky mutation-started state, and parks the main thread after watchdog expiry is claimed. It does not cancel a Wine API already entered. Diagnostic flushing or lower-layer shutdown can itself block. A timeout means unknown outcome, not a safe retry signal.

`python3 tests/borderless-watchdog/run.py` builds a macOS ASan/UBSan C fixture from exact helper methods and deterministic Win32 doubles. The test preserves existing identity checks, rendering and no-display-mode-call behavior. The fixture does not start Wine. Production PE build identity and compiler target are recorded in build-provenance.json; executable is excluded. The retained helper.patch refers to the original project path; full candidate source is supplied here without importing that project's management harness.

See independent-review.md for accepted scope. Installation and real-game observations must not be inferred from compilation or fixture success.

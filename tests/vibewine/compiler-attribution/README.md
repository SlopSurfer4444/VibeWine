# Compiler stage/read-lock source fixtures

Run `python3 tests/vibewine/compiler-attribution/run.py` (or `--tsan`). In a
standalone DXMT checkout set `DXMT_TRACE_TEST_HUB` to the VibeWine hub containing
`tools/telemetry/test_detail_producer.py`; this reuses its portable Windows
mapping seam. The runner uses existing clang++/Python, private temporary files,
20-second execution limits, a 64 MiB scratch cap, and cleanup on exit. It does
not open a real database, launch a game/GPU, install a runtime or collect data.

The actual trace/admission/profile helper executes with its clock function
counted (the clock still returns real steady nanoseconds). Shader/cache source
is extracted verbatim from current production methods, with the real parent
base and helper. WMT/compiler endpoints are deliberately fake. Negative source
controls prove that losing task binding or starting the lock timer after
acquisition fails the fixture. An unavailable reader returns without new work;
parentless reads remain explicitly unparented. Old task context in a new active
generation emits no stage event and allocates no new stage serial or timestamp.
Budget exhaustion bounds publication; it does not promise zero clock overhead
for subsequent armed attempts.

See `docs/compiler-attribution-20261026.md` for typed fields, clock, scope,
retention and interpretation limits. Full matched builds and application
measurements are separate gates.

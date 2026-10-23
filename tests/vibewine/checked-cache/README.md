# Checked native cache regressions

Run `python3 tests/vibewine/checked-cache/run.py` using existing Apple clang,
Foundation and SQLite. The ASAN/UBSAN executable has a 15-second timeout;
private sacrificial databases and generated files are removed after it exits.
No live user database, sidecar, installed runtime, Wine process or GPU is used.

The fixture compiles the complete production `unix/cache.c` with private Metal
API disabled and the exact new PE thunk bodies with an in-process dispatch
shim. Native SQLite is real. Wrappers count cache-owned DB open/close, prepared
statement/finalize and lock-file open/close; they do not replace SQLite results.
Checks cover checked initialization, payload hit, missing-key miss, writer BUSY
under another transaction, reader BUSY under an exclusive rollback transaction,
explicit test retry after release, non-database and corrupt-schema fallback
input, missing-file and invalid-input status, actual binding-length errors,
cleared SQLITE_STATIC bindings and legacy get/set behavior.

All active cache objects close before the test changes journal mode or creates
its corrupt schema using SQLite's own explicitly enabled writable-schema API.
The test also checks original 115–118 entries and additive 146–148 entries in
both dispatch tables and fixed parameter sizes. It is not a PE-loader or native
Metal integration test. The separate compiler-persistence fixture covers the
actual shader task's compilation fallback when cached library loading fails.

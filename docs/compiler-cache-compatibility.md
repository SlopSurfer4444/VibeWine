# Compiler output cache compatibility

`src/util/compiler_cache_compat.hpp` owns the persisted-output compatibility
key used by both the DXMT shader database table version and the optional CS2
graphics PSO archive digest. It combines the upstream `AIRCONV_VERSION` in the
high 32 bits with `kVibeWineCompilerCacheEpoch` in the low 32 bits. Bump the
local epoch whenever custom compiler output changes while the upstream API
version remains the same. This does not change the AIRCONV API or ABI version.

Epoch 1 separates this coherent compiler from older custom output that used
only version 26. The existing SQLite backend accepts uint64 versions and opens
`cache_<compatibility-key>` tables. Existing tables remain intact. PSO filenames
incorporate the same full 64-bit value through their digest. Existing archives
remain intact and will miss under the new key. No automatic migration, cache
deletion or manual SQLite maintenance is introduced. Possible incompatible
cache reuse motivated this change; it is not a proven cause of a game freeze.

Epoch 2 incorporates the upstream `ce001dfb` correction for hull shaders whose
relative patch index is `OUTPUT_CONTROL_POINT_ID`. Their compiled index now uses
the invoking control point plus the immediate offset. `AIRCONV_VERSION` stays 26;
new shader tables and optional PSO digests therefore separate this compiler output
from epoch 1 without removing any existing data.

## Data lifecycle contract

Owner: existing DXMT cache implementation. Scope: compiled shader output in
`shaders_<Metal-version>.db` under the existing default per-executable cache
directory or the explicit absolute `DXMT_SHADER_CACHE_PATH`, and opt-in
`.metalarc` graphics PSO output under `DXMT_CS2_PIPELINE_CACHE` for the existing
`cs2`, `compile-probe` and `prepare-pipelines` application gates. This change
selects a namespace within these existing caches. The W3 asynchronous writer
below changes the lifetime of optional writes, without changing keys, entries,
database tables, paths or output compatibility. No cleanup task is installed.

Shader-cache TTL and hard byte cap are unsupported by the current backend;
there is no expiry or automatic cleanup here. Existing optional PSO write
admission retains its 256 MiB process-accounted budget, 8 MiB entry limit and
4096-file limit; these are not a directory-wide quota across concurrent
processes. PSO TTL/automatic expiry is unsupported. No new collection occurs
solely because this header is present; normal DXMT use retains existing cache
production behavior.

### W3 optional persistence producer

Owner: each `PipelineCache` owns its `task_scheduler`, which owns one
`bounded_persistence` executor. Its single Wine-compatible worker starts lazily
on the first accepted write and runs only for that scheduler's lifetime. This
is an in-process producer, not an installed service. Scope/data class: copied
shader cache keys and retained compiled bitcode, or the existing opt-in Metal
archive/path/recipe. No shader, pipeline, device or cache-wrapper raw pointer
may occur in a job. Shader writes retain shared writer state and the data;
the existing shared writer mutex still serializes writers from separate
devices using the same cache. Native work establishes its own autorelease pool.

Admission bounds include queued **and active** work: at most 64 items, 32 MiB
charged payload/reservations, 64 retained framework objects and 16 MiB charged
per item. A shader job charges its known bitcode byte count and two retained
objects (data and shared writer state). An opaque archive job charges the
existing 8 MiB serialized-entry reservation, copied recipe and path bytes,
and one retained archive object. The API cannot measure an opaque Metal
archive's resident size: the reservation and object cap do not establish a
hard physical-memory quota. Queue/closure metadata has a separate fixed item
bound. These are per-scheduler bounds, not a process/global multi-device quota.

Source production remains limited to compiled variants and the existing
opt-in archive/recipe gates. Unavailable writers, unsuccessful compilation or
function creation, queue closure, allocation failure, and item/byte/object
overflow discard optional write work. They never fall back to performing I/O
on the compiler worker or suppress a draw. Successful function/pipeline
readiness is published before admission. Database-reader failure still falls
back to compilation. The additive checked cache operations report actual SQLite
results under the existing reader/writer locks; no retry is added. Executor
`completed` means a job returned, and `failed` counts caught exceptions;
neither means an entry was durably persisted. Snapshot counters are in-memory
only and add no trace sink, polling task or log producer.

The checked ABI adds only `Cache_alloc_init_checked`,
`CacheReader_get_checked` and `CacheWriter_set_checked`, at appended unix-call
IDs 146, 147 and 148 in both native tables. Existing exports, call IDs and
parameter layouts remain. New fixed-size parameter blocks are 32/40/40 bytes;
they must ship with their matching PE imports/native handlers. Mixing a new
D3D11/PE component with an older bridge is unsupported and must fail the matched
bundle gate before runtime selection. The compiler output epoch is unchanged.

Results preserve raw SQLite codes, including extended codes: init `0`, hit
`100` (`ROW`), lookup miss/write completion `101` (`DONE`), busy/locked `5/6`,
corrupt `11`, non-database `26`, and other errors. Detailed events 88 (write),
93 (read) and 94 (init) carry these raw results; `0xffffffff` means unavailable
and `0xfffffffe` a C++ exception. They use the existing opt-in generation cap,
CPU clock and no key/path payload. `DONE` is operation completion, not a durable
disk-sync guarantee. Legacy APIs continue discarding status. Bindings using
`SQLITE_STATIC` are reset and cleared before caller keys or mapped payloads are
released; failed initialization releases prepared statements, DB and lock fd.

TTL: queued writes live only until execution, rejection or owning scheduler
shutdown; active writes live until their native call returns. No wall-clock
timeout can safely cancel a native write. Shutdown first closes compilation
admission and joins compiler producers; it then closes persistence admission,
discards queued optional writes, joins the active writer, and releases retained
payloads before task/cache maps are destroyed. There is no replay, spill file,
extra archive or retry loop. Cleanup is this ownership-driven release; it
never removes existing cache data. Active holds remain until the writer joins.
The existing shader-database and PSO on-disk retention limitations above still
apply; W3 neither expires nor migrates those stores.

`DXMT_SCHEDULER_BACKLOG_GROWTH=1` independently enables runnable-backlog growth
on submission and continuation completion. The initial two workers and existing
maximum of twice reported hardware concurrency remain (with a minimum of two
when hardware concurrency is unavailable). Default growth remains submission-
triggered. No priority or numerical cap tuning is enabled by this option.

Verify with `python3 tests/vibewine/compiler-persistence/run.py` and the same
command with `--tsan`. They compile the production scheduler/cache headers and
the extracted production shader task with CPU-only platform/native shims,
covering blocked-write readiness, shared-writer lifetime, failures, active plus
queued bounds, shutdown/drop and independent burst-growth policies. They do
not validate native SQLite/Metal performance or complete device teardown.
`python3 tests/vibewine/checked-cache/run.py` exercises actual native SQLite and
the exact checked PE thunk bodies on application-owned sacrificial databases,
including hit/miss, read/write contention, corrupt/non-database initialization,
bind errors, transport layouts, legacy operations and resource cleanup. It does
not open user caches, run Wine/Metal or establish a matched-bundle loader result.

For a cold comparison, the operator can set `DXMT_SHADER_CACHE=0`, or choose an
explicit named fresh absolute `DXMT_SHADER_CACHE_PATH`. Omit
`DXMT_CS2_PIPELINE_CACHE` to disable the optional PSO store, or use its own named
fresh directory. These are manual owner controls, not permission to delete
existing caches. Preserve active cache files, SQLite sidecars, pinned evidence
and required rollback material; do not edit, remove sidecars, copy just the main
file or VACUUM a running cache database. Any later retirement requires a
separate scoped owner-approved lifecycle operation with writers stopped and
preservation verified.

Verify with `python3 tests/vibewine/compile_regressions.py`. The extracted
consumer check covers a local epoch change with an unchanged upstream version,
an upstream-version change, high-bit preservation, the full 8-byte PSO key,
both shader-cache reader/writer bindings and separation from legacy keys.

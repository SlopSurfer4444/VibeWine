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
only selects a namespace within these existing caches. No new background
producer or cleanup task is installed.

Shader-cache TTL and hard byte cap are unsupported by the current backend;
there is no expiry or automatic cleanup here. Existing optional PSO write
admission retains its 256 MiB process-accounted budget, 8 MiB entry limit and
4096-file limit; these are not a directory-wide quota across concurrent
processes. PSO TTL/automatic expiry is unsupported. No new collection occurs
solely because this header is present; normal DXMT use retains existing cache
production behavior.

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

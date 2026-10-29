# Experimental native ARM converter worker

The production Unix compiler thunks may call a persistent native converter through
`airconv_worker_bridge.cpp`. This preserves the synchronous compile-task owner,
local initialization/reflection, and existing cache publication. No second task
queue or persistent AIR cache is added. Paired shader APIs and WOW64 stay local.

The default is disabled. Both executable and bridge must be built with the same
64-character `DXMT_WORKER_BUILD_ID` string describing the exact converter, helper
bitcode, LLVM archives, options and protocol implementation. At runtime the owner
must explicitly set `DXMT_AIRCONV_WORKER` to an absolute matched worker path. The
worker checks that identity on every request. The normal Meson integration is
owned by the release build, including the separate ARM converter/LLVM architecture;
linking the worker to x64 archives does not create a native ARM worker.

## Bounds and ownership

The bridge copies release DXBC only for ordinary VS/PS/CS, at most 1 MiB each and
64 MiB retained including serialized reflection. The complete known recipe chain is serialized field by field,
including all nine node types, function name, arrays and root-signature bytes.
Cycles, duplicates, unknown types, malformed fields and cap violations return an
error before dispatch. Unsupported shader stages use the existing local path.

There is at most one child and one admitted request. Host compile-thread contention
uses the existing local compiler rather than accumulating a transport queue.
Unavailable configuration or a definite spawn failure is also local. An admitted
request that crashes, times out, is cancelled or returns malformed data never
replays inside the host. IDs bind each result to the exact request and worker
generation; AIR must be bounded and reflection must exactly match initialization.

The child uses `posix_spawn` with Darwin `POSIX_SPAWN_CLOEXEC_DEFAULT`, explicit
stdio actions, per-FD SIGPIPE suppression, a cleared signal mask and a fixed
`PATH=/usr/bin:/bin`, `LANG=C` environment. It inherits no application environment
or arbitrary FDs. Every admitted request has a 10-second wall deadline; failure
kills and reaps the owned child. Rotation occurs after 256 requests or five minutes
at next admission; the child exits after 60 seconds waiting on idle pipe IO.

Transport shutdown invalidates the active request, kills/reaps the child and closes
admission. It is also performed at transport destruction. There is no new task-owner
cancellation API: worker cancellation must not be described as application task
cancellation. Existing `ManagedShader` and synchronous call lifetime remain required.
Returned AIR is owned until its matching destroy thunk, with a 64 MiB retained cap.
This is not an LLVM memory sandbox or a hostile-DXBC verifier.

## Checks

`python3 tests/vibewine/native-arm-worker/run-recipe.py --sanitize` exercises the
actual codec: all node fields, copy/move ownership, limits, every truncation and
20,000 malformed cases. `--arch x86_64` also checks the consumer ABI on Rosetta.

A release admission must additionally exercise the real x64 bridge and native ARM
production converter, compare bytes against direct x64 conversion, verify actual
Metal libraries/functions/PSOs and representative rendered output, exercise
concurrent calls, rotation, pipe failures, identity mismatch, cancellation/reaping,
FD/environment isolation and deadlines including CPU cost. Measure cold spawn and
warm IPC separately. Tiny owned fixtures do not establish game compatibility or FPS.

`run-bridge.py` versions that real compiler-boundary acceptance. Build
`bridge_fixture.cpp`, `full_recipe_fixture.cpp` and `cap_fixture.cpp` for x86_64
with assertions enabled (`-UNDEBUG`), the current converter/parser sources and
the current Unix bridge compiled with `-DDXMT_WORKER_TESTING`. Use the ARM
builder's exact build ID for both the bridge and test objects. Do not link the
worker main into these three test executables. Existing LLVM15 x86_64 archives
remain separate from the ARM64 archives used by the actual worker.

```sh
python3 tests/vibewine/native-arm-worker/run-bridge.py \
  --worker /absolute/airconv-worker-arm64 \
  --bridge-executable /absolute/bridge-x86_64 \
  --full-recipe-executable /absolute/full-recipe-x86_64 \
  --cap-executable /absolute/cap-x86_64 \
  --fixtures tests/vibewine/native-arm-worker/fixtures \
  --output-dir /absolute/new-owned-acceptance-output
```

The runner requires a fresh output directory, matches executable-reported worker
and bridge IDs/architectures, compares every AIR against the direct x64 converter,
and records actual remote-success counts. It covers four unchanged Wine fixtures,
common flags, all nine recipe nodes, synchronized host contention, 258-request
rotation, shutdown, retained diagnostics, cancellation, FD/environment isolation,
pipe/protocol/identity/cap failures, and half-close CPU cost. Its whole-run wall
cap is85seconds (configurable30..90), each child has a bounded deadline/output,
and task-owned process groups are killed/reaped on success or failure. Python
`-O` is rejected. The source arrays, LGPL text and exact fixture provenance are
under `fixtures/`; no game shaders are collected.

## Data lifecycle

Owner: native compiler bridge within the process. Scope: ordinary shader DXBC,
serialized recipes, AIR and diagnostic results only. DXBC is process memory until
shader destruction (1 MiB/item, 64 MiB total); an active synchronous request holds
its source until completion. Recipe/request buffers are released at return
(approximately 3.125 MiB maximum payload). Returned AIR is process memory until
bitcode destruction (4 MiB/item, 64 MiB total). Diagnostics are bounded to 4 KiB per
owned error (at most 1024 retained errors, 4 MiB payload) and released by error
destruction; exhaustion returns a shared immutable diagnostic handle. Child stdio diagnostics go to
`/dev/null`; no new files, logs or persistent cache producer is introduced.

The separately owned 2026-10-28 validation artifacts are described in that run's
RETENTION.md: logs 7 days/32 MiB, results 30 days/8 MiB, builds 256 MiB; active
integration holds override expiry. Cleanup is manual and must recheck exact
ownership, active use and holds. No installed cleanup or vendor retention claim.

For a new harness run, the caller owns the exact fresh output directory. Raw AIR
evidence expires after7days with32MiB aggregate cap; JSON results expire
after30days with8MiB cap; combined output cap40MiB. Fault shims and PID/FD reports
are ephemeral validation evidence under the same seven-day log policy. Active
integration, unresolved findings and explicit recovery pins are holds. Cleanup
requires exact ownership, process absence and hold/reference rechecks; skip
symlinks and unknown/changing objects. These are manual retention obligations.

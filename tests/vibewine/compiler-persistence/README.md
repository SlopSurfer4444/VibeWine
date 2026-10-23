# Compiler persistence CPU regressions

Run `python3 tests/vibewine/compiler-persistence/run.py`, then add `--tsan` for
ThreadSanitizer. Requires existing clang++ and Python 3. Every binary has a
20-second timeout; generated sources/binaries live in private temporary
directories and are removed automatically. No live cache or installed runtime
is opened, no game/GPU is launched, and no additional collector is installed.

The first fixture includes the actual `dxmt_tasks.hpp`,
`dxmt_persistence.hpp` and `dxmt_shader_cache.hpp`; only OS/WMT calls are shims.
It checks legacy versus opt-in backlog growth, dependency continuation,
readiness while persistence is blocked, independent item/byte/object and
oversize rejection, payload destruction, shutdown cancellation and active-write
join, exception recovery, and shared writer state surviving cache-wrapper
destruction. Two executors must still serialize through one shared cache writer.

The second fixture extracts the complete `GeneralShaderCompileTask` class from
the current `d3d11_shader.cpp`, without a replacement implementation. It checks
that successful readiness can precede a blocked native write; failed compile,
library and function creation and warm hits do not enqueue writes; and compiled
bitcode is destroyed on each relevant path. Metal object calls are deliberately
fake, so this does not establish native framework behavior or cache durability.

The lifecycle/caps and unsupported on-disk retention are documented in
`docs/compiler-cache-compatibility.md`. Full matched PE/native builds and native
SQLite/Metal integration remain separate acceptance gates.

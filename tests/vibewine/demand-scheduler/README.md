# Demand scheduler CPU regressions

Run `python3 tests/vibewine/demand-scheduler/run.py`. Native AddressSanitizer
and UndefinedBehaviorSanitizer are enabled by default; `--tsan` selects
ThreadSanitizer. `--arch x86_64` builds and launches through Rosetta on Apple
Silicon when the existing compiler/runtime supports it. Unsupported compiler,
sanitizer or Rosetta environments fail explicitly. No installation is performed.

The fixture includes the production `dxmt_tasks.hpp` and persistence header
unchanged. It reuses the compiler-persistence OS/WMT mocks, whose four-core
hardware seam bounds the compiler pool at eight workers. Binaries and platform
shim headers exist only in a private temporary directory removed after the run.
The runner rejects a compiled scratch tree over 64 MiB and disables core dumps.
Every wait has a four-second failure bound and the binary has a 45-second bound.
No cache database, runtime, game or GPU is opened.

With both backlog-growth settings, all eight workers first block on explicit
gates. The fixture queues speculative work followed by demand and releases
exactly one worker. Disabled mode must take the FIFO speculative head; enabled
mode must take demand with the same eight workers. This proves queue bypass
after active work retires, not preemption or a reserved idle worker.

The runner also extracts both complete production CachedSM50Shader get_shader
methods verbatim into a mock cache owner using the real scheduler. It checks
that a public lookup promotes the exact queued speculative variant, repeated
lookups never duplicate compilation, ordinary public creation is demand, and
the speculative overload stays behind demand work.

Additional cases check repeated concurrent queued promotion, promotion during
Run, blocked-parent promotion before a dependency is submitted, demand inherited
by that late dependency, dependency completion before edge registration,
single readiness/run counts, one trace initialization per task, and bounded
admission of both normal and speculative classes under a demand backlog.
An explicit task deletes itself during readiness publication; repeated promotion
of its retired address must neither dereference it nor recreate scheduler work.
Dependency objects remain alive until all users finish, matching cache ownership.
The mocked task explicitly clears detached optional work: moved-from
std::function objects may retain captures with the x86_64 standard library.

Compilation failure is represented by a task returning itself with failure state,
as in the production compiler. It must still publish readiness. The fixture also
checks optional-writer exception recovery, readiness while optional I/O blocks,
worker joining, blocked-continuation cancellation, no growth after close and
release of cancelled optional-write payloads. It does not test C++ exceptions
escaping Run, native platform integration, compiler output or game performance.

The compute-prewarm aggregate reuses this fixture and adds exact compute cache/
constructor routing plus a fail-once variant-factory retry control.

# MSync WaitAll rollback regression

From the Wine component on macOS:

```sh
python3 vibewine/tests/msync-waitall/run.py --sanitize
python3 vibewine/tests/msync-waitall/run.py --arch x86_64 --sanitize
python3 vibewine/tests/msync-waitall/run.py --revision e20af583 --expect-regression --sanitize
python3 vibewine/tests/msync-waitall/run.py --revision e20af583 --expect-regression --arch x86_64 --sanitize
for mutation in ownership wake reset deadline; do
  python3 vibewine/tests/msync-waitall/run.py --mutation "$mutation" --sanitize
done
```

The runner extracts complete current production `msync_wait_objs`,
`do_single_wait`, client/server reset bodies, their actual shared object layouts,
and the candidate event/semaphore rollback helpers. It reads a historical source
with `git show`, never a checkout. The only server symbol rename permits both
reset entry points in one translation unit. Production LGPL headers are retained.

The baseline fails exactly ten named assertions: recursive ownership,
abandonment restoration, restored-event wake, semaphore maximum, client/server
reset preservation, retry deadline/APC checks, simultaneous pending APC before
expired deadline, and acquisition of an abandoned mutex. The candidate passes
15 cases, including unchanged success, recursive success, ordinary reset and
WaitAny controls. A new generation-wrap control applies only to the candidate.
Both architectures pass with ASan/UBSan. Four independent source mutations must
fail their expected assertions: removing acquisition ownership, rollback wake,
generation-aware restoration or deadline recheck. Compilation errors, sanitizer
failures and unrelated crashes do not count as detected regressions.

Atomic-call boundary seams force another actor's consumption/reset/release at
the exact readiness/acquisition race. The production algorithm, actual caller
arguments, timeout construction and return paths are compiled unchanged. Lower
single/multiple wait transports and `signal_all` are observation seams here;
they check object/address/TID/deadline/APC parameters and count requested wakes.
The separate [native pressure fixture](../msync-pressure/README.md) checks actual
extracted send functions and real Darwin ulock wake delivery. Neither fixture
is an integrated Wine kernel-scheduling or game-performance test.

The repair tracks which entries this attempt consumed, leaves pre-existing
recursive ownership intact, restores abandonment conditionally, caps semaphore
restoration, and wakes after rollback. A reset generation in the formerly unused
high word prevents rollback from undoing an intervening explicit client/server
reset. Retry checks retain APC precedence over timeout. This remains a sequence
of operations, **not an atomic grant of the complete WaitAll set**. Manual-event
changes and other cross-object linearization behavior are outside this repair.

The 16-byte shared entry size and protocol message layout remain unchanged, but
the reset-generation meaning requires Unix ntdll and wineserver built from the
same source; do not pair the candidate with an old running server. Reset helpers
are adapted from kageroumado / Claude Opus 5.5's LGPL-2.1-or-later
[Dormison 9d6a6ed5](https://github.com/kageroumado/dormison/commit/9d6a6ed55572957275f17a1d0b5d300b46227937),
whose parent already contained acquisition tracking. This is a reviewed
adaptation, not that commit applied without prerequisites, and does not import
the later server-owned atomic WaitAll subsystem.

Test source belongs to the Wine component owner. Each run owns one temporary C
source and executable, removed by `TemporaryDirectory`; the child has a 20-second
runner timeout. There is no persistent data producer, raw capture or runtime
installation. Printed hashes bind source and generated fixture, not live module
activation or FPS.

# Terminal NSObject release regression

`NSObject_release` must retain its normal ownership behavior while the process
is running, including explicit `FreeLibrary` cleanup. During process shutdown,
Windows has terminated sibling threads before calling DLL detach handlers.
Those threads may have held host allocator or Objective-C locks. Entering
native release at that point can deadlock; the kernel will reclaim these
process-lifetime resources on exit. This guard does not suppress exceptions,
change exit status, or bound shutdown by time. Explicit cache/persistent-state
write paths are unchanged, but work hidden in a terminal Objective-C destructor
will be skipped; required persistence must happen before this shutdown phase.

## Native host-lock regression

Run on macOS:

```sh
python3 tests/vibewine/terminal-release/run.py --keep /absolute/evidence/directory
```

Add `--arch x86_64` to exercise the translated host architecture on Apple
Silicon with Rosetta already present. The default compiles for the host CPU.

The fixture compiles the **entire production** `winemetal_thunks.c`, plus the
pre-fix version from `828429ca6b142230399d98272207af7714f12895`. It substitutes
only the Windows shutdown-state and Unix-call boundary; native release reaches
a real Objective-C `dealloc`. An owned worker is interrupted by SIGQUIT while
holding a pthread mutex; its real pthread TSD cleanup tries to reacquire that
mutex. A one-byte pipe receipt confirms cleanup entry before the main thread
attempts the release. This deliberately recreates unsafe asynchronous cleanup
only in a sacrificial fixture child.

Expected cases:

| Source | Mode | Receipt | Required outcome |
| --- | --- | --- | --- |
| Baseline | runtime | `NS` | natural exit 20; actual dealloc ran |
| Baseline | terminal | `DN` | native dealloc entered and blocked; child deadline cleanup |
| Candidate | runtime | `NS` | natural exit 20; actual dealloc ran |
| Candidate | terminal | `DS` | natural exit 20; native dealloc not entered |

`D` confirms pthread cleanup reached the held-lock boundary; `N` confirms
Objective-C dealloc entry; `S` confirms return from the production thunk.
The deadline is solely a negative-control test bound, not a production fix.
Ordinary mode also calls release on nil. The fixture cannot prove Wine sets
the real shutdown flag at the sampled application's problematic release.

## Real Windows lifecycle probe

First compile the production translation unit with the real Meson header paths
and compiler flags, without this directory's substitute `wineunixlib.h`:

```sh
python3 tests/vibewine/terminal-release/compile_production.py \
  --compile-commands /absolute/meson-build/compile_commands.json \
  --output /absolute/evidence/production-compile
```

This redirects object and dependency outputs away from the shared build. It
requires public revision `12a7ebf1` (the exact-tree equivalent of local
`2902c0de`) to fail specifically for the absent API declaration,
then requires the current TU to compile and reference the direct NTDLL import.
The production file supplies the BOOLEAN/WINAPI declaration because some
MinGW `winternl.h` versions omit it. The Windows fixture header deliberately
does not supply that declaration. Neither a seam-only test nor successful
compilation of the separate lifecycle DLL proves production header coverage.

Compile with the already-reviewed LLVM-MinGW toolchain; this does not run Wine:

```sh
python3 tests/vibewine/terminal-release/build_pe.py \
  --toolchain /absolute/llvm-mingw/bin --output /absolute/probe/directory
```

The DLL compiles the full production thunk with a bridge that records native
entry. It imports **real** `RtlDllShutdownInProgress` directly from NTDLL and
asserts the flag agrees with DLL_PROCESS_DETACH's `reserved` argument. Its
normal release and explicit unload both require a native call. This probe
does not load Winemetal's native module or a GPU; the native fixture above
separately validates the lock mechanism.

Only the integration owner runs the resulting commands in a private Wine
prefix, with exact host PID/birth and exit receipts:

```text
lifecycle.exe unload ABSOLUTE_WINDOWS_PATH_TO_lifecycle.dll
lifecycle.exe terminal ABSOLUTE_WINDOWS_PATH_TO_lifecycle.dll
```

- `unload`: exact stdout `RBSUBSRBSUBS`, natural host exit 20. This includes
  explicit unload and reload, with two normal and two detach releases.
- `terminal`: exact stdout `RBSPS`, natural host exit 20. A real Windows worker
  is alive when ExitProcess(20) starts; terminal detach omits the bridge call.
- `R`: normal release; `U`: explicit unload; `P`: terminating-process detach;
  `B`: bridge entered with the exact original object handle; `S`: completion.
- Probe failures use status 91–106; invalid arguments use 64. A deadline kill
  is failure. Use a 15-second host bound and 64 KiB output cap. The existing
  process supervisor may additionally verify Windows GetExitCodeProcess(20).

The PE pair is compile/import evidence until executed. Passing it and the
native fixture establishes the guard's mechanism and lifecycle scope, not
causality or success in the sampled game. Require the owner's controlled
application exit/relaunch check before claiming that outcome.

No recurring producer is installed. Scratch compilation is automatically
removed; `--keep` saves only the small report and compiler logs. The owner
retains chosen evidence and PE build artifacts under its existing integration
retention policy.

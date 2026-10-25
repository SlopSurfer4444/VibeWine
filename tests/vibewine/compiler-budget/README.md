# Opt-in compiler worker budget

Run `python3 tests/vibewine/compiler-budget/run.py`, optionally with `--tsan`
or `--arch x86_64`. Existing clang++ and Python are used; temporary binaries
are capped at 64 MiB and removed on exit. Each run is bounded to 30 seconds.
The task owns private temporary fixture data only; nothing is retained, and
no game, GPU, installed runtime, live database or collector is accessed.

The exact production scheduler executes real host threads through the shared
platform shim (which reports four logical CPUs). Tests observe created workers
and peak concurrently executing task bodies, for limits 1, 2, 3, 8 and values
above the automatic maximum, plus zero preserving two startup workers/eight
maximum. Both legacy/backlog growth and legacy/demand scheduling are covered.
A dependency chain admitted after its parents must complete with one worker.
Shutdown must join executing tasks, cancel queued work and never grow after
closure. The actual config parser rejects malformed, negative and unsigned
64-bit-overflowing strings; a source assertion guards config-to-scheduler
wiring. This is not a full D3D11 device/config-file integration test.

`d3d11.maxCompilerThreads` defaults to zero and is never enabled in an app
profile. It caps compiler workers per pipeline cache, excluding optional cache
I/O and other app threads. Lower values may increase compile stalls even if
CPU contention decreases. This is an experimental tuning control, with no
claim of FPS improvement. Full matched build and controlled application
measurements remain separate acceptance gates.

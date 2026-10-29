# Terminal queue consumer controls

`python3 tests/vibewine/queue-terminal/run.py` hashes and executes exact queue
admission/shutdown/error-event methods, the final native-submit section, staging
Map's complete retry loop, public fence methods, encoder discard, and all eleven
native encoder creation guards. Platform/resource types are controlled seams.
ASan/UBSan are enabled; generated files/binaries auto-delete.

`--mutation` accepts `drop-active`, `drop-drain`, `drop-map-failure`,
`admit-after-failure`, `submit-after-failure`, `drop-native-guard`, and `ctor-early-register`.
The constructor control injects `bad_alloc` after semaphore acquisition, then
executes queue failure; early registration is rejected by ASan.
Each corrupts one exercised production behavior and must fail the unchanged
assertions. Successful detection is reported as REJECTED.

This is CPU source-consumer validation; it does not exercise real COM interfaces,
Metal failure or foreign GPU cancellation. See the adjacent
`queue-failure-contract/README.md` for scope, API meanings and lifetime limits.
Same root-owned T2/T7 retention: logs7days/32MiB, curated results30days/8MiB;
no installed producer, runtime mutation or automatic cleanup.

`python3 tests/vibewine/queue-terminal/creation.py` executes both complete
production extern-C creation bodies under low/high device and swapchain
constructor OOM. It checks E_OUTOFMEMORY, cleared device/context/swap outputs,
zero feature-level output, actual C++ partial member cleanup and normal success.
`--source-ref 61aca2a3621386f1c6777972327149e48675883e --expect-baseline`
rejects the old CoreCreateDevice body because bad_alloc escapes its C ABI.

# Headless Dynamic resource GPU correctness guest

`guest.cpp` creates a feature-level 11.0 hardware D3D11 device and exercises dynamic buffers plus 128x128 RGBA8 dynamic textures. It creates no window, swapchain or shader and does not launch a game, subprocess or network operation. Source-level routing in this DXMT checkout tries `CreateDynamicLinearTexture2D` first for the selected single-mip/single-array/single-sample RGBA8 descriptor; the guest validates public Map/row-pitch/copy/readback behavior. It does not directly expose which internal allocator class was selected or its allocation counts.

For each resource type the guest:

- Produces 32 distinct full WRITE_DISCARD versions, copying each into a DEFAULT snapshot before the next Map, then verifies every byte by STAGING readback.
- Records two retained deferred command lists with 16 distinct mapped versions and snapshots each, one for each FinishCommandList RestoreDeferredContextState flag.
- Executes/replays each retained list with both ExecuteCommandList RestoreContextState flags after mutating the immediate resource name, verifying all snapshots.
- Retains those command lists through 384 completed copy/event-query submissions, then replays both flags again to check that aging cannot invalidate command-owned versions.
- Releases the lists, completes another 384 copy/event-query submissions to age retired allocations, then repeats the immediate bursts and deferred recording/replay.

Readback Map uses DO_NOT_WAIT and retries only DXGI_ERROR_WAS_STILL_DRAWING for up to five seconds within the global budget, because EVENT completion can precede finish-thread CPU coherence. Other HRESULT failures fail immediately. All texture comparisons honor mapped RowPitch and verify actual pixel bytes row by row. Buffer size is 64 KiB, larger than the one-page small-buffer suballocation route. CPU goldens and API-visible resources are bounded; opaque runtime pool usage is not measured. Aging uses GPU event completions, which precede CPU chunk-reset/coherence. Each 384-query window covers the 300-sequence policy plus two 32-chunk pipeline depths and 20 margin. This source-grounded margin does not directly observe exact internal retirement/coherence IDs or trim counts, so the result remains a byte-correctness and lifetime probe rather than measured reclamation proof.

Compile only using the existing LLVM-MinGW toolchain:

```sh
"$LLVM_MINGW/x86_64-w64-mingw32-clang++" -std=c++20 -O2 -Wall -Wextra -Werror \
  -static guest.cpp -o /absolute/private/output/guest.exe -ld3d11 -ldxgi -ldxguid
```

The output copy under `outputs/wine-wave7/staging-retention-20261023/dynamic-gpu-probe` includes `build.py`, `compile.json`, `compile.log`, `imports.txt`, source and executable. `build.py` compiles and inspects imports only; it never executes Wine or the PE. The first recorded compilation exited zero with no warnings; PE imports are D3D11, Windows kernel and UCRT. No networking library or shell/process-launch entry point appears in the import table. `GetProcAddress` and `getenv` are linked CRT support, not a runtime-selection mutation by the probe.

Execution/admission belongs to the parent in a disposable prefix with copied candidate runtime, external network isolation and separate host deadline. Capture loaded module paths, intended module hashes, full output and natural exit status before scoped private-server cleanup. Do not use a game prefix. Cooperative budget is 45 seconds, each completion poll has a five-second deadline, and the independent guest watchdog terminates at 55 seconds; termination is failure and does not prove already-submitted GPU work was cancelled.

Require four `IMMEDIATE_PASS`, eight `RECORDED`, 24 `DEFERRED_PASS`, two `AGED_PASS`, final `PASS`, natural exit zero and no `FAIL`. Both resource kinds must appear; unsupported texture capability is a failure rather than a silently accepted skip. A successful full run verifies 512 byte-exact snapshots and completes 1309 event queries. The bound is designed to match the sibling staging probe's order of work; the isolated candidate subsequently completed its guest workflow in 1.868 seconds (5.28 seconds including Wine startup), with natural exit zero, all required counts and module hash checks. The existing Steam/server identities were unchanged, and only the disposable server was cleaned up.

Passing establishes the tested public D3D11 rename/copy/deferred ownership behavior in that bound environment. It does not establish internal retained bytes, pool shrink magnitude, full context-state restoration semantics beyond the tested copy data, all possible driver interleavings, DS3 compatibility, FPS, or freeze repair.

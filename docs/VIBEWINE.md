# VibeWine DXMT integration

This component branch retains DXMT's Git history and carries reviewed VibeWine changes on upstream commit `50102f994b406f58f56d5d09f2ad20f9b90390a2`.
The reconstructed older overlay `9f13ff1e57b8af3b5eeb71d40fa299f6346be8dc` was a comparison input, not the new architecture.

## Changes by scope

**General correctness:** transactional swapchain resizing/fullscreen ownership, immutable queued presentation configurations, actual drawable geometry, error-path shader cleanup, movable lock ownership, immediate-context query locking, task shutdown synchronization, bounded display-mode count/fill retry, and successful-only clear pipeline caching. Upstream's new clear commands, stencil support, binding constants, compiler API/version 26, keyed mutex support, and hotpatch annotations remain intact. Failed clear pipeline creation retries no more than once per second per key; it logs once per failure episode.

**Native bridge:** the companion Wine-Metal code implements the versioned checked-layer and presented-feedback extensions used by the renderer, synchronized display snapshots, and safe shared-listener shutdown. An older bridge is not a supported drop-in replacement. This component is not an independently installable D3D11 DLL.

**Application profiles:** `dxgi.limitDisplayModeResolutions` defaults false; its Dark Souls III profile limits distinct enumerated sizes to avoid the application's fixed-capacity behavior. The separately gated `DXMT_DS3_BORDERLESS=1` fallback is in `src/util/ds3_borderless.hpp`; it is not generic fullscreen policy. These profiles must not be described as universal fixes.

**Experiments/measurement:** CS2-named trace, compilation, recipe, and binary archive facilities remain opt-in. They are not FPS improvements. Archive keys include compiler compatibility version; custom compilers sharing that version still need distinct cache directories. Upstream's maximum tessellation factor setting is a quality tradeoff, disabled by default.

## Reproduce bounded verification

From the source root:

```
python3 tests/vibewine/compile_regressions.py
python3 tests/vibewine/clear_retry.py
python3 tests/vibewine/native/run.py
python3 tests/vibewine/closest-mode/run.py
python3 tests/vibewine/mode-limit/test_modes.py
LLVM_MINGW=/path/to/llvm-mingw/bin python3 tools/check-pe-metal.py /path/to/scratch/build
```

The focused checks extract actual source and use native sanitizers. The last compiles 75 Windows translation units and rebuilds the matching helper Metal library; it deliberately does **not** claim linking, GPU behavior, or game compatibility. Requires Xcode's Metal toolchain. Use `DEVELOPER_DIR` if Xcode is not selected.

A complete runtime build additionally needs LLVM 15.0.7 native x86_64 development headers/static libraries, Meson/Ninja, LLVM-MinGW, matching Wine import libraries/native dependencies, and submodules. See upstream `.github/workflows/ci.yml` and Meson options for exact dependency configuration. Do not pair this compiler ABI with an older installed AIRCONV library.

## Acceptance limits

Source and fixture results do not establish FPS gains. Full matched linking, native/GPU integration, fullscreen recovery, dynamic-resolution behavior, and repeated game launch remain distinct acceptance stages. The inherited upstream release workflow is reference infrastructure; this component branch does not claim automatic PR/release validation from that workflow.

GS/TS arithmetic guards and focused zero/overflow verification were added in the coherent-bundle port below. A failed missing-drawable fallback blit-encoder allocation still needs end-to-end command-buffer failure propagation rather than silently skipping dependent fence updates.

## Full build verification (2026-10-18)

The standard Meson cross build completed with LLVM 15.0.7 static x86_64 libraries, LLVM-MinGW 20260826, Xcode 27.1/Metal, and matching source-built Wine dependencies. It linked D3D11, DXGI, D3D10Core, the PE Wine-Metal bridge, native Wine-Metal/AIRCONV, and the AIRCONV command-line tool. The tool's `--help` smoke invocation completed successfully. A missing target-private Wine-Metal header path in the WSI utility build was corrected.

This establishes compilation and linking of the coordinated candidate, not installed runtime acceptance. The build uses the current host SDK; deployment compatibility with older macOS releases is not established. Shader conversion, GPU rendering, real AppKit window transitions and game performance still require their own tests. No game installation was changed.

## Coherent bundle bridge port (2026-10-23)

The port starts at component `bd107f63a816c76c79e1452e0b606412a7c3476d`.
Behavioral reference is the native bridge in source commit
`1eb6b43f21a96b62045700f94831522cab6600bc`, signed bridge SHA-256
`7f7b1553b481266a975755b00ed96e47dc4e3e3c1e928a6ea8ca18bc8485f820`.
The newer component's public headers, call tables, command decoding, packed
swizzles, compiler and shader binding constants remain paired. General GS/TS
marshal guards now compile directly into the current Metal helper, preserving
zero/divisor/overflow safety without the old hash-selected library replacement
or original-AIRCONV donor loader. Upstream attribution and licenses are preserved.

The presentation profile retains the exact existing gates: aspect-fit requires
both `DXMT_DS3_ASPECT_FIT=1` and `DXMT_DS3_MARSHAL_FIX=1`, plus case-insensitive
`DarkSoulsIII.exe` basename in argument zero or one. It applies black bars and
aspect gravity through both layer-property APIs. The guard is general shader
correctness; `DXMT_DS3_MARSHAL_FIX` remains a compatibility gate for the DS3 profile.
`DXMT_SKIP_OCCLUDED_DRAWABLES=1` remains an experimental default-off native
acquisition policy with first-drawable allowance, unknown/stale-state recovery,
250 ms refresh and 1 s expiry. It can leave background screenshots stale; it is
not an FPS or freeze fix. All AppKit observations remain on the main thread.

`DXMT_ACQUIRE_DIAGNOSTICS_DIR` optionally selects a pre-existing owner-controlled,
symlink-free absolute directory with mode 0700. Default is no file producer.
The actual sink is retained byte for byte from the reference: one exclusive
mode-0600 `acquire-PID-BIRTH_SECONDS-BIRTH_MICROSECONDS.bin` per process, 82,176
bytes, 1,024 append-only records, 100 ms threshold, separate nil/clock-error/skip
counts, no per-frame filesystem calls, and no PE/feedback ABI change. It records
process birth, numeric thread/layer/drawable identities, clocks and counters;
it does not record window text, screenshots, credentials or input content.

Lifecycle owner is the private trial's launch/collection owner. Exact producer
scope is processes explicitly launched with that environment variable; mapping
lifetime ends at process exit. Raw files and ignored fixture outputs have a
manual 7-day TTL and 16 MiB per-session budget; the owner must stop new launches
at the budget and clean only identity-bound task-owned files after producer
quiescence and required snapshot preservation. Active trials, unresolved evidence,
pinned artifacts and rollback material are holds; their owner must resolve and
record a new expiry before cleanup. No automatic TTL, aggregate quota or cleaner
is implemented by this component. The sink's per-process byte cap is enforced;
the aggregate retention route is manual. Never delete a mapped/live sink to
pretend the producer stopped. Keep final decisions, receipts and minimal
reproducer evidence in the parent trial contract.

Run focused checks from this repository:

```sh
python3 tests/vibewine/native/run.py
python3 tests/vibewine/bridge-port/run.py
python3 tests/vibewine/bridge-port/run_occlusion.py
python3 tests/vibewine/bridge-port/run_profile.py
python3 tests/vibewine/bridge-port/test_marshal.py
```

The acquisition and occlusion fixtures reuse the reference's real sink and
source-extracted native thunk with Foundation mocks. Native syntax is checked in
Wine and `DXMT_NATIVE` modes. Profile checks use the actual helper with mock
layer properties. The marshal fixture executes the actual current Metal bodies
as a CPU C++ shim after stripping attributes/address spaces and inserting a
bounded iteration guard. It checks zero counts/divisors, incomplete patches,
wide products, caps, sentinels and multiple tasks; deliberately defective
arithmetic controls must fail. These checks do not exercise a Metal device,
GPU submission, Wine process, game, installed runtime or display. A clean matched
full build and isolated live verification remain the integration owner's gates.

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

Known follow-ups: GS/TS marshal products and zero-dimension handling still need dedicated overflow/zero-input verification against this exact helper; this port does not claim to fix inherited arithmetic edge cases. A failed missing-drawable fallback blit-encoder allocation also needs end-to-end command-buffer failure propagation rather than silently skipping dependent fence updates.

# Initializer transaction GPU guest

This procedural D3D11 correctness guest creates no window/swapchain, uses no game
assets or network, and measures no FPS. Reuse `../../gpu-wave/guest.cpp` helpers
at their exact relative layout. Compile via the existing bounded GPU builder
patterns with explicitly reviewed LLVM-MinGW C++20/O2/Wall/Wextra/Werror/static
and d3d11/dxgi/d3dcompiler/dxguid imports; snapshot both sources and hashes.
Execution belongs to root's disposable private-prefix/server sandbox with denied
network, bounded logs/process, process-birth and matched PE/native hash admission.

Seven mandatory stages exercise all six initializer entry/delegation paths:

1. Private output-buffer zero: exact1024 zero bytes after GPU copy/readback.
2. Padded2D upload: two slices×three mips, nonzero per-pixel patterns, independent
   CPU goldens for all bytes; source row padding differs from texel data.
3. Zero-buffer growth: create small and larger SRV textures before consumer flush,
   then independently verify every pixel is zero in both.
4. Render-target zero: exact RGBA(0,0,0,0) readback without an application clear.
5. Depth/stencil zero: no PS/color; equal zero depth+stencil passes4096 samples,
   wrong depth and wrong stencil reference each reject all samples.
6. D32S8 plus emulated D24S8 uploads: independent near/far/reference query goldens
   prove both planes were uploaded; writes disabled so tests preserve input.
7. Padded3D upload: distinct depth-slice patterns, row/image source padding and
   independently checked every-pixel readback using actual RowPitch/DepthPitch.

Negative modes `upload` and `depth` verify ordinary results first, then compare
against a deliberately wrong pixel/query golden. Expected natural exit20 only
at the intended stage; earlier API/native/timeout failure is not acceptance.
Normal output13 records=start+4modules+7stages+summary with coherent check totals.
Use the strict analyzer below; the generic five-stage schema does not match.

Per query/readback deadline5s, cooperative90s, watchdog100s, parent110s;
stdout64KiB/stderr4MiB, binary32MiB and source512KiB caps. Raw receipts share root's
seven-day/64MiB repair scope with active/recovery holds; manual exact cleanup only.
CPU fault injection, native private-Mach checks and GPU readbacks are separate
proofs. This guest does not inject real GPU OOM, exercise actual shared KMT/Mach
creation or prove asynchronous device-error liveness, fullformat coverage or game
compatibility. Installed runtime selection is not changed by this source.

## Captured-report gate

Run `python3 tests/vibewine/initializer-transaction/gpu/test_analyze.py` for
the bounded parser controls. `analyze.py --stdout FILE --exit-code CODE`
reads captured stdout only. All seven stages are mandatory; missing stages,
unsupported outcomes and contaminated/malformed output cannot pass. The two
intentional `--negative-control upload|depth` modes require their exact failing
stage, detail, HRESULT and process exit 20. Runtime identity and actual process
exit capture are responsibilities of the isolated launch owner.

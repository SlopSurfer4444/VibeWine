# Generic resource DXGI usage — 28 October 2026

`TResourceBase::GetDXGIUsage` previously returned zero for every resource. It
now reports the three generic bind capabilities shared by buffers and 1D/2D/3D
textures: shader resource → `SHADER_INPUT`, render target →
`RENDER_TARGET_OUTPUT`, unordered access → `UNORDERED_ACCESS`. Null output still
returns `E_INVALIDARG`. The query overwrites the output and leaves the resource
description unchanged.

This is limited GetUsage parity. CPU usage remains `DXGI_CPU_ACCESS_NONE`;
shared, back-buffer, read-only and discard-on-present flags are not inferred.
No game, ANGLE, CEF, GPU, installed runtime or FPS acceptance is claimed.

## Source decision and attribution

The small-project [Overpass patch by McKack](https://github.com/McKack/overpass-runtime/blob/b90803d4186642ee79eb9bedbc91c555a9c86db8/overpass/patches/0002-d3d11-report-DXGI-usage-from-bind-flags-and-CPU-acce.patch)
identified the generic zero-usage problem and motivated this change. Its
[pinned repository license](https://github.com/McKack/overpass-runtime/blob/b90803d4186642ee79eb9bedbc91c555a9c86db8/LICENSE)
is LGPL 2.1, compatible with DXMT's existing LGPL-2.1-or-later component
license. This is an adaptation of the idea with independently checked
semantics, not a literal port: its vertex/constant-buffer shader-input mapping
and dynamic/staging CPU mapping were deliberately omitted.

[Microsoft GetUsage](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getusage)
describes expected resource usage; [Microsoft DXGI_USAGE](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-usage)
defines the SRV/RTV/UAV capability bits and the encoded CPU field. Its low
nibble is an enum-like value (`NONE=0`, `DYNAMIC=1`, `READ_WRITE=2`, `SCRATCH=3`,
mask 15), not individual read/write flags. Neither `D3D11_USAGE` nor
`CPUAccessFlags` may be directly ORed into that field. Back-buffer/read-only/
discard status depends on swap-chain ownership; RTV or immutable usage alone
does not establish it.

[Wine f343ca60 resource implementation](https://github.com/wine-mirror/wine/blob/f343ca6085bfac807c33e9c1a8a43105071f1416/dlls/dxgi/resource.c#L393-L423)
uses its [three-bind-bit helper](https://github.com/wine-mirror/wine/blob/f343ca6085bfac807c33e9c1a8a43105071f1416/dlls/dxgi/utils.c#L490-L501).
Its [D3D11 test expectation](https://github.com/wine-mirror/wine/blob/f343ca6085bfac807c33e9c1a8a43105071f1416/dlls/d3d11/tests/d3d11.c#L2667-L2689)
and [resource matrix](https://github.com/wine-mirror/wine/blob/f343ca6085bfac807c33e9c1a8a43105071f1416/dlls/d3d11/tests/d3d11.c#L2741-L2816)
cover buffer and texture dimensions with DEFAULT/CPU=0/Misc=0, including
constant, vertex, index and stream-output buffers with expected usage zero.
Those tests were inspected, not run on native Windows here. Wine's repository
is LGPL-2.1-or-later; no Wine code was copied.

[DXVK 9c5551cb implementation](https://github.com/doitsujin/dxvk/blob/9c5551cbefa8a6782d1b94554e254a2ff483e254/src/d3d11/d3d11_resource.cpp)
also provides the three bind translations but includes constant-buffer shader
input and maps dynamic/staging to CPU codes 1/2 using Usage alone. That is a
different implementation policy, not proof of Windows behavior. The
[pinned DXVK license](https://github.com/doitsujin/dxvk/blob/9c5551cbefa8a6782d1b94554e254a2ff483e254/LICENSE)
is zlib/libpng; no DXVK code was copied. Native Windows probes remain necessary
before extending the CPU, shared, constant-buffer or swap-chain behavior.

## Focused verification

Run `python3 tests/vibewine/dxgi-resource-usage/run.py --arch arm64` (or
`--arch x86_64`). Add `--baseline-ref a6637cfe2c5f6a2c13ec17290e359f86b163de6e`
to require the old exact usage-bit failures and unchanged control passes.

The runner extracts the actual production `GetDXGIUsage` and aggregated
`IDXGIResource::GetUsage` forwarding bodies. It compiles them against the
project's real `d3d11_3.h`/DXGI/native Windows declarations with UBSan. It
substitutes resource construction and the rest of the device/COM/GPU system;
it does not instantiate the full production resource or exercise a Windows
runtime.

Both ARM64 and x86_64 runs passed 42 cases / 1,792 assertions each. Six
descriptor shapes cover all four production resource classes (Buffer,
Texture1D, Texture2D1, Texture3D1) and the public Texture2D/Texture3D descriptor
forms. The cases cover isolated and combined SRV/RTV/UAV bits, the 4×4
Usage/CPU-access combinations both unbound and SRV-bound, unrelated bind bits,
three shared-flag combinations, null output, output overwrite/adjacent guards,
repeated query and unchanged descriptor bytes. Some CPU/bind/misc combinations
are intentionally synthetic and would be rejected at resource creation; these
test query isolation, not creation validity or full API parity. The old source
fails all 36 capability cases with exactly `FAIL usage bits` and passes the
six control cases on each architecture.

## Evidence lifetime

The usage branch owns its small test logs under
`outputs/integrated-wave-20261023/memory-queue-wave-next/usage` in the task
workspace: raw logs 7 days, notes 30 days, combined cap 3 MiB, active review and
recovery holds excluded from cleanup. Manual cleanup must recheck exact
ownership and holds before deleting these branch-owned files. Fixture build
directories are automatically removed after each run. Source/tests and this
decision record are durable component results. No telemetry producer or
installed retention policy changed.

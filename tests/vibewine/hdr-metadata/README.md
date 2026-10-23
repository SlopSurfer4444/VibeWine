# HDR metadata API regression

This fixture compiles the current production `SetHDRMetaData`,
`Presenter::changeHDRMetadata` and `Presenter::synchronizeLayerProperties` bodies
with ASan/UBSan. It extracts both metadata structure layouts and presenter
snapshot declarations from this checkout. Native display discovery and pipeline
creation are injected; no actual window, display, Wine process, GPU, installed
runtime, profile, or user setting is touched. Generated sources/binaries live in
a temporary directory removed at process exit.

```sh
python3 tests/vibewine/hdr-metadata/run.py --mutation-check
python3 tests/vibewine/hdr-metadata/run.py --baseline-ref fab325e0adcd639d822dbfac87a1b840ccb2d4bf
```

The baseline check requires the historical unconditional-success body to fail at
its silent discard of valid HDR10 metadata. Mutations cover unconditional success,
ignored NONE/HDR10 sizes, lost metadata copies and omitted snapshot metadata.
The normal fixture also covers null HDR10, unknown/HDR10PLUS types, every mapped
field, no mutation on rejected requests, an unaligned transient caller payload,
clear/idempotent clear and preservation of earlier queued frame metadata/revision.
The API accepts NONE with size zero and ignores its optional pointer. HDR10 needs
its exact API-structure size and a nonnull pointer; unsupported types return
`DXGI_ERROR_UNSUPPORTED`.

The [API contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/nf-dxgi1_5-idxgiswapchain4-sethdrmetadata)
distinguishes metadata from pixel color-space interpretation. A passing fixture
establishes source validation, conversion/copy and snapshot behavior, not functional
HDR, monitor metadata delivery, matching native compiler/bridge linkage or visual
correctness.

## W5 remaining live acceptance

Run only after the integration owner admits a matched candidate and coordinates
exclusive GPU/window ownership. Record exact candidate/module identities and each
geometry independently; the portable WSI fixture uses injected Win32 state and
cannot establish these OS observations.

| Scenario | Required observation | Current evidence |
| --- | --- | --- |
| Window resize/Retina transition | Win32 client, Cocoa point frame, backing pixels, render texture and acquired drawable size; input/clip transform at corners | Not run by this fixture |
| Supported resolution/fullscreen roundtrip | Windowed/borderless/native fullscreen transitions, symmetric return to initial geometry and restored display capture; no stale view owner | Not run by this fixture |
| Mission Control/focus loss | Visible cursor release, return focus, observable target response and bounded recapture only when intended | Not run by this fixture |
| Controller reconnect | Reconnection produces target response without stale held input or process death | Not run by this fixture |
| Quit/crash/relaunch | Normal menu Quit twice with Steam left open, fresh target process/window on relaunch; separate bounded crash/recovery case | Not run by this fixture |
| HDR metadata | Matched native path, display capability/readback and visual color/luminance check with appropriate color space | Not run by this fixture |

The separate parameterized hub fixture can exercise this component's fullscreen
owner-token source checks:

```sh
python3 renderer/wave7/wsi-tests/run_fixture.py --source components/dxmt --mutation-check
python3 renderer/wave7/wsi-tests/run_fixture.py --source components/dxmt --baseline-ref fab325e0adcd639d822dbfac87a1b840ccb2d4bf
```

Those commands run from the hub root. They cover old-fail/new-pass HWND reuse at
present-rect/position callbacks, with move/topmost and absent/replacement tokens;
all Win32 callbacks and HWND lifecycle in that fixture are injected state.

Fullscreen capture ownership now uses a nonreused pointer-width token, rather
than a state address. One atomic allocator lives in DXGI, because WSI utilities
are statically linked into multiple PE DLLs. The existing HWND property stores
that opaque token; entry/leave/detach and updates preserve a newer capture when
callbacks reuse the same HWND and state storage. Zero and the maximum token are
reserved; exhaustion refuses capture without mutation, and never wraps.

`DXMTAllocateFullscreenToken` is a new **internal PE import/export**. Rebuild and
ship the matching DXGI with D3D11 and optional D3D12; an older DXGI lacks this
export. Existing DXGI export ordinals stay unchanged. This changes neither the
native WineMetal bridge nor its rendering ABI. Source fixtures exercise two
independent production WSI capture bodies against the actual shared allocator;
actual matched DLL linking/loading remains the integration owner's W8 gate.

Generation checks include same-state recapture at update and cleanup callbacks,
getter callbacks supported by the fixture, failed property installation, absent
properties and injected counter exhaustion. The counter exhaustion test changes
only fixture state and restores it after checking refusal. These remain injected
Win32 lifecycle checks; they are not evidence of real OS handle reuse.

The existing [AllocateLocallyUniqueId API](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-allocatelocallyuniqueid)
was considered for reuse. Its 64-bit LUID cannot be losslessly represented by the
same HANDLE property on a 32-bit consumer; truncation would discard uniqueness.
The shared allocator instead makes pointer-width exhaustion explicit and testable.

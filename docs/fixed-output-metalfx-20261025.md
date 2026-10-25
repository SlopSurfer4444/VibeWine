# Fixed-output spatial MetalFX experiment (2026-10-25)

Owner: VibeWine FPS experiment; implementation and CPU fixture evidence only.
Native validation, installation, runtime activation, image quality, input geometry,
and game timing are separate acceptance steps. This change creates no background
producer or capture and no persistent data other than source and focused tests.

## Admission and defaults

The Meson option `-Dvibewine_fixed_output_metalfx=true` enables the build switch
`DXMT_FIXED_OUTPUT_METALFX`, which defaults to `0`. An enabled build also
requires the existing `DXMT_METALFX_SPATIAL_SWAPCHAIN=1` environment variable, a
supported MetalFX spatial device, and these explicit configuration options:

```ini
d3d11.metalfxPerformanceUpscale = True
d3d11.metalfxOutputWidth = 1920
d3d11.metalfxOutputHeight = 1080
```

The boolean defaults to `False`. Both output dimensions must be positive and no
larger than `D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION` (16384). Missing, half-specified,
negative, or excessive dimensions reject swapchain initialization/resize with
`E_INVALIDARG`, before replacing committed buffers. No monitor-size inference,
window resize, display mode override, registry edit, or automatic activation is
performed. Use the existing per-application config/environment route. A build
switch or config file is not evidence that an installed game used this path.

When the feature is disabled, the existing spatial multiplier mode is retained:
`d3d11.metalSpatialUpscaleFactor` determines scaled output from game render size.
Fixed-output mode ignores that multiplier and keeps the native contents scale.

## Render versus output

For a game-selected 1280x720 backbuffer and explicit 1920x1080 output, GetDesc,
GetDesc1, GetBuffer, source size, and the scaler input remain 1280x720. The output
texture and requested drawable size are 1920x1080. Explicit subsequent render
resizes do not modify that output request. The game must choose its lower render
resolution; this feature does not rewrite its render targets or settings.

MetalFX is admitted only for strictly larger, exactly matching-aspect output,
single-sample input, SDR G22/P709 color space, and supported RGBA8/BGRA8 UNORM or
sRGB formats. Cross-products use 64-bit arithmetic; no aspect rounding is used.
Equal-size, downscale, mismatched-aspect, MSAA, HDR/scRGB, and unsupported-color
cases use ordinary presentation at the requested output. A live HDR color-space
switch also disables the optional spatial encode for that frame. The existing
presentation color, gamma, and metadata processing is retained.

The acquired drawable determines final viewport geometry. The fixed-output
presentation hook fits the source aspect into the actual acquired image and
clears uncovered pixels; requested drawable dimensions are not a receipt of
native display geometry. DS3 borderless render-size preservation and owner-thread
window handling remain in their existing WSI route. ResizeTarget's conservative
borderless refusal remains in force. Native fullscreen mode requests keep their
existing semantics; fixed-output scaling does not force a desktop mode.

## Failure and lifetime

Public input and optional scaled resources are prepared before publication.
Failed input/output texture allocation retains the committed descriptors,
resources, and layer request. In fixed-output mode a nil scaler or fence clears
the candidate scaled resources and publishes a usable public input with ordinary
presentation; it never reuses an earlier generation's scaler. Legacy multiplier
mode retains its existing allocation-failure result.

Queued frames retain their own input texture, optional output texture, scaler,
and immutable presentation configuration. Fallback selection occurs before any
optional texture dereference. Existing logical-frame admission, scene submission,
semaphore release, nil-drawable handling, and completion retirement remain on the
same queue paths.

## Reuse and provenance

Inspected donor: [slavazhulanov/dxmt a642d7045a52a731ae5447cd01c58e5d48a18778](https://github.com/slavazhulanov/dxmt/commit/a642d7045a52a731ae5447cd01c58e5d48a18778),
which separates game render and native desktop output, and its
[LGPL-2.1-or-later license](https://github.com/slavazhulanov/dxmt/blob/a642d7045a52a731ae5447cd01c58e5d48a18778/LICENSE).
The dimension-separation concept and opt-in configuration naming were reused.
Its older fullscreen/output-monitor code was not transplanted: that code inferred
desktop dimensions and forced fullscreen modes, while this version uses an
explicit size pair and preserves the current transactional WSI/presentation
ownership. Existing VibeWine buffer preparation, spatial scaler wrappers, retained
configuration, ordinary scaling, and gamma/HDR presentation are reused. The small
geometry helper avoids a new rendering dependency.

## Checks and limits

`tests/vibewine/fixed-output-metalfx/run.py` extracts the production constructor
configuration block, resize and layer property methods, and queued Present branch
into CPU resource/configuration doubles and uses the production geometry header.
The build macro is exercised both off and on, and retained callback resources are
checked across resize. Its baseline and mutation controls distinguish fixed output from
the previous render-times-multiplier behavior. Native presentation geometry is
checked separately by the integrating owner. Read the fixture README for exact
coverage; a CPU pass does not establish GPU, Wine, display, or game behavior.

Spatial MetalFX operates on color only. This experiment adds no temporal
reconstruction, motion-vector integration, DLSS, game compatibility guarantee, or
FPS claim. A meaningful performance comparison still requires native render,
lower render with ordinary scaling, and lower render with MetalFX at the same
observed output and controlled GPU-bound scene, plus UI/color/input quality and
latency checks.

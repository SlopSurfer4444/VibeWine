# Explicit color discard hint experiment, 25 October 2026

`vibewine_discard_hints` / `DXMT_DISCARD_HINTS` is default-off. This is an
independent explicit-API experiment, separate from the existing adjacent-full-
clear store optimization. It does not establish DS3 API use, GPU bandwidth savings,
FPS, or game compatibility. Disabled API paths retain the original diagnostic
no-op and never introduce pass boundaries.

## Admission and ordering

`DiscardResource` and a full `DiscardView`/`DiscardView1` RTV call may change only
the immediately preceding recorded render's single base color attachment
`Store` to `DontCare`. The API ends its current pass in command order, then
records a replayable state command retaining the `Texture` descriptor. At encode
time the helper checks the current allocation, descriptor, base view key and
native texture identity. Nonlayered D3D11 `TEXTURE2D` passes use array length zero;
arrays and array RTV aliases are deliberately unsupported.

Admission requires DEFAULT usage, no CPU/misc flags, one mip/slice/sample, a
render-target bind without depth/stencil bind, private standalone nonmapped
nonheap native storage, exact native/render extents, no depth/stencil attachment,
no resolve attachment and ordinary Store. Alternate/reinterpret views are
rejected even if they cover the whole image. DiscardView accepts only base
TEXTURE2D RTVs; SRV/UAV/DSV discard remains a no-op. Any rect pointer or nonzero
rect count is conservatively unsupported, including an explicit full rectangle.
This is narrower than the API's permitted behavior; ignoring a hint is legal.

The helper never searches past an intervening encoder. A copy, sampled read,
compute pass, clear, resolve, event, timestamp or submitted command-buffer
boundary therefore prevents backward admission. No future load changes and no
content-validity table are created. Draws, barriers, query state, fences and
resource owners remain intact. An earlier render can merge into the marked
render without changing its final Store action. Existing signature checks reject
merging the marked render into a later render, preserving the discard boundary.
Reads after discard see unspecified discarded content; tests redefine discarded
regions before asserting pixels. No newly uninitialized bytes are accepted as
correct pixels.

## Checked reuse and attribution

The prior bounded neighbor audit and canonical
`docs/integrations/neighbor-wave-20261025.md` in the parent integration were read.
Existing DXMT allocation/view ownership, ordered state lambdas and encoder
signature checks are reused rather than adding an allocation validity registry.
The old adjacent-clear helper does not consume explicit API intent and is
unchanged.

Exact [DXVK cbd2206d entrypoints](https://github.com/doitsujin/dxvk/blob/cbd2206d72d3d9dd9941d5d868bb03a858e85f9b/src/d3d11/d3d11_context.cpp),
[store admission and discardRenderTarget](https://github.com/doitsujin/dxvk/blob/cbd2206d72d3d9dd9941d5d868bb03a858e85f9b/src/dxvk/dxvk_context.cpp)
and its [zlib/libpng license](https://github.com/doitsujin/dxvk/blob/cbd2206d72d3d9dd9941d5d868bb03a858e85f9b/LICENSE)
were inspected. DXVK's Vulkan render-pass/deferred-discard machinery is not
transplanted: DXMT already owns mutable recorded Metal attachment descriptors.
This is an independent LGPL-2.1-or-later Metal adaptation; no DXVK source excerpt
was incorporated. Existing source notices remain. DXVK source attribution:
Copyright 2017 Philip Rebohle, 2019 Joshua Ashton, 2019 Robin Kertels and 2023
Jeffrey Ellison, distributed under zlib/libpng.

Official [DiscardResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-discardresource),
[DiscardView](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-discardview)
and [DiscardView1](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-discardview1)
semantics were checked: discard declares old contents unnecessary; views can
cover resource subsets; a null rect pointer denotes full-view intent. The initial
implementation deliberately leaves many valid calls as no-ops.

## Verification and remaining acceptance

Run `python3 tests/vibewine/discard-hints/run.py`. It compiles actual production
API methods, state lambda, helper and encoder relation/signature/dependency
methods into bounded CPU adapters with ASan/UBSan. Default and explicit-off keep
Store; enabled full Resource/View/View1 changes Store; a historical API no-op and
wrong-action mutations must fail behavioral assertions. Controls cover allocation,
view, pending resolve, mip and shared exclusions. Replays, descriptor lifetime,
pass merging, fences/query/command preservation and unrelated encoders are checked.
Adapters do not reproduce the complete COM implementation, native Metal encoder,
full command-queue scheduling or allocator behavior.

The prepared [offscreen GPU fixture](../tests/discard_hints_gpu_fixture.md) checks
25 defined overwritten/retained images across eight API/control groups, including
record/replay and partial/mip/slice/depth/stencil exclusions. Root must compile it
and run both matched default-off and enabled variants plus connected controls.
GPU pixel parity alone does not establish action admission or benefit: root must
also inspect actual native attachment Store actions/capture and compare traffic
or timing if claiming performance. No GPU, game, runtime install or full matched
bundle build was performed by this source owner.

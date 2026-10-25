# Canonical empty color slots (default-off experiment)

`DXMT_EMPTY_COLOR_SLOTS=1` skips an indexed getter plus eleven assignments only for a fully canonical empty color slot of a freshly created native Metal render-pass descriptor. It requires both texture handles zero; load DontCare; store Store; all six source/resolve indices zero; and individual double representations exactly +0,+0,+0,1. It never compares structure padding. Negative zero, NaN and every other noncanonical nil slot use the original assignment path. Active attachments use that original path as well. Depth/stencil, global properties, command-buffer creation, fences and ABI are unchanged.

`DXMT_PERF_REF_W4_GETTERS=1` takes priority over this experiment. With that historical reference selected, default/off/on empty-slot settings all retain eight slot visits and 88 indexed getter evaluations per descriptor; canonical suppression and its local constants are compiled out. The tests count actual getter-expression evaluations, compare real descriptor snapshots, and reject a mutation that re-enables suppression in the reference path.

Root owns adding the default-off option to the common matched build flag mapping and GPU acceptance. This isolated branch edits only the native create-descriptor hunk and these new tests. The source base is a21579fb2a03aef5fbe93d7bcba1ea882e63a192.

Run `python3 tests/vibewine/empty-color-slots/run.py --bench`. The fixture extracts the whole production native `_MTLCommandBuffer_renderCommandEncoder` function, actual ABI and actual `InitializeRenderPassInfo`. One explicit C-to-Objective-C++ void-pointer cast is mechanical. A test-only counter immediately before the indexed getter counts surviving slot visits. A CPU receiver copies the real Metal descriptor passed to it; it creates no actual command buffer or render encoder. Every observed descriptor, getter and setter is a real public Metal descriptor API call, not a mock selector implementation. The benchmark omits the test counter and uses nil as the command-buffer receiver.

Twenty-one ASan/UBSan scenarios compare complete assigned public color fields and additional store options, global descriptor settings, depth/stencil state, status and nil return against default/off. The 0/1/4/8 and sparse {0,3,7} branches use nil attachments with noncanonical load actions as sentinels. They do **not** substitute for active render-target testing. Controls cover each index, load/store/resolve-only action, each clear component, signed zero, NaN payload and nil depth/stencil/global settings. Mutations to skip every nil attachment, use numeric double equality, omit resolveDepthPlane, and bypass the optimization must fail snapshot/count controls. Full original Objective-C source syntax is checked in Wine/native/getter-reference modes with the option on.

ABBA measures real descriptor creation/setters/release and autoreleasepool completion, excluding actual encoder creation and GPU work. On macOS27.0 build26A428, arm64, SDK27.0, the initial median ns/call were:

| Pattern | Off | On |
| --- | ---: | ---: |
| Eight canonical nil slots | 1030.30 | 255.29 |
| Seven canonical, one noncanonical nil | 1034.40 | 362.40 |
| Eight noncanonical nil, no hits | 1027.57 | 1021.39 |

The no-hit ranges overlapped; this experiment did not establish a no-hit regression. These are bounded host CPU observations, not GPU time or FPS. With I matched slots, source-level count reduction is I indexed getters + 11I setters; the outer colorAttachments getter remains. No allocation-count claim follows.

Before a source checkpoint, root GPU checks must cover actual 1/4/8 MRT, sparse slots, depth-only, MSAA/resolve, mip/array/3D attachments, Present, queries and error/validation paths on the matched runtime. Nonzero real source/resolve texture guards were not exercised by this host-only fixture. Private validator metadata and cross-macOS behavior remain outside its proof. Keep the flag off by default; reject wrong pixels, changed progress/topology, changed meaningful validation outcomes or unacceptable no-hit/change-heavy CPU/frame-tail regression.

Primary evidence: Apple documents default color load DontCare, store Store and clear (0,0,0,1) in https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Render-Ctx/Render-Ctx.html . The installed public MTLRenderPass.h documents zero attachment indices, nullable source/resolve textures and storeActionOptions default None. An earlier descriptor-only snapshot proof is in next-round/descriptor-default-proof/receipt.json. This experiment still requires root's real encoder/GPU acceptance.

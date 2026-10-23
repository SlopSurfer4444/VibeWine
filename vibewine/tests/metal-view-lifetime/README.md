# Metal-view retirement on the existing exclusive lease ABI

This adapts the deferred hierarchy retirement in Gabe Zimbric's
[`0dad72df204142a2f276495af5baaf4d932ea91f`](https://github.com/gzimbric/wine-arm64-macos/commit/0dad72df204142a2f276495af5baaf4d932ea91f).
The donor's `cocoa_window.m` is LGPL-2.1-or-later, with the existing Wine and
CodeWeavers notices. Retain that license and the donor attribution
`Gabe Zimbric <10481275+gzimbric@users.noreply.github.com>`.

The donor reports Metal HUD work that retains a layer's delegate view and needs
that view's window. Its shared-client counter and view reuse are incompatible
with VibeWine's existing exclusive producer lease. This adaptation preserves the
lease: releasing it synchronously clears the delegate, hides the retired view
and relinquishes the current generation. Immediate acquisition gets a new native
view; removing an older generation cannot clear the newer lease. Content disposal
synchronously removes notification observers and marks the content terminal.
It includes retired Metal children when deciding whether to defer hierarchy
removal. A retained window keeps the hierarchy alive through earlier main-queue
FIFO blocks even after the C caller closes and releases that window. Detached
views and content without Metal children retain their synchronous cleanup path.

The native fixture compiles the actual production acquisition, retirement,
disposal and `willRemoveSubview:` bodies. It uses real unshown AppKit windows,
NSViews, CAMetalLayers, main GCD FIFO execution and the ordinary NSRunLoop.
`OnMainThread` executes inline because the fixture already runs on main; the
fixture does not exercise Wine's request-source dispatcher. The Metal-view
initializer uses a real CAMetalLayer without a device or drawable, and unrelated
WineWindow drawing/GL callbacks are no-ops. No game, installed runtime, visible
window, focus action, Space switch or application activation is involved.

From the Wine component root on macOS with Xcode and an available WindowServer
session:

```sh
python3 vibewine/tests/metal-view-lifetime/run.py
python3 vibewine/tests/metal-view-lifetime/run.py --baseline
python3 vibewine/tests/metal-view-lifetime/run.py --mutation missing-terminal-guard
python3 vibewine/tests/metal-view-lifetime/run.py --arch x86_64
```

The runner always enables AddressSanitizer and UndefinedBehaviorSanitizer and
preserves the production copyright/license header in the extracted fixture.
Compilation is bounded to 60 seconds and execution to 15 seconds. Every mode
rejects sanitizer/runtime-error diagnostics, requires the exact completion and
balanced ownership marker, and rejects unexpected or duplicate failures. The
runner removes generated source and binary using `TemporaryDirectory`. The baseline
uses the local pre-adaptation commit `d2237a9a82d9960360ce8400242ddeae6646c347`
and must fail exactly three window-association checks. The mutation must detect
reacquisition from a terminal content view whose old generation was already
retired. Current source checks three queued callbacks, overlap rejection,
immediate reacquisition, old-generation isolation, synchronous delegate cleanup,
terminal disposal, release after parent disposal, missing window/device, closing
windows, ordinary non-Metal disposal and balanced ownership of six native views,
six content views and five windows. Callback comparisons use scalar identities
so those callbacks cannot themselves retain the windows under test.

Verified 2026-10-23 on native arm64 and translated x86_64. Both passed with balanced
ownership. The baseline reached its three expected failures. AppKit emitted
`IOSurfaceSharedEventAddEventListener failed: 10000003` diagnostics in this
sandbox; the fixture acquires no drawable and submits no GPU work. The complete
production `cocoa_window.m` also passed x86_64 Objective-C syntax checking against
the retained configured Wine build headers, with two existing `nil`/nonnull
warnings outside this change. This is neither a fresh Wine build nor a matched
renderer/native bridge build.

The guarantee is specifically the lifetime needed by main-queue blocks already
enqueued before retirement. It does not cover arbitrary future, delayed or
recursively enqueued jobs. Apple's private Metal HUD code and the donor's
reported crash were not executed or independently reproduced. This check does
not establish an actual HUD crash repair, game compatibility or performance.
Removal still requires main-queue progress; retired native objects can remain
held while that queue is stalled. Duplicate releases/disposals are outside the
existing owning C-handle contract and are not made valid by this adaptation.

The newer Wine/WineAndAqua
[`1300ec99c7ed7461df1d400257aa83165faf0069`](https://github.com/wine-mirror/wine/commit/1300ec99c7ed7461df1d400257aa83165faf0069)
autorelease change targets `WineMetalSwapChain` wrappers absent from this driver.
VibeWine's existing legacy Metal wrappers already route their Objective-C work
through `OnMainThread`, which has a caller autorelease pool, and `PerformRequest`,
which has a pool around each main-thread block. No matching legacy wrapper
pool gap was established, so those newer ABI wrappers are not imported.

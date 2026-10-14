# Synchronization diagnostics candidate

Offline successor to installed diagnostic renderer `be8769bd7cbc63d129e36bad367a041a4cbdf76a5b74af04c3ba4b946efdbf8a`. This is instrumentation, not an FPS optimization. No live installation or game verification is claimed.

New events in the unchanged version-1 trace record layout:

| Type | Meaning | object | flags | detail0 / detail1 |
|---|---|---|---|---|
|30|CPU duration of staging Map's WaitCPUFence|awaited sequence|MapType|queue / frame|
|31|CPU duration of GetData S_FALSE-triggered Flush|query pointer|query type|queue / frame|
|32|CPU duration of WaitUntilGPUIdle's WaitCPUFence|awaited sequence|0|queue / frame|
|33|Instantaneous chunk publication commit attribution|queue|commit reason|chunk sequence / frame|

Reasons: Unknown=0, Present=1, StagingMap=2, GetDataNotReady=3, ExplicitFlush=4, GPUIdle=5, FenceSignal=6, FenceWait=7, PassPromotion=8. FenceWait labels the flush preceding GPU wait encoding; it is not a CPU wait timing. Deferred context Commit remains its original no-op. Only actual queue publication emits33. GetData that does not flush emits no31; repeated no-op Flush calls may produce31 without a chunk publication.

Scopes retain entry generation, including unarmed generation0, and same-queue nested fallback preserves the initiating reason. Cross-queue scope state restores correctly. Stop/rearm spanning scopes cannot attribute the new capture to the old operation. Existing chunk events20–24 retain their independent publication generation; if an initiating scope spans a capture boundary,33 is omitted and that chunk has no reason attribution. This deliberate boundary exclusion must not be synthesized as a known reason. No resetting producer indexes or capture storage.

When unarmed, scopes perform small thread-local bookkeeping and generation checks but no new timestamps or payloads. Diagnostic events are unfiltered, bounded by the existing ring capacity/drop counter. No altered fences, flushes, queue policy, frame limits, API return values or waits. Full trace volume is higher; use short capture windows and fresh private sessions/processes, never reset a live buffer.

NextDrawable is already timed by existing type12. No per-frame focus/visibility querying was added: preserve an external capture note rather than add potentially perturbing native/window calls. No shader changes.

Build: local copied build.py, same LLVM-MinGW compiler and donor Metal bridge as predecessor. verification.json binds candidate PE, source delta and preserved imports/exports/helper. Consumer source is exported under consumer; installed diagnostic/controller snapshots remain separate. Exported build.sh omits only the private predecessor-receipt writer. Source generation/reentry independent review and synthetic tests are separate from PE build success. No performance uplift or DS3 crash fix inferred from this instrumentation.

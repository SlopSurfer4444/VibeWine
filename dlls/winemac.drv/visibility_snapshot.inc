/* Private candidate API. Include in installed-source d3dmetal_objc.m before
 * @implementation WineMetalLayer. Private startup caller is in visibility_startup.inc.
 * Completion receives immutable JSON bytes on a worker queue, never AppKit
 * objects. A nil payload reports collection/serialization failure so the caller
 * can release request-owned resources. Exactly one admitted request per host
 * process. */
#include "visibility_guard.h"
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <libproc.h>

static double w7vis_now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) return -1.0;
    return t.tv_sec + t.tv_nsec / 1e9;
}

static NSArray *w7vis_rect(CGRect r)
{
    return @[@(r.origin.x), @(r.origin.y), @(r.size.width), @(r.size.height)];
}

static NSString *w7vis_identity(id object)
{
    return object ? [NSString stringWithFormat:@"%p", (void *)object] : @"nil";
}

static NSDictionary *w7vis_collect_main(CAMetalLayer *layer, uint64_t requestID)
{
    NSCAssert([NSThread isMainThread], @"visibility collector requires Cocoa main");
    id delegate = layer.delegate;
    NSView *view = [delegate isKindOfClass:[NSView class]] ? delegate : nil;
    NSWindow *window = view.window;
    NSApplication *app = NSApp;
    NSScreen *screen = window.screen;
    NSNumber *displayID = screen.deviceDescription[@"NSScreenNumber"];
    struct proc_bsdinfo birth = {0};
    BOOL haveBirth = proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &birth, sizeof(birth)) == sizeof(birth);
    NSMutableDictionary *record = [NSMutableDictionary dictionaryWithDictionary:@{
        @"schema": @1, @"requestID": @(requestID), @"pid": @(getpid()),
        @"sampleMonotonicRawSeconds": @(w7vis_now()), @"sampleThread": @"cocoa-main",
        @"status": @"sampled", @"phase": @"startup", @"settleDelayMs": @1000,
        @"processScope": @{@"source": @"PEB.ProcessParameters.ImagePathName", @"windowsImagePath": @W7VIS_DS3_IMAGE}, @"coordinateSystems": @{@"windowFrame": @"AppKit-screen-points", @"viewBounds": @"view-local-points", @"drawableSize": @"pixels"},
        @"birth": haveBirth ? @[@(birth.pbi_start_tvsec), @(birth.pbi_start_tvusec)] : (id)[NSNull null],
        @"app": app ? @{@"active": @(app.active), @"hidden": @(app.hidden), @"running": @(app.running)} : (id)[NSNull null],
        @"owner": @{@"layer": w7vis_identity(layer), @"delegate": w7vis_identity(delegate), @"view": w7vis_identity(view), @"superview": w7vis_identity(view.superview), @"window": w7vis_identity(window),
            @"viewLayerMatchesRequestedLayer": @(view && view.layer == layer), @"viewAttachedToWindow": @(window != nil),
            @"wineMetalView": @(view && [view isKindOfClass:NSClassFromString(@"WineMetalView")]),
            @"wineContentSuperview": @(view.superview && [view.superview isKindOfClass:NSClassFromString(@"WineContentView")])},
        @"layer": @{@"bounds": w7vis_rect(layer.bounds), @"hidden": @(layer.hidden), @"opacity": @(layer.opacity),
            @"contentsScale": @(layer.contentsScale), @"drawableSize": @[@(layer.drawableSize.width), @(layer.drawableSize.height)],
            @"framebufferOnly": @(layer.framebufferOnly), @"displaySyncEnabled": @(layer.displaySyncEnabled), @"opaque": @(layer.opaque)},
        @"view": view ? @{@"bounds": w7vis_rect(view.bounds), @"frame": w7vis_rect(view.frame), @"hidden": @(view.hidden),
            @"hiddenOrHasHiddenAncestor": @(view.hiddenOrHasHiddenAncestor), @"alphaValue": @(view.alphaValue)} : (id)[NSNull null],
        @"window": window ? @{@"windowNumber": @(window.windowNumber), @"frame": w7vis_rect(window.frame),
            @"visible": @(window.visible), @"miniaturized": @(window.miniaturized), @"onActiveSpace": @(window.onActiveSpace),
            @"occlusionState": @(window.occlusionState), @"keyWindow": @(window.keyWindow), @"mainWindow": @(window.mainWindow),
            @"hidesOnDeactivate": @(window.hidesOnDeactivate), @"alphaValue": @(window.alphaValue), @"level": @(window.level),
            @"screenDisplayID": displayID ?: (id)[NSNull null]} : (id)[NSNull null]
    }];
    return record;
}

/* latestMonotonicRawSeconds uses this host's CLOCK_MONOTONIC_RAW. A timeout
 * stops interpretation, not queue execution: the copied block can remain
 * queued until Wine's request source runs or the process exits. */
__attribute__((visibility("default")))
int macdrv_request_metal_visibility_v1(CAMetalLayer *layer, uint64_t requestID,
                                     double latestMonotonicRawSeconds,
                                     void (^completion)(NSData *))
{
    static atomic_bool admitted = ATOMIC_VAR_INIT(false);
    double now = w7vis_now();
    bool expected = false;
    if (!layer || !completion || !isfinite(latestMonotonicRawSeconds) || now < 0 ||
        latestMonotonicRawSeconds <= now || latestMonotonicRawSeconds > now + 2.0 ||
        !atomic_compare_exchange_strong(&admitted, &expected, true)) return 0;
    /* OnMainThreadAsync copies this block. Its object/block captures retain
     * layer and completion. No stack output pointer crosses this boundary. */
    OnMainThreadAsync(^{
        @autoreleasepool
        {
            NSData *data = nil;
            @try
            {
                NSDictionary *record;
                double sampled = w7vis_now();
                if (sampled < 0 || sampled > latestMonotonicRawSeconds)
                    record = @{@"schema": @1, @"requestID": @(requestID), @"pid": @(getpid()), @"phase": @"startup", @"status": @"expired-before-main-sample"};
                else
                {
                    record = w7vis_collect_main(layer, requestID);
                    if (w7vis_now() > latestMonotonicRawSeconds)
                        record = @{@"schema": @1, @"requestID": @(requestID), @"pid": @(getpid()), @"phase": @"startup", @"status": @"expired-during-main-sample"};
                }
                /* Nonfinite AppKit/Metal geometry is not valid JSON. Never
                 * allow validation or serialization exceptions onto main. */
                if (![NSJSONSerialization isValidJSONObject:record])
                    record = @{@"schema": @1, @"requestID": @(requestID), @"pid": @(getpid()), @"phase": @"startup", @"status": @"invalid-main-sample-json"};
                data = [NSJSONSerialization dataWithJSONObject:record options:0 error:NULL];
            }
            @catch (id exception)
            {
                (void)exception;
                /* Allocation/serialization can fail even for the fallback.
                 * Missing output is UNKNOWN; no exception escapes this task. */
                @try
                {
                    NSDictionary *fallback = @{@"schema": @1, @"requestID": @(requestID), @"pid": @(getpid()), @"phase": @"startup", @"status": @"main-sample-or-serialization-exception"};
                    data = [NSJSONSerialization dataWithJSONObject:fallback options:0 error:NULL];
                }
                @catch (id fallbackException) { (void)fallbackException; data = nil; }
            }
            /* Every executed admitted request must complete, including failed
             * serialization. The startup owner closes its directory fd there;
             * suppressing completion on failure would strand that ownership. */
            if (data.length > 32768) data = nil;
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                @autoreleasepool { completion(data); }
            });
        }
    });
    return 1;
}

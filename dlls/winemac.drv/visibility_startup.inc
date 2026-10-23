/* Included only by d3dmetal_objc.m after visibility_snapshot.inc. */
#include "visibility_guard.h"
static atomic_int w7vis_process_scope = ATOMIC_VAR_INIT(0);
static atomic_bool w7vis_startup_admitted = ATOMIC_VAR_INIT(false);

/* Called before native view creation on the actual Wine renderer thread.
 * ImagePathName bytes come from its PEB, never an inherited executable env. */
void macdrv_visibility_set_image_v1(const uint16_t *image, size_t bytes)
{
    int expected = 0;
    int scope = w7vis_exact_ds3_image(image, bytes) ? 1 : -1;
    (void)atomic_compare_exchange_strong(&w7vis_process_scope, &expected, scope);
}

static void w7vis_startup_from_layer(CAMetalLayer *layer)
{
    bool expected = false;
    const char *configured;
    NSString *directory;
    double started;
    if (atomic_load(&w7vis_process_scope) != 1 || atomic_load(&w7vis_startup_admitted)) return;
    configured = getenv("VIBEWINE_DS3_VISIBILITY_DIR");
    if (!configured || !*configured) return;
    if (!atomic_compare_exchange_strong(&w7vis_startup_admitted, &expected, true)) return;
    if (strnlen(configured, PATH_MAX) >= PATH_MAX) return;
    directory = [NSString stringWithUTF8String:configured];
    started = w7vis_now();
    if (!directory || started < 0) return;
    /* The copied delayed block retains the exact renderer-owned layer. There
     * is no AppKit read here and no per-frame I/O or synchronous main wait. */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC),
                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @autoreleasepool
        {
            const char *path = [directory fileSystemRepresentation];
            int dirfd = w7vis_open_private_directory(path);
            double now = w7vis_now();
            double deadline = fmin(started + 3.0, now + 1.99);
            if (dirfd < 0) return;
            if (now < 0 || now >= started + 3.0) { close(dirfd); return; }
            int accepted = macdrv_request_metal_visibility_v1(layer, 1, deadline, ^(NSData *data) {
                (void)w7vis_publish(dirfd, [directory fileSystemRepresentation], data.bytes, data.length);
                close(dirfd);
            });
            if (!accepted) close(dirfd);
        }
    });
}

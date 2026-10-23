#import <Foundation/Foundation.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>

static _Thread_local uint64_t fake_wall = 100, fake_cpu = 10;
static _Thread_local uint64_t fake_wall_delta, fake_cpu_delta;
static _Thread_local bool fail_wall, fail_cpu, reverse_wall, reverse_cpu;
static int fixture_clock_gettime(clockid_t clock, struct timespec *ts) {
    if ((clock == CLOCK_MONOTONIC_RAW && fail_wall) || (clock == CLOCK_THREAD_CPUTIME_ID && fail_cpu)) return -1;
    uint64_t value = clock == CLOCK_MONOTONIC_RAW ? fake_wall : fake_cpu;
    ts->tv_sec = value / 1000000000; ts->tv_nsec = value % 1000000000;
    return 0;
}
#define clock_gettime fixture_clock_gettime
#include "../../../src/winemetal/unix/wave7_acquire_diag.inc"
#undef clock_gettime

@interface FixtureLayer : NSObject
@end
@implementation FixtureLayer
- (id)nextDrawable {
    fake_wall = reverse_wall ? fake_wall - 1 : fake_wall + fake_wall_delta;
    fake_cpu = reverse_cpu ? fake_cpu - 1 : fake_cpu + fake_cpu_delta;
    return fake_wall_delta == 999 ? nil : self;
}
@end
#define CAMetalLayer FixtureLayer
typedef uintptr_t obj_handle_t;
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
struct unixcall_generic_obj_obj_ret { uintptr_t handle, ret; };
/* The diagnostic fixture never enables the admission policy. The separate
 * occlusion fixture compiles the real cache, guard and this same thunk together. */
static bool w7ds_should_skip_drawable(FixtureLayer *layer) { (void)layer; return false; }
static void w7ds_did_acquire_drawable(FixtureLayer *layer) { (void)layer; }
/* Replaced by run.py with the verbatim production thunk body. */
/* PRODUCTION_THUNK */

static void call(FixtureLayer *layer, uint64_t wall, uint64_t cpu) {
    fake_wall_delta = wall; fake_cpu_delta = cpu;
    struct unixcall_generic_obj_obj_ret request = {(uintptr_t)layer, 123};
    assert(_MetalLayer_nextDrawable(&request) == 0);
    assert(request.ret == (wall == 999 ? 0 : (uintptr_t)layer));
}
static void *parallel(void *layer) {
    for (unsigned i = 0; i < 200; ++i) call((FixtureLayer *)layer, W7AD_THRESHOLD_NS, 100);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    @autoreleasepool {
        FixtureLayer *layer = [[FixtureLayer alloc] init];
        if (!strcmp(argv[1], "disabled")) {
            assert(!w7ad_file); call(layer, W7AD_THRESHOLD_NS, 100); assert(!w7ad_file);
        } else if (!strcmp(argv[1], "path")) {
            assert(!w7ad_file); assert(setenv("DXMT_ACQUIRE_DIAGNOSTICS_DIR", argv[2], 1) == 0);
            w7ad_init(); assert(!w7ad_file);
        } else {
            assert(!w7ad_file); assert(setenv("DXMT_ACQUIRE_DIAGNOSTICS_DIR", argv[2], 1) == 0);
            w7ad_init(); assert(w7ad_file);
            struct W7ADFile *original = w7ad_file;
            /* O_EXCL refuses the same process/birth filename on re-init. */
            w7ad_init(); assert(w7ad_file == original);
            struct W7ADHeader *h = &w7ad_file->header;
            call(layer, 10000000, 100); assert(h->qualifying == 0);
            call(layer, 33333333, 100); assert(h->qualifying == 0);
            call(layer, W7AD_THRESHOLD_NS - 1, 100); assert(h->qualifying == 0);
            call(layer, W7AD_THRESHOLD_NS, 100); assert(h->qualifying == 1);
            call(layer, 717460500, 100); assert(h->qualifying == 2);
            call(layer, 999, 100); assert(h->nil_count == 1 && h->qualifying == 3);
            fail_wall = true; call(layer, 1, 1); fail_wall = false;
            assert(!(w7ad_file->records[3].flags & W7AD_WALL_VALID));
            assert(w7ad_file->records[3].wall_start == 0 && w7ad_file->records[3].wall_end == 0);
            fail_cpu = true; call(layer, 1, 1); fail_cpu = false;
            assert(!(w7ad_file->records[4].flags & W7AD_CPU_VALID));
            reverse_wall = true; call(layer, 1, 1); reverse_wall = false;
            reverse_cpu = true; call(layer, 1, 1); reverse_cpu = false;
            assert(h->clock_errors == 4);
            pthread_t threads[8];
            for (unsigned i = 0; i < 8; ++i) assert(!pthread_create(&threads[i], NULL, parallel, layer));
            for (unsigned i = 0; i < 8; ++i) assert(!pthread_join(threads[i], NULL));
            assert(h->started == 1610 && h->finished == 1610);
            assert(h->qualifying == 1607 && h->dropped == 583);
            bool calls[1611] = {0};
            for (unsigned i = 0; i < W7AD_CAPACITY; ++i) {
                struct W7ADRecord *r = &w7ad_file->records[i];
                assert(atomic_load_explicit(&r->committed, memory_order_acquire) == i + 1);
                assert(r->call <= 1610 && r->call > 3 && !calls[r->call]); calls[r->call] = true;
                assert(r->layer == (uintptr_t)layer && (r->flags & W7AD_TID_VALID));
            }
            uint64_t before = h->started;
            pid_t child = fork(); assert(child >= 0);
            if (!child) { _exit(w7ad_begin().call ? 42 : 0); }
            int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
            assert(h->started == before);
        }
        [layer release];
    }
    return 0;
}

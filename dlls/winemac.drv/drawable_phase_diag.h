/* Experimental native phase diagnostics. No Wine trace channel or filesystem opens.
 * Include once in each of the two instrumented translation units. Each TU owns
 * one distinct site and a process-lifetime, saturating 128-record budget.
 */
#ifndef W7_DRAWABLE_PHASE_DIAG_H
#define W7_DRAWABLE_PHASE_DIAG_H
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>

#define W7PD_THRESHOLD_NS UINT64_C(100000000)
#define W7PD_RECORD_LIMIT 128u
#ifndef W7PD_CLOCK_GETTIME
#define W7PD_CLOCK_GETTIME clock_gettime
#endif
#ifndef W7PD_WRITE
#define W7PD_WRITE write
#endif
#ifndef W7PD_THREAD_ID
#define W7PD_THREAD_ID pthread_threadid_np
#endif
static bool w7pd_enabled;
static _Atomic unsigned w7pd_emitted;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "diagnostic budget requires lock-free atomics");
static void __attribute__((constructor)) w7pd_initialize(void)
{
    const char *mode = getenv("VIBEWINE_DRAWABLE_PHASE_DIAG");
    w7pd_enabled = mode && !strcmp(mode, "1");
}
struct w7pd_sample {
    bool active, cpu_valid;
    uint64_t wall[5], cpu[5], tid;
};
static inline bool w7pd_clock(clockid_t clock, uint64_t *value)
{
    struct timespec ts;
    int saved_errno = errno;
    int result = W7PD_CLOCK_GETTIME(clock, &ts);
    errno = saved_errno;
    if (result || ts.tv_sec < 0 || ts.tv_nsec < 0 ||
        ts.tv_nsec >= 1000000000 || (uint64_t)ts.tv_sec >
        (UINT64_MAX - (uint64_t)ts.tv_nsec) / UINT64_C(1000000000)) return false;
    *value = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
    return true;
}
static inline void w7pd_mark(struct w7pd_sample *s, unsigned index)
{
    if (!s->active) return;
    if (!w7pd_clock(CLOCK_MONOTONIC_RAW, &s->wall[index])) { s->active = false; return; }
    if (s->cpu_valid && !w7pd_clock(CLOCK_THREAD_CPUTIME_ID, &s->cpu[index])) s->cpu_valid = false;
}
static inline struct w7pd_sample w7pd_begin(void)
{
    struct w7pd_sample s = {0};
    int saved_errno;
    if (!w7pd_enabled || atomic_load_explicit(&w7pd_emitted, memory_order_relaxed) >= W7PD_RECORD_LIMIT) return s;
    s.active = s.cpu_valid = true;
    saved_errno = errno;
    (void)W7PD_THREAD_ID(NULL, &s.tid);
    errno = saved_errno;
    w7pd_mark(&s, 0);
    return s;
}
/* Must be called after eventsLock is released. All object values are scalar
 * addresses for correlation, never dereferenced or retained by diagnostics.
 * marks are entry, phase1 boundary, phase2 boundary, phase3 boundary, exit.
 */
static inline void w7pd_report(struct w7pd_sample *s, const char *site,
    uintptr_t layer, uintptr_t queue, uintptr_t surface, uintptr_t drawable,
    uint64_t count_before, uint64_t count_after, int event_type)
{
    uint64_t phases[4], cpu = 0;
    unsigned i, slot;
    char line[1024];
    int length, saved_errno;
    if (!s->active) return;
    for (i = 0; i < 4; ++i) {
        if (s->wall[i + 1] < s->wall[i]) return;
        phases[i] = s->wall[i + 1] - s->wall[i];
        if (s->cpu[i + 1] < s->cpu[i]) s->cpu_valid = false;
    }
    if (s->wall[4] - s->wall[0] < W7PD_THRESHOLD_NS) return;
    slot = atomic_load_explicit(&w7pd_emitted, memory_order_relaxed);
    do {
        if (slot >= W7PD_RECORD_LIMIT) return;
    } while (!atomic_compare_exchange_weak_explicit(&w7pd_emitted, &slot, slot + 1,
                memory_order_relaxed, memory_order_relaxed));
    if (s->cpu_valid) cpu = s->cpu[4] - s->cpu[0];
    saved_errno = errno;
    length = snprintf(line, sizeof(line),
        "W7PD v=1 site=%s record=%u pid=%ld tid=%" PRIu64
        " layer=0x%" PRIxPTR " queue=0x%" PRIxPTR " surface=0x%" PRIxPTR " drawable=0x%" PRIxPTR
        " event_type=%d count_before=%" PRIu64 " count_after=%" PRIu64
        " start_ns=%" PRIu64 " end_ns=%" PRIu64 " wall_ns=%" PRIu64
        " cpu_valid=%u cpu_ns=%" PRIu64 " phase0_ns=%" PRIu64 " phase1_ns=%" PRIu64
        " phase2_ns=%" PRIu64 " phase3_ns=%" PRIu64 "\n",
        site, slot + 1, (long)getpid(), s->tid, layer, queue, surface, drawable,
        event_type, count_before, count_after, s->wall[0], s->wall[4], s->wall[4] - s->wall[0],
        s->cpu_valid ? 1u : 0u, cpu, phases[0], phases[1], phases[2], phases[3]);
    /* One bounded raw write; no stdio lock, allocation, open, retry or polling.
     * A blocked stderr can delay the caller after the measured interval. */
    if (length > 0 && (size_t)length < sizeof(line)) (void)W7PD_WRITE(STDERR_FILENO, line, (size_t)length);
    errno = saved_errno;
}
#endif

#include <assert.h>
#include <stdatomic.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
static _Atomic unsigned writes, clocks;
static _Atomic uint64_t seen[2];
unsigned second_site(void);
static _Thread_local int cpu_error, wall_error;
static int fake_clock(clockid_t c, struct timespec *ts) {
    atomic_fetch_add(&clocks, 1);
    if ((c == CLOCK_MONOTONIC_RAW && wall_error) || (c == CLOCK_THREAD_CPUTIME_ID && cpu_error)) return -1;
    ts->tv_sec = 1; ts->tv_nsec = c == CLOCK_THREAD_CPUTIME_ID ? 7 : 9;
    return 0;
}
static ssize_t fake_write(int fd, const void *data, size_t n) {
    assert(fd == STDERR_FILENO && n > 0 && n < 1024);
    assert(!strncmp(data, "W7PD v=1 ", 9));
    unsigned record = 0;
    assert(sscanf(strstr(data, " record="), " record=%u", &record) == 1);
    assert(record >= 1 && record <= 128);
    uint64_t bit = UINT64_C(1) << ((record - 1) % 64);
    assert(!(atomic_fetch_or(&seen[(record - 1) / 64], bit) & bit));
    atomic_fetch_add(&writes, 1); errno = EIO; return (ssize_t)n;
}
#define W7PD_CLOCK_GETTIME fake_clock
#define W7PD_WRITE fake_write
#include "drawable_phase_diag.h"
static struct w7pd_sample sample(uint64_t elapsed) {
    struct w7pd_sample s = w7pd_begin(); unsigned i;
    for (i = 0; i < 5; ++i) { s.wall[i] = 1000 + elapsed * i / 4; s.cpu[i] = 100 + i; }
    return s;
}
static void report(struct w7pd_sample *s) { w7pd_report(s,"test",1,2,3,4,5,6,7); }
static void *many(void *arg) {
    unsigned i; (void)arg;
    for (i = 0; i < 1000; ++i) { struct w7pd_sample s = sample(100000000); report(&s); }
    return NULL;
}
int main(int argc, char **argv) {
    struct w7pd_sample s; pthread_t threads[16]; unsigned i, before;
    assert(argc == 2);
    if (!strcmp(argv[1],"disabled")) {
        assert(!w7pd_enabled); s=w7pd_begin(); w7pd_mark(&s,1); report(&s);
        assert(!s.active && !atomic_load(&clocks) && !atomic_load(&writes)); return 0;
    }
    assert(w7pd_enabled);
    s=sample(99999999); report(&s); assert(!atomic_load(&writes));
    s=sample(100000000); errno=EDOM; report(&s); assert(errno==EDOM); assert(atomic_load(&writes)==1);
    s=sample(100000001); report(&s); assert(atomic_load(&writes)==2);
    /* Backwards/failed wall clock cannot fabricate a slow span. */
    s=sample(100000000); s.wall[2]=0; report(&s); assert(atomic_load(&writes)==2);
    wall_error=1; s=w7pd_begin(); assert(!s.active); report(&s); wall_error=0;
    assert(atomic_load(&writes)==2);
    /* CPU failure leaves valid wall diagnostics available. */
    cpu_error=1; s=sample(100000000); assert(s.active && !s.cpu_valid); report(&s); cpu_error=0;
    assert(atomic_load(&writes)==3);
    s=sample(100000000); s.cpu[2]=0; report(&s); assert(!s.cpu_valid && atomic_load(&writes)==4);
    for(i=0;i<16;++i) assert(!pthread_create(&threads[i],NULL,many,NULL));
    for(i=0;i<16;++i) assert(!pthread_join(threads[i],NULL));
    assert(atomic_load(&writes)==128 && atomic_load(&w7pd_emitted)==128);
    assert(atomic_load(&seen[0])==UINT64_MAX && atomic_load(&seen[1])==UINT64_MAX);
    assert(second_site()==128);
    before=atomic_load(&clocks); s=w7pd_begin(); w7pd_mark(&s,1); report(&s);
    assert(!s.active && atomic_load(&clocks)==before && atomic_load(&writes)==128);
    return 0;
}

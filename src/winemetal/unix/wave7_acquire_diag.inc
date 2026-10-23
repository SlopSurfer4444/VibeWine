/* Opt-in native nextDrawable diagnostics. No PE/feedback ABI changes.
 * Reuses the trace collector's no-follow directory walk and reserve/commit
 * publication pattern. Records are never overwritten; all loss is counted. */
#include <fcntl.h>
#include <libproc.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define W7AD_CAPACITY 1024u
#define W7AD_THRESHOLD_NS UINT64_C(100000000)
#define W7AD_NIL 1u
#define W7AD_WALL_VALID 2u
#define W7AD_CPU_VALID 4u
#define W7AD_TID_VALID 8u

struct W7ADRecord {
    _Atomic uint64_t committed;
    uint64_t call, thread, layer, drawable;
    uint64_t wall_start, wall_end, cpu_start, cpu_end;
    uint64_t flags;
};
struct W7ADHeader {
    char magic[8];
    uint64_t version, header_bytes, record_bytes, capacity, threshold_ns;
    uint64_t pid, birth_seconds, birth_microseconds;
    uint64_t wall_clock, cpu_clock; /* 1 = MONOTONIC_RAW ns; 2 = THREAD_CPUTIME ns */
    _Atomic uint64_t started, finished, qualifying, dropped, nil_count, clock_errors;
    _Atomic uint64_t skipped_occluded;
    uint64_t reserved[14];
};
struct W7ADFile {
    struct W7ADHeader header;
    struct W7ADRecord records[W7AD_CAPACITY];
};
_Static_assert(sizeof(struct W7ADHeader) == 256, "diagnostic header ABI");
_Static_assert(sizeof(struct W7ADRecord) == 80, "diagnostic record ABI");
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2, "lock-free counters required");
static struct W7ADFile *w7ad_file;
static pid_t w7ad_pid;

/* All setup occurs once at load, including getenv, path work, allocation and
 * mapping. Failure leaves diagnostics disabled and never alters nextDrawable. */
static int w7ad_directory(const char *path)
{
    if (!path || path[0] != '/' || strlen(path) >= PATH_MAX) return -1;
    char copy[PATH_MAX];
    strcpy(copy, path);
    int dir = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return -1;
    char *part, *save = NULL;
    for (part = strtok_r(copy + 1, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
        if (!strcmp(part, ".") || !strcmp(part, "..")) { close(dir); return -1; }
        int next = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(dir);
        if (next < 0) return -1;
        dir = next;
    }
    struct stat st;
    if (fstat(dir, &st) || st.st_uid != geteuid() || (st.st_mode & 0777) != 0700) {
        close(dir); return -1;
    }
    return dir;
}

static void __attribute__((constructor)) w7ad_init(void)
{
    const char *path = getenv("DXMT_ACQUIRE_DIAGNOSTICS_DIR");
    if (!path || !*path) return;
    int dir = w7ad_directory(path);
    if (dir < 0) return;
    struct proc_bsdinfo info;
    memset(&info, 0, sizeof(info));
    pid_t pid = getpid();
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) {
        close(dir); return;
    }
    char name[128];
    snprintf(name, sizeof(name), "acquire-%u-%llu-%llu.bin", (unsigned)pid,
             (unsigned long long)info.pbi_start_tvsec, (unsigned long long)info.pbi_start_tvusec);
    int fd = openat(dir, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { close(dir); return; }
    struct stat st;
    bool okay = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
                st.st_nlink == 1 && (st.st_mode & 0777) == 0600 &&
                ftruncate(fd, sizeof(struct W7ADFile)) == 0;
    struct W7ADFile *file = okay ? mmap(NULL, sizeof(*file), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    close(fd);
    /* Preserve a failed private file rather than unlink a potentially replaced
     * directory entry. Missing/invalid magic makes incomplete setup explicit. */
    if (file == MAP_FAILED) { close(dir); return; }
    close(dir);
    memset(file, 0, sizeof(*file)); /* fault in backing pages outside frame path */
    file->header.version = 2;
    file->header.header_bytes = sizeof(file->header);
    file->header.record_bytes = sizeof(file->records[0]);
    file->header.capacity = W7AD_CAPACITY;
    file->header.threshold_ns = W7AD_THRESHOLD_NS;
    file->header.pid = (uint64_t)pid;
    file->header.birth_seconds = info.pbi_start_tvsec;
    file->header.birth_microseconds = info.pbi_start_tvusec;
    file->header.wall_clock = 1;
    file->header.cpu_clock = 2;
    memcpy(file->header.magic, "W7ACQ02", 8);
    w7ad_pid = pid;
    w7ad_file = file;
}

struct W7ADSample { uint64_t call, wall, cpu; bool wall_valid, cpu_valid; };

static void w7ad_skip_occluded(void)
{
    if (w7ad_file && getpid() == w7ad_pid)
        atomic_fetch_add_explicit(&w7ad_file->header.skipped_occluded, 1, memory_order_relaxed);
}

static bool w7ad_now(clockid_t clock, uint64_t *value)
{
    struct timespec ts;
    if (clock_gettime(clock, &ts) || ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000 ||
        (uint64_t)ts.tv_sec > (UINT64_MAX - (uint64_t)ts.tv_nsec) / UINT64_C(1000000000)) {
        *value = 0; return false;
    }
    *value = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
    return true;
}

static struct W7ADSample w7ad_begin(void)
{
    struct W7ADSample sample = {0};
    /* A forked child must never write into its parent's identity/mapping. */
    if (!w7ad_file || getpid() != w7ad_pid) return sample;
    sample.call = atomic_fetch_add_explicit(&w7ad_file->header.started, 1, memory_order_relaxed) + 1;
    sample.wall_valid = w7ad_now(CLOCK_MONOTONIC_RAW, &sample.wall);
    sample.cpu_valid = w7ad_now(CLOCK_THREAD_CPUTIME_ID, &sample.cpu);
    return sample;
}

static void w7ad_end(struct W7ADSample sample, uintptr_t layer, uintptr_t drawable)
{
    if (!sample.call) return;
    uint64_t cpu = 0, wall = 0;
    bool cpu_valid = w7ad_now(CLOCK_THREAD_CPUTIME_ID, &cpu) && sample.cpu_valid && cpu >= sample.cpu;
    bool wall_valid = w7ad_now(CLOCK_MONOTONIC_RAW, &wall) && sample.wall_valid && wall >= sample.wall;
    struct W7ADHeader *h = &w7ad_file->header;
    if (!drawable) atomic_fetch_add_explicit(&h->nil_count, 1, memory_order_relaxed);
    if (!cpu_valid || !wall_valid) atomic_fetch_add_explicit(&h->clock_errors, 1, memory_order_relaxed);
    if (!drawable || !wall_valid || !cpu_valid || wall - sample.wall >= W7AD_THRESHOLD_NS) {
        uint64_t slot = atomic_fetch_add_explicit(&h->qualifying, 1, memory_order_relaxed);
        if (slot >= W7AD_CAPACITY) atomic_fetch_add_explicit(&h->dropped, 1, memory_order_relaxed);
        else {
            struct W7ADRecord *r = &w7ad_file->records[slot];
            uint64_t tid = 0;
            bool tid_valid = pthread_threadid_np(NULL, &tid) == 0 && tid != 0;
            r->call = sample.call; r->thread = tid_valid ? tid : 0; r->layer = layer; r->drawable = drawable;
            r->wall_start = wall_valid ? sample.wall : 0; r->wall_end = wall_valid ? wall : 0;
            r->cpu_start = cpu_valid ? sample.cpu : 0; r->cpu_end = cpu_valid ? cpu : 0;
            r->flags = (!drawable ? W7AD_NIL : 0) | (wall_valid ? W7AD_WALL_VALID : 0) |
                       (cpu_valid ? W7AD_CPU_VALID : 0) | (tid_valid ? W7AD_TID_VALID : 0);
            atomic_store_explicit(&r->committed, slot + 1, memory_order_release);
        }
    }
    atomic_fetch_add_explicit(&h->finished, 1, memory_order_release);
}

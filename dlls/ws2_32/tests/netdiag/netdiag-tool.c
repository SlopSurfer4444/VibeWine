/* Private metadata capture controller. LGPL-2.1-or-later. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "wine/netdiag.h"

static void fail(const char *message) { fprintf(stderr, "netdiag-tool: %s\n", message); exit(1); }
static int token_valid(const char *s)
{
    unsigned int i;
    if (strlen(s) != 32) return 0;
    for (i = 0; i < 32; ++i) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}
static int executable_valid(const char *s)
{
    size_t i, n = strlen(s);
    if (!n || n >= 512) return 0;
    for (i = 0; i < n; ++i) if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) return 0;
    return 1;
}
static uint64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) fail("monotonic clock unavailable");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}
static void init_file(const char *directory, const char *executable, const char *token)
{
    struct wine_netdiag_header *h;
    int dir, fd;
    if (directory[0] != '/' || !executable_valid(executable) || !token_valid(token)) fail("invalid absolute directory, ASCII executable or lowercase hex token");
    /* mkdir is the exclusivity boundary: never adopt or truncate an existing path. */
    if (mkdir(directory, 0700)) fail("init requires a fresh directory");
    dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir < 0 || fchmod(dir, 0700)) fail("private directory unavailable");
    fd = openat(dir, "netdiag.bin", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0 || fchmod(fd, 0600) || ftruncate(fd, WINE_NETDIAG_FILE_SIZE)) fail("cannot create private capture file");
    h = mmap(NULL, WINE_NETDIAG_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (h == MAP_FAILED) fail("mmap failed");
    /* Touch every page, including record storage, before the target can claim it. */
    memset(h, 0, WINE_NETDIAG_FILE_SIZE);
    h->version = 1; h->record_size = 128; h->capacity = WINE_NETDIAG_CAPACITY;
    memcpy(h->token, token, 32); strcpy(h->executable, executable);
    __atomic_store_n(&h->magic, WINE_NETDIAG_MAGIC, __ATOMIC_RELEASE);
    if (msync(h, WINE_NETDIAG_FILE_SIZE, MS_SYNC)) fail("cannot persist initialized file");
    munmap(h, WINE_NETDIAG_FILE_SIZE); close(fd); close(dir);
}
static struct wine_netdiag_header *open_file(const char *path, int *file)
{
    char parent[4096], *slash;
    struct stat st;
    struct wine_netdiag_header *h;
    int dir;
    if (path[0] != '/' || strlen(path) >= sizeof(parent)) fail("absolute capture path required");
    strcpy(parent, path); slash = strrchr(parent, '/');
    if (!slash || strcmp(slash + 1, "netdiag.bin")) fail("capture basename must be netdiag.bin");
    *slash = 0;
    dir = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir < 0 || fstat(dir, &st) || st.st_uid != getuid() || (st.st_mode & 0777) != 0700) fail("directory must be private and owned");
    *file = openat(dir, "netdiag.bin", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    close(dir);
    if (*file < 0 || fstat(*file, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_nlink != 1 ||
        (st.st_mode & 0777) != 0600 || st.st_size != WINE_NETDIAG_FILE_SIZE) fail("capture must be private, regular, singly linked and full size");
    if (flock(*file, LOCK_EX | LOCK_NB)) fail("capture busy or lock unavailable");
    h = mmap(NULL, WINE_NETDIAG_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, *file, 0);
    if (h == MAP_FAILED) fail("mmap failed");
    if (__atomic_load_n(&h->magic, __ATOMIC_ACQUIRE) != WINE_NETDIAG_MAGIC || h->version != 1 || h->record_size != 128 ||
        h->capacity != WINE_NETDIAG_CAPACITY || h->state > 3 || h->reserved > WINE_NETDIAG_CAPACITY ||
        !memchr(h->executable, 0, sizeof(h->executable)) || !executable_valid(h->executable)) fail("invalid capture header");
    return h;
}
static void verify_fresh(struct wine_netdiag_header *h, const char *executable, const char *token)
{
    struct wine_netdiag_header *expected;
    if (!executable_valid(executable) || !token_valid(token)) fail("invalid capture binding");
    expected = calloc(1, WINE_NETDIAG_FILE_SIZE);
    if (!expected) fail("verification allocation failed");
    expected->magic = WINE_NETDIAG_MAGIC;
    expected->version = 1; expected->record_size = 128; expected->capacity = WINE_NETDIAG_CAPACITY;
    memcpy(expected->token, token, 32); strcpy(expected->executable, executable);
    /* Compare the complete initialized image, including every control field,
     * socket slot, executable tail, alignment byte, header tail and record. */
    if (memcmp(h, expected, WINE_NETDIAG_FILE_SIZE)) fail("capture is not fresh or binding differs");
    free(expected);
    printf("{\"fresh\":true,\"version\":1,\"record_size\":128,\"capacity\":%u,\"state\":0,\"reserved\":0,\"dropped\":0,\"host_pid\":0,\"windows_pid\":0}\n", WINE_NETDIAG_CAPACITY);
}
static void dump_file(struct wine_netdiag_header *h)
{
    struct wine_netdiag_event *records = (void *)((char *)h + WINE_NETDIAG_MAP_HEADER);
    struct wine_netdiag_event *snapshot;
    uint64_t i, count = 0, reserved = __atomic_load_n(&h->reserved, __ATOMIC_ACQUIRE);
    uint32_t state = __atomic_load_n(&h->state, __ATOMIC_ACQUIRE);
    /* Count only fully published slots; never parse a concurrently written slot. */
    snapshot = calloc(reserved ? reserved : 1, sizeof(*snapshot));
    if (!snapshot) fail("snapshot allocation failed");
    for (i = 0; i < reserved; ++i)
        if (__atomic_load_n(&records[i].sequence, __ATOMIC_ACQUIRE) == i + 1)
        { memcpy(snapshot + count, records + i, sizeof(*snapshot)); ++count; }
    printf("{\"type\":\"header\",\"version\":1,\"record_size\":128,\"capacity\":%u,\"state\":%u,\"enabled\":%s,\"reserved\":%" PRIu64 ",\"count\":%" PRIu64 ",\"dropped\":%" PRIu64 ",\"host_pid\":%u,\"windows_pid\":%u,\"start_ns\":%" PRIu64 ",\"deadline_ns\":%" PRIu64 "}\n",
           h->capacity, state, state == 1 && now_ns() < h->deadline_ns ? "true" : "false", reserved, count,
           __atomic_load_n(&h->dropped, __ATOMIC_RELAXED), h->host_pid, h->windows_pid, h->start_ns, h->deadline_ns);
    for (i = 0; i < count; ++i)
    {
        struct wine_netdiag_event e;
        unsigned int j;
        memcpy(&e, snapshot + i, sizeof(e));
        printf("{\"type\":\"event\",\"sequence\":%" PRIu64 ",\"time_ns\":%" PRIu64 ",\"operation_id\":%" PRIu64 ",\"lifetime\":%" PRIu64 ",\"requested\":%" PRIu64 ",\"returned\":%" PRId64 ",\"elapsed_ns\":%" PRIu64 ",\"api\":%u,\"phase\":%u,\"status\":%u,\"error\":%u,\"native_fd\":%d,\"native_errno\":%d,\"family\":%u,\"port\":%u,\"scope\":%u,\"flags\":%u,\"host_pid\":%u,\"thread_id\":%u,\"address_hex\":\"",
               e.sequence, e.time_ns, e.operation_id, e.lifetime, e.requested, e.returned, e.elapsed_ns,
               e.api, e.phase, e.status, e.error, e.native_fd, e.native_errno, e.family, e.port, e.scope, e.flags, e.host_pid, e.thread_id);
        for (j = 0; j < 16; ++j) printf("%02x", e.address[j]);
        puts("\"}");
    }
    free(snapshot);
}
int main(int argc, char **argv)
{
    struct wine_netdiag_header *h;
    int fd;
    if (argc == 5 && !strcmp(argv[1], "init")) { init_file(argv[2], argv[3], argv[4]); return 0; }
    if (argc == 5 && !strcmp(argv[1], "verify-fresh"))
    {
        h = open_file(argv[2], &fd);
        verify_fresh(h, argv[3], argv[4]);
        munmap(h, WINE_NETDIAG_FILE_SIZE); close(fd); return 0;
    }
    if (argc < 3 || argc > 4) fail("usage: init DIR EXE TOKEN | verify-fresh PATH EXE TOKEN | arm PATH MILLIS | stop PATH | dump PATH");
    if (strcmp(argv[1], "arm") && strcmp(argv[1], "stop") && strcmp(argv[1], "dump")) fail("unknown command");
    if ((!strcmp(argv[1], "arm") && argc != 4) || (strcmp(argv[1], "arm") && argc != 3)) fail("incorrect argument count");
    h = open_file(argv[2], &fd);
    if (!strcmp(argv[1], "arm"))
    {
        char *end;
        unsigned long millis;
        errno = 0; millis = strtoul(argv[3], &end, 10);
        if (errno || !*argv[3] || *end || argv[3][0] < '0' || argv[3][0] > '9' || !millis || millis > 45000) fail("duration must be 1..45000 milliseconds");
        if (__atomic_load_n(&h->state, __ATOMIC_ACQUIRE) || !__atomic_load_n(&h->host_pid, __ATOMIC_ACQUIRE) || h->start_ns || h->deadline_ns || h->reserved || h->next_operation) fail("capture must be claimed and unused");
        h->start_ns = now_ns(); h->deadline_ns = h->start_ns + millis * UINT64_C(1000000);
        __atomic_store_n(&h->state, 1, __ATOMIC_RELEASE);
    }
    else if (!strcmp(argv[1], "stop"))
    {
        uint32_t state = __atomic_load_n(&h->state, __ATOMIC_ACQUIRE);
        while (state < 2 && !__atomic_compare_exchange_n(&h->state, &state, 2, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
    }
    else dump_file(h);
    munmap(h, WINE_NETDIAG_FILE_SIZE); close(fd); return 0;
}

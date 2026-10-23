/* Bounded process-local metadata mapping, following aoe_diag's opt-in mmap
 * pattern. No payload API, reverse DNS, per-record allocation or file writes.
 * LGPL-2.1-or-later. Unix translation units only. */
#ifndef __WINE_NETDIAG_UNIX_H
#define __WINE_NETDIAG_UNIX_H
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "wine/netdiag.h"

_Static_assert(__atomic_always_lock_free(8, 0), "netdiag requires lock-free 64-bit atomics");
static struct wine_netdiag_header *wine_netdiag_mapping;
static unsigned int wine_netdiag_initializing;

static uint64_t wine_netdiag_now(void)
{
    struct timespec ts;
    int saved = errno;
    uint64_t value = 0;
    if (!clock_gettime(CLOCK_MONOTONIC, &ts))
        value = (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
    errno = saved;
    return value;
}

#ifndef WINE_NETDIAG_IMAGE_MATCH
static int wine_netdiag_image_match(const char *expected)
{
    const UNICODE_STRING *image = &NtCurrentTeb()->Peb->ProcessParameters->ImagePathName;
    unsigned int i, len = strlen(expected);
    if (image->Length != len * sizeof(WCHAR)) return 0;
    for (i = 0; i < len; ++i)
        if ((unsigned char)expected[i] > 127 || image->Buffer[i] != (unsigned char)expected[i]) return 0;
    return 1;
}
#define WINE_NETDIAG_IMAGE_MATCH(s) wine_netdiag_image_match(s)
#define WINE_NETDIAG_WINDOWS_PID ((uint32_t)(uintptr_t)NtCurrentTeb()->ClientId.UniqueProcess)
#define WINE_NETDIAG_THREAD_ID ((uint32_t)(uintptr_t)NtCurrentTeb()->ClientId.UniqueThread)
#endif

static void wine_netdiag_init(void)
{
    const char *path = getenv("VIBEWINE_NETDIAG_FILE");
    const char *token = getenv("VIBEWINE_NETDIAG_TOKEN");
    struct wine_netdiag_header *map = MAP_FAILED;
    struct stat st;
    char directory_name[4096], *slash;
    uint32_t expected_pid = 0;
    unsigned int i;
    int directory = -1, fd = -1, saved = errno;

    if (!path || path[0] != '/' || strlen(path) >= sizeof(directory_name) ||
        !token || strlen(token) != 32) goto done;
    for (i = 0; i < 32; ++i)
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f'))) goto done;
    strcpy(directory_name, path);
    slash = strrchr(directory_name, '/');
    if (!slash || strcmp(slash + 1, "netdiag.bin")) goto done;
    *slash = 0;
    directory = open(directory_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0 || fstat(directory, &st) || st.st_uid != getuid() ||
        (st.st_mode & 0777) != 0700) goto done;
    fd = openat(directory, "netdiag.bin", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_nlink != 1 || (st.st_mode & 0777) != 0600 || st.st_size != WINE_NETDIAG_FILE_SIZE) goto done;
    map = mmap(NULL, WINE_NETDIAG_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) goto done;
    if (__atomic_load_n(&map->magic, __ATOMIC_ACQUIRE) != WINE_NETDIAG_MAGIC ||
        map->version != 1 || map->record_size != 128 || map->capacity != WINE_NETDIAG_CAPACITY ||
        memcmp(map->token, token, 32) || !memchr(map->executable, 0, sizeof(map->executable)) ||
        !WINE_NETDIAG_IMAGE_MATCH(map->executable)) goto done;
    if (!__atomic_compare_exchange_n(&map->host_pid, &expected_pid, (uint32_t)getpid(), 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) && expected_pid != (uint32_t)getpid()) goto done;
    __atomic_store_n(&map->windows_pid, WINE_NETDIAG_WINDOWS_PID, __ATOMIC_RELEASE);
    __atomic_store_n(&wine_netdiag_mapping, map, __ATOMIC_RELEASE);
    map = MAP_FAILED;
done:
    if (map != MAP_FAILED) munmap(map, WINE_NETDIAG_FILE_SIZE);
    if (fd >= 0) close(fd);
    if (directory >= 0) close(directory);
    errno = saved;
}

static struct wine_netdiag_header *wine_netdiag_active(void)
{
    struct wine_netdiag_header *map;
    unsigned int expected = 0;
    uint64_t now, start, end;
    uint32_t state = 1;
    int saved = errno;
    if (__atomic_compare_exchange_n(&wine_netdiag_initializing, &expected, 1, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        wine_netdiag_init();
        __atomic_store_n(&wine_netdiag_initializing, 2, __ATOMIC_RELEASE);
    }
    map = __atomic_load_n(&wine_netdiag_mapping, __ATOMIC_ACQUIRE);
    if (!map || map->host_pid != (uint32_t)getpid() ||
        __atomic_load_n(&map->state, __ATOMIC_ACQUIRE) != 1) { errno = saved; return NULL; }
    start = __atomic_load_n(&map->start_ns, __ATOMIC_ACQUIRE);
    end = __atomic_load_n(&map->deadline_ns, __ATOMIC_ACQUIRE);
    now = wine_netdiag_now();
    if (!now || !start || end <= start || end - start > WINE_NETDIAG_MAX_NS || now < start || now >= end)
    {
        __atomic_compare_exchange_n(&map->state, &state, 3, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        errno = saved;
        return NULL;
    }
    errno = saved;
    return map;
}

static void wine_netdiag_lifetime(struct wine_netdiag_header *map, struct wine_netdiag_event *event,
                                  int opening, int closing)
{
    unsigned int i, empty = 512;
    if (!event->handle || event->handle == UINT64_MAX) { event->flags |= ND_LIFETIME_UNKNOWN; return; }
    if (__atomic_exchange_n(&map->lifetime_lock, 1, __ATOMIC_ACQUIRE))
    {
        event->flags |= ND_LIFETIME_UNKNOWN;
        __atomic_fetch_add(&map->dropped, 1, __ATOMIC_RELAXED);
        return;
    }
    for (i = 0; i < 512; ++i)
    {
        struct wine_netdiag_lifetime *slot = &map->sockets[i];
        if (!slot->handle && empty == 512) empty = i;
        if (slot->handle != event->handle) continue;
        if (closing)
        {
            if (event->lifetime && event->lifetime == slot->lifetime) slot->handle = 0;
        }
        else if (opening)
        {
            slot->lifetime = __atomic_add_fetch(&map->next_lifetime, 1, __ATOMIC_RELAXED);
            event->lifetime = slot->lifetime;
            event->flags &= ~(ND_LIFETIME_OBSERVED | ND_LIFETIME_UNKNOWN);
        }
        else event->lifetime = slot->lifetime;
        goto done;
    }
    if (!closing && empty != 512)
    {
        map->sockets[empty].handle = event->handle;
        event->lifetime = map->sockets[empty].lifetime = __atomic_add_fetch(&map->next_lifetime, 1, __ATOMIC_RELAXED);
        if (!opening) event->flags |= ND_LIFETIME_OBSERVED;
        else event->flags &= ~(ND_LIFETIME_OBSERVED | ND_LIFETIME_UNKNOWN);
    }
    else event->flags |= ND_LIFETIME_UNKNOWN;
done:
    __atomic_store_n(&map->lifetime_lock, 0, __ATOMIC_RELEASE);
}

static void wine_netdiag_emit(struct wine_netdiag_event *event)
{
    struct wine_netdiag_header *map = wine_netdiag_active();
    struct wine_netdiag_event value, *record;
    uint64_t index, now;
    unsigned int tries;
    int saved = errno;
    if (!map || !event->operation_id) goto done;
    if (event->phase == ND_OPEN) wine_netdiag_lifetime(map, event, 1, 0);
    if (event->phase == ND_CLOSED) wine_netdiag_lifetime(map, event, 0, 1);
    now = wine_netdiag_now();
    value = *event;
    value.time_ns = now;
    if (event->phase != ND_ATTEMPT) value.elapsed_ns = now - event->time_ns;
    value.host_pid = getpid();
    value.thread_id = WINE_NETDIAG_THREAD_ID;
    index = __atomic_load_n(&map->reserved, __ATOMIC_RELAXED);
    for (tries = 0; tries < 8; ++tries)
    {
        if (index >= WINE_NETDIAG_CAPACITY) break;
        if (__atomic_compare_exchange_n(&map->reserved, &index, index + 1, 0,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        {
            record = (struct wine_netdiag_event *)((char *)map + WINE_NETDIAG_MAP_HEADER) + index;
            memcpy((char *)record + 8, (char *)&value + 8, 120);
            __atomic_store_n(&record->sequence, index + 1, __ATOMIC_RELEASE);
            goto done;
        }
    }
    __atomic_fetch_add(&map->dropped, 1, __ATOMIC_RELAXED);
done:
    errno = saved;
}

static void wine_netdiag_begin(struct wine_netdiag_event *event, uint32_t api, uint64_t handle)
{
    struct wine_netdiag_header *map;
    int saved = errno;
    memset(event, 0, sizeof(*event));
    event->api = api;
    event->phase = ND_BEGIN;
    event->handle = handle;
    event->native_fd = -1;
    event->flags = ND_ENDPOINT_UNKNOWN;
    if ((map = wine_netdiag_active()))
    {
        event->time_ns = wine_netdiag_now();
        event->operation_id = __atomic_add_fetch(&map->next_operation, 1, __ATOMIC_RELAXED);
        wine_netdiag_lifetime(map, event, 0, 0);
        wine_netdiag_emit(event);
    }
    errno = saved;
}
#endif

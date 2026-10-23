/* Private DS3 startup visibility diagnostic. No environment value establishes
 * process identity. These helpers are shared verbatim with offline tests. */
#ifndef W7_VISIBILITY_GUARD_H
#define W7_VISIBILITY_GUARD_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#define W7VIS_DS3_IMAGE "C:\\Program Files (x86)\\Steam\\steamapps\\common\\DARK SOULS III\\Game\\DarkSoulsIII.exe"
#define W7VIS_RESULT_NAME "startup-visibility.json"

static inline bool w7vis_exact_ds3_image(const uint16_t *image, size_t bytes)
{
    static const char expected[] = W7VIS_DS3_IMAGE;
    size_t i;
    if (!image || bytes != (sizeof(expected) - 1) * sizeof(uint16_t)) return false;
    for (i = 0; i < sizeof(expected) - 1; i++)
    {
        uint16_t a = image[i], b = (unsigned char)expected[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}

/* Every directory is opened without following links. Ancestors must be owned
 * by this uid or root and not group/world writable. The final dir is 0700. */
static inline int w7vis_open_private_directory(const char *path)
{
    char copy[PATH_MAX], *component, *next;
    struct stat st;
    int fd, child;
    size_t length;
    if (!path || path[0] != '/' || !(length = strnlen(path, sizeof(copy))) || length >= sizeof(copy)) return -1;
    memcpy(copy, path, length + 1);
    fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    component = copy + 1;
    if (!*component) { close(fd); return -1; }
    for (;;)
    {
        next = strchr(component, '/');
        if (next) *next = 0;
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) { close(fd); return -1; }
        child = openat(fd, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(fd);
        if (child < 0) return -1;
        fd = child;
        if (fstat(fd, &st) || !S_ISDIR(st.st_mode) || (st.st_uid != geteuid() && st.st_uid != 0) || (st.st_mode & 0022))
        { close(fd); return -1; }
        if (!next) break;
        component = next + 1;
    }
    if (st.st_uid != geteuid() || (st.st_mode & 07777) != 0700) { close(fd); return -1; }
    return fd;
}

static inline bool w7vis_directory_still_named(int held, const char *path)
{
    struct stat before, after;
    int fresh = w7vis_open_private_directory(path);
    bool same = fresh >= 0 && !fstat(held, &before) && !fstat(fresh, &after) &&
        before.st_dev == after.st_dev && before.st_ino == after.st_ino;
    if (fresh >= 0) close(fresh);
    return same;
}

/* Single bounded metadata file. On partial/error leave evidence untouched;
 * readers reject incomplete JSON. No truncate, replace or alternate basename. */
static inline int w7vis_publish(int directory, const char *path, const void *bytes, size_t length)
{
    struct stat st;
    const char *cursor = bytes;
    int fd, result = -1, interruptions = 0;
    size_t remaining = length;
    if (!bytes || !length || length > 32768 || !w7vis_directory_still_named(directory, path)) return -1;
    fd = openat(directory, W7VIS_RESULT_NAME, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 07777) != 0600) goto done;
    while (remaining)
    {
        ssize_t n = write(fd, cursor, remaining);
        if (n < 0 && errno == EINTR && interruptions++ < 8) continue;
        if (n <= 0) goto done;
        cursor += n;
        remaining -= n;
    }
    result = 0;
 done:
    close(fd);
    return result;
}
#endif

/* Opt-in native application dispatch. LGPL-2.1-or-later, like loader.c.
 * loader_exec runs after Wine's fork. Do not call Apple frameworks here.
 * Bundle/plist/signature admission belongs to the offline packager. This
 * plain-C manifest is routing metadata, never an AX/security permission. */
#include <stdint.h>

#define NATIVE_ROUTE_MAX_BYTES 65536
#define NATIVE_ROUTE_MAX_ROWS 64
#define NATIVE_ROUTE_HEADER "WINE-NATIVE-APPLICATIONS\t1\n"

static uint32_t native_route_cpu(unsigned short machine)
{
    if (machine == 0x8664) return 0x01000007; /* CPU_TYPE_X86_64 */
    if (machine == 0xaa64) return 0x0100000c; /* CPU_TYPE_ARM64 */
    if (machine != 0x014c) return 0;
#if defined(IS_WOW64_BUILD) && defined(__x86_64__)
    return 0x01000007;
#elif defined(IS_WOW64_BUILD) && defined(__aarch64__)
    return 0x0100000c;
#else
    return 7; /* CPU_TYPE_I386 */
#endif
}

/* A thin Mach-O executable prevents execv selecting another universal slice.
 * No runtime code-signature policy is inferred from this structural check. */
static int native_route_executable(const char *path, unsigned short machine)
{
    static const char suffix[] = ".app/Contents/MacOS/wine";
    char canonical[PATH_MAX];
    uint32_t header[8], expected = native_route_cpu(machine);
    struct stat st;
    ssize_t got;
    size_t length = strlen(path);
    int fd, valid = 0;
    if (!expected || path[0] != '/' || length <= sizeof(suffix) - 1 ||
        strcmp(path + length - (sizeof(suffix) - 1), suffix) ||
        !realpath(path, canonical) || strcmp(path, canonical) || access(path, X_OK)) return 0;
    if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)) < 0) return 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return 0; }
    do got = read(fd, header, sizeof(header)); while (got < 0 && errno == EINTR);
    if (got == sizeof(header) &&
        ((header[0] == 0xfeedfacf && (expected & 0x01000000)) ||
         (header[0] == 0xfeedface && !(expected & 0x01000000))) &&
        header[1] == expected && header[3] == 2) valid = 1; /* MH_EXECUTE */
    close(fd);
    return valid;
}

/* Format: version header, then machine(hex)<TAB>actual-image<TAB>bundle-loader<LF>.
 * No quoting/escaping: tabs/newlines/NUL in a path are unsupported and rejected.
 * 1 explicit match, 0 ordinary Wine dispatch, -1 invalid opted-in manifest.
 * Prefix uses initialized config_dir, not a new wrapper environment variable. */
static int native_app_route(const char *prefix, const char *image_path,
                            unsigned short machine, char **loader)
{
    char path[PATH_MAX], *bytes = NULL, *line, *next, *image, *executable, *end;
    struct stat st;
    size_t done = 0, length;
    ssize_t n;
    unsigned long row_machine;
    unsigned rows = 0, matches = 0;
    int fd = -1, result = -1, path_length;

    *loader = NULL;
    if (!prefix || !image_path) return 0;
    path_length = snprintf(path, sizeof(path), "%s/native-applications.routes", prefix);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) return -1;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        (st.st_mode & 0022) || st.st_size <= 0 || st.st_size > NATIVE_ROUTE_MAX_BYTES) goto out;
    length = st.st_size;
    if (!(bytes = malloc(length + 1))) goto out;
    while (done < length)
    {
        n = read(fd, bytes + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto out;
        done += n;
    }
    if (memchr(bytes, 0, length) || bytes[length - 1] != '\n' ||
        length < sizeof(NATIVE_ROUTE_HEADER) - 1 ||
        memcmp(bytes, NATIVE_ROUTE_HEADER, sizeof(NATIVE_ROUTE_HEADER) - 1)) goto out;
    bytes[length] = 0;
    line = bytes + sizeof(NATIVE_ROUTE_HEADER) - 1;
    while (*line)
    {
        if (++rows > NATIVE_ROUTE_MAX_ROWS || !(next = strchr(line, '\n'))) goto out;
        *next++ = 0;
        if (!(image = strchr(line, '\t'))) goto out;
        *image++ = 0;
        if (!(executable = strchr(image, '\t'))) goto out;
        *executable++ = 0;
        if (!*line || !*image || !*executable || strchr(executable, '\t') ||
            strchr(image, '\r') || strchr(executable, '\r') || strlen(image) >= PATH_MAX ||
            strlen(executable) >= PATH_MAX) goto out;
        errno = 0;
        row_machine = strtoul(line, &end, 16);
        if (errno || *end || strspn(line, "0123456789abcdefABCDEF") != strlen(line) ||
            row_machine == 0 || row_machine > 65535) goto out;
        if (row_machine == machine && !strcmp(image, image_path))
        {
            if (++matches != 1 || !native_route_executable(executable, machine)) goto out;
            *loader = strdup(executable);
            if (!*loader) goto out;
        }
        line = next;
    }
    result = matches;
out:
    if (result < 0) { free(*loader); *loader = NULL; }
    free(bytes);
    if (fd >= 0) close(fd);
    return result;
}

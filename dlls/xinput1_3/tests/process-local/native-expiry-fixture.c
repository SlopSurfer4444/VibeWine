/* The complete shipping test_backend.h is included unchanged. Only Win32
 * dependencies are mocked; clock faults occur at successful external I/O seams.
 * No Wine, files, sockets, input devices, or real sleeping are used.
 * LGPL-2.1-or-later. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef int16_t SHORT;
typedef uint32_t DWORD;
typedef unsigned long long ULONGLONG;
#ifdef TEST_HAS_DIAGNOSTICS
typedef long long LONG64;
typedef int32_t LONG;
#endif
typedef int BOOL;
typedef wchar_t WCHAR;
typedef void *HANDLE;
typedef struct { int held; } CRITICAL_SECTION;
typedef struct { int done; } INIT_ONCE;
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } XINPUT_VIBRATION;
typedef struct { BYTE Type, SubType; WORD Flags; XINPUT_GAMEPAD Gamepad; XINPUT_VIBRATION Vibration; } XINPUT_CAPABILITIES;
typedef struct { XINPUT_CAPABILITIES Capabilities; } XINPUT_CAPABILITIES_EX;
_Static_assert(sizeof(DWORD) == 4, "Windows DWORD width");
_Static_assert(sizeof(XINPUT_GAMEPAD) == 12, "wire gamepad width");
_Static_assert(sizeof(XINPUT_STATE) == 16, "Windows state width");
#define WINAPI
#define TRUE 1
#define FALSE 0
#define INIT_ONCE_STATIC_INIT {0}
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TRACE(...) ((void)0)
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 1u
#define GENERIC_WRITE 2u
#define FILE_SHARE_READ 1u
#define FILE_SHARE_WRITE 2u
#define FILE_SHARE_DELETE 4u
#define OPEN_EXISTING 1u
#define CREATE_ALWAYS 2u
#define FILE_ATTRIBUTE_NORMAL 0u
#define FILE_TYPE_DISK 1u
#define FILE_TYPE_UNKNOWN 0u
#define MOVEFILE_REPLACE_EXISTING 1u
#define XUSER_MAX_COUNT 4u
#define XINPUT_GAMEPAD_GUIDE 0x0400
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_DEVTYPE_GAMEPAD 1
#define XINPUT_DEVSUBTYPE_GAMEPAD 1
#define ERROR_SUCCESS 0u
#define ERROR_WRITE_FAULT 29u
#define ERROR_BAD_ARGUMENTS 160u
#define ERROR_DEVICE_NOT_CONNECTED 1167u

enum seam { NONE, COMMAND_OPEN, COMMAND_READ, COMMAND_CLOSE, ACK_OPEN, ACK_WRITE, ACK_CLOSE, ACK_MOVE, DIAG_OPEN, DIAG_WRITE, DIAG_CLOSE, DIAG_MOVE };
static ULONGLONG clock_now, jump_to;
static enum seam jump_at;
static unsigned int jumps, command_reads, ack_writes, diagnostic_writes, diagnostic_opens;
static enum seam failure_at;
static DWORD failure_error;
static char snapshot_bytes[2048];
static DWORD snapshot_length;
static unsigned int diagnostic_formats;
static DWORD fixture_pid = 4242;
static BOOL short_diagnostic_write, short_ack_write;
static unsigned int ack_moves;
#ifdef TEST_HAS_DIAGNOSTICS
static DWORD error_before_ack_close;
#endif
#ifdef TEST_HAS_DIAGNOSTICS
static DWORD last_error;
static HANDLE xinput_instance;
static DWORD GetLastError(void) { return last_error; }
static void SetLastError(DWORD value) { last_error = value; }
static LONG64 InterlockedIncrement64(volatile LONG64 *value) { return ++*value; }
static __attribute__((unused)) LONG64 InterlockedExchange64(volatile LONG64 *value, LONG64 next)
{ LONG64 old = *value; *value = next; return old; }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *value, LONG64 next, LONG64 expected)
{ LONG64 old = *value; if (old == expected) *value = next; return old; }
static LONG InterlockedExchange(volatile LONG *value, LONG next)
{ LONG old = *value; *value = next; return old; }
static LONG InterlockedCompareExchange(volatile LONG *value, LONG next, LONG expected)
{ LONG old = *value; if (old == expected) *value = next; return old; }
static DWORD GetCurrentThreadId(void) { return 777; }
#endif
static BYTE command[72];
static char ack_status[32];
static const char token[] = "0123456789abcdef0123456789abcdef";
static const HANDLE command_handle = (HANDLE)(uintptr_t)1;
static const HANDLE ack_handle = (HANDLE)(uintptr_t)2;
static const HANDLE diagnostic_handle = (HANDLE)(uintptr_t)3;
static const char *case_name;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%u: %s\n", case_name, __LINE__, #expr); return 1; } } while (0)

static void advance_at(enum seam seam)
{
    if (jump_at == seam)
    {
        assert(jump_to >= clock_now);
        clock_now = jump_to;
        jump_at = NONE;
        ++jumps;
    }
}
static ULONGLONG GetTickCount64(void) { return clock_now; }
static DWORD GetCurrentProcessId(void) { return fixture_pid; }
static DWORD GetEnvironmentVariableW(const WCHAR *name, WCHAR *value, DWORD count)
{ (void)name; (void)value; (void)count; return 0; }
static DWORD GetModuleFileNameW(HANDLE module, WCHAR *value, DWORD count)
{ (void)module; (void)value; (void)count; return 0; }
static int wcsicmp(const WCHAR *left, const WCHAR *right) { return wcscmp(left, right); }
static int fixture_swprintf(WCHAR *value, size_t count, const WCHAR *format, ...)
{ (void)value; (void)count; (void)format; return 0; }
static void InitializeCriticalSection(CRITICAL_SECTION *section) { section->held = 0; }
static void EnterCriticalSection(CRITICAL_SECTION *section) { assert(!section->held); section->held = 1; }
static void LeaveCriticalSection(CRITICAL_SECTION *section) { assert(section->held); section->held = 0; }
static BOOL InitOnceExecuteOnce(INIT_ONCE *once, BOOL (*callback)(INIT_ONCE *, void *, void **), void *param, void **context)
{ if (!once->done) { once->done = 1; return callback(once, param, context); } return TRUE; }
static HANDLE CreateFileW(const WCHAR *path, DWORD access, DWORD share, void *security,
                          DWORD disposition, DWORD attributes, HANDLE template)
{
    (void)share; (void)security; (void)disposition; (void)attributes; (void)template;
    if (wcsstr(path, L".diag"))
    {
        advance_at(DIAG_OPEN); ++diagnostic_opens;
#ifdef TEST_HAS_DIAGNOSTICS
        last_error = failure_at == DIAG_OPEN ? failure_error : 901;
#endif
        return failure_at == DIAG_OPEN ? INVALID_HANDLE_VALUE : diagnostic_handle;
    }
    assert(access == GENERIC_READ || access == GENERIC_WRITE);
    advance_at(access == GENERIC_READ ? COMMAND_OPEN : ACK_OPEN);
    return access == GENERIC_READ ? command_handle : ack_handle;
}
static DWORD GetFileType(HANDLE file) { assert(file == command_handle); return FILE_TYPE_DISK; }
static BOOL ReadFile(HANDLE file, void *data, DWORD count, DWORD *length, void *overlapped)
{
    (void)overlapped; assert(file == command_handle && count >= sizeof(command));
    advance_at(COMMAND_READ);
    if (failure_at == COMMAND_READ)
    {
#ifdef TEST_HAS_DIAGNOSTICS
        last_error = failure_error;
#endif
        return FALSE;
    }
    memcpy(data, command, sizeof(command)); *length = sizeof(command); ++command_reads;
    return TRUE;
}
static BOOL CloseHandle(HANDLE file)
{
    assert(file == command_handle || file == ack_handle || file == diagnostic_handle);
    advance_at(file == command_handle ? COMMAND_CLOSE : file == ack_handle ? ACK_CLOSE : DIAG_CLOSE);
#ifdef TEST_HAS_DIAGNOSTICS
    if (file == ack_handle) error_before_ack_close = last_error;
    last_error = 902;
#endif
    return TRUE;
}
static BOOL WriteFile(HANDLE file, const void *data, DWORD count, DWORD *written, void *overlapped)
{
    (void)data; (void)overlapped; assert(file == ack_handle || file == diagnostic_handle);
    advance_at(file == ack_handle ? ACK_WRITE : DIAG_WRITE); *written = count;
    if (file == ack_handle) ++ack_writes;
    else
    {
        ++diagnostic_writes;
        assert(count < sizeof(snapshot_bytes));
        memcpy(snapshot_bytes, data, count); snapshot_bytes[count] = 0; snapshot_length = count;
    }
#ifdef TEST_HAS_DIAGNOSTICS
    last_error = failure_at == (file == ack_handle ? ACK_WRITE : DIAG_WRITE) ? failure_error : 903;
#endif
    if (failure_at == (file == ack_handle ? ACK_WRITE : DIAG_WRITE)) { *written = 0; return FALSE; }
    if ((file == diagnostic_handle && short_diagnostic_write) || (file == ack_handle && short_ack_write))
    { assert(count); *written = count - 1; }
    return TRUE;
}
static BOOL DeleteFileW(const WCHAR *path) { (void)path; return TRUE; }
static BOOL MoveFileExW(const WCHAR *from, const WCHAR *to, DWORD flags)
{
    enum seam seam = wcsstr(from, L".diag") ? DIAG_MOVE : ACK_MOVE;
    (void)to; (void)flags; advance_at(seam);
    if (seam == ACK_MOVE) ++ack_moves;
#ifdef TEST_HAS_DIAGNOSTICS
    last_error = failure_at == seam ? failure_error : 904;
#endif
    return failure_at == seam ? FALSE : TRUE;
}
/* Normalize only the Windows32 unsigned-long format to Darwin's DWORD32
 * format. All original format bytes/arguments otherwise reach vsnprintf;
 * captured JSON therefore exercises the real shipping schema and size bound. */
static int fixture_snprintf(char *buffer, size_t count, const char *format, ...)
{
    char native_format[4096];
    size_t i = 0, out = 0;
    va_list args;
    int result;
    while (format[i])
    {
        assert(out + 3 < sizeof(native_format));
        if (format[i] == '%' && format[i + 1] == 'l' && format[i + 2] == 'u')
        { native_format[out++] = '%'; native_format[out++] = 'u'; i += 3; }
        else native_format[out++] = format[i++];
    }
    native_format[out] = 0;
    if (format[0] == '{') ++diagnostic_formats;
    va_start(args, format);
    result = vsnprintf(buffer, count, native_format, args);
    va_end(args);
    if (format[0] != '{' && result > 0 && (size_t)result < count)
    {
        const char *status = strrchr(buffer, ' ');
        size_t length = status ? strcspn(status + 1, "\n") : 0;
        assert(length < sizeof(ack_status));
        if (length) memcpy(ack_status, status + 1, length);
        ack_status[length] = 0;
    }
    return result;
}
#undef snprintf
#define snprintf fixture_snprintf
#define swprintf fixture_swprintf
/* Existing unused Win32 callback parameters are scoped to shipping source. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include TEST_BACKEND_HEADER
#pragma GCC diagnostic pop
#undef swprintf
#undef snprintf

static void u32(BYTE *output, DWORD value)
{ output[0] = value; output[1] = value >> 8; output[2] = value >> 16; output[3] = value >> 24; }
static void publish(ULONGLONG sequence, DWORD lease, WORD buttons, DWORD flags)
{
    memset(command, 0, sizeof(command));
    memcpy(command, "VXIP0001", 8); memcpy(command + 8, token, 32);
    u32(command + 40, (DWORD)sequence); u32(command + 44, (DWORD)(sequence >> 32));
    u32(command + 48, lease); command[52] = buttons; command[53] = buttons >> 8;
    u32(command + 64, GetCurrentProcessId()); u32(command + 68, flags);
}
static void reset(void)
{
    memset(&test_backend, 0, sizeof(test_backend));
#ifdef TEST_HAS_DIAGNOSTICS
    memset(&test_diagnostics, 0, sizeof(test_diagnostics));
    last_error = 77; error_before_ack_close = 0;
#endif
    test_backend_once.done = 1;
    InitializeCriticalSection(&test_backend_cs);
    test_backend.active = test_backend.enabled = test_backend.armed = test_backend.ever_armed = TRUE;
    memcpy(test_backend.token, token, sizeof(token));
    test_backend.sequence = 2;
    test_backend.deadline = 150;
    test_backend.state.dwPacketNumber = 7;
    test_backend.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
    test_backend.status = "STATE";
    clock_now = 100; jump_to = 0; jump_at = NONE;
    jumps = command_reads = ack_writes = diagnostic_writes = diagnostic_opens = 0;
    failure_at = NONE; failure_error = 0; fixture_pid = 4242; short_diagnostic_write = short_ack_write = FALSE; ack_moves = 0;
    ack_status[0] = snapshot_bytes[0] = 0; snapshot_length = 0; diagnostic_formats = 0;
    publish(2, 50, XINPUT_GAMEPAD_A, 0);
}
static int neutral(const XINPUT_STATE *state)
{ const XINPUT_GAMEPAD zero = {0}; return !memcmp(&state->Gamepad, &zero, sizeof(zero)); }
static XINPUT_STATE poll(void)
{
    XINPUT_STATE state;
    memset(&state, 0xcc, sizeof(state));
    assert(test_backend_get_state(0, &state) == ERROR_SUCCESS);
    assert(!test_backend_cs.held);
    return state;
}
static int existing(enum seam seam)
{
    XINPUT_STATE state;
    reset(); jump_at = seam; jump_to = 150; state = poll();
    CHECK(jumps == 1 && command_reads == 1);
    CHECK(neutral(&state)); CHECK(!test_backend.armed && !test_backend.deadline);
    CHECK(test_backend.sequence == 2 && state.dwPacketNumber == 8);
    state = poll(); CHECK(neutral(&state) && state.dwPacketNumber == 8);
    return 0;
}
static int incoming_state(enum seam seam)
{
    XINPUT_STATE state;
    reset(); publish(3, 1000, XINPUT_GAMEPAD_B, 0);
    jump_at = seam; jump_to = 150; state = poll();
    CHECK(jumps == 1 && command_reads == 1);
    CHECK(neutral(&state)); CHECK(!test_backend.armed && !test_backend.deadline);
    CHECK(test_backend.sequence == 2);
    /* The rejected STATE must not turn a later read into a renewal. */
    clock_now = 151; state = poll(); CHECK(neutral(&state) && test_backend.sequence == 2);
    return 0;
}
static int fresh_arm(enum seam seam)
{
    XINPUT_STATE state;
    reset(); publish(3, 50, 0, 1);
    jump_at = seam; jump_to = 200; state = poll();
    CHECK(jumps == 1 && command_reads == 1 && neutral(&state));
    CHECK(test_backend.armed && test_backend.sequence == 3 && test_backend.deadline == 250);
    clock_now = 201; test_backend.next_read = 0;
    publish(4, 50, XINPUT_GAMEPAD_B, 0); state = poll();
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_B && test_backend.sequence == 4);
    CHECK(test_backend.deadline == 251);
    return 0;
}
static int final_handoff(enum seam seam)
{
    XINPUT_STATE state;
    reset(); jump_at = seam; jump_to = 150;
    /* Skip command I/O: ACK is the last external operation in poll(TRUE).
     * The guard before the caller-visible copy must catch that boundary. */
    test_backend.next_read = 1000;
    state = poll();
    CHECK(jumps == 1 && command_reads == 0 && ack_writes == 1);
    CHECK(neutral(&state) && !test_backend.armed && !test_backend.deadline);
    CHECK(state.dwPacketNumber == 8 && test_backend.sequence == 2);
    state = poll(); CHECK(neutral(&state) && state.dwPacketNumber == 8);
    return 0;
}
static int new_state_ack(enum seam seam)
{
    XINPUT_STATE state;
    reset(); publish(3, 10, XINPUT_GAMEPAD_B, 0);
    jump_at = seam; jump_to = 110; state = poll();
    CHECK(jumps == 1 && command_reads == 1 && ack_writes == 1);
    CHECK(neutral(&state) && !test_backend.armed && !test_backend.deadline);
    CHECK(test_backend.sequence == 3 && state.dwPacketNumber == 9);
    return 0;
}
static int ordinary(void)
{
    XINPUT_STATE state;
    XINPUT_CAPABILITIES_EX caps;
    reset(); CHECK(test_backend_active());
    CHECK(test_backend_capabilities(0, &caps) == ERROR_SUCCESS);
    CHECK(caps.Capabilities.Type == XINPUT_DEVTYPE_GAMEPAD);
    CHECK(test_backend_get_state(4, &state) == ERROR_BAD_ARGUMENTS);
    CHECK(test_backend_get_state(0, NULL) == ERROR_BAD_ARGUMENTS);
    CHECK(test_backend_get_state(1, &state) == ERROR_DEVICE_NOT_CONNECTED);
    state = poll(); CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    CHECK(state.dwPacketNumber == 7 && test_backend.deadline == 150 && test_backend.sequence == 2);
    clock_now = 149; test_backend.next_read = 1000;
    state = poll(); CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_A && state.dwPacketNumber == 7);
    clock_now = 150; state = poll(); CHECK(neutral(&state) && state.dwPacketNumber == 8);
    clock_now = 151; state = poll(); CHECK(neutral(&state) && state.dwPacketNumber == 8);
    CHECK(test_backend.sequence == 2 && !test_backend.deadline);
    CHECK(!diagnostic_formats && !diagnostic_writes && !diagnostic_opens);
    return 0;
}
static int enabling(void)
{
    XINPUT_STATE state;
    reset();
    test_backend_enable(TRUE); CHECK(test_backend.armed && test_backend.state.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    test_backend_enable(FALSE); state = poll(); CHECK(neutral(&state) && !test_backend.armed && !test_backend.enabled);
    test_backend_enable(TRUE); CHECK(test_backend.enabled && !test_backend.armed);
    publish(3, 1000, XINPUT_GAMEPAD_B, 0); clock_now = 105; state = poll();
    CHECK(neutral(&state) && test_backend.sequence == 2);
    publish(4, 50, 0, 1); clock_now = 110; state = poll();
    CHECK(neutral(&state) && test_backend.armed && test_backend.sequence == 4);
    test_backend_enable(TRUE); CHECK(test_backend.armed && test_backend.deadline == 160);
    publish(5, 50, XINPUT_GAMEPAD_B, 0); clock_now = 115; state = poll();
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_B && test_backend.sequence == 5);
    CHECK(!diagnostic_formats && !diagnostic_writes && !diagnostic_opens);
    return 0;
}
#ifdef TEST_HAS_DIAGNOSTICS
static int diagnostic_handoff(enum seam seam)
{
    XINPUT_STATE state;
    reset(); test_diagnostics.enabled = TRUE;
    wcscpy(test_diagnostics.path, L"fixture.diag");
    wcscpy(test_diagnostics.temporary, L"fixture.diag.tmp");
    test_backend.next_read = 1000;
    jump_at = seam; jump_to = 150; state = poll();
    CHECK(jumps == 1 && command_reads == 0 && diagnostic_writes == 1);
    CHECK(neutral(&state) && !test_backend.armed && !test_backend.deadline);
    CHECK(state.dwPacketNumber == 8);
    CHECK(test_diagnostics.entries[TEST_STATE] == 1 && test_diagnostics.exits[TEST_STATE] == 1);
    CHECK(test_diagnostics.exit_tick[TEST_STATE] == 150);
    return 0;
}
static void diagnostics_on(void)
{
    test_diagnostics.enabled = TRUE;
    strcpy(test_diagnostics.module, "xinput1_3.dll");
    wcscpy(test_diagnostics.path, L"fixture.diag");
    wcscpy(test_diagnostics.temporary, L"fixture.diag.tmp");
}
static ULONGLONG field(const char *key)
{
    char needle[80];
    const char *start;
    int length = snprintf(needle, sizeof(needle), "\"%s\":", key);
    assert(length > 0 && (size_t)length < sizeof(needle));
    start = strstr(snapshot_bytes, needle);
    assert(start);
    return strtoull(start + length, NULL, 10);
}
static int diagnostic_throttle(void)
{
    reset(); diagnostics_on();
    (void)poll(); CHECK(diagnostic_writes == 1 && diagnostic_opens == 1 && field("snapshot_tick") == 100);
    clock_now = 101; (void)poll(); CHECK(diagnostic_writes == 1);
    clock_now = 199; (void)poll(); CHECK(diagnostic_writes == 1);
    clock_now = 200; (void)poll(); CHECK(diagnostic_writes == 2 && diagnostic_opens == 2);
    CHECK(field("snapshot_tick") == 200);
    CHECK(test_diagnostics.entries[TEST_STATE] == 4 && test_diagnostics.exits[TEST_STATE] == 4);
    CHECK(test_diagnostics.exit_tick[TEST_STATE] == 200);
    /* The snapshot is taken before the current handoff, so its exits lag one. */
    CHECK(field("state_entries") == 4 && field("state_exits") == 3);
    CHECK(field("state_thread") == 777 && field("handoff_tick") == 199);
    CHECK(test_diagnostics.handoff_tick == 200 && test_diagnostics.handoff_sequence == 2);
    return 0;
}
static int diagnostic_acceptance(void)
{
    XINPUT_STATE state;
    reset(); diagnostics_on(); test_diagnostics.next_snapshot = ULLONG_MAX;
    publish(3, 1000, 0, 1);
    EnterCriticalSection(&test_backend_cs); test_backend_poll(FALSE); LeaveCriticalSection(&test_backend_cs);
    CHECK(test_diagnostics.accepted_sequence == 3 && test_diagnostics.accepted_via == 2 && test_diagnostics.accepted_tick == 100);
    CHECK(test_backend.polls == 0 && !ack_writes && !test_diagnostics.entries[TEST_STATE]);
    clock_now = 105; publish(4, 1000, XINPUT_GAMEPAD_B, 0);
    EnterCriticalSection(&test_backend_cs); test_backend_poll(FALSE); LeaveCriticalSection(&test_backend_cs);
    CHECK(test_diagnostics.accepted_sequence == 4 && test_diagnostics.accepted_via == 2 && test_diagnostics.accepted_tick == 105);
    CHECK(test_backend.polls == 0 && !ack_writes);
    clock_now = 110; publish(5, 1000, XINPUT_GAMEPAD_A, 0); state = poll();
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    CHECK(test_diagnostics.accepted_sequence == 5 && test_diagnostics.accepted_via == 1 && test_diagnostics.accepted_tick == 110);
    CHECK(test_backend.polls == 1 && ack_writes == 1);
    return 0;
}
static int diagnostic_io_failure(enum seam seam)
{
    XINPUT_STATE state;
    reset(); diagnostics_on(); failure_at = seam; failure_error = 7331;
    state = poll();
    CHECK(test_diagnostics.io_failures == 1 && test_diagnostics.io_error == 7331);
    CHECK(test_diagnostics.io_error_stage == (DWORD)(seam == COMMAND_READ ? TEST_READ : TEST_ACK_WRITE));
    CHECK(test_diagnostics.io_error_tick == 100 && test_diagnostics.io_entries == test_diagnostics.io_exits);
    CHECK(field("io_error") == 7331 && field("io_failures") == 1);
    CHECK(field("io_error_stage") == (DWORD)(seam == COMMAND_READ ? TEST_READ : TEST_ACK_WRITE));
    if (seam == COMMAND_READ) CHECK(neutral(&state) && test_backend.sequence == 2);
    else CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    return 0;
}
static int diagnostic_snapshot_failure(enum seam seam)
{
    reset(); diagnostics_on(); failure_at = seam; failure_error = 8222; last_error = 8111;
    EnterCriticalSection(&test_backend_cs); test_diagnostics_snapshot(); LeaveCriticalSection(&test_backend_cs);
    CHECK(last_error == 8111 && test_diagnostics.snapshot_failures == 1 && test_diagnostics.snapshot_error == 8222);
    CHECK(diagnostic_opens == 1);
    clock_now = 101;
    EnterCriticalSection(&test_backend_cs); test_diagnostics_snapshot(); LeaveCriticalSection(&test_backend_cs);
    CHECK(last_error == 8111 && diagnostic_opens == 1 && test_diagnostics.snapshot_failures == 1);
    return 0;
}
static int diagnostic_worst_json(void)
{
    unsigned int i;
    reset(); diagnostics_on();
    memset(test_diagnostics.module, 'm', sizeof(test_diagnostics.module) - 1);
    test_diagnostics.module[sizeof(test_diagnostics.module) - 1] = 0;
    test_backend.sequence = test_backend.deadline = ULLONG_MAX;
    test_backend.status = "DISABLED";
    test_diagnostics.accepted_sequence = test_diagnostics.accepted_tick = ULLONG_MAX;
    test_diagnostics.enable_tick = test_diagnostics.enable_transitions = ULLONG_MAX;
    test_diagnostics.io_entries = test_diagnostics.io_exits = test_diagnostics.io_failures = ULLONG_MAX;
    test_diagnostics.io_enter_tick = test_diagnostics.io_exit_tick = test_diagnostics.io_error_tick = ULLONG_MAX;
    test_diagnostics.snapshot_failures = ULLONG_MAX;
    test_diagnostics.handoff_tick = test_diagnostics.handoff_sequence = ULLONG_MAX;
    test_diagnostics.handoff_api = UINT32_MAX; test_diagnostics.handoff_status = "DISABLED";
    fixture_pid = UINT32_MAX;
    for (i = 0; i < 3; ++i) test_diagnostics.thread[i] = -1;
    test_diagnostics.accepted_via = test_diagnostics.io_stage = test_diagnostics.io_error_stage = UINT32_MAX;
    test_diagnostics.io_error = test_diagnostics.snapshot_error = UINT32_MAX;
    for (i = 0; i < 3; ++i)
        test_diagnostics.entries[i] = test_diagnostics.exits[i] = test_diagnostics.entry_tick[i] = test_diagnostics.exit_tick[i] = -1;
    clock_now = ULLONG_MAX - 200; last_error = 8111;
    EnterCriticalSection(&test_backend_cs); test_diagnostics_snapshot(); LeaveCriticalSection(&test_backend_cs);
    CHECK(diagnostic_writes == 1 && snapshot_length > 1000 && snapshot_length < 2048);
    CHECK(snapshot_length == strlen(snapshot_bytes) && snapshot_bytes[snapshot_length - 1] == '\n');
    CHECK(snapshot_bytes[0] == '{' && snapshot_bytes[snapshot_length - 2] == '}');
    CHECK(field("sequence") == ULLONG_MAX && field("state_entries") == ULLONG_MAX);
    CHECK(field("io_error") == UINT32_MAX && field("snapshot_error") == UINT32_MAX);
    CHECK(field("snapshot_failures") == ULLONG_MAX && last_error == 8111);
    CHECK(field("pid") == UINT32_MAX && field("state_thread") == UINT32_MAX);
    CHECK(field("handoff_tick") == ULLONG_MAX && field("handoff_api") == UINT32_MAX);
    printf("diagnostic_worst_json_bytes=%u\n", snapshot_length);
    return 0;
}
static int diagnostic_short_write(void)
{
    reset(); diagnostics_on(); short_diagnostic_write = TRUE; last_error = 8111;
    EnterCriticalSection(&test_backend_cs); test_diagnostics_snapshot(); LeaveCriticalSection(&test_backend_cs);
    CHECK(diagnostic_writes == 1 && test_diagnostics.snapshot_failures == 1);
    CHECK(test_diagnostics.snapshot_error == ERROR_WRITE_FAULT && last_error == 8111);
    /* Own-write failure belongs in the next eventual snapshot. */
    clock_now = 200; short_diagnostic_write = FALSE;
    EnterCriticalSection(&test_backend_cs); test_diagnostics_snapshot(); LeaveCriticalSection(&test_backend_cs);
    CHECK(diagnostic_writes == 2 && field("snapshot_failures") == 1);
    CHECK(field("snapshot_error") == ERROR_WRITE_FAULT && last_error == 8111);
    return 0;
}

static int diagnostic_ack_short_write(void)
{
    XINPUT_STATE state;
    reset(); diagnostics_on(); short_ack_write = TRUE; state = poll();
    CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_A && test_backend.sequence == 2);
    CHECK(ack_writes == 1 && ack_moves == 0);
    CHECK(test_diagnostics.io_failures == 1 && test_diagnostics.io_error == ERROR_WRITE_FAULT);
    CHECK(test_diagnostics.io_error_stage == TEST_ACK_WRITE);
    CHECK(field("io_error") == ERROR_WRITE_FAULT && field("io_failures") == 1);
    CHECK(error_before_ack_close == 903 && last_error == 902);
    return 0;
}

#endif
int main(int argc, char **argv)
{
    int result;
    assert(argc == 2); case_name = argv[1];
    if (!strcmp(case_name, "ordinary")) result = ordinary();
    else if (!strcmp(case_name, "enabling")) result = enabling();
    else if (!strncmp(case_name, "existing-", 9)) result = existing((enum seam)atoi(case_name + 9));
    else if (!strncmp(case_name, "incoming-", 9)) result = incoming_state((enum seam)atoi(case_name + 9));
    else if (!strncmp(case_name, "arm-", 4)) result = fresh_arm((enum seam)atoi(case_name + 4));
    else if (!strncmp(case_name, "handoff-", 8)) result = final_handoff((enum seam)atoi(case_name + 8));
    else if (!strncmp(case_name, "accepted-ack-", 13)) result = new_state_ack((enum seam)atoi(case_name + 13));
#ifdef TEST_HAS_DIAGNOSTICS
    else if (!strcmp(case_name, "diag-throttle")) result = diagnostic_throttle();
    else if (!strcmp(case_name, "diag-accepted")) result = diagnostic_acceptance();
    else if (!strcmp(case_name, "diag-worst-json")) result = diagnostic_worst_json();
    else if (!strcmp(case_name, "diag-short-write")) result = diagnostic_short_write();
    else if (!strcmp(case_name, "diag-ack-short-write")) result = diagnostic_ack_short_write();
    else if (!strncmp(case_name, "diag-io-", 8)) result = diagnostic_io_failure((enum seam)atoi(case_name + 8));
    else if (!strncmp(case_name, "diag-fail-", 10)) result = diagnostic_snapshot_failure((enum seam)atoi(case_name + 10));
    else if (!strncmp(case_name, "diagnostic-", 11)) result = diagnostic_handoff((enum seam)atoi(case_name + 11));
#endif
    else return 2;
    if (!result) puts(case_name);
    return result;
}

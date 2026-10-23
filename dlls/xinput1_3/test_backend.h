/* Process-local, opt-in XInput test transport. LGPL-2.1-or-later, like main.c. */
/* This is a test backend, not a system input device or foreground policy. */

static struct
{
    BOOL active, enabled, armed, ever_armed;
    WCHAR path[1024], ack_path[1060], temp_path[1100];
    char token[33];
    ULONGLONG sequence, initial_sequence, deadline, next_read, next_ack, polls;
    XINPUT_STATE state;
    XINPUT_GAMEPAD last_keystroke;
    const char *status;
} test_backend;

static CRITICAL_SECTION test_backend_cs;
static INIT_ONCE test_backend_once = INIT_ONCE_STATIC_INIT;

enum test_api { TEST_STATE, TEST_KEY, TEST_ENABLE };
enum test_io { TEST_OPEN = 1, TEST_TYPE, TEST_READ, TEST_CLOSE,
               TEST_ACK_OPEN, TEST_ACK_WRITE, TEST_ACK_CLOSE, TEST_ACK_RENAME };

/* Optional, eventual observations. No worker, new input path, or synthetic poll.
 * Atomic entries can advance while another thread holds test_backend_cs. */
static struct
{
    BOOL enabled;
    WCHAR path[1060], temporary[1100];
    char module[64];
    volatile LONG64 entries[3], exits[3], entry_tick[3], exit_tick[3];
    volatile LONG thread[3];
    ULONGLONG next_snapshot, accepted_tick, accepted_sequence, enable_tick, enable_transitions;
    ULONGLONG io_entries, io_exits, io_failures, io_enter_tick, io_exit_tick, io_error_tick;
    ULONGLONG snapshot_failures;
    DWORD accepted_via, io_stage, io_error_stage, io_error, snapshot_error, handoff_api;
    ULONGLONG handoff_tick, handoff_sequence;
    const char *handoff_status;
} test_diagnostics;

static ULONGLONG test_diagnostics_atomic(volatile LONG64 *value)
{
    return InterlockedCompareExchange64(value, 0, 0);
}

/* Best-effort atomic maximum. A contended timestamp update may be omitted;
 * entries/exits remain counters, and timestamps never move backwards. */
static void test_diagnostics_tick(volatile LONG64 *value, ULONGLONG now)
{
    LONG64 previous = test_diagnostics_atomic(value), observed;
    unsigned int tries;

    for (tries = 0; tries < 8 && (ULONGLONG)previous < now; ++tries)
    {
        observed = InterlockedCompareExchange64(value, now, previous);
        if (observed == previous) break;
        previous = observed;
    }
}

static void test_diagnostics_init(void)
{
    WCHAR value[2], module[1024], *name;
    DWORD n, i;

    if (GetEnvironmentVariableW(L"VIBEWINE_XINPUT_TEST_DIAGNOSTICS", value, ARRAY_SIZE(value)) != 1 ||
        value[0] != '1') return;
    strcpy(test_diagnostics.module, "unknown");
    n = GetModuleFileNameW(xinput_instance, module, ARRAY_SIZE(module));
    if (n && n < ARRAY_SIZE(module))
    {
        name = wcsrchr(module, '\\');
        name = name ? name + 1 : module;
        for (i = 0; name[i] && i + 1 < ARRAY_SIZE(test_diagnostics.module); ++i)
            test_diagnostics.module[i] = (name[i] >= 'a' && name[i] <= 'z') ||
                (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') ||
                name[i] == '_' || name[i] == '.' ? name[i] : '_';
        test_diagnostics.module[i] = 0;
    }
    swprintf(test_diagnostics.path, ARRAY_SIZE(test_diagnostics.path), L"%s.diag", test_backend.path);
    swprintf(test_diagnostics.temporary, ARRAY_SIZE(test_diagnostics.temporary), L"%s.diag.tmp.%lu",
             test_backend.path, GetCurrentProcessId());
    test_diagnostics.enabled = TRUE;
}

static void test_diagnostics_enter(enum test_api api)
{
    if (!test_diagnostics.enabled) return;
    InterlockedIncrement64(&test_diagnostics.entries[api]);
    test_diagnostics_tick(&test_diagnostics.entry_tick[api], GetTickCount64());
    InterlockedExchange(&test_diagnostics.thread[api], GetCurrentThreadId());
}

/* Exit is the final handoff boundary, not proof the caller resumed execution. */
static void test_diagnostics_exit(enum test_api api, ULONGLONG now)
{
    if (!test_diagnostics.enabled) return;
    test_diagnostics_tick(&test_diagnostics.exit_tick[api], now);
    InterlockedIncrement64(&test_diagnostics.exits[api]);
    test_diagnostics.handoff_tick = now;
    test_diagnostics.handoff_sequence = test_backend.sequence;
    test_diagnostics.handoff_status = test_backend.status;
    test_diagnostics.handoff_api = api + 1;
}

static void test_diagnostics_io_begin(enum test_io stage)
{
    if (!test_diagnostics.enabled) return;
    ++test_diagnostics.io_entries;
    test_diagnostics.io_stage = stage;
    test_diagnostics.io_enter_tick = GetTickCount64();
}

static void test_diagnostics_io_end(BOOL success, DWORD failure)
{
    DWORD error;

    if (!test_diagnostics.enabled) return;
    error = GetLastError();
    ++test_diagnostics.io_exits;
    test_diagnostics.io_exit_tick = GetTickCount64();
    if (!success)
    {
        ++test_diagnostics.io_failures;
        test_diagnostics.io_error = failure ? failure : error;
        test_diagnostics.io_error_stage = test_diagnostics.io_stage;
        test_diagnostics.io_error_tick = test_diagnostics.io_exit_tick;
    }
    SetLastError(error);
}

/* Caller holds test_backend_cs. Snapshot is bounded in size/frequency, not in
 * filesystem latency. Its own exit/failure is visible only in a later snapshot.
 * Every input handoff must recheck expiry AFTER this function's final I/O. */
static void test_diagnostics_snapshot(void)
{
    char buffer[2048];
    DWORD saved, written;
    ULONGLONG now;
    int length;
    HANDLE file;
    BOOL success;

    if (!test_diagnostics.enabled) return;
    saved = GetLastError();
    now = GetTickCount64();
    if (now < test_diagnostics.next_snapshot) goto done;
    test_diagnostics.next_snapshot = now + 100;
    length = snprintf(buffer, sizeof(buffer),
        "{\"version\":1,\"token\":\"%s\",\"pid\":%lu,\"module\":\"%s\",\"snapshot_tick\":%llu,"
        "\"sequence\":%llu,\"status\":\"%s\",\"enabled\":%u,\"armed\":%u,\"deadline\":%llu,"
        "\"accepted_sequence\":%llu,\"accepted_via\":%lu,\"accepted_tick\":%llu,"
        "\"state_entries\":%llu,\"state_exits\":%llu,\"state_enter_tick\":%llu,\"state_exit_tick\":%llu,"
        "\"key_entries\":%llu,\"key_exits\":%llu,\"key_enter_tick\":%llu,\"key_exit_tick\":%llu,"
        "\"enable_entries\":%llu,\"enable_exits\":%llu,\"enable_enter_tick\":%llu,\"enable_exit_tick\":%llu,"
        "\"enable_transitions\":%llu,\"enable_tick\":%llu,"
        "\"state_thread\":%lu,\"key_thread\":%lu,\"enable_thread\":%lu,"
        "\"handoff_tick\":%llu,\"handoff_sequence\":%llu,\"handoff_api\":%lu,\"handoff_status\":\"%s\","
        "\"io_entries\":%llu,\"io_exits\":%llu,\"io_failures\":%llu,\"io_stage\":%lu,"
        "\"io_enter_tick\":%llu,\"io_exit_tick\":%llu,\"io_error_stage\":%lu,\"io_error\":%lu,\"io_error_tick\":%llu,"
        "\"snapshot_failures\":%llu,\"snapshot_error\":%lu}\n",
        test_backend.token, GetCurrentProcessId(), test_diagnostics.module, now,
        test_backend.sequence, test_backend.status, test_backend.enabled, test_backend.armed, test_backend.deadline,
        test_diagnostics.accepted_sequence, test_diagnostics.accepted_via, test_diagnostics.accepted_tick,
        test_diagnostics_atomic(&test_diagnostics.entries[TEST_STATE]), test_diagnostics_atomic(&test_diagnostics.exits[TEST_STATE]),
        test_diagnostics_atomic(&test_diagnostics.entry_tick[TEST_STATE]), test_diagnostics_atomic(&test_diagnostics.exit_tick[TEST_STATE]),
        test_diagnostics_atomic(&test_diagnostics.entries[TEST_KEY]), test_diagnostics_atomic(&test_diagnostics.exits[TEST_KEY]),
        test_diagnostics_atomic(&test_diagnostics.entry_tick[TEST_KEY]), test_diagnostics_atomic(&test_diagnostics.exit_tick[TEST_KEY]),
        test_diagnostics_atomic(&test_diagnostics.entries[TEST_ENABLE]), test_diagnostics_atomic(&test_diagnostics.exits[TEST_ENABLE]),
        test_diagnostics_atomic(&test_diagnostics.entry_tick[TEST_ENABLE]), test_diagnostics_atomic(&test_diagnostics.exit_tick[TEST_ENABLE]),
        test_diagnostics.enable_transitions, test_diagnostics.enable_tick,
        (DWORD)InterlockedCompareExchange(&test_diagnostics.thread[TEST_STATE], 0, 0),
        (DWORD)InterlockedCompareExchange(&test_diagnostics.thread[TEST_KEY], 0, 0),
        (DWORD)InterlockedCompareExchange(&test_diagnostics.thread[TEST_ENABLE], 0, 0),
        test_diagnostics.handoff_tick, test_diagnostics.handoff_sequence, test_diagnostics.handoff_api,
        test_diagnostics.handoff_status ? test_diagnostics.handoff_status : "NONE",
        test_diagnostics.io_entries, test_diagnostics.io_exits, test_diagnostics.io_failures, test_diagnostics.io_stage,
        test_diagnostics.io_enter_tick, test_diagnostics.io_exit_tick, test_diagnostics.io_error_stage,
        test_diagnostics.io_error, test_diagnostics.io_error_tick, test_diagnostics.snapshot_failures, test_diagnostics.snapshot_error);
    if (length < 0 || (size_t)length >= sizeof(buffer)) goto done;
    file = CreateFileW(test_diagnostics.temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto failed;
    success = WriteFile(file, buffer, length, &written, NULL);
    if (!success || written != length)
    {
        test_diagnostics.snapshot_error = success ? ERROR_WRITE_FAULT : GetLastError();
        CloseHandle(file);
        DeleteFileW(test_diagnostics.temporary);
        ++test_diagnostics.snapshot_failures;
        goto done;
    }
    success = CloseHandle(file);
    if (!success) goto failed;
    if (!MoveFileExW(test_diagnostics.temporary, test_diagnostics.path, MOVEFILE_REPLACE_EXISTING))
    {
        test_diagnostics.snapshot_error = GetLastError();
        DeleteFileW(test_diagnostics.temporary);
        ++test_diagnostics.snapshot_failures;
    }
    goto done;
failed:
    test_diagnostics.snapshot_error = GetLastError();
    ++test_diagnostics.snapshot_failures;
done:
    SetLastError(saved);
}

static BOOL WINAPI init_test_backend(INIT_ONCE *once, void *param, void **context)
{
    WCHAR exe[1024], target[1024], token[34];
    DWORD n, i;

    test_backend.enabled = TRUE;
    test_backend.status = "READY";
    n = GetEnvironmentVariableW(L"VIBEWINE_XINPUT_TEST_FILE", test_backend.path, ARRAY_SIZE(test_backend.path));
    TRACE("process-local test file length %lu, path %s.\n", n, debugstr_w(test_backend.path));
    if (!n || n >= ARRAY_SIZE(test_backend.path)) return TRUE;
    /* Exclude device/pipe/UNC names: the transport is an ordinary absolute file. */
    if (!((test_backend.path[0] >= 'A' && test_backend.path[0] <= 'Z') ||
          (test_backend.path[0] >= 'a' && test_backend.path[0] <= 'z')) ||
        test_backend.path[1] != ':' || test_backend.path[2] != '\\') return TRUE;
    n = GetEnvironmentVariableW(L"VIBEWINE_XINPUT_TEST_EXE", target, ARRAY_SIZE(target));
    if (!n || n >= ARRAY_SIZE(target)) return TRUE;
    TRACE("process-local test target length %lu, target %s.\n", n, debugstr_w(target));
    n = GetModuleFileNameW(NULL, exe, ARRAY_SIZE(exe));
    TRACE("process-local test executable length %lu, current %s.\n", n, debugstr_w(exe));
    if (!n || n >= ARRAY_SIZE(exe) || wcsicmp(target, exe)) return TRUE;
    n = GetEnvironmentVariableW(L"VIBEWINE_XINPUT_TEST_TOKEN", token, ARRAY_SIZE(token));
    TRACE("process-local test token length %lu (value redacted).\n", n);
    if (n != 32) return TRUE;
    for (i = 0; i < 32; ++i)
    {
        if (!((token[i] >= '0' && token[i] <= '9') || (token[i] >= 'a' && token[i] <= 'f'))) return TRUE;
        test_backend.token[i] = token[i];
    }
    swprintf(test_backend.ack_path, ARRAY_SIZE(test_backend.ack_path), L"%s.ack", test_backend.path);
    swprintf(test_backend.temp_path, ARRAY_SIZE(test_backend.temp_path), L"%s.ack.tmp.%lu", test_backend.path, GetCurrentProcessId());
    InitializeCriticalSection(&test_backend_cs);
    test_backend.active = TRUE;
    test_diagnostics_init();
    TRACE("process-local test backend active for pid %lu.\n", GetCurrentProcessId());
    return TRUE;
}

static BOOL test_backend_active(void)
{
    InitOnceExecuteOnce(&test_backend_once, init_test_backend, NULL, NULL);
    return test_backend.active;
}

static void test_backend_neutral(const char *status)
{
    static const XINPUT_GAMEPAD neutral;

    if (memcmp(&test_backend.state.Gamepad, &neutral, sizeof(neutral))) ++test_backend.state.dwPacketNumber;
    test_backend.state.Gamepad = neutral;
    test_backend.armed = FALSE;
    test_backend.deadline = 0;
    test_backend.status = status;
}

/* File and receipt I/O can block past a lease. Never reuse the entry clock at
 * acceptance or the final state handoff. Caller holds test_backend_cs. */
static ULONGLONG test_backend_expire(void)
{
    ULONGLONG now = GetTickCount64();

    if (test_backend.deadline && now >= test_backend.deadline)
    {
        test_backend_neutral("EXPIRED");
        test_backend.next_ack = 0;
    }
    return now;
}

static DWORD test_read_u32(const BYTE *p)
{
    return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
}

static void test_backend_ack(ULONGLONG now)
{
    char buffer[180];
    DWORD len, written;
    HANDLE file;
    BOOL success;

    if (now < test_backend.next_ack) return;
    test_backend.next_ack = now + 100;
    len = snprintf(buffer, sizeof(buffer), "VXIA0001 %s %lu %llu %llu %u %s\n", test_backend.token,
                   GetCurrentProcessId(), test_backend.sequence, test_backend.polls,
                   test_backend.enabled, test_backend.status);
    test_diagnostics_io_begin(TEST_ACK_OPEN);
    file = CreateFileW(test_backend.temp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    test_diagnostics_io_end(file != INVALID_HANDLE_VALUE, 0);
    if (file == INVALID_HANDLE_VALUE) return;
    test_diagnostics_io_begin(TEST_ACK_WRITE);
    success = WriteFile(file, buffer, len, &written, NULL);
    test_diagnostics_io_end(success && written == len, success && written != len ? ERROR_WRITE_FAULT : 0);
    if (!success || written != len)
    {
        CloseHandle(file);
        DeleteFileW(test_backend.temp_path);
        return;
    }
    test_diagnostics_io_begin(TEST_ACK_CLOSE);
    success = CloseHandle(file);
    test_diagnostics_io_end(success, 0);
    test_diagnostics_io_begin(TEST_ACK_RENAME);
    success = MoveFileExW(test_backend.temp_path, test_backend.ack_path, MOVEFILE_REPLACE_EXISTING);
    test_diagnostics_io_end(success, 0);
    if (!success)
        DeleteFileW(test_backend.temp_path);
}

/* Caller holds test_backend_cs. Malformed records never advance sequence. */
static void test_backend_poll(BOOL count_poll)
{
    static const XINPUT_GAMEPAD neutral;
    ULONGLONG now = test_backend_expire(), sequence;
    XINPUT_GAMEPAD gamepad;
    BYTE data[73];
    DWORD length, lease, pid, flags, type;
    HANDLE file;
    BOOL valid, success;

    if (count_poll) ++test_backend.polls;
    if (now < test_backend.next_read) goto done;
    test_backend.next_read = now + 4;
    test_diagnostics_io_begin(TEST_OPEN);
    file = CreateFileW(test_backend.path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    test_diagnostics_io_end(file != INVALID_HANDLE_VALUE, 0);
    if (file == INVALID_HANDLE_VALUE)
    {
        test_backend_neutral("INVALID");
        goto done;
    }
    test_diagnostics_io_begin(TEST_TYPE);
    type = GetFileType(file);
    test_diagnostics_io_end(type != FILE_TYPE_UNKNOWN || GetLastError() == ERROR_SUCCESS, 0);
    valid = FALSE;
    if (type == FILE_TYPE_DISK)
    {
        test_diagnostics_io_begin(TEST_READ);
        success = ReadFile(file, data, sizeof(data), &length, NULL);
        test_diagnostics_io_end(success, 0);
        valid = success && length == 72;
    }
    test_diagnostics_io_begin(TEST_CLOSE);
    success = CloseHandle(file);
    test_diagnostics_io_end(success, 0);
    now = test_backend_expire();
    if (!valid || memcmp(data, "VXIP0001", 8) || memcmp(data + 8, test_backend.token, 32)) goto invalid;
    sequence = test_read_u32(data + 40) | (ULONGLONG)test_read_u32(data + 44) << 32;
    lease = test_read_u32(data + 48);
    pid = test_read_u32(data + 64);
    flags = test_read_u32(data + 68);
    memcpy(&gamepad, data + 52, sizeof(gamepad));
    if (!sequence || !lease || lease > 1000 || flags > 2 ||
        (gamepad.wButtons & ~(0xf3ff | XINPUT_GAMEPAD_GUIDE))) goto invalid;
    if (pid != GetCurrentProcessId() && !(flags == 1 && !pid &&
        (!test_backend.ever_armed || (sequence == test_backend.initial_sequence && sequence == test_backend.sequence))))
        goto invalid;
    if ((flags == 1 || flags == 2) && memcmp(&gamepad, &neutral, sizeof(gamepad))) goto invalid;
    if (sequence < test_backend.sequence) goto invalid;
    if (sequence == test_backend.sequence) goto done;
    now = test_backend_expire();
    /* Only a fresh neutral ARM may recover after invalid, expiry or disable. */
    if (!test_backend.enabled)
    {
        test_backend_neutral("DISABLED");
        goto done;
    }
    if (flags == 1)
    {
        test_backend_neutral("READY");
        test_backend.armed = test_backend.ever_armed = TRUE;
        if (!pid) test_backend.initial_sequence = sequence;
    }
    else if (flags == 2) test_backend_neutral("RELEASE");
    else if (!test_backend.armed) goto invalid;
    else
    {
        if (memcmp(&test_backend.state.Gamepad, &gamepad, sizeof(gamepad))) ++test_backend.state.dwPacketNumber;
        test_backend.state.Gamepad = gamepad;
        test_backend.status = "STATE";
    }
    test_backend.sequence = sequence;
    if (test_diagnostics.enabled)
    {
        test_diagnostics.accepted_sequence = sequence;
        test_diagnostics.accepted_via = count_poll ? 1 : 2;
        test_diagnostics.accepted_tick = now;
    }
    if (flags != 2) test_backend.deadline = now + lease;
    test_backend.next_ack = 0;
    goto done;
invalid:
    test_backend_neutral("INVALID");
done:
    now = test_backend_expire();
    if (!test_backend.enabled) test_backend.status = "DISABLED";
    if (count_poll) test_backend_ack(now);
}

static DWORD test_backend_get_state(DWORD index, XINPUT_STATE *state)
{
    if (index >= XUSER_MAX_COUNT || !state) return ERROR_BAD_ARGUMENTS;
    if (index) return ERROR_DEVICE_NOT_CONNECTED;
    test_diagnostics_enter(TEST_STATE);
    EnterCriticalSection(&test_backend_cs);
    test_backend_poll(TRUE);
    test_diagnostics_snapshot();
    test_diagnostics_exit(TEST_STATE, test_backend_expire());
    *state = test_backend.state;
    LeaveCriticalSection(&test_backend_cs);
    return ERROR_SUCCESS;
}

static void test_backend_enable(BOOL enable)
{
    test_diagnostics_enter(TEST_ENABLE);
    EnterCriticalSection(&test_backend_cs);
    if (test_backend.enabled == !!enable)
    {
        goto done;
    }
    test_backend.enabled = !!enable;
    test_backend_neutral(enable ? "INVALID" : "DISABLED");
    test_backend.next_ack = 0;
    if (test_diagnostics.enabled)
    {
        ++test_diagnostics.enable_transitions;
        test_diagnostics.enable_tick = GetTickCount64();
    }
done:
    test_diagnostics_snapshot();
    test_diagnostics_exit(TEST_ENABLE, test_backend_expire());
    LeaveCriticalSection(&test_backend_cs);
}

static DWORD test_backend_capabilities(DWORD index, XINPUT_CAPABILITIES_EX *caps)
{
    if (index >= XUSER_MAX_COUNT || !caps) return ERROR_BAD_ARGUMENTS;
    if (index) return ERROR_DEVICE_NOT_CONNECTED;
    memset(caps, 0, sizeof(*caps));
    caps->Capabilities.Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->Capabilities.SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Capabilities.Gamepad.wButtons = 0xf3ff;
    caps->Capabilities.Gamepad.bLeftTrigger = caps->Capabilities.Gamepad.bRightTrigger = 255;
    caps->Capabilities.Gamepad.sThumbLX = caps->Capabilities.Gamepad.sThumbLY = 32767;
    caps->Capabilities.Gamepad.sThumbRX = caps->Capabilities.Gamepad.sThumbRY = 32767;
    return ERROR_SUCCESS;
}

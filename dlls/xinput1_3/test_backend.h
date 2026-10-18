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

static DWORD test_read_u32(const BYTE *p)
{
    return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24;
}

static void test_backend_ack(ULONGLONG now)
{
    char buffer[180];
    DWORD len, written;
    HANDLE file;

    if (now < test_backend.next_ack) return;
    test_backend.next_ack = now + 100;
    len = snprintf(buffer, sizeof(buffer), "VXIA0001 %s %lu %llu %llu %u %s\n", test_backend.token,
                   GetCurrentProcessId(), test_backend.sequence, test_backend.polls,
                   test_backend.enabled, test_backend.status);
    file = CreateFileW(test_backend.temp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    if (!WriteFile(file, buffer, len, &written, NULL) || written != len)
    {
        CloseHandle(file);
        DeleteFileW(test_backend.temp_path);
        return;
    }
    CloseHandle(file);
    if (!MoveFileExW(test_backend.temp_path, test_backend.ack_path, MOVEFILE_REPLACE_EXISTING))
        DeleteFileW(test_backend.temp_path);
}

/* Caller holds test_backend_cs. Malformed records never advance sequence. */
static void test_backend_poll(BOOL count_poll)
{
    static const XINPUT_GAMEPAD neutral;
    ULONGLONG now = GetTickCount64(), sequence;
    XINPUT_GAMEPAD gamepad;
    BYTE data[73];
    DWORD length, lease, pid, flags;
    HANDLE file;
    BOOL valid;

    if (count_poll) ++test_backend.polls;
    if (test_backend.deadline && now >= test_backend.deadline)
        test_backend_neutral("EXPIRED");
    if (now < test_backend.next_read) goto done;
    test_backend.next_read = now + 4;
    file = CreateFileW(test_backend.path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        test_backend_neutral("INVALID");
        goto done;
    }
    valid = GetFileType(file) == FILE_TYPE_DISK && ReadFile(file, data, sizeof(data), &length, NULL) && length == 72;
    CloseHandle(file);
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
    if (flags != 2) test_backend.deadline = now + lease;
    test_backend.next_ack = 0;
    goto done;
invalid:
    test_backend_neutral("INVALID");
done:
    if (!test_backend.enabled) test_backend.status = "DISABLED";
    if (count_poll) test_backend_ack(now);
}

static DWORD test_backend_get_state(DWORD index, XINPUT_STATE *state)
{
    if (index >= XUSER_MAX_COUNT || !state) return ERROR_BAD_ARGUMENTS;
    if (index) return ERROR_DEVICE_NOT_CONNECTED;
    EnterCriticalSection(&test_backend_cs);
    test_backend_poll(TRUE);
    *state = test_backend.state;
    LeaveCriticalSection(&test_backend_cs);
    return ERROR_SUCCESS;
}

static void test_backend_enable(BOOL enable)
{
    EnterCriticalSection(&test_backend_cs);
    if (test_backend.enabled == !!enable)
    {
        LeaveCriticalSection(&test_backend_cs);
        return;
    }
    test_backend.enabled = !!enable;
    test_backend_neutral(enable ? "INVALID" : "DISABLED");
    test_backend.next_ack = 0;
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

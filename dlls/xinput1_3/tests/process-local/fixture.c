/* Executable integration fixture for Wine's opt-in process-local XInput backend. */
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static DWORD (WINAPI *get_state)(DWORD, XINPUT_STATE *);
static DWORD (WINAPI *get_state_ex)(DWORD, XINPUT_STATE *);
static DWORD (WINAPI *get_caps)(DWORD, DWORD, XINPUT_CAPABILITIES *);
static DWORD (WINAPI *set_state)(DWORD, XINPUT_VIBRATION *);
static DWORD (WINAPI *get_key)(DWORD, DWORD, XINPUT_KEYSTROKE *);
static void (WINAPI *enable)(BOOL);
static char path[MAX_PATH], token[] = "0123456789abcdef0123456789abcdef";
static unsigned failures, assertions;
static uint64_t sequence;
static BOOL diagnostics;

#define CHECK(x) do { ++assertions; if (!(x)) { printf("FAIL line %u: %s\n", __LINE__, #x); ++failures; } } while (0)

static void u32(BYTE *p, DWORD v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void publish(WORD buttons, DWORD flags, DWORD lease, DWORD pid, BOOL malformed)
{
    BYTE data[72] = {0};
    char temporary[MAX_PATH];
    DWORD written;
    HANDLE file;
    ++sequence;
    memcpy(data, "VXIP0001", 8);
    memcpy(data + 8, token, 32);
    u32(data + 40, sequence); u32(data + 44, sequence >> 32);
    u32(data + 48, lease); data[52] = buttons; data[53] = buttons >> 8;
    u32(data + 64, pid); u32(data + 68, flags);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    CHECK(WriteFile(file, data, malformed ? 17 : 72, &written, NULL));
    CHECK(CloseHandle(file));
    CHECK(MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING));
    Sleep(8);
}

static XINPUT_STATE poll(void)
{
    XINPUT_STATE state;
    memset(&state, 0xcc, sizeof(state));
    CHECK(get_state(0, &state) == ERROR_SUCCESS);
    return state;
}

static BOOL neutral(XINPUT_STATE state)
{
    XINPUT_GAMEPAD zero = {0};
    return !memcmp(&state.Gamepad, &zero, sizeof(zero));
}

static uint64_t ack_sequence(void)
{
    char ack_path[MAX_PATH + 8], data[256], magic[16], received_token[40];
    unsigned long pid;
    unsigned long long seq = ~(uint64_t)0;
    DWORD length = 0;
    HANDLE file;
    snprintf(ack_path, sizeof(ack_path), "%s.ack", path);
    file = CreateFileA(ack_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return seq;
    CHECK(ReadFile(file, data, sizeof(data)-1, &length, NULL));
    CloseHandle(file); data[length] = 0;
    CHECK(sscanf(data, "%15s %39s %lu %llu", magic, received_token, &pid, &seq) == 4);
    return seq;
}

static XINPUT_STATE await_sequence(uint64_t expected)
{
    DWORD started = GetTickCount();
    XINPUT_STATE state;
    do
    {
        state = poll();
        if (ack_sequence() == expected) return state;
        Sleep(4);
    } while (GetTickCount() - started < 250);
    CHECK(!"timed out waiting for the DLL to acknowledge the published sequence");
    return state;
}

static void publish_accepted(WORD buttons, DWORD flags, DWORD lease)
{
    publish(buttons, flags, lease, GetCurrentProcessId(), FALSE);
    await_sequence(sequence);
}

static XINPUT_STATE poll_neutral(void)
{
    DWORD started = GetTickCount();
    XINPUT_STATE state;
    do
    {
        state = poll();
        if (neutral(state)) return state;
        Sleep(4);
    } while (GetTickCount() - started < 250);
    return state;
}

static void arm(void)
{
    publish_accepted(0, 1, 1000);
    CHECK(neutral(poll_neutral()));
}

static void check_ack_record(const char *expected, uint64_t expected_sequence)
{
    char ack_path[MAX_PATH + 8], data[256], magic[16], received_token[40], status[20];
    unsigned long pid, enabled;
    unsigned long long seq, count;
    DWORD length;
    HANDLE file;
    Sleep(110);
    poll();
    snprintf(ack_path, sizeof(ack_path), "%s.ack", path);
    file = CreateFileA(ack_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return;
    CHECK(ReadFile(file, data, sizeof(data)-1, &length, NULL));
    CloseHandle(file); data[length] = 0;
    CHECK(sscanf(data, "%15s %39s %lu %llu %llu %lu %19s", magic, received_token, &pid, &seq, &count, &enabled, status) == 7);
    CHECK(!strcmp(magic, "VXIA0001"));
    CHECK(!strcmp(received_token, token));
    CHECK(pid == GetCurrentProcessId());
    CHECK(seq == expected_sequence);
    CHECK(count > 0);
    CHECK(!strcmp(status, expected));
}

static void check_ack(const char *expected)
{
    check_ack_record(expected, sequence);
}

static unsigned long long diagnostic_number(const char *data, const char *name)
{
    char field[64];
    const char *value;
    unsigned long long number = ~(uint64_t)0;

    snprintf(field, sizeof(field), "\"%s\":", name);
    value = strstr(data, field);
    CHECK(value != NULL);
    if (value) CHECK(sscanf(value + strlen(field), "%llu", &number) == 1);
    return number;
}

static void check_diagnostic(uint64_t accepted, unsigned via)
{
    char diagnostic_path[MAX_PATH + 8], data[2048], binding[64];
    HANDLE file;
    DWORD length = 0;

    if (!diagnostics) return;
    snprintf(diagnostic_path, sizeof(diagnostic_path), "%s.diag", path);
    file = CreateFileA(diagnostic_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return;
    CHECK(GetFileSize(file, NULL) < sizeof(data));
    CHECK(ReadFile(file, data, sizeof(data) - 1, &length, NULL));
    CloseHandle(file);
    data[length] = 0;
    CHECK(length && data[0] == '{' && data[length - 1] == '\n');
    CHECK(diagnostic_number(data, "version") == 1);
    CHECK(diagnostic_number(data, "pid") == GetCurrentProcessId());
    snprintf(binding, sizeof(binding), "\"token\":\"%s\"", token);
    CHECK(strstr(data, binding) != NULL);
    CHECK(strstr(data, "\"module\":\"vibewine_test_xinput1_") != NULL);
    CHECK(diagnostic_number(data, "accepted_sequence") == accepted);
    CHECK(diagnostic_number(data, "accepted_via") == via);
    CHECK(diagnostic_number(data, "state_entries") > 0);
    CHECK(diagnostic_number(data, "state_exits") > 0);
    CHECK(diagnostic_number(data, "io_entries") >= diagnostic_number(data, "io_exits"));
    CHECK(diagnostic_number(data, "snapshot_failures") == 0);
}

int main(int argc, char **argv)
{
    HMODULE module;
    char exe[MAX_PATH], loaded[MAX_PATH], ack_path[MAX_PATH + 8];
    XINPUT_STATE state, again;
    XINPUT_CAPABILITIES caps;
    XINPUT_VIBRATION vibration = {65535, 65535};
    XINPUT_KEYSTROKE key;
    DWORD ret;
    uint64_t saved_sequence;
    BOOL optin, listening;
    DWORD duration = 0, started;
    unsigned polls = 0;
    XINPUT_STATE previous = {0};
    if (argc != 4) return 99;
    listening = !strcmp(argv[1], "listen");
    if (listening)
    {
        duration = strtoul(argv[3], NULL, 10);
        if (!duration || duration > 45000) return 97;
    }
    diagnostics = !strcmp(argv[1], "diagnostics");
    optin = !strcmp(argv[1], "enabled") || diagnostics;
    if (!listening)
    {
        snprintf(path, sizeof(path), "%s", argv[3]);
        snprintf(ack_path, sizeof(ack_path), "%s.ack", path);
        DeleteFileA(ack_path);
        snprintf(ack_path, sizeof(ack_path), "%s.diag", path);
        DeleteFileA(ack_path);
        snprintf(ack_path, sizeof(ack_path), "%s.ack", path);
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_DIAGNOSTICS", diagnostics ? "1" : NULL);
    }
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (listening) { /* Preserve the companion-provided environment. */ }
    else if (strcmp(argv[1], "default"))
    {
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_FILE", path);
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_EXE", optin ? exe : "Z:\\wrong-process.exe");
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_TOKEN", token);
    }
    else
    {
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_FILE", NULL);
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_EXE", NULL);
        SetEnvironmentVariableA("VIBEWINE_XINPUT_TEST_TOKEN", NULL);
    }
    module = LoadLibraryA(argv[2]);
    CHECK(module != NULL);
    if (!module) { printf("LoadLibrary error %lu\n", GetLastError()); return 98; }
    ret = GetModuleFileNameA(module, loaded, sizeof(loaded));
    CHECK(ret && ret < sizeof(loaded));
    CHECK(!_stricmp(loaded, argv[2]));
    printf("loaded=%s\n", loaded);
    get_state = (void *)GetProcAddress(module, "XInputGetState");
    get_state_ex = (void *)GetProcAddress(module, (LPCSTR)100);
    get_caps = (void *)GetProcAddress(module, "XInputGetCapabilities");
    set_state = (void *)GetProcAddress(module, "XInputSetState");
    enable = (void *)GetProcAddress(module, "XInputEnable");
    get_key = (void *)GetProcAddress(module, "XInputGetKeystroke");
    CHECK(get_state && get_caps && set_state && enable && get_key && get_state_ex);
    if (listening)
    {
        started = GetTickCount();
        do
        {
            ret = get_state(0, &state);
            ++polls;
            if (ret != ERROR_SUCCESS) { printf("listen get_state error=%lu\n", ret); return 96; }
            if (memcmp(&previous, &state, sizeof(state)))
            {
                printf("state polls=%u packet=%lu buttons=%u lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n",
                       polls, state.dwPacketNumber, state.Gamepad.wButtons, state.Gamepad.bLeftTrigger,
                       state.Gamepad.bRightTrigger, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                       state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);
                fflush(stdout);
                previous = state;
            }
            Sleep(4);
        } while (GetTickCount() - started < duration);
        printf("listen completed polls=%u duration=%lu pid=%lu\n", polls, duration, GetCurrentProcessId());
        return failures != 0;
    }
    publish(0, 1, 1000, 0, FALSE);
    if (!optin)
    {
        ret = get_state(0, &state);
        CHECK(ret == ERROR_DEVICE_NOT_CONNECTED || ret == ERROR_SUCCESS);
        CHECK(GetFileAttributesA(ack_path) == INVALID_FILE_ATTRIBUTES);
        CHECK(get_state(4, &state) == ERROR_BAD_ARGUMENTS);
        printf("mode=%s ordinary_slot0_result=%lu assertions=%u failures=%u\n", argv[1], ret, assertions, failures);
        return failures != 0;
    }
    CHECK(neutral(poll_neutral()));
    CHECK(get_state(4, &state) == ERROR_BAD_ARGUMENTS);
    CHECK(get_state(0, NULL) == ERROR_BAD_ARGUMENTS);
    CHECK(get_state(1, &state) == ERROR_DEVICE_NOT_CONNECTED);
    CHECK(get_caps(0, 0, NULL) == ERROR_BAD_ARGUMENTS);
    CHECK(get_caps(0, 0, &caps) == ERROR_SUCCESS);
    CHECK(caps.Type == XINPUT_DEVTYPE_GAMEPAD && caps.SubType == XINPUT_DEVSUBTYPE_GAMEPAD);
    CHECK(!(caps.Flags & XINPUT_CAPS_FFB_SUPPORTED));
    CHECK(set_state(0, &vibration) == ERROR_SUCCESS);
    CHECK(set_state(1, &vibration) == ERROR_DEVICE_NOT_CONNECTED);
    CHECK(set_state(0, NULL) == ERROR_BAD_ARGUMENTS);
    check_ack("READY");
    publish_accepted(XINPUT_GAMEPAD_A, 0, 1000);
    state = poll(); CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_A);
    CHECK(get_state_ex(0, &again) == ERROR_SUCCESS && !memcmp(&state, &again, sizeof(state)));
    CHECK(get_key(XUSER_INDEX_ANY, 0, &key) == ERROR_SUCCESS && key.VirtualKey == VK_PAD_A && key.Flags == XINPUT_KEYSTROKE_KEYDOWN);
    again = poll(); CHECK(state.dwPacketNumber == again.dwPacketNumber);
    check_ack("STATE");
    publish_accepted(XINPUT_GAMEPAD_A | 0x0400, 0, 1000);
    CHECK(poll().Gamepad.wButtons == XINPUT_GAMEPAD_A);
    CHECK(get_state_ex(0, &again) == ERROR_SUCCESS && again.Gamepad.wButtons == (XINPUT_GAMEPAD_A | 0x0400));
    publish_accepted(0, 2, 1000);
    again = poll(); CHECK(neutral(again) && again.dwPacketNumber != state.dwPacketNumber);
    CHECK(get_key(0, 0, &key) == ERROR_SUCCESS && key.Flags == XINPUT_KEYSTROKE_KEYUP);
    check_ack("RELEASE");
    arm();
    publish_accepted(XINPUT_GAMEPAD_B, 0, 60);
    CHECK(poll().Gamepad.wButtons == XINPUT_GAMEPAD_B);
    Sleep(90); CHECK(neutral(poll_neutral()));
    CHECK(neutral(poll_neutral())); /* an unchanged record never renews the lease */
    publish(XINPUT_GAMEPAD_A, 0, 1000, GetCurrentProcessId(), FALSE);
    CHECK(neutral(poll_neutral())); /* expiration requires a new neutral ARM */
    check_ack_record("INVALID", sequence - 1);
    arm();
    publish_accepted(XINPUT_GAMEPAD_A, 0, 1000);
    CHECK(poll().Gamepad.wButtons == XINPUT_GAMEPAD_A);
    CHECK(get_key(0, 0, &key) == ERROR_SUCCESS && key.Flags == XINPUT_KEYSTROKE_KEYDOWN);
    enable(FALSE); CHECK(neutral(poll_neutral()));
    CHECK(get_key(0, 0, &key) == ERROR_SUCCESS && key.Flags == XINPUT_KEYSTROKE_KEYUP);
    publish(XINPUT_GAMEPAD_B, 0, 1000, GetCurrentProcessId(), FALSE);
    CHECK(neutral(poll_neutral()));
    check_ack_record("DISABLED", sequence - 1);
    enable(TRUE); CHECK(neutral(poll_neutral()));
    check_ack_record("INVALID", sequence - 1);
    arm();
    /* A keystroke-consumed ARM followed by enable transitions cannot ACK READY. */
    publish(0, 1, 1000, GetCurrentProcessId(), FALSE);
    /* Advance the actual reader clock without a GetState delivery poll. */
    started = GetTickCount();
    for (ret = 0; ret < 64 && GetTickCount() - started < 4; ++ret) Sleep(4);
    CHECK(GetTickCount() - started >= 4);
    get_key(0, 0, &key);
    CHECK(ack_sequence() == sequence - 1); /* GetKeystroke cannot ACK delivery */
    if (diagnostics)
    {
        Sleep(110);
        get_key(0, 0, &key);
        CHECK(ack_sequence() == sequence - 1);
        check_diagnostic(sequence, 2); /* independently identifies key acceptance */
    }
    enable(FALSE); enable(TRUE);
    CHECK(neutral(poll_neutral()));
    check_ack("INVALID");
    arm();
    enable(TRUE); /* repeated true must not disarm */
    publish_accepted(XINPUT_GAMEPAD_A, 0, 1000);
    CHECK(poll().Gamepad.wButtons == XINPUT_GAMEPAD_A);
    publish(XINPUT_GAMEPAD_B, 0, 1000, GetCurrentProcessId(), TRUE);
    CHECK(neutral(poll_neutral()));
    check_ack_record("INVALID", sequence - 1);
    if (diagnostics) check_diagnostic(sequence - 1, 1);
    arm();
    saved_sequence = sequence;
    sequence += 10000;
    publish(XINPUT_GAMEPAD_B, 0, 1000, GetCurrentProcessId()+4, FALSE);
    CHECK(neutral(poll_neutral()));
    check_ack_record("INVALID", saved_sequence);
    sequence = saved_sequence; /* invalid high sequence must not block recovery */
    arm();
    publish_accepted(XINPUT_GAMEPAD_A, 0, 1000);
    CHECK(poll().Gamepad.wButtons == XINPUT_GAMEPAD_A);
    saved_sequence = sequence;
    sequence -= 2;
    publish(XINPUT_GAMEPAD_B, 0, 1000, GetCurrentProcessId(), FALSE);
    CHECK(neutral(poll_neutral())); /* older valid record is a replay */
    check_ack_record("INVALID", saved_sequence);
    sequence = saved_sequence;
    arm();
    token[0] = 'f';
    publish(XINPUT_GAMEPAD_B, 0, 1000, GetCurrentProcessId(), FALSE);
    token[0] = '0';
    CHECK(neutral(poll_neutral()));
    check_ack_record("INVALID", sequence - 1);
    arm();
    publish_accepted(XINPUT_GAMEPAD_A, 0, 1000);
    CHECK(poll().Gamepad.wButtons == XINPUT_GAMEPAD_A);
    DeleteFileA(path); Sleep(8); CHECK(neutral(poll_neutral()));
    arm();
    publish(XINPUT_GAMEPAD_A, 0, 1001, GetCurrentProcessId(), FALSE);
    CHECK(neutral(poll_neutral()));
    check_ack_record("INVALID", sequence - 1);
    printf("mode=%s assertions=%u failures=%u\n", argv[1], assertions, failures);
    return failures != 0;
}

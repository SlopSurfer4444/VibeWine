/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Integration-owner-only Win32 fixture. Native UI/cursor effects when launched.
 * No process injection, game input, telemetry file or persistent configuration. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define ID_ARM 101
#define ID_RELEASE 102
#define ID_QUIT 103
#define ID_WARP 104
static BOOL armed, active, request_clipping;
static unsigned attempts, accepted, rejected, clipped;
static DWORD started, last_report;

static void release_cursor(void)
{
    armed = FALSE;
    ClipCursor(NULL);
}

static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CREATE:
        CreateWindowA("BUTTON", "Arm clip + repeat warps", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                      15, 45, 215, 35, window, (HMENU)(INT_PTR)ID_ARM, NULL, NULL);
        CreateWindowA("BUTTON", "Arm warps only", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                      240, 45, 140, 35, window, (HMENU)(INT_PTR)ID_WARP, NULL, NULL);
        CreateWindowA("BUTTON", "Release", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                      390, 45, 90, 35, window, (HMENU)(INT_PTR)ID_RELEASE, NULL, NULL);
        CreateWindowA("BUTTON", "Normal Quit", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                      490, 45, 110, 35, window, (HMENU)(INT_PTR)ID_QUIT, NULL, NULL);
        started = last_report = GetTickCount();
        if (!SetTimer(window, 1, 20, NULL))
        {
            fprintf(stderr, "T8 ERROR timer creation failed; refusing live fixture\n");
            return -1;
        }
        printf("T8 START pid=%lu limit_ms=90000 policy=%s\n", GetCurrentProcessId(),
               getenv("VIBEWINE_FOREGROUND_CURSOR_WARP") ? getenv("VIBEWINE_FOREGROUND_CURSOR_WARP") : "unset");
        fflush(stdout);
        return 0;
    case WM_ACTIVATEAPP:
        active = (BOOL)wparam;
        printf("T8 ACTIVATE elapsed=%lu active=%d armed=%d\n", GetTickCount() - started, active, armed);
        fflush(stdout);
        /* Deliberately keep requests armed: driver must reject background warps. */
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_ARM || LOWORD(wparam) == ID_WARP)
        {
            release_cursor();
            request_clipping = LOWORD(wparam) == ID_ARM;
            armed = TRUE;
            printf("T8 ARMED elapsed=%lu clip=%d\n", GetTickCount() - started, request_clipping);
            fflush(stdout);
        }
        else if (LOWORD(wparam) == ID_RELEASE) release_cursor();
        else if (LOWORD(wparam) == ID_QUIT) SendMessageA(window, WM_CLOSE, 0, 0);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) release_cursor();
        return 0;
    case WM_TIMER:
    {
        DWORD now = GetTickCount();
        if (now - started >= 90000) { SendMessageA(window, WM_CLOSE, 0, 0); return 0; }
        if (armed)
        {
            RECT client;
            POINT origin, centre;
            GetClientRect(window, &client);
            origin.x = client.left; origin.y = client.top;
            ClientToScreen(window, &origin);
            centre.x = origin.x + (client.right - client.left) / 2;
            centre.y = origin.y + (client.bottom - client.top) / 2;
            /* Small known rectangle, independent of window size and DPI. */
            client.left = centre.x - 45; client.right = centre.x + 45;
            client.top = centre.y - 45; client.bottom = centre.y + 45;
            if (request_clipping && ClipCursor(&client)) clipped++;
            /* Alternate target so the server cannot elide an unchanged request. */
            centre.x += (attempts & 1) ? 15 : -15;
            attempts++;
            if (SetCursorPos(centre.x, centre.y)) accepted++; else rejected++;
        }
        if (now - last_report >= 1000)
        {
            char title[256];
            last_report = now;
            snprintf(title, sizeof(title), "VibeWine T8 Owned Cursor Fixture | armed=%d active=%d calls=%u api_ok=%u api_fail=%u", armed, active, attempts, accepted, rejected);
            SetWindowTextA(window, title);
            printf("T8 TICK elapsed=%lu armed=%d active=%d calls=%u api_ok=%u api_fail=%u clip_api_ok=%u\n", now - started, armed, active, attempts, accepted, rejected, clipped);
            fflush(stdout);
        }
        return 0;
    }
    case WM_CLOSE:
        release_cursor();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        release_cursor();
        printf("T8 NORMAL_EXIT elapsed=%lu calls=%u api_ok=%u api_fail=%u\n", GetTickCount() - started, attempts, accepted, rejected);
        fflush(stdout);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int main(void)
{
    WNDCLASSA type = {0};
    HWND window;
    MSG message;
    type.lpfnWndProc = procedure;
    type.hInstance = GetModuleHandleA(NULL);
    type.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    type.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    type.lpszClassName = "VibeWineT8OwnedCursorFixture";
    if (!RegisterClassA(&type)) return 2;
    window = CreateWindowA(type.lpszClassName, "VibeWine T8 Owned Cursor Fixture", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 640, 180, NULL, NULL, type.hInstance, NULL);
    if (!window) return 3;
    ShowWindow(window, SW_SHOW);
    for (;;)
    {
        int result = GetMessageA(&message, NULL, 0, 0);
        if (result <= 0) return result < 0 ? 4 : (int)message.wParam;
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE)
        {
            release_cursor();
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

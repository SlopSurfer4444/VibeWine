// SPDX-License-Identifier: MIT
// Full production thunk + real NTDLL phase; bridge is observable, not native.
#include <windows.h>
#include <stdint.h>
#include "wineunixlib.h"

extern void NSObject_release(uint64_t object);
static const uint64_t expected = 0x123456789abcdef0;
static unsigned int native_calls;

static void mark(char value) {
  DWORD written;
  if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &value, 1, &written, NULL) || written != 1)
    TerminateProcess(GetCurrentProcess(), 91);
}
NTSTATUS fixture_unix_call(unsigned int opcode, void *args) {
  if (opcode != 1 || *(uint64_t *)args != expected)
    TerminateProcess(GetCurrentProcess(), 92);
  ++native_calls;
  mark('B');
  return 0;
}
__declspec(dllexport) void run_release(void) {
  if (RtlDllShutdownInProgress()) TerminateProcess(GetCurrentProcess(), 93);
  unsigned int before = native_calls;
  mark('R');
  NSObject_release(expected);
  if (native_calls != before + 1) TerminateProcess(GetCurrentProcess(), 94);
  mark('S');
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) {
  if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
  if (reason == DLL_PROCESS_DETACH) {
    if (!!RtlDllShutdownInProgress() != !!reserved)
      TerminateProcess(GetCurrentProcess(), 95);
    unsigned int before = native_calls;
    mark(reserved ? 'P' : 'U');
    NSObject_release(expected);
    if (native_calls != before + (reserved ? 0 : 1))
      TerminateProcess(GetCurrentProcess(), 96);
    mark('S');
  }
  return TRUE;
}

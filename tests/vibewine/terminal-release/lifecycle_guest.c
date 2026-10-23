// SPDX-License-Identifier: MIT
// Root owner executes only in its private prefix, with host deadline/identity.
#include <windows.h>
#include <string.h>

static DWORD WINAPI waiting_worker(void *event) {
  SetEvent(event);
  Sleep(INFINITE);
  return 0;
}
int main(int argc, char **argv) {
  if (argc != 3 || (strcmp(argv[1], "unload") && strcmp(argv[1], "terminal"))) return 64;
  for (unsigned int i = 0; i != 2; ++i) {
    HMODULE module = LoadLibraryA(argv[2]);
    if (!module) return 101;
    void (*release)(void) = (void (*)(void))GetProcAddress(module, "run_release");
    if (!release) return 102;
    release();
    if (!strcmp(argv[1], "terminal")) {
      HANDLE entered = CreateEventW(NULL, TRUE, FALSE, NULL);
      if (!entered) return 103;
      HANDLE worker = CreateThread(NULL, 0, waiting_worker, entered, 0, NULL);
      if (!worker) return 104;
      if (WaitForSingleObject(entered, 5000) != WAIT_OBJECT_0) return 105;
      ExitProcess(20);
    }
    if (!FreeLibrary(module)) return 106;
  }
  return 20;
}

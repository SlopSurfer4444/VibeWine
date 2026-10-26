#include "log/log.hpp"
#include "util_env.hpp"
#include "dxmt_idle_diagnostics.hpp"
#include <cstdio>
#include <cstring>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace dxmt { Logger Logger::s_instance("idle-fixture.log"); }

struct Evidence { unsigned records = 0, old_failed = 0; bool enabled = false; } evidence;

int runFixture(char *command) {
  using namespace dxmt;
  const bool usable = std::strstr(command, "usable");
  const bool old = std::strstr(command, "old");
  if (usable) {
    int fd = _open("windows-stderr.txt", _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY,
                   _S_IREAD | _S_IWRITE);
    if (fd < 0 || _dup2(fd, 2)) return 20;
    if (fd != 2) _close(fd);
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(2)));
  } else {
    _close(2);
    SetStdHandle(STD_ERROR_HANDLE, INVALID_HANDLE_VALUE);
  }
  if (!usable && GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE) return 21;
  bool enabled = env::getEnvVar("DXMT_IDLE_ATTRIBUTION") == "1";
  evidence.enabled = enabled;
  IdleRetirementDiagnostics diagnostics(enabled);
  IdleRetirementDomain::Sample sample{};
  sample.registered_resources = 19;
  sample.scanned_resources = 3;
  sample.completed_idle = {8, 99, 1};
  sample.unclassified = {7, 0, 7};
  sample.released_count = 5;
  sample.released_bytes = 512;
  unsigned records = 0;
  for (unsigned i = 0; i != 100; ++i) {
    char line[1024]{};
    if (!diagnostics.record(1 + 10000000000ull * i, sample,
                            GetCurrentProcessId(), 2, line)) continue;
    ++records; ++evidence.records;
    if (old) {
      int result = std::fputs(line, stderr);
      if (result < 0) ++evidence.old_failed;
      if (usable ? (result < 0) : (result >= 0)) return 22;
    } else if (!Logger::diagnostic(line)) return 23;
  }
  if (records != (enabled ? 64u : 0u)) return 24;
  // Ordinary records retain the minimum-level policy even after diagnostic init.
  Logger::info("NORMAL_INFO_SENTINEL");
  Logger::err("NORMAL_ERROR_SENTINEL");
  if (Logger::diagnostic(nullptr) || Logger::diagnostic("") ||
      Logger::diagnostic("\n") || Logger::diagnostic("one\ntwo") ||
      Logger::diagnostic("one\rtwo")) return 25;
  char overlong[1025]; std::memset(overlong, 'x', sizeof(overlong));
  overlong[1024] = 0;
  if (Logger::diagnostic(overlong)) return 26;
  overlong[1012] = 0;
  if (Logger::diagnostic(overlong)) return 27;
  return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR command, int) {
  int result = runFixture(command);
  char path[4096]{};
  DWORD size = GetEnvironmentVariableA("DXMT_FIXTURE_RECEIPT", path, sizeof(path));
  if (!size || size >= sizeof(path)) return 30;
  HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return 31;
  char receipt[256]{};
  int length = std::snprintf(receipt, sizeof(receipt),
    "code=%d pid=%lu enabled=%u records=%u old_failed=%u stderr_invalid=%u\n",
    result, GetCurrentProcessId(), evidence.enabled, evidence.records, evidence.old_failed,
    GetStdHandle(STD_ERROR_HANDLE) == INVALID_HANDLE_VALUE);
  DWORD written = 0;
  bool saved = WriteFile(file, receipt, length, &written, nullptr) && written == DWORD(length);
  CloseHandle(file);
  return saved ? result : 32;
}

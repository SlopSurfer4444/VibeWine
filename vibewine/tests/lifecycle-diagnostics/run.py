#!/usr/bin/env python3
"""Compile current diagnostic code with native scheduling and NT API seams.

No Wine processes, target process memory, runtime installation or persistent logs.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def function(source, name):
    source = source.replace("GPT_IMPORT(" + name + ")", name)
    match = re.search(r"(?m)^(?:static[ \t]+)?(?:inline[ \t]+)?[A-Za-z_][\w \t*]*\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source)
    assert match, name
    start = source.index("{", match.start())
    depth = 1
    pos = start + 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]


COMMON = r'''
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t LONG, NTSTATUS;
typedef uint32_t DWORD, ULONG;
typedef int BOOL;
typedef void *HANDLE, *PVOID;
typedef void *(*PRTL_THREAD_START_ROUTINE)(void *);
#define TRUE 1
#define FALSE 0
#define WINAPI
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_PENDING ((NTSTATUS)0x103)
#define STATUS_INVALID_CID ((NTSTATUS)0xc000000b)
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xc0000022)
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#define ThreadBasicInformation 0
#define GetCurrentProcessId() ((DWORD)0x20)
#define GetCurrentThreadId() ((DWORD)0x24)
#define ULongToHandle(value) ((HANDLE)(uintptr_t)(uint32_t)(value))
#define TRACE_ON(channel) lifecycle_enabled
#define TRACE_(channel) trace_emit
static int lifecycle_enabled;
static _Atomic unsigned records, markers, admitted;
static int emit_payload;
static void trace_emit(const char *format, ...) {
    va_list args;
    atomic_fetch_add(&records, 1);
    if (strstr(format, "event=quota ")) atomic_fetch_add(&markers, 1);
    if (!emit_payload) return;
    va_start(args, format); vprintf(format, args); va_end(args);
}
static LONG InterlockedCompareExchange(LONG *ptr, LONG value, LONG expected) {
    __atomic_compare_exchange_n(ptr, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
'''

QUOTA_TEST = r'''
static void *race(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 10000; ++i)
        if (lifecycle_trace_admit()) atomic_fetch_add(&admitted, 1);
    return NULL;
}
static void test_quota(void) {
    pthread_t threads[8];
    lifecycle_enabled = 0;
    for (unsigned i = 0; i < 10000; ++i) assert(!lifecycle_trace_admit());
    assert(lifecycle_event_count == 0 && records == 0 && markers == 0);
    lifecycle_enabled = 1;
    for (unsigned i = 0; i < 8; ++i) assert(!pthread_create(&threads[i], NULL, race, NULL));
    for (unsigned i = 0; i < 8; ++i) assert(!pthread_join(threads[i], NULL));
    assert(lifecycle_event_count == LIFECYCLE_EVENT_LIMIT);
    assert(admitted == LIFECYCLE_EVENT_LIMIT - 1 && records == 1 && markers == 1);
    for (unsigned i = 0; i < 10000; ++i) assert(!lifecycle_trace_admit());
    assert(lifecycle_event_count == LIFECYCLE_EVENT_LIMIT && records == 1 && markers == 1);
    lifecycle_enabled = 0;
    assert(!lifecycle_trace_admit() && lifecycle_event_count == LIFECYCLE_EVENT_LIMIT);
    lifecycle_event_count = 0; records = 0; markers = 0; admitted = 0;
}
'''

CRITICAL_SEAMS = r'''
typedef struct {HANDLE UniqueProcess, UniqueThread;} CLIENT_ID;
typedef struct {NTSTATUS ExitStatus; PVOID TebBaseAddress; CLIENT_ID ClientId;} THREAD_BASIC_INFORMATION;
typedef struct {unsigned Length;} OBJECT_ATTRIBUTES;
typedef struct {void *DebugInfo; LONG LockCount, RecursionCount; HANDLE OwningThread, LockSemaphore;} RTL_CRITICAL_SECTION;
#define InitializeObjectAttributes(attr, name, flags, root, sd) ((attr)->Length = sizeof(*(attr)))
static void *ReadPointerAcquire(void *volatile *ptr) { return __atomic_load_n(ptr, __ATOMIC_ACQUIRE); }
static RTL_CRITICAL_SECTION section;
static unsigned opens, queries, closes;
static NTSTATUS wanted_open, wanted_query;
static int change_owner, wrong_pid;
static NTSTATUS NtOpenThread(HANDLE *thread, ULONG access, OBJECT_ATTRIBUTES *attr, CLIENT_ID *id) {
    assert(access == THREAD_QUERY_LIMITED_INFORMATION && attr->Length == sizeof(*attr));
    assert(id->UniqueProcess == ULongToHandle(0x20) && id->UniqueThread == ULongToHandle(0x1f4));
    ++opens;
    if (wanted_open) return wanted_open;
    *thread = ULongToHandle(0x99); return 0;
}
static NTSTATUS NtQueryInformationThread(HANDLE thread, int cls, void *buffer, ULONG size, void *length) {
    THREAD_BASIC_INFORMATION *basic = buffer;
    assert(thread == ULongToHandle(0x99) && cls == ThreadBasicInformation && size == sizeof(*basic) && !length);
    ++queries;
    if (change_owner) section.OwningThread = ULongToHandle(0x30);
    if (wanted_query) return wanted_query;
    basic->ClientId.UniqueProcess = ULongToHandle(wrong_pid ? 0x40 : 0x20);
    basic->ClientId.UniqueThread = ULongToHandle(0x1f4);
    basic->ExitStatus = STATUS_PENDING; return 0;
}
static NTSTATUS NtClose(HANDLE thread) {assert(thread == ULongToHandle(0x99)); ++closes; return 0;}
'''

CRITICAL_TEST = r'''
static void prepare(void) {
    section.OwningThread = ULongToHandle(0x1f4);
    section.LockCount = 2; section.RecursionCount = 2;
    wanted_open = wanted_query = 0; opens = queries = closes = 0;
    change_owner = wrong_pid = 0;
}
int main(void) {
    test_quota(); prepare();
    lifecycle_trace_critical_timeout(&section);
    assert(!opens && !queries && !closes && lifecycle_event_count == 0);
    lifecycle_enabled = emit_payload = 1;
    lifecycle_trace_critical_timeout(&section);
    assert(opens == 1 && queries == 1 && closes == 1);
    prepare(); wanted_open = STATUS_INVALID_CID;
    lifecycle_trace_critical_timeout(&section);
    assert(opens == 1 && queries == 0 && closes == 0);
    prepare(); wanted_query = STATUS_ACCESS_DENIED;
    lifecycle_trace_critical_timeout(&section);
    assert(opens == 1 && queries == 1 && closes == 1);
    prepare(); change_owner = 1;
    lifecycle_trace_critical_timeout(&section);
    assert(opens == 1 && queries == 1 && closes == 1);
    prepare(); wrong_pid = 1;
    lifecycle_trace_critical_timeout(&section);
    assert(opens == 1 && queries == 1 && closes == 1);
    prepare(); section.OwningThread = NULL;
    lifecycle_trace_critical_timeout(&section);
    assert(!opens && !queries && !closes);
    lifecycle_event_count = LIFECYCLE_EVENT_LIMIT;
    prepare(); lifecycle_trace_critical_timeout(&section);
    assert(!opens && !queries && !closes);
    return 0;
}
'''

UNIX_SEAMS = r'''
typedef struct {HANDLE UniqueProcess, UniqueThread;} CLIENT_ID;
typedef struct {NTSTATUS ExitStatus; PVOID TebBaseAddress; CLIENT_ID ClientId;} THREAD_BASIC_INFORMATION;
#define ThreadBasicInformation 0
#define NtCurrentThread() ((HANDLE)(intptr_t)-2)
static unsigned target_queries;
static NTSTATUS target_query_status;
static NTSTATUS NtQueryInformationThread(HANDLE thread, int cls, void *buffer, ULONG size, void *length) {
    THREAD_BASIC_INFORMATION *basic = buffer;
    assert(thread == ULongToHandle(0xabc) && cls == ThreadBasicInformation && size == sizeof(*basic) && !length);
    ++target_queries;
    basic->ClientId.UniqueProcess = ULongToHandle(0x20);
    basic->ClientId.UniqueThread = ULongToHandle(0x1f4);
    basic->ExitStatus = STATUS_PENDING;
    return target_query_status;
}
'''

UNIX_TEST = r'''
int main(void) {
    test_quota();
    lifecycle_trace_created(0x20, 0x1f4, (PRTL_THREAD_START_ROUTINE)(uintptr_t)0x1234,
                            ULongToHandle(0xabc), 2, 0);
    lifecycle_trace_terminate_begin(ULongToHandle(0xabc), 7);
    lifecycle_trace_terminate_result(ULongToHandle(0xabc), 7, 0, 0);
    assert(lifecycle_event_count == 0 && records == 0 && target_queries == 0);
    lifecycle_enabled = emit_payload = 1;
    lifecycle_trace_created(0x20, 0x1f4, (PRTL_THREAD_START_ROUTINE)(uintptr_t)0x1234,
                            ULongToHandle(0xabc), 2, 0);
    lifecycle_trace_created(0x40, 0x44, (PRTL_THREAD_START_ROUTINE)(uintptr_t)0x5678,
                            ULongToHandle(0xdef), 1, 1);
    lifecycle_trace_terminate_begin(ULongToHandle(0xabc), 7);
    assert(target_queries == 1);
    target_query_status = STATUS_ACCESS_DENIED;
    lifecycle_trace_terminate_begin(ULongToHandle(0xabc), 7);
    target_query_status = STATUS_INVALID_CID;
    lifecycle_trace_terminate_begin(ULongToHandle(0xabc), 7);
    assert(target_queries == 3);
    lifecycle_trace_terminate_begin(NtCurrentThread(), 0);
    assert(target_queries == 3);
    lifecycle_trace_terminate_result(ULongToHandle(0xabc), 7, STATUS_ACCESS_DENIED, 0);
    lifecycle_trace_terminate_result(NtCurrentThread(), 0, 0, 1);
    assert(lifecycle_event_count == 8 && records == 8);
    lifecycle_event_count = LIFECYCLE_EVENT_LIMIT;
    lifecycle_trace_terminate_begin(ULongToHandle(0xabc), 7);
    assert(target_queries == 3 && records == 8);
    return 0;
}
'''

EXIT_TEST = r'''
int main(void) {
    test_quota(); lifecycle_trace_exit_user(0xc0000005u);
    assert(lifecycle_event_count == 0 && records == 0);
    lifecycle_enabled = emit_payload = 1;
    lifecycle_trace_exit_user(0xc0000005u);
    assert(lifecycle_event_count == 1 && records == 1);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="/usr/bin/clang")
    parser.add_argument("--arch", choices=("arm64", "x86_64"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    files = {key: (ROOT / path).read_text() for key, path in {
        "exit": "dlls/ntdll/thread.c", "unix": "dlls/ntdll/unix/thread.c",
        "critical": "dlls/ntdll/sync.c", "macquota": "dlls/winemac.drv/dllmain.c"}.items()}
    # Bind the exercised emitters to their real control-flow sites.
    assert "lifecycle_trace_exit_user( status );" in function(files["exit"], "RtlExitUserThread")
    create = function(files["unix"], "NtCreateThreadEx")
    assert create.count("if (!status) lifecycle_trace_created(") == 2
    assert "if (attr_list) status = update_attr_list( attr_list, *handle, &client_id, teb );\n            if (!status) lifecycle_trace_created(" in create
    assert "if (attr_list) status = update_attr_list( attr_list, *handle, &teb->ClientId, teb );\n    if (!status) lifecycle_trace_created(" in create
    terminate = function(files["unix"], "NtTerminateThread")
    assert terminate.index("lifecycle_trace_terminate_begin") < terminate.index("SERVER_START_REQ")
    assert terminate.index("lifecycle_trace_terminate_result") < terminate.index("if (self)")
    for name in ("abort_thread", "exit_thread"):
        assert "lifecycle_trace" not in function(files["unix"], name)
    wait = function(files["critical"], "RtlpWaitForCriticalSection")
    assert wait.index("if (status != WAIT_TIMEOUT) return status;") < wait.index("lifecycle_trace_critical_timeout( crit );")
    outputs = {}
    with tempfile.TemporaryDirectory(prefix="wine-lifecycle-diagnostics-") as temporary:
        temp = Path(temporary)
        for name, source in files.items():
            prefix = source[source.index("#define LIFECYCLE_EVENT_LIMIT"):source.index("static BOOL lifecycle_trace_admit")]
            body = COMMON + prefix + function(source, "lifecycle_trace_admit") + QUOTA_TEST
            if name == "exit":
                body += function(source, "lifecycle_trace_exit_user") + EXIT_TEST
            elif name == "unix":
                body += UNIX_SEAMS
                for emitter in ("lifecycle_trace_created", "lifecycle_trace_terminate_begin", "lifecycle_trace_terminate_result"):
                    body += function(source, emitter)
                body += UNIX_TEST
            elif name == "critical":
                body += CRITICAL_SEAMS + function(source, "lifecycle_trace_critical_timeout") + CRITICAL_TEST
            else:
                body += "int main(void) { test_quota(); return 0; }"
            c, exe = temp / (name + ".c"), temp / name
            c.write_text(body)
            command = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread"]
            if args.arch:
                command += ["-arch", args.arch]
            if args.sanitize:
                command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            built = subprocess.run(command + [str(c), "-o", str(exe)], capture_output=True, text=True, timeout=60)
            assert built.returncode == 0 and not built.stdout and not built.stderr, built.stderr
            assert sum(path.stat().st_size for path in temp.rglob("*") if path.is_file()) < 64 * 1024 * 1024
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            assert ran.returncode == 0 and not ran.stderr, ran.stdout + ran.stderr
            assert len(ran.stdout) < 16384
            outputs[name] = ran.stdout.splitlines()
    assert outputs["macquota"] == []
    assert outputs["exit"] == ["event=exit_user_begin pid=00000020 tid=00000024 status=c0000005"]
    unix = outputs["unix"]
    assert len(unix) == 8
    assert unix[0] == "event=thread_created caller_pid=00000020 caller_tid=00000024 target_pid=00000020 target_tid=000001f4 start=0x1234 handle=0xabc flags=00000002 remote=0"
    assert "target_pid=00000040 target_tid=00000044 start=0x5678 handle=0xdef flags=00000001 remote=1" in unix[1]
    assert "handle=0xabc exit_status=00000007" in unix[2]
    assert "target_query_attempted=1 target_query_status=00000000 target_known=1 target_pid=0x20 target_tid=0x1f4 target_exit_status=00000103" in unix[2]
    assert "target_query_status=c0000022 target_known=0" in unix[3]
    assert "target_query_status=c000000b target_known=0" in unix[4]
    for line in unix[3:5]:
        assert "target_pid=0x0 target_tid=0x0" in line or "target_pid=(nil) target_tid=(nil)" in line
    assert "target_query_attempted=0 target_query_status=00000000 target_known=1 target_pid=0x20 target_tid=0x24" in unix[5]
    assert "result=c0000022 self=0" in unix[6]
    assert "handle=0xfffffffffffffffe exit_status=00000000 result=00000000 self=1" in unix[7]
    critical = outputs["critical"]
    assert len(critical) == 6
    for line in critical:
        assert line.startswith("event=critical_timeout pid=00000020 tid=00000024 section=")
        assert "lock_count=2 recursion=2" in line
    assert "owner_stable=1" in critical[0] and "owner_identity_matches=1 owner_exit_status=00000103" in critical[0]
    assert "open_status=c000000b query_attempted=0" in critical[1]
    assert "query_status=c0000022" in critical[2] and "owner_identity_matches=0" in critical[2]
    assert "owner_after=0x30 owner_stable=0" in critical[3]
    assert "queried_pid=0x40" in critical[4] and "owner_identity_matches=0" in critical[4]
    assert "open_attempted=0" in critical[5] and "query_attempted=0" in critical[5]
    print("PASS: four exact admission functions; concurrent caps, one marker, disabled paths; thread fields and six owner-query schedules")
    quit_command = ["python3", str(HERE / "test_quit.py"), "--cc", args.cc]
    if args.arch:
        quit_command += ["--arch", args.arch]
    if args.sanitize:
        quit_command += ["--sanitize"]
    subprocess.run(quit_command, check=True, timeout=120)


if __name__ == "__main__":
    main()

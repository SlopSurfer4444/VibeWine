/* SPDX-License-Identifier: MIT
 * Native fixture seam only. Production includes Wine's real declarations. */
#ifndef TERMINAL_RELEASE_FIXTURE_UNIXLIB_H
#define TERMINAL_RELEASE_FIXTURE_UNIXLIB_H
#ifdef _WIN32
#include <winternl.h>
NTSYSAPI BOOLEAN NTAPI RtlDllShutdownInProgress(void);
#else
typedef int NTSTATUS;
unsigned char RtlDllShutdownInProgress(void);
#endif
NTSTATUS fixture_unix_call(unsigned int opcode, void *args);
#define WINE_UNIX_CALL(opcode, args) fixture_unix_call(opcode, args)
#endif

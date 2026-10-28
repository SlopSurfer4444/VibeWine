/* SPDX-License-Identifier: MIT
 * Private Unix bridge; does not change the PE/unixcall or airconv public ABI. */
#pragma once
#include "airconv_public.h"
#ifdef __cplusplus
extern "C" {
#endif
int WMTWorkerInitialize(const void*, size_t, sm50_shader_t*, struct MTL_SHADER_REFLECTION*, sm50_error_t*);
void WMTWorkerDestroy(sm50_shader_t);
int WMTWorkerCompile(sm50_shader_t, struct SM50_SHADER_COMPILATION_ARGUMENT_DATA*, const char*, sm50_bitcode_t*, sm50_error_t*);
void WMTWorkerGetBitcode(sm50_bitcode_t, struct SM50_COMPILED_BITCODE*);
void WMTWorkerDestroyBitcode(sm50_bitcode_t);
size_t WMTWorkerGetError(sm50_error_t, char*, size_t);
void WMTWorkerFreeError(sm50_error_t);
/* Cancels only this bridge's active request, kills/reaps its child, then closes
 * admission permanently. Existing task owner must call this before unloading. */
void WMTWorkerShutdown(void);
#ifdef __cplusplus
}
#endif

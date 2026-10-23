// SPDX-License-Identifier: LGPL-2.1-or-later
// CPU-only public-API ownership regression. No Metal, runtime, or game launch.
#undef NDEBUG
#include "dxbc_converter.hpp"
#include "DXBCParser/BlobContainer.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
using namespace microsoft;
static long outstanding = 0;
void *operator new(size_t n) { auto *p = std::malloc(n ? n : 1); if (!p) std::abort(); ++outstanding; return p; }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { if (p) { --outstanding; std::free(p); } }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, size_t) noexcept { ::operator delete(p); }
static uint32_t op(uint32_t code, uint32_t size = 1) { return code | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(size); }
static std::vector<uint32_t> bytecode(bool hull = false, int signatures = 2, bool shader_blob = true) {
  std::vector<uint32_t> code{ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(hull ? D3D11_SB_HULL_SHADER : D3D11_SB_COMPUTE_SHADER, 5, 0), 0};
  if (hull) {
    code.insert(code.end(), {op(D3D11_SB_OPCODE_HS_FORK_PHASE), op(D3D11_SB_OPCODE_DCL_HS_FORK_PHASE_INSTANCE_COUNT, 2), 64});
  } else {
    code.insert(code.end(), {op(D3D11_SB_OPCODE_DCL_THREAD_GROUP, 4), 1, 1, 1});
  }
  code.push_back(op(D3D10_SB_OPCODE_RET)); code[1] = code.size();
  const unsigned count = signatures + shader_blob;
  std::vector<uint32_t> data(8 + count);
  data[0] = DXBC_FOURCC_NAME; data[5] = 1; data[7] = count;
  unsigned index = 8;
  auto blob = [&](uint32_t tag, const std::vector<uint32_t> &payload) {
    data[index++] = data.size() * 4; data.push_back(tag); data.push_back(payload.size() * 4);
    data.insert(data.end(), payload.begin(), payload.end());
  };
  if (shader_blob) blob(DXBC_GenericShaderEx, code);
  if (signatures > 0) blob(DXBC_InputSignature, {0, 8});
  if (signatures > 1) blob(DXBC_OutputSignature, {0, 8});
  data[6] = data.size() * 4; return data;
}
static void check_error(sm50_error_t error, const char *expected) {
  assert(error); char buf[256]; SM50GetErrorMessage(error, buf, sizeof(buf));
  assert(std::strstr(buf, expected)); SM50FreeError(error);
}
static void exercise(std::string_view mode, const std::vector<uint32_t> &good, const std::vector<uint32_t> &hull) {
  sm50_error_t error = reinterpret_cast<sm50_error_t>(uintptr_t(1));
  sm50_shader_t shader = nullptr; sm50_bitcode_t bitcode = nullptr;
  bool omit = mode.ends_with("-omit"); auto **error_out = omit ? nullptr : &error;
  if (mode.starts_with("null-init")) {
    assert(SM50Initialize(nullptr, 0, nullptr, nullptr, error_out) != 0);
    if (!omit) check_error(error, "ppShader");
  } else if (mode.starts_with("null-compile")) {
    int r = -1; char which = mode[12];
    switch (which) {
    case '0': r = SM50Compile(nullptr, nullptr, "test", nullptr, error_out); break;
    case '1': r = SM50CompileTessellationPipelineHull(nullptr, nullptr, nullptr, "test", nullptr, error_out); break;
    case '2': r = SM50CompileTessellationPipelineDomain(nullptr, nullptr, nullptr, "test", nullptr, error_out); break;
    case '3': r = SM50CompileGeometryPipelineVertex(nullptr, nullptr, nullptr, "test", nullptr, error_out); break;
    case '4': r = SM50CompileGeometryPipelineGeometry(nullptr, nullptr, nullptr, "test", nullptr, error_out); break;
    }
    assert(r == 1); if (!omit) check_error(error, "ppBitcode");
  } else if (mode.starts_with("partial")) {
    MTL_SHADER_REFLECTION reflection{};
    assert(SM50Initialize(hull.data(), hull.size()*4, &shader, &reflection, error_out) != 0);
    assert(!shader); if (!omit) check_error(error, "Threadgroup size");
  } else if (mode.starts_with("invalid")) {
    assert(SM50Initialize(nullptr, 0, &shader, nullptr, error_out) != 0);
    if (!omit) check_error(error, "Invalid DXBC");
  } else if (mode.starts_with("init")) {
    assert(SM50Initialize(good.data(), good.size()*4, &shader, nullptr, error_out) == 0);
    assert(shader); if (!omit) assert(!error); SM50Destroy(shader);
  } else if (mode.starts_with("compile-error")) {
    dxmt::dxbc::SM50ShaderInternal unsupported{}; unsupported.shader_type = D3D11_SB_HULL_SHADER;
    assert(SM50Compile(&unsupported, nullptr, "test", &bitcode, error_out) != 0);
    assert(!bitcode); if (!omit) check_error(error, "cannot be independently");
  } else if (mode.starts_with("compile-success")) {
    assert(SM50Initialize(good.data(), good.size()*4, &shader, nullptr, &error) == 0); assert(!error);
    assert(SM50Compile(shader, nullptr, "test", &bitcode, error_out) == 0);
    assert(bitcode); if (!omit) assert(!error);
    SM50_COMPILED_BITCODE data{}; SM50GetCompiledBitcode(bitcode, &data); assert(data.Size > 0);
    SM50DestroyBitcode(bitcode); SM50Destroy(shader);
  } else { std::abort(); }
}
int main(int argc, char **argv) {
  assert(argc == 2); auto good = bytecode(); auto hull = bytecode(true);
  // LLVM owns process-lifetime registrations. Warm up this exact path first;
  // repeated API calls must then return all caller-releasable allocations.
  for (int i = 0; i < 3; ++i) exercise(argv[1], good, hull);
  const auto before = outstanding;
  for (int i = 0; i < 5; ++i) exercise(argv[1], good, hull);
  const auto leaked = outstanding - before;
  if (leaked) { std::fprintf(stderr, "LEAK %ld allocations in %s\n", leaked, argv[1]); return 2; }
  std::printf("PASS %s\n", argv[1]);
}

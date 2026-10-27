// SPDX-License-Identifier: LGPL-2.1-or-later
// Actual DXBC parser -> owned CFG -> production LLVM lowering. No GPU/runtime.
#undef NDEBUG
#include "dxbc_converter.hpp"
#include "DXBCParser/ShaderBinary.h"
#include "llvm/IR/Verifier.h"
#include <cstdio>
#include <set>
#include <string_view>
#include <variant>
using namespace microsoft;
using namespace dxmt::dxbc;
namespace dxmt::dxbc {
llvm::Error convertDXBC(sm50_shader_t, const char *, llvm::LLVMContext &, llvm::Module &,
                       SM50_SHADER_COMPILATION_ARGUMENT_DATA *);
}
static uint32_t op(uint32_t code, uint32_t size = 1) {
  return code | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(size);
}
static constexpr uint32_t label_operand =
    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_LABEL) |
    ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D);
static constexpr uint32_t immediate_operand =
    ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
    ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);
static void call(std::vector<uint32_t> &code, uint32_t id) {
  code.insert(code.end(), {op(D3D10_SB_OPCODE_CALL, 3), label_operand, id});
}
static void label(std::vector<uint32_t> &code, uint32_t id) {
  code.insert(code.end(), {op(D3D10_SB_OPCODE_LABEL, 3), label_operand, id});
}
static void callc(std::vector<uint32_t> &code, bool nonzero, uint32_t value, uint32_t id) {
  code.insert(code.end(), {op(D3D10_SB_OPCODE_CALLC, 5) |
      ENCODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(nonzero ? D3D10_SB_INSTRUCTION_TEST_NONZERO :
                                                       D3D10_SB_INSTRUCTION_TEST_ZERO),
      immediate_operand, value, label_operand, id});
}
static int check(bool condition, const char *message) {
  if (!condition) { std::fprintf(stderr, "FAIL %s\n", message); return 43; }
  return 0;
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  std::string_view mode(argv[1]);
  std::vector<uint32_t> code{ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER, 5, 0), 0};
  unsigned expected_calls = 0, expected_conditions = 0;
  if (mode == "direct") { call(code, 7); expected_calls = 1; }
  else if (mode == "nested") { call(code, 7); expected_calls = 2; }
  else if (mode == "repeated") { for (int i = 0; i < 3; ++i) call(code, 7); expected_calls = 3; }
  else if (mode.starts_with("conditional-")) {
    bool nonzero = mode.starts_with("conditional-nz-");
    uint32_t value = mode.ends_with("1") ? 1 : 0;
    callc(code, nonzero, value, 7);
    expected_calls = (value != 0) == nonzero;
    expected_conditions = 1;
  } else if (mode == "mixed") {
    call(code, 7); callc(code, true, 1, 7); callc(code, true, 0, 7); call(code, 7);
    expected_calls = 3; expected_conditions = 2;
  } else if (mode != "no-call") return 2;
  code.push_back(op(D3D10_SB_OPCODE_RET));
  if (mode != "no-call") {
    label(code, 7);
    if (mode == "nested") call(code, 9);
    code.push_back(op(D3D10_SB_OPCODE_RET));
    if (mode == "nested") { label(code, 9); code.push_back(op(D3D10_SB_OPCODE_RET)); }
  }
  code[1] = code.size();
  SM50ShaderInternal shader{};
  shader.shader_type = D3D10_SB_PIXEL_SHADER;
  CSignatureParser input; CSignatureParser5 output;
  D3D10ShaderBinary::CShaderCodeParser parser(code.data());
  try { shader.bbs = read_control_flow(parser, &shader, input, output); }
  catch (const std::bad_variant_access &) {
    std::fprintf(stderr, "FAIL conditional-call targets wrong variant\n"); return 42;
  }
  std::set<BasicBlock *> owned;
  for (auto &bb : shader.bbs) owned.insert(bb.get());
  unsigned bodies = 0, conditions = 0, steps = 0;
  auto *current = shader.entry();
  for (;;) {
    if (int r = check(owned.contains(current), "return continuation is outside owned CFG")) return r;
    if (int r = check(++steps <= 200, "call return did not reach main return")) return r;
    bodies += current->debug_name == "func_body";
    if (std::holds_alternative<BasicBlockReturn>(current->target)) break;
    if (auto *branch = std::get_if<BasicBlockUnconditionalBranch>(&current->target)) {
      current = branch->target;
    } else if (auto *branch = std::get_if<BasicBlockConditionalBranch>(&current->target)) {
      ++conditions;
      auto *value = std::get_if<SrcOperandImmediate32>(&branch->cond.operand);
      if (int r = check(value != nullptr, "conditional source was not preserved")) return r;
      current = ((value->uvalue[0] != 0) == branch->cond.test_nonzero) ? branch->true_branch : branch->false_branch;
    } else return check(false, "reachable call was not inlined");
  }
  if (int r = check(bodies == expected_calls, "called-body execution count")) return r;
  if (int r = check(conditions == expected_conditions, "conditional-call gate was not preserved")) return r;
  llvm::LLVMContext ll; ll.setOpaquePointers(false);
  llvm::Module module("call-return", ll);
  if (auto error = convertDXBC(&shader, "call_return", ll, module, nullptr)) {
    llvm::consumeError(std::move(error)); return check(false, "production LLVM conversion");
  }
  if (int r = check(!llvm::verifyModule(module, &llvm::errs()), "production LLVM module verification")) return r;
  std::printf("PASS %s CFG path and LLVM lowering\n", argv[1]);
}

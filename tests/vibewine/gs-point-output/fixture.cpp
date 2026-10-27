// SPDX-License-Identifier: LGPL-2.1-or-later
// Exercise the real GS converter and validate its AIR/LLVM output, CPU only.
#undef NDEBUG
#include "dxbc_converter.hpp"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include <cassert>
#include <cstdio>
#include <string_view>
using namespace dxmt::dxbc;
using namespace microsoft;
static bool has_string(const llvm::Metadata *md, llvm::StringRef value) {
  if (auto *s = llvm::dyn_cast<llvm::MDString>(md)) return s->getString() == value;
  if (auto *n = llvm::dyn_cast<llvm::MDNode>(md))
    for (auto &op : n->operands()) if (op && has_string(op.get(), value)) return true;
  return false;
}
static bool exercise(D3D10_SB_PRIMITIVE_TOPOLOGY topology, unsigned emits, unsigned cut_at,
                     bool active, unsigned expected, std::string_view mode) {
  llvm::LLVMContext ll; ll.setOpaquePointers(false);
  llvm::Module module("gs-output", ll);
  SM50ShaderInternal shader{}, vertex{};
  shader.shader_type = D3D10_SB_GEOMETRY_SHADER;
  shader.gs_input_primitive = D3D10_SB_PRIMITIVE_POINT;
  shader.gs_output_topology = topology;
  shader.gs_max_vertex_output = 16;
  shader.bbs.push_back(std::make_unique<BasicBlock>("body"));
  auto &body = *shader.bbs.front();
  for (unsigned i = 0; i < emits; ++i) {
    body.instructions.push_back(InstEmit{});
    if (i + 1 == cut_at) body.instructions.push_back(InstCut{});
  }
  body.target = BasicBlockReturn{};
  auto error = convert_dxbc_geometry_shader(&shader, "gs", &vertex, ll, module, nullptr);
  if (error) { llvm::errs() << std::move(error); std::abort(); }
  assert(!llvm::verifyModule(module, &llvm::errs()));
  bool point = topology == D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST;
  bool metadata = false;
  for (auto *node : module.getNamedMetadata("air.mesh")->operands())
    metadata |= has_string(node, "air.point_size");
  if ((mode == "metadata" || !point) && metadata != point) return false;

  // Specialize only the invocation-validity branch. The production-generated
  // emit/cut arithmetic is then folded by LLVM, giving observable AIR operands.
  auto *fn = module.getFunction("gs");
  auto *branch = llvm::cast<llvm::BranchInst>(fn->getEntryBlock().getTerminator());
  assert(branch->isConditional());
  branch->setCondition(llvm::ConstantInt::get(llvm::Type::getInt1Ty(ll), active));
  llvm::legacy::PassManager passes;
  passes.add(llvm::createPromoteMemoryToRegisterPass());
  passes.add(llvm::createSCCPPass());
  passes.add(llvm::createCFGSimplificationPass());
  passes.add(llvm::createInstructionCombiningPass());
  passes.run(module);
  assert(!llvm::verifyModule(module, &llvm::errs()));
  unsigned size_calls = 0, count_calls = 0, index_calls = 0;
  for (auto &bb : *fn) for (auto &inst : bb) {
    auto *call = llvm::dyn_cast<llvm::CallInst>(&inst);
    if (!call || !call->getCalledFunction()) continue;
    auto name = call->getCalledFunction()->getName();
    if (name == "air.set_point_size_mesh") {
      ++size_calls;
      auto *id = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(1));
      auto *size = llvm::dyn_cast<llvm::ConstantFP>(call->getArgOperand(2));
      if (!point || !id || id->getZExtValue() != size_calls - 1 || !size ||
          !size->isExactlyValue(1.0)) return false;
    } else if (name == "air.set_primitive_count_mesh") {
      ++count_calls;
      auto *count = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(1));
      if ((mode == "count" || !point) && (!count || count->getZExtValue() != expected)) return false;
    } else if (name == "air.set_index_mesh") {
      ++index_calls;
      if (point) {
        auto *index = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(1));
        auto *value = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(2));
        if (!index || !value || index->getZExtValue() != index_calls - 1 ||
            value->getZExtValue() != index_calls - 1) return false;
      }
    }
  }
  assert(count_calls == 1);
  if ((mode == "size" || !point) && size_calls != (point && active ? emits : 0)) return false;
  if (point) assert(index_calls == (active ? emits : 0));
  return true;
}
int main(int argc, char **argv) {
  assert(argc == 2); std::string_view mode = argv[1];
  bool valid = true;
  if (mode == "controls") {
    for (auto topology : {D3D10_SB_PRIMITIVE_TOPOLOGY_LINESTRIP, D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP}) {
      unsigned reduction = topology == D3D10_SB_PRIMITIVE_TOPOLOGY_LINESTRIP ? 1 : 2;
      for (unsigned n = 0; n <= 5; ++n)
        valid &= exercise(topology, n, 0, true, n > reduction ? n - reduction : 0, mode);
      valid &= exercise(topology, 7, 3, true, (3 - reduction) + (4 - reduction), mode);
      valid &= exercise(topology, 5, 0, false, 0, mode);
    }
  } else {
    for (unsigned n : {0u, 1u, 4u})
      valid &= exercise(D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST, n, 0, true, n, mode);
    valid &= exercise(D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST, 4, 2, true, 4, mode);
    valid &= exercise(D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST, 4, 2, false, 0, mode);
  }
  if (!valid) { std::fprintf(stderr, "FAIL point %s\n", argv[1]); return 2; }
  std::printf("PASS %s\n", argv[1]);
}

// CPU-only regression using production parser and converter translation units.
#undef NDEBUG
#include "dxbc_converter.hpp"
#include "nt/dxbc_converter_base.hpp"
#include "llvm/IR/Verifier.h"
#include <cassert>
#include <iostream>
using namespace dxmt::dxbc;
using namespace microsoft;
struct EmptyBindings final : BindingMap {
#define EMPTY(name, type) llvm::Optional<type> name(llvm::air::AIRBuilder &, RangeId, llvm::Value *) override { return {}; }
  EMPTY(GetConstantBuffer, ConstantBufferDescriptor)
  EMPTY(GetSampler, SamplerDescriptor)
  EMPTY(GetSRVTexture, TextureDescirptor)
  EMPTY(GetUAVTexture, TextureDescirptor)
  EMPTY(GetSRVBuffer, BufferDescriptor)
  EMPTY(GetUAVBuffer, BufferDescriptor)
  EMPTY(GetUAVCounter, CounterDescriptor)
#undef EMPTY
};
int main() {
  llvm::LLVMContext ll;
  ll.setOpaquePointers(false);
  llvm::Module module("control_point_index", ll);
  llvm::IRBuilder<> ir(ll);
  llvm::air::AIRBuilder air(ir, llvm::errs());
  dxmt::air::AirType types(ll);
  EmptyBindings bindings;
  io_binding_map res;
  auto *fn = llvm::Function::Create(llvm::FunctionType::get(ir.getVoidTy(), {ir.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "hull", module);
  ir.SetInsertPoint(llvm::BasicBlock::Create(ll, "entry", fn));
  res.thread_id_in_patch = fn->getArg(0);
  auto *patch = llvm::ArrayType::get(llvm::ArrayType::get(air.getIntTy(4), 3), 32);
  res.input.ptr_int4 = ir.CreateAlloca(patch);
  context ctx{ir, air, bindings, ll, module, fn, res, types, 0, D3D11_SB_HULL_SHADER, SM50_SHADER_METAL_310};
  Converter converter(air, ctx, res);
  for (uint32_t offset : {0u, 3u, 17u}) {
    D3D10ShaderBinary::COperandBase op{};
    op.m_Type = D3D11_SB_OPERAND_TYPE_INPUT_CONTROL_POINT;
    op.m_NumComponents = D3D10_SB_OPERAND_4_COMPONENT;
    op.m_ComponentSelection = D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE;
    for (int i = 0; i < 4; ++i) op.m_Swizzle[i] = 0;
    op.m_IndexDimension = D3D10_SB_OPERAND_INDEX_2D;
    op.m_IndexType[0] = offset ? D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE : D3D10_SB_OPERAND_INDEX_RELATIVE;
    op.m_Index[0].m_RelRegType = D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT_ID;
    op.m_Index[0].m_RegIndex = offset;
    op.m_IndexType[1] = D3D10_SB_OPERAND_INDEX_IMMEDIATE32;
    op.m_Index[1].m_RegIndex = 2;
    auto parsed = readSrcOperand(op, ~0u, OperandDataType::Integer);
    auto *input = std::get_if<SrcOperandInputICP>(&parsed);
    assert(input && input->regid == 2);
    auto *index = converter.LoadOperandIndex(input->cpid);
    {
      auto *add = llvm::dyn_cast<llvm::BinaryOperator>(index);
      assert(add && add->getOpcode() == llvm::Instruction::Add);
      assert(add->getOperand(0) == res.thread_id_in_patch);
      assert(llvm::cast<llvm::ConstantInt>(add->getOperand(1))->getZExtValue() == offset);
    }
    auto *value = converter.LoadOperand(*input, 1);
    auto *load = llvm::dyn_cast<llvm::LoadInst>(value);
    assert(load);
    auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(load->getPointerOperand());
    assert(gep);
    auto *loaded_index = gep->getOperand(2);
    {
      auto *add = llvm::cast<llvm::BinaryOperator>(loaded_index);
      assert(add->getOpcode() == llvm::Instruction::Add);
      assert(add->getOperand(0) == res.thread_id_in_patch);
      assert(llvm::cast<llvm::ConstantInt>(add->getOperand(1))->getZExtValue() == offset);
    }
    // Immediate indexing remains independent of the invocation id.
    op.m_IndexType[0] = D3D10_SB_OPERAND_INDEX_IMMEDIATE32;
    op.m_Index[0].m_RelRegType = D3D10_SB_OPERAND_TYPE_IMMEDIATE32;
    auto immediate = std::get<SrcOperandInputICP>(readSrcOperand(op, ~0u, OperandDataType::Integer));
    assert(llvm::cast<llvm::ConstantInt>(converter.LoadOperandIndex(immediate.cpid))->getZExtValue() == offset);
    // Existing temporary/indexable-temporary decoding preserves register/phase/offset.
    op.m_IndexType[0] = D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE;
    op.m_Index[0].m_RelRegType = D3D10_SB_OPERAND_TYPE_TEMP;
    op.m_Index[0].m_RelIndex = 4;
    op.m_Index[0].m_ComponentName = D3D10_SB_4_COMPONENT_Y;
    auto temporary = std::get<SrcOperandInputICP>(readSrcOperand(op, 11, OperandDataType::Integer));
    auto relative = std::get<IndexByTempComponent>(temporary.cpid);
    assert(relative.regid == 4 && relative.phase == 11 && relative.component == 1 && relative.offset == offset);
    op.m_Index[0].m_RelRegType = D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP;
    op.m_Index[0].m_RelIndex1 = 6;
    auto indexed = std::get<SrcOperandInputICP>(readSrcOperand(op, 11, OperandDataType::Integer));
    auto array = std::get<IndexByIndexableTempComponent>(indexed.cpid);
    assert(array.regfile == 4 && array.regid == 6 && array.phase == 11 && array.component == 1 && array.offset == offset);
    // Unsupported relative register types have a deterministic release fallback.
    op.m_Index[0].m_RelRegType = D3D10_SB_OPERAND_TYPE_RESOURCE;
    auto unsupported = std::get<SrcOperandInputICP>(readSrcOperand(op, ~0u, OperandDataType::Integer));
    assert(std::get<uint32_t>(unsupported.cpid) == 0);
  }
  ir.CreateRetVoid();
  assert(!llvm::verifyModule(module, &llvm::errs()));
  std::cout << "PASS actual parser -> LLVM invocation+offset -> input-patch GEP, 3 relative, 3 immediate, 6 temp and 3 fallback cases\n";
}

// SPDX-License-Identifier: LGPL-2.1-or-later
// Production pipeline, AIR builders and public DXBC parser/converter; CPU only.
#undef NDEBUG
#include "airconv_context.hpp"
#include "dxbc_converter.hpp"
#include "metallib_writer.hpp"
#include "nt/air_builder.hpp"
#include "DXBCParser/BlobContainer.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/FileSystem.h"
#include <array>
#include <cassert>
#include <iostream>
#include <cstring>
using namespace llvm;
using namespace microsoft;
void shaderCleanupContractChecks();

static Function *halfwords(Module &m, unsigned lane) {
  IRBuilder<> b(m.getContext());
  auto *v = FixedVectorType::get(b.getInt32Ty(), 4);
  auto *f = Function::Create(FunctionType::get(b.getInt16Ty(), {v}, false),
      Function::ExternalLinkage, "halfwords" + std::to_string(lane), m);
  b.SetInsertPoint(BasicBlock::Create(m.getContext(), "entry", f));
  // Exact shape consumed by production Lower16BitTexReadPass. The distinct
  // low/high original extracts cannot CSE until that pass expands them.
  auto *halves = b.CreateBitCast(f->getArg(0), FixedVectorType::get(b.getInt16Ty(), 8));
  auto *low = b.CreateExtractElement(halves, b.getInt32(lane * 2));
  auto *high = b.CreateExtractElement(halves, b.getInt32(lane * 2 + 1));
  b.CreateRet(b.CreateAdd(low, high));
  return f;
}

static Constant *evaluate(Value *v, Constant *input, const DataLayout &dl) {
  if (auto *c = dyn_cast<Constant>(v)) return c;
  if (isa<Argument>(v)) return input;
  auto *i = cast<Instruction>(v);
  SmallVector<Constant *, 4> operands;
  for (auto &op : i->operands()) operands.push_back(evaluate(op, input, dl));
  auto *result = ConstantFoldInstOperands(i, operands, dl);
  assert(result && !isa<UndefValue>(result) && !isa<PoisonValue>(result));
  return result;
}

static void semantics(Module &m) {
  // Includes 0, sign bits, all bits, carry/wrap, alternating bits, and a
  // deterministic sample. This is bit mapping, not a shader/GPU benchmark.
  std::array<uint32_t, 8> edges{0, 1, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff, 0x55aaaa55};
  uint32_t state = 0x12345678;
  for (unsigned sample = 0; sample < 264; ++sample) {
    SmallVector<Constant *, 4> values;
    std::array<uint32_t, 4> bits;
    for (unsigned lane = 0; lane < 4; ++lane) {
      state = state * 1664525u + 1013904223u;
      bits[lane] = sample < edges.size() ? edges[(sample + lane) % edges.size()] : state;
      values.push_back(ConstantInt::get(Type::getInt32Ty(m.getContext()), bits[lane]));
    }
    auto *input = ConstantVector::get(values);
    for (unsigned lane = 0; lane < 4; ++lane) {
      auto *f = m.getFunction("halfwords" + std::to_string(lane));
      auto *ret = cast<ReturnInst>(f->back().getTerminator());
      auto *actual = cast<ConstantInt>(evaluate(ret->getReturnValue(), input, m.getDataLayout()));
      assert(actual->getZExtValue() == uint16_t((bits[lane] & 0xffffu) + (bits[lane] >> 16)));
    }
  }
}

static void safety(Module &m) {
  IRBuilder<> b(m.getContext());
  air::AIRBuilder air(b, errs());
  air::Texture tex{air::Texture::texture2d, air::Texture::sample_uint, air::Texture::access_write};
  auto *f = Function::Create(FunctionType::get(b.getVoidTy(),
      {b.getInt32Ty()->getPointerTo(3), b.getInt32Ty()->getPointerTo(1),
       air.getTextureHandleType(tex), b.getInt32Ty()}, false), Function::ExternalLinkage, "effects", m);
  b.SetInsertPoint(BasicBlock::Create(m.getContext(), "entry", f));
  b.CreateStore(f->getArg(3), f->getArg(0), true);
  auto *load = b.CreateLoad(b.getInt32Ty(), f->getArg(0), true);
  b.CreateStore(load, f->getArg(1), true);
  air.CreateAtomicRMW(AtomicRMWInst::Add, f->getArg(1), b.getInt32(1));
  air.CreateWrite(tex, f->getArg(2), air.getInt2(0, 0), nullptr, nullptr,
                  b.getInt32(0), air.getInt4(1, 2, 3, 4));
  air.CreateDiscard();
  b.CreateRetVoid();
  m.getOrInsertNamedMetadata("air.kernel");

  auto *precise = Function::Create(FunctionType::get(b.getFloatTy(),
      {b.getFloatTy(), b.getFloatTy(), b.getFloatTy()}, false), Function::ExternalLinkage, "precise", m);
  b.SetInsertPoint(BasicBlock::Create(m.getContext(), "entry", precise));
  auto *mul = b.CreateFMul(precise->getArg(0), precise->getArg(1));
  b.CreateRet(b.CreateFAdd(mul, precise->getArg(2)));
}

static void checkSafety(Module &m) {
  auto *f = m.getFunction("effects");
  unsigned stores = 0, loads = 0, barriers = 0, atomics = 0, writes = 0, discards = 0;
  bool barrierBeforeLoad = false;
  for (auto &bb : *f) for (auto &i : bb) {
    if (auto *s = dyn_cast<StoreInst>(&i)) { assert(s->isVolatile()); ++stores; }
    if (auto *l = dyn_cast<LoadInst>(&i)) {
      assert(l->isVolatile() && barrierBeforeLoad); ++loads;
    }
    if (auto *c = dyn_cast<CallInst>(&i)) {
      auto *callee = c->getCalledFunction(); assert(callee);
      auto name = callee->getName();
      if (name.startswith("air.simdgroup.barrier")) {
        assert(callee->hasFnAttribute(Attribute::Convergent));
        assert(!callee->hasFnAttribute(Attribute::ReadNone));
        assert(c->arg_size() == 2 && cast<ConstantInt>(c->getArgOperand(0))->getZExtValue() == 2 && cast<ConstantInt>(c->getArgOperand(1))->getZExtValue() == 1);
        ++barriers; barrierBeforeLoad = true;
      }
      if (name.startswith("air.atomic.global.add")) ++atomics;
      if (name.startswith("air.write_texture_2d")) ++writes;
      if (name == "air.discard_fragment") ++discards;
    }
  }
  assert(stores == 2 && loads == 1 && barriers == 1 && atomics == 1 && writes == 1 && discards == 1);
  unsigned fp = 0;
  for (auto &bb : *m.getFunction("precise")) for (auto &i : bb) {
    if (isa<FPMathOperator>(i)) {
      assert((i.getOpcode() == Instruction::FMul || i.getOpcode() == Instruction::FAdd) && !i.getFastMathFlags().any());
      ++fp;
    }
  }
  assert(fp == 2);
}

static void publicConverter() {
  // A minimal valid SM5 compute DXBC exercises actual container parsing,
  // converter construction, selected production pipeline and AIR serialization.
  auto opcode = [](uint32_t code, uint32_t n) { return code | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(n); };
  std::vector<uint32_t> code{
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D11_SB_COMPUTE_SHADER, 5, 0), 7,
      opcode(D3D11_SB_OPCODE_DCL_THREAD_GROUP, 4), 1, 1, 1,
      opcode(D3D10_SB_OPCODE_RET, 1)};
  std::vector<uint32_t> dxbc(11);
  dxbc[0] = DXBC_FOURCC_NAME; dxbc[5] = 1; dxbc[7] = 3;
  auto blob = [&](unsigned index, uint32_t tag, const std::vector<uint32_t> &payload) {
    dxbc[index] = dxbc.size() * 4; dxbc.push_back(tag); dxbc.push_back(payload.size() * 4);
    dxbc.insert(dxbc.end(), payload.begin(), payload.end());
  };
  blob(8, DXBC_GenericShaderEx, code); blob(9, DXBC_InputSignature, {0, 8}); blob(10, DXBC_OutputSignature, {0, 8});
  dxbc[6] = dxbc.size() * 4;
  sm50_shader_t shader = nullptr; sm50_bitcode_t bitcode = nullptr; sm50_error_t error = nullptr;
  assert(SM50Initialize(dxbc.data(), dxbc.size() * 4, &shader, nullptr, &error) == 0 && shader && !error);
  assert(SM50Compile(shader, nullptr, "cleanup_public", &bitcode, &error) == 0 && bitcode && !error);
  SM50_COMPILED_BITCODE data{}; SM50GetCompiledBitcode(bitcode, &data); assert(data.Size);
  LLVMContext ll;
  dxmt::metallib::MTLBHeader header{};
  assert(data.Size >= sizeof(header)); std::memcpy(&header, data.Data, sizeof(header));
  assert(header.Magic == dxmt::metallib::MTLB_Magic && header.FileSize == data.Size);
  assert(header.BitcodeOffset <= data.Size && header.BitcodeSize <= data.Size - header.BitcodeOffset);
  auto result = parseBitcodeFile(MemoryBufferRef(StringRef(static_cast<const char *>(data.Data) + header.BitcodeOffset, header.BitcodeSize), "public"), ll);
  assert(result && !verifyModule(**result, &errs()));
  SM50DestroyBitcode(bitcode); SM50Destroy(shader);
}

int main(int argc, char **argv) {
  assert(argc == 3);
  shaderCleanupContractChecks();
  errs() << "fixture: start\n";
  LLVMContext ll; ll.setOpaquePointers(false);
  Module m("late-halfword", ll); dxmt::initializeModule(m);
  for (unsigned lane = 0; lane < 4; ++lane) halfwords(m, lane);
  safety(m); assert(!verifyModule(m, &errs())); semantics(m); errs()<<"fixture: pre-semantics complete\n";
  dxmt::runOptimizationPasses(m); assert(!verifyModule(m, &errs())); errs()<<"fixture: pipeline complete\n";
  semantics(m); checkSafety(m);
  unsigned extracts = 0;
  for (unsigned lane = 0; lane < 4; ++lane)
    for (auto &bb : *m.getFunction("halfwords" + std::to_string(lane)))
      for (auto &i : bb) if (isa<ExtractElementInst>(i)) ++extracts;
  assert(extracts == (std::string(argv[1]) == "on" ? 4u : 8u));
  std::error_code ec; raw_fd_ostream output(argv[2], ec, sys::fs::OF_Text); assert(!ec); m.print(output, nullptr); output.close();
  errs()<<"fixture: safety complete\n"; publicConverter(); errs()<<"fixture: public converter complete\n";
  std::cout << "PASS cleanup=" << argv[1] << " extracts=" << extracts
            << " bit-mapping=1056 cases; precise FP + threadgroup barrier/load/stores + AIR atomic/texture-write/discard + public DXBC->AIR verified\n";
}

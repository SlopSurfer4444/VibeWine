// SPDX-License-Identifier: LGPL-2.1-or-later
// Appended only to the runner's private copy of the production context TU.
// This reaches the actual restricted pass without adding a production test API.
#undef NDEBUG
#include <cassert>
void shaderCleanupContractChecks() {
#if DXMT_SHADER_CLEANUP
  llvm::LLVMContext ll;
  llvm::Module m("cleanup-contract", ll);
  llvm::IRBuilder<> b(ll);
  auto *v = llvm::FixedVectorType::get(b.getInt32Ty(), 4);
  auto *f = llvm::Function::Create(llvm::FunctionType::get(b.getVoidTy(), {v, b.getInt32Ty(), b.getInt1Ty()}, false),
      llvm::Function::ExternalLinkage, "contract", m);
  auto *entry = llvm::BasicBlock::Create(ll, "entry", f);
  auto *left = llvm::BasicBlock::Create(ll, "left", f);
  auto *right = llvm::BasicBlock::Create(ll, "right", f);
  b.SetInsertPoint(entry);
  // Same lane, differently typed index operands remain distinct; a genuine
  // duplicate is removed. Exotic-width and out-of-range indices are excluded.
  b.CreateExtractElement(f->getArg(0), b.getInt32(0));
  b.CreateExtractElement(f->getArg(0), b.getInt64(0));
  b.CreateExtractElement(f->getArg(0), b.getInt64(0));
  auto *wide = llvm::ConstantInt::get(llvm::IntegerType::get(ll, 128), 0);
  b.CreateExtractElement(f->getArg(0), wide);
  b.CreateExtractElement(f->getArg(0), wide);
  b.Insert(llvm::ExtractElementInst::Create(f->getArg(0), b.getInt64(4)));
  b.Insert(llvm::ExtractElementInst::Create(f->getArg(0), b.getInt64(4)));
  auto *exact = llvm::cast<llvm::BinaryOperator>(b.CreateLShr(f->getArg(1), 16));
  exact->setIsExact(true);
  b.CreateLShr(f->getArg(1), 16);
  b.CreateLShr(f->getArg(1), 16);
  b.CreateTrunc(f->getArg(1), b.getInt16Ty());
  b.CreateTrunc(f->getArg(1), b.getInt16Ty());
  b.CreateCondBr(f->getArg(2), left, right);
  for (auto *block : {left, right}) {
    b.SetInsertPoint(block);
    b.CreateExtractElement(f->getArg(0), b.getInt64(0));
    b.CreateRetVoid();
  }
  llvm::FunctionAnalysisManager fam;
  dxmt::LateHalfwordCSEPass{}.run(*f, fam);
  unsigned extracts = 0, shifts = 0, truncs = 0;
  for (auto &i : *entry) {
    if (llvm::isa<llvm::ExtractElementInst>(i)) ++extracts;
    if (i.getOpcode() == llvm::Instruction::LShr) ++shifts;
    if (llvm::isa<llvm::TruncInst>(i)) ++truncs;
  }
  assert(extracts == 6 && shifts == 2 && truncs == 1);
  assert(left->size() == 2 && right->size() == 2);
  assert(exact->isExact());
#endif
}

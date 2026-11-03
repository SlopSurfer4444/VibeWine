// SPDX-License-Identifier: LGPL-2.1-or-later
#include "constant_loop_motion.hpp"

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Scalar/LICM.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"

namespace llvm::air {
namespace {

bool isFragmentEntry(const Function &F) {
  const auto &M = *F.getParent();
  // Combined graphics stages and kernels stay outside this experiment, even
  // when another function in the module has fragment metadata.
  for (auto Name : {"air.kernel", "air.vertex", "air.mesh", "air.object"})
    if (M.getNamedMetadata(Name))
      return false;
  const auto *Fragment = M.getNamedMetadata("air.fragment");
  if (!Fragment)
    return false;
  for (const auto *Entry : Fragment->operands()) {
    if (!Entry->getNumOperands())
      continue;
    const auto *Value = dyn_cast_or_null<ValueAsMetadata>(Entry->getOperand(0));
    if (Value && Value->getValue() == &F)
      return true;
  }
  return false;
}

bool isAllowedMath(const CallInst &Call) {
  const auto *Callee = Call.getCalledFunction();
  if (!Callee || !Callee->isDeclaration() || Call.isConvergent() ||
      Call.hasFnAttr(Attribute::StrictFP) || Callee->hasFnAttribute(Attribute::StrictFP) ||
      Call.hasFnAttr(Attribute::NoDuplicate) || Callee->isConvergent() ||
      Callee->hasFnAttribute(Attribute::NoDuplicate) || Call.hasOperandBundles() ||
      Call.isMustTailCall() || Callee->isVarArg() ||
      !Callee->doesNotAccessMemory() || !Callee->doesNotThrow() ||
      !Callee->hasFnAttribute(Attribute::NoSync) ||
      !Callee->hasFnAttribute(Attribute::WillReturn))
    return false;

  Type *ValueType = Call.getArgOperand(0)->getType();
  unsigned Width = 1;
  if (const auto *Vector = dyn_cast<FixedVectorType>(ValueType)) {
    Width = Vector->getNumElements();
    if (Width < 2 || Width > 4 || !Vector->getElementType()->isFloatTy())
      return false;
  } else if (!ValueType->isFloatTy()) {
    // getScalarType() also unwraps scalable vectors; those must never be
    // mistaken for the scalar .f32 overload of an AIR math declaration.
    return false;
  }
  const std::string Suffix = Width == 1 ? ".f32" : ".v" + std::to_string(Width) + "f32";
  StringRef Name = Callee->getName();
  unsigned Arity = 0;
  bool Dot = false;
  for (auto Op : {"saturate", "log2", "exp2", "sqrt", "rsqrt", "fract", "rint", "floor", "ceil", "trunc",
                  "cos", "sin", "fabs"}) {
    if (Name == "air." + std::string(Op) + Suffix || Name == "air.fast_" + std::string(Op) + Suffix)
      Arity = 1;
  }
  for (auto Op : {"fmax", "fmin"}) {
    if (Name == "air." + std::string(Op) + Suffix || Name == "air.fast_" + std::string(Op) + Suffix)
      Arity = 2;
  }
  if (Name == "air.fma" + Suffix)
    Arity = 3;
  if (Width > 1 && Name == "air.dot" + Suffix) {
    Arity = 2;
    Dot = true;
  }
  if (!Arity || Call.arg_size() != Arity ||
      Call.getType() != (Dot ? ValueType->getScalarType() : ValueType) ||
      Call.getFunctionType() != Callee->getFunctionType())
    return false;
  for (auto &Arg : Call.args())
    if (Arg->getType() != ValueType)
      return false;
  return true;
}

bool isAdmittedFragment(const Function &F) {
  if (F.isDeclaration() || F.hasFnAttribute(Attribute::StrictFP) || !isFragmentEntry(F))
    return false;
  // Check every block, including unreachable code. A rejected memory/call
  // anywhere excludes this function before LoopSimplify/LCSSA can alter it.
  for (const auto &BB : F) {
    for (const auto &I : BB) {
      if (const auto *Load = dyn_cast<LoadInst>(&I)) {
        if (Load->getPointerAddressSpace() != 2 || Load->isVolatile() || Load->isAtomic())
          return false;
        continue;
      }
      if (const auto *Call = dyn_cast<CallInst>(&I)) {
        if (!Call->arg_size() || !isAllowedMath(*Call))
          return false;
        continue;
      }
      switch (I.getOpcode()) {
      case Instruction::Ret: case Instruction::Br: case Instruction::Switch:
      case Instruction::PHI: case Instruction::Select:
      case Instruction::Add: case Instruction::Sub: case Instruction::Mul:
      case Instruction::UDiv: case Instruction::SDiv: case Instruction::URem: case Instruction::SRem:
      case Instruction::Shl: case Instruction::LShr: case Instruction::AShr:
      case Instruction::And: case Instruction::Or: case Instruction::Xor:
      case Instruction::FNeg: case Instruction::FAdd: case Instruction::FSub:
      case Instruction::FMul: case Instruction::FDiv: case Instruction::FRem:
      case Instruction::ICmp: case Instruction::FCmp:
      case Instruction::Trunc: case Instruction::ZExt: case Instruction::SExt:
      case Instruction::FPToUI: case Instruction::FPToSI: case Instruction::UIToFP: case Instruction::SIToFP:
      case Instruction::FPTrunc: case Instruction::FPExt: case Instruction::BitCast:
      case Instruction::ExtractElement: case Instruction::InsertElement: case Instruction::ShuffleVector:
      case Instruction::ExtractValue: case Instruction::InsertValue: case Instruction::GetElementPtr:
        break;
      default:
        // Includes stores, atomics/fences, allocas, address-space conversions,
        // invoke/callbr and all other instructions not explicitly admitted.
        return false;
      }
    }
  }
  return true;
}

} // namespace

PreservedAnalyses ConstantLoopMotionPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (!isAdmittedFragment(F))
    return PreservedAnalyses::all();
  if (AM.getResult<LoopAnalysis>(F).empty())
    return PreservedAnalyses::all();

  FunctionPassManager PM;
  PM.addPass(LoopSimplifyPass());
  PM.addPass(LCSSAPass());
  // No rotation or custom motion. LLVM retains responsibility for aliasing,
  // dereferenceability, guaranteed execution and safe speculation.
  PM.addPass(createFunctionToLoopPassAdaptor(LICMPass(LICMOptions()), true /* UseMemorySSA */));
  return PM.run(F, AM);
}

} // namespace llvm::air

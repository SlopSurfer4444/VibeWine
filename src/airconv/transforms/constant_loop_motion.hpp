// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm::air {

// Opt-in caller only. Admission is whole-function and precedes all transforms;
// rejected functions retain their IR exactly, including their original CFG.
class ConstantLoopMotionPass : public PassInfoMixin<ConstantLoopMotionPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm::air

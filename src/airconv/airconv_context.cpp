#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Transforms/Scalar/LowerExpectIntrinsic.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/IPO/DeadArgumentElimination.h"
#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/Transforms/IPO/SCCP.h"
#include "llvm/Transforms/IPO/Annotation2Metadata.h"
#include "llvm/Transforms/IPO/ForceFunctionAttrs.h"
#include "llvm/Transforms/IPO/InferFunctionAttrs.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/ADT/DenseMap.h"

#include "airconv_context.hpp"

#include "air_msad.h"
#include "air_samplepos.h"
#include "air_tessellation.h"

#include "transforms/lower_16bit_texread.hpp"
#include "transforms/simdgroup_implicit_membarrier.hpp"

using namespace llvm;

namespace dxmt {

#ifndef DXMT_SHADER_CLEANUP
#define DXMT_SHADER_CLEANUP 0
#endif

#if DXMT_SHADER_CLEANUP
namespace {
// Lower16BitTexRead runs after the final generic cleanup and can emit the
// same i32 lane extraction for the low and high halfwords. Keep this experiment
// deliberately narrower than another EarlyCSE: no memory, calls, FP, motion
// across blocks, or changes to the implicit-barrier detector's input.
class LateHalfwordCSEPass : public PassInfoMixin<LateHalfwordCSEPass> {
  static bool eligible(Instruction &I) {
    if (auto *EE = dyn_cast<ExtractElementInst>(&I)) {
      auto *VT = dyn_cast<FixedVectorType>(EE->getVectorOperandType());
      auto *Index = dyn_cast<ConstantInt>(EE->getIndexOperand());
      return VT && VT->getNumElements() == 4 &&
             VT->getElementType()->isIntegerTy(32) && Index &&
             (Index->getBitWidth() == 32 || Index->getBitWidth() == 64) &&
             Index->getValue().ult(4);
    }
    if (I.getOpcode() == Instruction::LShr && I.getType()->isIntegerTy(32)) {
      auto *Shift = dyn_cast<ConstantInt>(I.getOperand(1));
      return Shift && Shift->equalsInt(16);
    }
    return isa<TruncInst>(I) && I.getType()->isIntegerTy(16) &&
           I.getOperand(0)->getType()->isIntegerTy(32);
  }

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    bool Changed = false;
    for (BasicBlock &BB : F) {
      // The restricted shapes give a bounded number of distinct operations
      // per operand; exact identity includes poison-generating flags.
      DenseMap<Value *, SmallVector<Instruction *, 4>> Available;
      for (auto It = BB.begin(); It != BB.end();) {
        Instruction &I = *It++;
        if (!eligible(I))
          continue;
        auto &Candidates = Available[I.getOperand(0)];
        Instruction *Existing = nullptr;
        for (auto *Candidate : Candidates) {
          if (I.isIdenticalTo(Candidate)) {
            Existing = Candidate;
            break;
          }
        }
        if (!Existing) {
          Candidates.push_back(&I);
          continue;
        }
        I.replaceAllUsesWith(Existing);
        I.eraseFromParent();
        Changed = true;
      }
    }
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};
} // namespace
#endif

void initializeModule(llvm::Module &M) {
  M.setSourceFileName("airconv_generated.metal");
  M.setTargetTriple("air64-apple-macosx14.0.0");
  M.setDataLayout(
    "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:"
    "64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-"
    "v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:"
    "16:32"
  );
  M.setSDKVersion(VersionTuple(15, 0));
  M.addModuleFlag(Module::ModFlagBehavior::Error, "wchar_size", 4);
  M.addModuleFlag(Module::ModFlagBehavior::Max, "frame-pointer", 2);
  M.addModuleFlag(Module::ModFlagBehavior::Max, "air.max_device_buffers", 31);
  M.addModuleFlag(Module::ModFlagBehavior::Max, "air.max_constant_buffers", 31);
  M.addModuleFlag(
    Module::ModFlagBehavior::Max, "air.max_threadgroup_buffers", 31
  );
  M.addModuleFlag(Module::ModFlagBehavior::Max, "air.max_textures", 128);
  M.addModuleFlag(
    Module::ModFlagBehavior::Max, "air.max_read_write_textures", 8
  );
  M.addModuleFlag(Module::ModFlagBehavior::Max, "air.max_samplers", 16);

};

static std::atomic_flag llvm_overwrite = false;

void
runOptimizationPasses(llvm::Module &M) {

  if (!llvm_overwrite.test_and_set()) {
    auto Map = cl::getRegisteredOptions();
    auto InfiniteLoopThreshold = Map["instcombine-infinite-loop-threshold"];
    if (InfiniteLoopThreshold) {
      reinterpret_cast<cl::opt<unsigned> *>(InfiniteLoopThreshold)->setValue(1000);
    }
  }

  // Following optimization passes are picked from default LLVM pipelines
  // An unoptimized shader can make PSO compilation take a very long time
  // But a full optimization pipeline is also too heavy
  // So we choose some passes that really matter to run

  // Create the analysis managers.
  // These must be declared in this order so that they are destroyed in the
  // correct order due to inter-analysis-manager references.
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  // Create the new pass manager builder.
  // Take a look at the PassBuilder constructor parameters for more
  // customization, e.g. specifying a TargetMachine or various debugging
  // options.
  llvm::PassInstrumentationCallbacks PIC;
  PassBuilder PB(nullptr, PipelineTuningOptions(), {}, &PIC);

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // llvm::StandardInstrumentations SI(true);
  // SI.registerCallbacks(PIC, &FAM);

  ModulePassManager MPM;

  // Convert @llvm.global.annotations to !annotation metadata.
  MPM.addPass(Annotation2MetadataPass());

  // Force any function attributes we want the rest of the pipeline to observe.
  MPM.addPass(ForceFunctionAttrsPass());
  // Do basic inference of function attributes from known properties of system
  // libraries and other oracles.
  MPM.addPass(InferFunctionAttrsPass());

  {
    // Create an early function pass manager to cleanup the output of the
    // frontend.
    FunctionPassManager EarlyFPM;
    // Lower llvm.expect to metadata before attempting transforms.
    // Compare/branch metadata may alter the behavior of passes like SimplifyCFG.
    EarlyFPM.addPass(LowerExpectIntrinsicPass());
    EarlyFPM.addPass(SimplifyCFGPass());
    EarlyFPM.addPass(SROAPass());
    EarlyFPM.addPass(EarlyCSEPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(EarlyFPM), true /* ? */));
  }

  MPM.addPass(AlwaysInlinerPass());
  MPM.addPass(IPSCCPPass());
  MPM.addPass(GlobalOptPass());
  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));
  MPM.addPass(DeadArgumentEliminationPass());

  {
    // Create a small function pass pipeline to cleanup after all the global
    // optimizations.
    FunctionPassManager GlobalCleanupPM;
    GlobalCleanupPM.addPass(InstCombinePass());

    GlobalCleanupPM.addPass(SimplifyCFGPass(SimplifyCFGOptions().convertSwitchRangeToICmp(true)));

    GlobalCleanupPM.addPass(air::Lower16BitTexReadPass());
    GlobalCleanupPM.addPass(air::SimdgroupImplicitMemBarrierPass());
#if DXMT_SHADER_CLEANUP
    GlobalCleanupPM.addPass(LateHalfwordCSEPass());
#endif

    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(GlobalCleanupPM), true /* ? */));
  }

  // MPM.addPass(VerifierPass());

  // Optimize the IR!
  MPM.run(M, MAM);
}

void
removeNamedMetadata(llvm::Module &M, StringRef Name) {
  if (auto MD = M.getNamedMetadata(Name)) {
    M.eraseNamedMetadata(MD);
  }
}

template<size_t N>
void linkShader(llvm::Module &M, const unsigned char (&bitcode)[N]) {
  auto buffer = MemoryBuffer::getMemBufferCopy(StringRef((const char *)bitcode, N));
  Expected<std::unique_ptr<Module>> modOrErr = parseBitcodeFile(buffer->getMemBufferRef(), M.getContext());

  if (!modOrErr) {
    // not expected to see this unless something really bad happened in compile time
    errs() << "Failed to parse air bitcode\n";
    return;
  }

  auto module = std::move(modOrErr.get());

  removeNamedMetadata(*module, "air.compile_options");
  removeNamedMetadata(*module, "air.version");
  removeNamedMetadata(*module, "air.language_version");
  removeNamedMetadata(*module, "air.source_file_name");
  removeNamedMetadata(*module, "llvm.ident");
  removeNamedMetadata(*module, "llvm.module.flags");

  llvm::Linker::linkModules(M, std::move(module), Linker::LinkOnlyNeeded);
};

void
linkMSAD(llvm::Module &M) {
  linkShader(M, air_msad);
}

void
linkSamplePos(llvm::Module &M) {
  linkShader(M, air_samplepos);
}

void
linkTessellation(llvm::Module &M) {
  linkShader(M, air_tessellation);
}

} // namespace dxmt

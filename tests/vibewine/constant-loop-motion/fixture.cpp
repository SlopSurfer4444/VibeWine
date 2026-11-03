// SPDX-License-Identifier: LGPL-2.1-or-later
// Actual production admission/pass and pipeline on retained produced AIR.
#include "airconv_context.hpp"
#include "transforms/constant_loop_motion.hpp"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace llvm;
static void check(bool ok, const std::string &why) {
  if (!ok) { std::cerr << "FAIL " << why << '\n'; std::exit(3); }
}
static std::string snapshot(Module &M) { std::string s; raw_string_ostream out(s); M.print(out,nullptr); return s; }
static std::string replaced(std::string s, const std::string &old, const std::string &value) {
  size_t pos=0, count=0;
  while ((pos=s.find(old,pos))!=std::string::npos) { s.replace(pos,old.size(),value); pos+=value.size(); ++count; }
  check(count>0,"mutation anchor "+old); return s;
}
static std::unique_ptr<Module> parse(LLVMContext &C,const std::string &text) {
  SMDiagnostic error; auto M=parseAssemblyString(text,error,C);
  if (!M) error.print("constant-loop-motion",errs());
  check(bool(M),"input parse"); check(!verifyModule(*M,&errs()),"input verifier"); return M;
}
struct Counts { unsigned loads=0,calls=0,loops=0; };
static Counts counts(Module &M) {
  Counts result;
  for (auto &F:M) if (!F.isDeclaration()) {
    DominatorTree DT(F); LoopInfo LI(DT); result.loops+=LI.getLoopsInPreorder().size();
    for (auto &BB:F) if (LI.getLoopFor(&BB)) for (auto &I:BB) {
      result.loads+=isa<LoadInst>(I); result.calls+=isa<CallBase>(I);
    }
  }
  return result;
}
static std::vector<unsigned> fpFlags(Module &M) {
  std::vector<unsigned> result;
  for (auto &F:M) for (auto &BB:F) for (auto &I:BB) if (isa<FPMathOperator>(I)) {
    auto f=I.getFastMathFlags();
    result.push_back(f.allowReassoc() | (f.noNaNs()<<1) | (f.noInfs()<<2) | (f.noSignedZeros()<<3) |
                     (f.allowReciprocal()<<4) | (f.allowContract()<<5) | (f.approxFunc()<<6));
  }
  std::sort(result.begin(),result.end()); return result;
}
static std::map<std::string,std::string> declarations(Module &M) {
  std::map<std::string,std::string> result;
  for (auto &F:M) if (F.isDeclaration()) { std::string s; raw_string_ostream out(s); F.print(out); result[F.getName().str()]=s; }
  return result;
}
static void apply(Module &M) {
  LoopAnalysisManager LAM; FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM; ModuleAnalysisManager MAM; PassBuilder PB;
  PB.registerModuleAnalyses(MAM); PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM); PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM,FAM,CGAM,MAM);
  ModulePassManager PM; PM.addPass(createModuleToFunctionPassAdaptor(air::ConstantLoopMotionPass())); PM.run(M,MAM);
  check(!verifyModule(M,&errs()),"output verifier");
}
static void save(const std::string &name,Module &M) { std::ofstream out(name,std::ios::binary); out<<snapshot(M); check(out.good(),"save IR"); }
int main(int argc,char **argv) {
  check(argc==4,"arguments: heavy.ll on|off output-prefix");
  std::ifstream input(argv[1],std::ios::binary);
  std::string original((std::istreambuf_iterator<char>(input)),{}); check(!original.empty(),"fixture input");
  bool enabled=std::string(argv[2])=="on"; std::string output=argv[3];
  {
    LLVMContext C; C.setOpaquePointers(false); auto M=parse(C,original);
    auto before=counts(*M); auto flags=fpFlags(*M); auto attrs=declarations(*M);
    check(before.loads==2 && before.calls==10 && before.loops==1,"heavy input counts");
    apply(*M); auto after=counts(*M);
    check(after.loads==0 && after.calls==8 && after.loops==1,"heavy direct hoist");
    check(fpFlags(*M)==flags && declarations(*M)==attrs,"heavy flags/declarations preserved");
    auto &entry=M->getFunction("shader_main")->getEntryBlock(); unsigned loads=0,trig=0;
    for (auto &I:entry) { loads+=isa<LoadInst>(I); if (auto *Call=dyn_cast<CallInst>(&I)) {
      auto name=Call->getCalledFunction()->getName(); trig+=name=="air.fast_cos.f32" || name=="air.fast_sin.f32";
    } }
    check(loads==4 && trig==2,"heavy chain in entry"); save(output+".direct.ll",*M);
  }
  {
    LLVMContext C; C.setOpaquePointers(false); auto M=parse(C,original);
    FastMathFlags FMF; FMF.setAllowContract(); FMF.setAllowReassoc(); FMF.setNoSignedZeros();
    for (auto &F:*M) for (auto &BB:F) for (auto &I:BB) if (isa<FPMathOperator>(I)) I.setFastMathFlags(FMF);
    auto flags=fpFlags(*M); apply(*M); auto after=counts(*M);
    check(after.loads==0 && after.calls==8 && fpFlags(*M)==flags,"explicit FP flags preserved");
  }
  {
    LLVMContext C; C.setOpaquePointers(false);
    auto text=replaced(original,"%.sroa.82.0 = phi i32 [ 0, %entry ]","%.sroa.82.0 = phi i32 [ 96, %entry ]");
    auto M=parse(C,text); auto flags=fpFlags(*M); apply(*M); auto after=counts(*M);
    check(after.loads==2 && after.calls==10,"zero-trip motion safety");
    check(fpFlags(*M)==flags,"zero-trip FP flags"); save(output+".zero-trip.ll",*M);
  }
  const std::string load="%34 = load float, float addrspace(2)* %33, align 4";
  auto inject=[&](const std::string &instruction,const std::string &declaration="") {
    return replaced(original,load,load+"\n  "+instruction)+"\n"+declaration+"\n";
  };
  std::vector<std::pair<std::string,std::string>> rejected={
    {"as1",replaced(original,"addrspace(2)","addrspace(1)")},
    {"as3",replaced(original,"addrspace(2)","addrspace(3)")},
    {"volatile",replaced(original,load,"%34 = load volatile float, float addrspace(2)* %33, align 4")},
    {"atomic-load",replaced(original,load,"%34 = load atomic float, float addrspace(2)* %33 monotonic, align 4")},
    {"store",inject("store float 0.000000e+00, float addrspace(2)* %33, align 4")},
    {"atomic-rmw",inject("%atomic = atomicrmw fadd float addrspace(2)* %33, float 1.000000e+00 monotonic")},
    {"convergent",replaced(original,"attributes #0 = {","attributes #0 = { convergent")},
    {"noduplicate",replaced(original,"attributes #0 = {","attributes #0 = { noduplicate")},
    {"strictfp-callee",replaced(original,"attributes #0 = {","attributes #0 = { strictfp")},
    {"unknown-call",replaced(original,"@air.fast_cos.f32","@probe_unknown_cos")},
    {"wrong-signature",replaced(original,"@air.fast_cos.f32","@air.fast_cos.v2f32")},
    {"scalable-vector",inject("%scalable = call <vscale x 2 x float> @air.sin.f32(<vscale x 2 x float> zeroinitializer)", "declare <vscale x 2 x float> @air.sin.f32(<vscale x 2 x float>) #0")},
    {"derivative",replaced(original,"@air.fast_cos.f32","@air.dfdx.f32")},
    {"barrier",inject("call void @air.simdgroup.barrier(i32 2, i32 1)","declare void @air.simdgroup.barrier(i32, i32) convergent nounwind willreturn")},
    {"discard",inject("call void @air.discard_fragment()","declare void @air.discard_fragment() nounwind willreturn")},
    {"missing-readnone",replaced(original," readnone "," ")},
    {"missing-nosync",replaced(original," nosync "," ")},
    {"missing-willreturn",replaced(original," willreturn "," ")},
    {"vertex-stage",replaced(original,"!air.fragment =","!air.vertex =")},
    {"missing-stage",replaced(original,"!air.fragment =","!probe.unclassified =")},
    {"mixed-kernel",original+"\n!air.kernel = !{}\n"},
    {"strictfp",replaced(original,"local_unnamed_addr {","local_unnamed_addr strictfp {")},
    {"constrained-fp",inject("%constrained = call float @llvm.experimental.constrained.fadd.f32(float %34, float 0.000000e+00, metadata !\"round.dynamic\", metadata !\"fpexcept.strict\")", "declare float @llvm.experimental.constrained.fadd.f32(float, float, metadata, metadata)")},
  };
  for (auto &[name,text]:rejected) {
    LLVMContext C; C.setOpaquePointers(false); auto M=parse(C,text); auto before=snapshot(*M);
    apply(*M); check(snapshot(*M)==before,"rejected-"+name+" changed IR");
    save(output+".rejected-"+name+".ll",*M);
  }
  // A non-entry helper in a fragment module must not inherit its stage.
  {
    LLVMContext C; C.setOpaquePointers(false); auto M=parse(C,original);
    auto *F=M->getFunction("shader_main");
    auto *other=Function::Create(F->getFunctionType(),Function::ExternalLinkage,"other_entry",*M);
    M->getNamedMetadata("air.fragment")->getOperand(0)->replaceOperandWith(0,ValueAsMetadata::get(other));
    check(!verifyModule(*M,&errs()),"other stage input verifier"); auto before=snapshot(*M);
    apply(*M); check(snapshot(*M)==before,"metadata identity gate");
  }
  {
    LLVMContext C; C.setOpaquePointers(false); auto M=parse(C,original);
    dxmt::runOptimizationPasses(*M); check(!verifyModule(*M,&errs()),"full pipeline verifier");
    auto after=counts(*M);
    check(after.loads==(enabled?0u:2u) && after.calls==(enabled?8u:10u),"full pipeline flag wiring");
    save(output+".full.ll",*M);
  }
  std::cout<<"PASS heavy 2->0 loads/10->8 calls, zero-trip, "<<rejected.size()
           <<" unchanged rejection modules, metadata identity, FP flags/declarations, actual full pipeline "
           <<(enabled?"on":"off")<<'\n';
}

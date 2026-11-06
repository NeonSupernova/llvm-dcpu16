// Validate memset representability and its real MemCpyOpt consumer.
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include <cstdlib>
using namespace llvm;
static unsigned Checks;
static void check(bool OK, const char *Message) {
  ++Checks;
  if (!OK) {
    errs() << "FAIL: " << Message << '\n';
    std::exit(1);
  }
}
static void pattern(Value *V, const DataLayout &DL, int Expected) {
  auto *Result = isBytewiseValue(V, DL);
  auto *CI = dyn_cast_or_null<ConstantInt>(Result);
  check(Expected < 0 ? !Result
                     : CI && CI->getType()->isIntegerTy(8) &&
                           CI->getZExtValue() == unsigned(Expected),
        "memset pattern classification");
}
static void copyCase(StringRef Layout, unsigned Unit, unsigned Word,
                     bool CanFill) {
  LLVMContext C;
  SMDiagnostic D;
  auto M = parseAssemblyString(
      "target datalayout = \"" + Layout.str() +
          "\"\n"
          "@source = constant [4 x i16] [i16 " +
          std::to_string(Word) + ",i16 " + std::to_string(Word) + ",i16 " +
          std::to_string(Word) + ",i16 " + std::to_string(Word) +
          "], align 1\n"
          "declare void @llvm.memcpy.p0.p0.i16(ptr,ptr,i16,i1)\n"
          "define i64 @entry(ptr %p) {\ncall void @llvm.memcpy.p0.p0.i16(ptr "
          "align 1 %p,ptr align 1 @source,i16 " +
          std::to_string(64 / Unit) +
          ",i1 false)\n%r = load i64,ptr %p,align 1\nret i64 %r }",
      D, C);
  if (!M) {
    D.print("bytewise-memory", errs());
    std::exit(1);
  }
  auto *F = M->getFunction("entry");
  {
    FunctionAnalysisManager AM;
    AM.registerPass([] { return PassInstrumentationAnalysis(); });
    AM.registerPass([] { return DominatorTreeAnalysis(); });
    AM.registerPass([] { return PostDominatorTreeAnalysis(); });
    AM.registerPass([] { return AssumptionAnalysis(); });
    AM.registerPass([] { return TargetIRAnalysis(); });
    AM.registerPass([] { return TargetLibraryAnalysis(); });
    AM.registerPass([] { return BasicAA(); });
    AM.registerPass([] {
      AAManager AA;
      AA.registerFunctionAnalysis<BasicAA>();
      return AA;
    });
    AM.registerPass([] { return MemorySSAAnalysis(); });
    MemCpyOptPass().run(*F, AM);
    AM.getResult<MemorySSAAnalysis>(*F).getMSSA().verifyMemorySSA();
    check(!verifyModule(*M, &errs()), "MemCpyOpt IR verifies");
    unsigned Sets = 0;
    for (auto &BB : *F)
      for (auto &I : BB)
        if (isa<MemSetInst>(I))
          ++Sets;
    check(bool(Sets) == CanFill,
          "MemCpyOpt only emits representable native fills");
  }
  legacy::PassManager PM;
  PM.add(createInstructionCombiningPass());
  PM.add(createGVNPass());
  PM.run(*M);
  check(!verifyModule(*M, &errs()), "MemCpyOpt/InstCombine/GVN IR verifies");
  auto *Ret = cast<ReturnInst>(F->back().getTerminator());
  auto *Value = dyn_cast<ConstantInt>(Ret->getReturnValue());
  uint64_t Expected = uint64_t(Word) | uint64_t(Word) << 16 |
                      uint64_t(Word) << 32 | uint64_t(Word) << 48;
  if (!Value || Value->getZExtValue() != Expected) {
    F->print(errs());
    check(false, "copy to fill optimization preserves every original word bit");
  }
  check(true, "copy to fill optimization preserves every original word bit");
}
int main() {
  auto &R = *PassRegistry::getPassRegistry();
  initializeCore(R);
  initializeAnalysis(R);
  initializeTransformUtils(R);
  initializeScalarOpts(R);
  initializeInstCombine(R);
  LLVMContext C;
  for (unsigned Unit : {16u, 8u})
    for (bool Big : {false, true}) {
      std::string Layout = (Big ? "E-B" : "e-B") + std::to_string(Unit) +
                           "-p:16:16-i8:" + std::to_string(Unit) +
                           "-i16:16-i32:16-i64:16-a:" + std::to_string(Unit) +
                           (Unit == 16 ? "-n16-S16" : "-n8:16-S16");
      DataLayout DL(Layout);
      for (unsigned Width : {8u, 16u, 32u, 64u, 128u}) {
        for (unsigned Fill : {0u, 0x80u, 0xabu, 0xffu}) {
          APInt Bits(Width, 0);
          for (unsigned i = 0; i < Width; i += Unit)
            Bits |= APInt(Width, Fill) << i;
          pattern(ConstantInt::get(C, Bits), DL, Fill);
        }
        pattern(ConstantInt::get(C, APInt::getAllOnes(Width)), DL,
                Unit == 8 || Width == 8 ? 255 : -1);
      }
      pattern(ConstantInt::get(C, APInt(16, 0x00ff)), DL,
              Unit == 16 ? 255 : -1);
      pattern(ConstantInt::get(C, APInt(16, 0xabab)), DL,
              Unit == 8 ? 0xab : -1);
      pattern(ConstantInt::get(C, APInt(24, 0xababab)), DL,
              Unit == 8 ? 0xab : -1);
      pattern(ConstantDataArray::get(C, ArrayRef<uint8_t>({0xff, 0xff})), DL,
              255);
      pattern(ConstantDataArray::get(C, ArrayRef<uint16_t>({0x00ff, 0x00ff})),
              DL, Unit == 16 ? 255 : -1);
      pattern(ConstantDataVector::get(C, ArrayRef<uint8_t>({0xff, 0xff})), DL,
              Unit == 8 ? 255 : -1);
      pattern(ConstantDataVector::get(C, ArrayRef<uint8_t>({0, 0})), DL, 0);
      pattern(ConstantDataVector::get(C, ArrayRef<uint16_t>({0x00ff, 0x00ff})),
              DL, Unit == 16 ? 255 : -1);
      auto *U = isBytewiseValue(UndefValue::get(Type::getInt32Ty(C)), DL);
      check(U && isa<UndefValue>(U) && U->getType()->isIntegerTy(8),
            "undef pattern retains i8 API");
      Module Arguments("args", C);
      Arguments.setDataLayout(DL);
      auto *F = Function::Create(
          FunctionType::get(Type::getVoidTy(C),
                            {Type::getInt8Ty(C), Type::getInt16Ty(C)}, false),
          GlobalValue::ExternalLinkage, "args", Arguments);
      check(isBytewiseValue(F->getArg(0), DL) == F->getArg(0),
            "variable i8 fills retain their value");
      check(!isBytewiseValue(F->getArg(1), DL),
            "unknown i16 is not assumed representable");
      auto *P = ConstantExpr::getIntToPtr(
          ConstantInt::get(Type::getInt16Ty(C), 0xffff),
          PointerType::get(C, 0));
      pattern(P, DL, Unit == 8 ? 255 : -1);
      for (unsigned Word :
           {0xffffu, 0x00ffu, 0xababu, 0x0080u, 0x0000u, 0x1234u}) {
        bool CanFill = Unit == 16 ? Word <= 255 : (Word & 255) == (Word >> 8);
        copyCase(Layout, Unit, Word, CanFill);
      }
    }
  outs() << "Passed " << Checks << " bytewise/MemCpyOpt checks\n";
}

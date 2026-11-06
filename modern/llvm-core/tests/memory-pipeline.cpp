// Real InstCombine/GVN integration checks for addressable-unit memory
// semantics.
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <cstdlib>
#include <string>
using namespace llvm;
static unsigned Checks;
static void test(StringRef Name, StringRef Layout, StringRef Body,
                 const APInt *Expected, bool Combine) {
  LLVMContext C;
  SMDiagnostic D;
  auto M = parseAssemblyString(
      "target datalayout = \"" + Layout.str() + "\"\n" + Body.str(), D, C);
  if (!M) {
    D.print("memory-pipeline", errs());
    std::exit(1);
  }
  legacy::PassManager PM;
  if (Combine)
    PM.add(createInstructionCombiningPass());
  PM.add(createGVNPass());
  PM.run(*M);
  if (verifyModule(*M, &errs()))
    std::exit(1);
  ++Checks;
  auto *F = M->getFunction("entry");
  auto *Ret = dyn_cast<ReturnInst>(F->back().getTerminator());
  auto *Value =
      Ret ? dyn_cast_or_null<ConstantInt>(Ret->getReturnValue()) : nullptr;
  if ((Expected && (!Value || Value->getValue() != *Expected)) ||
      (!Expected && Value)) {
    errs() << "FAIL " << Name << " combine=" << Combine << " " << Layout
           << '\n';
    F->print(errs());
    std::exit(1);
  }
}
int main() {
  auto &R = *PassRegistry::getPassRegistry();
  initializeCore(R);
  initializeAnalysis(R);
  initializeTransformUtils(R);
  initializeScalarOpts(R);
  initializeInstCombine(R);
  for (bool Combine : {false, true})
    for (unsigned Unit : {16u, 8u})
      for (bool Big : {false, true}) {
        std::string Layout = (Big ? "E-B" : "e-B") + std::to_string(Unit) +
                             "-p:16:16-i8:" + std::to_string(Unit) +
                             "-i16:16-i32:16-i64:16-a:" + std::to_string(Unit) +
                             (Unit == 16 ? "-n16-S16" : "-n8:16-S16");
        for (unsigned Len : {1u, 2u, 4u, 8u}) {
          unsigned Width = Len * Unit;
          std::string W = std::to_string(Width), L = std::to_string(Len);
          for (unsigned Fill : {0u, 0x80u, 0xabu, 0xffu}) {
            APInt Expected(Width, 0);
            for (unsigned i = 0; i < Len; ++i)
              Expected |= APInt(Width, Fill) << (i * Unit);
            test("fill", Layout,
                 "declare void @llvm.memset.p0.i16(ptr,i8,i16,i1)\n"
                 "define i" +
                     W +
                     " @entry(ptr %p) {\n"
                     "call void @llvm.memset.p0.i16(ptr align 1 %p, i8 " +
                     std::to_string(Fill) + ", i16 " + L +
                     ", i1 false)\n"
                     "%r = load i" +
                     W + ", ptr %p, align 1\nret i" + W + " %r }",
                 &Expected, Combine);
          }
          const uint16_t Words[] = {0x1122, 0x3344, 0x5566, 0x7788,
                                    0x99aa, 0xbbcc, 0xddee, 0xff00};
          APInt Expected(Width, 0);
          for (unsigned i = 0; i < Len; ++i) {
            uint64_t Value =
                Unit == 16
                    ? Words[i]
                    : (Words[i / 2] >> (Big ? 8 * (1 - i % 2) : 8 * (i % 2))) &
                          255;
            Expected |= APInt(Width, Value) << ((Big ? Len - 1 - i : i) * Unit);
          }
          for (StringRef Operation : {"memcpy", "memmove"}) {
            std::string Op = Operation.str();
            test(Op, Layout,
                 "@source = constant [8 x i16] [i16 4386,i16 13124,i16 "
                 "21862,i16 30600,i16 39338,i16 48076,i16 56814,i16 65280], "
                 "align 1\n"
                 "declare void @llvm." +
                     Op +
                     ".p0.p0.i16(ptr,ptr,i16,i1)\n"
                     "define i" +
                     W +
                     " @entry(ptr %p) {\n"
                     "call void @llvm." +
                     Op + ".p0.p0.i16(ptr align 1 %p,ptr align 1 @source,i16 " +
                     L +
                     ",i1 false)\n"
                     "%r = load i" +
                     W + ",ptr %p,align 1\nret i" + W + " %r }",
                 &Expected, Combine);
          }
        }
        APInt Extracted(16, Big ? 0x3344 : 0x1122);
        test("extract", Layout, R"(
      define i16 @entry(ptr %p) {
        store i32 287454020,ptr %p,align 1
        %q = getelementptr i16,ptr %p,i16 1
        %r = load i16,ptr %q,align 1
        ret i16 %r
      })",
             &Extracted, Combine);
        APInt Filled(16, Unit == 16 ? 0x00ab : 0xabab);
        std::string Prefix =
            "declare void @llvm.memset.p0.i16(ptr,i8,i16,i1)\n"
            "define i16 @entry(ptr %p) {\n"
            "call void @llvm.memset.p0.i16(ptr align 1 %p,i8 171,i16 " +
            std::to_string(32 / Unit) + ",i1 false)\n";
        test("alias-bounds", Layout, Prefix + R"(
      store i16 43981,ptr %p,align 1
      %q = getelementptr i16,ptr %p,i16 1
      %r = load i16,ptr %q,align 1
      ret i16 %r
    })",
             &Filled, Combine);
        test("outside-write", Layout, Prefix + R"(
      %q = getelementptr i16,ptr %p,i16 2
      %r = load i16,ptr %q,align 1
      ret i16 %r
    })",
             nullptr, Combine);
      }
  outs() << "Passed " << Checks << " memory-pipeline cases\n";
}

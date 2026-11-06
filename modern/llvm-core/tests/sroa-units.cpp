// Native-unit SROA regression cases. Validate the pass's scalar return values,
// not merely that the resulting IR parses. No target machine code is emitted.
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
using namespace llvm;
static unsigned Checks = 0;
static void test(StringRef Name, StringRef Layout, StringRef Body,
                 uint64_t Expected) {
  LLVMContext C;
  SMDiagnostic D;
  std::string Source = Layout.empty() ? Body.str()
                                      : "target datalayout = \"" +
                                            Layout.str() + "\"\n" + Body.str();
  auto M = parseAssemblyString(Source, D, C);
  if (!M) {
    D.print("sroa-units", errs());
    std::exit(1);
  }
  FunctionAnalysisManager AM;
  AM.registerPass([] { return PassInstrumentationAnalysis(); });
  AM.registerPass([] { return DominatorTreeAnalysis(); });
  AM.registerPass([] { return AssumptionAnalysis(); });
  AM.registerPass([] { return TargetIRAnalysis(); });
  auto *F = M->getFunction("entry");
  if (!F)
    F = M->getFunction("extract");
  if (!F) {
    errs() << "missing regression function\n";
    std::exit(1);
  }
  SROAPass(SROAOptions()).run(*F, AM);
  // SROA can build arithmetic before promoted loads become constants. Fold
  // only constant instructions here, not another unaudited memory pass.
  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (auto &BB : *F)
      for (auto &I : BB)
        if (!I.use_empty())
          if (auto *K = ConstantFoldInstruction(&I, M->getDataLayout())) {
            I.replaceAllUsesWith(K);
            Changed = true;
          }
  }
  if (verifyModule(*M, &errs()))
    std::exit(1);
  ++Checks;
  auto *Ret = dyn_cast<ReturnInst>(F->back().getTerminator());
  auto *Value =
      Ret ? dyn_cast_or_null<ConstantInt>(Ret->getReturnValue()) : nullptr;
  if (!Value || Value->getZExtValue() != Expected) {
    errs() << "FAIL " << Name << " (" << Layout << "): expected " << Expected
           << '\n';
    F->print(errs());
    std::exit(1);
  }
}
int main(int argc, char **argv) {
  for (unsigned Unit : {16u, 8u})
    for (bool Big : {false, true}) {
      std::string Layout = (Big ? "E-B" : "e-B") + std::to_string(Unit) +
                           "-p:16:16-i8:" + std::to_string(Unit) +
                           "-i16:16-i32:16-i64:16-a:" + std::to_string(Unit) +
                           (Unit == 16 ? "-n16-S16" : "-n8:16-S16");
      for (unsigned Width : {32u, 64u}) {
        uint64_t Value = Width == 32 ? 0x11223344ULL : 0x1122334455667788ULL;
        for (unsigned Index = 0; Index < Width / 16; ++Index) {
          unsigned Shift = 16 * (Big ? Width / 16 - Index - 1 : Index);
          std::string W = std::to_string(Width), I = std::to_string(Index);
          test("extract", Layout,
               "define i16 @entry() { %p = alloca i" + W +
                   ", align 1\n"
                   "store i" +
                   W + " " + std::to_string(Value) +
                   ", ptr %p, align 1\n"
                   "%q = getelementptr i16, ptr %p, i16 " +
                   I +
                   "\n"
                   "%v = load i16, ptr %q, align 1\nret i16 %v }",
               (Value >> Shift) & 65535);
          uint64_t Expected =
              (Value & ~(0xffffULL << Shift)) | (0xabcdULL << Shift);
          test("insert", Layout,
               "define i" + W + " @entry() { %p = alloca i" + W +
                   ", align 1\n"
                   "store i" +
                   W + " " + std::to_string(Value) +
                   ", ptr %p, align 1\n"
                   "%q = getelementptr i16, ptr %p, i16 " +
                   I +
                   "\n"
                   "store i16 43981, ptr %q, align 1\n"
                   "%v = load i" +
                   W + ", ptr %p, align 1\nret i" + W + " %v }",
               Expected);
        }
        unsigned Units = Width / Unit;
        for (unsigned Fill : {0u, 0x80u, 0xabu, 0xffu}) {
          uint64_t Expected = 0;
          for (unsigned i = 0; i < Units; ++i)
            Expected |= uint64_t(Fill) << (i * Unit);
          std::string W = std::to_string(Width);
          test("memset", Layout,
               "declare void @llvm.memset.p0.i16(ptr, i8, i16, i1)\n"
               "define i" +
                   W + " @entry() { %p = alloca i" + W +
                   ", align 1\n"
                   "call void @llvm.memset.p0.i16(ptr align 1 %p, i8 " +
                   std::to_string(Fill) + ", i16 " + std::to_string(Units) +
                   ", i1 false)\n"
                   "%v = load i" +
                   W + ", ptr %p, align 1\nret i" + W + " %v }",
               Expected);
        }
      }

      test("struct-to-wide", Layout, R"(
      define i64 @entry() {
        %p = alloca {i16, i32, i16}, align 1
        %x = getelementptr {i16, i32, i16}, ptr %p, i16 0, i32 1
        %y = getelementptr {i16, i32, i16}, ptr %p, i16 0, i32 2
        store i16 4386, ptr %p, align 1
        store i32 860116326, ptr %x, align 1
        store i16 30600, ptr %y, align 1
        %r = load i64, ptr %p, align 1
        ret i64 %r
      })",
           Big ? 0x1122334455667788ULL : 0x7788334455661122ULL);
      test("i8-array", Layout, R"(
      define i8 @entry() {
        %p = alloca [3 x i8], align 1
        %q = getelementptr [3 x i8], ptr %p, i16 0, i16 1
        store i8 17, ptr %p, align 1
        store i8 187, ptr %q, align 1
        %r = load i8, ptr %q, align 1
        ret i8 %r
      })",
           187);
      unsigned Offset = Unit == 16 ? 1 : 2;
      test("memcpy", Layout,
           "declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)\n"
           "define i16 @entry() { %a = alloca i32, align 1\n%b = alloca i32, "
           "align 1\n"
           "store i32 287454020, ptr %a, align 1\n"
           "call void @llvm.memcpy.p0.p0.i16(ptr align 1 %b, ptr align 1 %a, "
           "i16 " +
               std::to_string(32 / Unit) +
               ", i1 false)\n"
               "%q = getelementptr i8, ptr %b, i16 " +
               std::to_string(Offset) +
               "\n"
               "%r = load i16, ptr %q, align 1\nret i16 %r }",
           Big ? 0x3344 : 0x1122);

      test("mixed-padding", Layout, R"(
        define i16 @entry() {
          %p = alloca {i8, i16, i8}, align 1
          %q = getelementptr {i8, i16, i8}, ptr %p, i16 0, i32 1
          %z = getelementptr {i8, i16, i8}, ptr %p, i16 0, i32 2
          store i8 17, ptr %p, align 1
          store i16 43981, ptr %q, align 1
          store i8 34, ptr %z, align 1
          %r = load i16, ptr %q, align 1
          ret i16 %r
        })",
           0xabcd);
      test("packed-padding", Layout, R"(
        define i32 @entry() {
          %p = alloca <{i8, i32, i8}>, align 1
          %q = getelementptr <{i8, i32, i8}>, ptr %p, i16 0, i32 1
          %z = getelementptr <{i8, i32, i8}>, ptr %p, i16 0, i32 2
          store i8 17, ptr %p, align 1
          store i32 287454020, ptr %q, align 1
          store i8 34, ptr %z, align 1
          %r = load i32, ptr %q, align 1
          ret i32 %r
        })",
           0x11223344);
      test("i24-padding", Layout, R"(
        define i24 @entry() {
          %p = alloca [2 x i24], align 1
          %q = getelementptr [2 x i24], ptr %p, i16 0, i16 1
          store i24 11259375, ptr %q, align 1
          %r = load i24, ptr %q, align 1
          ret i24 %r
        })",
           0xabcdef);
      test("vector-to-scalar", Layout, R"(
        define i32 @entry() {
          %p = alloca <4 x i16>, align 1
          store <4 x i16> <i16 4386, i16 13124, i16 21862, i16 30600>, ptr %p, align 1
          %q = getelementptr i16, ptr %p, i16 1
          %r = load i32, ptr %q, align 1
          ret i32 %r
        })",
           Big ? 0x33445566 : 0x55663344);
      test("scalar-to-vector", Layout, R"(
        define i16 @entry() {
          %p = alloca [4 x i16], align 1
          store i64 1234605616436508552, ptr %p, align 1
          %v = load <4 x i16>, ptr %p, align 1
          %r = extractelement <4 x i16> %v, i32 2
          ret i16 %r
        })",
           Big ? 0x5566 : 0x3344);
      test("partial-copy", Layout,
           "declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)\n"
           "define i16 @entry() { %p = alloca i32, align 1\n%q = alloca i16, "
           "align 1\n"
           "store i32 287454020, ptr %p, align 1\nstore i16 21862, ptr %q, "
           "align 1\n"
           "call void @llvm.memcpy.p0.p0.i16(ptr align 1 %q, ptr align 1 %p, "
           "i16 1, i1 false)\n"
           "%r = load i16, ptr %q, align 1\nret i16 %r }",
           Unit == 16 ? (Big ? 0x1122 : 0x3344) : (Big ? 0x1166 : 0x5544));
      uint64_t FilledWord = Unit == 16 ? 0x00ab : 0xabab;
      test("partial-fill", Layout,
           "declare void @llvm.memset.p0.i16(ptr, i8, i16, i1)\n"
           "define i32 @entry() { %p = alloca i32, align 1\nstore i32 "
           "287454020, ptr %p, align 1\n"
           "%q = getelementptr i16, ptr %p, i16 1\n"
           "call void @llvm.memset.p0.i16(ptr align 1 %q, i8 171, i16 " +
               std::to_string(16 / Unit) +
               ", i1 false)\n"
               "%r = load i32, ptr %p, align 1\nret i32 %r }",
           Big ? 0x11220000 | FilledWord : FilledWord << 16 | 0x3344);
    }
  if (argc == 2) {
    std::ifstream Input(argv[1]);
    if (!Input) {
      errs() << "cannot read regression fixture\n";
      return 1;
    }
    std::string Source((std::istreambuf_iterator<char>(Input)),
                       std::istreambuf_iterator<char>());
    test("original-reproducer", "", Source, 0x1122);
  }
  outs() << "Passed " << Checks << " SROA cases\n";
}

// Exercise real DSE, its memory analyses, and downstream constant forwarding.
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include <cstdlib>
using namespace llvm;
static unsigned Checks;
static void check(bool OK, StringRef Message, Function *F = nullptr) {
  ++Checks;
  if (!OK) {
    errs() << "FAIL: " << Message << '\n';
    if (F)
      F->print(errs());
    std::exit(1);
  }
}
static std::string number(const APInt &V) {
  SmallString<48> S;
  V.toStringUnsigned(S);
  return S.str().str();
}
static std::unique_ptr<Module> parse(LLVMContext &C, StringRef Layout,
                                     const std::string &Body) {
  SMDiagnostic D;
  auto M = parseAssemblyString(
      "target datalayout = \"" + Layout.str() + "\"\n" + Body, D, C);
  if (!M) {
    D.print("dse-units", errs());
    std::exit(1);
  }
  check(!verifyModule(*M, &errs()), "input IR verifies");
  return M;
}
static void dse(Function &F) {
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
  AM.registerPass([] { return CycleAnalysis(); });
  DSEPass().run(F, AM);
  AM.getResult<MemorySSAAnalysis>(F).getMSSA().verifyMemorySSA();
  check(!verifyModule(*F.getParent(), &errs()), "DSE IR verifies", &F);
}
static void result(Module &M, const APInt &Expected) {
  legacy::PassManager PM;
  PM.add(createGVNPass());
  PM.run(M);
  auto *F = M.getFunction("entry");
  check(!verifyModule(M, &errs()), "DSE/GVN IR verifies", F);
  auto *R = cast<ReturnInst>(F->back().getTerminator());
  auto *K = dyn_cast<ConstantInt>(R->getReturnValue());
  check(K && K->getValue() == Expected, "DSE/GVN scalar result", F);
}
// Mode 0: simple, 1: matching unordered atomic, 2: volatile killer,
// 3: ordered atomic killer. Only the first two may be merged.
static void mergeCase(StringRef Layout, unsigned Unit, bool Big, unsigned Width,
                      unsigned Narrow, unsigned Offset, int Base,
                      unsigned Mode) {
  LLVMContext C;
  APInt Before(Width, 0);
  for (unsigned I = 0; I < Width; I += 8)
    Before |= APInt(Width, (0x11 + I) & 255) << I;
  APInt Kill = APInt(32, 0xabcd1234).trunc(Narrow);
  unsigned Shift = Big ? Width - Offset * Unit - Narrow : Offset * Unit;
  APInt Expected = (Before & ~APInt::getBitsSet(Width, Shift, Shift + Narrow)) |
                   (Kill.zext(Width) << Shift);
  std::string W = std::to_string(Width), N = std::to_string(Narrow);
  std::string Dead = Mode == 1 ? "atomic " : "";
  std::string Killing = Mode == 1 || Mode == 3 ? "atomic "
                        : Mode == 2            ? "volatile "
                                               : "";
  std::string Body =
      "define i" + W +
      " @entry(ptr %p) {\n"
      "%base = getelementptr i" +
      std::to_string(Unit) + ", ptr %p, i16 " + std::to_string(Base) +
      "\nstore " + Dead + "i" + W + " " + number(Before) + ", ptr %base" +
      (Mode == 1 ? " unordered" : "") +
      ", align 1\n"
      "%q = getelementptr i" +
      std::to_string(Unit) + ", ptr %base, i16 " + std::to_string(Offset) +
      "\nstore " + Killing + "i" + N + " " + number(Kill) + ", ptr %q" +
      (Mode == 1   ? " unordered"
       : Mode == 3 ? " monotonic"
                   : "") +
      ", align 1\n%r = load i" + W + ", ptr %base, align 1\nret i" + W +
      " %r }";
  auto M = parse(C, Layout, Body);
  auto *F = M->getFunction("entry");
  dse(*F);
  unsigned Stores = 0;
  StoreInst *Remaining = nullptr;
  for (auto &BB : *F)
    for (auto &I : BB)
      if (auto *S = dyn_cast<StoreInst>(&I)) {
        ++Stores;
        Remaining = S;
      }
  bool Merge = Mode < 2;
  check(Stores == (Merge ? 1u : 2u), "merge only deletable matching stores", F);
  if (Merge) {
    auto *K = dyn_cast<ConstantInt>(Remaining->getValueOperand());
    check(K && K->getValue() == Expected,
          "merged store preserves each target unit", F);
    check(Remaining->isAtomic() == (Mode == 1),
          "merged store retains atomicity", F);
  }
  if (Mode == 0)
    result(*M, Expected);
}
// Padding and disjoint writes must not become a merged constant store.
static void preserveCase(StringRef Layout, unsigned Unit, bool Padding) {
  LLVMContext C;
  auto M = parse(
      C, Layout,
      "define void @entry(ptr %p) {\nstore i32 287454020,ptr %p,align 1\n"
      "%q = getelementptr i" +
          std::to_string(Unit) + ",ptr %p,i16 " +
          std::to_string(Padding ? 1 : 32 / Unit) + "\nstore i" +
          std::to_string(Padding ? 8 : 16) + " 127,ptr %q,align 1\nret void }");
  auto *F = M->getFunction("entry");
  dse(*F);
  unsigned Stores = 0;
  ConstantInt *Wide = nullptr;
  for (auto &BB : *F)
    for (auto &I : BB)
      if (auto *Store = dyn_cast<StoreInst>(&I)) {
        ++Stores;
        if (Store->getValueOperand()->getType()->isIntegerTy(32))
          Wide = dyn_cast<ConstantInt>(Store->getValueOperand());
      }
  check(Stores == 2, "padding/disjoint stores are preserved", F);
  check(Wide && Wide->equalsInt(287454020), "original wide store is unchanged",
        F);
}
static void debugCase(StringRef Layout, unsigned Unit, bool Big, bool KillEnd) {
  LLVMContext C;
  unsigned Width = Unit * 4, Narrow = Unit * 2;
  std::string W = std::to_string(Width), N = std::to_string(Narrow);
  std::string Body =
      "declare void @llvm.memset.p0.i16(ptr,i8,i16,i1)\n"
      "define i" +
      W +
      " @entry(ptr %p) !dbg !4 {\n"
      "call void @llvm.memset.p0.i16(ptr align 1 %p,i8 171,i16 4,i1 false), "
      "!DIAssignID !10, !dbg !9\n"
      "#dbg_assign(i" +
      W +
      " 0, !7, !DIExpression(), !10, ptr %p, !DIExpression(), !9)\n"
      "%q = getelementptr i" +
      std::to_string(Unit) + ", ptr %p, i16 " +
      std::to_string(KillEnd ? 2 : 0) + "\nstore i" + N +
      " 4660,ptr %q,align 1\n"
      "%tail = getelementptr i" +
      std::to_string(Unit) +
      ",ptr %p,i16 2\n"
      "%a = load i" +
      N + ",ptr %p,align 1\n%b = load i" + N +
      ",ptr %tail,align 1\n"
      "%aw = zext i" +
      N + " %a to i" + W + "\n%bw = zext i" + N + " %b to i" + W +
      "\n"
      "%upper = shl i" +
      W + (Big ? " %aw, " : " %bw, ") + N +
      "\n"
      "%r = or i" +
      W + (Big ? " %upper,%bw\n" : " %aw,%upper\n") + "ret i" + W +
      " %r }\n"
      "!llvm.module.flags = !{!0,!1}\n!llvm.dbg.cu = !{!2}\n"
      "!0 = !{i32 2, !\"Debug Info Version\", i32 3}\n"
      "!1 = !{i32 7, !\"debug-info-assignment-tracking\", i1 true}\n"
      "!2 = distinct !DICompileUnit(language: DW_LANG_C99, file: !3, producer: "
      "\"test\", isOptimized: true, runtimeVersion: 0, emissionKind: "
      "FullDebug)\n"
      "!3 = !DIFile(filename: \"test.c\", directory: \"/\")\n"
      "!4 = distinct !DISubprogram(name: \"entry\", scope: !3, file: !3, line: "
      "1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition | "
      "DISPFlagOptimized, unit: !2)\n"
      "!5 = !DISubroutineType(types: !6)\n!6 = !{}\n"
      "!7 = !DILocalVariable(name: \"buffer\", scope: !4, file: !3, line: 1, "
      "type: !8)\n"
      "!8 = !DIBasicType(name: \"buffer\", size: " +
      W +
      ", encoding: DW_ATE_unsigned)\n"
      "!9 = !DILocation(line: 1, column: 1, scope: !4)\n!10 = distinct "
      "!DIAssignID()\n";
  auto M = parse(C, Layout, Body);
  auto *F = M->getFunction("entry");
  dse(*F);
  MemSetInst *Set = nullptr;
  unsigned Sets = 0, Fragments = 0;
  for (auto &BB : *F)
    for (auto &I : BB) {
      if (auto *S = dyn_cast<MemSetInst>(&I)) {
        Set = S;
        ++Sets;
      }
      for (auto &DR : I.getDbgRecordRange())
        if (auto *V = dyn_cast<DbgVariableRecord>(&DR))
          if (V->isDbgAssign())
            if (auto Frag = V->getExpression()->getFragmentInfo()) {
              ++Fragments;
              check(V->isKillAddress() && Set &&
                        V->getAssignID() !=
                            Set->getMetadata(LLVMContext::MD_DIAssignID),
                    "dead fragment is unlinked with a killed address", F);
              check(Frag->OffsetInBits == (KillEnd ? Narrow : 0) &&
                        Frag->SizeInBits == Narrow,
                    "shortened assignment fragment uses native bits", F);
            }
    }
  check(Sets == 1 && cast<ConstantInt>(Set->getLength())->equalsInt(2),
        "memset is shortened by target units", F);
  check(Fragments == 1, "dead assignment gets exactly one fragment", F);
  APInt Expected(Width, 0);
  for (unsigned I = 0; I < Width; I += Unit)
    Expected |= APInt(Width, 171) << I;
  unsigned Shift = (KillEnd != Big) ? Narrow : 0;
  Expected &= ~APInt::getBitsSet(Width, Shift, Shift + Narrow);
  Expected |= APInt(Width, 4660) << Shift;
  result(*M, Expected);
}
int main() {
  auto &R = *PassRegistry::getPassRegistry();
  initializeCore(R);
  initializeAnalysis(R);
  initializeTransformUtils(R);
  initializeScalarOpts(R);
  for (unsigned Unit : {16u, 8u})
    for (bool Big : {false, true}) {
      std::string Layout = (Big ? "E-B" : "e-B") + std::to_string(Unit) +
                           "-p:16:16-i8:" + std::to_string(Unit) +
                           "-i16:16-i32:16-i64:16-a:" + std::to_string(Unit) +
                           (Unit == 16 ? "-n16-S16" : "-n8:16-S16");
      for (unsigned Width : {32u, 64u, 128u})
        for (unsigned Narrow : {16u, 32u}) {
          if (Narrow >= Width)
            continue;
          for (unsigned Offset = 0; Offset <= (Width - Narrow) / Unit; ++Offset)
            for (int Base : {0, -3})
              mergeCase(Layout, Unit, Big, Width, Narrow, Offset, Base, 0);
        }
      preserveCase(Layout, Unit, false);
      if (Unit == 16)
        preserveCase(Layout, Unit, true);
      else
        for (unsigned Offset : {1u, 3u})
          mergeCase(Layout, Unit, Big, 32, 8, Offset, 0, 0);
      for (unsigned Mode : {1u, 2u, 3u})
        mergeCase(Layout, Unit, Big, 32, 16, 16 / Unit, 0, Mode);
      for (bool End : {false, true})
        debugCase(Layout, Unit, Big, End);
    }
  outs() << "Passed " << Checks << " DSE checks\n";
}

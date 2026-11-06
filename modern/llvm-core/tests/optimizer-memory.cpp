// Exercise optimizer utilities and expanded native-unit loops independently of
// the DCPU backend. Only lowered scalar loops are executed with host pointers.
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"
#include "llvm/Transforms/Utils/VNCoercion.h"
#include <array>
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
static uint64_t integer(Value *V) {
  auto *C = dyn_cast_or_null<ConstantInt>(V);
  return C ? C->getZExtValue() : UINT64_MAX;
}
int main(int argc, char **argv) {
  LLVMContext C;
  if (argc < 2 || StringRef(argv[1]) != "loops") {
    auto M = std::make_unique<Module>("coercion", C);
    M->setDataLayout("e-B16-p:16:16-i8:16-i16:16-i32:16-i64:16-a:16-n16-S16");
    auto *I16 = Type::getInt16Ty(C), *I32 = Type::getInt32Ty(C);
    auto *F = Function::Create(
        FunctionType::get(I16, {PointerType::get(C, 0)}, false),
        GlobalValue::ExternalLinkage, "forward", *M);
    auto *BB = BasicBlock::Create(C, "entry", F);
    IRBuilder<> B(BB);
    Value *Wide = B.getInt32(0x11223344);
    auto *Store = B.CreateStore(Wide, F->getArg(0));
    Value *Next = B.CreateGEP(I16, F->getArg(0), B.getInt16(1));
    auto *Ret = B.CreateRet(B.getInt16(0));
    check(VNCoercion::analyzeLoadFromClobberingStore(I16, Next, Store,
                                                     M->getDataLayout()) == 1,
          "native forwarding offset counts words");
    check(integer(VNCoercion::getValueForLoad(Wide, 1, I16, Ret, F)) == 0x1122,
          "native forwarding extracts high word at offset one");
    Value *Past = GetElementPtrInst::Create(I16, F->getArg(0), {B.getInt16(2)},
                                            "past", Ret->getIterator());
    check(VNCoercion::analyzeLoadFromClobberingStore(I16, Past, Store,
                                                     M->getDataLayout()) == -1,
          "forwarding rejects a load beyond native store");
    M->setDataLayout("E-B16-p:16:16-i8:16-i16:16-i32:16");
    check(integer(VNCoercion::getValueForLoad(Wide, 1, I16, Ret, F)) == 0x3344,
          "big endian native forwarding extracts low word at offset one");
    M->setDataLayout("e-p:16:16-i16:16-i32:16");
    check(integer(VNCoercion::getValueForLoad(Wide, 1, I16, Ret, F)) == 0x2233,
          "octet forwarding remains unchanged");
    M->setDataLayout("e-B16-p:16:16-i16:16-i32:16");
    B.SetInsertPoint(Ret);
    auto *Fill = cast<MemIntrinsic>(
        B.CreateMemSet(F->getArg(0), B.getInt8(0xab), B.getInt16(2), Align(1)));
    check(VNCoercion::analyzeLoadFromClobberingMemInst(I32, F->getArg(0), Fill,
                                                       M->getDataLayout()) == 0,
          "native fill length counts complete words");
    check(integer(VNCoercion::getMemInstValueForLoad(
              Fill, 0, I32, Ret, M->getDataLayout())) == 0x00ab00ab,
          "native fill forwarding splats zero-extended i8 per word");
    check(integer(VNCoercion::getConstantMemInstValueForLoad(
              Fill, 0, I32, M->getDataLayout())) == 0x00ab00ab,
          "native constant fill forwarding agrees");
    M->setDataLayout("e-p:16:16-i16:16-i32:16");
    Fill->setLength(B.getInt16(4));
    check(integer(VNCoercion::getMemInstValueForLoad(
              Fill, 0, I32, Ret, M->getDataLayout())) == 0xabababab,
          "octet fill forwarding remains unchanged");
    check(!verifyModule(*M, &errs()), "forwarding IR verifies");
  }
  if (argc > 1 && StringRef(argv[1]) == "coercion") {
    outs() << "Passed " << Checks << " coercion checks\n";
    return 0;
  }
  SMDiagnostic D;
  auto Loops = parseAssemblyString(R"(
    target datalayout = "e-B16-p:16:16-i8:16-i16:16-i32:16-i64:16-a:16-n16-S16"
    declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)
    declare void @llvm.memmove.p0.p0.i16(ptr, ptr, i16, i1)
    declare void @llvm.memset.p0.i16(ptr, i8, i16, i1)
    define void @copy_known(ptr %d, ptr %s, i16 %n, i8 %v) {
      call void @llvm.memcpy.p0.p0.i16(ptr align 1 %d, ptr align 1 %s, i16 5, i1 false)
      ret void
    }
    define void @copy_dynamic(ptr %d, ptr %s, i16 %n, i8 %v) {
      call void @llvm.memcpy.p0.p0.i16(ptr align 1 %d, ptr align 1 %s, i16 %n, i1 false)
      ret void
    }
    define void @move_known(ptr %d, ptr %s, i16 %n, i8 %v) {
      call void @llvm.memmove.p0.p0.i16(ptr align 1 %d, ptr align 1 %s, i16 5, i1 false)
      ret void
    }
    define void @move_dynamic(ptr %d, ptr %s, i16 %n, i8 %v) {
      call void @llvm.memmove.p0.p0.i16(ptr align 1 %d, ptr align 1 %s, i16 %n, i1 false)
      ret void
    }
    define void @fill_known(ptr %d, ptr %s, i16 %n, i8 %v) {
      call void @llvm.memset.p0.i16(ptr align 1 %d, i8 %v, i16 5, i1 false)
      ret void
    }
    define void @fill_dynamic(ptr %d, ptr %s, i16 %n, i8 %v) {
      call void @llvm.memset.p0.i16(ptr align 1 %d, i8 %v, i16 %n, i1 false)
      ret void
    }
  )",
                                   D, C);
  if (!Loops) {
    D.print("optimizer-memory", errs());
    return 1;
  }
  auto Original = std::move(Loops);
  for (unsigned UnitBits : {16u, 8u}) {
    Loops = CloneModule(*Original);
    if (UnitBits == 8)
      Loops->setDataLayout("e-p:64:64-i64:64-n8:16:32:64");
    SmallVector<MemIntrinsic *> Intrinsics;
    for (auto &Fn : *Loops)
      for (auto &Block : Fn)
        for (auto &I : Block)
          if (auto *MI = dyn_cast<MemIntrinsic>(&I))
            Intrinsics.push_back(MI);
    TargetTransformInfo TTI(Loops->getDataLayout());
    for (auto *MI : Intrinsics) {
      if (auto *Copy = dyn_cast<MemCpyInst>(MI))
        expandMemCpyAsLoop(Copy, TTI);
      else if (auto *Move = dyn_cast<MemMoveInst>(MI))
        check(expandMemMoveAsLoop(Move, TTI), "memmove expansion succeeds");
      else
        expandMemSetAsLoop(cast<MemSetInst>(MI));
      MI->eraseFromParent();
    }
    check(!verifyModule(*Loops, &errs()), "native loop IR verifies");
    for (auto &Fn : *Loops)
      for (auto &Block : Fn)
        for (auto &I : Block) {
          if (auto *L = dyn_cast<LoadInst>(&I))
            check(L->getType()->isIntegerTy(UnitBits),
                  "copy reads full addressable units");
          if (auto *S = dyn_cast<StoreInst>(&I))
            check(S->getValueOperand()->getType()->isIntegerTy(UnitBits),
                  "loop writes full addressable units");
        }
    // After checking native IR, run only these explicit scalar loops with a
    // host layout. This tests loop semantics, not native code
    // generation/optimization.
    Loops->setDataLayout("e-p:64:64-i64:64-n8:16:32:64");
    // The upstream interpreter only accepts 64-bit GEP indices. Adapt those
    // indices after native validation; retain the original i16 loop arithmetic.
    for (auto &Fn : *Loops)
      for (auto &Block : Fn)
        for (auto &I : Block)
          if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
            IRBuilder<> Host(GEP);
            for (unsigned Index = 1; Index < GEP->getNumOperands(); ++Index)
              if (!GEP->getOperand(Index)->getType()->isIntegerTy(64))
                GEP->setOperand(Index, Host.CreateSExt(GEP->getOperand(Index),
                                                       Host.getInt64Ty()));
          }
    check(!verifyModule(*Loops, &errs()), "host-adapted loop IR verifies");
    std::string Error;
    auto *EE = EngineBuilder(std::move(Loops))
                   .setEngineKind(EngineKind::Interpreter)
                   .setErrorStr(&Error)
                   .create();
    check(EE != nullptr, Error.c_str());
    std::unique_ptr<ExecutionEngine> Engine(EE);
    auto ExecuteLoops = [&](auto InitialUnit) {
      using Unit = decltype(InitialUnit);
      for (StringRef Name : {"copy_known", "copy_dynamic", "move_known",
                             "move_dynamic", "fill_known", "fill_dynamic"}) {
        auto *Fn = Engine->FindFunctionNamed(Name.str().c_str());
        for (unsigned N : {0u, 1u, 5u, 9u}) {
          if (Name.ends_with("known") && N != 5)
            continue;
          for (auto Offsets :
               {std::pair<unsigned, unsigned>{2, 17}, {3, 2}, {2, 3}, {2, 2}}) {
            if (Name.starts_with("copy") &&
                Offsets != std::pair<unsigned, unsigned>{2, 17})
              continue;
            std::array<Unit, 32> Memory, Expected;
            for (unsigned i = 0; i < Memory.size(); ++i)
              Memory[i] = 0x8100 + i * 0x31;
            Expected = Memory;
            auto [Dst, Src] = Offsets;
            if (Name.starts_with("fill"))
              for (unsigned i = 0; i < N; ++i)
                Expected[Dst + i] = 0xab;
            else
              for (unsigned i = 0; i < N; ++i)
                Expected[Dst + i] = Memory[Src + i];
            SmallVector<GenericValue, 4> Args(4);
            Args[0].PointerVal = Memory.data() + Dst;
            Args[1].PointerVal = Memory.data() + Src;
            Args[2].IntVal = APInt(16, N);
            Args[3].IntVal = APInt(8, 0xab);
            Engine->runFunction(Fn, Args);
            check(Memory == Expected,
                  "expanded loop matches full-unit reference including "
                  "guards/overlap/zero length");
          }
        }
      }
    };
    if (UnitBits == 16)
      ExecuteLoops(uint16_t(0));
    else
      ExecuteLoops(uint8_t(0));
  }
  outs() << "Passed " << Checks << " optimizer/memory checks\n";
}

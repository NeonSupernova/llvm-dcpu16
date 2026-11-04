// Standalone DCPU-16 driver. Backend portions retain their original license.
#include "DCPU16.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"
#include <optional>

using namespace llvm;
extern "C" void LLVMInitializeDCPU16TargetInfo();
extern "C" void LLVMInitializeDCPU16Target();
extern "C" void LLVMInitializeDCPU16TargetMC();
extern "C" void LLVMInitializeDCPU16AsmPrinter();

static cl::opt<std::string> Input(cl::Positional, cl::desc("input LLVM IR"),
                                  cl::init("-"));
static cl::opt<std::string> Output("o", cl::desc("output assembly"),
                                   cl::init("-"));
static cl::opt<char> OptLevel("O", cl::Prefix,
                              cl::desc("optimization level (0, 1, 2, 3)"),
                              cl::init('2'));
static cl::opt<unsigned>
    ByteSize("byte-size", cl::desc("addressable byte size (currently 8)"),
             cl::init(8));

static bool supportedType(Type *T) {
  if (T->isFloatingPointTy() || T->isVectorTy())
    return false;
  if (auto *P = dyn_cast<PointerType>(T))
    return P->getAddressSpace() == 0;
  if (auto *A = dyn_cast<ArrayType>(T))
    return supportedType(A->getElementType());
  if (auto *S = dyn_cast<StructType>(T))
    for (auto *Element : S->elements())
      if (!supportedType(Element))
        return false;
  return true;
}

static bool validateInput(Module &M) {
  auto Error = [](StringRef Message) {
    errs() << "error: " << Message << '\n';
    return false;
  };
  for (auto &G : M.globals())
    if (!supportedType(G.getValueType()))
      return Error("floating-point, vector, and nonzero-address-space storage "
                   "is not yet supported");
  for (auto &F : M) {
    if (F.isDeclaration())
      continue;
    if (F.getCallingConv() != CallingConv::C &&
        F.getCallingConv() != CallingConv::Fast)
      return Error("this calling convention is not yet supported by the "
                   "standalone driver");
    if (F.isVarArg())
      return Error("variadic function definitions are not yet supported");
    if (!supportedType(F.getReturnType()))
      return Error("floating-point/vector return values are not yet supported");
    for (auto &Arg : F.args())
      if (!supportedType(Arg.getType()))
        return Error("unsupported argument type");
    for (auto &BB : F)
      for (auto &I : BB) {
        if (auto *MI = dyn_cast<MemIntrinsic>(&I))
          if (MI->isVolatile())
            return Error("volatile memory intrinsics are not yet supported");
        if (!supportedType(I.getType()))
          return Error("floating-point/vector operations and nonzero address "
                       "spaces are not yet supported");
        for (auto &Operand : I.operands())
          if (!supportedType(Operand->getType()))
            return Error("unsupported operand type");
        if (isa<AtomicRMWInst, AtomicCmpXchgInst, FenceInst>(I))
          return Error("atomic operations are not supported on DCPU16");
        if (auto *L = dyn_cast<LoadInst>(&I)) {
          if (L->isAtomic())
            return Error("atomic loads are not supported");
          if (L->isVolatile() &&
              (!L->getType()->isIntegerTy(16) || L->getAlign() < Align(2)))
            return Error("volatile memory requires aligned i16 accesses");
        }
        if (auto *S = dyn_cast<StoreInst>(&I)) {
          if (S->isAtomic())
            return Error("atomic stores are not supported");
          if (S->isVolatile() &&
              (!S->getValueOperand()->getType()->isIntegerTy(16) ||
               S->getAlign() < Align(2)))
            return Error("volatile memory requires aligned i16 accesses");
        }
        if (auto *A = dyn_cast<AllocaInst>(&I))
          if (!A->isStaticAlloca())
            return Error("dynamic stack allocation is not yet supported");
        if (isa<InvokeInst, ResumeInst, LandingPadInst, IndirectBrInst,
                CallBrInst>(I))
          return Error("exception handling and indirect branch instructions "
                       "are not yet supported");
      }
  }
  return true;
}

int main(int argc, char **argv) {
  InitLLVM Init(argc, argv);
  setBugReportMsg("Please report DCPU16 compiler failures at "
                  "https://github.com/NeonSupernova/llvm-dcpu16/issues/\n");
  cl::ParseCommandLineOptions(argc, argv, "Modern DCPU-16 LLVM compiler\n");
  if (OptLevel < '0' || OptLevel > '3') {
    errs() << "error: -O must be 0, 1, 2, or 3\n";
    return 1;
  }
  if (ByteSize != 8) {
    errs() << "error: non-octet byte ABIs require the LLVM-core memory-model "
              "port; only --byte-size=8 is supported\n";
    return 1;
  }
  LLVMInitializeDCPU16TargetInfo();
  LLVMInitializeDCPU16Target();
  LLVMInitializeDCPU16TargetMC();
  LLVMInitializeDCPU16AsmPrinter();
  LLVMContext Context;
  SMDiagnostic Diagnostic;
  auto M = parseIRFile(Input, Diagnostic, Context);
  if (!M) {
    Diagnostic.print(argv[0], errs());
    return 1;
  }
  if (verifyModule(*M, &errs()))
    return 1;
  if (!validateInput(*M))
    return 1;
  TargetOptions Options;
  auto TM =
      std::unique_ptr<TargetMachine>(getTheDCPU16Target().createTargetMachine(
          Triple("unknown-unknown-elf"), "generic", "", Options, Reloc::Static,
          CodeModel::Small,
          OptLevel == '0' ? CodeGenOptLevel::None : CodeGenOptLevel::Default));
  if (!TM) {
    errs() << "unable to create DCPU16 target\n";
    return 1;
  }
  if (!M->getDataLayoutStr().empty() &&
      M->getDataLayout() != TM->createDataLayout()) {
    errs() << "error: input data layout does not match the DCPU16 ABI\n";
    return 1;
  }
  M->setDataLayout(TM->createDataLayout());
  M->setTargetTriple(TM->getTargetTriple());
  // Assembly symbols denote hardware words. Keep every global symbol on a
  // word boundary; byte elements within an object remain densely packed.
  for (auto &Global : M->globals())
    Global.setAlignment(std::max(Global.getAlign().valueOrOne(), Align(2)));
  if (OptLevel != '0') {
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;
    PassBuilder PB(TM.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    auto Level = OptLevel == '1' ? OptimizationLevel::O1
                                 : (OptLevel == '3' ? OptimizationLevel::O3
                                                    : OptimizationLevel::O2);
    auto Pipeline = PB.buildPerModuleDefaultPipeline(Level);
    Pipeline.run(*M, MAM);
    if (verifyModule(*M, &errs()))
      return 1;
  }
  // Freestanding images cannot depend on a host libc for memory intrinsics.
  // Lower them after optimization so the optimizer cannot re-form them.
  SmallVector<MemIntrinsic *, 8> MemoryIntrinsics;
  for (auto &F : *M)
    for (auto &BB : F)
      for (auto &I : BB)
        if (auto *MI = dyn_cast<MemIntrinsic>(&I))
          MemoryIntrinsics.push_back(MI);
  for (auto *MI : MemoryIntrinsics) {
    auto TTI = TM->getTargetTransformInfo(*MI->getFunction());
    if (auto *Copy = dyn_cast<MemCpyInst>(MI))
      expandMemCpyAsLoop(Copy, TTI);
    else if (auto *Move = dyn_cast<MemMoveInst>(MI)) {
      if (!expandMemMoveAsLoop(Move, TTI)) {
        errs() << "error: unable to lower memmove for this ABI\n";
        return 1;
      }
    } else if (auto *Set = dyn_cast<MemSetInst>(MI))
      expandMemSetAsLoop(Set);
    else {
      errs() << "error: unsupported memory intrinsic\n";
      return 1;
    }
    MI->eraseFromParent();
  }
  std::error_code EC;
  ToolOutputFile Out(Output, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }
  legacy::PassManager Passes;
  Out.os() << ".ident \"dcpu16-abi:octet16\"\n";
  if (TM->addPassesToEmitFile(Passes, Out.os(), nullptr,
                              CodeGenFileType::AssemblyFile)) {
    errs() << "DCPU16 assembly emission unavailable\n";
    return 1;
  }
  Passes.run(*M);
  Out.keep();
  return 0;
}

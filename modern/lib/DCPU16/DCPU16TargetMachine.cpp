//===-- DCPU16TargetMachine.cpp - Define TargetMachine for DCPU16 ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Top-level implementation for the DCPU16 target.
//
//===----------------------------------------------------------------------===//

#include "DCPU16TargetMachine.h"
#include "DCPU16.h"
#include "DCPU16MachineFunctionInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

extern "C" void LLVMInitializeDCPU16Target() {
  // Register the target.
  RegisterTargetMachine<DCPU16TargetMachine> X(getTheDCPU16Target());
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  if (!RM.has_value())
    return Reloc::Static;
  return *RM;
}

DCPU16TargetMachine::DCPU16TargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    // Standard LLVM octet addressing with 16-bit integral pointers. The
    // SelectionDAG memory bridge translates octet addresses to hardware words.
    : CodeGenTargetMachineImpl(
          T, "e-m:e-p:16:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16", TT, CPU,
          FS, Options, getEffectiveRelocModel(RM),
          getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, CPU.str(), FS.str(), *this) {
  initAsmInfo();
}

DCPU16TargetMachine::~DCPU16TargetMachine() {}

namespace {
/// DCPU16 Code Generator Pass Configuration Options.
class DCPU16PassConfig : public TargetPassConfig {
public:
  DCPU16PassConfig(DCPU16TargetMachine *TM, PassManagerBase &PM)
      : TargetPassConfig(*TM, PM) {}

  DCPU16TargetMachine &getDCPU16TargetMachine() const {
    return getTM<DCPU16TargetMachine>();
  }

  bool addInstSelector() override;
};
} // namespace

TargetPassConfig *DCPU16TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new DCPU16PassConfig(this, PM);
}

bool DCPU16PassConfig::addInstSelector() {
  // Install an instruction selector.
  addPass(createDCPU16ISelDag(getDCPU16TargetMachine(), getOptLevel()));
  return false;
}

MachineFunctionInfo *DCPU16TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return DCPU16MachineFunctionInfo::create<DCPU16MachineFunctionInfo>(Allocator,
                                                                      F, STI);
}

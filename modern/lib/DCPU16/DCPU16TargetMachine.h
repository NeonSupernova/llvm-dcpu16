//===-- DCPU16TargetMachine.h - Define TargetMachine for DCPU16 -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the DCPU16 specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DCPU16_DCPU16TARGETMACHINE_H
#define LLVM_LIB_TARGET_DCPU16_DCPU16TARGETMACHINE_H

#include "DCPU16Subtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

/// DCPU16TargetMachine
///
class DCPU16TargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  DCPU16Subtarget Subtarget;

public:
  DCPU16TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                      StringRef FS, const TargetOptions &Options,
                      std::optional<Reloc::Model> RM,
                      std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                      bool JIT);
  ~DCPU16TargetMachine() override;

  const DCPU16Subtarget *getSubtargetImpl(const Function &F) const override {
    return &Subtarget;
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
}; // DCPU16TargetMachine.

} // end namespace llvm

#endif

//==- DCPU16FrameLowering.h - Define frame lowering for DCPU16 --*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DCPU16_DCPU16FRAMELOWERING_H
#define LLVM_LIB_TARGET_DCPU16_DCPU16FRAMELOWERING_H

#include "DCPU16.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class DCPU16FrameLowering : public TargetFrameLowering {
protected:
public:
  explicit DCPU16FrameLowering()
      : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(2), -2,
                            Align(2)) {}

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  bool hasFPImpl(const MachineFunction &MF) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  void processFunctionBeforeFrameFinalized(
      MachineFunction &MF, RegScavenger *RS = nullptr) const override;
};

} // namespace llvm

#endif

//===-- DCPU16MCAsmInfo.cpp - DCPU16 asm properties -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the DCPU16MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "DCPU16MCAsmInfo.h"
#include "llvm/Config/llvm-config.h"
using namespace llvm;

void DCPU16MCAsmInfo::anchor() {}

DCPU16MCAsmInfo::DCPU16MCAsmInfo(const Triple &TT,
                                 const MCTargetOptions &Options)
#if LLVM_VERSION_MAJOR >= 23
    : MCAsmInfoELF(Options)
#endif
{
  CodePointerSize = CalleeSaveStackSlotSize = 2;

  CommentString = ";";

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = false;
  HasDotTypeDotSizeDirective = false;
}

//===-- DCPU16Subtarget.cpp - DCPU16 Subtarget Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the DCPU16 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "DCPU16Subtarget.h"
#include "DCPU16.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "dcpu16-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "DCPU16GenSubtargetInfo.inc"

void DCPU16Subtarget::anchor() {}

void DCPU16Subtarget::initLibcallLoweringInfo(LibcallLoweringInfo &Info) const {
  // The standalone target has no upstream triple/runtime profile. Select the
  // integer helpers supplied by our freestanding runtime explicitly.
  Info.setLibcallImpl(RTLIB::UDIV_I32, RTLIB::impl___udivsi3);
  Info.setLibcallImpl(RTLIB::SDIV_I32, RTLIB::impl___divsi3);
  Info.setLibcallImpl(RTLIB::UREM_I32, RTLIB::impl___umodsi3);
  Info.setLibcallImpl(RTLIB::SREM_I32, RTLIB::impl___modsi3);
  Info.setLibcallImpl(RTLIB::UDIV_I64, RTLIB::impl___udivdi3);
  Info.setLibcallImpl(RTLIB::SDIV_I64, RTLIB::impl___divdi3);
  Info.setLibcallImpl(RTLIB::UREM_I64, RTLIB::impl___umoddi3);
  Info.setLibcallImpl(RTLIB::SREM_I64, RTLIB::impl___moddi3);
}

DCPU16Subtarget &
DCPU16Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  ParseSubtargetFeatures("generic", "generic", FS);
  return *this;
}

DCPU16Subtarget::DCPU16Subtarget(const Triple &TT, const std::string &CPU,
                                 const std::string &FS, const TargetMachine &TM)
    : DCPU16GenSubtargetInfo(TT, CPU, CPU, FS), FrameLowering(),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this) {}

// DCPU-16 memory bridge for LLVM's standard octet-addressed IR.
// Hardware words contain two little-endian octets; register pointers count
// octets.
#include "DCPU16ISelLowering.h"
#include "llvm/CodeGen/MachineMemOperand.h"

using namespace llvm;

SDValue DCPU16TargetLowering::LowerMemory(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  auto *Memory = cast<MemSDNode>(Op);
  auto *Load = dyn_cast<LoadSDNode>(Op);
  auto *Store = dyn_cast<StoreSDNode>(Op);
  SDValue Chain = Memory->getChain();
  SDValue Address = Load ? Load->getBasePtr() : Store->getBasePtr();
  EVT MemVT = Memory->getMemoryVT();
  if (MemVT != MVT::i16 && MemVT != MVT::i8)
    report_fatal_error(
        "DCPU16 memory bridge requires legalized i8/i16 accesses");
  if (Memory->isAtomic())
    report_fatal_error("DCPU16 does not support atomic memory accesses");
  if (Memory->isVolatile() &&
      (MemVT == MVT::i8 || Memory->getAlign() < Align(2)))
    report_fatal_error("volatile DCPU16 memory must be aligned 16-bit words");

  auto Constant = [&](uint16_t N) { return DAG.getConstant(N, DL, MVT::i16); };
  auto Binary = [&](unsigned Opcode, SDValue A, SDValue B) {
    return DAG.getNode(Opcode, DL, MVT::i16, A, B);
  };
  auto WordAddress = [&](SDValue P) {
    return Binary(ISD::SRL, P, Constant(1));
  };
  auto RawLoad = [&](SDValue P, SDValue C) {
    auto *N = DAG.getMachineNode(
        DCPU16::MOV16rm, DL, DAG.getVTList(MVT::i16, MVT::Other),
        {WordAddress(P), DAG.getTargetConstant(0, DL, MVT::i16), C});
    // The hardware access covers a whole word. Use conservative memory metadata
    // rather than describing a read-modify-write as a narrow original access.
    auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
        MachinePointerInfo(),
        MachineMemOperand::MOLoad |
            (Memory->isVolatile() ? MachineMemOperand::MOVolatile
                                  : MachineMemOperand::MONone),
        LocationSize::precise(2), Align(2));
    DAG.setNodeMemRefs(N, {MMO});
    return SDValue(N, 0);
  };
  auto RawStore = [&](SDValue P, SDValue V, SDValue C) {
    auto *N = DAG.getMachineNode(
        DCPU16::MOV16mr, DL, MVT::Other,
        {WordAddress(P), DAG.getTargetConstant(0, DL, MVT::i16), V, C});
    auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
        MachinePointerInfo(),
        MachineMemOperand::MOStore |
            (Memory->isVolatile() ? MachineMemOperand::MOVolatile
                                  : MachineMemOperand::MONone),
        LocationSize::precise(2), Align(2));
    DAG.setNodeMemRefs(N, {MMO});
    return SDValue(N, 0);
  };
  auto Shift = [&](SDValue P) {
    return Binary(ISD::SHL, Binary(ISD::AND, P, Constant(1)), Constant(3));
  };
  auto ByteLoad = [&](SDValue P, SDValue C) {
    SDValue Word = RawLoad(P, C);
    return DAG.getMergeValues(
        {Binary(ISD::AND, Binary(ISD::SRL, Word, Shift(P)), Constant(255)),
         Word.getValue(1)},
        DL);
  };
  auto ByteStore = [&](SDValue P, SDValue V, SDValue C) {
    SDValue Word = RawLoad(P, C);
    SDValue S = Shift(P);
    SDValue Mask =
        Binary(ISD::XOR, Binary(ISD::SHL, Constant(255), S), Constant(65535));
    SDValue New =
        Binary(ISD::OR, Binary(ISD::AND, Word, Mask),
               Binary(ISD::SHL, Binary(ISD::AND, V, Constant(255)), S));
    return RawStore(P, New, Word.getValue(1));
  };

  if (Load) {
    SDValue Value;
    if (MemVT == MVT::i16 && Memory->getAlign() >= Align(2)) {
      Value = RawLoad(Address, Chain);
      Chain = Value.getValue(1);
    } else {
      Value = ByteLoad(Address, Chain);
      Chain = Value.getValue(1);
      if (MemVT == MVT::i16) {
        SDValue High = ByteLoad(Binary(ISD::ADD, Address, Constant(1)), Chain);
        Chain = High.getValue(1);
        Value = Binary(ISD::OR, Value, Binary(ISD::SHL, High, Constant(8)));
      } else if (Load->getExtensionType() == ISD::SEXTLOAD) {
        Value =
            Binary(ISD::SRA, Binary(ISD::SHL, Value, Constant(8)), Constant(8));
      }
    }
    return DAG.getMergeValues({Value, Chain}, DL);
  }
  SDValue Value = Store->getValue();
  if (MemVT == MVT::i16 && Memory->getAlign() >= Align(2))
    return RawStore(Address, Value, Chain);
  Chain = ByteStore(Address, Value, Chain);
  if (MemVT == MVT::i16)
    Chain = ByteStore(Binary(ISD::ADD, Address, Constant(1)),
                      Binary(ISD::SRL, Value, Constant(8)), Chain);
  return Chain;
}

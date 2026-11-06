// LLVM-core layout tests. No DCPU backend or process-global byte-width state.
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>

using namespace llvm;
static unsigned Checks = 0;
static void check(bool OK, const char *Message) {
  ++Checks;
  if (!OK) {
    errs() << "FAIL: " << Message << '\n';
    std::exit(1);
  }
}
static const char *WordSpec =
    "e-B16-p:16:16-i8:16-i16:16-i32:16-i64:16-a:16-n16-S16";
static const char *OctetSpec =
    "e-p:16:16-i8:8-i16:16-i32:16-i64:16-a:8-n8:16-S16";

int main() {
  LLVMContext C;
  DataLayout Word(WordSpec), Octet(OctetSpec);
  check(Word.getBitsPerByte() == 16, "native addressable unit");
  check(Octet.getBitsPerByte() == 8, "octet addressable unit");
  check(Word != Octet, "layouts with different byte widths differ");
  check(DataLayout("B8") == DataLayout(""), "explicit B8 preserves defaults");
  check(DataLayout("B16") == DataLayout("B16"), "default word layout is stable");
  for (unsigned Bits : {1u, 8u, 16u, 17u, 32u, 64u}) {
    Type *T = IntegerType::get(C, Bits);
    check(Word.getTypeSizeInBits(T) == Bits, "semantic integer width unchanged");
    check(Word.getTypeStoreSize(T) == (Bits + 15) / 16, "word store size");
    check(Word.getTypeStoreSizeInBits(T) == ((Bits + 15) / 16) * 16,
          "word storage includes subword padding");
    check(Octet.getTypeStoreSize(T) == (Bits + 7) / 8, "octet store size unchanged");
  }
  Type *I8 = Type::getInt8Ty(C), *I16 = Type::getInt16Ty(C);
  Type *I32 = Type::getInt32Ty(C);
  Type *Ptr = PointerType::get(C, 0);
  check(Word.getPointerSize() == 1, "pointer storage counts words");
  check(Word.getPointerSizeInBits() == 16, "pointer bits unchanged");
  check(Word.getIndexSize(0) == 1, "index storage counts words");
  check(Word.getIndexSizeInBits(0) == 16, "index bits unchanged");
  check(Word.getPointerTypeSize(Ptr) == 1, "pointer type storage counts words");
  check(Octet.getPointerSize() == 2, "octet pointer unaffected");
  check(Word.getABITypeAlign(I32) == Align(1), "bit-valued alignment converts to words");
  check(Word.getStackAlignment() == Align(1), "stack alignment converts to words");
  check(DataLayout("p:16:16-i32:32-B16").getABITypeAlign(I32) == Align(2),
        "B16 placement does not affect parsing");
  check(DataLayout("B16").getABITypeAlign(I16) == Align(1),
        "implicit type alignments convert to words");
  check(DataLayout("B16").getPointerABIAlignment(0) == Align(4),
        "implicit pointer alignment converts to words");

  auto *Array = ArrayType::get(I8, 3);
  check(Word.getTypeAllocSize(Array) == 3, "i8 array consumes one word per element");
  check(Word.getTypeAllocSizeInBits(Array) == 48, "array allocation bits use word width");
  auto *Struct = StructType::get(C, {I8, I16, I32});
  auto *Packed = StructType::get(C, {I8, I16, I32}, true);
  for (auto *S : {Struct, Packed}) {
    const StructLayout *SL = Word.getStructLayout(S);
    check(SL->getElementOffset(1) == 1, "struct i16 offset");
    check(SL->getElementOffset(2) == 2, "struct i32 offset");
    check(SL->getElementOffsetInBits(2) == 32, "struct offset in bits");
    check(SL->getSizeInBytes() == 4, "struct size in addressable units");
    check(SL->getSizeInBits() == 64, "struct size in physical bits");
    check(Word.getTypeSizeInBits(S) == 64, "struct physical bit size");
  }
  check(Octet.getStructLayout(Struct)->getElementOffset(1) == 2,
        "shared context octet struct is independent");
  check(Octet.getStructLayout(Packed)->getSizeInBytes() == 7,
        "packed octet struct unchanged");

  // Assignment must discard a cached struct layout from the old byte model.
  DataLayout Assigned = Octet;
  Assigned.getStructLayout(Struct);
  Assigned = Word;
  check(Assigned.getStructLayout(Struct)->getElementOffset(1) == 1,
        "assignment invalidates layout cache and copies byte width");
  DataLayout Copied(Word);
  check(Copied == Word, "copy preserves addressable unit");
  check(Copied.getTypeStoreSize(I32) == 2, "copy uses native storage size");

  for (StringRef Bad : {"B", "B0", "B7", "B32", "B16-B8", "B8-B8",
                        "B16-p:16:8", "B16-i16:8", "B16-S8", "B16-Fi8", "B16-a:8", "B16-i32:16:8"}) {
    auto Parsed = DataLayout::parse(Bad);
    check(!Parsed, "malformed/contradictory byte model rejected");
    consumeError(Parsed.takeError());
  }

  // Reinterpreting native-unit memory must not combine octets from adjacent
  // words. The original octet-only folder silently produced a different value.
  auto *Data = ConstantDataArray::get(C, ArrayRef<uint16_t>({0x1122, 0x3344}));
  auto FoldedValue = [](Constant *Value) -> uint64_t {
    if (auto *I = dyn_cast_or_null<ConstantInt>(Value))
      return I->getZExtValue();
    return UINT64_MAX;
  };
  check(FoldedValue(ConstantFoldLoadFromConst(Data, I32, Word)) == 0x33441122,
        "native little-endian constant load combines words");
  DataLayout BigWord("E-B16-p:16:16-i16:16-i32:16");
  check(FoldedValue(ConstantFoldLoadFromConst(Data, I32, BigWord)) == 0x11223344,
        "native big-endian constant load combines words");
  check(FoldedValue(ConstantFoldLoadFromConst(Data, I16, APInt(16, 1), Word)) == 0x3344,
        "constant load offset counts native words");
  check(FoldedValue(ConstantFoldLoadFromConst(Data, I32, Octet)) == 0x33441122,
        "octet constant load remains unchanged");
  check(FoldedValue(ConstantFoldLoadFromConst(Data, I8, APInt(16, 1), Octet)) == 0x11,
        "octet constant load offset counts octets");
  check(FoldedValue(ConstantFoldLoadFromConst(Data, I8, APInt(16, 1), Word)) == 0x44,
        "native subword load truncates one addressable unit");

  auto *SubwordData = ConstantDataArray::get(C, ArrayRef<uint8_t>({17, 34}));
  check(!ConstantFoldLoadFromConst(SubwordData, I16, Word),
        "subword initializer padding is not invented during reinterpretation");
  Module NativeConstants("native-constants", C);
  NativeConstants.setDataLayout(Word);
  auto *NativeGlobal = new GlobalVariable(NativeConstants, Data->getType(), true,
      GlobalValue::PrivateLinkage, Data, "words");
  check(!ReadByteArrayFromGlobal(NativeGlobal, 0),
        "host-octet extraction declines native words rather than truncating");
  Module OctetConstants("octet-constants", C);
  OctetConstants.setDataLayout(Octet);
  auto *OctetGlobal = new GlobalVariable(OctetConstants, Data->getType(), true,
      GlobalValue::PrivateLinkage, Data, "octets");
  auto *Bytes = dyn_cast_or_null<ConstantDataArray>(ReadByteArrayFromGlobal(OctetGlobal, 0));
  check(Bytes && Bytes->getNumElements() == 4 && Bytes->getElementAsInteger(1) == 0x11,
        "host-octet extraction remains unchanged for octet modules");

  // GEP offsets and textual/bitcode round trips use the same module-local DL.
  SMDiagnostic Diagnostic;
  auto M = parseAssemblyString(R"(
    target datalayout = "e-B16-p:16:16-i8:16-i16:16-i32:16-i64:16-a:16-n16-S16"
    @data = global [3 x i8] [i8 17, i8 34, i8 51], align 1
    @third = global ptr getelementptr ([3 x i8], ptr @data, i16 0, i16 2)
    @wide = global [3 x i32] zeroinitializer, align 1
    @last = global ptr getelementptr ([3 x i32], ptr @wide, i16 0, i16 2)
  )", Diagnostic, C);
  if (!M) {
    Diagnostic.print("word-layout", errs());
    return 1;
  }
  check(!verifyModule(*M, &errs()), "native module verifies");
  auto OffsetOf = [](Module &Mod, StringRef Name) {
    auto *GEP = cast<GEPOperator>(Mod.getNamedGlobal(Name)->getInitializer());
    APInt Offset(16, 0);
    if (!GEP->accumulateConstantOffset(Mod.getDataLayout(), Offset))
      std::exit(2);
    return Offset.getZExtValue();
  };
  check(OffsetOf(*M, "third") == 2, "i8 GEP advances native words");
  check(OffsetOf(*M, "last") == 4, "i32 GEP advances two words per element");
  SmallVector<char, 0> Buffer;
  raw_svector_ostream OS(Buffer);
  WriteBitcodeToFile(*M, OS);
  LLVMContext OtherContext;
  auto RoundTrip = parseBitcodeFile(MemoryBufferRef(StringRef(Buffer.data(), Buffer.size()),
                                                   "native.bc"), OtherContext);
  if (!RoundTrip) {
    errs() << toString(RoundTrip.takeError()) << '\n';
    return 1;
  }
  check((*RoundTrip)->getDataLayout() == Word, "bitcode preserves word layout");
  check(OffsetOf(**RoundTrip, "last") == 4, "bitcode preserves GEP layout semantics");
  check(Octet.getTypeStoreSize(I32) == 4, "native parse did not mutate octet module");
  outs() << "Passed " << Checks << " word-layout checks\n";
}

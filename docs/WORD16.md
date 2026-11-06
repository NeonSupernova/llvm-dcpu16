# Native word addressing: experimental core port

The core port is in progress. `dcpu16-llc --byte-size=16` still rejects the
request. The current compiler, objects, and runtime remain octet16. The isolated
core test build described below does not generate DCPU machine code.

## Candidate word16 ABI

The new ABI will have a distinct object tag (`word16-v1`), incompatible with
both octet16 and the historical B16 fork. These rules guide the remaining port:

| Property | Candidate word16-v1 behavior |
| --- | --- |
| Addressable unit | One 16-bit hardware word |
| Integer values | i8 is 8 bits, i16 is 16 bits, i32 is 32 bits, i64 is 64 bits |
| Scalar storage | Round up to a whole word: i8/i16 use one, i32 two, i64 four |
| Narrow storage | i8 occupies low 8 bits of one word; upper bits are padding |
| Integer word order | Low word first in memory |
| Generic pointers | 16-bit word addresses; no shift between generic pointers and PC/SP |
| Pointer arithmetic | GEP offsets and pointer differences count addressable words |
| Alignment | IR align values count words; DataLayout alignment fields remain in bits |
| Stack | Downward in words; native call slots and saved PC each occupy one word |
| Address range | Up to 65536 words, subject to image/stack placement |
| Scalar arguments/results | Preserve version-1 register piece order; wide results use hidden word pointers |
| Copy/move intrinsic lengths | Count addressable words, with word-sized loop accesses |
| Binary serialization | Still two host octets per hardware word; selectable file word endianness |

Candidate layout:

```text
e-B16-m:e-p:16:16-i8:16-i16:16-i32:16-i64:16-a:16-n16-S16
```

The core patches implement the layout rules, including packed structs and arrays.
`B16` is a downstream experimental extension, not upstream LLVM syntax. It is
stored per DataLayout, copied and compared with the layout, and preserved in IR
and bitcode. There is no mutable global byte-width setting. Default/B8 layouts
retain ordinary octet behavior in the same LLVM context and process.

Subword padding is not an extra integer value; optimizations must not infer its
contents from an adjacent word. Physical emitters may choose padding bits, but
a reinterpretation cannot assume arbitrary initialized padding is meaningful.

The following contracts apply before enabling the ABI:

- LLVM memset retains its i8 fill operand. The experimental scalar expansion
  zero-extends that value into each 16-bit addressable unit: filling with 0xab
  writes 0x00ab per word. Forwarding from memset follows the same rule. A native
  C library with 16-bit char still needs a distinct full-word fill path; this
  expansion must not be used as an implicit implementation of that C API.
- Assembly `.byte`, `.ascii`, alignment, and zero-fill directives in the reference
  assembler retain their existing octet definitions. A word16 compiler must emit
  native words and convert target storage/alignment to directive units explicitly.
  In particular, an i8 array must not become a packed `.ascii` string.

## Reproduce the core experiment

The patch series lives in `modern/llvm-core/patches/`, separate from backend
changes. `series.json` pins the LLVM version and SHA-256 of each patch and every
original/patched source file. The script rejects unknown or partly patched
sources and recognizes the exact already-patched state on repeat runs.

Use an independent source and build directory. The patch changes LLVM's C++ ABI;
do not mix these headers/libraries with an existing ordinary LLVM development kit.

```sh
python3 modern/scripts/test_word_layout.py --bootstrap --jobs 4
```

Or provide a separate copy of the pinned llvm-project source root:

```sh
python3 modern/scripts/test_word_layout.py \
  --llvm-source /path/to/llvm-project-23.1.4.src \
  --work-dir /path/to/isolated-word-build --jobs 4
```

This builds the required static LLVM components, including transformation
utilities and the interpreter, and two independent C++ test programs. Tests cover integer widths and storage, alignments,
struct/array layout, pointers, GEP, invalid specifications, copy/cache behavior,
textual/bitcode round trips, endian-sensitive constant loads, subword padding,
and independent octet/native layouts. Additional tests cover forwarding offsets,
bounds and fill values. Scalar loops are validated as native IR, then adapted to
host pointers and 64-bit GEP indices for LLVM interpreter execution. Full-unit
copy, overlapping move, zero lengths, fill guards, known/dynamic lengths and
ordinary octet behavior are checked. This tests loop semantics, not native
backend execution or a complete optimization pipeline. The ordinary backend build never applies
these patches. Experimental CI runs on Ubuntu; the core is also tested locally
on macOS.

## Remaining correctness audit

| Area | Status |
| --- | --- |
| DataLayout sizes, alignments, struct offsets, pointer/index sizes | Patched and tested |
| Module-local state, copies, textual/bitcode persistence | Patched and tested |
| Constant initializer load reinterpretation | Native-unit folding tested; unsafe host-octet extraction declined |
| GVN coercion utilities: offsets, bounds, extraction and memset forwarding | Patched; targeted utility checks pass |
| SROA, whole GVN/InstCombine pipelines, value tracking, other memory optimizations | Audit/port pending |
| Non-atomic scalar memcpy/memmove/memset expansion | Full native-unit accesses and i8 zero-extended fill tested at IR level |
| Target-widened/atomic/pattern memory intrinsics and native C full-word fill | Pending |
| SelectionDAG value storage, split memory operations and offsets | Pending |
| Frame/call units, backend memory bridge and function pointers | Pending |
| AsmPrinter constants, strings, padding, directive unit conversion | Pending |
| word16 object metadata, runtime, linker limits and ABI rejection tests | Pending |
| Native Clang layout, character model, frontend and emulator tests | Pending |

Do not enable the native flag merely because layout tests pass. Acceptance needs
optimized frontend/IR-to-binary execution, pointer and memory tests, independently
linked runtime objects, and mixed-ABI rejection. The existing octet16 suite must
continue to pass against unpatched LLVM 22 and 23.

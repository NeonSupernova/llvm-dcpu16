# Experimental octet16 ABI, version 1

This document specifies the modern standalone build. It does **not** describe
the legacy `B16` ABI on the historical `new-dcpu16` branch.

## Values, pointers, and storage

- DCPU-16 v1.7 instructions and 16-bit hardware words.
- An LLVM byte is eight bits. `i8`, `i16`, `i32`, and `i64` retain their exact
  bit widths. Two octets are packed into a hardware word, low octet first.
- Data and function pointers are 16-bit integers counting octets. A pointer
  value `p` refers to hardware word `p >> 1`; `p & 1` selects the octet.
- Generic pointer addressing therefore covers the lower 32768 hardware words
  (64 KiB of octet-addressed storage). Code, globals, heap, and stack must be
  placed within that range when their addresses enter generic pointers.
- Assembly labels and the hardware PC/SP count **hardware words**. The backend
  converts symbols to octet pointers and converts indirect call pointers back.
  Serialized pointers use octet addresses; global pointer initializers do too.
- Globals and function labels start on hardware-word boundaries. Arrays of
  `i8` remain densely packed. Struct offsets follow the LLVM data layout below.
- The stack grows downward; SP and an optional J frame pointer hold hardware
  word addresses. Stack object offsets are tracked in octets and converted
  during frame lowering. Startup code must initialize SP below `0x8000`.

```text
e-m:e-p:16:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16
```

This is compatible with Clang's MSP430 frontend layout. Float layout entries
do not imply implemented floating-point operations.

## Calling convention

- A, B, C carry integer argument pieces, in argument order. Multiword integers
  use low-word-first order. An argument is wholly in registers or wholly on
  the stack; after one argument spills, subsequent arguments use the stack.
- Stack pieces occupy two octets, aligned to two octets. JSR pushes the hardware
  return PC; the first stack argument follows it.
- Register return pieces occupy A, B, C, **high-word first**. For example,
  i32 `0x12345678` returns A=`0x1234`, B=`0x5678`. This preserves the inherited
  version-1 convention; argument word order differs. Previous documentation
  incorrectly stated low-word-first returns.
- Results requiring more than three register pieces use LLVM's hidden result
  pointer. It occupies the first argument slot; the callee stores the result
  through that octet pointer in ordinary low-word-first memory order. i64
  arithmetic and helper calls exercise this path. Other aggregate signatures
  still require their own validation.
- X, Y, Z, I, J are callee saved. A, B, C and EX may be clobbered by calls.
- The emitted assembly contains functions only. The embedder supplies startup,
  stack/heap placement, termination, peripherals, and any external runtime calls.
  The supplied freestanding division object is linked explicitly; see RUNTIME.md.

## Memory access and I/O

Aligned i16 accesses map to a single hardware word access. i8 stores use a
read-modify-write sequence. Unaligned i16 accesses span words. These sequences
do not provide atomicity and are inappropriate for device registers with read
side effects. Atomic operations are rejected. Volatile accesses must be aligned
i16 operations. Memory intrinsic lengths count octets; freestanding memcpy,
memmove, and memset intrinsics are lowered to loops rather than host libc calls.

Binary word serialization (`--endian big` or `--endian little` in the reference
assembler) is distinct from the low-octet-first memory representation.

Compiler assembly includes `.ident "dcpu16-abi:octet16"`. The assembler carries
this declaration into relocatable objects and rejects a contradictory `--abi`.
The static linker requires matching object ABIs and checks the complete image
against the selected address space. Symbol addresses in objects/maps count
hardware words; expressions such as `symbol << 1` produce octet pointers after
final placement. See `OBJECT_FORMAT.md` for the format and linker contract.

## Byte-size options and future ABIs

`--byte-size=8` explicitly selects the implemented mode. Other values produce
an error. A native 16-bit-byte ABI is a separate LLVM-core modernization task:
the historical fork changed DataLayout, value sizes, optimizers, SelectionDAG,
and assembly emission. Copying its process-global byte-width setting into modern
LLVM would break mixed-module and mixed-target compilation.

Before enabling a second mode, define its pointer representation, size/alignment
units, i8 storage, GEP semantics, memory intrinsic lengths, object flags, and
module/link compatibility checks, then validate it at O0 and O2. Never reinterpret
already-generated host IR or mix the historical ABI with octet16 silently.

## Current limits

Native LLVM MC object encoding, a native Clang target, a hosted runtime, soft
float, vector operations, atomics, dynamic alloca, exceptions, varargs and
interrupt/peripheral SDK support are not complete. Some unsupported IR is
diagnosed up front; additional unsupported legalizer/libcall cases may still
produce LLVM errors. Consult execution tests for measured coverage.

Reference DCPU JSON object encoding and static image linking are implemented.
ELF objects, archives, weak/common symbols, dynamic linking, and garbage
collection of sections are not supported by that linker.

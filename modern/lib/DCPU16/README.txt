Modern experimental DCPU-16 v1.7 backend
=======================================

Ported from FrOSt-Foundation/new-dcpu16 to LLVM 22/23. See the root README.md,
PROJECT_MEMORY.md, and docs/ABI.md for measured validation and limitations.

The standalone dcpu16-llc driver uses upstream LLVM libraries. It does not
register a dcpu16 triple in upstream Clang or upstream llc. Assembly output
uses LLVM/GAS labels and directives; the reference test assembler can encode
this output. Native MC object emission is not implemented.

Default ABI: octet bytes, little-endian memory, 16-bit integral pointers.
Memory lowering packs two bytes into each hardware word. Non-octet byte modes
require a separate LLVM-core port and must not be claimed as working here.

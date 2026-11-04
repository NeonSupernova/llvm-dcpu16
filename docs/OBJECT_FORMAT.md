# DCPU relocatable objects, version 1

The reference assembler can emit relocatable JSON objects with `assemble -c`.
The static `link` command places them in argument order starting at hardware
word zero, applies relocations, and emits a flat binary. This format is separate
from LLVM's unfinished native MC object emitter and is not ELF.

## Object fields

| Field | Meaning |
| --- | --- |
| `format` | Exactly `dcpu16-object`. |
| `version` | Integer `1`. Unknown versions fail explicitly. |
| `target` | Exactly `dcpu16-v1.7`. |
| `abi` | `octet16` or `raw`; all linked inputs must agree. |
| `alignment` | Power-of-two alignment in hardware words. |
| `words` | Logical 16-bit words, independent of binary serialization order. |
| `symbols` | Names mapped to an object-relative word `offset` and `local`/`global` binding. |
| `relocations` | Fields to patch after placement, described below. |

`octet16` selects the compiler ABI in `ABI.md` and limits images to 32768 words.
`raw` is for hand-written assembly and permits the full 65536-word hardware
image; it does not implement the legacy 16-bit-byte compiler ABI. Unannotated
CLI assembly defaults to `octet16`. Compiler assembly declares its ABI using
`.ident "dcpu16-abi:octet16"`; a conflicting explicit selection fails.

## Symbols and placement

Labels are word aligned. `.globl`/`.global` exports definitions, `.local` keeps
them local, and `.extern` permits a forward external declaration. Unexported
symbols are private to their object. In particular, identical LLVM labels such
as `.LBB0_2` in different modules do not collide. A reference first resolves in
its own object and then through the exported definitions of all inputs.
Label names use letters, digits, underscore, dot, and dollar sign and cannot
start with a digit. Register/operand spellings such as `A`, `SP`, and `POP`
are reserved, including case variants, because they would be ambiguous operands.

Each object starts at its declared word alignment; the linker inserts zero
padding as needed. Internal padding remains part of the object. The assembler
records the largest `.p2align` used so adding a base address preserves internal
alignment. Byte data is packed low octet first. `.long` and `.quad` data use
low-word-first order. A word-aligned label following an odd byte requires an
appropriate alignment directive.

The `--map` JSON output contains exported hardware-word addresses and each
object's base and complete symbol table. Startup remains explicit: put its
object first and initialize SP according to the ABI. The linker adds no hidden
startup code, call, entry jump, or termination routine.

## Relocations

Each record contains:

- `offset`: object-relative word index to patch.
- `expression`: a deferred address expression.
- `bits`: logical value width, one of 8, 16, 32, 64.
- `shift`: which 16-bit portion of that logical value the field holds.
- `bit_offset`: zero, or eight for a high-octet 8-bit field.

Expressions support integer literals, symbol names, unary plus/minus,
addition/subtraction, shifts by 0–63, bitwise AND, and parentheses. They are
parsed as restricted syntax, never executed as Python. Final values must fit
the signed or unsigned range of their logical width. Explicit masking is
available when truncation is intended. Relocations cannot overlap field bits.

For example, `JSR increment` resolves a hardware code address. A pointer
initializer `.short increment << 1` doubles the final hardware address to form
an octet16 function pointer. The expression must survive placement intact;
resolving it before the object's base is known would create incorrect pointers.

## Diagnostics and limits

The linker rejects undefined references, duplicate exported definitions,
incompatible targets/ABIs, malformed records, overlapping relocation fields,
out-of-range relocations, and image overflow. It validates and resolves all
inputs before writing the binary, so an unresolved symbol leaves an existing
output file intact.

Section directives do not create separate address spaces: data and code stay
in each object's emission order in flat RAM. The current tool does not support
ELF, archives, weak/common symbol merging, dynamic linking, or section garbage
collection. Unsupported assembly directives are rejected. Binary word endianness
is selected at final link time and does not change pointer or data semantics.

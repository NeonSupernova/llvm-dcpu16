# Modernization plan

## Deliverable

A maintained DCPU-16 v1.7 toolchain on a pinned modern LLVM release, with a
documented ABI, tested code generation, reproducible builds, and clearly defined
memory-model options. Retain original licenses and attribution. Seek upstreamable
changes where practical; avoid claiming to be the original maintainers.

## Milestones and acceptance criteria

1. Modern backend: TableGen, register allocation, instruction selection, stack
   frames, calling convention, and assembly printing build against LLVM 23.1.4.
2. Execution: arithmetic, comparisons, branches, calls, recursion, stack spills,
   globals, arrays and wider arithmetic pass emulator tests at O0 and O2.
3. Memory model: port and audit the old non-octet core changes. Explicitly define
   supported byte sizes, i8 storage, GEP, memcpy/memmove/memset length units,
   alignment, pointer conversions, object format and ABI compatibility checks.
4. Toolchain: assembler/linker integration, startup code, basic runtime and device
   intrinsics. Unsupported operations produce useful diagnostics.
5. Maintenance: CI, reproducible source/bootstrap scripts, examples, contribution
   guide and release procedure. Publish only after verified artifacts exist.
6. Consumers: Clang target integration and Nyet target-layout/runtime changes,
   with frontend-to-emulator tests. Keep consumers outside backend internals.

## Architecture

During migration, `modern/` builds the port against an installed LLVM development
kit. This permits fast API-porting iterations without rebuilding all LLVM for each
edit. LLVM-core byte-model changes must remain a separate patch series and require
an LLVM source build. Do not present the standalone build as complete core support.

Use one well-tested default ABI before expanding the feature matrix. Feature
options must be target-local or module-local; avoid process-global mutable byte
sizes, which can corrupt mixed-target compilation in a single process.

## Known uncertainties

The historical backend README contains inherited MCU terminology and may not
reflect actual DCPU implementation coverage. Verify code and execution behavior.
Object emission, soft-float, varargs, interrupts and wider integer libcalls each
need independent validation. Existing LLVM IR is not layout-neutral.

## Measured progress (2026-11-03)

- LLVM 23.1.4 source was downloaded, checksum verified, configured with assertions,
  and built locally. The standalone backend builds against those libraries.
- A compatibility build against Homebrew LLVM 22.1.8 also works; rerun its test
  suite after changes that touch version-specific interfaces.
- The root build entry point now uses modern CMake and the standalone backend.
- Execution coverage includes integer arithmetic, signed/unsigned comparisons,
  boolean values, shifts, recursion, stack arguments, stack locals, globals,
  packed byte accesses, unaligned words, multiword arguments, indirect calls,
  pointer initializers, memcpy, and a C frontend bridge. Tests run at O0 and O2.
- The reference assembler emits actual DCPU words; the test CPU decodes those
  words. Known ISA encodings are checked independently of compiler output.
- Fixed inherited missing conditional jumps, signed comparison semantics,
  incorrect boolean flag interpretation, split-argument ordering/allocation,
  obsolete shift lowering, and byte/word stack and pointer-unit mismatches.
- Default memory model is upstream-compatible octet16. Non-octet requests and
  incompatible input layouts are rejected. This is **not** the old B16 core port.
- The checksum-pinned source bootstrap and O0/O2 execution suites passed actual
  GitHub Actions jobs on ubuntu-24.04 and macos-14 for implementation commit
  c8a89120888d3bc039da4444933afe8e5b6e6eb6:
  https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/41747559232
  The optional C bridge also passes locally with Clang explicitly supplied.

Native 16-bit bytes, native MC object encoding/linking, a native Clang target,
peripheral/interrupt SDK, complete runtime helpers, and Nyet integration remain
open. The modernization is an adoption in progress, not a completed full SDK.

## Separate compilation progress (2026-11-04)

The reference assembler now emits relocatable JSON objects and the static linker
combines independently compiled modules. It handles scoped local names, exports,
word alignment, deferred address/data expressions, final symbol maps, ABI checks,
and overflow/undefined/duplicate diagnostics. Compiler assembly declares octet16
using a standard `.ident` record. The schema and limitations are in OBJECT_FORMAT.md.

Cross-module direct calls, external globals, and stored function pointers execute
correctly after relocation at O0/O2. The test suites now contain 32 methods per
optimization level, including installation and binary CLI checks. Installation
also preserves the LLVM shared-library search path; installed tools compile and
link the sample to a binary that returns A=42.

This completes reference object/static-link integration for the current ABI.
Native LLVM MC/ELF object generation remains a separate task. Common/weak symbols,
archives and section garbage collection are not implemented by the reference
linker. The full SDK and non-octet-core milestones remain open.

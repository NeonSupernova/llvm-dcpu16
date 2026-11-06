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

Linker implementation and the bootstrap-layout install fix are verified at
4110de2e9bab203d970c0efde07c84241cca596b by successful Ubuntu and macOS CI:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/41977019496
The installed compiler explicitly retains LLVM_LIBRARY_DIRS, including SDKs
inside the project that automatic RPATH discovery would exclude.

## Freestanding runtime progress (2026-11-05)

The build now supplies an explicitly linked octet16 runtime object for signed
and unsigned i32/i64 division and remainder. Modern LLVM libcall initialization
selects these implementations without requiring an upstream DCPU triple.
Generic hidden result pointers enable i64 returns. Argument grouping now handles
LLVM's hidden first argument and its sentinel original index.

Execution exposed a missing EX use on the select pseudo instruction: optimization
could remove a comparison despite later selects depending on its result. That
metadata is corrected. The ABI documentation now accurately describes inherited
high-word-first register returns, verified by a physical-register test.

The suites contain 38 methods per optimization level, including independent
runtime compilation, full-result arithmetic comparison, built runtime execution,
and installed runtime linking. Local LLVM 23.1.4 and LLVM 22.1.8 suites pass at
O0/O2 with the optional C frontend bridge enabled. See RUNTIME.md for scope and
helper edge behavior. Complete compiler-rt, native bytes, Clang and device SDK
work remain open. Implementation commit
b8acc95ce21f6c4a79935e2a33bcb882bc35f84c passed Ubuntu and macOS source-bootstrap,
O0/O2 execution and installation CI:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/41986967332

## Verifier and non-octet core progress (2026-11-06)

Execution compilations and the packaged runtime now run LLVM's strict machine
verifier. Frame address selection, direct call operand descriptors, and select
call-frame metadata are corrected. Local LLVM 22/23 O0/O2 suites pass. Ubuntu CI
passed checkpoint 21573e44; macOS CI jobs were cancelled before doing work.

A separately pinned LLVM 23.1.4 core patch series now models module-local B16
layouts and folds constant loads using native addressable units. An independent
core build passes 86 checks, including mixed octet/native layouts in one process,
GEP, structs, endian-sensitive reads and bitcode persistence. The full validation
script and repeat run passed locally. This is the first core stage; native
code generation remains disabled pending the optimizer, SelectionDAG, emitter,
object/runtime and frontend audit described in [WORD16.md](WORD16.md).
The isolated core source-bootstrap/build/test workflow passed Ubuntu CI at
09d47501 and d9cac4b8:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42153353192
Ordinary Ubuntu backend regressions also passed 09d47501; macOS CI was cancelled,
with macOS coverage provided by the successful local builds.

## Forwarding and memory-loop progress (2026-11-06)

Two additional isolated core patches convert GVN coercion utility offsets,
bounds, extraction and memset splats to native units, and expand scalar memory
intrinsics using full-unit accesses. Memset's existing i8 operand is explicitly
zero-extended into each unit; native C full-word fill remains separate.

The core suite now has 224 checks in three groups, including interpreter execution
of expanded loops with high-bit data, overlap, zero lengths and guard regions.
Both octet and native paths pass locally. Interpreter execution uses host-adapted
pointers/indices after native IR validation; native code generation and complete
optimization pipelines remain pending. Read WORD16.md before enabling any mode.

The corrected four-patch core series passed verified-source bootstrap, GCC build
and all three CTest groups on Ubuntu at ea37d375:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42180910683
An initial build caught a missing complete-Module declaration masked by Clang's
PCH; the correction uses the existing function-level DataLayout accessor and
also passed explicit no-PCH Clang/GCC checks locally.

## Nyet boundary progress (2026-11-06)

Actual Nyet arithmetic IR now executes as DCPU machine words, including i32
integer division linked with the separate runtime at O0 and O2. This exposed
and fixed late DataLayout assignment: implicit instruction alignments must use
the target during IR parsing. Explicit unsupported stack realignment is rejected.

The backend suites now have 40 methods per optimization level. Local LLVM 22/23
suites pass; Ubuntu bootstrap/O0/O2/installation CI passed d852b957:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42182863602
The macOS CI job was cancelled; local macOS coverage passed. Read
[NYET_INTEGRATION.md](NYET_INTEGRATION.md) for verified commands and remaining
driver, layout/profile and freestanding runtime integration. Nyet source was
not changed. Native word16 still needs the outstanding optimizer/codegen audit.

## SROA progress (2026-11-06)

The fifth isolated core patch converts SROA partition/slice sizes, integer
extraction/insertion, replacement storage, vector unit checks and memset splats
to the module's addressable unit. The recorded native second-word load failure
is now a positive regression. Semantic i8/i24 widths and padding stay intact.

The core suite now runs 121 SROA pass-level cases in addition to the prior 224
checks: 345 total in four groups. Both unit sizes and endiannesses, partial
writes/copies/fills, mixed and packed structs, padding, scalar/vector cases and
the original reproducer pass locally. Fresh patch application and no-PCH
Clang/GCC checks pass too. The full optimizer pipeline and native code generation
remain pending; see WORD16.md for the remaining audit boundaries.

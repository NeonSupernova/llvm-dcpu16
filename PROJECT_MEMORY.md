# Project memory

## Intent and authorization

On 2026-11-03 the user authorized adopting the open-source DCPU-16 LLVM backend,
modernizing it fully, adding byte-size/ABI options and useful enhancements, and
creating AGENTS.md plus persistent project memory. The result should be useful
to the wider community. Nyet prompted this effort but must remain a consumer.

## Repositories and branches

- Fork: https://github.com/NeonSupernova/llvm-dcpu16
- Upstream: https://github.com/FrOSt-Foundation/llvm-dcpu16
- Local checkout: /Users/novapy/Dev/llvm-dcpu16
- Historical default: new-dcpu16, bdd3c695c99ced1cf98a61422f296025afd3fccd
- Development branch: modernize/llvm23
- Build worktree: /private/tmp/llvm-dcpu16-modern (now detached at the verified
  implementation commit; the main checkout owns modernize/llvm23)
- Sibling Nyet has preexisting user changes; leave them alone.

## Findings

- The upstream default dcpu16 branch is LLVM 3.2. new-dcpu16 is LLVM 4.0.
- new-dcpu16 adds its backend on top of the llvm-16bits branch, whose core
  modifications implement BitsPerByte / B16 data layouts. This is a separate
  porting problem from modern backend APIs.
- Historical root CMake does not configure with CMake 4.2.3: CMP0051 OLD has
  been removed, and old Python discovery fails with the local mise shim.
- Host: Apple Silicon macOS, Apple clang 21, CMake 4.2.3, Ninja 1.13.2.
- Installed Homebrew LLVM development kit: 22.1.8 at /opt/homebrew/opt/llvm.
- GitHub latest release verified as llvmorg-23.1.4 (2026-10-20).
- Only about 11 GiB free at start: avoid full debug/all-target LLVM builds.

## Verified modernization results

The standalone backend in `modern/` now builds against **LLVM 23.1.4** and
**LLVM 22.1.8**. The root CMake entry point selects this modern build; the old
root `include/`, `lib/`, etc. are retained for historical reference only.

- Built official LLVM 23.1.4 from a checksum-verified source archive with
  assertions enabled. This is an upstream LLVM build, not a patched B16 fork.
- `modern/llvm-source.json` pins its URL and SHA-256. The reproducible build
  entry point is `python3 modern/scripts/build.py --bootstrap`.
- LLVM 23 backend build: `/private/tmp/dcpu23-maintainer/dcpu16-build`.
- LLVM 22 compatibility build: `/private/tmp/dcpu22-maintainer/dcpu16-build`.
- Upstream LLVM 23 libraries/source are at `/private/tmp/llvm23-dcpu-build` and
  `/private/tmp/llvm-project-23.1.4.src`. Temporary paths may disappear; rebuild
  with the checked-in bootstrap script rather than relying on them permanently.
- The initial port had 20 unittest methods; current coverage has 32 methods
  (plus parameterized execution cases, including installed-tool tests) at each
  of O0/O2. The optional C bridge test uses Homebrew Clang 22.1.8 with MSP430
  frontend layout; it produces LLVM IR only, never MSP430 machine code.
- Tests cover arithmetic, comparisons, boolean results, shifts, recursion,
  stack arguments and locals, byte/unaligned/global memory, wide arguments,
  indirect calls/global function-pointer initializers, memory intrinsics,
  binary CLI serialization, known encodings, and unsupported-mode diagnostics.
- The example in `modern/examples` compiles to assembly, assembles to binary,
  and executes with A=42. Its local artifacts are in `/private/tmp/dcpu-adoption-demo`.
- Original licenses/attribution are retained. The modern fork is experimental;
  test coverage does not establish complete LLVM IR or hardware support.

## Decisions

Use the **octet16 ABI** for the modern standalone build: standard LLVM 8-bit
bytes, 16-bit pointers counting octets, low-octet-first packed memory, hardware
PC/SP counting words. This covers the lower 32768 hardware words through generic
pointers. The memory bridge translates between octets and hardware words.

This choice avoids porting the legacy process-global mutable BitsPerByte state
into modern LLVM. `--byte-size=16` is explicitly rejected; it is not implemented.
The historical new-dcpu16 branch remains the reference for the old word-byte ABI.
Read docs/ABI.md before changing pointer or memory units.

Fixed inherited missing branch jumps, unsigned handling of signed comparisons,
incorrect boolean flag interpretation, split-argument placement/order, obsolete
shift lowering and bit.w patterns, frame-unit mismatches, missing modern
MachineFunctionInfo allocation, symbol materialization and pointer serialization.

## Remaining work

Full adoption is still in progress. Native 16-bit-byte core support, native LLVM
MC object encoding/linker integration, native Clang target support, a hosted runtime and
wide arithmetic helpers, peripheral/interrupt SDK, broader conformance testing,
and Nyet integration are not finished. Some unsupported IR is diagnosed early;
other unsupported legalizer/runtime cases may still fail in LLVM.

## CI verification

Implementation commit c8a89120888d3bc039da4444933afe8e5b6e6eb6 passed the actual
GitHub Actions source-bootstrap/build/execution workflow on ubuntu-24.04 and
macos-14:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/41747559232
Both jobs completed successfully. CI runs the O0/O2 suites; the optional Clang
bridge test is exercised locally with an explicitly supplied Clang path.

The implementation and documentation are pushed on modernize/llvm23. The fork's
historical default new-dcpu16 branch is preserved. No release has been published.
Do not publish a release until its actual artifacts and support matrix have been
verified. Further byte-model/SDK work remains as listed above.

## Separate compilation and linker continuation (2026-11-04)

Added relocatable DCPU JSON objects (`assemble -c`) and a static image linker
(`link`) to the reference tools. Native LLVM MC/ELF object emission remains open.
Read docs/OBJECT_FORMAT.md before changing the versioned schema or relocations.

- Module-local symbols remain scoped; `.globl` exports resolve across modules.
- Deferred expressions are resolved after final alignment and placement, including
  `symbol << 1` octet-pointer initializers and 8/16/32/64-bit data fields.
- Objects carry target and ABI metadata. Compiler assembly emits
  `.ident "dcpu16-abi:octet16"`; contradictory assembler flags and mixed-ABI
  objects are rejected. Raw assembly mode is not the legacy B16 compiler ABI.
- Undefined references, duplicate exports, range/image overflow, malformed
  records, and overlapping relocations produce errors before binary output.
- Link maps expose hardware-word addresses. No hidden entry/startup is inserted;
  the startup object must appear first.
- Installation now retains LLVM's shared-library search path and installs the
  object/linker module beside the Python tool. An installed-tool smoke test
  compiles, assembles separate objects, links, and executes the sample with A=42.
- New local LLVM 23 build: /private/tmp/dcpu-linker-build23/dcpu16-build.
- New local LLVM 22 build: /private/tmp/dcpu-linker-build22/dcpu16-build.
- Current staging source: /private/tmp/dcpu-linker-work; canonical source remains
  /Users/novapy/Dev/llvm-dcpu16 on modernize/llvm23.
- Local O0/O2 execution, relocation/diagnostic tests, cross-module compiled calls,
  shared globals, stored function pointers, both binary word orders, and installed
  toolchain execution pass. The optional Clang bridge is enabled locally.

The linker continuation is verified at implementation commit
4110de2e9bab203d970c0efde07c84241cca596b. Its actual Ubuntu and macOS source
bootstrap/build/O0/O2/installation CI jobs passed:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/41977019496

The first linker commit (54c98eac) exposed a CI-only installation failure:
CMake's automatic link-path RPATH property excludes SDK directories inside the
source project, which is exactly where the default bootstrap places LLVM.
The fix explicitly sets INSTALL_RPATH to LLVM_LIBRARY_DIRS as well. Do not remove
that setting as redundant; an installed executable was also verified against a
project-local SDK, and the default-bootstrap CI install tests now pass.

No release is authorized. Remaining SDK work includes native non-octet bytes,
native LLVM object emission, wide runtime helpers, device/interrupt support,
native Clang, and Nyet integration.

## Freestanding integer runtime continuation (2026-11-05)

Implemented i32/i64 signed/unsigned division/remainder helpers in
modern/runtime/division.ll. CMake builds an O2 dcpu16-rt.dco and installs it plus
source under share/llvm-dcpu16/runtime. Linking is explicit, with no automatic
helper overrides or archive extraction. docs/RUNTIME.md explains usage/semantics.

Backend changes:
- DCPU16Subtarget::initLibcallLoweringInfo explicitly maps the eight helpers;
  unknown upstream triples otherwise have no available runtime implementations.
- CanLowerReturn checks the return CC; LLVM generically demotes >3-word returns
  to a hidden first result pointer, enabling i64 results.
- ParseFunctionArgs handles the first hidden argument's ~0U original index and
  tracks actual OrigArgIndex values rather than incrementing an assumed index.
- Select16 now declares Uses=[EX]. O2 execution exposed reused comparison results
  after intervening EX writes. O0 passed before this metadata fix; O2 did not.
- The existing version-1 i32 register return order is HIGH word in A, LOW in B;
  arguments and memory remain low-first. Preserve binary behavior. Prior ABI.md
  text was wrong; physical register execution now guards the documented order.

Local validation: 38 methods per optimization level, O0/O2 passed against both
assertion-enabled LLVM 23.1.4 and Homebrew LLVM 22.1.8, optional Clang bridge on.
New tests compare full wide results against Python, cover signed extrema,
high-bit divisors, deterministic random pairs, helper zero/overflow behavior,
i64 inline multiply/shifts/add, built runtime, installed compile/link/runtime.
Staging source: /private/tmp/dcpu-runtime-work.
Builds: /private/tmp/dcpu-runtime-build23/dcpu16-build and
/private/tmp/dcpu-runtime-build22/dcpu16-build. O2 runtime is 3551 hardware words
with LLVM 23 on this host; no archive/section stripping yet.

A diagnostic -verify-machineinstrs probe also identified a preexisting tied-use
frame-index representation in ADD16ri before frame elimination. Normal builds
and executions pass; strict machine-verifier cleanup remains separate work.
Implementation commit b8acc95ce21f6c4a79935e2a33bcb882bc35f84c passed actual
source-bootstrap/build/O0/O2/installation CI on ubuntu-24.04 and macos-14:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/41986967332
Both jobs completed successfully. The optional Clang bridge is validated locally;
remote CI skips that optional method. No release was published.
Native non-octet bytes, native LLVM objects, native Clang, full compiler-rt/libc,
peripherals/interrupts, and Nyet integration remain open.

## Strict machine verification (2026-11-05)

Every execution-suite IR compilation and the packaged runtime build now use
-verify-machineinstrs. Fixed inherited tied frame-index ADD operands with a
LEA16fi pseudo, expanded at frame elimination into MOV/SHL/offset arithmetic.
Direct symbol calls now select CALLi rather than register-only CALLr. Select
custom insertion propagates the active call-frame size to newly created blocks.

Local LLVM 23.1.4 and 22.1.8 builds pass 38 tests at each of O0/O2, including the
optional C bridge and installed toolchain/runtime test. Staging/build paths:
/private/tmp/dcpu-word-work and /private/tmp/dcpu-word-build{22,23}/dcpu16-build.
The separate non-octet core port is next; --byte-size=16 remains rejected.

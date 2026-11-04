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
- CTest runs 20 unittest methods (plus parameterized execution cases) at each
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
MC object encoding/linking, native Clang target support, a hosted runtime and
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

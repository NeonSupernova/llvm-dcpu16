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

## Native addressable-unit core experiment (2026-11-06)

Verifier checkpoint 21573e44b77b27ab9382d90e11a5f124fe4f05d4 passed Ubuntu CI:
https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42142366658
macOS runner jobs were cancelled before doing work in attempts 2/3; do not claim
remote macOS verification for this checkpoint. Local LLVM 22/23 O0/O2 execution
with strict machine verification and the optional C bridge passed on macOS.

The initial core series is isolated in modern/llvm-core/patches, with version,
patch and source checksums in series.json. Never apply it to the ordinary SDK.
- Patch 1 adds a module-local B8/B16 DataLayout addressable unit, defaults/explicit
  alignments, scalar/aggregate storage, struct bit offsets, pointer/index storage,
  copying, equality and cache behavior. Integer widths stay exact. No global EVT
  byte-size state was reintroduced from the historical fork.
- Patch 2 fixes constant initializer load reinterpretation to assemble target
  addressable units rather than host octets. The native i32-from-two-i16 test
  failed before the fix. Host-octet array extraction declines native-word input.
- Independent C++ tests pass 86 checks against an assertion-enabled LLVM 23.1.4
  static core build. Tests exercise endian-sensitive reads, subword padding,
  simultaneous octet/native layouts, GEP and IR/bitcode round trips.
- test_word_layout.py validates sources and patch hashes, applies the series,
  builds the necessary LLVM static components, and runs the tests. Fresh patch
  application, exact-repeat handling and refusal of modified source were checked.
  The full script build/test and its repeat both passed locally.
- Source: /private/tmp/dcpu-llvm-word-src (APFS clone of pristine pinned source).
  Independent initial build: /private/tmp/dcpu-llvm-word-build; script validation:
  /private/tmp/dcpu-word-core-validation/{llvm-core-build,layout-tests}.
  Pristine source and ordinary SDK remain /private/tmp/llvm-project-23.1.4.src
  and /private/tmp/llvm23-dcpu-build.
- The Ubuntu experimental workflow passed the verified archive bootstrap, patched
  core build and test suite at both core commit 09d475014f1e849da1259889acff0f4c610ac07b
  and CI prerequisite improvement d9cac4b8f63e8e4c7c1c14871bf43f5a0ddd9b9b:
  https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42152775057
  https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42153353192
  The latter workflow uses existing build tools when present, avoiding an
  unnecessary package installation. Tests and core patches are unchanged.
- Ordinary backend Ubuntu O0/O2/installation CI also passed 09d47501:
  https://github.com/NeonSupernova/llvm-dcpu16/actions/runs/42152775033
  Its macOS job was cancelled before execution. Local macOS validation is the
  platform evidence for this milestone; do not report a remote macOS success.

Read docs/WORD16.md for candidate ABI and audit matrix. This is NOT a complete
non-octet compiler: --byte-size=16 stays rejected. SROA/GVN/InstCombine/value
tracking, memory intrinsic units/fill, SelectionDAG offsets/splits, frames/calls,
assembly constants/strings/alignment, native objects/runtime/linker and frontend
execution all remain to port/validate. The next step is the optimizer and
SelectionDAG audit, not enabling the command-line option on layout-only evidence.

## Forwarding and memory expansion continuation (2026-11-06)

Appended core patches 3/4; original/patched hashes updated in series.json.
- VNCoercion now scales offsets and write lengths using module addressable units,
  extracts native words correctly in either endianness, checks containment in
  initialized storage, and splats memset's i8 value zero-extended per native unit.
- Non-octet memcpy/memmove fallback loops use full-unit i16 loads/stores and GEPs,
  with conservative unit-sized accesses in place of target widening. Memset loop
  expansion zero-extends the i8 operand and writes i16 units. Octet paths retain
  existing target preferences. Atomic/pattern/widened native operations are not
  claimed validated; native C full-word fill remains separate.
- Before patches, new tests failed on native offset-one forwarding and on i8
  accesses in native copy loops. Both regression groups now pass.
- Validation has 3 CTest groups: 86 original layout checks, 10 coercion checks,
  128 loop checks = 224 checks. Loops execute in the upstream LLVM interpreter
  after native IR verification plus host pointer/GEP-index adaptation. Coverage
  includes high word bits, overlap in both directions/self move, zero length,
  known/dynamic sizes, fill guards, and octet/native paths. This is IR semantics
  evidence; native machine code and complete optimizer pipelines are not tested.
- test_word_layout.py now builds LLVMTransformUtils and LLVMInterpreter too.
  Full script/build/test passed locally against assertion-enabled LLVM 23.1.4.
  The fresh four-patch apply and exact repeat were checked on pristine files.
- Staging: /private/tmp/dcpu-optimizer-work. Patched source remains
  /private/tmp/dcpu-llvm-word-src. Build/test: /private/tmp/dcpu-word-core-validation
  (layout-tests was reconfigured for the new staging source; its old cache was
  moved to layout-tests-initial). Additional test build: /private/tmp/dcpu-optimizer-tests.

Remote verification of this continuation is pending. Native --byte-size=16 is
still disabled. Next: SROA and the full GVN/InstCombine/value-tracking audit,
followed by SelectionDAG/frame/emitter and native object/runtime execution.

SROA follow-up audit: a real SROAPass run on the native i32 store 0x11223344 /
GEP i16 offset 1 / load i16 must preserve 0x1122. Current SROA creates an i8
alloca/store of 0x33 and reloads i16. Preserved input:
modern/llvm-core/probes/sroa-native.ll. Local executable probe source/build:
/private/tmp/dcpu-sroa-probe. Its FunctionAnalysisManager registers
PassInstrumentationAnalysis, DominatorTreeAnalysis, AssumptionAnalysis, and
TargetIRAnalysis before invoking SROAPass(SROAOptions()). This confirms the next
optimizer blocker; it is not included as a passing CTest. LLVMScalarOpts was
built locally for this audit but is not required by the checked-in positive
core tests. The first audit must cover SROA's fixed-eight-bit slice sizes,
extraction/insertion shifts, narrow allocas and memset splats.

Continuation CI at ac62a26c reached a genuine portability failure in patch 4:
LowerMemIntrinsics accessed Memset->getModule()->getDataLayout without including
the complete Module declaration. Clang's local PCH masked it; Ubuntu GCC caught
it. Replaced the access with getFunction()->getDataLayout, matching existing
helpers in that file, and updated patch/source checksums. A Clang no-PCH syntax
check reproduced the original error and passed after the fix. GCC 16 no-PCH
syntax checks passed for both modified utility sources and both C++ test files.
The full local script's three groups pass again. Corrected remote CI is pending.
The scoped patch-file whitespace attribute also permits valid blank context
lines at the end of unified diffs; ordinary source whitespace checks stay on.

## Nyet frontend boundary validation (2026-11-06)

While answering integration readiness, tested actual current Nyet emitted IR
without modifying the Nyet checkout (which has unrelated user changes).
- Nyet entry(a:i16,b:i16) addition compiled and ran as DCPU machine words: 20+22=42.
- An i32 divide(a,b) and entry()->divide(100000,7) initially returned 0 at O0.
  Layout-free IR was parsed before assigning the target DL, so implicit i32
  allocas inherited host align4. The backend cannot realign that stack and could
  make invalid aligned-address assumptions.
- Driver now constructs TM first and supplies ParserCallbacks.DataLayout only
  when the input has no DL. LLVM resolves omitted instruction alignments against
  the target while parsing. Explicit incompatible layouts are still rejected.
  Explicit stack align >2 octets is diagnosed until realignment is implemented.
- New implicit-alignment execution and unsupported-alignment diagnostic bring the
  suites to 40 methods at O0/O2. LLVM 23.1.4 and 22.1.8 local suites pass with the
  optional Clang bridge and installed tools/runtime checks enabled.
- Actual Nyet division now passes at O0 and O2 through compiler, CLI assemble -c,
  CLI link with startup + separate dcpu16-rt.dco, flat binary, and CPU CLI:
  A=0, B=14285 (v1 high-first i32 return). O0 executes the real integer helper.
- Artifacts: /private/tmp/dcpu-nyet-proof. Current unpatched-SDK backend builds:
  /private/tmp/dcpu-frontend-build23 and /private/tmp/dcpu-frontend-build22.
- docs/NYET_INTEGRATION.md has reproducing commands and the remaining consumer
  work: Nyet target driver/profile, entry/startup, bounded allocator, supported
  string/container runtime and I/O/device contracts. Native word16 is not needed
  for the first octet16 demo. No Nyet source file was changed.

Frontend fix remote CI is pending. Core continuation CI remains a separate
verification of the patched LLVM experiment; it does not validate native codegen.

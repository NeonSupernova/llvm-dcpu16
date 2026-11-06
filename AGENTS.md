# LLVM DCPU-16 maintenance

Read `PROJECT_MEMORY.md` and `docs/MODERNIZATION.md` before changing this project.
Update project memory with decisions, exact build/test results, and remaining work
at meaningful milestones so another session can continue without guessing.

## Objective

Adopt FrOSt-Foundation/llvm-dcpu16's `new-dcpu16` branch as a maintained,
community-useful modern LLVM toolchain. Nyet is a downstream consumer, not the
toolchain's only supported frontend. Preserve original authorship and licenses.

## Development rules

- Work on `modernize/llvm23`; preserve the historical `new-dcpu16` baseline.
- Pin upstream LLVM source versions and keep backend and LLVM-core patches
  separately reviewable. Prefer upstream interfaces to broad core changes.
- The current baseline is LLVM 23.1.4. LLVM 22.1.8 is available locally for
  compatibility checks. Never describe an unbuilt version as validated.
- A byte-size option is an ABI choice, not just an instruction-selection flag.
  Specify pointer arithmetic, object layout, memory intrinsic units, serialization,
  and cross-module compatibility before declaring a mode supported.
- Keep semantic integer widths unchanged: i8 is eight bits, i16 is sixteen bits.
- Do not silently accept unsupported IR, CPU features, or ABI combinations.
- Use LLVM regression tests and emulator-based execution tests. Compile success
  alone does not demonstrate correct code generation. Test optimized code too.
- Keep generated files, downloaded LLVM sources, and build outputs out of Git.
- Do not modify the sibling Nyet checkout as part of toolchain maintenance.
- User authorization covers modernization, local builds, tests, and project docs.
  Do not publish releases or contact prior maintainers without explicit direction.

## Layout

The root retains LLVM 4-era sources for historical reference; its CMake entry
point now builds `modern/`. Do not restore the old LLVM 4 build accidentally.
`modern/` contains the standalone backend, driver, reference assembler/CPU,
execution tests, and pinned build script. Its current ABI is octet16, described
in `docs/ABI.md`; `--byte-size=16` is deliberately rejected pending a core port.

Build with `python3 modern/scripts/build.py --bootstrap` or provide `--llvm-dir`.
CTest runs binary execution at O0/O2. `--clang /path/to/clang` enables the optional
MSP430-layout C frontend bridge test. Use the build script, not historical justfiles
or LLVM 4-era root build instructions.

`modern/tools/dcpu16.py assemble -c` emits versioned relocatable objects; `link`
combines them into a flat image. Read `docs/OBJECT_FORMAT.md` before changing the
format. Keep local symbols scoped to each input object, defer symbol expressions
until final placement, and reject incompatible ABI declarations and overflowing
relocations. Install `dcpu16_object.py` beside `dcpu16.py` when packaging tools.

## Integer runtime

Read docs/RUNTIME.md for supplied i32/i64 division/remainder helpers. Runtime
objects must be linked explicitly; CMake builds and installs dcpu16-rt.dco.
Preserve the inherited high-word-first register returns (arguments and memory
are low-word first). i64 returns use a hidden result pointer. Keep Select16's
implicit EX use: without it, optimized code can reuse invalid comparisons.

## Non-octet LLVM core

Read docs/WORD16.md before touching the experimental core series. Patch files
and original/patched source hashes live in modern/llvm-core/series.json.
Run modern/scripts/test_word_layout.py with an independent source/build tree;
never replace the ordinary SDK or mix its headers with patched libraries.
Keep core patches separate from backend changes. No global mutable byte width.
The layout/forwarding/memory-loop experiment does not enable --byte-size=16. Remaining optimizer,
SelectionDAG, assembly, memory-intrinsic and object/runtime work remain pending.
Execution compilations and the built runtime now use -verify-machineinstrs.

Core tests execute expanded scalar loops using LLVM's interpreter after adapting
pointers and GEP indices to its host interface. Treat this as loop-semantics
coverage, not native DCPU execution. llvm.memset's i8 value is zero-extended per
native unit; native C full-word fill needs a separate frontend/runtime contract.

For layout-free IR, supply the target DataLayout through the IR-reader callback
before parsing instructions. Assigning it only after parsing leaves implicit
host stack/load/store alignments in place. Preserve explicit input layouts for
compatibility checks; do not override mismatches. Stack realignment is currently
unsupported: reject explicit alloca alignment above two octets.

The core suite now includes real SROAPass tests. Preserve semantic i8/i24 widths
and padding checks while converting memory slice units; a native storage unit
is not an integer-width override. The old SROA probe is now a required positive
fixture. Scalar-return tests fold constant arithmetic after SROA, not a second
unaudited memory optimization pass. Full native code generation stays disabled.

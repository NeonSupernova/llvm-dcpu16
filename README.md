# Modern LLVM for DCPU-16

A community continuation of [FrOSt-Foundation/llvm-dcpu16](https://github.com/FrOSt-Foundation/llvm-dcpu16/tree/new-dcpu16), originally developed by the LLVM DCPU-16 contributors. Original copyright notices and licenses are retained.

The modern backend targets DCPU-16 v1.7 using **LLVM 23.1.4**, with compatibility
for LLVM 22. It builds as a standalone compiler against upstream LLVM libraries;
the old LLVM 4 sources remain in this repository as historical reference.
The root CMake build now selects `modern/`.

This is an experimental compiler, with binary execution regression tests.
It is not yet a complete replacement SDK for the historical word-byte ABI.
Read [the ABI](docs/ABI.md) and [the remaining modernization work](docs/MODERNIZATION.md).

## Build and test

Requirements: Python 3.10+, a C++17 compiler, CMake 3.20+, Ninja, and an LLVM
22/23 development kit including `llvm-tblgen` and the shared LLVM library.
The compiler itself has no Python package dependencies. Installed compilers
retain the library search path to the matching LLVM development kit; they do not
bundle that kit.

Build the pinned LLVM release from its verified source archive, then the backend:

```sh
python3 modern/scripts/build.py --bootstrap
```

Or use an existing development kit:

```sh
python3 modern/scripts/build.py --llvm-dir /path/to/llvm/lib/cmake/llvm
```

The script runs execution tests at `-O0` and `-O2`. Build output defaults to
`build-modern/`; adjust `--work-dir` and `--jobs` as needed. Building upstream LLVM
requires several GiB of disk and takes substantially longer than building the backend.
The LLVM version, download URL, and SHA-256 are pinned in
[modern/llvm-source.json](modern/llvm-source.json).

## Compile LLVM IR

```llvm
define i16 @add(i16 %a, i16 %b) {
  %result = add i16 %a, %b
  ret i16 %result
}
```

```sh
build-modern/dcpu16-build/modern/dcpu16-llc add.ll -O2 --byte-size=8 -o add.s
```

An empty input data layout adopts the DCPU16 layout. An explicitly incompatible
layout is rejected. Do not feed arbitrary host-targeted IR to the compiler:
frontend layouts and runtime declarations must match the target ABI.

The output uses LLVM/GAS-style labels and directives. To produce an image,
combine it with startup assembly and use the reference assembler:

```sh
python3 modern/tools/dcpu16.py assemble program.s -o program.bin --endian big
```

The reference tools also include a binary test CPU without peripherals. They are
deliberately small; unsupported directives/instructions fail explicitly. The
static linker uses versioned DCPU JSON objects. Native LLVM MC object emission
remains pending.

An executable example is in `modern/examples/`: compile `add.ll`, concatenate
`start.s` before the generated assembly, and assemble the result. Running that
image with `python3 modern/tools/dcpu16.py run program.bin --stop 8` prints
registers with `A` equal to 42.

## Separate compilation and linking

Compile and assemble each module independently, then link the objects in image
order. Place startup code first because the image loads at hardware word zero:

```sh
build-modern/dcpu16-build/modern/dcpu16-llc modern/examples/add.ll -O2 -o add.s
python3 modern/tools/dcpu16.py assemble -c modern/examples/start.s -o start.dco
python3 modern/tools/dcpu16.py assemble -c add.s -o add.dco
python3 modern/tools/dcpu16.py link start.dco add.dco -o program.bin --map program.map.json
python3 modern/tools/dcpu16.py run program.bin --stop 8
```

The linker resolves local and exported symbols, aligns modules, applies address
relocations, and checks the final ABI/address-space limits. `--endian big|little`
chooses binary word serialization. The optional map reports hardware-word
addresses. Compiler output carries an ABI declaration; contradictory assembler
flags and mixed-ABI objects are rejected. See [the object format](docs/OBJECT_FORMAT.md)
for supported relocations and limitations.

## Integer runtime

The build produces `build-modern/dcpu16-build/modern/runtime/dcpu16-rt.dco`.
Link it after your application objects when using signed or unsigned i32/i64
integer division or remainder:

```sh
python3 modern/tools/dcpu16.py link start.dco app.dco \
  build-modern/dcpu16-build/modern/runtime/dcpu16-rt.dco -o program.bin
```

Installation places the object and its LLVM IR source under
`share/llvm-dcpu16/runtime/`. See [runtime support](docs/RUNTIME.md) for symbols,
edge behavior, and limitations. This is a freestanding integer runtime; a hosted
C library and floating-point support remain open.

## C frontend bridge

Clang's MSP430 frontend has a compatible octet/int16/pointer16 data layout. It can
produce freestanding LLVM IR for this backend; the generated assembly is DCPU16:

```sh
clang --target=msp430 -ffreestanding -fno-builtin -S -emit-llvm example.c -o example.ll
build-modern/dcpu16-build/modern/dcpu16-llc example.ll -O2 -o example.s
```

Use a Clang build with MSP430 support. No MSP430 machine code is involved. This is
a frontend bridge, not native `clang --target=dcpu16` support or a hosted C library.
To include its end-to-end execution test, pass `--clang /path/to/clang` to the build script.

## Contributing

Start with [AGENTS.md](AGENTS.md), [PROJECT_MEMORY.md](PROJECT_MEMORY.md), and
[MODERNIZATION.md](docs/MODERNIZATION.md). New backend changes need execution
coverage, including optimized code. Keep non-octet LLVM-core changes separate
from backend changes. Nyet is a downstream consumer, not a dependency.

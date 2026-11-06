# Nyet integration status

A basic Nyet-to-DCPU path is demonstrated with the current **octet16** ABI.
Native 16-bit addressable bytes are not required for this first milestone.
Nyet's lexer, parser and type checker were not modified for the demonstration.

Verified examples (2026-11-06):

- A Nyet `entry(a:i16, b:i16)` addition function compiles through LLVM IR and
  DCPU assembly to actual machine words; emulator arguments 20 and 22 return 42.
- A Nyet i32 division function and no-argument entry calling `divide(100000, 7)`
  compile at O0, assemble separately from startup and the supplied integer
  runtime, link, and execute with result 14285. O0 keeps the runtime call live.

These examples establish a frontend/backend boundary, not full language/runtime
support or a built-in Nyet target option. Nyet is not pinned as a toolchain
dependency; these commands require a compatible frontend checkout.

## Reproduce the i32 runtime example

In a Nyet checkout, create `demo.no`:

```lisp
(fn divide (a:i32 b:i32) -> i32
  (/ a b))
(fn entry () -> i32
  (divide 100000 7))
```

Use a compiler containing the layout-before-parse fix described below:

```sh
mkdir -p /tmp/nyet-dcpu-demo
python3 -m pynyet.driver build demo.no --emit-llvm -o /tmp/nyet-dcpu-demo/app
/path/to/dcpu16-llc /tmp/nyet-dcpu-demo/app.ll -O0 --byte-size=8 -o /tmp/nyet-dcpu-demo/app.s
```

Save startup assembly as `/tmp/nyet-dcpu-demo/start.s`:

```asm
.globl halt
SET SP, 0x7f00
JSR entry
halt:
SET PC, halt
```

From the toolchain checkout, assemble and link explicitly:

```sh
python3 modern/tools/dcpu16.py assemble -c /tmp/nyet-dcpu-demo/start.s -o /tmp/nyet-dcpu-demo/start.dco
python3 modern/tools/dcpu16.py assemble -c /tmp/nyet-dcpu-demo/app.s -o /tmp/nyet-dcpu-demo/app.dco
python3 modern/tools/dcpu16.py link /tmp/nyet-dcpu-demo/start.dco /tmp/nyet-dcpu-demo/app.dco \
  build-modern/dcpu16-build/modern/runtime/dcpu16-rt.dco -o /tmp/nyet-dcpu-demo/app.bin
python3 modern/tools/dcpu16.py run /tmp/nyet-dcpu-demo/app.bin --stop 4
```

At halt, A is 0 and B is 14285. Version-1 i32 returns use A for the high word,
B for the low word. `--stop 4` corresponds to this exact startup sequence.

## Frontend boundary fix

Layout-free textual IR must acquire the DCPU DataLayout **during parsing**.
LLVM assigns omitted load/store/alloca alignments while parsing; changing the
module layout afterwards leaves host default alignments behind. A Nyet i32
stack/division case exposed incorrect O0 execution from that mismatch.

The driver now creates its target first and supplies an IR-reader callback that
fills only an absent DataLayout. Explicit incompatible layouts are still rejected.
Explicit stack alignment above two octets is diagnosed because dynamic stack
realignment is not implemented. Positive implicit-alignment execution and the
unsupported-alignment diagnostic are covered in the O0/O2 regression suite.

## Next integration milestones

1. Add a Nyet build target that invokes the DCPU compiler, object assembler and
   linker, with defined entry/startup and inspectable emulator execution.
2. Introduce a Nyet target profile for pointer-sized types and object layout.
   Current Nyet `usize`, allocation declarations, container headers and size
   calculations contain host-oriented assumptions. Audit them rather than
   changing semantic i32/i64 widths to match the machine word.
3. Port a bounded allocator and the supported string/collection operations to
   a freestanding DCPU runtime. Hosted malloc/printf/file/thread APIs are not
   supplied by the compiler or the reference CPU.
4. Define device output, errors and unavailable-feature diagnostics. Floating
   point, threads/async OS services and hosted file I/O cannot be assumed.
5. Run representative Nyet programs at O0/O2 through the complete binary path,
   including memory pressure, aggregate layouts, pointers and runtime calls.

Native word16 remains a parallel toolchain task described in WORD16.md. Do not
make initial octet16 integration depend on completion of the non-octet core port.

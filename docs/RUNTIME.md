# Freestanding integer runtime

The default build compiles `modern/runtime/division.ll` with the built DCPU16
compiler at O2, then assembles `modern/runtime/dcpu16-rt.dco` in the build tree.
`cmake --install` installs both object and source in
`share/llvm-dcpu16/runtime/`. No host runtime library is used.

## Supported operations

| LLVM operation | i32 helper | i64 helper |
| --- | --- | --- |
| udiv | `__udivsi3` | `__udivdi3` |
| sdiv | `__divsi3` | `__divdi3` |
| urem | `__umodsi3` | `__umoddi3` |
| srem | `__modsi3` | `__moddi3` |

The backend explicitly maps these operations to the supplied symbols. Assemble
application modules separately, then pass the runtime object to `dcpu16.py link`
after startup and application objects. Inclusion is explicit. The reference
linker has no archives or dead-section elimination: the complete runtime object
is included, and duplicate helper definitions are diagnosed. Supply your own
compatible helper object instead when overriding the implementation.

The helpers use the octet16 version-1 calling convention in ABI.md, including
high-word-first i32 register returns and hidden result pointers for i64 returns.
They are specific to this ABI, not drop-in objects for the historical B16 port
or another compiler's calling convention.

## Arithmetic behavior

Unsigned helpers use restoring binary division with 32 or 64 iterations. Signed
helpers divide unsigned magnitudes, then apply the signs: quotient truncates
toward zero and nonzero remainder follows the dividend's sign. Arithmetic in the
implementation is modular, so signed minimum magnitudes are representable.

Direct calls to these helpers return zero for both quotient and remainder when
the divisor is zero. Signed minimum divided by minus one wraps to the signed
minimum; its remainder is zero. These are helper implementation choices. LLVM
`udiv`/`sdiv`/`urem`/`srem` by zero and signed minimum divided by minus one retain
their LLVM undefined/poison semantics; optimization may remove such source
operations before helper lowering. Applications needing defined behavior must
guard the operation or call the helper explicitly.

## Validation and limits

Execution tests compile the source runtime at O0 and O2, link it independently,
and compare full results against Python integer arithmetic. Cases include
boundary values, high-bit divisors, signed extremes, deterministic random input,
wide returns, and direct helper edge behavior. Additional tests execute the
built O2 runtime and an installed toolchain's runtime through separate objects.

Add, subtract, multiply, and shifts through i64 are lowered inline; representative
wide returns and operations are tested. This is not a complete compiler-rt port.
Floating point, arbitrary integer widths, hosted libc, interrupts and device
support still need implementation and independent validation. The test CPU
models instructions, not physical device timing or peripherals.

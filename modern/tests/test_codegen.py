"""Compile modern LLVM IR, assemble it, and execute actual DCPU words."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dcpu16 import assemble, invoke

COMPILER = os.environ.get("DCPU16_LLC", "dcpu16-llc")


def compile_ir(ir):
    with tempfile.TemporaryDirectory() as tmp:
        source, output = Path(tmp) / "input.ll", Path(tmp) / "output.s"
        source.write_text(ir)
        result = subprocess.run([COMPILER, str(source), "-verify-machineinstrs", "-O" + os.environ.get("DCPU16_OPT_LEVEL", "2"), "-o", str(output)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(f"compiler failed ({result.returncode}):\n{result.stderr}")
        return output.read_text()


class EncodingTests(unittest.TestCase):
    def test_spec_encoding(self):
        self.assertEqual(assemble("SET A, 0x30")[0], [0x7C01, 0x30])
        self.assertEqual(assemble("SET [0x1000], 0x20")[0], [0x7FC1, 0x20, 0x1000])
        self.assertEqual(assemble("JSR 0x1000")[0], [0x7C20, 0x1000])

    def test_binary_cli(self):
        import json
        tool = Path(__file__).resolve().parents[1] / "tools/dcpu16.py"
        source = "SET SP, 0x7f00\nSET A, 20\nSET B, 22\nJSR add\nhalt:\nSET PC, halt\nadd:\nADD A, B\nSET PC, POP\n"
        with tempfile.TemporaryDirectory() as tmp:
            asm, binary = Path(tmp) / "program.s", Path(tmp) / "program.bin"
            asm.write_text(source)
            for endian, prefix in [("big", bytes.fromhex("7f617f00")), ("little", bytes.fromhex("617f007f"))]:
                with self.subTest(endian=endian):
                    subprocess.run([sys.executable, str(tool), "assemble", str(asm), "-o", str(binary), "--endian", endian], check=True)
                    self.assertEqual(binary.read_bytes()[:4], prefix)
                    result = subprocess.run([sys.executable, str(tool), "run", str(binary), "--endian", endian, "--stop", "8"], check=True, capture_output=True, text=True)
                    self.assertEqual(json.loads(result.stdout)["A"], 42)


class CodegenTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("DCPU16_CLANG"), "set DCPU16_CLANG for the optional C frontend bridge test")
    def test_c_frontend(self):
        with tempfile.TemporaryDirectory() as tmp:
            c, ll = Path(tmp) / "input.c", Path(tmp) / "input.ll"
            c.write_text("unsigned short entry(unsigned short n) { volatile unsigned short x[2]; x[0] = n; return (x[0] + 3) * 7; }\n")
            subprocess.run([os.environ["DCPU16_CLANG"], "-target", "msp430", "-ffreestanding",
                            "-fno-builtin", "-Xclang", "-disable-O0-optnone", "-S", "-emit-llvm",
                            str(c), "-o", str(ll)], check=True, capture_output=True)
            assembly = compile_ir(ll.read_text())
        self.assertEqual(invoke(assembly, "entry", (100,)).reg[0], 721)

    def test_arithmetic(self):
        for op, a, b, expected in [
            ("add", 65535, 2, 1), ("sub", 0, 1, 65535), ("mul", 123, 91, 11193),
            ("and", 0xCAFE, 0xFF00, 0xCA00), ("or", 0x1234, 0x00F0, 0x12F4),
            ("xor", 0xFFFF, 0xFF00, 255), ("udiv", 65535, 13, 5041),
            ("sdiv", -101, 10, -10), ("urem", 65535, 13, 2),
        ]:
            with self.subTest(op=op):
                assembly = compile_ir(f"define i16 @entry(i16 %a, i16 %b) {{ %r = {op} i16 %a, %b ret i16 %r }}")
                self.assertEqual(invoke(assembly, "entry", (a, b)).reg[0], expected & 65535)

    def test_shifts(self):
        for op, value, amount, expected in [
            ("shl", 5, 4, 80), ("lshr", 0x8000, 4, 0x800),
            ("ashr", 0x8000, 4, 0xF800), ("lshr", 0x8000, 0, 0x8000),
        ]:
            with self.subTest(op=op, amount=amount):
                assembly = compile_ir(f"define i16 @entry(i16 %a, i16 %b) {{ %r = {op} i16 %a, %b ret i16 %r }}")
                self.assertEqual(invoke(assembly, "entry", (value, amount)).reg[0], expected)

    def test_comparisons(self):
        pairs = [(0, 0), (0, 1), (1, 0), (0xFFFF, 0), (0x8000, 0x7FFF), (0x7FFF, 0x8000)]
        for predicate in ("eq", "ne", "ult", "ule", "ugt", "uge", "slt", "sle", "sgt", "sge"):
            assembly = compile_ir(f"""define i16 @entry(i16 %a, i16 %b) {{
              %c = icmp {predicate} i16 %a, %b
              %r = select i1 %c, i16 123, i16 456
              ret i16 %r
            }}""")
            for a, b in pairs:
                with self.subTest(predicate=predicate, a=a, b=b):
                    lhs, rhs = a, b
                    if predicate.startswith("s"):
                        lhs = a - 65536 if a & 0x8000 else a
                        rhs = b - 65536 if b & 0x8000 else b
                    relation = predicate.lstrip("us")
                    truth = {"eq": lhs == rhs, "ne": lhs != rhs, "lt": lhs < rhs,
                             "le": lhs <= rhs, "gt": lhs > rhs, "ge": lhs >= rhs}[relation]
                    self.assertEqual(invoke(assembly, "entry", (a, b)).reg[0], 123 if truth else 456)

    def test_calls_and_recursion(self):
        assembly = compile_ir("""
        define i16 @factorial(i16 %n) noinline {
          %done = icmp ule i16 %n, 1
          br i1 %done, label %base, label %step
        base:
          ret i16 1
        step:
          %n1 = sub i16 %n, 1
          %prev = call i16 @factorial(i16 %n1)
          %r = mul i16 %prev, %n
          ret i16 %r
        }
        """)
        self.assertEqual(invoke(assembly, "factorial", (6,)).reg[0], 720)

    def test_boolean_result(self):
        for predicate, a, b, expected in [("eq", 42, 42, 1), ("ne", 42, 42, 0),
                                         ("ult", 0, 65535, 1), ("slt", 65535, 0, 1)]:
            with self.subTest(predicate=predicate):
                assembly = compile_ir(f"""define i16 @entry(i16 %a, i16 %b) {{
                  %cmp = icmp {predicate} i16 %a, %b
                  %result = zext i1 %cmp to i16
                  ret i16 %result
                }}""")
                self.assertEqual(invoke(assembly, "entry", (a, b)).reg[0], expected)

    def test_stack_memory(self):
        assembly = compile_ir("""define i16 @entry(i16 %a, i16 %b) {
          %array = alloca [4 x i16], align 2
          %p = getelementptr [4 x i16], ptr %array, i16 0, i16 2
          store volatile i16 %a, ptr %p, align 2
          %x = load volatile i16, ptr %p, align 2
          %r = add i16 %x, %b
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (1234, 5678)).reg[0], 6912)

    def test_byte_memory(self):
        assembly = compile_ir("""
        define void @write(ptr %p, i16 %v) noinline {
          %byte = trunc i16 %v to i8
          store i8 %byte, ptr %p, align 1
          ret void
        }
        define i16 @read(ptr %p) noinline {
          %v = load i8, ptr %p, align 1
          %r = zext i8 %v to i16
          ret i16 %r
        }
        define i16 @entry(i16 %a, i16 %b) {
          %array = alloca [2 x i8], align 2
          %p1 = getelementptr i8, ptr %array, i16 1
          call void @write(ptr %array, i16 %a)
          call void @write(ptr %p1, i16 %b)
          %x = call i16 @read(ptr %array)
          %y = call i16 @read(ptr %p1)
          %yshift = shl i16 %y, 8
          %r = or i16 %x, %yshift
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (0xAB, 0xCD)).reg[0], 0xCDAB)

    def test_stack_arguments(self):
        assembly = compile_ir("""
        define i16 @sum4(i16 %a, i16 %b, i16 %c, i16 %d) noinline {
          %ab = add i16 %a, %b
          %cd = add i16 %c, %d
          %r = add i16 %ab, %cd
          ret i16 %r
        }
        define i16 @entry(i16 %n) {
          %r = call i16 @sum4(i16 %n, i16 2, i16 3, i16 4)
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (100,)).reg[0], 109)

    def test_wide_arguments(self):
        assembly = compile_ir("""
        define i32 @sum(i32 %a, i32 %b) noinline {
          %r = add i32 %a, %b
          ret i32 %r
        }
        define i16 @entry(i16 %low, i16 %high) {
          %lo = zext i16 %low to i32
          %hi = zext i16 %high to i32
          %upper = shl i32 %hi, 16
          %a = or i32 %lo, %upper
          %r = call i32 @sum(i32 %a, i32 65537)
          %rhi = lshr i32 %r, 16
          %hi16 = trunc i32 %rhi to i16
          %lo16 = trunc i32 %r to i16
          %answer = xor i16 %hi16, %lo16
          ret i16 %answer
        }""")
        self.assertEqual(invoke(assembly, "entry", (65535, 41)).reg[0], 43)

    def test_globals(self):
        assembly = compile_ir("""
        @word = global i16 123, align 2
        @bytes = global [3 x i8] [i8 42, i8 127, i8 -128], align 1
        define i16 @entry(i16 %n) {
          store volatile i16 %n, ptr @word, align 2
          %w = load volatile i16, ptr @word, align 2
          %p = getelementptr [3 x i8], ptr @bytes, i16 0, i16 2
          %v = load i8, ptr %p, align 1
          %s = sext i8 %v to i16
          %r = add i16 %w, %s
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (1000,)).reg[0], 872)

    def test_unaligned_word_memory(self):
        assembly = compile_ir("""
        define void @write(ptr %p, i16 %v) noinline {
          store i16 %v, ptr %p, align 1
          ret void
        }
        define i16 @read(ptr %p) noinline {
          %v = load i16, ptr %p, align 1
          ret i16 %v
        }
        define i16 @entry(i16 %v) {
          %data = alloca [4 x i8], align 2
          %odd = getelementptr i8, ptr %data, i16 1
          call void @write(ptr %odd, i16 %v)
          %r = call i16 @read(ptr %odd)
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (0xABCD,)).reg[0], 0xABCD)

    def test_indirect_call(self):
        assembly = compile_ir("""
        @function = global ptr @increment, align 2
        define i16 @increment(i16 %n) noinline {
          %r = add i16 %n, 1
          ret i16 %r
        }
        define i16 @entry(i16 %n) {
          %f = load volatile i16, ptr @function, align 2
          %fp = inttoptr i16 %f to ptr
          %r = call i16 %fp(i16 %n)
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (999,)).reg[0], 1000)

    def test_memcpy(self):
        assembly = compile_ir("""
        declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1 immarg)
        define i16 @entry(i16 %n) {
          %source = alloca [2 x i16], align 2
          %dest = alloca [2 x i16], align 2
          store volatile i16 %n, ptr %source, align 2
          call void @llvm.memcpy.p0.p0.i16(ptr align 2 %dest, ptr align 2 %source, i16 2, i1 false)
          %r = load volatile i16, ptr %dest, align 2
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry", (12345,)).reg[0], 12345)

    def test_memory_intrinsic_loops(self):
        assembly = compile_ir("""
        @initial = constant [8 x i8] c"12345678", align 2
        declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1 immarg)
        declare void @llvm.memmove.p0.p0.i16(ptr, ptr, i16, i1 immarg)
        declare void @llvm.memset.p0.i16(ptr, i8, i16, i1 immarg)
        define void @move(ptr %dest, ptr %src, i16 %n) noinline {
          call void @llvm.memmove.p0.p0.i16(ptr %dest, ptr %src, i16 %n, i1 false)
          ret void
        }
        define void @fill(ptr %dest, i16 %n) noinline {
          call void @llvm.memset.p0.i16(ptr %dest, i8 -86, i16 %n, i1 false)
          ret void
        }
        define i16 @entry() {
          %a = alloca [8 x i8], align 2
          call void @llvm.memcpy.p0.p0.i16(ptr align 2 %a, ptr align 2 @initial, i16 8, i1 false)
          %one = getelementptr i8, ptr %a, i16 1
          call void @move(ptr %one, ptr %a, i16 7)
          %two = getelementptr i8, ptr %a, i16 2
          call void @fill(ptr %two, i16 3)
          %three = getelementptr i8, ptr %a, i16 3
          %byte = load i8, ptr %three, align 1
          %wide = zext i8 %byte to i16
          %six = getelementptr i8, ptr %a, i16 6
          %word = load i16, ptr %six, align 2
          %r = add i16 %wide, %word
          ret i16 %r
        }""")
        self.assertEqual(invoke(assembly, "entry").reg[0], 0x37E0)


class DiagnosticTests(unittest.TestCase):
    def test_non_octet_mode_is_explicitly_unsupported(self):
        result = subprocess.run([COMPILER, "--byte-size=16"], input="", capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("only --byte-size=8 is supported", result.stderr)

    def test_incompatible_data_layout(self):
        result = subprocess.run([COMPILER], input='target datalayout = "e-p:64:64"\ndefine i16 @entry() { ret i16 0 }\n', capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("data layout does not match", result.stderr)

    def test_float_diagnostic(self):
        result = subprocess.run([COMPILER], input='define float @entry() { ret float 1.0 }\n', capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not yet supported", result.stderr)


if __name__ == "__main__":
    unittest.main()

"""Execute wide operations through separately compiled freestanding helpers."""
import os
from pathlib import Path
import random
import unittest

from test_codegen import compile_ir
from dcpu16 import CPU, invoke
from dcpu16_object import assemble_object, link_objects

START = '.globl halt\nSET SP, 0x7f00\nJSR exercise\nhalt:\nSET PC, halt\n'


def program(bits, operation, helper=None):
    declaration = f'declare i{bits} @{helper}(i{bits}, i{bits})' if helper else ''
    expression = (f'call i{bits} @{helper}(i{bits} %a, i{bits} %b)' if helper
                  else f'{operation} i{bits} %a, %b')
    return f'''
      @lhs = global i{bits} 0, align 2
      @rhs = global i{bits} 0, align 2
      @result = global i{bits} 0, align 2
      {declaration}
      define i{bits} @calculate(i{bits} %a, i{bits} %b) noinline {{
        %r = {expression}
        ret i{bits} %r
      }}
      define void @exercise() {{
        %a = load i{bits}, ptr @lhs, align 2
        %b = load i{bits}, ptr @rhs, align 2
        %r = call i{bits} @calculate(i{bits} %a, i{bits} %b)
        store i{bits} %r, ptr @result, align 2
        ret void
      }}
    '''


def execute(image, mapping, bits, a, b):
    symbols = mapping['symbols']
    cpu = CPU(image)
    for name, value in [('lhs', a), ('rhs', b)]:
        for index in range(bits // 16):
            cpu.ram[symbols[name] + index] = (value >> (index * 16)) & 65535
    cpu.run(symbols['halt'], limit=1000000)
    return sum(cpu.ram[symbols['result'] + i] << (16 * i) for i in range(bits // 16))


class RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(__file__).resolve().parents[1] / 'runtime/division.ll'
        cls.runtime = assemble_object(compile_ir(source.read_text()))

    def check_operation(self, bits, op, pairs, helper=None):
        app = assemble_object(compile_ir(program(bits, op, helper)))
        image, mapping = link_objects([assemble_object(START), app, self.runtime])
        mask = (1 << bits) - 1
        for a, b in pairs:
            with self.subTest(bits=bits, op=op, a=a, b=b, helper=helper):
                if op in ('udiv', 'sdiv', 'urem', 'srem'):
                    q = abs(a) // abs(b) if b else 0
                    if (a < 0) != (b < 0):
                        q = -q
                    expected = q if op.endswith('div') else (a - q * b if b else 0)
                else:
                    expected = {'add': lambda: a + b, 'mul': lambda: a * b,
                                'shl': lambda: a << b, 'lshr': lambda: a >> b,
                                'ashr': lambda: a >> b}[op]()
                self.assertEqual(execute(image, mapping, bits, a, b), expected & mask)

    def test_unsigned_division_and_remainder(self):
        rng = random.Random(0xDC16)
        for bits in (32, 64):
            maximum, high = (1 << bits) - 1, 1 << (bits - 1)
            pairs = [(0, 1), (1, maximum), (maximum, 1), (maximum, maximum),
                     (maximum, high + 1), (high, 3), (maximum, 65537),
                     (0x12345678, 257), (high - 1, high), (high + 1, high)]
            pairs += [(rng.getrandbits(bits), rng.getrandbits(bits) or 1) for _ in range(12)]
            for op in ('udiv', 'urem'):
                self.check_operation(bits, op, pairs)

    def test_signed_division_and_remainder(self):
        rng = random.Random(0x51D)
        for bits in (32, 64):
            minimum, maximum = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
            pairs = [(minimum, 1), (minimum, 3), (minimum, minimum), (maximum, -1),
                     (-101, 10), (101, -10), (-101, -10), (0, -1), (1, minimum)]
            pairs += [(rng.randrange(minimum, maximum), rng.randrange(minimum, maximum) or 1) for _ in range(12)]
            for op in ('sdiv', 'srem'):
                self.check_operation(bits, op, pairs)

    def test_direct_helpers_defined_edge_behavior(self):
        for bits, suffix in [(32, 'si3'), (64, 'di3')]:
            for op, stem in [('udiv', '__udiv'), ('urem', '__umod'), ('sdiv', '__div'), ('srem', '__mod')]:
                pairs = [(0, 0), (123, 0)]
                if op.startswith('s'):
                    pairs.append((-(1 << (bits - 1)), -1))
                self.check_operation(bits, op, pairs, stem + suffix)

    def test_wide_returns_and_inline_arithmetic(self):
        for op, pairs in [('add', [(0x123456789abcdef0, 0xfedcba9876543210)]),
                          ('mul', [(0x123456789, 0xabcdef123)]),
                          ('shl', [(0x123456789abcdef0, x) for x in (0, 1, 16, 32, 63)]),
                          ('lshr', [(0xfedcba9876543210, x) for x in (0, 1, 16, 32, 63)]),
                          ('ashr', [(-0x123456789abcdef, x) for x in (0, 1, 16, 32, 63)])]:
            self.check_operation(64, op, pairs)

    def test_frontend_ir_with_implicit_stack_alignment(self):
        # Match layout-free textual frontend IR (including Nyet): alignments
        # omitted here must be resolved using the DCPU layout during parsing.
        ir = program(32, 'sdiv').replace('%r = sdiv i32 %a, %b', """
          %left = alloca i32
          %right = alloca i32
          store i32 %a, ptr %left
          store i32 %b, ptr %right
          %x = load i32, ptr %left
          %y = load i32, ptr %right
          %r = sdiv i32 %x, %y
        """)
        image, mapping = link_objects([assemble_object(START),
                                      assemble_object(compile_ir(ir)), self.runtime])
        for a, b, expected in [(100000, 7, 14285), (-100000, 7, -14285),
                               (0x12345678, 257, 0x12345678 // 257)]:
            with self.subTest(a=a, b=b):
                self.assertEqual(execute(image, mapping, 32, a, b), expected & 0xffffffff)

    def test_physical_return_register_order(self):
        assembly = compile_ir('define i32 @entry() { ret i32 305419896 }')
        cpu = invoke(assembly, 'entry')
        self.assertEqual(cpu.reg[:2], [0x1234, 0x5678])

    @unittest.skipUnless(os.environ.get('DCPU16_RUNTIME'), 'set DCPU16_RUNTIME to test the packaged object')
    def test_built_runtime_object(self):
        import json
        runtime = json.loads(Path(os.environ['DCPU16_RUNTIME']).read_text())
        app = assemble_object(compile_ir(program(64, 'udiv')))
        image, mapping = link_objects([assemble_object(START), app, runtime])
        self.assertEqual(execute(image, mapping, 64, 0xfedcba9876543210, 65537),
                         0xfedcba9876543210 // 65537)

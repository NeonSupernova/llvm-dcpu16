#!/usr/bin/env python3
"""Reference assembler, static linker, and DCPU-16 test machine (no host syscalls).

The CPU executes encoded 16-bit words, independently of LLVM machine nodes.
Unsupported directives/opcodes fail rather than being treated as no-ops.
"""
import ast
import re

REGS = {name: i for i, name in enumerate("A B C X Y Z I J".split())}
BASIC = dict(zip("SET ADD SUB MUL MLI DIV DVI MOD MDI AND BOR XOR SHR ASR SHL".split(), range(1, 16)))
BASIC.update(dict(zip("IFB IFC IFE IFN IFG IFA IFL IFU".split(), range(0x10, 0x18))))
BASIC.update(ADX=0x1A, SBX=0x1B, STI=0x1E, STD=0x1F)
SPECIAL = {"JSR": 1}


def expression(text, labels, unresolved=False, truncate=True):
    text = text.strip()
    def resolve(match):
        name = match.group()
        if name in labels:
            return str(labels[name])
        if unresolved:
            return "0"
        raise ValueError(f"undefined symbol: {name}")
    text = re.sub(r"(?<![\w.$])[A-Za-z_.$][\w.$]*", resolve, text, flags=re.ASCII)
    node = ast.parse(text, mode="eval").body

    def value(n):
        if isinstance(n, ast.Constant) and type(n.value) is int:
            return n.value
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.USub, ast.UAdd)):
            return -value(n.operand) if isinstance(n.op, ast.USub) else value(n.operand)
        if isinstance(n, ast.BinOp):
            left, right = value(n.left), value(n.right)
            if isinstance(n.op, ast.Add): return left + right
            if isinstance(n.op, ast.Sub): return left - right
            if isinstance(n.op, ast.LShift) and 0 <= right <= 63: return left << right
            if isinstance(n.op, ast.RShift) and 0 <= right <= 63: return left >> right
            if isinstance(n.op, ast.BitAnd): return left & right
        raise ValueError(f"unsupported assembly expression: {text}")
    result = value(node)
    return result & 0xFFFF if truncate else result


def operand(text, is_a):
    text = text.strip()
    upper = text.upper()
    if upper in REGS:
        return REGS[upper], []
    if upper in ("PUSH", "POP"):
        if (upper == "POP") != is_a:
            raise ValueError("POP is a source; PUSH is a destination")
        return 0x18, []
    if upper in ("PEEK", "[SP]"):
        return 0x19, []
    if upper.startswith("PICK "):
        return 0x1A, [text[5:]]
    if upper in ("SP", "PC", "EX"):
        return {"SP": 0x1B, "PC": 0x1C, "EX": 0x1D}[upper], []
    if text.startswith("[") and text.endswith("]"):
        inner = text[1:-1].strip()
        if inner.upper() in REGS:
            return 8 + REGS[inner.upper()], []
        for reg, number in REGS.items():
            found = re.search(r"(?<![\w.$])" + reg + r"(?![\w.$])", inner, re.I)
            if found:
                offset = inner[:found.start()] + "0" + inner[found.end():]
                return 0x10 + number, [offset]
        return 0x1E, [inner]
    return 0x1F, [text]


def assemble(source):
    from dcpu16_object import assemble_object, declared_abi, link_objects
    obj = assemble_object(source, abi=declared_abi(source) or "raw")
    words, _ = link_objects([obj])
    return words, {name: record["offset"] for name, record in obj["symbols"].items()}


def signed(n):
    return n - 65536 if n & 0x8000 else n


class CPU:
    def __init__(self, image):
        self.ram = [0] * 65536
        self.ram[:len(image)] = image
        self.reg = [0] * 8
        self.sp = 0
        self.pc = 0
        self.ex = 0

    def fetch(self):
        value = self.ram[self.pc]
        self.pc = (self.pc + 1) & 0xFFFF
        return value

    def reference(self, code, is_a):
        if code < 8:
            return "reg", code
        if code < 0x10:
            return "ram", self.reg[code - 8]
        if code < 0x18:
            return "ram", (self.reg[code - 0x10] + self.fetch()) & 0xFFFF
        if code == 0x18:
            if is_a:
                address = self.sp
                self.sp = (self.sp + 1) & 0xFFFF
            else:
                self.sp = (self.sp - 1) & 0xFFFF
                address = self.sp
            return "ram", address
        if code == 0x19:
            return "ram", self.sp
        if code == 0x1A:
            return "ram", (self.sp + self.fetch()) & 0xFFFF
        if code in (0x1B, 0x1C, 0x1D):
            return ("sp", "pc", "ex")[code - 0x1B], 0
        if code == 0x1E:
            return "ram", self.fetch()
        if code == 0x1F:
            return "literal", self.fetch()
        return "literal", (code - 0x21) & 0xFFFF

    def read(self, ref):
        kind, index = ref
        return index if kind == "literal" else (getattr(self, kind)[index] if kind in ("ram", "reg") else getattr(self, kind))

    def write(self, ref, value):
        kind, index = ref
        if kind == "literal":
            return
        if kind in ("ram", "reg"):
            getattr(self, kind)[index] = value & 0xFFFF
        else:
            setattr(self, kind, value & 0xFFFF)

    def skip(self):
        while True:
            word = self.fetch()
            op, b, a = word & 31, (word >> 5) & 31, word >> 10
            for code in ([a, b] if op else [a]):
                if 0x10 <= code <= 0x17 or code in (0x1A, 0x1E, 0x1F):
                    self.fetch()
            if not 0x10 <= op <= 0x17:
                return

    def step(self):
        word = self.fetch()
        op, bc, ac = word & 31, (word >> 5) & 31, word >> 10
        a = self.read(self.reference(ac, True))
        if not op:
            if bc != 1:
                raise ValueError(f"unsupported special opcode {bc:#x}")
            self.sp = (self.sp - 1) & 0xFFFF
            self.ram[self.sp] = self.pc
            self.pc = a
            return
        dest = self.reference(bc, False)
        b = self.read(dest)
        if 0x10 <= op <= 0x17:
            condition = [bool(b & a), not (b & a), b == a, b != a, b > a,
                         signed(b) > signed(a), b < a, signed(b) < signed(a)][op - 0x10]
            if not condition:
                self.skip()
            return
        if op == 1: result = a
        elif op in (2, 0x1A):
            result = b + a + (self.ex if op == 0x1A else 0)
            self.ex = result >> 16
        elif op in (3, 0x1B):
            result = b - a + (self.ex if op == 0x1B else 0)
            self.ex = 0xFFFF if result < 0 else (1 if result > 0xFFFF else 0)
        elif op in (4, 5):
            result = b * a if op == 4 else signed(b) * signed(a)
            self.ex = (result >> 16) & 0xFFFF
        elif op in (6, 7):
            divisor, dividend = (a, b) if op == 6 else (signed(a), signed(b))
            result = int(dividend / divisor) if divisor else 0
            self.ex = int((dividend << 16) / divisor) & 0xFFFF if divisor else 0
        elif op in (8, 9):
            divisor, dividend = (a, b) if op == 8 else (signed(a), signed(b))
            result = dividend - int(dividend / divisor) * divisor if divisor else 0
        elif op == 10: result = b & a
        elif op == 11: result = b | a
        elif op == 12: result = b ^ a
        elif op in (13, 14):
            value = b if op == 13 else signed(b)
            result = value >> a
            self.ex = ((value << 16) >> a) & 0xFFFF
        elif op == 15:
            result = b << a
            self.ex = (result >> 16) & 0xFFFF
        else:
            raise ValueError(f"unsupported opcode {op:#x}")
        self.write(dest, result)

    def run(self, stop, limit=100000):
        for _ in range(limit):
            if self.pc == stop:
                return self
            self.step()
        raise TimeoutError("DCPU execution exceeded instruction limit")


def invoke(assembly, function, args=(), limit=100000):
    if len(args) > 3:
        raise ValueError("test invocation accepts at most three register arguments")
    startup = "SET SP, 0x7f00\n"
    startup += "".join(f"SET {reg}, {value & 65535}\n" for reg, value in zip(REGS, args))
    startup += f"JSR {function}\n__test_halt:\nSET PC, __test_halt\n"
    image, labels = assemble(startup + assembly)
    return CPU(image).run(labels["__test_halt"], limit)


def main():
    import argparse
    import json
    from pathlib import Path
    import struct

    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    asm = commands.add_parser("assemble", help="assemble LLVM/GAS-style DCPU text")
    asm.add_argument("input", type=Path)
    asm.add_argument("-o", "--output", type=Path, required=True)
    asm.add_argument("--endian", choices=("big", "little"), default="big",
                     help="binary word serialization; memory octets remain little endian")
    asm.add_argument("--abi", choices=("raw", "octet16"), help="require this ABI (defaults to declared ABI or octet16)")
    asm.add_argument("-c", "--relocatable", action="store_true", help="emit a versioned JSON object with relocations")
    link = commands.add_parser("link", help="link relocatable objects into a flat binary")
    link.add_argument("inputs", nargs="+", type=Path)
    link.add_argument("-o", "--output", required=True, type=Path)
    link.add_argument("--map", dest="map_file", type=Path, help="write final hardware-word symbol addresses")
    link.add_argument("--endian", choices=("big", "little"), default="big")
    run = commands.add_parser("run", help="execute a binary without peripherals")
    run.add_argument("input", type=Path)
    run.add_argument("--endian", choices=("big", "little"), default="big")
    run.add_argument("--stop", type=lambda x: int(x, 0), required=True)
    run.add_argument("--entry", type=lambda x: int(x, 0), default=0)
    run.add_argument("--limit", type=int, default=100000)
    args = parser.parse_args()
    from dcpu16_object import assemble_object, link_objects
    try:
        order = ">" if args.endian == "big" else "<"
        if args.command == "assemble":
            obj = assemble_object(args.input.read_text(), abi=args.abi)
            if args.relocatable:
                args.output.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")
            else:
                words, _ = link_objects([obj], [str(args.input)])
                args.output.write_bytes(struct.pack(order + "H" * len(words), *words))
        elif args.command == "link":
            objects = []
            for path in args.inputs:
                try:
                    objects.append(json.loads(path.read_text()))
                except ValueError as error:
                    raise ValueError(f"{path}: {error}") from error
            words, link_map = link_objects(objects, [str(path) for path in args.inputs])
            args.output.write_bytes(struct.pack(order + "H" * len(words), *words))
            if args.map_file:
                args.map_file.write_text(json.dumps(link_map, indent=2, sort_keys=True) + "\n")
        else:
            data = args.input.read_bytes()
            if len(data) % 2 or len(data) > 131072:
                raise ValueError("binary must contain at most 65536 whole words")
            if not 0 <= args.entry <= 65535 or not 0 <= args.stop <= 65535 or args.limit <= 0:
                raise ValueError("entry/stop must be 16-bit addresses and limit must be positive")
            cpu = CPU(struct.unpack(order + "H" * (len(data) // 2), data))
            cpu.pc = args.entry
            cpu.run(args.stop, args.limit)
            print(json.dumps(dict(zip(REGS, cpu.reg)), sort_keys=True))
    except (OSError, ValueError, SyntaxError, TimeoutError) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()

"""Versioned relocatable DCPU objects and a static image linker.

Symbols and placement count hardware words. Relocations retain expressions so
octet pointers such as ``symbol << 1`` are evaluated after final placement.
"""
import ast
import re

from dcpu16 import BASIC, REGS, SPECIAL, expression, operand

FORMAT = "dcpu16-object"
TARGET = "dcpu16-v1.7"
ABIS = {"octet16": 32768, "raw": 65536}
SYMBOL = re.compile(r"[A-Za-z_.$][\w.$]*\Z", re.ASCII)
TOKEN = re.compile(r"(?<![\w.$])[A-Za-z_.$][\w.$]*", re.ASCII)


def strip_comment(line):
    quote, escaped = None, False
    for index, char in enumerate(line):
        if escaped:
            escaped = False
        elif char == "\\" and quote:
            escaped = True
        elif quote and char == quote:
            quote = None
        elif not quote and char in ('"', "'"):
            quote = char
        elif char == ";" and not quote:
            return line[:index].strip()
    return line.strip()


def check_expression(text):
    if not isinstance(text, str) or not text or len(text) > 1024:
        raise ValueError("invalid relocation expression")
    # Validate the operators without needing the final symbol values.
    try:
        expression(TOKEN.sub("0", text), {}, truncate=False)
    except (SyntaxError, ValueError) as error:
        raise ValueError(f"invalid relocation expression: {error}") from error


def declared_abi(source):
    abi = None
    for line in source.splitlines():
        match = re.fullmatch(r'\.ident\s+"dcpu16-abi:([^"\\]*)"', strip_comment(line))
        if match:
            value = match[1]
            if value not in ABIS:
                raise ValueError(f"unsupported declared ABI: {value}")
            if abi is not None and abi != value:
                raise ValueError("conflicting assembly ABI declarations")
            abi = value
    return abi


def assemble_object(source, abi=None):
    declared = declared_abi(source)
    if abi is None:
        abi = declared or "octet16"
    if declared is not None and abi != declared:
        raise ValueError(f"assembly ABI mismatch: declared {declared}, requested {abi}")
    if abi not in ABIS:
        raise ValueError(f"unsupported ABI: {abi}")
    words, symbols, relocations = [], {}, []
    globals_, locals_ = set(), set()
    alignment = 1
    pending_byte = False

    def reserve(count):
        if count < 0 or len(words) + count > ABIS[abi]:
            raise ValueError(f"image exceeds {abi} address space")
        words.extend([0] * count)

    def emit_expression(text, bits=16, bit_offset=0):
        check_expression(text)
        symbolic = bool(TOKEN.search(text))
        value = 0 if symbolic else expression(text, {}, truncate=False)
        if not symbolic and not -(1 << (bits - 1)) <= value < (1 << bits):
            raise ValueError(f"value does not fit {bits} bits: {text}")
        for shift in range(0, bits, 16):
            if bits == 8 and bit_offset == 8:
                offset = len(words) - 1
            else:
                offset = len(words)
                reserve(1)
            words[offset] |= ((value >> shift) & ((1 << min(bits, 16)) - 1)) << bit_offset
            if symbolic:
                relocations.append({"offset": offset, "expression": text,
                                    "bits": bits, "shift": shift,
                                    "bit_offset": bit_offset})

    def emit_byte(text):
        nonlocal pending_byte
        emit_expression(text, 8, 8 if pending_byte else 0)
        pending_byte = not pending_byte

    def symbol_list(args):
        names = args.replace(",", " ").split()
        if not names or any(not SYMBOL.fullmatch(name) for name in names):
            raise ValueError("invalid symbol declaration")
        return names

    for number, original in enumerate(source.splitlines(), 1):
        line = strip_comment(original)
        if not line:
            continue
        try:
            if line.endswith(":"):
                name = line[:-1]
                if not SYMBOL.fullmatch(name):
                    raise ValueError(f"invalid label: {name}")
                if name.upper() in REGS or name.upper() in {"SP", "PC", "EX", "PUSH", "POP", "PEEK", "PICK"}:
                    raise ValueError(f"label conflicts with a reserved register/operand name: {name}")
                if pending_byte:
                    raise ValueError("label must be word aligned")
                if name in symbols:
                    raise ValueError(f"duplicate label: {name}")
                symbols[name] = {"offset": len(words), "binding": "local"}
                continue
            parts = line.split(None, 1)
            op, args = parts[0], parts[1] if len(parts) > 1 else ""
            if op in (".globl", ".global"):
                globals_.update(symbol_list(args))
                continue
            if op == ".local":
                locals_.update(symbol_list(args))
                continue
            if op == ".extern":
                symbol_list(args)
                continue
            if op in (".text", ".data", ".bss", ".file", ".type", ".size",
                      ".section", ".ident", ".addrsig"):
                continue
            if op == ".p2align":
                components = args.split(",")
                exponent = int(components[0], 0)
                if not 0 <= exponent <= 17:
                    raise ValueError("alignment exponent must be between 0 and 17")
                if len(components) > 1 and components[1].strip() and int(components[1], 0) != 0:
                    raise ValueError("only zero alignment fill is supported")
                if len(components) > 2:
                    raise ValueError("maximum-skip alignment is not supported")
                if exponent:
                    pending_byte = False
                align = max(1, (1 << exponent) // 2)
                alignment = max(alignment, align)
                reserve(-len(words) % align)
                continue
            if op == ".byte":
                for item in args.split(","):
                    emit_byte(item.strip())
                continue
            if op in (".ascii", ".asciz"):
                value = ast.literal_eval(args)
                if not isinstance(value, str):
                    raise ValueError("expected quoted assembly string")
                data = value.encode("latin-1") + (b"\0" if op == ".asciz" else b"")
                for byte in data:
                    emit_byte(str(byte))
                continue
            if op == ".zero":
                count = int(args, 0)
                if not 0 <= count <= 2 * ABIS[abi]:
                    raise ValueError("invalid byte reservation")
                for _ in range(count):
                    emit_byte("0")
                continue
            if pending_byte:
                raise ValueError("unaligned instruction or word data")
            if op in (".short", ".word", ".long", ".quad"):
                bits = {".short": 16, ".word": 16, ".long": 32, ".quad": 64}[op]
                for item in args.split(","):
                    emit_expression(item.strip(), bits)
                continue
            op = op.upper()
            if op in BASIC:
                b, a = args.split(",", 1)
                ac, aw = operand(a, True)
                bc, bw = operand(b, False)
                if bc == 0x1F:
                    raise ValueError("literal destination")
                reserve(1)
                words[-1] = BASIC[op] | bc << 5 | ac << 10
                for extra in aw + bw:
                    emit_expression(extra)
            elif op in SPECIAL:
                ac, aw = operand(args, True)
                reserve(1)
                words[-1] = SPECIAL[op] << 5 | ac << 10
                for extra in aw:
                    emit_expression(extra)
            else:
                raise ValueError(f"unsupported assembly: {line}")
        except (ValueError, SyntaxError, UnicodeError) as error:
            raise ValueError(f"line {number}: {error}") from error
    for name, record in symbols.items():
        if name in globals_ and name not in locals_:
            record["binding"] = "global"
    return {"format": FORMAT, "version": 1, "target": TARGET, "abi": abi,
            "alignment": alignment, "words": words, "symbols": symbols,
            "relocations": relocations}


def validate_object(obj):
    if not isinstance(obj, dict) or obj.get("format") != FORMAT:
        raise ValueError("not a DCPU relocatable object")
    if type(obj.get("version")) is not int or obj["version"] != 1:
        raise ValueError("unsupported object format version")
    if obj.get("target") != TARGET or not isinstance(obj.get("abi"), str) or obj["abi"] not in ABIS:
        raise ValueError("unsupported object target or ABI")
    alignment = obj.get("alignment")
    if type(alignment) is not int or not 1 <= alignment <= 65536 or alignment & (alignment - 1):
        raise ValueError("invalid object alignment")
    words = obj.get("words")
    if not isinstance(words, list) or len(words) > ABIS[obj["abi"]] or any(
            type(word) is not int or not 0 <= word <= 65535 for word in words):
        raise ValueError("invalid object words")
    symbols = obj.get("symbols")
    if not isinstance(symbols, dict):
        raise ValueError("invalid object symbols")
    for name, symbol in symbols.items():
        if not isinstance(name, str) or not SYMBOL.fullmatch(name) or not isinstance(symbol, dict):
            raise ValueError("invalid symbol record")
        offset = symbol.get("offset")
        if type(offset) is not int or not 0 <= offset <= len(words) or symbol.get("binding") not in ("local", "global"):
            raise ValueError(f"invalid symbol definition: {name}")
    relocations = obj.get("relocations")
    if not isinstance(relocations, list):
        raise ValueError("invalid object relocations")
    occupied = {}
    for reloc in relocations:
        if not isinstance(reloc, dict):
            raise ValueError("invalid relocation record")
        offset, bits, shift, bit_offset = (reloc.get(k) for k in ("offset", "bits", "shift", "bit_offset"))
        if any(type(v) is not int for v in (offset, bits, shift, bit_offset)):
            raise ValueError("invalid relocation fields")
        if not 0 <= offset < len(words) or bits not in (8, 16, 32, 64) or shift not in range(0, bits, 16):
            raise ValueError("relocation is outside its field")
        if bit_offset not in ((0, 8) if bits == 8 else (0,)):
            raise ValueError("invalid relocation bit offset")
        mask = ((1 << min(bits, 16)) - 1) << bit_offset
        if occupied.get(offset, 0) & mask:
            raise ValueError("overlapping relocation fields")
        occupied[offset] = occupied.get(offset, 0) | mask
        check_expression(reloc.get("expression"))


def link_objects(objects, names=None):
    if not objects:
        raise ValueError("no objects to link")
    if names is None:
        names = [f"object {index}" for index in range(len(objects))]
    if len(names) != len(objects):
        raise ValueError("object names do not match inputs")
    image, placed, exports = [], [], {}
    abi = None
    for obj, name in zip(objects, names):
        try:
            validate_object(obj)
            if abi is not None and obj["abi"] != abi:
                raise ValueError(f"ABI mismatch: {obj['abi']} versus {abi}")
            abi = obj["abi"]
            padding = -len(image) % obj["alignment"]
            base = len(image) + padding
            if base + len(obj["words"]) > ABIS[abi]:
                raise ValueError(f"linked image exceeds {abi} address space")
            local = {symbol: base + record["offset"] for symbol, record in obj["symbols"].items()}
            for symbol, record in obj["symbols"].items():
                if record["binding"] == "global":
                    if symbol in exports:
                        raise ValueError(f"duplicate global symbol: {symbol}")
                    exports[symbol] = local[symbol]
            image.extend([0] * padding)
            image.extend(obj["words"])
            placed.append((obj, name, base, local))
        except ValueError as error:
            raise ValueError(f"{name}: {error}") from error
    for obj, name, base, local in placed:
        resolved = exports | local
        for reloc in obj["relocations"]:
            try:
                value = expression(reloc["expression"], resolved, truncate=False)
                bits = reloc["bits"]
                if not -(1 << (bits - 1)) <= value < (1 << bits):
                    raise ValueError(f"relocation does not fit {bits} bits: {reloc['expression']}")
                field_mask = (1 << min(bits, 16)) - 1
                mask = field_mask << reloc["bit_offset"]
                offset = base + reloc["offset"]
                patch = ((value >> reloc["shift"]) & field_mask) << reloc["bit_offset"]
                image[offset] = (image[offset] & ~mask) | patch
            except ValueError as error:
                raise ValueError(f"{name}: relocation at word {reloc['offset']}: {error}") from error
    link_map = {"target": TARGET, "abi": abi, "words": len(image),
                "symbols": exports,
                "objects": [{"name": name, "base": base, "symbols": local}
                            for _, name, base, local in placed]}
    return image, link_map

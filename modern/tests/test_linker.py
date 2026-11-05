"""Link independent objects and execute the relocated binary image."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dcpu16 import CPU, assemble
from dcpu16_object import assemble_object, link_objects
from test_codegen import compile_ir

TOOL = Path(__file__).resolve().parents[1] / "tools/dcpu16.py"


class ObjectLinkerTests(unittest.TestCase):
    def test_rebases_local_labels(self):
        prefix = assemble_object(".short 0, 0, 0, 0")
        body = assemble_object("SET PC, local\nlocal:\nSET A, local")
        image, link_map = link_objects([prefix, body])
        self.assertEqual(image[5], 6)
        self.assertEqual(image[7], 6)
        self.assertEqual(link_map["objects"][1]["symbols"]["local"], 6)

    def test_local_names_do_not_collide(self):
        first = assemble_object(".globl first\nfirst:\nSET A, .Lsame\n.Lsame:\nSET PC, POP")
        second = assemble_object(".globl second\nsecond:\nSET B, .Lsame\n.Lsame:\nSET PC, POP")
        image, link_map = link_objects([first, second])
        self.assertEqual(image[1], 2)
        self.assertEqual(image[4], 5)
        self.assertEqual(link_map["symbols"], {"first": 0, "second": 3})

    def test_word_alignment_and_octet_pointer(self):
        pointer = assemble_object(".short target << 1")
        data = assemble_object(".p2align 3\n.globl target\ntarget:\n.short 0xbeef")
        image, link_map = link_objects([pointer, data])
        self.assertEqual(image, [8, 0, 0, 0, 0xBEEF])
        self.assertEqual(link_map["symbols"]["target"], 4)

    def test_two_external_memory_operands(self):
        startup = assemble_object(".globl halt\nSET SP, 0x7f00\nJSR copy\nhalt:\nSET PC, halt")
        function = assemble_object(".globl copy\ncopy:\nSET [destination], [source]\nSET PC, POP")
        data = assemble_object(".globl source, destination\nsource:\n.short 0xabcd\ndestination:\n.short 0")
        image, link_map = link_objects([startup, function, data])
        cpu = CPU(image).run(link_map["symbols"]["halt"])
        self.assertEqual(cpu.ram[link_map["symbols"]["destination"]], 0xABCD)

    def test_byte_and_wide_data_relocations(self):
        uses = assemble_object(".byte lo, hi\n.long lo << 1\n.quad hi + 0x100000000")
        definitions = assemble_object(".globl lo, hi\nlo:\n.short 123\nhi:\n.short 456")
        image, link_map = link_objects([uses, definitions])
        lo, hi = link_map["symbols"]["lo"], link_map["symbols"]["hi"]
        self.assertEqual(image[:7], [lo | hi << 8, lo << 1, 0, hi, 0, 1, 0])
        self.assertEqual(assemble(".quad 0x1122334455667788")[0], [0x7788, 0x5566, 0x3344, 0x1122])
        self.assertEqual(assemble(".quad 1 << 63")[0], [0, 0, 0, 0x8000])

    def test_string_semicolon_is_data(self):
        self.assertEqual(assemble('.ascii "a;b" ; comment')[0], [0x3B61, 0x62])
        with self.assertRaisesRegex(ValueError, "word aligned"):
            assemble_object('.byte 1\n.p2align 0\nodd:')
        with self.assertRaisesRegex(ValueError, "reserved register"):
            assemble_object('a:\n.short 1')
        words, _ = assemble('SET A, [global$A]\nglobal$A:\n.short 123')
        self.assertEqual(CPU(words).run(2).reg[0], 123)

    def test_undefined_and_duplicate_symbols(self):
        with self.assertRaisesRegex(ValueError, "undefined symbol: absent"):
            link_objects([assemble_object("JSR absent")])
        definition = assemble_object(".globl twice\ntwice:\n.short 1")
        with self.assertRaisesRegex(ValueError, "duplicate global symbol: twice"):
            link_objects([definition, definition])
        private = assemble_object("hidden:\n.short 1")
        with self.assertRaisesRegex(ValueError, "undefined symbol: hidden"):
            link_objects([assemble_object("JSR hidden"), private])

    def test_abi_and_range_errors(self):
        with self.assertRaisesRegex(ValueError, "ABI mismatch"):
            link_objects([assemble_object(".short 0"), assemble_object(".short 0", abi="raw")])
        use = assemble_object(".short target << 16")
        target = assemble_object(".globl target\ntarget:\n.short 0")
        with self.assertRaisesRegex(ValueError, "relocation does not fit 16 bits"):
            link_objects([use, target])
        large = assemble_object("")
        large["words"] = [0] * 32768
        with self.assertRaisesRegex(ValueError, "exceeds octet16"):
            link_objects([large, assemble_object(".short 0")])
        with self.assertRaisesRegex(ValueError, "assembly ABI mismatch"):
            assemble_object('.ident "dcpu16-abi:octet16"\n.short 0', abi="raw")
        with self.assertRaisesRegex(ValueError, "conflicting assembly ABI"):
            assemble_object('.ident "dcpu16-abi:octet16"\n.ident "dcpu16-abi:raw"')

    def test_corrupt_object_diagnostics(self):
        valid = assemble_object(".short elsewhere")
        for field, value, message in [
            ("version", 99, "format version"), ("alignment", 3, "alignment"),
            ("words", [65536], "words"), ("symbols", {"bad": {"offset": -1, "binding": "global"}}, "symbol"),
            ("abi", [], "ABI"),
        ]:
            with self.subTest(field=field):
                malformed = copy.deepcopy(valid)
                malformed[field] = value
                with self.assertRaisesRegex(ValueError, message):
                    link_objects([malformed])
        overlap = copy.deepcopy(valid)
        overlap["relocations"].append(copy.deepcopy(overlap["relocations"][0]))
        with self.assertRaisesRegex(ValueError, "overlapping"):
            link_objects([overlap])
        invalid = copy.deepcopy(valid)
        invalid["relocations"][0]["expression"] = "elsewhere +"
        with self.assertRaisesRegex(ValueError, "invalid relocation expression"):
            link_objects([invalid])

    def test_cli_objects_link_and_execute(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            start, library = tmp / "start.s", tmp / "library.s"
            start.write_text(".globl halt\nSET SP, 0x7f00\nSET A, 41\nJSR increment\nhalt:\nSET PC, halt\n")
            library.write_text(".globl increment\nincrement:\nADD A, 1\nSET PC, POP\n")
            objects = []
            for source in (start, library):
                obj = source.with_suffix(".dco")
                subprocess.run([sys.executable, str(TOOL), "assemble", "-c", str(source), "-o", str(obj)], check=True)
                self.assertEqual(json.loads(obj.read_text())["format"], "dcpu16-object")
                objects.append(str(obj))
            for endian in ("big", "little"):
                binary, mapping = tmp / "image.bin", tmp / "image.map.json"
                subprocess.run([sys.executable, str(TOOL), "link", *objects, "-o", str(binary), "--map", str(mapping), "--endian", endian], check=True)
                stop = json.loads(mapping.read_text())["symbols"]["halt"]
                result = subprocess.run([sys.executable, str(TOOL), "run", str(binary), "--endian", endian, "--stop", str(stop)], check=True, text=True, capture_output=True)
                self.assertEqual(json.loads(result.stdout)["A"], 42)
            broken = tmp / "broken.dco"
            broken.write_text(json.dumps(assemble_object("JSR undefined")))
            binary.write_bytes(b"preserve")
            result = subprocess.run([sys.executable, str(TOOL), "link", str(broken), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("undefined symbol", result.stderr)
            self.assertEqual(binary.read_bytes(), b"preserve")
            broken.write_text('{"broken":')
            result = subprocess.run([sys.executable, str(TOOL), "link", str(broken), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(str(broken), result.stderr)
            self.assertNotIn("Traceback", result.stderr)


class SeparateCompilationTests(unittest.TestCase):
    def test_compiled_calls_globals_and_function_pointers(self):
        caller = compile_ir("""
          @shared = external global i16
          @callback = external global ptr
          declare i16 @increment(i16)
          define i16 @entry() {
            %x = load volatile i16, ptr @shared, align 2
            %y = call i16 @increment(i16 %x)
            %raw = load volatile i16, ptr @callback, align 2
            %f = inttoptr i16 %raw to ptr
            %z = call i16 %f(i16 %y)
            ret i16 %z
          }
        """)
        library = compile_ir("""
          @shared = global i16 40, align 2
          @callback = global ptr @increment, align 2
          define i16 @increment(i16 %n) noinline {
            %next = add i16 %n, 1
            ret i16 %next
          }
        """)
        self.assertIn('.ident "dcpu16-abi:octet16"', caller)
        startup = assemble_object(".globl halt\nSET SP, 0x7f00\nJSR entry\nhalt:\nSET PC, halt\n")
        image, link_map = link_objects([startup, assemble_object(caller), assemble_object(library)])
        cpu = CPU(image).run(link_map["symbols"]["halt"])
        self.assertEqual(cpu.reg[0], 42)
        callback = link_map["symbols"]["callback"]
        self.assertEqual(image[callback], link_map["symbols"]["increment"] << 1)


class InstallationTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("DCPU16_BUILD_DIR"), "run through CTest to check installed tools")
    def test_installed_compiler_assembler_and_linker(self):
        examples = Path(__file__).resolve().parents[1] / "examples"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            prefix = tmp / "install"
            subprocess.run(["cmake", "--install", os.environ["DCPU16_BUILD_DIR"], "--prefix", str(prefix)], check=True, capture_output=True)
            compiler = prefix / "bin" / ("dcpu16-llc" + Path(os.environ.get("DCPU16_LLC", "")).suffix)
            tool = prefix / "share/llvm-dcpu16/dcpu16.py"
            generated = tmp / "add.s"
            compiled = subprocess.run([str(compiler), str(examples / "add.ll"), "-o", str(generated)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            objects = []
            for index, source in enumerate((examples / "start.s", generated)):
                obj = tmp / f"{index}.dco"
                subprocess.run([sys.executable, str(tool), "assemble", "-c", str(source), "-o", str(obj)], check=True, capture_output=True)
                objects.append(str(obj))
            image = tmp / "program.bin"
            subprocess.run([sys.executable, str(tool), "link", *objects, "-o", str(image)], check=True, capture_output=True)
            result = subprocess.run([sys.executable, str(tool), "run", str(image), "--stop", "8"], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(result.stdout)["A"], 42)


if __name__ == "__main__":
    unittest.main()

"""Check frame-load selection and execute its ABI and control-flow boundaries."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes

ROOT = Path(__file__).resolve().parents[1]


def function_bytes(data):
    validate_i386_relocatable_bytes(data)
    offset = struct.unpack_from("<I", data, 32)[0]
    count = struct.unpack_from("<H", data, 48)[0]
    sections = [struct.unpack_from("<10I", data, offset + index * 40)
                for index in range(count)]
    functions = {}
    for section in sections:
        if section[1] != 2:
            continue
        names_section = sections[section[6]]
        names = data[names_section[4]:names_section[4] + names_section[5]]
        for index in range(0, section[5], 16):
            name, value, size, info, _, target = struct.unpack_from(
                "<IIIBBH", data, section[4] + index)
            if not name or info & 15 != 2 or not target:
                continue
            text = sections[target]
            assert value <= text[5] and size <= text[5] - value
            label = names[name:names.index(0, name)].decode("ascii")
            functions[label] = data[text[4] + value:text[4] + value + size]
    return functions


class FrameToolCase(unittest.TestCase):
    fixture_source = '/toolchain/tests/cupidc_frame_load_runtime.cc'
    compiler_flags = ()
    @classmethod
    def setUpClass(cls):
        cls.native_build = None
        supplied = [os.environ.get("CUPIDC_FRAME_LOAD_" + name.upper())
                    for name in ("cupidc", "cupidasm", "cupidld", "cupiddis")]
        if any(supplied):
            if not all(supplied):
                raise AssertionError("provide all four frame-load tools")
            cls.tools = dict(zip(("cupidc", "cupidasm", "cupidld", "cupiddis"),
                                 map(Path, supplied)))
            return
        cls.native_build = tempfile.TemporaryDirectory(
            prefix=".frame-load-native-", dir=ROOT / "toolchain")
        build = Path(cls.native_build.name)
        relative = build.relative_to(ROOT / "toolchain").as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.tools = {name: build / (name + suffix)
                     for name in ("cupidc", "cupidasm", "cupidld", "cupiddis")}
        result = subprocess.run(
            ["make", "-C", str(ROOT / "toolchain"), "BUILD_DIR=" + relative,
             *[relative + "/" + tool.name for tool in cls.tools.values()]],
            capture_output=True, text=True, timeout=180)
        if result.returncode:
            cls.native_build.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        if cls.native_build:
            cls.native_build.cleanup()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(
            prefix=".frame-load-case-", dir=ROOT / "toolchain")
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        self.object = self.output / "fixture.o"
        self.run_tool("cupidc", "--root", ROOT, "-c",
                      *self.compiler_flags, self.fixture_source, "-o",
                      "/" + self.object.relative_to(ROOT).as_posix())
        self.code = function_bytes(self.object.read_bytes())
        self.run_tool("cupiddis", "--require-known", "--require-local-targets",
                      "--require-code-anchors", self.object)

    def run_tool(self, name, *arguments):
        result = subprocess.run([str(self.tools[name]), *map(str, arguments)],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def execute_runtime(self):
        host = "windows" if os.name == "nt" else "linux"
        start = self.output / "start.o"
        self.run_tool("cupidasm", "-f", "elf32",
                      ROOT / ("toolchain/hosted/i386-" + host + "/start.asm"),
                      "-o", start)
        program = self.output / ("fixture.exe" if os.name == "nt" else "fixture.elf")
        arguments = ["-m", "i386pe" if os.name == "nt" else "elf_i386",
                     "--text-address", "0x00401000" if os.name == "nt" else "0x08048000",
                     "--entry", "_start"]
        if os.name == "nt":
            for name in ("ExitProcess", "GetStdHandle", "WriteFile"):
                arguments += ["--import", "__imp_" + name + "=KERNEL32.dll:" + name]
        self.run_tool("cupidld", *arguments, "-o", program, start, self.object)
        if os.name != "nt":
            program.chmod(0o700)
        result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class CupidCFrameLoadTests(FrameToolCase):
    def test_word_parameters_select_one_frame_read(self):
        expected = bytes.fromhex("5589e58b85080000005058c9c3")
        for name in ("signed_parameter", "unsigned_parameter", "volatile_parameter"):
            with self.subTest(name=name):
                self.assertEqual(self.code[name], expected)

    def test_local_and_large_negative_offsets_use_direct_reads(self):
        self.assertIn(bytes.fromhex("8b45fc50"), self.code["local_alias"])
        self.assertIn(bytes.fromhex("8b85fcfeffff50"), self.code["large_frame"])
        self.assertIn(bytes.fromhex("8b45fc50"), self.code["control_flow"])

    def test_narrow_and_wide_values_keep_their_load_protocols(self):
        self.assertIn(bytes.fromhex("580fbe0050"), self.code["narrow_signed"])
        self.assertIn(bytes.fromhex("580fb60050"), self.code["narrow_unsigned"])
        self.assertGreater(len(self.code["wide_parameter"]), 13)
        self.assertIn(bytes.fromhex("588b0050"), self.code["pointer_parameter"])

    def test_aliases_branches_and_full_value_width_execute(self):
        self.execute_runtime()

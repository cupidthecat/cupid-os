"""Real PE32 execution and rejection controls for the private i386 launcher."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from tests.windows_i386_oracle import WindowsI386Oracle


def oracle_elf(code=b"\xb8\x25\0\0\0\xc3", data=b"oracle console\n\0"):
    text = bytes(0x1110)
    offset = 52 + 3 * 32
    header = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + bytes(9),
                         2, 3, 1, 0x01100000, 52, 0, 0, 52, 32, 3, 0, 0, 0)
    segments = (
        (1, offset, 0x01000000, 0x01000000, len(text), len(text), 5, 1),
        (1, offset + len(text), 0x01100000, 0x01100000, len(code), len(code), 5, 1),
        (1, offset + len(text) + len(code), 0x01200000, 0x01200000, len(data), len(data) + 16, 6, 1),
    )
    return header + b"".join(struct.pack("<8I", *row) for row in segments) + text + code + data


@unittest.skipUnless(os.name == "nt", "the native PE32 oracle requires Windows")
class WindowsI386OracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="windows-i386-oracle-")
        cls.addClassCleanup(cls.build.cleanup)
        cls.oracle = WindowsI386Oracle(Path(cls.build.name))

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix="i386 oracle α 猫 ")
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.image = self.root / "oracle image.elf"
        self.image.write_bytes(oracle_elf())

    def launch(self, *arguments, timeout=5):
        return subprocess.run([str(self.oracle.launcher), *map(str, arguments)],
                              capture_output=True, timeout=timeout)

    def test_returns_exact_result_from_native_machine_code(self):
        result = self.launch(self.image, "0x01100000")
        self.assertEqual((result.returncode, result.stdout, result.stderr), (37, b"", b""))

    def test_console_bridge_preserves_exact_bytes_and_unicode_path(self):
        code = (b"\x68\0\0\x20\x01\xb8\0\x11\0\x01\xff\xd0\x83\xc4\x04"
                b"\xb8\x25\0\0\0\xc3")
        self.image.write_bytes(oracle_elf(code))
        result = self.launch(self.image, "0x01100000")
        self.assertEqual((result.returncode, result.stdout, result.stderr), (37, b"", b"oracle console\n"))

    def test_supplies_fresh_stack_and_initialized_x87(self):
        # Read untouched stack bytes, then FNSTCW and require FNINIT's 0x037f.
        code = (b"\x8b\x44\x24\xc0\x0b\x44\x24\xc4\x85\xc0\x75\x14"
                b"\x83\xec\x04\xd9\x3c\x24\x0f\xb7\x04\x24\x83\xc4\x04"
                b"\x2d\x7f\x03\0\0\xc3\xb8\x01\0\0\0\xc3")
        self.image.write_bytes(oracle_elf(code))
        result = self.launch(self.image, "0x01100000")
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, b"", b""))

    def test_rejects_argument_and_file_errors_without_output(self):
        cases = (((), 64), ((self.image, "0x"), 64), ((self.image, "0x100000000"), 64),
                 ((self.image, "0x01100000", "extra"), 64),
                 ((self.root / "missing", "0x01100000"), 65))
        for arguments, status in cases:
            with self.subTest(arguments=arguments):
                result = self.launch(*arguments)
                self.assertEqual((result.returncode, result.stdout, result.stderr), (status, b"", b""))
        self.image.write_bytes(b"short")
        self.assertEqual(self.launch(self.image, "0x01100000").returncode, 66)

    def test_rejects_malformed_elf_headers_and_segments(self):
        original = oracle_elf()
        mutations = (
            (0, "B", 0), (4, "B", 2), (5, "B", 2), (6, "B", 0),
            (16, "H", 3), (18, "H", 62), (20, "I", 0), (28, "I", 0xffffffff),
            (40, "H", 51), (42, "H", 31), (44, "H", 0), (44, "H", 65),
            (52, "I", 2), (52, "I", 3), (56, "I", 0xffffffff),
            (60, "I", 0x100), (60, "I", 0x02000000),
            (68, "I", 0xffffffff), (72, "I", 0xffffffff), (72, "I", 1),
            (84 + 8, "I", 0x01000010),  # overlapping load segments
            (84 + 24, "I", 6),          # entry in non-executable data
            (52 + 16, "I", 0x1104),     # incomplete print trampoline
        )
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, format=fmt, value=value):
                payload = bytearray(original)
                struct.pack_into("<" + fmt, payload, offset, value)
                self.image.write_bytes(payload)
                result = self.launch(self.image, "0x01100000")
                self.assertEqual((result.returncode, result.stdout, result.stderr), (67, b"", b""))
        self.image.write_bytes(original)
        self.assertEqual(self.launch(self.image, "0x01100000").returncode, 37)

    def test_rejects_entries_in_gaps_bss_or_outside_loaded_memory(self):
        for entry in ("0x00000001", "0x01004000", "0x01200010", "0x02000000"):
            with self.subTest(entry=entry):
                result = self.launch(self.image, entry)
                self.assertEqual((result.returncode, result.stdout, result.stderr), (67, b"", b""))

    def test_timeout_terminates_a_nonreturning_native_program(self):
        self.image.write_bytes(oracle_elf(b"\xeb\xfe"))
        with self.assertRaises(subprocess.TimeoutExpired):
            self.launch(self.image, "0x01100000", timeout=0.2)

    def test_assembler_failure_is_reported_without_execution(self):
        (self.root / "runner.S").write_text("this is not assembly\n")
        result = self.oracle.run(self.root, 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error", result.stderr.lower())
        self.assertFalse((self.root / "runner").exists())

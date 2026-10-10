import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class CupidX87IntegerCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".x87-integer-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.assembler_path = build_path / ("cupidasm" + suffix)
        cls.disassembler_path = build_path / ("cupiddis" + suffix)
        relative_prefix = relative_build.as_posix()
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_prefix}",
                f"{relative_prefix}/cupidasm{suffix}",
                f"{relative_prefix}/cupiddis{suffix}",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "Cupid x87 integer CLI build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_signed_integer_memory_forms_round_trip_through_public_clis(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "integer.asm"
            binary = root / "integer.bin"
            source.write_text(
                "BITS 32\n"
                "    fild word [eax]\n"
                "    fild dword [eax]\n"
                "    fild qword [eax]\n"
                "    fistp word [eax]\n"
                "    fistp dword [eax]\n"
                "    fistp qword [eax]\n",
                encoding="utf-8",
            )
            assembled = subprocess.run(
                [
                    str(self.assembler_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(
                binary.read_bytes(),
                bytes.fromhex("df 00 db 00 df 28 df 18 db 18 df 38"),
            )

            disassembled = subprocess.run(
                [
                    str(self.disassembler_path),
                    "--raw",
                    "--mode=32",
                    "--base",
                    "0",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(disassembled.returncode, 0, disassembled.stderr)
            for instruction in (
                "fild word [eax]",
                "fild dword [eax]",
                "fild qword [eax]",
                "fistp word [eax]",
                "fistp dword [eax]",
                "fistp qword [eax]",
            ):
                self.assertIn(instruction, disassembled.stdout)

    def test_integer_forms_reject_register_and_tword_operands(self):
        for name, body in (
            ("register", "    fild eax\n"),
            ("byte", "    fistp byte [eax]\n"),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / f"invalid-{name}.asm"
                binary = root / f"invalid-{name}.bin"
                source.write_text("BITS 32\n" + body, encoding="utf-8")
                assembled = subprocess.run(
                    [
                        str(self.assembler_path),
                        "-f",
                        "bin",
                        str(source),
                        "-o",
                        str(binary),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertNotEqual(assembled.returncode, 0)
                self.assertIn("no x86 form matches", assembled.stderr)
                self.assertFalse(binary.exists())


if __name__ == "__main__":
    unittest.main()

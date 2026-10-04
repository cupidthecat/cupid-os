import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class CupidX87ComparisonCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".x87-compare-build-", dir=TOOLCHAIN_ROOT
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
                f"BUILD_DIR={relative_build.as_posix()}",
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
                "Cupid x87 comparison CLI build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_fucomip_and_stack_pop_round_trip_through_public_clis(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "compare.asm"
            binary = root / "compare.bin"
            source.write_text(
                "BITS 32\n"
                "    fldz\n"
                "    fucomip st0, st1\n"
                "    fstp st0\n",
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
                binary.read_bytes(), bytes.fromhex("d9 ee df e9 dd d8")
            )

            disassembled = subprocess.run(
                [
                    str(self.disassembler_path),
                    "--raw",
                    "--mode=32",
                    "--base",
                    "0x1000",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(disassembled.returncode, 0, disassembled.stderr)
            self.assertIn("fldz", disassembled.stdout)
            self.assertIn("fucomip st0, st1", disassembled.stdout)
            self.assertIn("fstp st0", disassembled.stdout)

    def test_fucomip_rejects_a_non_st0_first_operand(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "invalid-compare.asm"
            binary = root / "invalid-compare.bin"
            source.write_text(
                "BITS 32\n"
                "    fucomip st1, st0\n",
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
            self.assertNotEqual(assembled.returncode, 0)
            self.assertIn("no x86 form matches", assembled.stderr)
            self.assertFalse(binary.exists())

    def test_fldz_rejects_an_operand_without_publishing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "invalid-zero.asm"
            binary = root / "invalid-zero.bin"
            source.write_text("BITS 32\n    fldz st0\n", encoding="utf-8")
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

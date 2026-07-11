import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"
sys.path.insert(0, str(REPO_ROOT / "tools"))

from hostbuild import _symbols_from_nm
import bootstrap_baseline


def configured_symbol_reader_command():
    configured = bootstrap_baseline.optional_oracle_commands()[
        "symbol_reader"
    ]
    return bootstrap_baseline.resolve_tool_command(configured)


class CupidDisOracleConfigurationTests(unittest.TestCase):
    def test_configured_symbol_reader_arguments_are_preserved(self):
        with mock.patch.dict(
            os.environ,
            {"NM": f'"{sys.executable}" --symbol-oracle-mode'},
        ):
            command = configured_symbol_reader_command()

        self.assertEqual(
            command,
            (str(Path(sys.executable).resolve()), "--symbol-oracle-mode"),
        )


class CupidDisContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupiddis-build-", dir=TOOLCHAIN_ROOT
        )
        cls._fixture_directory = tempfile.TemporaryDirectory(
            prefix=".cupiddis-fixture-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupiddis-contract" + suffix)
        cls.elf_contract_path = build_path / ("elf32-contract" + suffix)
        cls.cli_path = build_path / ("cupiddis" + suffix)
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                "all",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._fixture_directory.cleanup()
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidDis hosted build failed\n" + result.stdout + result.stderr
            )
        fixture = subprocess.run(
            [
                str(cls.elf_contract_path),
                "write-oracle",
                cls._fixture_directory.name,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if fixture.returncode != 0:
            cls._fixture_directory.cleanup()
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidDis fixture creation failed\n"
                + fixture.stdout
                + fixture.stderr
            )
        cls.object_path = Path(cls._fixture_directory.name) / "cupid.o"
        cls.raw_path = Path(cls._fixture_directory.name) / "boot.bin"
        cls.raw_path.write_bytes(bytes([0xB8, 0x34, 0x12, 0xC3]))
        cls.not_elf_path = Path(cls._fixture_directory.name) / "not-elf.bin"
        cls.not_elf_path.write_bytes(b"not elf")
        cls.bad_elf_path = Path(cls._fixture_directory.name) / "bad.elf"
        cls.bad_elf_path.write_bytes(b"\x7fELF")
        cls.exec_path = Path(cls._fixture_directory.name) / "program.elf"
        executable = bytearray(90)
        executable[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HHIIIIIHHHHHH", executable, 16, 2, 3, 1,
                         0x00400000, 52, 0, 0, 52, 32, 1, 0, 0, 0)
        struct.pack_into("<IIIIIIII", executable, 52, 1, 84, 0x00400000,
                         0x00400000, 6, 6, 5, 4)
        executable[84:] = bytes([0xB8, 0x78, 0x56, 0x34, 0x12, 0xC3])
        cls.exec_path.write_bytes(executable)
        cls.symbol_reader_command = configured_symbol_reader_command()

    @classmethod
    def tearDownClass(cls):
        cls._fixture_directory.cleanup()
        cls._build_directory.cleanup()

    def run_contract(self, mode):
        result = subprocess.run(
            [str(self.contract_path), mode],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"{mode}: ok\n")

    def test_raw_16_and_32_bit_decode_and_recovery(self):
        self.run_contract("raw")

    def test_relocatable_object_report_and_relocation_overlay(self):
        self.run_contract("object")

    def test_sectionless_executable_uses_executable_load_segment(self):
        self.run_contract("exec")

    def test_nm_order_and_failure_contracts(self):
        self.run_contract("nm")
        self.run_contract("errors")

    def test_cli_default_inspects_all_relocatable_object_views(self):
        result = subprocess.run(
            [str(self.cli_path), str(self.object_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ELF32 REL i386", result.stdout)
        self.assertIn("[sections]", result.stdout)
        self.assertIn("[symbols]", result.stdout)
        self.assertIn("[relocations]", result.stdout)
        self.assertIn("[disassembly .text]", result.stdout)

    def test_cli_inspects_sectionless_executable_load_segment(self):
        result = subprocess.run(
            [
                str(self.cli_path),
                "--headers",
                "--disassemble",
                str(self.exec_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ELF32 EXEC i386", result.stdout)
        self.assertIn("[program headers]", result.stdout)
        self.assertIn("[disassembly LOAD#0]", result.stdout)
        self.assertIn("mov eax, 0x12345678", result.stdout)

    def test_cli_explicit_view_and_nm_modes_are_deterministic(self):
        sections = subprocess.run(
            [str(self.cli_path), "--sections", str(self.object_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(sections.returncode, 0, sections.stderr)
        self.assertIn("[sections]", sections.stdout)
        self.assertNotIn("[symbols]", sections.stdout)
        symbols = subprocess.run(
            [str(self.cli_path), "--nm", str(self.object_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(symbols.returncode, 0, symbols.stderr)
        self.assertIn("00000000 T entry\n", symbols.stdout)
        self.assertNotIn("[symbols]", symbols.stdout)
        addressed_rows = [
            line.split() for line in symbols.stdout.splitlines()
            if len(line.split()) >= 3
        ]
        addresses = [parts[0] for parts in addressed_rows]
        self.assertEqual(addresses, sorted(addresses))
        numeric_sort = subprocess.run(
            [str(self.cli_path), "-n", str(self.object_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(numeric_sort.returncode, 0, numeric_sort.stderr)
        self.assertEqual(numeric_sort.stdout, symbols.stdout)

    def test_cli_is_a_drop_in_numeric_nm_symbol_reader(self):
        oracle = self.symbol_reader_command
        if oracle is None:
            self.skipTest("configured host nm oracle is not installed")
        expected = _symbols_from_nm(oracle, self.object_path)
        actual = _symbols_from_nm(str(self.cli_path), self.object_path)
        self.assertEqual(actual, expected)

    def test_cli_raw_mode_requires_explicit_mode_and_base(self):
        missing = subprocess.run(
            [str(self.cli_path), "--raw", str(self.raw_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(missing.returncode, 2)
        self.assertIn("usage: cupiddis", missing.stderr)
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--base",
                "0x7c00",
                str(self.raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("00007C00", decoded.stdout)
        self.assertIn("mov ax, 0x1234", decoded.stdout)

    def test_cli_distinguishes_usage_and_processing_failures(self):
        not_elf = subprocess.run(
            [str(self.cli_path), str(self.not_elf_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(not_elf.returncode, 1)
        self.assertIn("raw input requires --raw", not_elf.stderr)
        malformed = subprocess.run(
            [str(self.cli_path), str(self.bad_elf_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(malformed.returncode, 1)
        self.assertIn("ELF32 header is truncated", malformed.stderr)

    def test_cli_reports_late_stdout_flush_failures(self):
        full_device = Path("/dev/full")
        if not full_device.exists():
            self.skipTest("/dev/full is not available on this host")
        with full_device.open("wb") as output:
            result = subprocess.run(
                [str(self.cli_path), "--headers", str(self.object_path)],
                cwd=REPO_ROOT,
                stdout=output,
                stderr=subprocess.PIPE,
                text=True,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("CupidDis could not complete report output", result.stderr)

    def test_cli_overlays_relocations_retained_in_executable(self):
        compiler = shutil.which("clang") or shutil.which("gcc")
        linker = shutil.which("ld.lld") or shutil.which("ld")
        if compiler is None or linker is None:
            self.skipTest("assembler/linker oracle tools are not installed")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "retained.s"
            object_path = root / "retained.o"
            executable = root / "retained.elf"
            source.write_text(
                ".text\n"
                ".globl _start\n"
                ".type _start,@function\n"
                "_start:\n"
                "  movl $data_symbol, %eax\n"
                "  call target_symbol\n"
                "  ret\n"
                ".size _start, .-_start\n"
                ".globl target_symbol\n"
                ".type target_symbol,@function\n"
                "target_symbol:\n"
                "  ret\n"
                ".size target_symbol, .-target_symbol\n"
                ".data\n"
                ".globl data_symbol\n"
                ".type data_symbol,@object\n"
                "data_symbol:\n"
                "  .long 1\n"
                ".size data_symbol, .-data_symbol\n",
                encoding="utf-8",
            )
            command = [compiler]
            if "clang" in Path(compiler).name.lower():
                command.append("--target=i386-unknown-elf")
            else:
                command.append("-m32")
            command.extend(["-c", str(source), "-o", str(object_path)])
            assembled = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            linked = subprocess.run(
                [
                    linker,
                    "-m",
                    "elf_i386",
                    "--emit-relocs",
                    "-e",
                    "_start",
                    str(object_path),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            report = subprocess.run(
                [
                    str(self.cli_path),
                    "--relocations",
                    "--disassemble",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("R_386_32 data_symbol", report.stdout)
            self.assertIn("R_386_PC32 target_symbol-4", report.stdout)
            self.assertIn("mov eax, data_symbol\n", report.stdout)
            self.assertIn("call target_symbol-4\n", report.stdout)


if __name__ == "__main__":
    unittest.main()

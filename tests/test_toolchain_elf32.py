import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainElf32ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".elf32-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("elf32-contract" + suffix)
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
            cls._build_directory.cleanup()
            raise AssertionError(
                "toolchain ELF32 contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_writer_emits_deterministic_elf32_relocatable_object(self):
        result = subprocess.run(
            [str(self.contract_path), "writer-basic"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "writer-basic: ok\n")

    def test_writer_serializes_symbols_common_storage_and_relocations(self):
        result = subprocess.run(
            [str(self.contract_path), "writer-model"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "writer-model: ok\n")

    def test_writer_rejects_invalid_models_and_rolls_back_limits(self):
        result = subprocess.run(
            [str(self.contract_path), "writer-errors"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "writer-errors: ok\n")

    def test_reader_exposes_typed_writer_roundtrip(self):
        result = subprocess.run(
            [str(self.contract_path), "reader-roundtrip"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "reader-roundtrip: ok\n")

    def test_reader_exposes_bounded_elf32_executable_view(self):
        result = subprocess.run(
            [str(self.contract_path), "reader-exec"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "reader-exec: ok\n")

    def test_reader_rejects_malformed_elf32_program_headers(self):
        result = subprocess.run(
            [str(self.contract_path), "reader-exec-malformed"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "reader-exec-malformed: ok\n")

    def test_reader_bounds_descending_string_table_offsets(self):
        result = subprocess.run(
            [str(self.contract_path), "reader-string-offsets"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "reader-string-offsets: ok\n")

    def test_reader_rejects_malformed_objects_with_stable_diagnostics(self):
        result = subprocess.run(
            [str(self.contract_path), "reader-malformed"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "reader-malformed: ok\n")

    def test_cupid_object_is_oracle_reader_and_linker_compatible(self):
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        linker = shutil.which("ld.lld") or shutil.which("ld")
        if readelf is None or linker is None:
            self.skipTest("ELF oracle reader/linker is not installed")
        with tempfile.TemporaryDirectory() as first_dir, tempfile.TemporaryDirectory() as second_dir:
            first = subprocess.run(
                [str(self.contract_path), "write-oracle", first_dir],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            second = subprocess.run(
                [str(self.contract_path), "write-oracle", second_dir],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            first_object = Path(first_dir) / "cupid.o"
            second_object = Path(second_dir) / "cupid.o"
            self.assertEqual(first_object.read_bytes(), second_object.read_bytes())

            inspection = subprocess.run(
                [readelf, "-h", "-S", "-s", "-r", "-W", str(first_object)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(inspection.returncode, 0, inspection.stderr)
            self.assertIn("REL (Relocatable file)", inspection.stdout)
            self.assertIn("Intel 80386", inspection.stdout)
            self.assertIn(".rel.text", inspection.stdout)
            self.assertIn(".rel.data", inspection.stdout)
            self.assertIn("R_386_PC32", inspection.stdout)
            self.assertIn("R_386_32", inspection.stdout)
            self.assertIn("WEAK", inspection.stdout)
            self.assertIn("common_data", inspection.stdout)

            linked_object = Path(first_dir) / "linked.o"
            linked = subprocess.run(
                [linker, "-m", "elf_i386", "-r", str(first_object), "-o", str(linked_object)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            parsed = subprocess.run(
                [str(self.contract_path), "inspect", first_dir, "/linked.o"],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(parsed.returncode, 0, parsed.stderr)
            self.assertRegex(
                parsed.stdout,
                r"^inspect: sections=\d+ symbols=\d+ relocations=\d+\n$",
            )

    def test_reader_matches_host_compiler_weak_common_oracle(self):
        compiler = shutil.which("clang") or shutil.which("gcc")
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if compiler is None or readelf is None:
            self.skipTest("C/ELF oracle tools are not installed")
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "oracle.o"
            command = [compiler]
            if "clang" in Path(compiler).name.lower():
                command.append("--target=i386-unknown-elf")
            else:
                command.append("-m32")
            command.extend(
                [
                    "-std=c11",
                    "-O0",
                    "-ffreestanding",
                    "-fno-pie",
                    "-fno-pic",
                    "-fcommon",
                    "-fno-asynchronous-unwind-tables",
                    "-fno-unwind-tables",
                    "-c",
                    str(TOOLCHAIN_ROOT / "tests" / "elf32_oracle.c"),
                    "-o",
                    str(output),
                ]
            )
            compiled = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            parsed = subprocess.run(
                [str(self.contract_path), "reader-c-oracle", td],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(parsed.returncode, 0, parsed.stderr)
            self.assertEqual(parsed.stdout, "reader-c-oracle: ok\n")
            inspection = subprocess.run(
                [readelf, "-s", "-r", "-W", str(output)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(inspection.returncode, 0, inspection.stderr)
            self.assertIn("common_value", inspection.stdout)
            self.assertIn("external_value", inspection.stdout)
            self.assertIn("weak_function", inspection.stdout)
            self.assertIn("R_386_32", inspection.stdout)
            self.assertIn("R_386_PC32", inspection.stdout)

    def test_reader_matches_real_nasm_isr_oracle(self):
        nasm = shutil.which("nasm")
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if nasm is None or readelf is None:
            self.skipTest("NASM/ELF oracle tools are not installed")
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "isr.o"
            assembled = subprocess.run(
                [nasm, "-f", "elf32", str(REPO_ROOT / "kernel" / "cpu" / "isr.asm"), "-o", str(output)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            parsed = subprocess.run(
                [str(self.contract_path), "reader-isr-oracle", td],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(parsed.returncode, 0, parsed.stderr)
            self.assertEqual(parsed.stdout, "reader-isr-oracle: ok\n")
            inspection = subprocess.run(
                [readelf, "-r", "-W", str(output)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(inspection.returncode, 0, inspection.stderr)
            self.assertEqual(inspection.stdout.count("R_386_PC32"), 11)

    def test_reader_accepts_linked_tdata_and_tbss_symbols(self):
        compiler = shutil.which("clang") or shutil.which("gcc")
        linker = shutil.which("ld.lld") or shutil.which("ld")
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if compiler is None or linker is None or readelf is None:
            self.skipTest("C/linker/ELF oracle tools are not installed")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "tls.c"
            object_path = root / "tls.o"
            executable = root / "tls.elf"
            source.write_text(
                "__thread int tls_init = 1;\n"
                "__thread int tls_zero;\n"
                "void _start(void) {}\n",
                encoding="utf-8",
            )
            command = [compiler]
            if "clang" in Path(compiler).name.lower():
                command.append("--target=i386-unknown-elf")
            else:
                command.append("-m32")
            command.extend(
                [
                    "-std=c11",
                    "-O0",
                    "-ffreestanding",
                    "-fno-pie",
                    "-fno-pic",
                    "-fno-asynchronous-unwind-tables",
                    "-fno-unwind-tables",
                    "-c",
                    str(source),
                    "-o",
                    str(object_path),
                ]
            )
            compiled = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
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
            oracle = subprocess.run(
                [readelf, "-l", "-S", "-s", "-W", str(executable)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            self.assertIn(" TLS ", oracle.stdout)
            self.assertIn("tls_init", oracle.stdout)
            self.assertIn("tls_zero", oracle.stdout)
            parsed = subprocess.run(
                [str(self.contract_path), "inspect", td, "/tls.elf"],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(parsed.returncode, 0, parsed.stderr)
            report = subprocess.run(
                [str(self.cli_path), "--headers", "--symbols", str(executable)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("] TLS off=", report.stdout)
            self.assertIn(" TLS DEFAULT ", report.stdout)


if __name__ == "__main__":
    unittest.main()

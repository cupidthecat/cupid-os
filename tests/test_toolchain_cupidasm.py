import os
import re
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


def _elf_string(table, offset):
    end = table.find(b"\0", offset)
    if offset < 0 or offset >= len(table) or end < 0:
        raise AssertionError("ELF string is outside its string table")
    return table[offset:end].decode("ascii")


def _elf_sections(image):
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", image, 0)
    if header[0][:7] != b"\x7fELF\x01\x01\x01":
        raise AssertionError("output is not little-endian ELF32")
    section_offset = header[6]
    section_size = header[11]
    section_count = header[12]
    string_index = header[13]
    if section_size != 40 or string_index >= section_count:
        raise AssertionError("ELF32 section table is malformed")
    rows = [
        struct.unpack_from("<IIIIIIIIII", image, section_offset + index * 40)
        for index in range(section_count)
    ]
    string_row = rows[string_index]
    strings = image[string_row[4] : string_row[4] + string_row[5]]
    sections = []
    for index, row in enumerate(rows):
        name = "" if index == 0 else _elf_string(strings, row[0])
        payload = b"" if row[1] == 8 else image[row[4] : row[4] + row[5]]
        sections.append({"index": index, "name": name, "row": row, "data": payload})
    return header, sections


class CupidAsmCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidasm-cli-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli_path = build_path / ("cupidasm" + suffix)
        cli_target = relative_build.as_posix() + "/cupidasm" + suffix
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build.as_posix()}",
                cli_target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidASM hosted CLI build failed\n" + result.stdout + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_cli_assembles_nasm_style_raw_command_to_exact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "simple.asm"
            output = root / "simple.bin"
            source.write_text(
                "BITS 16\n"
                "ORG 0x7c00\n"
                "start:\n"
                "    mov ax, 0x1234\n"
                "    ret\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), bytes.fromhex("b8 34 12 c3"))

    def test_cli_assembles_nasm_style_elf32_command_with_symbols_and_relocation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "simple.asm"
            output = root / "simple.o"
            source.write_text(
                "BITS 32\n"
                "extern target\n"
                "global entry\n"
                "section .text\n"
                "entry:\n"
                "    call target\n"
                "    ret\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "elf32",
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

            image = output.read_bytes()
            header, sections = _elf_sections(image)
            self.assertEqual(header[1:4], (1, 3, 1))
            by_name = {section["name"]: section for section in sections}
            text = by_name[".text"]
            self.assertEqual(text["row"][1], 1)
            self.assertEqual(text["row"][2], 0x6)
            self.assertEqual(text["row"][8], 16)
            self.assertEqual(text["data"], bytes.fromhex("e8 fc ff ff ff c3"))

            symtab = by_name[".symtab"]
            strtab = sections[symtab["row"][6]]
            symbols = []
            for offset in range(0, len(symtab["data"]), 16):
                row = struct.unpack_from("<IIIBBH", symtab["data"], offset)
                name = "" if row[0] == 0 else _elf_string(strtab["data"], row[0])
                symbols.append({"name": name, "row": row})
            named_symbols = {symbol["name"]: symbol for symbol in symbols if symbol["name"]}
            entry = named_symbols["entry"]["row"]
            target = named_symbols["target"]["row"]
            self.assertEqual((entry[1], entry[3] >> 4, entry[5]), (0, 1, text["index"]))
            self.assertEqual((target[3] >> 4, target[5]), (1, 0))

            relocations = by_name[".rel.text"]
            self.assertEqual(relocations["row"][7], text["index"])
            self.assertEqual(len(relocations["data"]), 8)
            relocation_offset, relocation_info = struct.unpack(
                "<II", relocations["data"]
            )
            self.assertEqual((relocation_offset, relocation_info & 0xFF), (1, 2))
            self.assertEqual(symbols[relocation_info >> 8]["name"], "target")
            self.assertEqual(struct.unpack_from("<i", text["data"], 1)[0], -4)

    def test_cli_absolute_input_keeps_working_directory_include_root(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidasm-absolute-", dir=REPO_ROOT
        ) as directory:
            root = Path(directory)
            source = root / "include-root.asm"
            output = root / "include-root.o"
            source.write_text(
                '%include "demos/include_helper.asm"\n'
                "extern print\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "elf32",
                    str(source.resolve()),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertGreater(output.stat().st_size, 32)

    def test_cli_reports_usage_errors_with_exit_two(self):
        result = subprocess.run(
            [str(self.cli_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage: cupidasm", result.stderr)

    def test_cli_reports_source_errors_with_structured_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "invalid.asm"
            output = root / "invalid.bin"
            source.write_text(
                "BITS 16\nthis_is_not_an_instruction ax\n", encoding="utf-8"
            )
            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertRegex(
                result.stderr,
                re.compile(r"invalid\.asm:\d+:\d+: error CT[0-9A-Fa-f]+:"),
            )


if __name__ == "__main__":
    unittest.main()

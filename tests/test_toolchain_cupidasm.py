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

    def test_cli_rejects_duplicate_raw_origin_without_publishing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "duplicate-org.asm"
            output = root / "duplicate-org.bin"
            source.write_text(
                "BITS 16\n"
                "ORG 0x7c00\n"
                "    db 0x11\n"
                "ORG 0x8000\n"
                "    db 0x22\n",
                encoding="utf-8",
            )
            output.write_bytes(b"prior output")

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
                re.compile(
                    r"duplicate-org\.asm:4:1: error CT6000010: "
                    r"raw output accepts only one ORG directive"
                ),
            )
            self.assertEqual(output.read_bytes(), b"prior output")

    def test_cli_allows_absolute_equ_before_raw_section_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "equ-preamble.asm"
            output = root / "equ-preamble.bin"
            source.write_text(
                "BITS 32\n"
                "VALUE equ 1\n"
                "section .data\n"
                "    db VALUE\n",
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
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            self.assertEqual(output.read_bytes(), b"\x01")

    def test_cli_rejects_raw_multi_section_layout_without_publishing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "multi-section.asm"
            output = root / "multi-section.bin"
            source.write_text(
                "BITS 32\n"
                "section .text\n"
                "start: ret\n"
                "section .data\n"
                "    db 0x2a\n",
                encoding="utf-8",
            )
            output.write_bytes(b"prior output")

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
                re.compile(
                    r"multi-section\.asm:4:1: error CT6000011: "
                    r"raw output supports only one source section"
                ),
            )
            self.assertEqual(output.read_bytes(), b"prior output")

    def test_cli_publishes_coalesced_raw_layout_ranges(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "mixed.asm"
            output = root / "mixed.bin"
            range_map = root / "mixed.cupidmap"
            repeated_map = root / "mixed-repeat.cupidmap"
            source.write_text(
                "BITS 16\n"
                "ORG 0x7c00\n"
                "    mov ax, 0x1234\n"
                "    db 0, 0, 0, 0\n"
                "BITS 32\n"
                "    mov eax, 0x12345678\n"
                "    align 16, 0\n"
                "BITS 16\n"
                "    ret\n",
                encoding="utf-8",
            )
            command = [
                str(self.cli_path),
                "-f",
                "bin",
                "--map",
                str(range_map),
                str(source),
                "-o",
                str(output),
            ]
            result = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            first_map = range_map.read_bytes() if range_map.exists() else b""
            repeated = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(repeated_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(repeated.returncode, 0, repeated.stderr)
            self.assertEqual(
                output.read_bytes(),
                bytes.fromhex(
                    "b8 34 12 00 00 00 00 b8 78 56 34 12 "
                    "00 00 00 00 c3"
                ),
            )
            self.assertEqual(
                first_map,
                b"cupid.raw-map.v2\n"
                b"size 17\n"
                b"base 0x00007c00\n"
                b"edges 0\n"
                b"range 0x00000000 code16\n"
                b"range 0x00000003 data\n"
                b"range 0x00000007 code32\n"
                b"range 0x0000000c data\n"
                b"range 0x00000010 code16\n",
            )
            self.assertEqual(repeated_map.read_bytes(), first_map)

    def test_cli_limits_raw_maps_to_distinct_raw_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "simple.asm"
            output = root / "simple.o"
            range_map = root / "simple.cupidmap"
            source.write_text("BITS 32\nmain: ret\n", encoding="utf-8")

            object_map = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "elf32",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            collision = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(output),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(object_map.returncode, 2)
            self.assertIn("usage: cupidasm", object_map.stderr)
            self.assertEqual(collision.returncode, 2)
            self.assertIn("usage: cupidasm", collision.stderr)
            self.assertFalse(output.exists())
            self.assertFalse(range_map.exists())

    def test_cli_publishes_source_resolved_raw_control_edges(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "edges.asm"
            output = root / "edges.bin"
            range_map = root / "edges.cupidmap"
            source.write_text(
                "BITS 16\n"
                "ORG 0x8000\n"
                "start:\n"
                "    jmp next\n"
                "next:\n"
                "    jmp dword 0x08:pm32\n"
                "BITS 32\n"
                "pm32:\n"
                "    call eax\n"
                "    jmp 0x08:0x00100000\n",
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                range_map.read_text(encoding="ascii").splitlines(),
                [
                    "cupid.raw-map.v2",
                    "size 19",
                    "base 0x00008000",
                    "edges 4",
                    "range 0x00000000 code16",
                    "range 0x0000000a code32",
                    "edge 0x00000000 relative local 0x00000002 "
                    "0x00008002 16 0x00000000",
                    "edge 0x00000002 far local 0x0000000a "
                    "0x0000800a 32 0x00000008",
                    "edge 0x0000000a indirect unprovable - - unknown -",
                    "edge 0x0000000c far external - 0x00100000 "
                    "32 0x00000008",
                ],
            )

    def test_cli_assembles_padding_nops_in_both_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "padding.asm"
            output = root / "padding.bin"
            source.write_text(
                "BITS 32\n"
                "    nop\n"
                "    nop eax\n"
                "    nop [eax]\n"
                "    nop word [eax]\n"
                "BITS 16\n"
                "    nop ax\n"
                "    nop dword [bx + si]\n",
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
            self.assertEqual(
                output.read_bytes(),
                bytes.fromhex(
                    "90 0f 1f c0 0f 1f 00 66 0f 1f 00 "
                    "0f 1f c0 66 0f 1f 00"
                ),
            )

    def test_cli_assembles_double_precision_right_shifts_in_both_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "shrd.asm"
            output = root / "shrd.bin"
            source.write_text(
                "BITS 32\n"
                "    shrd eax, edi, cl\n"
                "    shrd dword [ebx + 4], esi, 7\n"
                "BITS 16\n"
                "    shrd ax, di, cl\n"
                "    shrd dword [bx + si], esi, 0x1f\n",
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
            self.assertEqual(
                output.read_bytes(),
                bytes.fromhex(
                    "0f ad f8 0f ac 73 04 07 "
                    "0f ad f8 66 0f ac 30 1f"
                ),
            )

    def test_cli_rejects_invalid_double_precision_right_shift_operands(self):
        cases = {
            "width-mismatch": "shrd eax, di, cl",
            "memory-source": "shrd eax, dword [edi], cl",
            "wrong-count-register": "shrd eax, edi, dl",
            "lock-prefix": "lock shrd eax, edi, cl",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, instruction in cases.items():
                with self.subTest(name=name):
                    source = root / f"{name}.asm"
                    output = root / f"{name}.bin"
                    source.write_text(
                        f"BITS 32\n    {instruction}\n", encoding="utf-8"
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
                    self.assertIn(
                        "instruction has no supported x86 encoding",
                        result.stderr,
                    )
                    self.assertFalse(output.exists())

    def test_cli_assembles_parity_setcc_in_both_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "parity-setcc.asm"
            output = root / "parity-setcc.bin"
            source.write_text(
                "BITS 16\n"
                "    setp dl\n"
                "    a32 setnp byte [ebx + ecx * 4 + 0x12345678]\n"
                "BITS 32\n"
                "    setnp dl\n"
                "    a16 setp byte [bx + si + 0x7f]\n",
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
            self.assertEqual(
                output.read_bytes(),
                bytes.fromhex(
                    "0f 9a c2 67 0f 9b 84 8b 78 56 34 12 "
                    "0f 9b c2 67 0f 9a 40 7f"
                ),
            )

    def test_cli_rejects_invalid_parity_setcc_operands(self):
        cases = {
            "non-byte-register": (
                "setp eax",
                "instruction has no supported x86 encoding",
            ),
            "immediate": (
                "setnp 1",
                "instruction has no supported x86 encoding",
            ),
            "lock-prefix": (
                "lock setp byte [eax]",
                "instruction has no supported x86 encoding",
            ),
            "setpe-alias": (
                "setpe dl",
                "unknown Cupid ASM instruction mnemonic",
            ),
            "setpo-alias": (
                "setpo dl",
                "unknown Cupid ASM instruction mnemonic",
            ),
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, (instruction, diagnostic) in cases.items():
                with self.subTest(name=name):
                    source = root / f"{name}.asm"
                    output = root / f"{name}.bin"
                    source.write_text(
                        f"BITS 32\n    {instruction}\n", encoding="utf-8"
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
                    self.assertIn(diagnostic, result.stderr)
                    self.assertFalse(output.exists())

    def test_cli_aligns_raw_addresses_and_elf32_sections(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw_source = root / "aligned-raw.asm"
            raw_output = root / "aligned-raw.bin"
            raw_source.write_text(
                "BITS 32\n"
                "ORG 0x101\n"
                "    db 0x11\n"
                "    align 4, 0xa5\n"
                "aligned_raw:\n"
                "    dw aligned_raw\n"
                "    align 8\n"
                "    db 0x22\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    str(raw_source),
                    "-o",
                    str(raw_output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                raw_output.read_bytes(), bytes.fromhex("11 a5 a5 04 01 00 00 22")
            )

            object_source = root / "aligned-object.asm"
            object_output = root / "aligned-object.o"
            object_source.write_text(
                "BITS 32\n"
                "section .data\n"
                "    db 1\n"
                "    align 16, 0x5a\n"
                "aligned_data:\n"
                "    dd aligned_data\n"
                "section .bss\n"
                "    resb 3\n"
                "    align 32\n"
                "aligned_bss:\n"
                "    resb 5\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "elf32",
                    str(object_source),
                    "-o",
                    str(object_output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

            _, sections = _elf_sections(object_output.read_bytes())
            by_name = {section["name"]: section for section in sections}
            data = by_name[".data"]
            bss = by_name[".bss"]
            self.assertEqual(data["row"][8], 16)
            self.assertEqual(
                data["data"], b"\x01" + b"\x5a" * 15 + b"\x00" * 4
            )
            self.assertEqual(
                (bss["row"][1], bss["row"][5], bss["row"][8]),
                (8, 37, 32),
            )

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

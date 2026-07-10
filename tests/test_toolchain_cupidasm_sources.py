import hashlib
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import bootstrap_baseline


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


RAW_FIXTURES = (
    {
        "name": "boot",
        "source": REPO_ROOT / "boot" / "boot.asm",
        "size": 2560,
        "sha256": "57884f86c907d8669f16a667e83238b5f840f2b67e7b82eeeedea09ec5244445",
    },
    {
        "name": "smp-trampoline",
        "source": REPO_ROOT / "kernel" / "smp" / "smp_trampoline.S",
        "size": 4096,
        "sha256": "b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90",
    },
)


OBJECT_FIXTURES = (
    {
        "name": "isr",
        "source": REPO_ROOT / "kernel" / "cpu" / "isr.asm",
        "text_size": 417,
        "text_sha256": "bcf582569c26029d5143ec42f6de24388596c412bca2b4a672608800fe2606e3",
        "symbol_count": 41,
        "binding_counts": {0: 2, 1: 39},
        "placement_counts": {".text": 33, "UND": 8},
        "relocations": (
            (".text", 0x12, 2, -4, "percpu_interrupt_enter"),
            (".text", 0x18, 2, -4, "isr_handler"),
            (".text", 0x20, 2, -4, "percpu_interrupt_leave"),
            (".text", 0x25, 2, -4, "process_reschedule_if_pending"),
            (".text", 0x47, 2, -4, "percpu_interrupt_enter"),
            (".text", 0x4D, 2, -4, "irq_handler"),
            (".text", 0x55, 2, -4, "percpu_interrupt_leave"),
            (".text", 0x5A, 2, -4, "process_reschedule_if_pending"),
            (".text", 0x170, 2, -4, "fpu_nm_handler"),
            (".text", 0x180, 2, -4, "fpu_mf_handler"),
            (".text", 0x190, 2, -4, "fpu_xf_handler"),
        ),
    },
    {
        "name": "context-switch",
        "source": REPO_ROOT / "kernel" / "core" / "context_switch.asm",
        "text_size": 73,
        "text_sha256": "25b78f4c2cbf3dfadc6dc87a9731a097bfd9df0675534d8449c24d890114fbfa",
        "symbol_count": 6,
        "binding_counts": {0: 3, 1: 3},
        "placement_counts": {".text": 5, "UND": 1},
        "relocations": (
            (".text", 0x21, 2, -4, "bkl_context_switch_release"),
        ),
    },
)


def _string_at(table, offset):
    if offset < 0 or offset >= len(table):
        raise AssertionError("ELF string offset is outside its string table")
    end = table.find(b"\0", offset)
    if end < 0:
        raise AssertionError("ELF string is not terminated")
    return table[offset:end].decode("utf-8")


def _count(values):
    counts = {}
    for value in values:
        counts[value] = counts.get(value, 0) + 1
    return counts


def _parse_elf32_semantics(image):
    if len(image) < 52:
        raise AssertionError("CupidASM output is shorter than an ELF32 header")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", image, 0)
    if header[0][:7] != b"\x7fELF\x01\x01\x01":
        raise AssertionError("CupidASM output is not little-endian ELF32")
    if header[1:4] != (1, 3, 1):
        raise AssertionError("CupidASM output is not an i386 ET_REL object")

    section_offset = header[6]
    section_entry_size = header[11]
    section_count = header[12]
    section_names_index = header[13]
    if section_entry_size != 40 or section_names_index >= section_count:
        raise AssertionError("ELF32 section table metadata is malformed")
    if section_offset + section_count * section_entry_size > len(image):
        raise AssertionError("ELF32 section table extends past the image")

    rows = [
        struct.unpack_from(
            "<IIIIIIIIII", image, section_offset + index * section_entry_size
        )
        for index in range(section_count)
    ]
    names_row = rows[section_names_index]
    names_end = names_row[4] + names_row[5]
    if names_end > len(image):
        raise AssertionError("ELF32 section-name table extends past the image")
    section_names = image[names_row[4] : names_end]

    sections = []
    for index, row in enumerate(rows):
        name = "" if index == 0 else _string_at(section_names, row[0])
        data = b""
        if row[1] != 8:
            data_end = row[4] + row[5]
            if data_end > len(image):
                raise AssertionError(f"ELF32 section {name!r} extends past the image")
            data = image[row[4] : data_end]
        sections.append(
            {
                "index": index,
                "name": name,
                "type": row[1],
                "flags": row[2],
                "link": row[6],
                "info": row[7],
                "alignment": row[8],
                "entry_size": row[9],
                "data": data,
            }
        )
    sections_by_name = {section["name"]: section for section in sections}

    symbol_tables = {}
    symbols_by_name = {}
    for section in sections:
        if section["type"] != 2:
            continue
        if section["link"] >= len(sections) or section["entry_size"] != 16:
            raise AssertionError("ELF32 symbol table metadata is malformed")
        strings = sections[section["link"]]["data"]
        if len(section["data"]) % 16 != 0:
            raise AssertionError("ELF32 symbol table has a partial entry")
        symbols = []
        for offset in range(0, len(section["data"]), 16):
            name_offset, value, _size, info, _other, section_index = (
                struct.unpack_from("<IIIBBH", section["data"], offset)
            )
            name = "" if name_offset == 0 else _string_at(strings, name_offset)
            if section_index == 0:
                placement = "UND"
            elif section_index == 0xFFF1:
                placement = "ABS"
            elif section_index == 0xFFF2:
                placement = "COMMON"
            elif section_index < len(sections):
                placement = sections[section_index]["name"]
            else:
                placement = f"SHN_{section_index:04x}"
            symbol = {
                "name": name,
                "value": value,
                "binding": info >> 4,
                "type": info & 0xF,
                "placement": placement,
            }
            symbols.append(symbol)
            if name and symbol["type"] not in (3, 4):
                if name in symbols_by_name:
                    raise AssertionError(f"duplicate named ELF symbol {name!r}")
                symbols_by_name[name] = {
                    "binding": symbol["binding"],
                    "placement": placement,
                    "value": value,
                }
        symbol_tables[section["index"]] = symbols

    relocations = []
    for section in sections:
        if section["type"] != 9:
            continue
        if section["link"] not in symbol_tables or section["info"] >= len(sections):
            raise AssertionError("ELF32 relocation table metadata is malformed")
        if section["entry_size"] != 8 or len(section["data"]) % 8 != 0:
            raise AssertionError("ELF32 relocation table has a partial entry")
        target = sections[section["info"]]
        symbols = symbol_tables[section["link"]]
        for offset in range(0, len(section["data"]), 8):
            target_offset, info = struct.unpack_from("<II", section["data"], offset)
            symbol_index = info >> 8
            relocation_type = info & 0xFF
            if symbol_index >= len(symbols) or target_offset + 4 > len(target["data"]):
                raise AssertionError("ELF32 relocation references invalid object data")
            addend = struct.unpack_from("<i", target["data"], target_offset)[0]
            relocations.append(
                (
                    target["name"],
                    target_offset,
                    relocation_type,
                    addend,
                    symbols[symbol_index]["name"],
                )
            )

    return {
        "sections": sections_by_name,
        "symbols": symbols_by_name,
        "relocations": tuple(sorted(relocations)),
    }


class CupidAsmActiveSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidasm-active-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli_path = build_path / ("cupidasm" + suffix)
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                f"{relative_build}/cupidasm{suffix}",
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
        configured_nasm = bootstrap_baseline.optional_oracle_commands()["nasm"]
        cls.nasm_command = bootstrap_baseline.resolve_tool_command(configured_nasm)

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def _assemble(self, assembler, source, output, output_format):
        result = subprocess.run(
            [*assembler, "-f", output_format, str(source), "-o", str(output)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"{Path(assembler[0]).name} failed to assemble {source.relative_to(REPO_ROOT)}\n"
            + result.stdout
            + result.stderr,
        )
        self.assertTrue(output.is_file(), f"assembler did not create {output.name}")
        return output.read_bytes()

    def test_active_raw_sources_match_oracle_artifacts(self):
        with tempfile.TemporaryDirectory(prefix="cupidasm-active-raw-") as directory:
            root = Path(directory)
            for fixture in RAW_FIXTURES:
                with self.subTest(source=fixture["source"].relative_to(REPO_ROOT)):
                    cupid = self._assemble(
                        (str(self.cli_path),),
                        fixture["source"],
                        root / f"{fixture['name']}.cupid.bin",
                        "bin",
                    )
                    self.assertEqual(len(cupid), fixture["size"])
                    self.assertEqual(
                        hashlib.sha256(cupid).hexdigest(), fixture["sha256"]
                    )
                    if self.nasm_command is not None:
                        oracle = self._assemble(
                            self.nasm_command,
                            fixture["source"],
                            root / f"{fixture['name']}.nasm.bin",
                            "bin",
                        )
                        self.assertEqual(cupid, oracle)

    def test_active_elf32_sources_match_oracle_semantics(self):
        with tempfile.TemporaryDirectory(prefix="cupidasm-active-elf-") as directory:
            root = Path(directory)
            for fixture in OBJECT_FIXTURES:
                with self.subTest(source=fixture["source"].relative_to(REPO_ROOT)):
                    cupid_image = self._assemble(
                        (str(self.cli_path),),
                        fixture["source"],
                        root / f"{fixture['name']}.cupid.o",
                        "elf32",
                    )
                    cupid = _parse_elf32_semantics(cupid_image)
                    text = cupid["sections"].get(".text")
                    self.assertIsNotNone(text)
                    self.assertEqual((text["flags"], text["alignment"]), (0x6, 16))
                    self.assertEqual(len(text["data"]), fixture["text_size"])
                    self.assertEqual(
                        hashlib.sha256(text["data"]).hexdigest(),
                        fixture["text_sha256"],
                    )
                    self.assertEqual(len(cupid["symbols"]), fixture["symbol_count"])
                    self.assertEqual(
                        _count(
                            symbol["binding"]
                            for symbol in cupid["symbols"].values()
                        ),
                        fixture["binding_counts"],
                    )
                    self.assertEqual(
                        _count(
                            symbol["placement"]
                            for symbol in cupid["symbols"].values()
                        ),
                        fixture["placement_counts"],
                    )
                    self.assertEqual(cupid["relocations"], fixture["relocations"])

                    if self.nasm_command is not None:
                        oracle_image = self._assemble(
                            self.nasm_command,
                            fixture["source"],
                            root / f"{fixture['name']}.nasm.o",
                            "elf32",
                        )
                        oracle = _parse_elf32_semantics(oracle_image)
                        oracle_text = oracle["sections"][".text"]
                        self.assertEqual(text["data"], oracle_text["data"])
                        self.assertEqual(
                            (text["flags"], text["alignment"]),
                            (oracle_text["flags"], oracle_text["alignment"]),
                        )
                        self.assertEqual(cupid["symbols"], oracle["symbols"])
                        self.assertEqual(
                            cupid["relocations"], oracle["relocations"]
                        )


if __name__ == "__main__":
    unittest.main()

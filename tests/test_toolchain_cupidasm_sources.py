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
SMP_RAW_MAP = (
    b"cupid.raw-map.v2\n"
    b"size 4096\n"
    b"base 0x00008000\n"
    b"edges 6\n"
    b"range 0x00000000 code16\n"
    b"range 0x0000001f data\n"
    b"range 0x00000210 code32\n"
    b"range 0x00000254 data\n"
    b"edge 0x00000017 far local 0x00000210 0x00008210 32 0x00000008\n"
    b"edge 0x0000022f relative local 0x0000023a 0x0000823a 32 0x00000000\n"
    b"edge 0x00000235 relative local 0x00000229 0x00008229 32 0x00000000\n"
    b"edge 0x00000238 relative local 0x00000237 0x00008237 32 0x00000000\n"
    b"edge 0x00000250 indirect unprovable - - unknown -\n"
    b"edge 0x00000252 relative local 0x00000237 0x00008237 32 0x00000000\n"
)


RAW_FIXTURES = (
    {
        "name": "iso-big-pattern",
        "source": REPO_ROOT / "test_iso" / "big_pattern.asm",
        "size": 4096,
        "sha256": "c8f5d0341d54d951a71b136e6e2afcb14d11ed8489a7ae126a8fee0df6ecf193",
        # NASM freezes `$` across TIMES; CupidASM updates it per emission.
        "nasm_oracle": False,
    },
    {
        "name": "boot",
        "source": REPO_ROOT / "boot" / "boot.asm",
        "size": 2560,
        "sha256": "46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3",
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
        "function_count": 31,
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
        "function_count": 2,
        "relocations": (
            (".text", 0x21, 2, -4, "bkl_context_switch_release"),
        ),
    },
)


STARTUP_OBJECT_FIXTURES = (
    {
        "name": "linux-start",
        "source": REPO_ROOT
        / "toolchain"
        / "hosted"
        / "i386-linux"
        / "start.asm",
        "function_count": 6,
    },
    {
        "name": "windows-contract-start",
        "source": REPO_ROOT / "toolchain" / "hosted" / "i386-windows" / "start.asm",
        "function_count": 2,
    },
    {
        "name": "windows-tool-start",
        "source": REPO_ROOT
        / "toolchain"
        / "hosted"
        / "i386-windows"
        / "tool_start.asm",
        "function_count": 11,
    },
    {
        "name": "windows-publication-start",
        "source": REPO_ROOT
        / "toolchain"
        / "hosted"
        / "i386-windows"
        / "publication_start.asm",
        "function_count": 4,
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
                "file_offset": row[4],
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
                    "type": symbol["type"],
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
        cls.dis_path = build_path / ("cupiddis" + suffix)
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                f"{relative_build}/cupidasm{suffix}",
                f"{relative_build}/cupiddis{suffix}",
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
        try:
            source_display = source.relative_to(REPO_ROOT)
        except ValueError:
            source_display = source
        result = subprocess.run(
            [*assembler, "-f", output_format, str(source), "-o", str(output)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"{Path(assembler[0]).name} failed to assemble {source_display}\n"
            + result.stdout
            + result.stderr,
        )
        self.assertTrue(output.is_file(), f"assembler did not create {output.name}")
        return output.read_bytes()

    def _assert_function_annotations_preserve_code(self, root, fixture):
        source_text = fixture["source"].read_text(encoding="utf-8")
        function_names = {
            line.split()[1].removesuffix(":function")
            for line in source_text.splitlines()
            if line.strip().lower().startswith("global ")
            and line.strip().lower().endswith(":function")
        }
        self.assertEqual(len(function_names), fixture["function_count"])
        typed_path = root / f"{fixture['name']}.typed.o"
        ordinary_source = root / f"{fixture['name']}.ordinary.asm"
        ordinary_path = root / f"{fixture['name']}.ordinary.o"
        ordinary_source.write_text(
            source_text.replace(":function", ""), encoding="utf-8"
        )
        typed = _parse_elf32_semantics(
            self._assemble(
                (str(self.cli_path),), fixture["source"], typed_path, "elf32"
            )
        )
        ordinary = _parse_elf32_semantics(
            self._assemble(
                (str(self.cli_path),), ordinary_source, ordinary_path, "elf32"
            )
        )
        self.assertEqual(
            {
                name
                for name, symbol in typed["symbols"].items()
                if symbol["type"] == 2
            },
            function_names,
        )
        self.assertEqual(
            typed["sections"][".text"]["data"],
            ordinary["sections"][".text"]["data"],
        )
        self.assertEqual(typed["relocations"], ordinary["relocations"])
        self.assertEqual(
            {
                name: (symbol["binding"], symbol["placement"], symbol["value"])
                for name, symbol in typed["symbols"].items()
            },
            {
                name: (symbol["binding"], symbol["placement"], symbol["value"])
                for name, symbol in ordinary["symbols"].items()
            },
        )
        inspected = subprocess.run(
            [
                str(self.dis_path),
                "--require-known",
                "--require-local-targets",
                "--require-code-anchors",
                str(typed_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(inspected.returncode, 0, inspected.stderr)
        self.assertEqual(inspected.stdout, "")
        self.assertEqual(inspected.stderr, "")

    def test_nasm_oracle_exception_is_limited_to_cupid_owned_fixture(self):
        exceptions = [
            fixture["source"].relative_to(REPO_ROOT).as_posix()
            for fixture in RAW_FIXTURES
            if not fixture.get("nasm_oracle", True)
        ]
        self.assertEqual(exceptions, ["test_iso/big_pattern.asm"])

    def test_active_raw_sources_match_oracle_artifacts(self):
        with tempfile.TemporaryDirectory(prefix="cupidasm-active-raw-") as directory:
            root = Path(directory)
            for fixture in RAW_FIXTURES:
                with self.subTest(source=fixture["source"].relative_to(REPO_ROOT)):
                    cupid_path = root / f"{fixture['name']}.cupid.bin"
                    cupid = self._assemble(
                        (str(self.cli_path),),
                        fixture["source"],
                        cupid_path,
                        "bin",
                    )
                    self.assertEqual(len(cupid), fixture["size"])
                    self.assertEqual(
                        hashlib.sha256(cupid).hexdigest(), fixture["sha256"]
                    )
                    if (
                        self.nasm_command is not None
                        and fixture.get("nasm_oracle", True)
                    ):
                        oracle = self._assemble(
                            self.nasm_command,
                            fixture["source"],
                            root / f"{fixture['name']}.nasm.bin",
                            "bin",
                        )
                        self.assertEqual(cupid, oracle)

    def test_smp_map_drives_strict_source_derived_inspection(self):
        with tempfile.TemporaryDirectory(
            prefix="cupidasm-active-smp-map-"
        ) as directory:
            root = Path(directory)
            images = []
            maps = []
            for repetition in range(2):
                image = root / f"smp-{repetition}.bin"
                range_map = root / f"smp-{repetition}.cupidmap"
                assembled = subprocess.run(
                    [
                        str(self.cli_path),
                        "-f",
                        "bin",
                        "--map",
                        str(range_map),
                        str(REPO_ROOT / "kernel" / "smp" / "smp_trampoline.S"),
                        "-o",
                        str(image),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(assembled.returncode, 0, assembled.stderr)
                images.append(image.read_bytes())
                maps.append(range_map.read_bytes())

            self.assertEqual(images[0], images[1])
            self.assertEqual(len(images[0]), 4096)
            self.assertEqual(
                hashlib.sha256(images[0]).hexdigest(),
                "b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90",
            )
            self.assertEqual(maps, [SMP_RAW_MAP, SMP_RAW_MAP])

            checked = subprocess.run(
                [
                    str(self.dis_path),
                    "--raw",
                    "--range-map",
                    str(root / "smp-0.cupidmap"),
                    "--require-known",
                    "--require-local-targets",
                    "--require-source-edges",
                    str(root / "smp-0.bin"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(checked.stdout, "")
            self.assertEqual(checked.stderr, "")

            bad_target = bytearray(images[0])
            self.assertEqual(bad_target[0x230], 0x09)
            bad_target[0x230] = 0x08
            bad_path = root / "smp.bad-target.bin"
            bad_path.write_bytes(bad_target)
            rejected = subprocess.run(
                [
                    str(self.dis_path),
                    "--raw",
                    "--range-map",
                    str(root / "smp-0.cupidmap"),
                    "--require-known",
                    "--require-local-targets",
                    str(bad_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "1 of 4 direct relative targets invalid",
                rejected.stderr,
            )
            self.assertIn("1 mid-instruction", rejected.stderr)

            wrong_mode_far = bytearray(images[0])
            self.assertEqual(wrong_mode_far[0x17:0x1F], bytes.fromhex(
                "66 ea 10 82 00 00 08 00"
            ))
            wrong_mode_far[0x19] = 0x00
            wrong_mode_far[0x1A] = 0x80
            wrong_mode_path = root / "smp.wrong-mode-far.bin"
            wrong_mode_path.write_bytes(wrong_mode_far)
            structural_only = subprocess.run(
                [
                    str(self.dis_path),
                    "--raw",
                    "--range-map",
                    str(root / "smp-0.cupidmap"),
                    "--require-known",
                    "--require-local-targets",
                    str(wrong_mode_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            source_checked = subprocess.run(
                [
                    str(self.dis_path),
                    "--raw",
                    "--range-map",
                    str(root / "smp-0.cupidmap"),
                    "--require-known",
                    "--require-local-targets",
                    "--require-source-edges",
                    str(wrong_mode_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(structural_only.returncode, 0,
                             structural_only.stderr)
            self.assertEqual(source_checked.returncode, 1)
            self.assertIn("1 target mismatch", source_checked.stderr)
            self.assertIn("1 target-mode mismatch", source_checked.stderr)

    def test_boot_map_drives_strict_source_derived_inspection(self):
        with tempfile.TemporaryDirectory(
            prefix="cupidasm-active-boot-map-"
        ) as directory:
            root = Path(directory)
            image = root / "boot.bin"
            range_map = root / "boot.cupidmap"
            assembled = subprocess.run(
                [
                    str(self.cli_path),
                    "-f",
                    "bin",
                    "--map",
                    str(range_map),
                    str(REPO_ROOT / "boot" / "boot.asm"),
                    "-o",
                    str(image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(len(image.read_bytes()), 2560)
            lines = range_map.read_text(encoding="ascii").splitlines()
            self.assertEqual(lines[:4], [
                "cupid.raw-map.v2",
                "size 2560",
                "base 0x00007c00",
                "edges 12",
            ])
            self.assertEqual(lines[4], "range 0x00000000 code16")
            self.assertIn("code32", {line.rsplit(" ", 1)[-1] for line in lines[4:]})
            self.assertIn("data", {line.rsplit(" ", 1)[-1] for line in lines[4:]})
            self.assertIn(
                "edge 0x0000037d far external - 0x00100000 32 0x00000008",
                lines,
            )

            checked = subprocess.run(
                [
                    str(self.dis_path),
                    "--require-known",
                    "--require-local-targets",
                    "--require-source-edges",
                    "--raw",
                    "--range-map",
                    str(range_map),
                    str(image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(checked.stdout, "")
            self.assertEqual(checked.stderr, "")

            bad_target = bytearray(image.read_bytes())
            self.assertEqual(bad_target[0x1B], 0x15)
            bad_target[0x1B] = 0x14
            bad_image = root / "boot.bad-target.bin"
            bad_image.write_bytes(bad_target)
            rejected = subprocess.run(
                [
                    str(self.dis_path),
                    "--require-known",
                    "--require-local-targets",
                    "--raw",
                    "--range-map",
                    str(range_map),
                    str(bad_image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "1 of 9 direct relative targets invalid", rejected.stderr
            )
            self.assertIn("1 in data", rejected.stderr)

            external_far = bytearray(image.read_bytes())
            self.assertEqual(
                external_far[0x37D:0x384], bytes.fromhex("ea 00 00 10 00 08 00")
            )
            external_far[0x37E] = 0x01
            external_path = root / "boot.changed-external-far.bin"
            external_path.write_bytes(external_far)
            structural_only = subprocess.run(
                [
                    str(self.dis_path),
                    "--require-known",
                    "--require-local-targets",
                    "--raw",
                    "--range-map",
                    str(range_map),
                    str(external_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            source_checked = subprocess.run(
                [
                    str(self.dis_path),
                    "--require-known",
                    "--require-local-targets",
                    "--require-source-edges",
                    "--raw",
                    "--range-map",
                    str(range_map),
                    str(external_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(structural_only.returncode, 0,
                             structural_only.stderr)
            self.assertEqual(source_checked.returncode, 1)
            self.assertIn("1 target mismatch", source_checked.stderr)

    def test_active_elf32_sources_match_oracle_semantics(self):
        with tempfile.TemporaryDirectory(prefix="cupidasm-active-elf-") as directory:
            root = Path(directory)
            for fixture in OBJECT_FIXTURES:
                with self.subTest(source=fixture["source"].relative_to(REPO_ROOT)):
                    object_path = root / f"{fixture['name']}.cupid.o"
                    cupid_image = self._assemble(
                        (str(self.cli_path),),
                        fixture["source"],
                        object_path,
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
                    self.assertEqual(
                        sum(
                            symbol["type"] == 2
                            for symbol in cupid["symbols"].values()
                        ),
                        fixture["function_count"],
                    )

                    inspected = subprocess.run(
                        [
                            str(self.dis_path),
                            "--require-known",
                            "--require-local-targets",
                            "--require-code-anchors",
                            str(object_path),
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(inspected.returncode, 0, inspected.stderr)
                    self.assertEqual(inspected.stdout, "")

                    if fixture["name"] == "context-switch":
                        bad_target = bytearray(cupid_image)
                        displacement = text["file_offset"] + 0x29
                        self.assertEqual(bad_target[displacement], 0x0D)
                        bad_target[displacement] = 0x0C
                        bad_object = root / "context-switch.bad-target.o"
                        bad_object.write_bytes(bad_target)
                        rejected = subprocess.run(
                            [
                                str(self.dis_path),
                                "--require-known",
                                "--require-local-targets",
                                str(bad_object),
                            ],
                            cwd=REPO_ROOT,
                            text=True,
                            capture_output=True,
                        )
                        self.assertEqual(rejected.returncode, 1)
                        self.assertEqual(rejected.stdout, "")
                        self.assertIn(
                            "1 of 3 direct relative targets invalid",
                            rejected.stderr,
                        )
                        self.assertIn("1 mid-instruction", rejected.stderr)

                    rendered = subprocess.run(
                        [str(self.dis_path), str(object_path)],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(rendered.returncode, 0, rendered.stderr)
                    self.assertIn("[disassembly .text]", rendered.stdout)
                    for relocation in fixture["relocations"]:
                        self.assertIn(relocation[4], rendered.stdout)

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

    def test_active_function_annotations_preserve_code_and_relocations(self):
        with tempfile.TemporaryDirectory(
            prefix="cupidasm-function-symbols-"
        ) as directory:
            root = Path(directory)
            for fixture in (*OBJECT_FIXTURES, *STARTUP_OBJECT_FIXTURES):
                with self.subTest(source=fixture["source"].relative_to(REPO_ROOT)):
                    self._assert_function_annotations_preserve_code(root, fixture)


if __name__ == "__main__":
    unittest.main()

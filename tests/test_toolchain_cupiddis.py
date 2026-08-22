import hashlib
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


def elf32_executable(segments):
    header_size = 52
    program_header_size = 32
    payload_offset = header_size + program_header_size * len(segments)
    image = bytearray(payload_offset + sum(len(data) for _, _, data, _ in segments))
    image[:7] = b"\x7fELF\x01\x01\x01"
    entry = segments[0][0] if segments else 0
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        2,
        3,
        1,
        entry,
        header_size,
        0,
        0,
        header_size,
        program_header_size,
        len(segments),
        0,
        0,
        0,
    )
    cursor = payload_offset
    for index, (address, flags, data, memory_size) in enumerate(segments):
        struct.pack_into(
            "<IIIIIIII",
            image,
            header_size + index * program_header_size,
            1,
            cursor,
            address,
            address,
            len(data),
            memory_size,
            flags,
            1,
        )
        image[cursor : cursor + len(data)] = data
        cursor += len(data)
    return bytes(image)


def configured_symbol_reader_command():
    configured = bootstrap_baseline.optional_oracle_commands()[
        "symbol_reader"
    ]
    return bootstrap_baseline.resolve_tool_command(configured)


def elf32_relocation_sections(image):
    section_headers = struct.unpack_from("<I", image, 32)[0]
    section_header_size = struct.unpack_from("<H", image, 46)[0]
    section_count = struct.unpack_from("<H", image, 48)[0]
    sections = []
    for section_index in range(section_count):
        header = section_headers + section_index * section_header_size
        section_type = struct.unpack_from("<I", image, header + 4)[0]
        if section_type != 9:
            continue
        target_index = struct.unpack_from("<I", image, header + 28)[0]
        target_header = section_headers + target_index * section_header_size
        target_flags = struct.unpack_from("<I", image, target_header + 8)[0]
        relocation_offset = struct.unpack_from("<I", image, header + 16)[0]
        relocation_size = struct.unpack_from("<I", image, header + 20)[0]
        sections.append(
            (
                header,
                target_index,
                target_flags,
                relocation_offset,
                relocation_size,
            )
        )
    return sections


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
        cls.asm_path = build_path / ("cupidasm" + suffix)
        relative_prefix = relative_build.as_posix()
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                f"{relative_prefix}/cupiddis-contract{suffix}",
                f"{relative_prefix}/elf32-contract{suffix}",
                f"{relative_prefix}/cupiddis{suffix}",
                f"{relative_prefix}/cupidasm{suffix}",
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
        cls.unowned_relocation_path = (
            Path(cls._fixture_directory.name) / "unowned-relocation.o"
        )
        unowned_relocation = bytearray(cls.object_path.read_bytes())
        relocation_rewritten = False
        for _, _, target_flags, relocation_offset, _ in (
            elf32_relocation_sections(unowned_relocation)
        ):
            if target_flags & 4 == 0:
                continue
            struct.pack_into("<I", unowned_relocation, relocation_offset, 0)
            relocation_rewritten = True
            break
        if not relocation_rewritten:
            raise AssertionError("CupidDis fixture has no code relocation")
        cls.unowned_relocation_path.write_bytes(unowned_relocation)
        cls.duplicate_relocation_path = (
            Path(cls._fixture_directory.name) / "duplicate-relocation.o"
        )
        duplicate_relocation = bytearray(cls.object_path.read_bytes())
        relocation_sections = elf32_relocation_sections(duplicate_relocation)
        executable_section = next(
            (section for section in relocation_sections if section[2] & 4),
            None,
        )
        spare_section = next(
            (
                section
                for section in relocation_sections
                if (section[2] & 4) == 0
            ),
            None,
        )
        if executable_section is None or spare_section is None:
            raise AssertionError(
                "CupidDis fixture needs code and data relocations"
            )
        _, code_target, _, code_offset, code_size = (
            executable_section
        )
        spare_header, _, _, spare_offset, spare_size = spare_section
        if code_size == 0 or code_size % 8 != 0 or spare_size < 8:
            raise AssertionError("CupidDis fixture has invalid relocation rows")
        duplicate_relocation[spare_offset : spare_offset + 8] = (
            duplicate_relocation[code_offset : code_offset + 8]
        )
        struct.pack_into("<I", duplicate_relocation, spare_header + 20, 8)
        struct.pack_into(
            "<I", duplicate_relocation, spare_header + 28, code_target
        )
        cls.duplicate_relocation_path.write_bytes(duplicate_relocation)
        cls.raw_path = Path(cls._fixture_directory.name) / "boot.bin"
        cls.raw_path.write_bytes(bytes([0xB8, 0x34, 0x12, 0xC3]))
        cls.shrd_path = Path(cls._fixture_directory.name) / "ctool-shrd.bin"
        cls.shrd_path.write_bytes(
            bytes([0x0F, 0xAD, 0xF8, 0x0F, 0xAD, 0xF8, 0xC3])
        )
        cls.parity_setcc_path = (
            Path(cls._fixture_directory.name) / "cupidc-parity-setcc.bin"
        )
        cls.parity_setcc_path.write_bytes(
            bytes.fromhex(
                "0f 94 c0 0f 9b c2 20 d0 0f b6 c0 "
                "0f 95 c0 0f 9a c2 08 d0 0f b6 c0 c3"
            )
        )
        cls.mixed_raw_path = (
            Path(cls._fixture_directory.name) / "mixed-mode.bin"
        )
        cls.mixed_raw_path.write_bytes(
            bytes(
                [
                    0xB8, 0x34, 0x12,
                    0x00, 0x00, 0x90, 0xC3,
                    0xB8, 0x78, 0x56, 0x34, 0x12,
                    0xB8, 0xCD, 0xAB, 0xC3,
                ]
            )
        )
        cls.mode_alias_path = (
            Path(cls._fixture_directory.name) / "code-only-modes.bin"
        )
        cls.mode_alias_path.write_bytes(
            bytes(
                [
                    0xB8, 0x34, 0x12,
                    0xB8, 0x78, 0x56, 0x34, 0x12,
                    0xB8, 0xCD, 0xAB, 0xC3,
                ]
            )
        )
        cls.not_elf_path = Path(cls._fixture_directory.name) / "not-elf.bin"
        cls.not_elf_path.write_bytes(b"not elf")
        cls.bad_elf_path = Path(cls._fixture_directory.name) / "bad.elf"
        cls.bad_elf_path.write_bytes(b"\x7fELF")
        cls.incomplete_code_path = (
            Path(cls._fixture_directory.name) / "incomplete-code.bin"
        )
        cls.incomplete_code_path.write_bytes(
            bytes.fromhex("90 0f ff c0 66 66 90 0f 0f ff 66 66 0f")
        )
        cls.clean_code_path = (
            Path(cls._fixture_directory.name) / "clean-code.bin"
        )
        cls.clean_code_path.write_bytes(bytes.fromhex("90 c3"))
        cls.bad_code_path = Path(cls._fixture_directory.name) / "bad-code.bin"
        cls.bad_code_path.write_bytes(
            bytes.fromhex("90 0f ff c0 66 66 90 0f")
        )
        cls.truncated_code_path = (
            Path(cls._fixture_directory.name) / "truncated-code.bin"
        )
        cls.truncated_code_path.write_bytes(bytes.fromhex("90 0f"))
        cls.valid_target_path = (
            Path(cls._fixture_directory.name) / "valid-target.bin"
        )
        cls.valid_target_path.write_bytes(bytes.fromhex("eb 01 90 c3"))
        cls.middle_target_path = (
            Path(cls._fixture_directory.name) / "middle-target.bin"
        )
        cls.middle_target_path.write_bytes(
            bytes.fromhex("eb 01 b8 00 00 00 00 c3")
        )
        cls.outside_target_path = (
            Path(cls._fixture_directory.name) / "outside-target.bin"
        )
        cls.outside_target_path.write_bytes(bytes.fromhex("eb 7f"))
        cls.data_target_path = (
            Path(cls._fixture_directory.name) / "data-target.bin"
        )
        cls.data_target_path.write_bytes(bytes.fromhex("eb 00 90 c3"))
        cls.wrong_mode_target_path = (
            Path(cls._fixture_directory.name) / "wrong-mode-target.bin"
        )
        cls.wrong_mode_target_path.write_bytes(bytes.fromhex("eb 00 c3"))
        cls.cross_data_target_path = (
            Path(cls._fixture_directory.name) / "cross-data-target.bin"
        )
        cls.cross_data_target_path.write_bytes(
            bytes.fromhex("eb 02 11 22 c3")
        )
        cls.wrapped_target_path = (
            Path(cls._fixture_directory.name) / "wrapped-target.bin"
        )
        cls.wrapped_target_path.write_bytes(bytes.fromhex("eb 00 c3"))
        local_object_sources = (
            (
                "local_target_object_path",
                "valid-local-target.asm",
                "valid-local-target.o",
                "BITS 32\n"
                "extern external\n"
                "section .text\n"
                "entry:\n"
                "    call external\n"
                "    jmp done\n"
                "    nop\n"
                "done:\n"
                "    ret\n",
            ),
            (
                "outside_target_object_path",
                "outside-local-target.asm",
                "outside-local-target.o",
                "BITS 32\nsection .text\ndb 0xeb, 0x7f\n",
            ),
            (
                "middle_target_object_path",
                "middle-local-target.asm",
                "middle-local-target.o",
                "BITS 32\nsection .text\n"
                "db 0xeb, 0x01, 0xb8, 0, 0, 0, 0, 0xc3\n",
            ),
        )
        for attribute, source_name, object_name, source_text in (
            local_object_sources
        ):
            source_path = Path(cls._fixture_directory.name) / source_name
            object_path = Path(cls._fixture_directory.name) / object_name
            source_path.write_text(source_text, encoding="utf-8")
            assembled = subprocess.run(
                [
                    str(cls.asm_path),
                    "-f",
                    "elf32",
                    str(source_path),
                    "-o",
                    str(object_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            if assembled.returncode != 0:
                raise AssertionError(
                    "CupidDis local-target fixture assembly failed\n"
                    + assembled.stdout
                    + assembled.stderr
                )
            setattr(cls, attribute, object_path)
        cls.exec_path = Path(cls._fixture_directory.name) / "program.elf"
        executable = bytearray(90)
        executable[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HHIIIIIHHHHHH", executable, 16, 2, 3, 1,
                         0x00400000, 52, 0, 0, 52, 32, 1, 0, 0, 0)
        struct.pack_into("<IIIIIIII", executable, 52, 1, 84, 0x00400000,
                         0x00400000, 6, 6, 5, 4)
        executable[84:] = bytes([0xB8, 0x78, 0x56, 0x34, 0x12, 0xC3])
        cls.exec_path.write_bytes(executable)
        exec_target_fixtures = (
            (
                "valid_exec_target_path",
                "valid-exec-target.elf",
                (
                    (
                        0x00400000,
                        5,
                        bytes.fromhex("eb 01 90 c3 e9 f7 00 00 00"),
                        9,
                    ),
                    (0x00400100, 5, bytes.fromhex("c3"), 1),
                ),
            ),
            (
                "outside_exec_target_path",
                "outside-exec-target.elf",
                ((0x00400000, 5, bytes.fromhex("e9 fb 02 00 00 c3"), 6),),
            ),
            (
                "data_exec_target_path",
                "data-exec-target.elf",
                (
                    (0x00400000, 5, bytes.fromhex("e9 fb 01 00 00 c3"), 6),
                    (0x00400200, 4, bytes.fromhex("00"), 1),
                ),
            ),
            (
                "executable_bss_exec_target_path",
                "executable-bss-exec-target.elf",
                (
                    (
                        0x00400000,
                        5,
                        bytes.fromhex("e9 fb 00 00 00 c3"),
                        0x101,
                    ),
                ),
            ),
            (
                "middle_exec_target_path",
                "middle-exec-target.elf",
                ((0x00400000, 5, bytes.fromhex("eb ff c3"), 3),),
            ),
            (
                "far_indirect_exec_target_path",
                "far-indirect-exec-target.elf",
                (
                    (
                        0x00400000,
                        5,
                        bytes.fromhex(
                            "ea 00 01 40 00 08 00 ff d0 c3"
                        ),
                        10,
                    ),
                ),
            ),
            (
                "overlapping_exec_target_path",
                "overlapping-exec-target.elf",
                (
                    (0x00400000, 5, bytes.fromhex("90 c3"), 2),
                    (0x00400001, 5, bytes.fromhex("c3"), 1),
                ),
            ),
        )
        for attribute, name, segments in exec_target_fixtures:
            fixture_path = Path(cls._fixture_directory.name) / name
            fixture_path.write_bytes(elf32_executable(segments))
            setattr(cls, attribute, fixture_path)
        unsupported_exec_programs = (
            ("dynamic_exec_target_path", "dynamic-exec-target.elf", 2),
            ("interpreter_exec_target_path", "interpreter-exec-target.elf", 3),
        )
        for attribute, name, program_type in unsupported_exec_programs:
            executable = bytearray(
                elf32_executable(
                    (
                        (0x00400000, 5, bytes.fromhex("c3"), 1),
                        (0x00400100, 4, b"/ld.so\0", 7),
                    )
                )
            )
            struct.pack_into("<I", executable, 84, program_type)
            fixture_path = Path(cls._fixture_directory.name) / name
            fixture_path.write_bytes(executable)
            setattr(cls, attribute, fixture_path)
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

    def test_prepared_decoder_is_reused_across_indexed_inspections(self):
        self.run_contract("indexed")

    def test_typed_local_relative_target_policy(self):
        self.run_contract("targets")

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

    def test_cli_requires_every_code_region_to_decode_cleanly(self):
        clean = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                str(self.exec_path),
                str(self.object_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(clean.returncode, 0, clean.stderr)
        self.assertEqual(clean.stdout, "")
        self.assertEqual(clean.stderr, "")

        incomplete = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--raw",
                "--mode=32",
                "--range-at=8:data",
                "--base=0x407000",
                str(self.incomplete_code_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(incomplete.returncode, 1)
        self.assertEqual(incomplete.stdout, "")
        self.assertIn(
            f"{self.incomplete_code_path}: code check failed: "
            "3 known, 1 unknown, 1 invalid, 1 truncated",
            incomplete.stderr,
        )

        mixed = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--raw",
                "--mode=32",
                "--base=0",
                str(self.clean_code_path),
                str(self.bad_code_path),
                str(self.truncated_code_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(mixed.returncode, 1)
        self.assertEqual(mixed.stdout, "")
        self.assertIn(
            f"{self.bad_code_path}: code check failed: "
            "3 known, 1 unknown, 1 invalid, 1 truncated",
            mixed.stderr,
        )
        self.assertIn(
            f"{self.truncated_code_path}: code check failed: "
            "1 known, 0 unknown, 0 invalid, 1 truncated",
            mixed.stderr,
        )
        self.assertNotIn(
            f"{self.clean_code_path}: code check failed", mixed.stderr
        )

        unowned_relocation = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                str(self.unowned_relocation_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(unowned_relocation.returncode, 1)
        self.assertEqual(unowned_relocation.stdout, "")
        self.assertIn(
            f"{self.unowned_relocation_path}: code check failed: "
            "2 known, 0 unknown, 0 invalid, 0 truncated, "
            "1 of 1 executable relocations unmatched",
            unowned_relocation.stderr,
        )

        duplicate_relocation = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                str(self.duplicate_relocation_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(duplicate_relocation.returncode, 1)
        self.assertEqual(duplicate_relocation.stdout, "")
        self.assertIn(
            "ELF32 relocation fields overlap",
            duplicate_relocation.stderr,
        )

        missing_path = Path(self._fixture_directory.name) / "missing-code.bin"
        missing = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--raw",
                "--mode=32",
                "--base=0",
                str(self.clean_code_path),
                str(missing_path),
                str(self.truncated_code_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(missing.returncode, 1)
        self.assertEqual(missing.stdout, "")
        self.assertIn(str(missing_path), missing.stderr)
        self.assertIn(
            f"{self.truncated_code_path}: code check failed: "
            "1 known, 0 unknown, 0 invalid, 1 truncated",
            missing.stderr,
        )

        ordinary_multi = subprocess.run(
            [str(self.cli_path), str(self.exec_path), str(self.object_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(ordinary_multi.returncode, 2)
        self.assertEqual(ordinary_multi.stdout, "")
        self.assertIn("usage: cupiddis", ordinary_multi.stderr)

    def test_cli_prepares_one_decoder_for_the_whole_strict_batch(self):
        source = (TOOLCHAIN_ROOT / "cupiddis_main.cc").read_text(
            encoding="utf-8"
        )
        strict_start = source.index("if (cli.require_known == CTOOL_TRUE)")
        strict_end = source.index("free(cli.raw_ranges);", strict_start)
        strict_branch = source[strict_start:strict_end]

        self.assertEqual(
            strict_branch.count("ctool_x86_decoder_prepare("), 1
        )
        prepare = strict_branch.index("ctool_x86_decoder_prepare(")
        loop = strict_branch.index(
            "for (index = 0u; index < cli.input_count; index++)"
        )
        reuse = strict_branch.index(
            "cupiddis_check_known_input(&cli, decoder,"
        )
        self.assertLess(prepare, loop)
        self.assertLess(loop, reuse)

        helper_start = source.index("static int cupiddis_check_known_input(")
        helper_end = source.index("\nint main(", helper_start)
        helper = source[helper_start:helper_end]
        self.assertIn(
            "ctool_dis_inspect_indexed(job, decoder,", helper
        )

    def test_cli_requires_local_targets_on_raw_object_and_executable_code(self):
        def run(path, *options):
            return subprocess.run(
                [
                    str(self.cli_path),
                    "--require-known",
                    "--require-local-targets",
                    "--raw",
                    *options,
                    str(path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        valid = run(self.valid_target_path, "--mode=32", "--base=0")
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertEqual(valid.stdout, "")
        self.assertEqual(valid.stderr, "")

        legacy = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--raw",
                "--mode=32",
                "--base=0",
                str(self.middle_target_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(legacy.returncode, 0, legacy.stderr)

        failures = (
            (
                self.middle_target_path,
                ("--mode=32", "--base=0"),
                "0 outside image, 0 in data, 0 wrong mode, "
                "1 mid-instruction",
            ),
            (
                self.outside_target_path,
                ("--mode=32", "--base=0"),
                "1 outside image, 0 in data, 0 wrong mode, "
                "0 mid-instruction",
            ),
            (
                self.data_target_path,
                ("--mode=32", "--range-at=2:data", "--base=0"),
                "0 outside image, 1 in data, 0 wrong mode, "
                "0 mid-instruction",
            ),
            (
                self.wrong_mode_target_path,
                ("--mode=32", "--range-at=2:16", "--base=0"),
                "0 outside image, 0 in data, 1 wrong mode, "
                "0 mid-instruction",
            ),
        )
        for path, options, reason in failures:
            with self.subTest(path=path.name):
                result = run(path, *options)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(
                    f"{path}: local target check failed: "
                    "1 of 1 direct relative targets invalid",
                    result.stderr,
                )
                self.assertIn(reason, result.stderr)

        cross_data = run(
            self.cross_data_target_path,
            "--mode=32",
            "--range-at=2:data",
            "--range-at=4:32",
            "--base=0",
        )
        self.assertEqual(cross_data.returncode, 0, cross_data.stderr)

        wrapped = run(
            self.wrapped_target_path, "--mode=16", "--base=0xfffe"
        )
        self.assertEqual(wrapped.returncode, 0, wrapped.stderr)

        missing_known = subprocess.run(
            [
                str(self.cli_path),
                "--require-local-targets",
                "--raw",
                "--mode=32",
                "--base=0",
                str(self.valid_target_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(missing_known.returncode, 2)
        self.assertIn("usage: cupiddis", missing_known.stderr)

        valid_object = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--require-local-targets",
                str(self.local_target_object_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(valid_object.returncode, 0, valid_object.stderr)
        self.assertEqual(valid_object.stdout, "")
        self.assertEqual(valid_object.stderr, "")

        object_failures = (
            (self.outside_target_object_path, "1 outside section, "),
            (self.middle_target_object_path, "0 outside section, "),
        )
        for path, outside_reason in object_failures:
            with self.subTest(path=path.name):
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-local-targets",
                        str(path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(
                    f"{path}: local target check failed: "
                    "1 of 1 direct relative targets invalid",
                    result.stderr,
                )
                self.assertIn(outside_reason, result.stderr)
                self.assertIn(
                    "0 mid-instruction"
                    if path == self.outside_target_object_path
                    else "1 mid-instruction",
                    result.stderr,
                )

        for path in (
            self.valid_exec_target_path,
            self.far_indirect_exec_target_path,
        ):
            with self.subTest(path=path.name):
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-local-targets",
                        str(path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")

        exec_failures = (
            (
                self.outside_exec_target_path,
                "1 outside loaded image, 0 in loaded bytes without "
                "file-backed executable code, "
                "0 mid-instruction",
            ),
            (
                self.data_exec_target_path,
                "0 outside loaded image, 1 in loaded bytes without "
                "file-backed executable code, "
                "0 mid-instruction",
            ),
            (
                self.executable_bss_exec_target_path,
                "0 outside loaded image, 1 in loaded bytes without "
                "file-backed executable code, "
                "0 mid-instruction",
            ),
            (
                self.middle_exec_target_path,
                "0 outside loaded image, 0 in loaded bytes without "
                "file-backed executable code, "
                "1 mid-instruction",
            ),
        )
        for path, reason in exec_failures:
            with self.subTest(path=path.name):
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-local-targets",
                        str(path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(
                    f"{path}: local target check failed: "
                    "1 of 1 direct relative targets invalid",
                    result.stderr,
                )
                self.assertIn(reason, result.stderr)

        overlap = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--require-local-targets",
                str(self.overlapping_exec_target_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(overlap.returncode, 1)
        self.assertEqual(overlap.stdout, "")
        self.assertNotIn("usage: cupiddis", overlap.stderr)
        self.assertIn(
            "executable local target checks require non-overlapping "
            "executable load regions",
            overlap.stderr,
        )

        for path in (
            self.dynamic_exec_target_path,
            self.interpreter_exec_target_path,
        ):
            with self.subTest(path=path.name):
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--require-known",
                        "--require-local-targets",
                        str(path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertNotIn("usage: cupiddis", result.stderr)
                self.assertIn(
                    "executable local target checks require a static image "
                    "without PT_DYNAMIC or PT_INTERP",
                    result.stderr,
                )

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

    def test_cupidasm_source_selectors_round_trip_through_cupiddis(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "shared-x86-selectors.asm"
            image = root / "shared-x86-selectors.bin"
            source.write_text(
                "BITS 32\n"
                "    shrd eax, edi, cl\n"
                "    nop word [eax]\n"
                "    setc dl\n"
                "    iretd\n"
                "    pushad\n"
                "    popad\n"
                "    pushfd\n"
                "    popfd\n",
                encoding="utf-8",
            )
            assembled = subprocess.run(
                [
                    str(self.asm_path),
                    "-f",
                    "bin",
                    str(source),
                    "-o",
                    str(image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(
                image.read_bytes(),
                bytes.fromhex(
                    "0f ad f8 66 0f 1f 00 0f 92 c2 cf 60 61 9c 9d"
                ),
            )

            command = [
                str(self.cli_path),
                "--raw",
                "--mode=32",
                "--base=0x2000",
                str(image),
            ]
            decoded = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            repeated = subprocess.run(
                command, cwd=REPO_ROOT, text=True, capture_output=True
            )
            checked = subprocess.run(
                [
                    str(self.cli_path),
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0x2000",
                    str(image),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(decoded.returncode, 0, decoded.stderr)
            self.assertEqual(repeated.returncode, 0, repeated.stderr)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(decoded.stdout, repeated.stdout)
            self.assertEqual(checked.stdout, "")
            self.assertIn("shrd eax, edi, cl", decoded.stdout)
            self.assertIn("nop word [eax]", decoded.stdout)
            self.assertIn("setb dl", decoded.stdout)
            self.assertIn("iret", decoded.stdout)
            self.assertIn("pusha", decoded.stdout)
            self.assertIn("popa", decoded.stdout)
            self.assertIn("pushf", decoded.stdout)
            self.assertIn("popf", decoded.stdout)

    def test_cli_decodes_active_ctool_double_precision_right_shifts(self):
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=32",
                "--base=0x1790",
                str(self.shrd_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn(
            "00001790:  0F AD F8  shrd eax, edi, cl", decoded.stdout
        )
        self.assertIn(
            "00001793:  0F AD F8  shrd eax, edi, cl", decoded.stdout
        )
        self.assertIn("00001796:  C3  ret", decoded.stdout)
        self.assertNotIn("db 0x0F", decoded.stdout)
        self.assertNotIn("clc", decoded.stdout)

    def test_cli_decodes_private_cupidc_parity_setcc_sequences(self):
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=32",
                "--base=0x1800",
                str(self.parity_setcc_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("00001803:  0F 9B C2  setnp dl", decoded.stdout)
        self.assertIn("00001806:  20 D0  and al, dl", decoded.stdout)
        self.assertIn("0000180E:  0F 9A C2  setp dl", decoded.stdout)
        self.assertIn("00001811:  08 D0  or al, dl", decoded.stdout)
        self.assertIn(
            "00001813:  0F B6 C0  movzx eax, al", decoded.stdout
        )
        self.assertIn("00001816:  C3  ret", decoded.stdout)
        self.assertNotIn("db 0x0F", decoded.stdout)

    def test_cli_raw_mode_changes_decode_one_flat_image(self):
        decoded = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--range-at",
                "3:data",
                "--range-at=7:32",
                "--range-at=12:16",
                "--base",
                "0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertIn("00007C00", decoded.stdout)
        self.assertIn("mov ax, 0x1234", decoded.stdout)
        self.assertIn("00007C03", decoded.stdout)
        self.assertIn("db 0x00, 0x00, 0x90, 0xC3", decoded.stdout)
        self.assertNotIn("add byte", decoded.stdout)
        self.assertIn("00007C07", decoded.stdout)
        self.assertIn("mov eax, 0x12345678", decoded.stdout)
        self.assertIn("00007C0C", decoded.stdout)
        self.assertIn("mov ax, 0xABCD", decoded.stdout)

        duplicate_start = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--mode-at=0:32",
                "--base=0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(duplicate_start.returncode, 1)
        self.assertIn(
            "raw range starts must increase without overlap",
            duplicate_start.stderr,
        )

        outside_input = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                f"--range-at={len(self.mixed_raw_path.read_bytes())}:data",
                "--base=0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(outside_input.returncode, 1)
        self.assertIn("raw range start is outside input", outside_input.stderr)

        code_only_alias = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--mode=16",
                "--mode-at=3:32",
                "--mode-at=8:16",
                "--base=0x7c00",
                str(self.mode_alias_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(code_only_alias.returncode, 0, code_only_alias.stderr)
        self.assertIn("mov eax, 0x12345678", code_only_alias.stdout)
        self.assertIn("00007C08", code_only_alias.stdout)

    def test_cli_consumes_a_size_bound_raw_range_map(self):
        range_map = Path(self._fixture_directory.name) / "mixed-mode.cupidmap"
        range_map.write_text(
            "cupid.raw-map.v1\n"
            "size 16\n"
            "base 0x7c00\n"
            "range 0 code16\n"
            "range 3 data\n"
            "range 7 code32\n"
            "range 12 code16\n",
            encoding="ascii",
        )
        command = [
            str(self.cli_path),
            "--raw",
            "--range-map",
            str(range_map),
            str(self.mixed_raw_path),
        ]
        decoded = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        repeated = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        checked = subprocess.run(
            [
                str(self.cli_path),
                "--require-known",
                "--raw",
                "--range-map",
                str(range_map),
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)
        self.assertEqual(decoded.stdout, repeated.stdout)
        self.assertIn("mov ax, 0x1234", decoded.stdout)
        self.assertIn("db 0x00, 0x00, 0x90, 0xC3", decoded.stdout)
        self.assertIn("mov eax, 0x12345678", decoded.stdout)
        self.assertIn("mov ax, 0xABCD", decoded.stdout)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        self.assertEqual(checked.stdout, "")
        self.assertEqual(checked.stderr, "")

    def test_cli_rejects_stale_and_malformed_raw_range_maps(self):
        fixture_root = Path(self._fixture_directory.name)
        cases = (
            (
                "unknown schema",
                "cupid.raw-map.v2\nsize 16\nbase 0\nrange 0 code16\n",
                "raw range map has an unsupported schema",
            ),
            (
                "missing size",
                "cupid.raw-map.v1\nbase 0\nrange 0 code16\n",
                "raw range map requires one size",
            ),
            (
                "duplicate start",
                "cupid.raw-map.v1\nsize 16\nbase 0\n"
                "range 0 code16\nrange 0 data\n",
                "raw range starts must increase",
            ),
            (
                "invalid kind",
                "cupid.raw-map.v1\nsize 16\nbase 0\nrange 0 maybe\n",
                "raw range kind must be code16, code32, or data",
            ),
        )
        for name, contents, expected in cases:
            with self.subTest(name=name):
                range_map = fixture_root / f"bad-{name.replace(' ', '-')}.cupidmap"
                range_map.write_text(contents, encoding="ascii")
                result = subprocess.run(
                    [
                        str(self.cli_path),
                        "--raw",
                        "--range-map",
                        str(range_map),
                        str(self.mixed_raw_path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(expected, result.stderr)

        stale_map = fixture_root / "stale.cupidmap"
        stale_map.write_text(
            "cupid.raw-map.v1\n"
            "size 15\n"
            "base 0x7c00\n"
            "range 0 code16\n",
            encoding="ascii",
        )
        stale = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--range-map",
                str(stale_map),
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(stale.returncode, 1)
        self.assertEqual(stale.stdout, "")
        self.assertIn(
            "raw range map expects 15 bytes, input has 16",
            stale.stderr,
        )

        conflict = subprocess.run(
            [
                str(self.cli_path),
                "--raw",
                "--range-map",
                str(stale_map),
                "--mode=16",
                "--base=0x7c00",
                str(self.mixed_raw_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(conflict.returncode, 2)
        self.assertEqual(conflict.stdout, "")
        self.assertIn("usage: cupiddis", conflict.stderr)

    def test_cli_typed_ranges_follow_the_active_smp_trampoline_layout(self):
        trampoline = Path(self._fixture_directory.name) / "smp-trampoline.bin"
        assembled = subprocess.run(
            [
                str(self.asm_path),
                "-f",
                "bin",
                str(REPO_ROOT / "kernel" / "smp" / "smp_trampoline.S"),
                "-o",
                str(trampoline),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(assembled.returncode, 0, assembled.stderr)
        image = trampoline.read_bytes()
        self.assertEqual(len(image), 4096)
        self.assertEqual(
            hashlib.sha256(image).hexdigest(),
            "b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90",
        )

        command = [
            str(self.cli_path),
            "--raw",
            "--mode=16",
            "--range-at=0x1f:data",
            "--range-at=0x210:32",
            "--range-at=0x254:data",
            "--base=0x8000",
            str(trampoline),
        ]
        decoded = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        repeated = subprocess.run(
            command, cwd=REPO_ROOT, text=True, capture_output=True
        )
        self.assertEqual(decoded.returncode, 0, decoded.stderr)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)
        self.assertEqual(decoded.stdout, repeated.stdout)
        self.assertIn("00008000", decoded.stdout)
        self.assertIn("cli", decoded.stdout)
        self.assertIn("0000801F", decoded.stdout)
        self.assertIn("db 0x00", decoded.stdout)
        self.assertIn("00008210", decoded.stdout)
        self.assertIn("mov ax, 0x10", decoded.stdout)
        self.assertIn("00008254", decoded.stdout)
        self.assertNotIn("add byte [bx+si], al", decoded.stdout)
        self.assertNotIn("add byte [eax], al", decoded.stdout)

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

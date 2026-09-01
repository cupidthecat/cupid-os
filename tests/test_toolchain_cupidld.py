import os
import shutil
import stat
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


def _host_compiler():
    configured = os.environ.get("CC")
    candidates = [configured] if configured else []
    candidates += ["clang", "gcc", "cc"]
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    raise unittest.SkipTest("a hosted C compiler is required")


def _build_cli(build: Path):
    suffix = ".exe" if os.name == "nt" else ""
    output = build / ("cupidld" + suffix)
    command = [
        _host_compiler(),
        "-I",
        str(TOOLCHAIN_ROOT),
        "-std=c11",
        "-O2",
        "-pedantic",
        "-Werror",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wpointer-arith",
        "-Wcast-qual",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Wconversion",
        "-Wsign-conversion",
        "-x",
        "c",
        str(TOOLCHAIN_ROOT / "ctool.cc"),
        str(TOOLCHAIN_ROOT / "ctool_host.cc"),
        str(TOOLCHAIN_ROOT / "elf32.cc"),
        str(TOOLCHAIN_ROOT / "cupidld.cc"),
        str(TOOLCHAIN_ROOT / "cupidld_main.cc"),
        "-o",
        str(output),
    ]
    result = subprocess.run(
        command, cwd=REPO_ROOT, text=True, capture_output=True
    )
    if result.returncode != 0:
        raise AssertionError(
            "CupidLD hosted build failed\n" + result.stdout + result.stderr
        )
    return output


def _build_publication_harness(build: Path):
    suffix = ".exe" if os.name == "nt" else ""
    source = build / "cupidld-publication-harness.c"
    output = build / ("cupidld-publication-harness" + suffix)
    source.write_text(
        r'''
#define main cupidld_embedded_main
int cupidld_embedded_main(int argc, char **argv);
#include "cupidld_main.cc"
#undef main

enum {
  FAULT_NONE = 0,
  FAULT_SHORT_WRITE = 1,
  FAULT_CLOSE = 2,
  FAULT_REPLACE = 3,
  FAULT_SUBSTITUTE = 4
};

static ctool_u8 fake_destination[64];
static ctool_u8 fake_candidate[64];
static ctool_u32 fake_destination_size;
static ctool_u32 fake_candidate_size;
static int fake_fault;
static int fake_discard_count;
static ctool_u32 fake_open_failures;
static ctool_u32 fake_open_count;

static void fake_set_destination(const char *text) {
  fake_destination_size = (ctool_u32)strlen(text);
  (void)memcpy(fake_destination, text, fake_destination_size);
  fake_candidate_size = 0u;
  fake_discard_count = 0;
  fake_open_failures = 0u;
  fake_open_count = 0u;
}

static int fake_destination_is(const char *text) {
  size_t size = strlen(text);
  return size == (size_t)fake_destination_size &&
         memcmp(fake_destination, text, size) == 0;
}

static ctool_status_t fake_open(
    const char *path, cupidld_publication_file_t *file_out) {
  if (path == (const char *)0 || strstr(path, ".cupid-tmp-") == (char *)0 ||
      file_out == (cupidld_publication_file_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  fake_open_count++;
  if (fake_open_failures != 0u) {
    fake_open_failures--;
    return CTOOL_ERR_IO;
  }
  (void)memset(file_out, 0, sizeof(*file_out));
  fake_candidate_size = 0u;
  return CTOOL_OK;
}

static ctool_status_t fake_write(
    cupidld_publication_file_t *file, ctool_bytes_t contents) {
  (void)file;
  if (contents.size > (ctool_u32)sizeof(fake_candidate)) {
    return CTOOL_ERR_LIMIT;
  }
  if (fake_fault == FAULT_SHORT_WRITE) {
    if (contents.size != 0u) {
      fake_candidate[0] = contents.data[0];
      fake_candidate_size = 1u;
    }
    return CTOOL_ERR_IO;
  }
  if (contents.size != 0u) {
    (void)memcpy(fake_candidate, contents.data, contents.size);
  }
  fake_candidate_size = contents.size;
  return CTOOL_OK;
}

static ctool_status_t fake_close(cupidld_publication_file_t *file) {
  (void)file;
  return fake_fault == FAULT_CLOSE ? CTOOL_ERR_IO : CTOOL_OK;
}

static ctool_status_t fake_verify(
    const char *candidate, ctool_bytes_t contents) {
  (void)candidate;
  if (fake_fault == FAULT_SUBSTITUTE && fake_candidate_size != 0u) {
    fake_candidate[0] ^= 0xffu;
  }
  if (fake_candidate_size != contents.size ||
      (contents.size != 0u &&
       memcmp(fake_candidate, contents.data, contents.size) != 0)) {
    return CTOOL_ERR_IO;
  }
  return CTOOL_OK;
}

static ctool_status_t fake_replace(const char *candidate,
                                   const char *destination) {
  (void)candidate;
  (void)destination;
  if (fake_fault == FAULT_REPLACE) {
    return CTOOL_ERR_IO;
  }
  (void)memcpy(fake_destination, fake_candidate, fake_candidate_size);
  fake_destination_size = fake_candidate_size;
  fake_candidate_size = 0u;
  return CTOOL_OK;
}

static void fake_discard(const char *candidate) {
  (void)candidate;
  fake_candidate_size = 0u;
  fake_discard_count++;
}

static int expect_failed_publication(
    int fault, ctool_bytes_t replacement,
    const cupidld_publication_ops_t *ops) {
  ctool_status_t status;
  fake_set_destination("sentinel");
  fake_fault = fault;
  status = cupidld_publish_output_with_ops(
      "output.exe", replacement, ops);
  return status == CTOOL_ERR_IO && fake_destination_is("sentinel") != 0 &&
         fake_candidate_size == 0u && fake_discard_count == 1;
}

int main(void) {
  static const ctool_u8 replacement_data[] = "replacement";
  static const cupidld_publication_ops_t ops = {
      fake_open, fake_write, fake_close, fake_verify, fake_replace,
      fake_discard};
  ctool_bytes_t replacement;
  ctool_status_t status;
  replacement.data = replacement_data;
  replacement.size = (ctool_u32)(sizeof(replacement_data) - 1u);
  if (expect_failed_publication(FAULT_SHORT_WRITE, replacement, &ops) == 0 ||
      expect_failed_publication(FAULT_CLOSE, replacement, &ops) == 0 ||
      expect_failed_publication(FAULT_SUBSTITUTE, replacement, &ops) == 0 ||
      expect_failed_publication(FAULT_REPLACE, replacement, &ops) == 0) {
    return 1;
  }
  fake_set_destination("sentinel");
  fake_fault = FAULT_NONE;
  fake_open_failures = 2u;
  status = cupidld_publish_output_with_ops(
      "output.exe", replacement, &ops);
  if (status != CTOOL_OK || fake_destination_is("replacement") == 0 ||
      fake_candidate_size != 0u || fake_discard_count != 0) {
    return 1;
  }
  if (fake_open_count != 3u) {
    return 1;
  }
  fake_set_destination("sentinel");
  fake_open_failures = CUPIDLD_PUBLICATION_ATTEMPTS;
  status = cupidld_publish_output_with_ops(
      "output.exe", replacement, &ops);
  if (status != CTOOL_ERR_IO || fake_destination_is("sentinel") == 0 ||
      fake_open_count != CUPIDLD_PUBLICATION_ATTEMPTS ||
      fake_discard_count != 0) {
    return 1;
  }
  (void)puts("atomic-publication: ok");
  return 0;
}
''',
        encoding="utf-8",
    )
    command = [
        _host_compiler(),
        "-I",
        str(TOOLCHAIN_ROOT),
        "-std=c11",
        "-O2",
        "-pedantic",
        "-Werror",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wpointer-arith",
        "-Wcast-qual",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Wconversion",
        "-Wsign-conversion",
        "-x",
        "c",
        str(TOOLCHAIN_ROOT / "ctool.cc"),
        str(TOOLCHAIN_ROOT / "ctool_host.cc"),
        str(TOOLCHAIN_ROOT / "elf32.cc"),
        str(TOOLCHAIN_ROOT / "cupidld.cc"),
        str(source),
        "-o",
        str(output),
    ]
    result = subprocess.run(
        command, cwd=REPO_ROOT, text=True, capture_output=True
    )
    if result.returncode != 0:
        raise AssertionError(
            "CupidLD publication harness build failed\n"
            + result.stdout
            + result.stderr
        )
    return output


def _compile_i386(source: Path, output: Path):
    compiler = shutil.which("clang") or shutil.which("gcc")
    if compiler is None:
        raise unittest.SkipTest("Clang or GCC is required for the ELF32 fixture")
    command = [compiler]
    if "clang" in Path(compiler).name.lower():
        command.append("--target=i386-unknown-elf")
    else:
        command.append("-m32")
    command += ["-ffreestanding", "-fno-pie", "-fno-pic", "-c", str(source), "-o", str(output)]
    result = subprocess.run(
        command, cwd=REPO_ROOT, text=True, capture_output=True
    )
    if result.returncode != 0:
        raise AssertionError(
            "ELF32 fixture compilation failed\n" + result.stdout + result.stderr
        )


def _elf_header_and_sections(path: Path):
    image = path.read_bytes()
    if image[:7] != b"\x7fELF\x01\x01\x01":
        raise AssertionError("linked output is not little-endian ELF32")
    file_type, machine = struct.unpack_from("<HH", image, 16)
    entry = struct.unpack_from("<I", image, 24)[0]
    section_table = struct.unpack_from("<I", image, 32)[0]
    section_size, section_count, names_index = struct.unpack_from(
        "<HHH", image, 46
    )
    headers = [
        struct.unpack_from("<IIIIIIIIII", image, section_table + i * section_size)
        for i in range(section_count)
    ]
    names_header = headers[names_index]
    names = image[names_header[4] : names_header[4] + names_header[5]]

    def string_at(offset):
        if offset == 0:
            return ""
        end = names.index(0, offset)
        return names[offset:end].decode("ascii")

    sections = {
        string_at(header[0]): {
            "type": header[1],
            "flags": header[2],
            "address": header[3],
            "offset": header[4],
            "size": header[5],
        }
        for header in headers
    }
    return image, file_type, machine, entry, sections


_CANONICAL_DOS_STUB = bytes.fromhex(
    "4d5a90000300000004000000ffff0000"
    "b8000000000000004000000000000000"
    "00000000000000000000000000000000"
    "00000000000000000000000080000000"
    "0e1fba0e00b409cd21b8014ccd215468"
    "69732070726f6772616d2063616e6e6f"
    "742062652072756e20696e20444f5320"
    "6d6f64652e0d0d0a2400000000000000"
)


def _pe32_header_and_sections(path: Path):
    image = path.read_bytes()

    def require_range(offset, size, label):
        if offset < 0 or size < 0 or offset > len(image) - size:
            raise AssertionError(f"{label} is outside the PE image")

    def read_u16(offset, label):
        require_range(offset, 2, label)
        return struct.unpack_from("<H", image, offset)[0]

    def read_u32(offset, label):
        require_range(offset, 4, label)
        return struct.unpack_from("<I", image, offset)[0]

    require_range(0, 64, "DOS header")
    if image[0:2] != b"MZ":
        raise AssertionError("linked output has no DOS MZ signature")
    pe_offset = read_u32(0x3C, "DOS PE offset")
    require_range(pe_offset, 24, "PE signature and COFF header")
    if image[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise AssertionError("linked output has no PE signature")

    coff_values = struct.unpack_from("<HHIIIHH", image, pe_offset + 4)
    coff = {
        "machine": coff_values[0],
        "section_count": coff_values[1],
        "timestamp": coff_values[2],
        "symbol_table": coff_values[3],
        "symbol_count": coff_values[4],
        "optional_size": coff_values[5],
        "characteristics": coff_values[6],
    }
    optional_offset = pe_offset + 24
    require_range(optional_offset, coff["optional_size"], "optional header")
    optional = {
        "magic": read_u16(optional_offset, "optional-header magic"),
        "linker_major": image[optional_offset + 2],
        "linker_minor": image[optional_offset + 3],
        "code_size": read_u32(optional_offset + 4, "code size"),
        "initialized_size": read_u32(
            optional_offset + 8, "initialized-data size"
        ),
        "uninitialized_size": read_u32(
            optional_offset + 12, "uninitialized-data size"
        ),
        "entry_rva": read_u32(optional_offset + 16, "entry RVA"),
        "code_rva": read_u32(optional_offset + 20, "code RVA"),
        "data_rva": read_u32(optional_offset + 24, "data RVA"),
        "image_base": read_u32(optional_offset + 28, "image base"),
        "section_alignment": read_u32(
            optional_offset + 32, "section alignment"
        ),
        "file_alignment": read_u32(
            optional_offset + 36, "file alignment"
        ),
        "os_version": (
            read_u16(optional_offset + 40, "major OS version"),
            read_u16(optional_offset + 42, "minor OS version"),
        ),
        "image_version": (
            read_u16(optional_offset + 44, "major image version"),
            read_u16(optional_offset + 46, "minor image version"),
        ),
        "subsystem_version": (
            read_u16(optional_offset + 48, "major subsystem version"),
            read_u16(optional_offset + 50, "minor subsystem version"),
        ),
        "win32_version": read_u32(
            optional_offset + 52, "Win32 version"
        ),
        "image_size": read_u32(optional_offset + 56, "image size"),
        "headers_size": read_u32(optional_offset + 60, "headers size"),
        "checksum": read_u32(optional_offset + 64, "checksum"),
        "subsystem": read_u16(optional_offset + 68, "subsystem"),
        "dll_characteristics": read_u16(
            optional_offset + 70, "DLL characteristics"
        ),
        "stack_reserve": read_u32(
            optional_offset + 72, "stack reserve"
        ),
        "stack_commit": read_u32(optional_offset + 76, "stack commit"),
        "heap_reserve": read_u32(optional_offset + 80, "heap reserve"),
        "heap_commit": read_u32(optional_offset + 84, "heap commit"),
        "loader_flags": read_u32(optional_offset + 88, "loader flags"),
        "directory_count": read_u32(
            optional_offset + 92, "data-directory count"
        ),
    }
    directory_offset = optional_offset + 96
    if optional["directory_count"] > 16:
        raise AssertionError("PE32 data-directory count exceeds the header")
    directories = []
    for index in range(optional["directory_count"]):
        offset = directory_offset + index * 8
        require_range(offset, 8, f"data directory {index}")
        directories.append(struct.unpack_from("<II", image, offset))

    section_offset = optional_offset + coff["optional_size"]
    require_range(
        section_offset,
        coff["section_count"] * 40,
        "section table",
    )
    sections = {}
    section_order = []
    for index in range(coff["section_count"]):
        offset = section_offset + index * 40
        name_bytes = image[offset : offset + 8]
        try:
            name = name_bytes.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as error:
            raise AssertionError(
                f"section {index} name is not ASCII"
            ) from error
        values = struct.unpack_from("<IIIIIIHHI", image, offset + 8)
        section = {
            "virtual_size": values[0],
            "rva": values[1],
            "raw_size": values[2],
            "raw_offset": values[3],
            "relocation_offset": values[4],
            "line_offset": values[5],
            "relocation_count": values[6],
            "line_count": values[7],
            "characteristics": values[8],
        }
        if name in sections:
            raise AssertionError(f"duplicate PE section name: {name}")
        if section["raw_size"]:
            require_range(
                section["raw_offset"],
                section["raw_size"],
                f"{name} raw payload",
            )
        sections[name] = section
        section_order.append(name)

    return image, pe_offset, coff, optional, directories, section_order, sections


class CupidLdHostedCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidld-cli-build-", dir=TOOLCHAIN_ROOT
        )
        cls._fixture_directory = tempfile.TemporaryDirectory(
            prefix=".cupidld-cli-fixture-", dir=TOOLCHAIN_ROOT
        )
        cls.cli = _build_cli(Path(cls._build_directory.name))
        cls.publication_harness = _build_publication_harness(
            Path(cls._build_directory.name)
        )
        cls.fixture_root = Path(cls._fixture_directory.name)
        cls.source = cls.fixture_root / "entry.s"
        cls.object = cls.fixture_root / "entry.o"
        cls.helper_source = cls.fixture_root / "helper.s"
        cls.helper_object = cls.fixture_root / "helper.o"
        cls.pe_source = cls.fixture_root / "pe_entry.s"
        cls.pe_object = cls.fixture_root / "pe_entry.o"
        cls.pe_empty_middle_source = cls.fixture_root / "pe_empty_middle.s"
        cls.pe_empty_middle_object = cls.fixture_root / "pe_empty_middle.o"
        cls.pe_wx_source = cls.fixture_root / "pe_wx_entry.s"
        cls.pe_wx_object = cls.fixture_root / "pe_wx_entry.o"
        cls.pe_data_entry_source = cls.fixture_root / "pe_data_entry.s"
        cls.pe_data_entry_object = cls.fixture_root / "pe_data_entry.o"
        cls.pe_import_source = cls.fixture_root / "pe_import_entry.s"
        cls.pe_import_object = cls.fixture_root / "pe_import_entry.o"
        cls.pe_direct_import_source = cls.fixture_root / "pe_direct_import.s"
        cls.pe_direct_import_object = cls.fixture_root / "pe_direct_import.o"
        cls.pe_nonzero_import_source = (
            cls.fixture_root / "pe_nonzero_import.s"
        )
        cls.pe_nonzero_import_object = (
            cls.fixture_root / "pe_nonzero_import.o"
        )
        cls.pe_multi_import_source = cls.fixture_root / "pe_multi_import.s"
        cls.pe_multi_import_object = cls.fixture_root / "pe_multi_import.o"
        cls.oversize_source = cls.fixture_root / "oversize.s"
        cls.oversize_object = cls.fixture_root / "oversize.o"
        cls.source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            "_start:\n"
            "  movl $message, %eax\n"
            "  call helper\n"
            "  ret\n"
            ".size _start, .-_start\n"
            '.section .rodata,"a",@progbits\n'
            "message:\n"
            "  .long 0x12345678\n",
            encoding="utf-8",
        )
        _compile_i386(cls.source, cls.object)
        cls.helper_source.write_text(
            '.section .text,"ax",@progbits\n'
            ".globl helper\n"
            ".type helper,@function\n"
            "helper:\n"
            "  ret\n"
            ".size helper, .-helper\n",
            encoding="utf-8",
        )
        _compile_i386(cls.helper_source, cls.helper_object)
        cls.pe_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            "_start:\n"
            "  movl $message, %eax\n"
            "  movl $writable_value, %edx\n"
            "  movl $scratch, %ecx\n"
            "  call helper\n"
            "  ret\n"
            ".size _start, .-_start\n"
            '.section .rodata,"a",@progbits\n'
            "  .balign 4\n"
            "message:\n"
            "  .long 0x12345678\n"
            '.section .data,"aw",@progbits\n'
            "  .balign 4\n"
            "writable_value:\n"
            "  .long 0x89abcdef\n"
            '.section .bss,"aw",@nobits\n'
            "  .balign 16\n"
            "scratch:\n"
            "  .skip 16\n",
            encoding="utf-8",
        )
        _compile_i386(cls.pe_source, cls.pe_object)
        cls.pe_empty_middle_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            "_start:\n"
            "  movl $middle_value, %eax\n"
            "  ret\n"
            ".size _start, .-_start\n"
            '.section .rodata,"a",@progbits\n'
            '.section .data,"aw",@progbits\n'
            ".globl middle_value\n"
            ".type middle_value,@object\n"
            "middle_value:\n"
            "  .long 0x12345678\n"
            ".size middle_value, .-middle_value\n",
            encoding="utf-8",
        )
        _compile_i386(cls.pe_empty_middle_source, cls.pe_empty_middle_object)
        cls.pe_wx_source.write_text(
            '.section .text.start,"awx",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            "_start:\n"
            "  ret\n"
            ".size _start, .-_start\n",
            encoding="utf-8",
        )
        _compile_i386(cls.pe_wx_source, cls.pe_wx_object)
        cls.pe_data_entry_source.write_text(
            '.section .data,"aw",@progbits\n'
            ".globl _start\n"
            ".type _start,@object\n"
            "_start:\n"
            "  .long 0\n"
            ".size _start, .-_start\n",
            encoding="utf-8",
        )
        _compile_i386(cls.pe_data_entry_source, cls.pe_data_entry_object)
        cls.pe_import_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            ".extern __imp_ExitProcess\n"
            ".extern __imp_GetStdHandle\n"
            ".extern __imp_WriteFile\n"
            "_start:\n"
            "  pushl $-11\n"
            "  call *__imp_GetStdHandle\n"
            "  movl %eax, %ebx\n"
            "  pushl $0\n"
            "  pushl $written\n"
            "  pushl $message_end-message\n"
            "  pushl $message\n"
            "  pushl %ebx\n"
            "  call *__imp_WriteFile\n"
            "  pushl $37\n"
            "  call *__imp_ExitProcess\n"
            "  hlt\n"
            ".size _start, .-_start\n"
            '.section .rodata,"a",@progbits\n'
            "message:\n"
            '.ascii "Cupid PE32 import runtime: ok\\n"\n'
            "message_end:\n"
            '.section .bss,"aw",@nobits\n'
            "  .balign 4\n"
            "written:\n"
            "  .skip 4\n",
            encoding="utf-8",
        )
        _compile_i386(cls.pe_import_source, cls.pe_import_object)
        cls.pe_direct_import_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            ".extern __imp_ExitProcess\n"
            "_start:\n"
            "  call __imp_ExitProcess\n"
            "  ret\n"
            ".size _start, .-_start\n",
            encoding="utf-8",
        )
        _compile_i386(
            cls.pe_direct_import_source, cls.pe_direct_import_object
        )
        cls.pe_nonzero_import_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            ".extern __imp_ExitProcess\n"
            "_start:\n"
            "  ret\n"
            ".size _start, .-_start\n"
            '.section .data,"aw",@progbits\n'
            "  .long __imp_ExitProcess+4\n",
            encoding="utf-8",
        )
        _compile_i386(
            cls.pe_nonzero_import_source, cls.pe_nonzero_import_object
        )
        cls.pe_multi_import_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            ".type _start,@function\n"
            ".extern __imp_ExitProcess\n"
            ".extern __imp_MessageBoxA\n"
            "_start:\n"
            "  ret\n"
            ".size _start, .-_start\n"
            '.section .rodata,"a",@progbits\n'
            "  .byte 0x5a\n"
            '.section .data,"aw",@progbits\n'
            "  .long __imp_MessageBoxA\n"
            "  .long __imp_ExitProcess\n"
            '.section .bss,"aw",@nobits\n'
            "  .balign 4\n"
            "  .skip 4\n",
            encoding="utf-8",
        )
        _compile_i386(
            cls.pe_multi_import_source, cls.pe_multi_import_object
        )
        cls.oversize_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            "_start:\n"
            "  ret\n"
            '.section .bss,"aw",@nobits\n'
            "  .balign 4096\n"
            "  .skip 0x00e00000\n",
            encoding="utf-8",
        )
        _compile_i386(cls.oversize_source, cls.oversize_object)
        cls.script = cls.fixture_root / "small.ld"
        cls.script.write_bytes((REPO_ROOT / "link.ld").read_bytes())
        cls.work = cls.fixture_root / "work"
        cls.work.mkdir()

    @classmethod
    def tearDownClass(cls):
        cls._fixture_directory.cleanup()
        cls._build_directory.cleanup()

    def test_script_profile_links_relative_paths_and_applies_relocation(self):
        output = self.fixture_root / "script.elf"
        result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_i386",
                "-T",
                "../small.ld",
                "-o",
                "../script.elf",
                "../entry.o",
                str(self.helper_object.resolve()),
            ],
            cwd=self.work,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        image, file_type, machine, entry, sections = _elf_header_and_sections(output)
        self.assertEqual((file_type, machine, entry), (2, 3, 0x00100000))
        self.assertEqual(sections[".text"]["address"], 0x00100000)
        self.assertGreater(sections[".rodata"]["address"], 0x00100000)
        text = sections[".text"]
        immediate = struct.unpack_from("<I", image, text["offset"] + 1)[0]
        self.assertEqual(immediate, sections[".rodata"]["address"])

    def test_fixed_text_profile_links_absolute_paths_at_requested_address(self):
        output = self.fixture_root / "fixed.elf"
        result = subprocess.run(
            [
                str(self.cli),
                "-m=elf_i386",
                "--text-address",
                "0x00d00000",
                "--entry=_start",
                "-o",
                str(output.resolve()),
                str(self.object.resolve()),
                str(self.helper_object.resolve()),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        _, file_type, machine, entry, sections = _elf_header_and_sections(output)
        self.assertEqual((file_type, machine, entry), (2, 3, 0x00D00000))
        self.assertEqual(sections[".text"]["address"], 0x00D00000)
        self.assertGreater(sections[".rodata"]["address"], 0x00D00000)

    def test_i386pe_writes_one_deterministic_import_free_fixed_image(self):
        first = self.fixture_root / "fixed-first.exe"
        second = self.fixture_root / "fixed-second.exe"

        def link(output):
            return subprocess.run(
                [
                    str(self.cli),
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "-o",
                    str(output),
                    str(self.pe_object),
                    str(self.helper_object),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        first.write_bytes(b"sentinel")
        first_result = link(first)
        second_result = link(second)
        self.assertEqual(first_result.returncode, 0, first_result.stderr)
        self.assertEqual(second_result.returncode, 0, second_result.stderr)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        if os.name != "nt":
            self.assertNotEqual(first.stat().st_mode & stat.S_IXUSR, 0)
            self.assertNotEqual(second.stat().st_mode & stat.S_IXUSR, 0)
        self.assertEqual(
            list(self.fixture_root.glob("*.cupid-tmp-*")), []
        )

        (
            image,
            pe_offset,
            coff,
            optional,
            directories,
            section_order,
            sections,
        ) = _pe32_header_and_sections(first)
        self.assertEqual(image[:0x80], _CANONICAL_DOS_STUB)
        self.assertEqual(pe_offset, 0x80)
        self.assertEqual(
            coff,
            {
                "machine": 0x014C,
                "section_count": 4,
                "timestamp": 0,
                "symbol_table": 0,
                "symbol_count": 0,
                "optional_size": 0x00E0,
                "characteristics": 0x0103,
            },
        )
        self.assertEqual(
            optional,
            {
                "magic": 0x010B,
                "linker_major": 0,
                "linker_minor": 0,
                "code_size": 0x0200,
                "initialized_size": 0x0400,
                "uninitialized_size": 16,
                "entry_rva": 0x1000,
                "code_rva": 0x1000,
                "data_rva": 0x2000,
                "image_base": 0x00400000,
                "section_alignment": 0x1000,
                "file_alignment": 0x0200,
                "os_version": (6, 0),
                "image_version": (0, 0),
                "subsystem_version": (6, 0),
                "win32_version": 0,
                "image_size": 0x5000,
                "headers_size": 0x0400,
                "checksum": 0,
                "subsystem": 3,
                "dll_characteristics": 0x0100,
                "stack_reserve": 0x00100000,
                "stack_commit": 0x00100000,
                "heap_reserve": 0x00100000,
                "heap_commit": 0x00001000,
                "loader_flags": 0,
                "directory_count": 16,
            },
        )
        self.assertEqual(directories, [(0, 0)] * 16)
        self.assertEqual(
            section_order,
            [".text", ".rodata", ".data", ".bss"],
        )
        expected_sections = {
            ".text": (25, 0x1000, 0x0200, 0x0400, 0x60000020),
            ".rodata": (4, 0x2000, 0x0200, 0x0600, 0x40000040),
            ".data": (4, 0x3000, 0x0200, 0x0800, 0xC0000040),
            ".bss": (16, 0x4000, 0, 0, 0xC0000080),
        }
        for name, expected in expected_sections.items():
            section = sections[name]
            self.assertEqual(
                (
                    section["virtual_size"],
                    section["rva"],
                    section["raw_size"],
                    section["raw_offset"],
                    section["characteristics"],
                ),
                expected,
            )
            self.assertEqual(section["relocation_offset"], 0)
            self.assertEqual(section["line_offset"], 0)
            self.assertEqual(section["relocation_count"], 0)
            self.assertEqual(section["line_count"], 0)

        text = sections[".text"]
        text_offset = text["raw_offset"]
        self.assertEqual(image[text_offset], 0xB8)
        self.assertEqual(
            struct.unpack_from("<I", image, text_offset + 1)[0],
            0x00402000,
        )
        self.assertEqual(image[text_offset + 5], 0xBA)
        self.assertEqual(
            struct.unpack_from("<I", image, text_offset + 6)[0],
            0x00403000,
        )
        self.assertEqual(image[text_offset + 10], 0xB9)
        self.assertEqual(
            struct.unpack_from("<I", image, text_offset + 11)[0],
            0x00404000,
        )
        for name in (".text", ".rodata", ".data"):
            section = sections[name]
            padding = image[
                section["raw_offset"] + section["virtual_size"] :
                section["raw_offset"] + section["raw_size"]
            ]
            self.assertEqual(padding, b"\0" * len(padding))
        self.assertEqual(len(image), 0x0A00)

    def test_i386pe_builds_canonical_imports_and_runs_the_image(self):
        first = self.fixture_root / "import-first.exe"
        second = self.fixture_root / "import-second.exe"
        import_arguments = [
            "--import",
            "__imp_WriteFile=KERNEL32.dll:WriteFile",
            "--import",
            "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
            "--import",
            "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
        ]

        def link(output, arguments):
            return subprocess.run(
                [
                    str(self.cli),
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    *arguments,
                    "-o",
                    str(output),
                    str(self.pe_import_object),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        first_result = link(first, import_arguments)
        second_result = link(
            second,
            import_arguments[4:] + import_arguments[:4],
        )
        self.assertEqual(first_result.returncode, 0, first_result.stderr)
        self.assertEqual(second_result.returncode, 0, second_result.stderr)
        self.assertEqual(first.read_bytes(), second.read_bytes())

        image, _, coff, optional, directories, section_order, sections = (
            _pe32_header_and_sections(first)
        )
        self.assertEqual(coff["section_count"], 4)
        self.assertEqual(section_order, [".text", ".rodata", ".bss", ".idata"])
        self.assertEqual(optional["image_size"], 0x5000)
        self.assertEqual(directories[1], (0x4000, 0x28))
        self.assertEqual(directories[12], (0x4038, 0x10))
        idata = sections[".idata"]
        self.assertEqual(idata["rva"], 0x4000)
        self.assertEqual(idata["virtual_size"], 0x80)
        self.assertEqual(idata["characteristics"], 0xC0000040)
        payload = idata["raw_offset"]
        self.assertEqual(
            struct.unpack_from("<IIIII", image, payload),
            (0x4028, 0, 0, 0x4048, 0x4038),
        )
        self.assertEqual(image[payload + 20 : payload + 40], b"\0" * 20)
        expected_thunks = (0x4056, 0x4064, 0x4074, 0)
        self.assertEqual(struct.unpack_from("<IIII", image, payload + 0x28), expected_thunks)
        self.assertEqual(struct.unpack_from("<IIII", image, payload + 0x38), expected_thunks)
        self.assertEqual(image[payload + 0x48 : payload + 0x55], b"KERNEL32.dll\0")
        self.assertEqual(image[payload + 0x56 : payload + 0x64], b"\0\0ExitProcess\0")
        self.assertEqual(image[payload + 0x64 : payload + 0x73], b"\0\0GetStdHandle\0")
        self.assertEqual(image[payload + 0x74 : payload + 0x80], b"\0\0WriteFile\0")

        if os.name == "nt":
            native = subprocess.run(
                [str(first)], cwd=REPO_ROOT, capture_output=True, timeout=10
            )
            self.assertEqual(native.returncode, 37, native.stderr)
            self.assertEqual(native.stderr, b"")
            self.assertEqual(
                native.stdout, b"Cupid PE32 import runtime: ok\n"
            )

    def test_i386pe_rejects_invalid_import_contracts_without_publication(self):
        base = [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
        ]
        cases = (
            (
                "malformed import selector",
                base + ["--import", "missing-separators"],
                "usage:",
            ),
            (
                "imports are PE32 only",
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    "__imp_WriteFile=KERNEL32.dll:WriteFile",
                ],
                "usage:",
            ),
            (
                "unsafe library name",
                base
                + [
                    "--import",
                    "__imp_WriteFile=KERNEL32/evil.dll:WriteFile",
                ],
                "import names are invalid",
            ),
            (
                "duplicate IAT symbol",
                base
                + [
                    "--import",
                    "__imp_WriteFile=KERNEL32.dll:WriteFile",
                    "--import",
                    "__imp_WriteFile=KERNEL32.dll:ExitProcess",
                ],
                "same IAT symbol twice",
            ),
            (
                "nonadjacent duplicate IAT symbol",
                base
                + [
                    "--import",
                    "__imp_WriteFile=ADVAPI32.dll:RegCloseKey",
                    "--import",
                    "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
                    "--import",
                    "__imp_WriteFile=USER32.dll:MessageBoxA",
                ],
                "same IAT symbol twice",
            ),
            (
                "duplicate imported procedure",
                base
                + [
                    "--import",
                    "__imp_WriteFile=KERNEL32.dll:WriteFile",
                    "--import",
                    "__imp_GetStdHandle=KERNEL32.dll:WriteFile",
                ],
                "same procedure twice",
            ),
            (
                "inconsistent library spelling",
                base
                + [
                    "--import",
                    "__imp_WriteFile=KERNEL32.dll:WriteFile",
                    "--import",
                    "__imp_GetStdHandle=kernel32.DLL:GetStdHandle",
                ],
                "inconsistent library spelling",
            ),
            (
                "unused import symbol",
                base
                + [
                    "--import",
                    "__imp_Missing=KERNEL32.dll:WriteFile",
                ],
                "does not match an undefined symbol",
            ),
            (
                "already defined import symbol",
                base
                + ["--import", "_start=KERNEL32.dll:ExitProcess"],
                "unused or already defined",
            ),
        )
        for index, (label, selector, message) in enumerate(cases):
            with self.subTest(label=label):
                output = self.fixture_root / f"bad-import-{index}.exe"
                output.write_bytes(b"sentinel")
                result = subprocess.run(
                    [
                        str(self.cli),
                        *selector,
                        "-o",
                        str(output),
                        str(self.pe_import_object),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertEqual(output.read_bytes(), b"sentinel")
                self.assertEqual(
                    list(output.parent.glob(output.name + ".cupid-tmp-*")),
                    [],
                )

    def test_i386pe_canonicalizes_two_libraries_with_all_five_sections(self):
        output = self.fixture_root / "multi-library.exe"
        result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
                "--import",
                "__imp_MessageBoxA=USER32.dll:MessageBoxA",
                "--import",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "-o",
                str(output),
                str(self.pe_multi_import_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        image, _, coff, optional, directories, section_order, sections = (
            _pe32_header_and_sections(output)
        )
        self.assertEqual(coff["section_count"], 5)
        self.assertEqual(
            section_order,
            [".text", ".rodata", ".data", ".bss", ".idata"],
        )
        self.assertEqual(optional["image_size"], 0x6000)
        self.assertEqual(directories[1], (0x5000, 0x3C))
        self.assertEqual(directories[12], (0x504C, 0x10))
        self.assertEqual(
            image[sections[".data"]["raw_offset"] :
                  sections[".data"]["raw_offset"] + 8],
            struct.pack("<II", 0x00405054, 0x0040504C),
        )
        payload = sections[".idata"]["raw_offset"]
        self.assertEqual(
            struct.unpack_from("<IIIII", image, payload),
            (0x503C, 0, 0, 0x505C, 0x504C),
        )
        self.assertEqual(
            struct.unpack_from("<IIIII", image, payload + 20),
            (0x5044, 0, 0, 0x5069, 0x5054),
        )
        self.assertEqual(image[payload + 40 : payload + 60], b"\0" * 20)
        self.assertEqual(
            struct.unpack_from("<IIIIIIII", image, payload + 0x3C),
            (0x5074, 0, 0x5082, 0, 0x5074, 0, 0x5082, 0),
        )
        self.assertEqual(
            image[payload + 0x5C : payload + 0x69], b"KERNEL32.dll\0"
        )
        self.assertEqual(
            image[payload + 0x69 : payload + 0x74], b"USER32.dll\0"
        )
        self.assertEqual(
            image[payload + 0x74 : payload + 0x82],
            b"\0\0ExitProcess\0",
        )
        self.assertEqual(
            image[payload + 0x82 : payload + 0x90],
            b"\0\0MessageBoxA\0",
        )

    def test_i386pe_rejects_a_direct_call_to_an_iat_slot(self):
        output = self.fixture_root / "direct-import.exe"
        output.write_bytes(b"sentinel")
        result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
                "--import",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "-o",
                str(output),
                str(self.pe_direct_import_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "IAT symbols require an absolute zero-addend relocation",
            result.stderr,
        )
        self.assertEqual(output.read_bytes(), b"sentinel")

    def test_i386pe_rejects_a_nonzero_iat_addend(self):
        output = self.fixture_root / "nonzero-import.exe"
        output.write_bytes(b"sentinel")
        result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
                "--import",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "-o",
                str(output),
                str(self.pe_nonzero_import_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "IAT symbols require an absolute zero-addend relocation",
            result.stderr,
        )
        self.assertEqual(output.read_bytes(), b"sentinel")
        self.assertEqual(
            list(output.parent.glob(output.name + ".cupid-tmp-*")), []
        )

    def test_i386pe_omits_empty_sections_without_reusing_an_rva(self):
        elf_output = self.fixture_root / "empty-middle.elf"
        elf_result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_i386",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
                "-o",
                str(elf_output),
                str(self.pe_empty_middle_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(elf_result.returncode, 0, elf_result.stderr)
        _, _, _, _, elf_sections = _elf_header_and_sections(elf_output)
        self.assertEqual(elf_sections[".rodata"]["size"], 0)
        self.assertEqual(
            elf_sections[".rodata"]["address"],
            elf_sections[".data"]["address"],
        )

        output = self.fixture_root / "empty-middle.exe"
        output.write_bytes(b"sentinel")
        result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
                "-o",
                str(output),
                str(self.pe_empty_middle_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        (
            image,
            _,
            coff,
            optional,
            _,
            section_order,
            sections,
        ) = _pe32_header_and_sections(output)
        self.assertEqual(
            len({section["rva"] for section in sections.values()}),
            len(sections),
            "PE section headers must not reuse an RVA",
        )
        self.assertEqual(coff["section_count"], 2)
        self.assertEqual(section_order, [".text", ".data"])
        self.assertEqual(
            {
                name: (
                    section["virtual_size"],
                    section["rva"],
                    section["raw_size"],
                    section["raw_offset"],
                    section["characteristics"],
                )
                for name, section in sections.items()
            },
            {
                ".text": (6, 0x1000, 0x0200, 0x0200, 0x60000020),
                ".data": (4, 0x2000, 0x0200, 0x0400, 0xC0000040),
            },
        )
        self.assertEqual(optional["code_size"], 0x0200)
        self.assertEqual(optional["initialized_size"], 0x0200)
        self.assertEqual(optional["uninitialized_size"], 0)
        self.assertEqual(optional["entry_rva"], 0x1000)
        self.assertEqual(optional["code_rva"], 0x1000)
        self.assertEqual(optional["data_rva"], 0x2000)
        self.assertEqual(optional["image_size"], 0x3000)
        self.assertEqual(optional["headers_size"], 0x0200)
        text = sections[".text"]
        self.assertEqual(image[text["raw_offset"]], 0xB8)
        self.assertEqual(
            struct.unpack_from("<I", image, text["raw_offset"] + 1)[0],
            0x00402000,
        )
        data = sections[".data"]
        self.assertEqual(
            image[data["raw_offset"] : data["raw_offset"] + 4],
            bytes.fromhex("78563412"),
        )
        self.assertEqual(len(image), 0x0600)
        self.assertEqual(
            list(output.parent.glob(output.name + ".cupid-tmp-*")),
            [],
        )

    def test_i386pe_reports_selector_mistakes_without_touching_output(self):
        cases = (
            (
                "linker scripts are not a PE fixed-layout selector",
                [
                    "-m",
                    "i386pe",
                    "-T",
                    str(self.script),
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                ],
            ),
            (
                "the text address is required",
                ["-m", "i386pe", "--entry", "_start"],
            ),
            (
                "the entry symbol is required",
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                ],
            ),
        )
        for index, (label, selector) in enumerate(cases):
            with self.subTest(label=label):
                output = self.fixture_root / f"pe-usage-{index}.exe"
                output.write_bytes(b"sentinel")
                result = subprocess.run(
                    [
                        str(self.cli),
                        *selector,
                        "-o",
                        str(output),
                        str(self.pe_object),
                        str(self.helper_object),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 2)
                self.assertIn("usage: cupidld", result.stderr)
                self.assertIn("i386pe", result.stderr)
                self.assertEqual(output.read_bytes(), b"sentinel")

    def test_atomic_publication_preserves_output_and_recovers(self):
        result = subprocess.run(
            [str(self.publication_harness)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "atomic-publication: ok\n")

    def test_i386pe_link_failures_keep_the_previous_executable(self):
        malformed = self.fixture_root / "malformed-pe-input.o"
        malformed.write_bytes(b"\x7fELF")
        cases = (
            (
                "a noncanonical image base",
                "0x00402000",
                self.pe_object,
                ("PE32", "0x00401000"),
            ),
            (
                "a malformed object",
                "0x00401000",
                malformed,
                ("ELF32 header is truncated",),
            ),
            (
                "writable executable code",
                "0x00401000",
                self.pe_wx_object,
                ("PE32 rejects writable executable sections",),
            ),
            (
                "entry outside file-backed executable code",
                "0x00401000",
                self.pe_data_entry_object,
                ("CupidLD entry is not file-backed executable code",),
            ),
        )
        for index, (label, address, source, messages) in enumerate(cases):
            with self.subTest(label=label):
                output = self.fixture_root / f"pe-failure-{index}.exe"
                output.write_bytes(b"sentinel")
                result = subprocess.run(
                    [
                        str(self.cli),
                        "-m",
                        "i386pe",
                        "--text-address",
                        address,
                        "--entry",
                        "_start",
                        "-o",
                        str(output),
                        str(source),
                        str(self.helper_object),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 1)
                for message in messages:
                    self.assertIn(message, result.stderr)
                self.assertEqual(output.read_bytes(), b"sentinel")
                self.assertEqual(
                    list(output.parent.glob(output.name + ".cupid-tmp-*")),
                    [],
                )

    def test_production_script_rejects_an_image_that_reaches_the_kernel_stack(self):
        output = self.fixture_root / "stack-overlap.elf"
        output.write_bytes(b"sentinel")
        result = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_i386",
                "-T",
                str(self.script),
                "-o",
                str(output),
                str(self.oversize_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn(
            "Kernel memory image overlaps the fixed kernel stack", result.stderr
        )
        self.assertEqual(output.read_bytes(), b"sentinel")

    def test_usage_and_link_failures_have_distinct_status_and_preserve_output(self):
        invalid = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_x86_64",
                "--text-address",
                "0x600000",
                "--entry",
                "_start",
                "-o",
                "bad.elf",
                str(self.object),
                str(self.helper_object),
            ],
            cwd=self.fixture_root,
            text=True,
            capture_output=True,
        )
        self.assertEqual(invalid.returncode, 2)
        self.assertIn("usage: cupidld", invalid.stderr)

        output = self.fixture_root / "preserve.elf"
        output.write_bytes(b"sentinel")
        malformed = self.fixture_root / "malformed.o"
        malformed.write_bytes(b"\x7fELF")
        failed = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_i386",
                "--text-address",
                "0x00600000",
                "--entry",
                "_start",
                "-o",
                str(output),
                str(malformed),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(failed.returncode, 1)
        self.assertIn("ELF32 header is truncated", failed.stderr)
        self.assertEqual(output.read_bytes(), b"sentinel")

        missing = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_i386",
                "-T",
                str(self.script),
                "-o",
                str(output),
                "missing.o",
            ],
            cwd=self.fixture_root,
            text=True,
            capture_output=True,
        )
        self.assertEqual(missing.returncode, 1)
        self.assertIn("cannot load", missing.stderr)
        self.assertEqual(output.read_bytes(), b"sentinel")

    def test_host_readelf_accepts_linked_executable_when_available(self):
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if readelf is None:
            self.skipTest("host readelf oracle is not installed")
        output = self.fixture_root / "oracle.elf"
        linked = subprocess.run(
            [
                str(self.cli),
                "-m",
                "elf_i386",
                "--text-address=0x00600000",
                "--entry",
                "_start",
                "-o",
                str(output),
                str(self.object),
                str(self.helper_object),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(linked.returncode, 0, linked.stderr)
        report = subprocess.run(
            [readelf, "-h", "-l", "-S", "-s", "-W", str(output)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(report.returncode, 0, report.stderr)
        self.assertIn("EXEC (Executable file)", report.stdout)
        self.assertIn("_start", report.stdout)
        self.assertIn("LOAD", report.stdout)


if __name__ == "__main__":
    unittest.main()

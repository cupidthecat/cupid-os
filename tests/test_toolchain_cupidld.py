import os
import shutil
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
        str(TOOLCHAIN_ROOT / "ctool.c"),
        str(TOOLCHAIN_ROOT / "ctool_host.c"),
        str(TOOLCHAIN_ROOT / "elf32.c"),
        str(TOOLCHAIN_ROOT / "cupidld.c"),
        str(TOOLCHAIN_ROOT / "cupidld_main.c"),
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
        cls.fixture_root = Path(cls._fixture_directory.name)
        cls.source = cls.fixture_root / "entry.s"
        cls.object = cls.fixture_root / "entry.o"
        cls.helper_source = cls.fixture_root / "helper.s"
        cls.helper_object = cls.fixture_root / "helper.o"
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
        cls.oversize_source.write_text(
            '.section .text.start,"ax",@progbits\n'
            ".globl _start\n"
            "_start:\n"
            "  ret\n"
            '.section .bss,"aw",@nobits\n'
            "  .balign 4096\n"
            "  .skip 0x00b00000\n",
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

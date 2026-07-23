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


def _build_cli(build: Path, name: str, sources):
    suffix = ".exe" if os.name == "nt" else ""
    output = build / (name + suffix)
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
    ]
    command += [str(TOOLCHAIN_ROOT / source) for source in sources]
    command += ["-o", str(output)]
    result = subprocess.run(
        command, cwd=REPO_ROOT, text=True, capture_output=True
    )
    if result.returncode != 0:
        raise AssertionError(
            f"{name} hosted build failed\n{result.stdout}{result.stderr}"
        )
    return output


def _elf32_sections_and_symbols(path: Path):
    image = path.read_bytes()
    if image[:7] != b"\x7fELF\x01\x01\x01":
        raise AssertionError("output is not little-endian ELF32")
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

    def string_at(table, offset):
        end = table.index(0, offset)
        return table[offset:end].decode("ascii")

    sections = {}
    for index, header in enumerate(headers):
        name = string_at(names, header[0]) if header[0] else ""
        sections[name] = {
            "index": index,
            "type": header[1],
            "flags": header[2],
            "offset": header[4],
            "size": header[5],
            "alignment": header[8],
            "entry_size": header[9],
        }
    symtab_header = next(header for header in headers if header[1] == 2)
    strings_header = headers[symtab_header[6]]
    strings = image[
        strings_header[4] : strings_header[4] + strings_header[5]
    ]
    symbols = {}
    for offset in range(
        symtab_header[4],
        symtab_header[4] + symtab_header[5],
        symtab_header[9],
    ):
        name_offset, value, size, info, other, section = struct.unpack_from(
            "<IIIBBH", image, offset
        )
        if name_offset:
            symbols[string_at(strings, name_offset)] = {
                "value": value,
                "size": size,
                "info": info,
                "other": other,
                "section": section,
            }
    return image, sections, symbols


def _sectionless_executable(path: Path):
    image = bytearray(131)
    image[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        2,
        3,
        1,
        0x1000,
        52,
        0,
        0,
        52,
        32,
        2,
        0,
        0,
        0,
    )
    struct.pack_into("<IIIIIIII", image, 52, 1, 128, 0x1000, 0x1000, 2, 2, 5, 1)
    struct.pack_into("<IIIIIIII", image, 84, 1, 130, 0x1004, 0x1004, 1, 1, 4, 1)
    image[128:130] = b"\xaa\xbb"
    image[130] = 0xCC
    path.write_bytes(image)


class CupidObjHostedCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidobj-cli-build-", dir=TOOLCHAIN_ROOT
        )
        cls.cli = _build_cli(
            Path(cls._build_directory.name),
            "cupidobj",
            ["ctool.c", "ctool_host.c", "elf32.c", "cupidobj.c", "cupidobj_main.c"],
        )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_wrap_relative_input_uses_gnu_binary_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            asset = root / "assets" / "hi-world.bin"
            asset.parent.mkdir()
            asset.write_bytes(b"Cupid\x00bytes")
            output = root / "wrapped.o"
            first = subprocess.run(
                [str(self.cli), "wrap", "assets/hi-world.bin", "-o", "wrapped.o"],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            image, sections, symbols = _elf32_sections_and_symbols(output)
            data = sections[".data"]
            self.assertEqual(data["type"], 1)
            self.assertEqual(data["flags"], 0x3)
            self.assertEqual(data["alignment"], 1)
            self.assertEqual(
                image[data["offset"] : data["offset"] + data["size"]],
                asset.read_bytes(),
            )
            stem = "_binary_assets_hi_world_bin"
            self.assertEqual(symbols[stem + "_start"]["value"], 0)
            self.assertEqual(symbols[stem + "_end"]["value"], len(asset.read_bytes()))
            self.assertEqual(symbols[stem + "_size"]["value"], len(asset.read_bytes()))
            self.assertEqual(symbols[stem + "_size"]["section"], 0xFFF1)

            duplicate = root / "duplicate.o"
            second = subprocess.run(
                [str(self.cli), "wrap", "assets/hi-world.bin", "-o", str(duplicate)],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(duplicate.read_bytes(), output.read_bytes())

    def test_wrap_absolute_input_supports_identity_stem_section_and_readonly(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            asset = root / "blob.bin"
            asset.write_bytes(b"abc")
            identity_output = root / "identity.o"
            identity = subprocess.run(
                [
                    str(self.cli),
                    "wrap",
                    str(asset.resolve()),
                    "--identity",
                    "logical/lib.data",
                    "--section=.rodata",
                    "--readonly",
                    "-o",
                    str(identity_output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(identity.returncode, 0, identity.stderr)
            _, sections, symbols = _elf32_sections_and_symbols(identity_output)
            self.assertEqual(sections[".rodata"]["flags"], 0x2)
            self.assertIn("_binary_logical_lib_data_start", symbols)

            stem_output = root / "stem.o"
            stem = subprocess.run(
                [
                    str(self.cli),
                    "wrap",
                    str(asset),
                    "--stem=payload",
                    "-o",
                    str(stem_output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(stem.returncode, 0, stem.stderr)
            _, _, stem_symbols = _elf32_sections_and_symbols(stem_output)
            self.assertIn("payload_start", stem_symbols)
            self.assertIn("payload_end", stem_symbols)
            self.assertIn("payload_size", stem_symbols)

    def test_wrap_text_canonicalizes_crlf_without_changing_binary_wrap(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            lf_source = root / "lf.txt"
            crlf_source = root / "crlf.txt"
            lf_source.write_bytes(b"first\nsecond\nthird\rfourth\n")
            crlf_source.write_bytes(b"first\r\nsecond\r\nthird\rfourth\r\n")
            lf_object = root / "lf.o"
            crlf_object = root / "crlf.o"
            binary_object = root / "binary.o"

            for source, output in (
                (lf_source, lf_object),
                (crlf_source, crlf_object),
            ):
                result = subprocess.run(
                    [
                        str(self.cli),
                        "wrap-text",
                        str(source),
                        "--identity=manual.txt",
                        "-o",
                        str(output),
                    ],
                    cwd=root,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

            self.assertEqual(crlf_object.read_bytes(), lf_object.read_bytes())
            image, sections, symbols = _elf32_sections_and_symbols(crlf_object)
            data = sections[".data"]
            expected = lf_source.read_bytes()
            self.assertEqual(
                image[data["offset"] : data["offset"] + data["size"]], expected
            )
            self.assertEqual(
                symbols["_binary_manual_txt_end"]["value"], len(expected)
            )
            self.assertEqual(
                symbols["_binary_manual_txt_size"]["value"], len(expected)
            )

            binary = subprocess.run(
                [
                    str(self.cli),
                    "wrap",
                    str(crlf_source),
                    "--identity=manual.txt",
                    "-o",
                    str(binary_object),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(binary.returncode, 0, binary.stderr)
            binary_image, binary_sections, _ = _elf32_sections_and_symbols(
                binary_object
            )
            binary_data = binary_sections[".data"]
            self.assertEqual(
                binary_image[
                    binary_data["offset"] : binary_data["offset"]
                    + binary_data["size"]
                ],
                crlf_source.read_bytes(),
            )

    def test_flat_extracts_initialized_load_bytes_and_zero_fills_gap(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            executable = root / "program.elf"
            _sectionless_executable(executable)
            result = subprocess.run(
                [str(self.cli), "flat", str(executable.resolve()), "-o", "program.bin"],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "program.bin").read_bytes(), b"\xaa\xbb\x00\x00\xcc")

    def test_usage_and_processing_failures_do_not_clobber_output(self):
        invalid = subprocess.run(
            [str(self.cli), "wrap", "input.bin", "--identity=x", "--stem=y", "-o", "out.o"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(invalid.returncode, 2)
        self.assertIn("usage: cupidobj", invalid.stderr)

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            output = root / "preserve.o"
            output.write_bytes(b"sentinel")
            missing = subprocess.run(
                [str(self.cli), "wrap-text", "missing.txt", "-o", str(output)],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(missing.returncode, 1)
            self.assertIn("cannot load", missing.stderr)
            self.assertEqual(output.read_bytes(), b"sentinel")

            malformed = root / "bad.elf"
            malformed.write_bytes(b"\x7fELF")
            flat = subprocess.run(
                [str(self.cli), "flat", str(malformed), "-o", str(output)],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(flat.returncode, 1)
            self.assertIn("ELF32 header is truncated", flat.stderr)
            self.assertEqual(output.read_bytes(), b"sentinel")

            asset = root / "asset.bin"
            asset.write_bytes(b"x")
            reserved = subprocess.run(
                [
                    str(self.cli),
                    "wrap",
                    str(asset),
                    "--section=.symtab",
                    "-o",
                    str(output),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(reserved.returncode, 1)
            self.assertIn("section", reserved.stderr.lower())
            self.assertEqual(output.read_bytes(), b"sentinel")

    def test_host_readelf_accepts_wrapped_object_when_available(self):
        readelf = shutil.which("readelf") or shutil.which("llvm-readelf")
        if readelf is None:
            self.skipTest("host readelf oracle is not installed")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            asset = root / "asset.bin"
            output = root / "asset.o"
            asset.write_bytes(b"oracle")
            wrapped = subprocess.run(
                [str(self.cli), "wrap", str(asset), "--stem=asset", "-o", str(output)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(wrapped.returncode, 0, wrapped.stderr)
            report = subprocess.run(
                [readelf, "-h", "-S", "-s", "-W", str(output)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("REL (Relocatable file)", report.stdout)
            self.assertIn("asset_start", report.stdout)


if __name__ == "__main__":
    unittest.main()

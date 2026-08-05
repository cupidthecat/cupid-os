import contextlib
import filecmp
import io
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import hostbuild


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"
BASELINE_JPEG = (
    b"\xff\xd8"
    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    b"\xff\xd9"
)
ENTROPY_JPEG = (
    BASELINE_JPEG[:-2]
    + b"\x12\xff\x00\x34\xff\xd0\x56"
    + BASELINE_JPEG[-2:]
)
ACTIVE_IMAGE_SECTORS = 200 * 1024 * 1024 // hostbuild.SECTOR_SIZE
ACTIVE_FAT_START_LBA = 20480


def _disk_template_size(image_sectors, fat_start_lba):
    layout = hostbuild._choose_layout(image_sectors - fat_start_lba)
    data_start = (
        layout.reserved_sectors
        + layout.num_fats * layout.sectors_per_fat
        + layout.root_dir_sectors
    )
    return (fat_start_lba + data_start) * hostbuild.SECTOR_SIZE


def _python_disk_template(
    output,
    boot,
    kernel,
    image_sectors,
    fat_start_lba,
):
    written = hostbuild._write_pristine_disk_template(
        output,
        boot,
        kernel,
        image_sectors,
        fat_start_lba,
    )
    if written != _disk_template_size(image_sectors, fat_start_lba):
        raise AssertionError("disk-template oracle produced the wrong size")


def _jpeg_byte(payload, offset, value):
    return payload[:offset] + bytes((value,)) + payload[offset + 1 :]


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
        "-x",
        "c",
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
            ["ctool.cc", "ctool_host.cc", "elf32.cc", "cupidobj.cc", "cupidobj_main.cc"],
        )
        cls.asm_cli = _build_cli(
            Path(cls._build_directory.name),
            "cupidasm",
            [
                "ctool.cc",
                "ctool_host.cc",
                "elf32.cc",
                "x86.cc",
                "cupidasm.cc",
                "cupidasm_main.cc",
            ],
        )
        cls.dis_cli = _build_cli(
            Path(cls._build_directory.name),
            "cupiddis",
            [
                "ctool.cc",
                "ctool_host.cc",
                "elf32.cc",
                "x86.cc",
                "cupiddis.cc",
                "cupiddis_main.cc",
            ],
        )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def _run_disk_template(
        self,
        root,
        boot,
        kernel,
        output,
        image_sectors,
        fat_start_lba,
    ):
        return subprocess.run(
            [
                str(self.cli),
                "disk-template",
                str(boot),
                "--kernel",
                str(kernel),
                "--image-sectors",
                str(image_sectors),
                "--fat-start-lba",
                str(fat_start_lba),
                "-o",
                str(output),
            ],
            cwd=root,
            text=True,
            capture_output=True,
        )

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

    def test_wrap_jpeg_accepts_sof0_and_sof1_without_changing_payload(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            cases = (
                ("sof0", BASELINE_JPEG),
                ("sof1", _jpeg_byte(BASELINE_JPEG, 3, 0xC1)),
                ("entropy", ENTROPY_JPEG),
            )
            for name, payload in cases:
                with self.subTest(name=name):
                    source = root / f"photo-{name}.jpg"
                    output = root / f"photo-{name}.o"
                    oracle = root / f"oracle-{name}.jpg"
                    source.write_bytes(payload)
                    with contextlib.redirect_stdout(io.StringIO()):
                        hostbuild._prepare_baseline_jpeg(source, oracle)
                    self.assertEqual(oracle.read_bytes(), payload)
                    wrapped = subprocess.run(
                        [
                            str(self.cli),
                            "wrap-jpeg",
                            str(source),
                            "--identity=photo.jpg",
                            "-o",
                            str(output),
                        ],
                        cwd=root,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(wrapped.returncode, 0, wrapped.stderr)
                    image, sections, symbols = _elf32_sections_and_symbols(output)
                    data = sections[".data"]
                    self.assertEqual(
                        image[data["offset"] : data["offset"] + data["size"]],
                        payload,
                    )
                    self.assertEqual(
                        symbols["_binary_photo_jpg_size"]["value"], len(payload)
                    )

    def test_wrap_jpeg_active_asset_matches_binary_wrap(self):
        asset = REPO_ROOT / "file_example_JPG_1MB.jpg"
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            checked = root / "checked.o"
            binary = root / "binary.o"
            for operation, output in (
                ("wrap-jpeg", checked),
                ("wrap", binary),
            ):
                wrapped = subprocess.run(
                    [
                        str(self.cli),
                        operation,
                        str(asset),
                        "--identity=file_example_JPG_1MB.jpg",
                        "-o",
                        str(output),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(wrapped.returncode, 0, wrapped.stderr)
            self.assertEqual(checked.read_bytes(), binary.read_bytes())

    def test_wrap_jpeg_rejects_bad_frames_without_clobbering_and_recovers(self):
        cases = (
            (
                "missing-soi",
                _jpeg_byte(BASELINE_JPEG, 0, 0x00),
                "JPEG input has no SOI marker",
            ),
            (
                "malformed-marker-stream",
                BASELINE_JPEG[:15] + b"\x01" + BASELINE_JPEG[15:],
                "JPEG marker stream is malformed outside a scan",
            ),
            (
                "stuffed-before-scan",
                BASELINE_JPEG[:15] + b"\xff\x00" + BASELINE_JPEG[15:],
                "JPEG marker stream contains stuffed data before a scan",
            ),
            (
                "trailing-after-eoi",
                BASELINE_JPEG + b"\x00",
                "JPEG input has trailing bytes after the EOI marker",
            ),
            (
                "standalone-marker",
                BASELINE_JPEG[:15] + b"\xff\xd8" + BASELINE_JPEG[15:],
                "unexpected standalone JPEG marker 0xd8",
            ),
            (
                "truncated-marker-length",
                b"\xff\xd8\xff\xdb\x00",
                "JPEG marker length is truncated",
            ),
            (
                "invalid-marker-length",
                b"\xff\xd8\xff\xdb\x00\x01",
                "JPEG marker length is invalid",
            ),
            (
                "duplicate-frame",
                BASELINE_JPEG[:15]
                + BASELINE_JPEG[2:15]
                + BASELINE_JPEG[15:],
                "JPEG input contains more than one frame header",
            ),
            (
                "truncated-frame",
                _jpeg_byte(BASELINE_JPEG, 5, 0x07),
                "JPEG frame header is truncated",
            ),
            (
                "malformed-frame-components",
                _jpeg_byte(BASELINE_JPEG, 5, 0x08),
                "JPEG frame header has an invalid component table",
            ),
            (
                "invalid-frame-precision",
                _jpeg_byte(BASELINE_JPEG, 6, 0x00),
                "JPEG frame header has an invalid sample precision",
            ),
            (
                "invalid-frame-size",
                _jpeg_byte(BASELINE_JPEG, 8, 0x00),
                "JPEG frame header has an invalid image size",
            ),
            (
                "scan-before-frame",
                BASELINE_JPEG[:2] + BASELINE_JPEG[15:],
                "JPEG scan appears before its frame header",
            ),
            (
                "truncated-scan",
                _jpeg_byte(BASELINE_JPEG, 18, 0x05),
                "JPEG scan header is truncated",
            ),
            (
                "malformed-scan-components",
                _jpeg_byte(BASELINE_JPEG, 18, 0x06),
                "JPEG scan header has an invalid component table",
            ),
            (
                "partial-entropy-marker",
                BASELINE_JPEG[:25] + b"\xff",
                "JPEG entropy data ends with a partial marker",
            ),
            (
                "progressive-frame",
                _jpeg_byte(BASELINE_JPEG, 3, 0xC2),
                "unsupported progressive JPEG frame; "
                "check in a baseline SOF0/SOF1 asset",
            ),
            (
                "missing-frame",
                b"\xff\xd8\xff\xd9",
                "JPEG input has no supported SOF0/SOF1 frame",
            ),
            (
                "unsupported-frame",
                _jpeg_byte(BASELINE_JPEG, 3, 0xC3),
                "unsupported JPEG frame marker 0xc3; "
                "check in a baseline SOF0/SOF1 asset",
            ),
            (
                "missing-scan",
                BASELINE_JPEG[:15] + BASELINE_JPEG[-2:],
                "JPEG input has no scan",
            ),
            (
                "missing-eoi",
                BASELINE_JPEG[:-2],
                "JPEG input has no EOI marker",
            ),
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "photo.jpg"
            output = root / "photo.o"
            for name, payload, message in cases:
                with self.subTest(name=name):
                    source.write_bytes(payload)
                    output.write_bytes(b"existing object")
                    oracle_output = root / "checked-baseline.jpg"
                    with self.assertRaises(hostbuild.EmbedJpegError) as error:
                        hostbuild._prepare_baseline_jpeg(source, oracle_output)
                    self.assertEqual(str(error.exception), message)
                    self.assertFalse(oracle_output.exists())
                    wrapped = subprocess.run(
                        [
                            str(self.cli),
                            "wrap-jpeg",
                            str(source),
                            "-o",
                            str(output),
                        ],
                        cwd=root,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(wrapped.returncode, 1)
                    self.assertIn(message, wrapped.stderr)
                    self.assertEqual(output.read_bytes(), b"existing object")

            source.write_bytes(BASELINE_JPEG)
            recovered = subprocess.run(
                [
                    str(self.cli),
                    "wrap-jpeg",
                    str(source),
                    "-o",
                    str(output),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(recovered.returncode, 0, recovered.stderr)
            self.assertNotEqual(output.read_bytes(), b"existing object")

    def test_disk_template_matches_python_layout_for_small_geometry_and_repeats(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            expected = root / "expected.img"
            first = root / "first.img"
            second = root / "second.img"
            image_sectors = 8192
            fat_start_lba = 16
            boot.write_bytes(
                bytes(
                    (index * 37 + 11) & 0xFF
                    for index in range(5 * hostbuild.SECTOR_SIZE)
                )
            )
            kernel.write_bytes(
                bytes((index * 13 + 5) & 0xFF for index in range(4097))
            )

            for output in (first, second):
                generated = self._run_disk_template(
                    root,
                    boot,
                    kernel,
                    output,
                    image_sectors,
                    fat_start_lba,
                )
                self.assertEqual(generated.returncode, 0, generated.stderr)
                self.assertEqual(generated.stdout, "")
                self.assertEqual(generated.stderr, "")

            _python_disk_template(
                expected,
                boot,
                kernel,
                image_sectors,
                fat_start_lba,
            )
            self.assertTrue(filecmp.cmp(first, expected, shallow=False))
            self.assertTrue(filecmp.cmp(second, expected, shallow=False))

    def test_disk_template_matches_python_layout_for_active_geometry(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            expected = root / "expected.img"
            output = root / "actual.img"
            boot.write_bytes(
                bytes(
                    (index * 29 + 7) & 0xFF
                    for index in range(5 * hostbuild.SECTOR_SIZE)
                )
            )
            kernel.write_bytes(
                b"Cupid kernel\0"
                + bytes(
                    (index * 17 + 3) & 0xFF
                    for index in range(1024 * 1024 + 37)
                )
            )

            generated = self._run_disk_template(
                root,
                boot,
                kernel,
                output,
                ACTIVE_IMAGE_SECTORS,
                ACTIVE_FAT_START_LBA,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")

            _python_disk_template(
                expected,
                boot,
                kernel,
                ACTIVE_IMAGE_SECTORS,
                ACTIVE_FAT_START_LBA,
            )
            self.assertEqual(
                output.stat().st_size,
                _disk_template_size(
                    ACTIVE_IMAGE_SECTORS,
                    ACTIVE_FAT_START_LBA,
                ),
            )
            self.assertTrue(filecmp.cmp(output, expected, shallow=False))

    def test_disk_template_accepts_kernel_ending_at_fat_boundary(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            output = root / "disk.img"
            image_sectors = 4208
            fat_start_lba = 8
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(
                b"K" * ((fat_start_lba - 5) * hostbuild.SECTOR_SIZE)
            )

            generated = self._run_disk_template(
                root,
                boot,
                kernel,
                output,
                image_sectors,
                fat_start_lba,
            )

            self.assertEqual(generated.returncode, 0, generated.stderr)
            payload = output.read_bytes()
            self.assertEqual(
                payload[
                    5 * hostbuild.SECTOR_SIZE :
                    fat_start_lba * hostbuild.SECTOR_SIZE
                ],
                kernel.read_bytes(),
            )
            self.assertEqual(
                payload[fat_start_lba * hostbuild.SECTOR_SIZE],
                0xEB,
            )

    def test_disk_template_advances_after_a_fat_size_cycle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            output = root / "disk.img"
            image_sectors = 8304
            fat_start_lba = 16
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"Cupid")

            generated = self._run_disk_template(
                root,
                boot,
                kernel,
                output,
                image_sectors,
                fat_start_lba,
            )

            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            layout = hostbuild._choose_layout(
                image_sectors - fat_start_lba
            )
            expected_size = (
                fat_start_lba
                + layout.reserved_sectors
                + layout.num_fats * layout.sectors_per_fat
                + layout.root_dir_sectors
            ) * hostbuild.SECTOR_SIZE
            payload = output.read_bytes()
            bpb_offset = fat_start_lba * hostbuild.SECTOR_SIZE
            self.assertEqual(len(payload), expected_size)
            self.assertEqual(payload[bpb_offset + 13], 2)
            self.assertEqual(
                struct.unpack_from("<H", payload, bpb_offset + 22)[0],
                17,
            )

    def test_disk_template_rejects_bad_numbers_without_clobbering_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            output = root / "disk.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            cases = (
                ("image-text", "not-a-number", "8192"),
                ("image-overflow", "4294967296", "8192"),
                ("fat-text", "8192", "not-a-number"),
                ("fat-negative", "8192", "-1"),
            )

            for name, image_sectors, fat_start_lba in cases:
                with self.subTest(name=name):
                    output.write_bytes(b"existing disk image")
                    rejected = self._run_disk_template(
                        root,
                        boot,
                        kernel,
                        output,
                        image_sectors,
                        fat_start_lba,
                    )
                    self.assertEqual(rejected.returncode, 2)
                    self.assertEqual(rejected.stdout, "")
                    self.assertIn("usage: cupidobj", rejected.stderr)
                    self.assertIn("--image-sectors", rejected.stderr)
                    self.assertIn("--fat-start-lba", rejected.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"existing disk image"
                    )

    def test_disk_template_rejects_invalid_geometry_without_clobbering_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            output = root / "disk.img"
            cases = (
                (
                    "short-boot",
                    5 * hostbuild.SECTOR_SIZE - 1,
                    64,
                    8192,
                    16,
                    "expected at least 5 sectors",
                ),
                (
                    "reserved-area",
                    5 * hostbuild.SECTOR_SIZE,
                    64,
                    8192,
                    5,
                    "FAT partition must start after bootloader and kernel area",
                ),
                (
                    "partition-past-image",
                    5 * hostbuild.SECTOR_SIZE,
                    64,
                    8192,
                    8192,
                    "FAT partition start is beyond image size",
                ),
                (
                    "kernel-overlap",
                    5 * hostbuild.SECTOR_SIZE,
                    6000,
                    8192,
                    16,
                    "overlaps FAT partition at LBA 16",
                ),
                (
                    "fat16-too-small",
                    5 * hostbuild.SECTOR_SIZE,
                    64,
                    4096,
                    16,
                    "cannot make FAT16 layout",
                ),
            )

            for (
                name,
                boot_size,
                kernel_size,
                image_sectors,
                fat_start_lba,
                message,
            ) in cases:
                with self.subTest(name=name):
                    boot.write_bytes(b"B" * boot_size)
                    kernel.write_bytes(b"K" * kernel_size)
                    output.write_bytes(b"existing disk image")
                    rejected = self._run_disk_template(
                        root,
                        boot,
                        kernel,
                        output,
                        image_sectors,
                        fat_start_lba,
                    )
                    self.assertEqual(rejected.returncode, 1)
                    self.assertEqual(rejected.stdout, "")
                    self.assertIn(message, rejected.stderr)
                    if name == "kernel-overlap":
                        self.assertIn(kernel.as_posix(), rejected.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"existing disk image"
                    )

    def test_disk_template_rejects_i386_size_overflow_without_clobbering_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            output = root / "disk.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            output.write_bytes(b"existing disk image")

            rejected = self._run_disk_template(
                root,
                boot,
                kernel,
                output,
                8392808,
                8388608,
            )

            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "CupidObj disk template size overflows i386",
                rejected.stderr,
            )
            self.assertEqual(output.read_bytes(), b"existing disk image")

    def test_disk_template_reports_missing_inputs_without_clobbering_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            output = root / "disk.img"
            boot.write_bytes(b"B" * (5 * hostbuild.SECTOR_SIZE))
            kernel.write_bytes(b"kernel")
            missing_boot = root / "missing-boot.bin"
            missing_kernel = root / "missing-kernel.bin"

            for name, boot_path, kernel_path, missing in (
                ("boot", missing_boot, kernel, missing_boot),
                ("kernel", boot, missing_kernel, missing_kernel),
            ):
                with self.subTest(name=name):
                    output.write_bytes(b"existing disk image")
                    rejected = self._run_disk_template(
                        root,
                        boot_path,
                        kernel_path,
                        output,
                        8192,
                        16,
                    )
                    self.assertEqual(rejected.returncode, 1)
                    self.assertEqual(rejected.stdout, "")
                    self.assertIn("cannot load", rejected.stderr)
                    self.assertIn(missing.name, rejected.stderr)
                    self.assertIn("not_found", rejected.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"existing disk image"
                    )

    def test_install_source_demos_matches_python_oracle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            demos = root / "demos"
            demos.mkdir()
            (demos / "alpha.asm").write_text("ret\n", encoding="ascii")
            (demos / "beta_test.asm").write_text("ret\n", encoding="ascii")
            expected = root / "expected.cc"
            actual = root / "actual.cc"
            oracle = subprocess.run(
                [
                    shutil.which("python") or "python",
                    str(REPO_ROOT / "tools" / "hostbuild.py"),
                    "gen-demos-programs",
                    "--out",
                    str(expected),
                    "--demos",
                    "demos/alpha.asm",
                    "demos/beta_test.asm",
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            generated = subprocess.run(
                [
                    str(self.cli),
                    "install-source",
                    "demos",
                    "--demos",
                    "demos/alpha.asm",
                    "demos/beta_test.asm",
                    "-o",
                    str(actual),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(actual.read_bytes(), expected.read_bytes())

    def test_install_source_bin_matches_python_oracle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "bin" / "browser").mkdir(parents=True)
            for relative in (
                "bin/alpha.cc",
                "bin/beta_test.cc",
                "bin/shared.h",
                "bin/browser/dom.cc",
                "bin/browser/url_hash.cc",
            ):
                (root / relative).write_text("\n", encoding="ascii")
            expected = root / "expected.cc"
            actual = root / "actual.cc"
            arguments = [
                "--bin",
                "bin/alpha.cc",
                "bin/beta_test.cc",
                "--headers",
                "bin/shared.h",
                "--browser",
                "bin/browser/dom.cc",
                "bin/browser/url_hash.cc",
            ]
            oracle = subprocess.run(
                [
                    shutil.which("python") or "python",
                    str(REPO_ROOT / "tools" / "hostbuild.py"),
                    "gen-bin-programs",
                    "--out",
                    str(expected),
                    *arguments,
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            generated = subprocess.run(
                [
                    str(self.cli),
                    "install-source",
                    "bin",
                    *arguments,
                    "-o",
                    str(actual),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(actual.read_bytes(), expected.read_bytes())

    def test_install_source_docs_matches_python_oracle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "cupidos-txt").mkdir()
            for relative in (
                "cupidos-txt/00INDEX.CTXT",
                "cupidos-txt/12HOLYC-CUPIDC.CTXT",
                "image.bmp",
                "snail.bmp",
                "test.png",
                "photo.jpg",
                "scan.jpeg",
            ):
                (root / relative).write_bytes(b"fixture")
            expected = root / "expected.cc"
            actual = root / "actual.cc"
            arguments = [
                "--ctxt",
                "cupidos-txt/00INDEX.CTXT",
                "cupidos-txt/12HOLYC-CUPIDC.CTXT",
                "--doc-assets",
                "image.bmp",
                "--home-assets",
                "test.png",
                "scan.jpeg",
                "image.bmp",
                "photo.jpg",
                "snail.bmp",
            ]
            oracle = subprocess.run(
                [
                    shutil.which("python") or "python",
                    str(REPO_ROOT / "tools" / "hostbuild.py"),
                    "gen-docs-programs",
                    "--out",
                    str(expected),
                    *arguments,
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            generated = subprocess.run(
                [
                    str(self.cli),
                    "install-source",
                    "docs",
                    *arguments,
                    "-o",
                    str(actual),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(actual.read_bytes(), expected.read_bytes())
            output = actual.read_text(encoding="utf-8")
            ordered_entries = [
                'install_home_asset("/home/test.png"',
                'install_home_asset("/home/scan.jpeg"',
                'install_home_asset("/home/image.bmp"',
                'install_home_asset("/home/photo.jpg"',
                'install_home_asset("/home/snail.bmp"',
            ]
            positions = [output.index(entry) for entry in ordered_entries]
            self.assertEqual(positions, sorted(positions))

    def test_install_source_active_inventory_matches_oracle_and_repeats(self):
        bin_sources = sorted(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "bin").glob("*.cc")
            if path.name not in {"old_cc2.cc", "old_cc2_single.cc"}
        )
        bin_headers = sorted(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "bin").glob("*.h")
        )
        browser_sources = sorted(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "bin" / "browser").glob("*.cc")
        )
        ctxt_sources = sorted(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "cupidos-txt").glob("*.CTXT")
        )
        home_assets = [
            path.relative_to(REPO_ROOT).as_posix()
            for extension in ("*.bmp", "*.png", "*.jpg", "*.jpeg")
            for path in sorted(REPO_ROOT.glob(extension))
        ]
        demo_sources = sorted(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "demos").glob("*.asm")
        )
        cases = (
            (
                "bin",
                "gen-bin-programs",
                [
                    "--bin",
                    *bin_sources,
                    "--headers",
                    *bin_headers,
                    "--browser",
                    *browser_sources,
                ],
            ),
            (
                "docs",
                "gen-docs-programs",
                [
                    "--ctxt",
                    *ctxt_sources,
                    "--doc-assets",
                    "image.bmp",
                    "--home-assets",
                    *home_assets,
                ],
            ),
            (
                "demos",
                "gen-demos-programs",
                ["--demos", *demo_sources],
            ),
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for mode, oracle_command, arguments in cases:
                expected = root / f"{mode}-expected.cc"
                first = root / f"{mode}-first.cc"
                second = root / f"{mode}-second.cc"
                oracle = subprocess.run(
                    [
                        shutil.which("python") or "python",
                        str(REPO_ROOT / "tools" / "hostbuild.py"),
                        oracle_command,
                        "--out",
                        str(expected),
                        *arguments,
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(oracle.returncode, 0, oracle.stderr)
                for output in (first, second):
                    generated = subprocess.run(
                        [
                            str(self.cli),
                            "install-source",
                            mode,
                            *arguments,
                            "-o",
                            str(output),
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(generated.returncode, 0, generated.stderr)
                self.assertEqual(first.read_bytes(), expected.read_bytes())
                self.assertEqual(second.read_bytes(), first.read_bytes())

    def test_install_source_rejects_bad_paths_without_clobbering_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            demos = root / "demos"
            demos.mkdir()
            (demos / "good.asm").write_text("ret\n", encoding="ascii")
            (demos / "wrong.cc").write_text("return 0;\n", encoding="ascii")
            output = root / "install.cc"
            output.write_bytes(b"sentinel")
            invalid = subprocess.run(
                [
                    str(self.cli),
                    "install-source",
                    "demos",
                    "--demos",
                    "demos/wrong.cc",
                    "-o",
                    str(output),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(invalid.returncode, 1)
            self.assertIn("must match demos/NAME.asm", invalid.stderr)
            self.assertEqual(output.read_bytes(), b"sentinel")

            duplicate = subprocess.run(
                [
                    str(self.cli),
                    "install-source",
                    "demos",
                    "--demos",
                    "demos/good.asm",
                    "demos/good.asm",
                    "-o",
                    str(output),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(duplicate.returncode, 1)
            self.assertIn("duplicated", duplicate.stderr)
            self.assertEqual(output.read_bytes(), b"sentinel")

            recovered = subprocess.run(
                [
                    str(self.cli),
                    "install-source",
                    "demos",
                    "--demos",
                    "demos/good.asm",
                    "-o",
                    str(output),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(recovered.returncode, 0, recovered.stderr)
            self.assertNotEqual(output.read_bytes(), b"sentinel")

    def test_install_source_rejects_emitted_symbol_collisions(self):
        cases = (
            (
                "bin",
                [
                    "--bin",
                    "bin/browser_alpha.cc",
                    "--browser",
                    "bin/browser/alpha.cc",
                ],
            ),
            (
                "docs",
                [
                    "--ctxt",
                    "cupidos-txt/a-b.CTXT",
                    "cupidos-txt/a_b.CTXT",
                ],
            ),
            (
                "docs",
                [
                    "--doc-assets",
                    "a-b.bmp",
                    "--home-assets",
                    "a_b.bmp",
                ],
            ),
        )
        commands = {
            "bin": "gen-bin-programs",
            "docs": "gen-docs-programs",
        }
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for relative in (
                "bin/browser_alpha.cc",
                "bin/browser/alpha.cc",
                "cupidos-txt/a-b.CTXT",
                "cupidos-txt/a_b.CTXT",
                "a-b.bmp",
                "a_b.bmp",
            ):
                fixture = root / relative
                fixture.parent.mkdir(parents=True, exist_ok=True)
                fixture.write_bytes(b"fixture")
            for case_index, (mode, arguments) in enumerate(cases):
                with self.subTest(mode=mode, case=case_index):
                    native_output = root / f"native-{case_index}.cc"
                    oracle_output = root / f"oracle-{case_index}.cc"
                    native_output.write_bytes(b"sentinel")
                    oracle_output.write_bytes(b"sentinel")
                    native = subprocess.run(
                        [
                            str(self.cli),
                            "install-source",
                            mode,
                            *arguments,
                            "-o",
                            str(native_output),
                        ],
                        cwd=root,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(native.returncode, 1)
                    self.assertIn("same binary symbol", native.stderr)
                    self.assertEqual(native_output.read_bytes(), b"sentinel")

                    oracle = subprocess.run(
                        [
                            shutil.which("python") or "python",
                            str(REPO_ROOT / "tools" / "hostbuild.py"),
                            commands[mode],
                            "--out",
                            str(oracle_output),
                            *arguments,
                        ],
                        cwd=root,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(oracle.returncode, 1)
                    self.assertIn("same binary symbol", oracle.stderr)
                    self.assertEqual(oracle_output.read_bytes(), b"sentinel")

    def test_ksyms_source_matches_python_oracle_and_repeats(self):
        symbol_text = (
            "00002000 T second\n"
            "00001000 T first\n"
            "00001000 T duplicate\n"
            "         U unresolved\n"
            "00003000 D data_only\n"
            "00004000 t .Lprivate\n"
            "00005000 W weak_text\n"
        )
        symbols = hostbuild._parse_nm_symbols(symbol_text)
        expected = hostbuild._render_ksyms_source(
            hostbuild.build_ksyms_blob(symbols)
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "kernel.symbols"
            first = root / "first.cc"
            second = root / "second.cc"
            source.write_text(symbol_text, encoding="ascii")
            for output in (first, second):
                generated = subprocess.run(
                    [
                        str(self.cli),
                        "ksyms-source",
                        str(source),
                        "-o",
                        str(output),
                    ],
                    cwd=root,
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(first.read_bytes(), expected)
            self.assertEqual(second.read_bytes(), expected)

    def test_ksyms_source_consumes_real_cupiddis_numeric_symbols(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            assembly = root / "symbols.asm"
            object_path = root / "symbols.o"
            symbol_text = root / "symbols.txt"
            generated_source = root / "ksyms_data.cc"
            assembly.write_text(
                "BITS 32\n"
                "global global_text\n"
                "section .text\n"
                "local_text:\n"
                "    nop\n"
                "global_text:\n"
                "    ret\n",
                encoding="ascii",
            )
            assembled = subprocess.run(
                [
                    str(self.asm_cli),
                    "-f",
                    "elf32",
                    str(assembly),
                    "-o",
                    str(object_path),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            inspected = subprocess.run(
                [str(self.dis_cli), "-n", str(object_path)],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(inspected.returncode, 0, inspected.stderr)
            self.assertEqual(inspected.stderr, "")
            self.assertIn("00000000 t local_text\n", inspected.stdout)
            self.assertIn("00000001 T global_text\n", inspected.stdout)
            symbol_text.write_text(inspected.stdout, encoding="ascii")
            generated = subprocess.run(
                [
                    str(self.cli),
                    "ksyms-source",
                    str(symbol_text),
                    "-o",
                    str(generated_source),
                ],
                cwd=root,
                text=True,
                capture_output=True,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            expected = hostbuild._render_ksyms_source(
                hostbuild.build_ksyms_blob(
                    hostbuild._parse_nm_symbols(inspected.stdout)
                )
            )
            self.assertEqual(generated_source.read_bytes(), expected)

    def test_ksyms_source_rejects_bad_symbol_text_without_clobbering_output(self):
        cases = (
            ("not-an-address T broken\n", "invalid address"),
            (
                "00001000 T valid\nnot-an-address T broken\n",
                ":2:0: error CT8000002:",
            ),
            ("100000000 T too_wide\n", "outside i386"),
            ("T missing_address\n", "omitted an address"),
            ("00001000 TT broken\n", "malformed row"),
            ("00002000 D data_only\n", "no kernel text symbols"),
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / "kernel.symbols"
            output = root / "ksyms_data.cc"
            for case_index, (symbol_text, message) in enumerate(cases):
                with self.subTest(case=case_index):
                    source.write_text(symbol_text, encoding="ascii")
                    output.write_bytes(b"existing generated source")
                    generated = subprocess.run(
                        [
                            str(self.cli),
                            "ksyms-source",
                            str(source),
                            "-o",
                            str(output),
                        ],
                        cwd=root,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(generated.returncode, 1)
                    self.assertIn(message, generated.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"existing generated source"
                    )

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

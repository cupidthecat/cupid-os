import contextlib
import io
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild

REPO_ROOT = Path(__file__).resolve().parents[1]
BASELINE_JPEG = (
    b"\xff\xd8"
    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    b"\xff\xd9"
)
PROGRESSIVE_JPEG = BASELINE_JPEG.replace(b"\xff\xc0", b"\xff\xc2", 1)


class HostBuildImageTests(unittest.TestCase):
    def test_image_create_stages_file_and_preserves_existing_fat(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"
            staged = root / "hello.iso"

            boot.write_bytes(bytes((i & 0xFF) for i in range(5 * 512)))
            kernel.write_bytes(b"KERNEL" * 200)
            staged.write_bytes(b"iso fixture")

            hostbuild.create_or_update_image(
                image=image,
                bootloader=boot,
                kernel=kernel,
                hdd_mb=16,
                fat_start_lba=2048,
                stage_files=[hostbuild.StageFile(staged, "/hello.iso")],
                force_format=False,
            )

            data = image.read_bytes()
            self.assertEqual(data[510:512], b"\x55\xaa")
            self.assertEqual(data[446], 0x80)
            self.assertEqual(data[450], 0x06)
            self.assertEqual(struct.unpack_from("<I", data, 454)[0], 2048)
            self.assertEqual(data[:446], boot.read_bytes()[:446])
            self.assertEqual(data[512:5 * 512], boot.read_bytes()[512:5 * 512])
            self.assertEqual(data[5 * 512:5 * 512 + kernel.stat().st_size], kernel.read_bytes())

            fat_offset = 2048 * 512
            self.assertEqual(data[fat_offset + 510:fat_offset + 512], b"\x55\xaa")
            self.assertEqual(struct.unpack_from("<H", data, fat_offset + 11)[0], 512)
            self.assertEqual(data[fat_offset + 54:fat_offset + 62].rstrip(), b"FAT16")
            self.assertIn(b"HELLO   ISO", data[fat_offset:fat_offset + 256 * 1024])

            kernel.write_bytes(b"NEWKERNEL")
            hostbuild.create_or_update_image(
                image=image,
                bootloader=boot,
                kernel=kernel,
                hdd_mb=16,
                fat_start_lba=2048,
                stage_files=[],
                force_format=False,
            )

            data2 = image.read_bytes()
            self.assertIn(b"HELLO   ISO", data2[fat_offset:fat_offset + 256 * 1024])
            self.assertEqual(data2[5 * 512:5 * 512 + kernel.stat().st_size], b"NEWKERNEL")

    def test_image_rejects_kernel_overlap_with_fat_partition(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            image = root / "cupidos.img"

            boot.write_bytes(b"B" * (5 * 512))
            kernel.write_bytes(b"K" * (20 * 512))

            with self.assertRaisesRegex(ValueError, "overlaps FAT partition"):
                hostbuild.create_or_update_image(
                    image=image,
                    bootloader=boot,
                    kernel=kernel,
                    hdd_mb=8,
                    fat_start_lba=16,
                    stage_files=[],
                    force_format=False,
                )


class HostBuildSymbolTests(unittest.TestCase):
    def test_mksyms_runs_checked_cupiddis_from_the_seed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            calls = []

            def run_seed(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                calls.append(
                    (
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                        timeout,
                    )
                )
                self.assertEqual(tool_name, "cupiddis")
                self.assertEqual(arguments[0], "-n")
                self.assertNotEqual(Path(arguments[1]), elf)
                self.assertEqual(
                    Path(arguments[1]).read_bytes(),
                    elf.read_bytes(),
                )
                return subprocess.CompletedProcess(
                    ["cupiddis", *arguments],
                    0,
                    "00001000 T first\n00002000 T second\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=run_seed,
                create=True,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            self.assertTrue(output.is_file())

    def test_mksyms_maps_checked_seed_failure_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            failure = subprocess.CompletedProcess(
                ["cupiddis", "-n"],
                9,
                "",
                "invalid pass-one ELF\n",
            )
            diagnostic = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=failure,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidDis failed with status 9: "
                "invalid pass-one ELF",
                diagnostic.getvalue(),
            )
            self.assertEqual(
                output.read_bytes(),
                b"existing generated source",
            )

    def test_mksyms_rejects_seed_manifest_drift(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            manifest.write_bytes(b"checked manifest")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            diagnostic = io.StringIO()

            def run_seed(*_args, **_kwargs):
                manifest.write_bytes(b"changed manifest")
                return subprocess.CompletedProcess(
                    ["cupiddis", "-n"],
                    0,
                    "00001000 T first\n",
                    "",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--seed-manifest",
                        str(manifest),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "kernel symbol inputs changed while generating source",
                diagnostic.getvalue(),
            )
            self.assertEqual(
                output.read_bytes(),
                b"existing generated source",
            )

    def test_mksyms_uses_one_frozen_reader_and_elf_snapshot(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            calls = []

            def run(command, **kwargs):
                calls.append((command, kwargs))
                self.assertNotEqual(Path(command[0]), reader)
                self.assertNotEqual(Path(command[-1]), elf)
                self.assertEqual(Path(command[0]).read_bytes(), reader.read_bytes())
                self.assertEqual(Path(command[-1]).read_bytes(), elf.read_bytes())
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n00002000 T second\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            source = output.read_text(encoding="utf-8")
            self.assertIn("const unsigned int", source)
            self.assertIn("const unsigned int ksym_blob_size = 45u;", source)

    def test_mksyms_keeps_same_basename_inputs_in_distinct_snapshots(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "reader" / "shared.exe"
            elf = root / "input" / "shared.exe"
            output = root / "ksyms_data.cc"
            reader.parent.mkdir()
            elf.parent.mkdir()
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")

            def run(command, **_kwargs):
                frozen_reader = Path(command[0])
                frozen_elf = Path(command[-1])
                self.assertNotEqual(frozen_reader, frozen_elf)
                self.assertEqual(frozen_reader.read_bytes(), reader.read_bytes())
                self.assertEqual(frozen_elf.read_bytes(), elf.read_bytes())
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertTrue(output.is_file())

    def test_mksyms_rejects_malformed_symbol_output_without_replacing_source(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "not-an-address T broken\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_a_defined_symbol_without_an_address(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "T missing_address\n00001000 T valid\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_an_address_outside_i386(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "100000000 T too_wide\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_an_empty_text_symbol_set(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            completed = subprocess.CompletedProcess(
                [str(reader), "-n", str(elf)],
                0,
                "00002000 D data_only\n",
                "",
            )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                return_value=completed,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_maps_reader_failure_and_preserves_the_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")
            failure = subprocess.CalledProcessError(
                7,
                [str(reader), "-n", str(elf)],
                stderr="invalid ELF",
            )
            diagnostic = io.StringIO()

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=failure,
            ), contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "symbol reader failed with status 7: invalid ELF",
                diagnostic.getvalue(),
            )
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_live_input_drift_without_replacing_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")

            def run(command, **_kwargs):
                elf.write_bytes(b"changed pass-one ELF")
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_mksyms_rejects_reader_drift_without_replacing_source(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            output.write_bytes(b"existing generated source")

            def run(command, **_kwargs):
                reader.write_bytes(b"changed CupidDis")
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "00001000 T first\n",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=run,
            ):
                status = hostbuild.main(
                    [
                        "mksyms",
                        "--nm",
                        str(reader),
                        str(elf),
                        str(output),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(output.read_bytes(), b"existing generated source")

    def test_symbol_reader_preserves_configured_command_arguments(self):
        reader = ("custom-nm", "--target=i386")
        elf = Path("kernel.elf")
        completed = subprocess.CompletedProcess(
            [*reader, "-n", str(elf)],
            0,
            "00001000 T first\n00002000 D ignored\n",
            "",
        )

        with mock.patch(
            "tools.hostbuild.subprocess.run", return_value=completed
        ) as run:
            symbols = hostbuild._symbols_from_nm(reader, elf)

        run.assert_called_once_with(
            [*reader, "-n", str(elf)],
            check=True,
            text=True,
            capture_output=True,
        )
        self.assertEqual(symbols, [(0x1000, "first")])

    def test_ksyms_blob_is_stable_sorted_and_deduplicated(self):
        blob = hostbuild.build_ksyms_blob(
            [
                (0x2000, "second"),
                (0x1000, "first"),
                (0x1000, "duplicate"),
                (0x3000, ".Llocal"),
            ]
        )

        magic, count, string_off, total_size = struct.unpack_from("<IIII", blob, 0)
        self.assertEqual(magic, 0x4D59534B)
        self.assertEqual(count, 2)
        self.assertEqual(total_size, len(blob))
        entries = [
            struct.unpack_from("<II", blob, 16 + i * 8)
            for i in range(count)
        ]
        self.assertEqual([addr for addr, _ in entries], [0x1000, 0x2000])
        strings = blob[string_off:]
        self.assertIn(b"first\x00", strings)
        self.assertIn(b"second\x00", strings)

    def test_ksyms_words_preserve_little_endian_bytes_and_padding(self):
        cases = (
            (b"\x01\x02\x03\x04\x05", b"\0\0\0"),
            (b"\x01\x02\x03\x04\x05\x06", b"\0\0"),
            (b"\x01\x02\x03\x04\x05\x06\x07", b"\0"),
        )

        for blob, padding in cases:
            with self.subTest(blob_size=len(blob)):
                words = hostbuild._pack_ksyms_words(blob)
                encoded = b"".join(
                    struct.pack("<I", word) for word in words
                )
                self.assertEqual(encoded[: len(blob)], blob)
                self.assertEqual(encoded[len(blob) :], padding)

    def test_ksyms_source_uses_words_and_keeps_the_exact_blob_size(self):
        symbols = [(0x1000, "first"), (0x2000, "second")]
        blob = hostbuild.build_ksyms_blob(symbols)

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            reader = root / "cupiddis.exe"
            elf = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            reader.write_bytes(b"checked CupidDis")
            elf.write_bytes(b"pass-one ELF")
            with mock.patch.object(
                hostbuild,
                "_symbols_from_nm",
                return_value=symbols,
            ):
                hostbuild.write_ksyms_source(str(reader), elf, output)
            source = output.read_text(encoding="utf-8")

        self.assertIn("const unsigned int\n", source)
        self.assertNotIn("const unsigned char\n", source)
        for word in hostbuild._pack_ksyms_words(blob):
            self.assertIn(f"0x{word:08x}u,", source)
        self.assertIn(
            f"const unsigned int ksym_blob_size = {len(blob)}u;",
            source,
        )

        consumer = (
            REPO_ROOT / "kernel" / "cpu" / "ksyms.cc"
        ).read_text(encoding="utf-8")
        self.assertIn("extern const unsigned int ksym_blob[];", consumer)
        self.assertIn("const unsigned int ksym_blob[4]", consumer)
        self.assertNotIn("extern const unsigned char ksym_blob[];", consumer)


class HostBuildAssetTests(unittest.TestCase):
    def test_embed_jpeg_runs_checked_cupidobj_from_the_seed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            calls = []

            def run_seed(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                calls.append(
                    (
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                        timeout,
                    )
                )
                self.assertEqual(tool_name, "cupidobj")
                self.assertEqual(arguments[0], "wrap")
                self.assertEqual(arguments[2:4], ["--identity", str(src)])
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=run_seed,
                create=True,
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            self.assertEqual(out.read_bytes(), b"checked object")

    def test_embed_jpeg_preserves_output_after_checked_seed_failure(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            failure = subprocess.CompletedProcess(
                ["cupidobj", "wrap"],
                6,
                "",
                "wrap failed\n",
            )
            diagnostic = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=failure,
                    create=True,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidObj failed with status 6: wrap failed",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_input_drift_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                src.write_bytes(BASELINE_JPEG + b"\x00")
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked JPEG inputs changed while wrapping the object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_wraps_checked_in_bytes_with_original_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            src.write_bytes(BASELINE_JPEG)
            object_tool_commands = []

            def fake_run(args, **kwargs):
                if args[0] == "cupidobj":
                    object_tool_commands.append(args)
                    self.assertEqual(
                        Path(args[2]).read_bytes(),
                        BASELINE_JPEG,
                    )
                    Path(args[-1]).write_bytes(b"object")
                    return subprocess.CompletedProcess(args, 0)
                raise AssertionError(f"unexpected command: {args}")

            with mock.patch(
                "tools.hostbuild.shutil.which",
                side_effect=AssertionError("host converter lookup"),
            ), mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=fake_run,
            ):
                hostbuild.embed_jpeg("cupidobj", src, out)

            self.assertEqual(
                object_tool_commands,
                [[
                    "cupidobj",
                    "wrap",
                    str(out) + ".baseline.jpg",
                    "--identity",
                    str(src),
                    "-o",
                    str(out),
                ]],
            )
            self.assertTrue(out.exists())

    def test_embed_jpeg_rejects_progressive_input_without_replacing_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(PROGRESSIVE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            with mock.patch(
                "tools.hostbuild.shutil.which",
                side_effect=AssertionError("host converter lookup"),
            ), mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=AssertionError("CupidObj must not run"),
            ), contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "unsupported progressive JPEG frame",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_malformed_frame_without_running_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(
                BASELINE_JPEG.replace(
                    b"\xff\xc0\x00\x0b",
                    b"\xff\xc0\x00\x08",
                    1,
                )
            )
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=AssertionError("CupidObj must not run"),
            ), contextlib.redirect_stderr(diagnostic):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "JPEG frame header has an invalid component table",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")


if __name__ == "__main__":
    unittest.main()

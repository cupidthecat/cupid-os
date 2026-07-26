import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild

REPO_ROOT = Path(__file__).resolve().parents[1]


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
            output = Path(td) / "ksyms_data.cc"
            with mock.patch.object(
                hostbuild,
                "_symbols_from_nm",
                return_value=symbols,
            ):
                hostbuild.write_ksyms_source("unused-nm", Path("unused"), output)
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
    def test_embed_jpeg_wraps_converted_bytes_with_original_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            src.write_bytes(b"source jpeg")
            ffmpeg_outputs = []
            object_tool_commands = []

            def fake_which(name):
                return "ffmpeg" if name == "ffmpeg" else None

            def fake_run(args, **kwargs):
                if args[0] == "ffmpeg":
                    tmp = Path(args[-1])
                    ffmpeg_outputs.append(tmp)
                    if tmp.suffix.lower() == ".jpg":
                        tmp.write_bytes(b"converted jpeg")
                        return subprocess.CompletedProcess(args, 0)
                    return subprocess.CompletedProcess(args, 1)
                if args[0] == "cupidobj":
                    object_tool_commands.append(args)
                    Path(args[-1]).write_bytes(b"object")
                    return subprocess.CompletedProcess(args, 0)
                raise AssertionError(f"unexpected command: {args}")

            with mock.patch("tools.hostbuild.shutil.which", side_effect=fake_which), \
                mock.patch("tools.hostbuild.subprocess.run", side_effect=fake_run):
                hostbuild.embed_jpeg("cupidobj", src, out)

            self.assertEqual(ffmpeg_outputs[0].suffix.lower(), ".jpg")
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


if __name__ == "__main__":
    unittest.main()

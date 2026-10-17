import argparse
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from tools import hostbuild


class StagePathTests(unittest.TestCase):
    def test_windows_absolute_source_preserves_drive_colon(self):
        source = "C:/a"
        stage = hostbuild._parse_stage(source + ":/b")
        self.assertEqual(stage.source, Path(source))
        self.assertEqual(stage.dest, "/b")

    def test_windows_source_spellings_preserve_complete_path(self):
        for source in (
            "D:/Cupid assets/café-日本語-" + chr(0x1F600) + ".wad",
            r"d:\Cupid assets\freedoom1.wad",
            "D:freedoom1.wad",
            r"\\server\share\freedoom1.wad",
        ):
            with self.subTest(source=source):
                stage = hostbuild._parse_stage(source + ":/wads/freedoom1.wad")
                self.assertEqual(stage.source, Path(source))
                self.assertEqual(stage.dest, "/wads/freedoom1.wad")

    def test_existing_sources_and_destination_colon_keep_their_meaning(self):
        for source in ("third_party/freedoom/freedoom1.wad", "/tmp/freedoom1.wad", "a.wad"):
            with self.subTest(source=source):
                stage = hostbuild._parse_stage(source + ":/guest:edition")
                self.assertEqual(stage.source, Path(source))
                self.assertEqual(stage.dest, "/guest:edition")

    def test_one_letter_source_keeps_existing_separator(self):
        stage = hostbuild._parse_stage("a:/guest")
        self.assertEqual(stage.source, Path("a"))
        self.assertEqual(stage.dest, "/guest")

    def test_missing_separator_is_rejected(self):
        with self.assertRaisesRegex(argparse.ArgumentTypeError, "SRC:/guest/path"):
            hostbuild._parse_stage("freedoom1.wad")

    def test_relative_guest_destination_is_rejected(self):
        for value in ("freedoom1.wad:guest", "C:/freedoom1.wad:guest", r"C:\freedoom1.wad:guest"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(argparse.ArgumentTypeError, "destination must start with /"):
                    hostbuild._parse_stage(value)

    def test_image_and_stage_commands_share_the_path_parser(self):
        source = "C:/Cupid assets/freedoom1.wad"
        expected = [hostbuild.StageFile(Path(source), "/wads/freedoom1.wad")]
        with mock.patch.object(hostbuild, "stage_files") as stage:
            self.assertEqual(hostbuild.main([
                "stage", "--image", "private.img", "--fat-start-lba", "20480",
                source + ":/wads/freedoom1.wad",
            ]), 0)
            self.assertEqual(stage.call_args.args[2], expected)
        with mock.patch.object(hostbuild, "create_or_update_image") as image:
            self.assertEqual(hostbuild.main([
                "image", "--image", "private.img", "--bootloader", "boot.bin", "--kernel", "kernel.bin",
                "--seed-manifest", "manifest.json", "--hdd-mb", "200", "--fat-start-lba", "20480",
                "--stage", source + ":/wads/freedoom1.wad",
            ]), 0)
            self.assertEqual(image.call_args.args[5], expected)

    def test_cli_stages_unicode_source_and_rejects_bad_destination_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / ("caf" + chr(0xE9) + "-" + chr(0x65E5) + "-" + chr(0x1F600) + ".bin")
            payload = bytes(range(256)) * 7
            source.write_bytes(payload)
            image = root / "private.img"
            image_sectors = 16384
            layout = hostbuild._choose_layout(image_sectors - 1)
            with image.open("w+b") as stream:
                stream.truncate(image_sectors * hostbuild.SECTOR_SIZE)
                hostbuild._write_fat16_filesystem(stream, 1, layout)
            command = ["stage", "--image", str(image), "--fat-start-lba", "1"]
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(hostbuild.main(command + [source.as_posix() + ":/payload.bin"]), 0)

            # Read the directory and cluster chain independently of the writer.
            data = image.read_bytes()
            boot = data[512:1024]
            self.assertEqual(struct.unpack_from("<H", boot, 11)[0], 512)
            reserved = struct.unpack_from("<H", boot, 14)[0]
            sectors_per_fat = struct.unpack_from("<H", boot, 22)[0]
            root_entries = struct.unpack_from("<H", boot, 17)[0]
            fat_offset = (1 + reserved) * 512
            root_offset = (1 + reserved + boot[16] * sectors_per_fat) * 512
            root_bytes = data[root_offset:root_offset + root_entries * 32]
            entries = [root_bytes[offset:offset + 32] for offset in range(0, len(root_bytes), 32)
                       if root_bytes[offset:offset + 11] == b"PAYLOAD BIN"]
            self.assertEqual(len(entries), 1)
            entry = entries[0]
            self.assertEqual(entry[11], 0x20)
            self.assertEqual(struct.unpack_from("<I", entry, 28)[0], len(payload))
            cluster = struct.unpack_from("<H", entry, 26)[0]
            cluster_bytes = boot[13] * 512
            data_offset = root_offset + ((root_entries * 32 + 511) // 512) * 512
            observed = bytearray()
            visited = set()
            while cluster < 0xFFF8:
                self.assertGreaterEqual(cluster, 2)
                self.assertNotIn(cluster, visited)
                visited.add(cluster)
                offset = data_offset + (cluster - 2) * cluster_bytes
                observed.extend(data[offset:offset + cluster_bytes])
                cluster = struct.unpack_from("<H", data, fat_offset + cluster * 2)[0]
            self.assertEqual(bytes(observed[:len(payload)]), payload)
            self.assertEqual(len(visited), (len(payload) + cluster_bytes - 1) // cluster_bytes)

            previous = image.read_bytes()
            diagnostic = io.StringIO()
            with contextlib.redirect_stderr(diagnostic), self.assertRaises(SystemExit) as failure:
                hostbuild.main(command + [source.as_posix() + ":relative"])
            self.assertEqual(failure.exception.code, 2)
            self.assertIn("destination must start with /", diagnostic.getvalue())
            self.assertEqual(image.read_bytes(), previous)
            self.assertEqual(source.read_bytes(), payload)


if __name__ == "__main__":
    unittest.main()

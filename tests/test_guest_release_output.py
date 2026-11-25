from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bootstrap" / "guest"))

import check_guest_kernel_plan as replay


class GuestReleaseOutputTests(unittest.TestCase):
    def test_export_uses_compared_bytes_only_after_boot(self):
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            destination = scratch / "cupidos.img"
            previous = b"previous checked image"
            checked = b"image read back from HomeFS"
            destination.write_bytes(previous)
            args = (scratch, scratch, scratch / "work.img", "qemu", 10, {})
            kwargs = {"image_mib": 200, "release_output": destination}
            products = {"cupidos.img": checked}
            with (patch.object(replay, "run"), patch.object(replay, "write_reference_disk"),
                  patch.object(replay, "read_homefs"),
                  patch.object(replay, "compare_homefs_files", return_value=products)):
                with patch.object(replay, "boot_disk", side_effect=RuntimeError("boot failed")):
                    with self.assertRaisesRegex(RuntimeError, "boot failed"):
                        replay.execute_kernel_plan(*args, **kwargs)
                self.assertEqual(destination.read_bytes(), previous)

                def check_boot_order(media, contents, smoke, log, *, complete):
                    self.assertEqual(destination.read_bytes(), previous)
                    self.assertEqual(contents, checked)
                    self.assertTrue(complete)

                with patch.object(replay, "boot_disk", side_effect=check_boot_order):
                    replay.execute_kernel_plan(*args, **kwargs)

            self.assertEqual(destination.read_bytes(), checked)
            self.assertEqual(list(scratch.iterdir()), [destination])

    def test_failed_image_writeback_keeps_previous_release(self):
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            destination = scratch / "cupidos.img"
            previous = b"previous checked image"
            destination.write_bytes(previous)
            with patch.object(replay.os, "fsync", side_effect=OSError("writeback failed")):
                with self.assertRaisesRegex(OSError, "writeback failed"):
                    replay.save_checked_disk(destination, b"new checked image")
            self.assertEqual(destination.read_bytes(), previous)
            self.assertEqual(list(scratch.iterdir()), [destination])


if __name__ == "__main__":
    unittest.main()

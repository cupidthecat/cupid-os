from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bootstrap" / "guest"))

import check_guest_kernel_plan as replay
import check_guest_release_runtime as runtime
import check_guest_source_plan as plan
from tools.hostbuild_disk import StageFile


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


class GuestReleaseInputsTests(unittest.TestCase):
    def test_release_assets_stay_on_the_working_fat_volume(self):
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory)
            source = scratch / "main.cc"
            source.write_bytes(b"void main() {}\n")
            iso = scratch / "hello.iso"
            iso.write_bytes(b"ISO staged on FAT only")
            wad = scratch / "freedoom1.wad"
            wad.write_bytes(b"WAD staged on FAT only")
            assets = (StageFile(iso, "/hello.iso"), StageFile(wad, "/wads/freedoom1.wad"))
            image = scratch / "working.img"
            jobs = {Path("cupidbuild.job"): b"project-kernel\nkernel.job\n"}

            with patch.object(plan, "create_disk") as create:
                plan.stage_installed_tree(scratch, scratch, image, (), (Path("main.cc"),),
                                          image_assets=assets, native_plan=jobs)

            manifest = scratch / "stage-expected"
            manifest.write_bytes(b"/disk/release/a0\n/hello.iso\n/disk/release/a1\n/wads/freedoom1.wad\n")
            payload = (scratch / "homefs.sys").read_bytes()
            replay.compare_homefs_files(payload, {
                "project/main.cc": source, "project/disk-assets": manifest,
            })
            self.assertNotIn(iso.read_bytes(), payload)
            self.assertNotIn(wad.read_bytes(), payload)
            self.assertEqual(create.call_args.args[-2:], (f"{iso}:/release/a0", f"{wad}:/release/a1"))


class GuestReleaseRuntimeTests(unittest.TestCase):
    def test_doom_reports_missing_wad_as_failure(self):
        with patch.object(runtime, "run") as run:
            runtime.check_doom(Path("cupidos.img"), ("smoke",), Path("build"))
        command = run.call_args.args
        failure = command[command.index("--failure-pattern") + 1]
        success = command[command.index("--success-pattern") + 1]
        opened_wrong_path = "IWAD file '/disk/was/freedo~1.wad' not found!\n[doom] returned to shell"
        completed = "timed 7117 gametics in 19179 realtics (12.987904 fps)\n[doom] returned to shell"
        self.assertRegex(opened_wrong_path, failure)
        self.assertNotRegex(opened_wrong_path, re.compile(success, re.S))
        self.assertRegex(completed, re.compile(success, re.S))
        self.assertNotRegex(completed, failure)


if __name__ == "__main__":
    unittest.main()

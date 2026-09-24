import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "usb_msc_lifetime_contract.c"


class UsbMscLifetimeTests(unittest.TestCase):
    def test_disconnect_releases_state_after_the_last_registry_reference(self):
        compiler = shlex.split(
            os.environ.get("CC", "clang" if os.name == "nt" else "cc")
        )
        with tempfile.TemporaryDirectory(
            prefix="cupid-usb-msc-lifetime-"
        ) as build_dir:
            executable = Path(build_dir) / (
                "usb-msc-lifetime.exe" if os.name == "nt"
                else "usb-msc-lifetime"
            )
            command = compiler + [
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-pointer-to-int-cast",
                "-Wno-int-to-pointer-cast",
                f"-I{ROOT / 'kernel'}",
                f"-I{ROOT / 'kernel' / 'core'}",
                f"-I{ROOT / 'kernel' / 'cpu'}",
                f"-I{ROOT / 'kernel' / 'fs'}",
                f"-I{ROOT / 'kernel' / 'mm'}",
                f"-I{ROOT / 'kernel' / 'usb'}",
                f"-I{ROOT / 'drivers'}",
                str(FIXTURE),
                "-o",
                str(executable),
            ]
            built = subprocess.run(
                command,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(
                built.returncode,
                0,
                built.stdout + built.stderr,
            )
            exercised = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(
                exercised.returncode,
                0,
                (
                    "MSC lifetime fixture failed with case "
                    f"{exercised.returncode}\n"
                    f"{exercised.stdout}{exercised.stderr}"
                ),
            )


if __name__ == "__main__":
    unittest.main()

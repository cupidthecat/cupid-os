import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "usb_reconciliation_runtime.c"


class UsbReconciliationRuntimeTests(unittest.TestCase):
    def test_real_core_retains_work_and_reuses_addresses(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("a host C compiler is required for this contract")

        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "usb-reconciliation"
            if Path(compiler).name.lower().startswith("clang"):
                if Path(compiler).suffix.lower() == ".exe":
                    executable = executable.with_suffix(".exe")
            command = [
                compiler,
                "-std=gnu11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-gnu-zero-variadic-macro-arguments",
                "-Ikernel",
                "-Ikernel/core",
                "-Ikernel/cpu",
                "-Ikernel/mm",
                "-Ikernel/usb",
                "-Idrivers",
                str(FIXTURE),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(
                command,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)

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
                    "USB reconciliation fixture failed with case "
                    f"{exercised.returncode}\n{exercised.stderr}"
                ),
            )


if __name__ == "__main__":
    unittest.main()

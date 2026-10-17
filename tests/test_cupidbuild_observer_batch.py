"""Retained batch behavior, using native or supplied checked callers."""
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CupidBuildObserverBatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        selected = os.environ.get("CUPIDBUILD_OBSERVER_BATCH_PROGRAM")
        if selected:
            cls.program = Path(selected).resolve()
            return
        temporary = tempfile.TemporaryDirectory(prefix="observer-batch-build-")
        cls.addClassCleanup(temporary.cleanup)
        cls.program = Path(temporary.name) / ("caller.exe" if os.name == "nt" else "caller")
        command = [shutil.which("clang" if os.name == "nt" else "cc"), "-std=c11", "-O2",
                   "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
                   "-I", str(ROOT / "toolchain"), "-x", "c",
                   str(ROOT / "toolchain/tests/cupidbuild_observer_batch_contract.cc"),
                   str(ROOT / "toolchain/cupidbuild_host.cc"),
                   str(ROOT / "toolchain/path_encoding.cc"),
                   *(["-lntdll"] if os.name == "nt" else []), "-o", str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors="replace"))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="observer-batch-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "nested").mkdir()
        (self.root / "directory").mkdir()
        (self.root / "first").write_bytes(b"one")
        (self.root / "nested/second").write_bytes(b"three")

    def test_success_partial_poison_limits_and_invalid_arguments(self):
        for case in ("valid", "partial", "empty", "limit", "null-paths", "null-results", "null-observer"):
            with self.subTest(case=case):
                result = subprocess.run([str(self.program), str(self.root), case], capture_output=True, timeout=30)
                self.assertEqual((result.returncode, result.stdout, result.stderr), (0, b"", b""))

    def test_successful_batch_retains_final_drift_checks(self):
        process = subprocess.Popen([str(self.program), str(self.root), "drift"],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        messages = queue.Queue()
        threading.Thread(target=lambda: messages.put(process.stdout.readline()), daemon=True).start()
        try:
            self.assertEqual(messages.get(timeout=30).replace(b"\r\n", b"\n"), b"ready\n")
            (self.root / "nested/second").write_bytes(b"longer")
            stdout, stderr = process.communicate(b"x", timeout=30)
            self.assertEqual((process.returncode, stdout, stderr), (0, b"", b""))
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

if __name__ == "__main__":
    unittest.main()

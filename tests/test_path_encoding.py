import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_artifact_size_policy_contract import _host_compiler


ROOT = Path(__file__).resolve().parents[1]


class PathEncodingTests(unittest.TestCase):
    def test_scalar_boundaries_malformed_spans_and_output_capacity(self):
        with tempfile.TemporaryDirectory(prefix="cupid-path-encoding-") as temporary:
            executable = Path(temporary) / ("codec.exe" if os.name == "nt" else "codec")
            command = [
                _host_compiler(), "-std=c11", "-O2", "-Wall", "-Wextra",
                "-Werror", "-I", str(ROOT / "toolchain"), "-x", "c",
                str(ROOT / "toolchain/path_encoding.cc"),
                str(ROOT / "toolchain/tests/path_encoding_contract.cc"),
                "-o", str(executable),
            ]
            built = subprocess.run(command, capture_output=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(executable)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout, b"")
            self.assertEqual(result.stderr, b"")


if __name__ == "__main__":
    unittest.main()

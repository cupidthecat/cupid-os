import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


def _contract_path():
    suffix = ".exe" if os.name == "nt" else ""
    return TOOLCHAIN_ROOT / "build" / ("core-contract" + suffix)


class ToolchainCoreContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        result = subprocess.run(
            ["make", "-C", str(TOOLCHAIN_ROOT), "clean", "all"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            raise AssertionError(
                "toolchain contract build failed\n"
                + result.stdout
                + result.stderr
            )

    def _run_contract(self, mode, root=None):
        command = [str(_contract_path()), mode]
        if root is not None:
            command.append(str(root))
        return subprocess.run(command, text=True, capture_output=True)

    def test_arena_and_buffer_contract(self):
        result = self._run_contract("foundations")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "foundations: ok\n")

    def test_logical_path_contract(self):
        result = self._run_contract("paths")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "paths: ok\n")

    def test_diagnostic_contract(self):
        result = self._run_contract("diagnostics")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "diagnostics: ok\n")
        self.assertEqual(
            result.stderr,
            "/src/main.c:4:7: warning CT1000001: first warning\n"
            "/src/main.c:5:2: error CT1000002: then error\n",
        )

    def test_real_host_file_store_contract(self):
        with tempfile.TemporaryDirectory() as td:
            (Path(td) / "nested").mkdir()
            result = self._run_contract("host-io", Path(td))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "host-io: ok\n")
            self.assertEqual(
                (Path(td) / "nested" / "roundtrip.bin").read_bytes(),
                b"Cupid toolchain core\x00\x7f\xff",
            )

    def test_real_host_file_store_reports_missing_input(self):
        with tempfile.TemporaryDirectory() as td:
            result = self._run_contract("missing", Path(td))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "missing: ok\n")

    def test_checked_failure_contract(self):
        result = self._run_contract("limits")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "limits: ok\n")

    def test_invocation_commits_only_successful_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "nested").mkdir()
            (root / "input.txt").write_bytes(b"source")
            (root / "nested" / "error.bin").write_bytes(b"keep")
            result = self._run_contract("invocation", root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "invocation: ok\n")
            self.assertEqual(
                result.stderr,
                "/input.txt:1:1: error CT1000003: deliberate failure\n",
            )
            self.assertEqual(
                (root / "nested" / "success.bin").read_bytes(),
                b"source:built",
            )
            self.assertEqual(
                (root / "nested" / "error.bin").read_bytes(), b"keep"
            )


if __name__ == "__main__":
    unittest.main()

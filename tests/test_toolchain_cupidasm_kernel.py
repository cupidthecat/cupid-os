import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class CupidAsmKernelElfContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build_path = Path(
            tempfile.mkdtemp(prefix=".cupidasm-kernel-build-", dir=TOOLCHAIN_ROOT)
        )
        relative_build = cls.build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = cls.build_path / (
            "cupidasm-kernel-elf-contract" + suffix
        )
        target = relative_build + "/cupidasm-kernel-elf-contract" + suffix
        result = subprocess.run(
            ["make", "-C", str(TOOLCHAIN_ROOT), f"BUILD_DIR={relative_build}", target],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            raise AssertionError(
                "CupidASM kernel ELF contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.build_path, ignore_errors=True)

    def _run(self, mode):
        return subprocess.run(
            [str(self.contract_path), mode],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

    def test_code_only_executable_layout(self):
        result = self._run("code-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "code-only: ok\n")

    def test_code_data_and_bss_executable_layout(self):
        result = self._run("code-data-bss")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "code-data-bss: ok\n")

    def test_malformed_artifacts_roll_back_output(self):
        result = self._run("errors")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "errors: ok\n")

    def test_kernel_adapter_returns_mixed_mode_raw_bytes_and_map(self):
        result = self._run("artifact-raw")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "artifact-raw: ok\n")

    def test_kernel_adapter_returns_unlinked_relocatable_object(self):
        result = self._run("artifact-relocatable")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "artifact-relocatable: ok\n")

    def test_kernel_artifact_request_and_command_errors_recover(self):
        result = self._run("artifact-errors")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "artifact-errors: ok\n")

    def test_kernel_artifact_pair_publication_preserves_old_outputs(self):
        result = self._run("artifact-publication")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "artifact-publication: ok\n")


if __name__ == "__main__":
    unittest.main()

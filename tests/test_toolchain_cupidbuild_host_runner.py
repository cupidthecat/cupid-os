import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class CupidBuildHostRunnerContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidbuild-host-runner-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / (
            "cupidbuild-host-runner-contract" + suffix
        )
        target = (
            relative_build.as_posix()
            + "/cupidbuild-host-runner-contract"
            + suffix
        )
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build.as_posix()}",
                target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidBuild host runner contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def _run_contract(self, mode, close_stdout=False, close_stderr=False):
        if os.name == "nt":
            if close_stdout or close_stderr:
                raise AssertionError("closed inherited streams require POSIX")
            return subprocess.run(
                [str(self.contract_path), mode],
                cwd=self._build_directory.name,
                text=True,
                capture_output=True,
                timeout=120,
            )
        else:
            with tempfile.TemporaryDirectory(
                prefix="cupidbuild-host-runner-native-"
            ) as native_directory:
                native_contract = Path(native_directory) / self.contract_path.name
                shutil.copy2(self.contract_path, native_contract)
                native_contract.chmod(0o700)

                def close_inherited_streams():
                    if close_stdout:
                        os.close(1)
                    if close_stderr:
                        os.close(2)

                return subprocess.run(
                    [str(native_contract), mode],
                    cwd=native_directory,
                    text=True,
                    capture_output=True,
                    timeout=120,
                    preexec_fn=(
                        close_inherited_streams
                        if close_stdout or close_stderr
                        else None
                    ),
                )

    def _assert_capture_with_closed_streams(
        self, close_stdout=False, close_stderr=False
    ):
        result = self._run_contract(
            "capture-no-forward",
            close_stdout=close_stdout,
            close_stderr=close_stderr,
        )

        self.assertEqual(result.returncode, 0)
        self.assertEqual((result.stdout, result.stderr), ("", ""))

    def test_frozen_host_runner_boundaries(self):
        result = self._run_contract("all")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "cupidbuild host runner contract: ok\n",
        )
        self.assertEqual(result.stderr, "")

    def test_stdout_limit_is_forwarded_exactly(self):
        result = self._run_contract("stdout-4096-forward")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "x" * 4096)
        self.assertEqual(result.stderr, "")

    def test_stdout_and_stderr_are_forwarded_exactly(self):
        result = self._run_contract("stream-pair-forward")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            (result.stdout, result.stderr),
            ("runner stdout\n", "runner stderr\n"),
        )

    def test_buffered_prefixes_precede_forwarded_streams(self):
        result = self._run_contract("stream-prefix-forward")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            (result.stdout, result.stderr),
            (
                "caller stdout prefix\nrunner stdout\n",
                "caller stderr prefix\nrunner stderr\n",
            ),
        )

    @unittest.skipIf(os.name == "nt", "requires native POSIX fault injection")
    def test_native_posix_launch_protocol_retries_eintr(self):
        result = self._run_contract("native-posix-eintr")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "cupidbuild host runner EINTR contract: ok\n",
        )
        self.assertEqual(result.stderr, "")

    @unittest.skipIf(os.name == "nt", "requires POSIX descriptor inheritance")
    def test_capture_succeeds_with_inherited_stdout_closed(self):
        self._assert_capture_with_closed_streams(close_stdout=True)

    @unittest.skipIf(os.name == "nt", "requires POSIX descriptor inheritance")
    def test_capture_succeeds_with_inherited_stderr_closed(self):
        self._assert_capture_with_closed_streams(close_stderr=True)

    @unittest.skipIf(os.name == "nt", "requires POSIX descriptor inheritance")
    def test_capture_succeeds_with_both_inherited_streams_closed(self):
        self._assert_capture_with_closed_streams(
            close_stdout=True,
            close_stderr=True,
        )


if __name__ == "__main__":
    unittest.main()

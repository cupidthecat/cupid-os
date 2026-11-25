## @file
#  @brief Check WSL invocation, process outcomes, and input drift.
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.bootstrap_toolchain import (
    BootstrapError,
    ToolRunner,
    WSL_PRIVATE_RUN_SCRIPT,
    _remove_private_tool_directory,
    freeze_seed_inputs,
    main as bootstrap_main,
    run_seed_tool,
)

from tests.bootstrap_seed_suite_fixtures import (
    BOOTSTRAP_TOOL,
    REPO_ROOT,
    SEED_MANIFEST,
)


class BootstrapSeedRunnerCases:
    def test_wsl_runner_uses_a_private_temporary_directory(self):
        self.assertIn("umask 077", WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn("mktemp -d", WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn('chmod 700 "$private"', WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn('probe="$private/tool"', WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn('rm -rf -- "$private"', WSL_PRIVATE_RUN_SCRIPT)
        self.assertNotIn("$$", WSL_PRIVATE_RUN_SCRIPT)

    def test_tool_runner_reads_only_the_executable_format_signature(self):
        reads: list[int] = []
        signature = b"MZ\0\0" if os.name == "nt" else b"\x7fELF"

        class SignatureReader(io.BytesIO):
            def read(self, size: int = -1) -> bytes:
                reads.append(size)
                return super().read(size)

        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-format-probe-"
        ) as temporary:
            root = Path(temporary)
            executable = root / "checked-tool"
            executable.write_bytes(signature + b"fixture payload")
            runner = ToolRunner(root)

            with mock.patch.object(
                Path,
                "read_bytes",
                side_effect=AssertionError("whole-file read is forbidden"),
            ), mock.patch.object(
                Path,
                "open",
                side_effect=lambda *_args, **_kwargs: SignatureReader(
                    signature
                ),
            ), mock.patch(
                "tools.bootstrap_toolchain.subprocess.run",
                return_value=subprocess.CompletedProcess([], 0, "", ""),
            ):
                self.assertEqual(
                    runner.display_argument(executable, "--version"),
                    "--version",
                )
                result = runner.run(executable, ["--version"], timeout=1)

        self.assertEqual(result.returncode, 0)
        self.assertEqual(reads, [4, 4])

    @unittest.skipUnless(os.name == "nt", "Windows WSL resolution")
    def test_wsl_runner_uses_the_system_copy_with_a_poisoned_path(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-wsl-system-root-"
        ) as temporary:
            system_root = Path(temporary)
            executable = system_root / "System32" / "wsl.exe"
            executable.parent.mkdir()
            executable.write_bytes(b"fixture")
            with mock.patch(
                "tools.bootstrap_toolchain.shutil.which",
                return_value=str(Path(temporary) / "poison" / "wsl.exe"),
            ), mock.patch.dict(
                os.environ, {"SystemRoot": str(system_root)}, clear=False
            ):
                self.assertEqual(
                    ToolRunner._wsl_command(), str(executable)
                )

    @unittest.skipUnless(os.name == "nt", "Windows WSL resolution")
    def test_wsl_runner_rejects_a_missing_system_copy(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-wsl-missing-root-"
        ) as temporary, mock.patch(
            "tools.bootstrap_toolchain.shutil.which",
            return_value=str(Path(temporary) / "poison" / "wsl.exe"),
        ), mock.patch.dict(
            os.environ, {"SystemRoot": temporary}, clear=False
        ), self.assertRaisesRegex(
            BootstrapError,
            "^WSL is required to run the i386 Linux seed on Windows$",
        ):
            ToolRunner._wsl_command()

    def test_checked_seed_run_rejects_a_changed_tool_before_execution(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-run-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            assembler = copied_seed / "cupidasm.elf"
            image = bytearray(assembler.read_bytes())
            image[-1] ^= 0x01
            assembler.write_bytes(image)

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "run",
                    "--manifest",
                    str(copied_seed / "manifest.json"),
                    "--root",
                    str(REPO_ROOT),
                    "--tool",
                    "cupidasm",
                    "--",
                    "--help",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "checked seed tool failed: SHA-256 differs for cupidasm.elf\n",
        )

    def test_checked_seed_run_rejects_live_tool_drift_after_execution(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-run-drift-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            assembler = copied_seed / "cupidasm.elf"

            def mutate_live_seed(*_arguments, **_keywords):
                image = bytearray(assembler.read_bytes())
                image[-1] ^= 0x01
                assembler.write_bytes(image)
                return subprocess.CompletedProcess(
                    ["cupidasm", "--help"],
                    0,
                    "usage: cupidasm\n",
                    "",
                )

            with mock.patch(
                "tools.bootstrap_toolchain.ToolRunner.run",
                side_effect=mutate_live_seed,
            ):
                with self.assertRaisesRegex(
                    BootstrapError,
                    "checked seed inputs changed while CupidASM ran: "
                    "SHA-256 differs for cupidasm.elf",
                ):
                    run_seed_tool(
                        copied_seed / "manifest.json",
                        REPO_ROOT,
                        "cupidasm",
                        ("--help",),
                    )

    def test_checked_seed_run_uses_an_injected_runner_and_rechecks_the_seed(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-injected-runner-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            live_assembler = copied_seed / "cupidasm.elf"

            class DriftingRunner:
                def __init__(self):
                    self.calls = []

                def run(self, executable, arguments, timeout):
                    self.calls.append(
                        (executable, tuple(arguments), timeout)
                    )
                    image = bytearray(live_assembler.read_bytes())
                    image[-1] ^= 0x01
                    live_assembler.write_bytes(image)
                    return subprocess.CompletedProcess(
                        ["cupidasm", "--help"],
                        0,
                        "usage: cupidasm\n",
                        "",
                    )

            runner = DriftingRunner()
            with self.assertRaisesRegex(
                BootstrapError,
                "checked seed inputs changed while CupidASM ran: "
                "SHA-256 differs for cupidasm.elf",
            ):
                run_seed_tool(
                    copied_seed / "manifest.json",
                    REPO_ROOT,
                    "cupidasm",
                    ("--help",),
                    timeout=12,
                    runner=runner,
                )

            self.assertEqual(len(runner.calls), 1)
            executable, arguments, timeout = runner.calls[0]
            self.assertNotEqual(executable, live_assembler)
            self.assertEqual(executable.name, "cupidasm.elf")
            self.assertEqual(arguments, ("--help",))
            self.assertEqual(timeout, 12)

    def test_checked_seed_run_rechecks_unselected_seed_tools(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-cohort-drift-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            live_linker = copied_seed / "cupidld.elf"

            class DriftingRunner:
                def run(self, _executable, _arguments, _timeout):
                    image = bytearray(live_linker.read_bytes())
                    image[-1] ^= 0x01
                    live_linker.write_bytes(image)
                    return subprocess.CompletedProcess(
                        ["cupidasm", "--help"],
                        0,
                        "usage: cupidasm\n",
                        "",
                    )

            with self.assertRaisesRegex(
                BootstrapError,
                "checked seed inputs changed while CupidASM ran: "
                "SHA-256 differs for cupidld.elf",
            ):
                run_seed_tool(
                    copied_seed / "manifest.json",
                    REPO_ROOT,
                    "cupidasm",
                    ("--help",),
                    runner=DriftingRunner(),
                )

    def test_checked_seed_run_rejects_live_manifest_byte_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-manifest-drift-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            live_manifest = copied_seed / "manifest.json"

            class DriftingRunner:
                def run(self, _executable, _arguments, _timeout):
                    live_manifest.write_bytes(
                        live_manifest.read_bytes() + b"\n"
                    )
                    return subprocess.CompletedProcess(
                        ["cupidasm", "--help"],
                        0,
                        "usage: cupidasm\n",
                        "",
                    )

            with self.assertRaisesRegex(
                BootstrapError,
                "checked seed inputs changed while CupidASM ran: "
                "manifest content differs",
            ):
                run_seed_tool(
                    live_manifest,
                    REPO_ROOT,
                    "cupidasm",
                    ("--help",),
                    runner=DriftingRunner(),
                )

    def test_checked_seed_run_uses_the_supplied_frozen_capture(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-run-frozen-"
        ) as temporary:
            root = Path(temporary)
            copied_seed = root / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            frozen = freeze_seed_inputs(
                copied_seed / "manifest.json",
                root / "frozen",
            )
            completed = subprocess.CompletedProcess(
                ["cupidasm", "--help"],
                0,
                "usage: cupidasm\n",
                "",
            )
            with (
                mock.patch(
                    "tools.bootstrap_toolchain.ToolRunner"
                ) as runner_type,
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_seed_inputs"
                ) as freeze_inputs,
            ):
                runner_type.return_value.run.return_value = completed
                result = run_seed_tool(
                    copied_seed / "manifest.json",
                    REPO_ROOT,
                    "cupidasm",
                    ("--help",),
                    timeout=12,
                    frozen_seed=frozen,
                )

            self.assertIs(result, completed)
            freeze_inputs.assert_not_called()
            runner_type.return_value.run.assert_called_once_with(
                frozen.tools["cupidasm"],
                ("--help",),
                12,
            )

    def test_checked_seed_run_maps_timeout_and_invalid_requests(self):
        with mock.patch(
            "tools.bootstrap_toolchain.ToolRunner"
        ) as runner_type:
            runner_type.return_value.run.side_effect = (
                subprocess.TimeoutExpired(["cupidasm"], 1)
            )
            with self.assertRaisesRegex(
                BootstrapError,
                "CupidASM timed out after 1 second",
            ):
                run_seed_tool(
                    SEED_MANIFEST,
                    REPO_ROOT,
                    "cupidasm",
                    (),
                    timeout=1,
                )

        with self.assertRaisesRegex(
            BootstrapError,
            "tool timeout must be positive",
        ):
            run_seed_tool(
                SEED_MANIFEST,
                REPO_ROOT,
                "cupidasm",
                (),
                timeout=0,
            )
        with self.assertRaisesRegex(
            BootstrapError,
            "checked seed has no tool named unknown",
        ):
            run_seed_tool(
                SEED_MANIFEST,
                REPO_ROOT,
                "unknown",
                (),
            )

    def test_private_tool_cleanup_retries_only_windows_sharing_violations(self):
        def sharing_violation() -> OSError:
            error = OSError("private executable remains locked")
            error.winerror = 32
            return error

        with tempfile.TemporaryDirectory(
            prefix="cupid-private-tool-cleanup-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            transient = root / "transient"
            transient.mkdir()
            (transient / "cupidc.exe").write_bytes(b"MZ")
            real_rmtree = shutil.rmtree
            attempts = 0

            def remove_after_two_locks(path):
                nonlocal attempts
                attempts += 1
                if attempts <= 2:
                    raise sharing_violation()
                return real_rmtree(path)

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.rmtree",
                    side_effect=remove_after_two_locks,
                ),
                mock.patch("tools.bootstrap_toolchain.time.sleep") as pause,
            ):
                _remove_private_tool_directory(transient)

            self.assertEqual(attempts, 3)
            self.assertFalse(transient.exists())
            self.assertEqual(pause.call_count, 2)

            unrelated = root / "unrelated"
            unrelated.mkdir()
            unrelated_error = OSError("unrelated cleanup failure")
            unrelated_error.winerror = 5
            with (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.rmtree",
                    side_effect=unrelated_error,
                ) as remove,
                mock.patch("tools.bootstrap_toolchain.time.sleep") as pause,
                self.assertRaisesRegex(OSError, "unrelated cleanup failure"),
            ):
                _remove_private_tool_directory(unrelated)
            remove.assert_called_once_with(unrelated)
            pause.assert_not_called()
            real_rmtree(unrelated)

            persistent = root / "persistent"
            persistent.mkdir()
            with (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.rmtree",
                    side_effect=sharing_violation(),
                ) as remove,
                mock.patch("tools.bootstrap_toolchain.time.sleep") as pause,
                self.assertRaisesRegex(OSError, "private executable remains locked"),
            ):
                _remove_private_tool_directory(persistent)
            self.assertEqual(remove.call_count, 41)
            self.assertEqual(pause.call_count, 40)
            real_rmtree(persistent)

    def test_private_tool_cleanup_preserves_native_tool_failures(self):
        def sharing_violation() -> OSError:
            error = OSError("private executable remains locked")
            error.winerror = 32
            return error

        with tempfile.TemporaryDirectory(
            prefix="cupid-private-tool-failure-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            executable = root / "cupidc.exe"
            executable.write_bytes(b"MZ" if os.name == "nt" else b"\x7fELF")
            runner = ToolRunner(root)

            timeout_stage = root / "timeout-stage"
            timeout_stage.mkdir()
            timeout_error = subprocess.TimeoutExpired(["cupidc"], 1)
            with (
                mock.patch(
                    "tools.bootstrap_toolchain.tempfile.mkdtemp",
                    return_value=str(timeout_stage),
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.subprocess.run",
                    side_effect=timeout_error,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._remove_private_tool_directory",
                    side_effect=sharing_violation(),
                ) as remove,
                self.assertRaises(subprocess.TimeoutExpired) as raised,
            ):
                runner.run(executable, (), timeout=1)
            remove.assert_called_once_with(timeout_stage)
            if hasattr(raised.exception, "add_note"):
                self.assertIn(
                    "private checked-tool cleanup also failed",
                    "\n".join(raised.exception.__notes__),
                )
            shutil.rmtree(timeout_stage)

            launch_stage = root / "launch-stage"
            launch_stage.mkdir()
            with (
                mock.patch(
                    "tools.bootstrap_toolchain.tempfile.mkdtemp",
                    return_value=str(launch_stage),
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.subprocess.run",
                    side_effect=OSError("native tool launch failed"),
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._remove_private_tool_directory"
                ) as remove,
                self.assertRaisesRegex(OSError, "native tool launch failed"),
            ):
                runner.run(executable, (), timeout=1)
            remove.assert_not_called()
            self.assertFalse(launch_stage.exists())

    def test_checked_seed_run_forwards_exact_tool_streams_and_status(self):
        completed = subprocess.CompletedProcess(
            ["cupiddis", "--bad"],
            7,
            "tool stdout\n",
            "tool stderr\n",
        )
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            mock.patch(
                "tools.bootstrap_toolchain.run_seed_tool",
                return_value=completed,
            ),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            status = bootstrap_main(
                [
                    "run",
                    "--manifest",
                    str(SEED_MANIFEST),
                    "--root",
                    str(REPO_ROOT),
                    "--tool",
                    "cupiddis",
                    "--",
                    "--bad",
                ]
            )

        self.assertEqual(status, 7)
        self.assertEqual(stdout.getvalue(), "tool stdout\n")
        self.assertEqual(stderr.getvalue(), "tool stderr\n")

    def test_wsl_path_preserves_utf8_with_a_non_utf8_host_default(self):
        expected = "/mnt/c/caf\u00e9-\u65e5\u672c\u8a9e-\U0001f600"

        def translate(command, **options):
            payload = (expected + "\n").encode("utf-8")
            return subprocess.CompletedProcess(
                command, 0, payload.decode(options.get("encoding", "cp1252")), ""
            )

        with mock.patch.object(ToolRunner, "_wsl_command", return_value="wsl"), \
                mock.patch.object(subprocess, "run", side_effect=translate):
            self.assertEqual(
                ToolRunner(REPO_ROOT)._wsl_argument(Path("caf\u00e9-\u65e5\u672c\u8a9e-\U0001f600")),
                expected,
            )

    def test_wsl_path_rejects_translation_failure_and_empty_success(self):
        for status, output in ((1, "/mnt/c/unused\n"), (0, "\n")):
            with self.subTest(status=status), \
                    mock.patch.object(ToolRunner, "_wsl_command", return_value="wsl"), \
                    mock.patch.object(subprocess, "run", return_value=
                                      subprocess.CompletedProcess([], status, output, "translation failed")), \
                    self.assertRaisesRegex(BootstrapError, "WSL could not translate"):
                ToolRunner(REPO_ROOT)._wsl_path(REPO_ROOT)

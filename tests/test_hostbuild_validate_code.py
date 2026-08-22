import ast
import contextlib
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild
from tools.bootstrap_toolchain import BootstrapError


class HostbuildValidateCodeTests(unittest.TestCase):
    def _write_seed_capture_fixture(self, manifest: Path) -> None:
        manifest.write_text(
            '{"artifacts":[{"file":"cupiddis.elf"}]}\n',
            encoding="utf-8",
        )
        (manifest.parent / "cupiddis.elf").write_bytes(b"checked seed tool")

    def _write_publication_fixture(
        self, root: Path
    ) -> tuple[Path, Path, Path, Path]:
        seed_manifest = root / "seed.json"
        input_manifest = root / "inputs.txt"
        kernel_pass1 = root / "kernel" / "kernel.elf.pass1"
        kernel_elf = root / "kernel" / "kernel.elf"
        output = root / "kernel" / "kernel.bin"
        kernel_elf.parent.mkdir()
        self._write_seed_capture_fixture(seed_manifest)
        input_manifest.write_text(
            "kernel/kernel.elf.pass1\nkernel/kernel.elf\n",
            encoding="utf-8",
            newline="\n",
        )
        kernel_pass1.write_bytes(b"validated pass-one ELF")
        kernel_elf.write_bytes(b"validated ELF")
        output.write_bytes(b"last known good kernel")
        return seed_manifest, input_manifest, kernel_elf, output

    def _run_cli(
        self,
        root: Path,
        manifest: Path,
        inputs: list[str] | None = None,
        *,
        input_manifest: str | None = None,
        output: str | None = None,
    ) -> tuple[int, str, str]:
        arguments = [
            "validate-code",
            "--seed-manifest",
            str(manifest),
            "--root",
            str(root),
        ]
        if input_manifest is not None:
            arguments.extend(("--input-manifest", input_manifest))
        if output is not None:
            arguments.extend(("--output", output))
        arguments.extend(inputs or [])
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            status = hostbuild.main(arguments)
        return status, stdout.getvalue(), stderr.getvalue()

    def test_validate_code_flattens_the_frozen_elf_and_rejects_live_drift(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-frozen-flat-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest = root / "seed.json"
            input_manifest = root / "inputs.txt"
            kernel_pass1 = root / "kernel" / "kernel.elf.pass1"
            kernel_elf = root / "kernel" / "kernel.elf"
            output = root / "kernel" / "kernel.bin"
            kernel_elf.parent.mkdir()
            self._write_seed_capture_fixture(seed_manifest)
            input_manifest.write_text(
                "kernel/kernel.elf.pass1\nkernel/kernel.elf\n",
                encoding="utf-8",
                newline="\n",
            )
            kernel_pass1.write_bytes(b"validated pass-one ELF")
            kernel_elf.write_bytes(b"validated ELF")
            output.write_bytes(b"last known good kernel")
            calls = []

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest_path
                self.assertIs(frozen_seed, checked_seed)
                private = Path(working_directory)
                calls.append(
                    (
                        tool_name,
                        tuple(str(argument) for argument in arguments),
                        timeout,
                    )
                )
                self.assertEqual(
                    (private / "kernel" / "kernel.elf").read_bytes(),
                    b"validated ELF",
                )
                if (
                    tool_name == "cupiddis"
                    and "--require-local-targets" in arguments
                ):
                    kernel_elf.write_bytes(b"unvalidated replacement")
                elif tool_name == "cupidobj":
                    self.assertEqual(
                        tuple(str(argument) for argument in arguments[:3]),
                        ("flat", "kernel/kernel.elf", "-o"),
                    )
                    candidate = private / str(arguments[3])
                    candidate.parent.mkdir(parents=True, exist_ok=True)
                    candidate.write_bytes(b"flat validated ELF")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            checked_seed = object()
            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(
                calls,
                [
                    (
                        "cupiddis",
                        (
                            "--require-known",
                            "kernel/kernel.elf.pass1",
                            "kernel/kernel.elf",
                        ),
                        300,
                    ),
                    (
                        "cupiddis",
                        (
                            "--require-known",
                            "--require-local-targets",
                            "kernel/kernel.elf.pass1",
                            "kernel/kernel.elf",
                        ),
                        600,
                    ),
                ],
            )
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(
                stderr,
                "[hostbuild] validate-code failed: code input changed while "
                "CupidDis local-target validation ran: kernel/kernel.elf\n",
            )
            self.assertEqual(output.read_bytes(), b"last known good kernel")

    def test_validate_code_publishes_the_flattened_frozen_elf(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-publish-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, output = self._write_publication_fixture(root)
            checked_seed = object()
            calls = []

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest_path, timeout
                self.assertIs(frozen_seed, checked_seed)
                private = Path(working_directory)
                self.assertEqual(
                    (private / "kernel" / "kernel.elf").read_bytes(),
                    b"validated ELF",
                )
                calls.append(
                    (tool_name, tuple(str(argument) for argument in arguments))
                )
                if tool_name == "cupidobj":
                    candidate = private / str(arguments[3])
                    candidate.parent.mkdir(parents=True, exist_ok=True)
                    candidate.write_bytes(b"flat validated ELF")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ) as freeze,
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(
                calls,
                [
                    (
                        "cupiddis",
                        (
                            "--require-known",
                            "kernel/kernel.elf.pass1",
                            "kernel/kernel.elf",
                        ),
                    ),
                    (
                        "cupiddis",
                        (
                            "--require-known",
                            "--require-local-targets",
                            "kernel/kernel.elf.pass1",
                            "kernel/kernel.elf",
                        ),
                    ),
                    (
                        "cupidobj",
                        (
                            "flat",
                            "kernel/kernel.elf",
                            "-o",
                            ".cupid-output/kernel.bin",
                        ),
                    ),
                ],
            )
            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertEqual(output.read_bytes(), b"flat validated ELF")
            freeze.assert_called_once()

    def test_validate_code_preserves_output_for_local_target_failures(self):
        cases = (
            ("status", 7, "", False),
            ("stdout", 0, "unexpected listing\n", False),
            ("runner", 0, "", True),
        )
        for name, status_code, tool_stdout, runner_failure in cases:
            with (
                self.subTest(name=name),
                tempfile.TemporaryDirectory(
                    prefix="hostbuild-validate-code-local-target-failure-"
                ) as temporary,
            ):
                root = Path(temporary)
                seed_manifest, _, _, output = self._write_publication_fixture(
                    root
                )
                checked_seed = object()
                calls = []

                def run_checked(
                    seed_manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    *,
                    timeout,
                    frozen_seed,
                ):
                    del seed_manifest_path, working_directory, timeout
                    self.assertIs(frozen_seed, checked_seed)
                    calls.append((tool_name, tuple(arguments)))
                    if len(calls) == 1:
                        return subprocess.CompletedProcess(
                            list(arguments), 0, "", "broad diagnostics\n"
                        )
                    self.assertEqual(tool_name, "cupiddis")
                    self.assertIn("--require-local-targets", arguments)
                    if runner_failure:
                        raise BootstrapError("seed execution failed")
                    return subprocess.CompletedProcess(
                        list(arguments),
                        status_code,
                        tool_stdout,
                        "local-target diagnostics\n",
                    )

                with (
                    mock.patch(
                        "tools.hostbuild.freeze_seed_inputs",
                        return_value=checked_seed,
                    ),
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=run_checked,
                    ),
                ):
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest="inputs.txt",
                        output="kernel/kernel.bin",
                    )

                self.assertEqual(
                    [call[0] for call in calls],
                    ["cupiddis", "cupiddis"],
                )
                self.assertEqual(stdout, "")
                self.assertNotEqual(status, 0)
                self.assertEqual(output.read_bytes(), b"last known good kernel")
                if runner_failure:
                    self.assertIn(
                        "checked CupidDis local-target validation could not run: "
                        "seed execution failed",
                        stderr,
                    )
                elif tool_stdout:
                    self.assertIn(
                        "checked CupidDis local-target validation wrote unexpected "
                        "standard output",
                        stderr,
                    )
                else:
                    self.assertEqual(status, status_code)
                    self.assertIn("broad diagnostics", stderr)
                    self.assertIn("local-target diagnostics", stderr)

    def test_validate_code_requires_both_linked_kernels_before_tools_run(self):
        for missing in ("kernel/kernel.elf.pass1", "kernel/kernel.elf"):
            with (
                self.subTest(missing=missing),
                tempfile.TemporaryDirectory(
                    prefix="hostbuild-validate-code-missing-linked-kernel-"
                ) as temporary,
            ):
                root = Path(temporary)
                seed_manifest, input_manifest, _, output = (
                    self._write_publication_fixture(root)
                )
                present = (
                    "kernel/kernel.elf"
                    if missing.endswith("pass1")
                    else "kernel/kernel.elf.pass1"
                )
                input_manifest.write_text(
                    present + "\n", encoding="utf-8", newline="\n"
                )
                with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest="inputs.txt",
                        output="kernel/kernel.bin",
                    )

                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(missing, stderr)
                self.assertEqual(output.read_bytes(), b"last known good kernel")
                checked.assert_not_called()

    def test_validate_code_publishes_when_the_output_does_not_exist(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-new-output-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, output = self._write_publication_fixture(root)
            output.unlink()
            checked_seed = object()

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest_path, timeout
                self.assertIs(frozen_seed, checked_seed)
                if tool_name == "cupidobj":
                    candidate = Path(working_directory) / str(arguments[3])
                    candidate.write_bytes(b"first flat kernel")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertEqual(output.read_bytes(), b"first flat kernel")

    def test_validate_code_preserves_an_output_that_appears_during_tools(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-output-appeared-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, output = self._write_publication_fixture(root)
            output.unlink()
            checked_seed = object()

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest_path, timeout
                self.assertIs(frozen_seed, checked_seed)
                if tool_name == "cupidobj":
                    candidate = Path(working_directory) / str(arguments[3])
                    candidate.write_bytes(b"candidate")
                    output.write_bytes(b"concurrent publisher")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("code output appeared while checked tools ran", stderr)
            self.assertEqual(output.read_bytes(), b"concurrent publisher")

    def test_validate_code_anchors_relative_seed_manifests_at_the_root(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-relative-seed-"
        ) as temporary:
            work = Path(temporary)
            root = work / "repo"
            outside = work / "outside"
            seed_manifest = root / "bootstrap" / "seed" / "manifest.json"
            seed_manifest.parent.mkdir(parents=True)
            outside.mkdir()
            self._write_seed_capture_fixture(seed_manifest)
            kernel_pass1 = root / "kernel" / "kernel.elf.pass1"
            kernel_elf = root / "kernel" / "kernel.elf"
            output = root / "kernel" / "kernel.bin"
            kernel_elf.parent.mkdir()
            kernel_pass1.write_bytes(b"validated pass-one ELF")
            kernel_elf.write_bytes(b"validated ELF")
            output.write_bytes(b"last known good kernel")
            (root / "inputs.txt").write_text(
                "kernel/kernel.elf.pass1\nkernel/kernel.elf\n",
                encoding="utf-8",
                newline="\n",
            )
            checked_seed = object()

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del timeout
                self.assertEqual(seed_manifest_path, seed_manifest)
                self.assertIs(frozen_seed, checked_seed)
                if tool_name == "cupidobj":
                    candidate = Path(working_directory) / str(arguments[3])
                    candidate.write_bytes(b"flat validated ELF")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with (
                contextlib.chdir(outside),
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    Path("bootstrap/seed/manifest.json"),
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertEqual(output.read_bytes(), b"flat validated ELF")

            def run_validation_only(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                del working_directory, arguments, timeout
                self.assertEqual(seed_manifest_path, seed_manifest)
                self.assertEqual(tool_name, "cupiddis")
                return subprocess.CompletedProcess([tool_name], 0, "", "")

            with (
                contextlib.chdir(outside),
                mock.patch("tools.hostbuild.freeze_seed_inputs") as freeze,
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_validation_only,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    Path("bootstrap/seed/manifest.json"),
                    input_manifest="inputs.txt",
                )

            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            freeze.assert_not_called()

    def test_validate_code_preserves_output_for_checked_tool_failures(self):
        cases = (
            ("disassembly failure", "cupiddis", 7, "", False),
            ("disassembly stdout", "cupiddis", 0, "listing\n", False),
            ("flattening failure", "cupidobj", 9, "", False),
            ("flattening stdout", "cupidobj", 0, "listing\n", False),
            ("flattening runner failure", "cupidobj", 0, "", True),
            ("missing flat output", "cupidobj", 0, "", False),
        )
        for name, failing_tool, status_code, tool_stdout, runner_failure in cases:
            with (
                self.subTest(name=name),
                tempfile.TemporaryDirectory(
                    prefix="hostbuild-validate-code-tool-failure-"
                ) as temporary,
            ):
                root = Path(temporary)
                seed_manifest, _, _, output = self._write_publication_fixture(root)
                checked_seed = object()
                calls = []

                def run_checked(
                    seed_manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    *,
                    timeout,
                    frozen_seed,
                ):
                    del seed_manifest_path, timeout
                    self.assertIs(frozen_seed, checked_seed)
                    calls.append(tool_name)
                    if tool_name == "cupidobj" and name != "missing flat output":
                        candidate = Path(working_directory) / str(arguments[3])
                        candidate.parent.mkdir(parents=True, exist_ok=True)
                        candidate.write_bytes(b"candidate")
                    if (
                        tool_name == "cupiddis"
                        and "--require-local-targets" in arguments
                    ):
                        return subprocess.CompletedProcess(
                            list(arguments), 0, "", "linked context\n"
                        )
                    if tool_name != failing_tool:
                        return subprocess.CompletedProcess(list(arguments), 0, "", "")
                    if runner_failure:
                        raise BootstrapError("seed execution failed")
                    return subprocess.CompletedProcess(
                        list(arguments),
                        status_code,
                        tool_stdout,
                        f"{name}\n",
                    )

                with (
                    mock.patch(
                        "tools.hostbuild.freeze_seed_inputs",
                        return_value=checked_seed,
                    ),
                    mock.patch(
                        "tools.hostbuild.run_seed_tool",
                        side_effect=run_checked,
                    ),
                ):
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest="inputs.txt",
                        output="kernel/kernel.bin",
                    )

                self.assertEqual(stdout, "")
                self.assertNotEqual(status, 0)
                self.assertEqual(output.read_bytes(), b"last known good kernel")
                if failing_tool == "cupiddis":
                    self.assertEqual(calls, ["cupiddis"])
                else:
                    self.assertEqual(
                        calls,
                        ["cupiddis", "cupiddis", "cupidobj"],
                    )
                if runner_failure:
                    self.assertIn(
                        "checked CupidObj could not run: seed execution failed",
                        stderr,
                    )
                    self.assertIn("linked context", stderr)
                elif tool_stdout:
                    self.assertIn(
                        f"checked {'CupidDis' if failing_tool == 'cupiddis' else 'CupidObj'} "
                        "wrote unexpected standard output",
                        stderr,
                    )
                elif name == "missing flat output":
                    self.assertIn(
                        "checked CupidObj output does not exist",
                        stderr,
                    )
                else:
                    self.assertEqual(status, status_code)
                    self.assertIn(name, stderr)

    def test_validate_code_preserves_output_when_publication_fails(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-publish-failure-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, output = self._write_publication_fixture(root)
            checked_seed = object()

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest_path, timeout
                self.assertIs(frozen_seed, checked_seed)
                if tool_name == "cupidobj":
                    candidate = Path(working_directory) / str(arguments[3])
                    candidate.parent.mkdir(parents=True, exist_ok=True)
                    candidate.write_bytes(b"candidate")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
                mock.patch(
                    "tools.hostbuild._publish_code_output",
                    side_effect=PermissionError("publication denied"),
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "validated kernel could not be published: publication denied",
                stderr,
            )
            self.assertEqual(output.read_bytes(), b"last known good kernel")

    def test_validate_code_preserves_output_when_seed_freeze_fails(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-seed-freeze-failure-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, output = self._write_publication_fixture(root)
            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    side_effect=BootstrapError("seed capture rejected"),
                ),
                mock.patch("tools.hostbuild.run_seed_tool") as checked,
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "checked seed could not be frozen: seed capture rejected",
                stderr,
            )
            self.assertEqual(output.read_bytes(), b"last known good kernel")
            checked.assert_not_called()

    def test_validate_code_holds_the_output_parent_through_publication(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-output-parent-swap-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, _ = self._write_publication_fixture(root)
            publication = root / "published"
            publication.mkdir()
            output = publication / "kernel.bin"
            output.write_bytes(b"last known good kernel")
            displaced = root / "validated-publication"
            replacement = root / "replacement-publication"
            replacement.mkdir()
            (replacement / "kernel.bin").write_bytes(b"outside output")
            checked_seed = object()
            replacement_was_blocked = False

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                nonlocal replacement_was_blocked
                del seed_manifest_path, timeout
                self.assertIs(frozen_seed, checked_seed)
                if tool_name == "cupidobj":
                    candidate = Path(working_directory) / str(arguments[3])
                    candidate.write_bytes(b"flat validated ELF")
                    try:
                        publication.rename(displaced)
                        replacement.rename(publication)
                    except PermissionError:
                        replacement_was_blocked = True
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with (
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_checked,
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="published/kernel.bin",
                )

            self.assertEqual(stdout, "")
            if os.name == "nt":
                self.assertTrue(replacement_was_blocked)
                self.assertEqual(status, 0)
                self.assertEqual(stderr, "")
                self.assertEqual(output.read_bytes(), b"flat validated ELF")
            else:
                self.assertFalse(replacement_was_blocked)
                self.assertEqual(status, 1)
                self.assertIn(
                    "code output parent changed while checked tools ran",
                    stderr,
                )
                self.assertEqual(output.read_bytes(), b"outside output")
                self.assertEqual(
                    (displaced / "kernel.bin").read_bytes(),
                    b"last known good kernel",
                )

    def test_validate_code_rejects_linked_output_parents_and_hardlink_aliases(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-output-alias-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, kernel_elf, output = self._write_publication_fixture(root)
            output.unlink()
            os.link(kernel_elf, output)
            with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("code output may not replace an input", stderr)
            checked.assert_not_called()

        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-output-parent-link-"
        ) as temporary:
            work = Path(temporary)
            root = work / "repo"
            outside = work / "outside"
            root.mkdir()
            outside.mkdir()
            seed_manifest = root / "seed.json"
            self._write_seed_capture_fixture(seed_manifest)
            (root / "inputs.txt").write_text(
                "kernel/kernel.elf\n", encoding="utf-8", newline="\n"
            )
            (outside / "kernel.elf").write_bytes(b"validated ELF")
            (outside / "kernel.bin").write_bytes(b"outside output")
            linked_parent = root / "kernel"
            try:
                linked_parent.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")
            with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "code output may not be a symbolic link or junction",
                stderr,
            )
            self.assertEqual(
                (outside / "kernel.bin").read_bytes(), b"outside output"
            )
            checked.assert_not_called()

    def test_validate_code_rejects_a_concurrent_publisher(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-concurrent-publisher-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, _, output = self._write_publication_fixture(root)
            publication_lock = hostbuild._acquire_disk_publication_lock(output)
            try:
                with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest="inputs.txt",
                        output="kernel/kernel.bin",
                    )
            finally:
                hostbuild._release_disk_publication_lock(publication_lock)
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("another hostbuild publisher is active", stderr)
            self.assertEqual(output.read_bytes(), b"last known good kernel")
            checked.assert_not_called()

    def test_validate_code_rejects_linked_or_input_alias_outputs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-output-path-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest, _, kernel_elf, output = self._write_publication_fixture(root)
            victim = root / "victim.bin"
            victim.write_bytes(b"victim")
            output.unlink()
            try:
                output.symlink_to(victim)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "code output may not be a symbolic link or junction",
                stderr,
            )
            self.assertEqual(victim.read_bytes(), b"victim")
            checked.assert_not_called()

            output.unlink()
            with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.elf",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("code output may not replace an input", stderr)
            self.assertEqual(kernel_elf.read_bytes(), b"validated ELF")
            checked.assert_not_called()

    def test_validate_code_rejects_manifest_and_seed_output_aliases(self):
        cases = (
            ("code manifest path", "inputs.txt", None),
            ("code manifest hardlink", "kernel/kernel.bin", "inputs.txt"),
            ("seed manifest path", "seed.json", None),
            ("seed manifest hardlink", "kernel/kernel.bin", "seed.json"),
            ("seed artifact path", "cupiddis.elf", None),
            (
                "seed artifact hardlink",
                "kernel/kernel.bin",
                "cupiddis.elf",
            ),
        )
        for name, output_logical, alias_target in cases:
            with (
                self.subTest(name=name),
                tempfile.TemporaryDirectory(
                    prefix="hostbuild-validate-code-trust-alias-"
                ) as temporary,
            ):
                root = Path(temporary)
                seed_manifest, input_manifest, _, default_output = (
                    self._write_publication_fixture(root)
                )
                output = root.joinpath(*output_logical.split("/"))
                if alias_target is not None:
                    default_output.unlink()
                    target = root.joinpath(*alias_target.split("/"))
                    os.link(target, output)
                protected = output.read_bytes()

                with (
                    mock.patch(
                        "tools.hostbuild.freeze_seed_inputs",
                        return_value=object(),
                    ),
                    mock.patch("tools.hostbuild.run_seed_tool") as checked,
                ):
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest=input_manifest.name,
                        output=output_logical,
                    )

                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn("code output may not replace an input", stderr)
                self.assertEqual(output.read_bytes(), protected)
                checked.assert_not_called()

    def test_windows_code_walk_uses_parent_relative_nofollow_handles(self):
        tree = ast.parse(Path(hostbuild.__file__).read_text(encoding="utf-8"))
        child = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_open_windows_code_child"
        )
        called_attributes = {
            node.func.attr
            for node in ast.walk(child)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
        }
        referenced_names = {
            node.id for node in ast.walk(child) if isinstance(node, ast.Name)
        }
        self.assertIn("NtCreateFile", called_attributes)
        self.assertNotIn("CreateFileW", called_attributes)
        self.assertIn("parent_handle", referenced_names)
        self.assertIn("_CODE_WINDOWS_OBJECT_DONT_REPARSE", referenced_names)

        publisher = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_rename_windows_code_output"
        )
        publisher_calls = {
            node.func.attr
            for node in ast.walk(publisher)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
        }
        root_assignments = [
            node
            for node in ast.walk(publisher)
            if isinstance(node, ast.Assign)
            and any(
                isinstance(target, ast.Attribute)
                and target.attr == "root_directory"
                for target in node.targets
            )
        ]
        self.assertIn("NtSetInformationFile", publisher_calls)
        self.assertEqual(len(root_assignments), 1)

    @unittest.skipUnless(
        os.name == "nt",
        "Windows parent pinning requires Windows handles",
    )
    def test_windows_parent_handle_blocks_replacement_before_final_open(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-windows-pin-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            input_manifest = root / "inputs.txt"
            objects = root / "kernel" / "objects"
            displaced = root / "displaced-objects"
            objects.mkdir(parents=True)
            (objects / "test.o").write_bytes(b"object")
            manifest.write_text("{}\n", encoding="utf-8")
            input_manifest.write_text(
                "kernel/objects/test.o\n",
                encoding="utf-8",
                newline="\n",
            )
            original_child_open = hostbuild._open_windows_code_child
            replacement_was_blocked = False

            def open_after_replacement_attempt(
                parent_handle,
                name,
                *,
                directory,
                logical,
                subject,
            ):
                nonlocal replacement_was_blocked
                if name == "test.o" and not directory and not replacement_was_blocked:
                    try:
                        objects.rename(displaced)
                    except PermissionError:
                        replacement_was_blocked = True
                return original_child_open(
                    parent_handle,
                    name,
                    directory=directory,
                    logical=logical,
                    subject=subject,
                )

            with (
                mock.patch(
                    "tools.hostbuild._open_windows_code_child",
                    side_effect=open_after_replacement_attempt,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=subprocess.CompletedProcess(["cupiddis"], 0, "", ""),
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    manifest,
                    input_manifest="inputs.txt",
                )

            self.assertTrue(replacement_was_blocked)
            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertEqual((objects / "test.o").read_bytes(), b"object")

    @unittest.skipUnless(os.name == "nt", "junctions are a Windows path type")
    def test_windows_code_walk_rejects_a_runtime_junction(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-windows-junction-"
        ) as temporary:
            root = Path(temporary) / "repo"
            outside = Path(temporary) / "outside"
            root.mkdir()
            outside.mkdir()
            manifest = root / "manifest.json"
            input_manifest = root / "inputs.txt"
            linked_parent = root / "linked-parent"
            manifest.write_text("{}\n", encoding="utf-8")
            input_manifest.write_text(
                "linked-parent/escape.o\n",
                encoding="utf-8",
                newline="\n",
            )
            (outside / "escape.o").write_bytes(b"outside")
            created = subprocess.run(
                ["cmd", "/c", "mklink", "/J", str(linked_parent), str(outside)],
                capture_output=True,
                text=True,
                check=False,
            )
            if created.returncode != 0:
                self.skipTest(f"junction creation is unavailable: {created.stderr}")
            try:
                with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                    status, stdout, stderr = self._run_cli(
                        root,
                        manifest,
                        input_manifest="inputs.txt",
                    )
            finally:
                os.rmdir(linked_parent)

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "code input may not be a symbolic link or junction: "
                "linked-parent/escape.o",
                stderr,
            )
            checked.assert_not_called()

    def test_validate_code_freezes_seed_artifacts_through_pinned_parents(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-seed-parent-swap-"
        ) as temporary:
            root = Path(temporary)
            seed_directory = root / "bootstrap" / "seed"
            seed_directory.mkdir(parents=True)
            seed_manifest = seed_directory / "manifest.json"
            self._write_seed_capture_fixture(seed_manifest)
            replacement = root / "replacement-seed"
            replacement.mkdir()
            replacement_manifest = replacement / "manifest.json"
            self._write_seed_capture_fixture(replacement_manifest)
            (replacement / "cupiddis.elf").write_bytes(b"untrusted seed tool")
            input_manifest = root / "inputs.txt"
            kernel_pass1 = root / "kernel" / "kernel.elf.pass1"
            kernel_elf = root / "kernel" / "kernel.elf"
            output = root / "kernel" / "kernel.bin"
            kernel_elf.parent.mkdir()
            kernel_pass1.write_bytes(b"validated pass-one ELF")
            kernel_elf.write_bytes(b"validated ELF")
            output.write_bytes(b"last known good kernel")
            input_manifest.write_text(
                "kernel/kernel.elf.pass1\nkernel/kernel.elf\n",
                encoding="utf-8",
                newline="\n",
            )
            displaced = root / "validated-seed"
            swapped = False
            replacement_was_blocked = False
            checked_seed = object()

            def run_checked(
                seed_manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest_path, timeout
                self.assertIs(frozen_seed, checked_seed)
                if tool_name == "cupidobj":
                    candidate = Path(working_directory) / str(arguments[3])
                    candidate.write_bytes(b"flat validated ELF")
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            if os.name == "nt":
                original_child = hostbuild._open_windows_code_child

                def child_with_swap(
                    parent_handle,
                    name,
                    *,
                    directory,
                    logical,
                    subject,
                    **kwargs,
                ):
                    nonlocal swapped, replacement_was_blocked
                    if name == "cupiddis.elf" and not swapped:
                        try:
                            seed_directory.rename(displaced)
                            replacement.rename(seed_directory)
                            swapped = True
                        except PermissionError:
                            replacement_was_blocked = True
                    return original_child(
                        parent_handle,
                        name,
                        directory=directory,
                        logical=logical,
                        subject=subject,
                        **kwargs,
                    )

                path_patch = mock.patch(
                    "tools.hostbuild._open_windows_code_child",
                    side_effect=child_with_swap,
                )
            else:
                original_open = os.open

                def open_with_swap(path, flags, mode=0o777, *, dir_fd=None):
                    nonlocal swapped
                    if path == "cupiddis.elf" and dir_fd is not None and not swapped:
                        seed_directory.rename(displaced)
                        replacement.rename(seed_directory)
                        swapped = True
                    return original_open(path, flags, mode, dir_fd=dir_fd)

                path_patch = mock.patch(
                    "tools.hostbuild.os.open", side_effect=open_with_swap
                )

            with (
                path_patch,
                mock.patch(
                    "tools.hostbuild.freeze_seed_inputs",
                    return_value=checked_seed,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool", side_effect=run_checked
                ),
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="inputs.txt",
                    output="kernel/kernel.bin",
                )

            self.assertEqual(stdout, "")
            if os.name == "nt":
                self.assertTrue(replacement_was_blocked)
                self.assertFalse(swapped)
                self.assertEqual(status, 0)
                self.assertEqual(stderr, "")
                self.assertEqual(output.read_bytes(), b"flat validated ELF")
            else:
                self.assertTrue(swapped)
                self.assertEqual(status, 1)
                self.assertIn(
                    "checked seed inputs changed while checked tools ran",
                    stderr,
                )
                self.assertEqual(output.read_bytes(), b"last known good kernel")

    @unittest.skipIf(
        os.name == "nt",
        "Windows prevents replacement while the parent handle is pinned",
    )
    def test_capture_uses_the_pinned_parent_when_it_is_replaced_before_openat(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-parent-swap-"
        ) as temporary:
            root = Path(temporary)
            objects = root / "kernel" / "objects"
            objects.mkdir(parents=True)
            (objects / "test.o").write_bytes(b"validated object")
            replacement = root / "replacement-objects"
            replacement.mkdir()
            (replacement / "test.o").write_bytes(b"outside object")
            displaced = root / "original-objects"
            frozen = root / "frozen" / "test.o"
            original_open = os.open
            swapped = False

            def open_with_swap(path, flags, mode=0o777, *, dir_fd=None):
                nonlocal swapped
                if path == "test.o" and dir_fd is not None and not swapped:
                    objects.rename(displaced)
                    replacement.rename(objects)
                    swapped = True
                return original_open(path, flags, mode, dir_fd=dir_fd)

            with mock.patch("tools.hostbuild.os.open", side_effect=open_with_swap):
                snapshot = hostbuild._capture_code_input(
                    root,
                    "kernel/objects/test.o",
                    frozen=frozen,
                )

            self.assertTrue(swapped)
            self.assertEqual(snapshot.size, len(b"validated object"))
            self.assertEqual(frozen.read_bytes(), b"validated object")
            self.assertEqual((objects / "test.o").read_bytes(), b"outside object")

    def test_validate_code_snapshots_manifest_paths_and_runs_once(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-"
        ) as temporary:
            root = Path(temporary) / "repo"
            first = root / "kernel" / "cpu" / "first.o"
            second = root / "user" / "hello.elf"
            manifest = root / "seed" / "manifest.json"
            input_manifest = root / "bootstrap" / "code-inputs.txt"
            first.parent.mkdir(parents=True)
            second.parent.mkdir(parents=True)
            manifest.parent.mkdir(parents=True)
            input_manifest.parent.mkdir(parents=True)
            first.write_bytes(b"first object")
            second.write_bytes(b"second executable")
            manifest.write_text("{}\n", encoding="utf-8")
            input_manifest.write_text(
                "kernel/cpu/first.o\nuser/hello.elf\n",
                encoding="utf-8",
                newline="\n",
            )

            def run_checked(
                seed_manifest,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                private = Path(working_directory)
                self.assertEqual(seed_manifest, manifest)
                self.assertEqual(tool_name, "cupiddis")
                self.assertEqual(timeout, 300)
                self.assertEqual(
                    (private / "bootstrap" / "code-inputs.txt").read_text(
                        encoding="utf-8"
                    ),
                    "kernel/cpu/first.o\nuser/hello.elf\n",
                )
                self.assertEqual(
                    tuple(arguments),
                    (
                        "--require-known",
                        "kernel/cpu/first.o",
                        "user/hello.elf",
                    ),
                )
                self.assertEqual(
                    (private / "kernel" / "cpu" / "first.o").read_bytes(),
                    b"first object",
                )
                self.assertEqual(
                    (private / "user" / "hello.elf").read_bytes(),
                    b"second executable",
                )
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=run_checked,
            ) as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    manifest,
                    input_manifest="bootstrap/code-inputs.txt",
                )

            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            checked.assert_called_once()

    def test_validate_code_rejects_unsafe_input_manifests(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-manifest-path-"
        ) as temporary:
            work = Path(temporary)
            root = work / "repo"
            root.mkdir()
            seed_manifest = root / "seed.json"
            seed_manifest.write_text("{}\n", encoding="utf-8")
            outside = work / "outside.txt"
            outside.write_text("kernel.o\n", encoding="utf-8")
            directory = root / "manifest-directory"
            directory.mkdir()
            target = root / "target.txt"
            target.write_text("kernel.o\n", encoding="utf-8")
            linked = root / "linked.txt"
            try:
                linked.symlink_to(target)
            except OSError:
                linked = None

            cases = [
                (
                    str(outside.resolve()),
                    "code input manifest must be relative to the repository root",
                ),
                (
                    "../outside.txt",
                    "code input manifest may not contain a parent traversal",
                ),
                (
                    "missing.txt",
                    "code input manifest does not exist: missing.txt",
                ),
                (
                    "manifest-directory",
                    "code input manifest is not a regular file: manifest-directory",
                ),
            ]
            if linked is not None:
                cases.append(
                    (
                        "linked.txt",
                        "code input manifest may not be a symbolic link or "
                        "junction: linked.txt",
                    )
                )
            for path, message in cases:
                with (
                    self.subTest(path=path),
                    mock.patch("tools.hostbuild.run_seed_tool") as checked,
                ):
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest=path,
                    )
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(message, stderr)
                checked.assert_not_called()

    def test_validate_code_rejects_malformed_manifest_lines(self):
        cases = (
            (b"", "code input manifest may not be empty"),
            (b"kernel.o", "code input manifest must end with a newline"),
            (
                b"kernel.o\r\n",
                "code input manifest must use LF newlines",
            ),
            (b"kernel.o\n\n", "code input manifest line 2 is blank"),
            (
                b"# kernel objects\nkernel.o\n",
                "code input manifest line 1 may not be a comment",
            ),
            (
                b"kernel object.o\n",
                "code input manifest line 1 may not contain whitespace",
            ),
            (
                b"kernel\\object.o\n",
                "code input manifest line 1 must use forward slashes",
            ),
            (
                b"./kernel.o\n",
                "code input manifest line 1 is not a canonical repository path",
            ),
            (
                b"--all\n",
                "code input may not begin with an option marker",
            ),
            (
                b"kernel.o\nkernel.o\n",
                "code input is listed more than once: kernel.o",
            ),
        )
        for payload, message in cases:
            with (
                self.subTest(payload=payload),
                tempfile.TemporaryDirectory(
                    prefix="hostbuild-validate-code-manifest-line-"
                ) as temporary,
            ):
                root = Path(temporary)
                seed_manifest = root / "seed.json"
                input_manifest = root / "inputs.txt"
                seed_manifest.write_text("{}\n", encoding="utf-8")
                input_manifest.write_bytes(payload)
                with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest="inputs.txt",
                    )
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(message, stderr)
                checked.assert_not_called()

    def test_validate_code_rejects_absolute_and_escaping_manifest_entries(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-manifest-entry-"
        ) as temporary:
            work = Path(temporary)
            root = work / "repo"
            root.mkdir()
            seed_manifest = root / "seed.json"
            input_manifest = root / "inputs.txt"
            outside = work / "outside.o"
            seed_manifest.write_text("{}\n", encoding="utf-8")
            outside.write_bytes(b"outside")
            cases = (
                (
                    outside.resolve().as_posix(),
                    "code input must be relative to the repository root",
                ),
                (
                    "../outside.o",
                    "code input may not contain a parent traversal",
                ),
            )
            for entry, message in cases:
                input_manifest.write_text(entry + "\n", encoding="utf-8", newline="\n")
                with (
                    self.subTest(entry=entry),
                    mock.patch("tools.hostbuild.run_seed_tool") as checked,
                ):
                    status, stdout, stderr = self._run_cli(
                        root,
                        seed_manifest,
                        input_manifest="inputs.txt",
                    )
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(message, stderr)
                checked.assert_not_called()

    def test_validate_code_rejects_absolute_and_outside_inputs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-path-"
        ) as temporary:
            work = Path(temporary)
            root = work / "repo"
            root.mkdir()
            manifest = root / "manifest.json"
            manifest.write_text("{}\n", encoding="utf-8")
            outside = work / "outside.o"
            outside.write_bytes(b"outside")

            cases = (
                (
                    [str(outside.resolve())],
                    "code input must be relative to the repository root",
                ),
                (
                    ["../outside.o"],
                    "code input may not contain a parent traversal",
                ),
            )
            for inputs, message in cases:
                with (
                    self.subTest(inputs=inputs),
                    mock.patch("tools.hostbuild.run_seed_tool") as checked,
                ):
                    status, stdout, stderr = self._run_cli(root, manifest, inputs)
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(message, stderr)
                checked.assert_not_called()

    def test_validate_code_rejects_duplicate_missing_and_directory_inputs(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-input-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            present = root / "kernel" / "present.o"
            directory = root / "kernel" / "objects"
            input_manifest = root / "inputs.txt"
            present.parent.mkdir()
            directory.mkdir()
            present.write_bytes(b"present")
            manifest.write_text("{}\n", encoding="utf-8")

            cases = (
                (
                    ["kernel/present.o", "kernel/present.o"],
                    "code input is listed more than once: kernel/present.o",
                ),
                (
                    ["kernel/missing.o"],
                    "code input does not exist: kernel/missing.o",
                ),
                (
                    ["kernel/objects"],
                    "code input is not a regular file: kernel/objects",
                ),
            )
            for inputs, message in cases:
                input_manifest.write_text(
                    "".join(f"{path}\n" for path in inputs),
                    encoding="utf-8",
                    newline="\n",
                )
                with (
                    self.subTest(inputs=inputs),
                    mock.patch("tools.hostbuild.run_seed_tool") as checked,
                ):
                    status, stdout, stderr = self._run_cli(
                        root,
                        manifest,
                        input_manifest="inputs.txt",
                    )
                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(message, stderr)
                checked.assert_not_called()

    def test_validate_code_rejects_symbolic_link_inputs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-link-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            target = root / "target.o"
            linked = root / "linked.o"
            input_manifest = root / "inputs.txt"
            manifest.write_text("{}\n", encoding="utf-8")
            target.write_bytes(b"target")
            input_manifest.write_text("linked.o\n", encoding="utf-8", newline="\n")
            try:
                linked.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    manifest,
                    input_manifest="inputs.txt",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "code input may not be a symbolic link or junction: linked.o",
                stderr,
            )
            checked.assert_not_called()

    def test_validate_code_rejects_a_linked_parent_directory(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-parent-link-"
        ) as temporary:
            work = Path(temporary)
            root = work / "repo"
            outside = work / "outside"
            root.mkdir()
            outside.mkdir()
            manifest = root / "manifest.json"
            input_manifest = root / "inputs.txt"
            linked_parent = root / "linked-parent"
            manifest.write_text("{}\n", encoding="utf-8")
            input_manifest.write_text(
                "linked-parent/escape.o\n",
                encoding="utf-8",
                newline="\n",
            )
            (outside / "escape.o").write_bytes(b"outside")
            try:
                linked_parent.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            with mock.patch("tools.hostbuild.run_seed_tool") as checked:
                status, stdout, stderr = self._run_cli(
                    root,
                    manifest,
                    input_manifest="inputs.txt",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn(
                "code input may not be a symbolic link or junction: "
                "linked-parent/escape.o",
                stderr,
            )
            checked.assert_not_called()

    def test_validate_code_rejects_unexpected_tool_stdout(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-stdout-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            source = root / "kernel.o"
            manifest.write_text("{}\n", encoding="utf-8")
            source.write_bytes(b"object")
            completed = subprocess.CompletedProcess(
                ["cupiddis"],
                0,
                "unexpected listing\n",
                "tool warning\n",
            )

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                return_value=completed,
            ):
                status, stdout, stderr = self._run_cli(root, manifest, ["kernel.o"])

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(
                stderr,
                "tool warning\n"
                "[hostbuild] validate-code failed: checked CupidDis wrote "
                "unexpected standard output\n",
            )

    def test_validate_code_preserves_all_tool_errors_and_status(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-errors-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            first = root / "first.o"
            second = root / "second.o"
            manifest.write_text("{}\n", encoding="utf-8")
            first.write_bytes(b"first")
            second.write_bytes(b"second")
            diagnostic = (
                "cupiddis: first.o: code check failed: 2 known, 1 unknown, "
                "0 invalid, 0 truncated\n"
                "cupiddis: second.o: code check failed: 1 known, 0 unknown, "
                "1 invalid, 1 truncated\n"
            )
            completed = subprocess.CompletedProcess(["cupiddis"], 7, "", diagnostic)

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                return_value=completed,
            ) as checked:
                status, stdout, stderr = self._run_cli(
                    root, manifest, ["first.o", "second.o"]
                )

            self.assertEqual(status, 7)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, diagnostic)
            self.assertEqual(checked.call_count, 1)

    def test_validate_code_reports_checked_seed_runner_failures(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-seed-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            source = root / "kernel.o"
            manifest.write_text("{}\n", encoding="utf-8")
            source.write_bytes(b"object")

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=BootstrapError("seed hash differs"),
            ):
                status, stdout, stderr = self._run_cli(root, manifest, ["kernel.o"])

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(
                stderr,
                "[hostbuild] validate-code failed: checked CupidDis could "
                "not run: seed hash differs\n",
            )

    def test_validate_code_rejects_live_input_drift_after_the_tool_runs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-drift-"
        ) as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            source = root / "kernel" / "drift.o"
            source.parent.mkdir()
            manifest.write_text("{}\n", encoding="utf-8")
            source.write_bytes(b"before")

            def run_then_change(
                seed_manifest,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                del seed_manifest, tool_name, timeout
                self.assertEqual(
                    (Path(working_directory) / "kernel" / "drift.o").read_bytes(),
                    b"before",
                )
                source.write_bytes(b"after")
                return subprocess.CompletedProcess(
                    list(arguments), 0, "", "tool detail\n"
                )

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=run_then_change,
            ):
                status, stdout, stderr = self._run_cli(
                    root, manifest, ["kernel/drift.o"]
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(
                stderr,
                "tool detail\n"
                "[hostbuild] validate-code failed: code input changed while "
                "CupidDis ran: kernel/drift.o\n",
            )

    def test_validate_code_rejects_manifest_drift_after_the_tool_runs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-validate-code-manifest-drift-"
        ) as temporary:
            root = Path(temporary)
            seed_manifest = root / "seed.json"
            input_manifest = root / "bootstrap" / "inputs.txt"
            source = root / "kernel.o"
            input_manifest.parent.mkdir()
            seed_manifest.write_text("{}\n", encoding="utf-8")
            input_manifest.write_text("kernel.o\n", encoding="utf-8", newline="\n")
            source.write_bytes(b"object")

            def run_then_change(*args, **kwargs):
                del args, kwargs
                input_manifest.write_text(
                    "replacement.o\n", encoding="utf-8", newline="\n"
                )
                return subprocess.CompletedProcess(["cupiddis"], 0, "", "tool detail\n")

            with mock.patch(
                "tools.hostbuild.run_seed_tool",
                side_effect=run_then_change,
            ):
                status, stdout, stderr = self._run_cli(
                    root,
                    seed_manifest,
                    input_manifest="bootstrap/inputs.txt",
                )

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(
                stderr,
                "tool detail\n"
                "[hostbuild] validate-code failed: code input manifest "
                "changed while CupidDis ran: bootstrap/inputs.txt\n",
            )


if __name__ == "__main__":
    unittest.main()

import contextlib
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild


class HostbuildAssembleSmpTrampolineTests(unittest.TestCase):
    def _write_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        seed = root / "bootstrap" / "seeds" / "manifest.json"
        source = root / "kernel" / "smp" / "smp_trampoline.S"
        output = root / "kernel" / "smp_trampoline.bin"
        seed.parent.mkdir(parents=True)
        source.parent.mkdir(parents=True)
        seed.write_text(
            '{"artifacts":['
            '{"file":"cupidasm.elf"},'
            '{"file":"cupiddis.elf"}'
            ']}\n',
            encoding="utf-8",
        )
        (seed.parent / "cupidasm.elf").write_bytes(b"checked assembler")
        (seed.parent / "cupiddis.elf").write_bytes(b"checked disassembler")
        source.write_text("bits 16\norg 0x8000\nnop\n", encoding="utf-8")
        output.write_bytes(b"last known good trampoline")
        return seed, source, output

    def _run_cli(self, root: Path, seed: Path) -> tuple[int, str, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            status = hostbuild.main(
                [
                    "assemble-smp-trampoline",
                    "--seed-manifest",
                    str(seed),
                    "--root",
                    str(root),
                    "--source",
                    "kernel/smp/smp_trampoline.S",
                    "--output",
                    "kernel/smp_trampoline.bin",
                ]
            )
        return status, stdout.getvalue(), stderr.getvalue()

    def test_checked_assembler_candidate_passes_the_exact_cupiddis_map(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-smp-trampoline-map-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            checked_seed = object()
            candidate_payload = bytes(range(256)) * 16
            calls: list[tuple[str, tuple[str, ...]]] = []

            def run_checked(
                seed_manifest,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest, timeout
                self.assertIs(frozen_seed, checked_seed)
                private_root = Path(working_directory)
                string_arguments = tuple(str(argument) for argument in arguments)
                calls.append((tool_name, string_arguments))
                if tool_name == "cupidasm":
                    self.assertEqual(
                        string_arguments,
                        (
                            "-f",
                            "bin",
                            "-o",
                            ".cupid-output/smp_trampoline.bin",
                            "kernel/smp/smp_trampoline.S",
                        ),
                    )
                    candidate = private_root / string_arguments[3]
                    candidate.write_bytes(candidate_payload)
                else:
                    self.assertEqual(tool_name, "cupiddis")
                    self.assertEqual(
                        string_arguments,
                        (
                            "--raw",
                            "--mode",
                            "16",
                            "--range-at",
                            "0x1f:data",
                            "--range-at",
                            "0x210:32",
                            "--range-at",
                            "0x254:data",
                            "--base",
                            "0x8000",
                            "--require-known",
                            ".cupid-output/smp_trampoline.bin",
                        ),
                    )
                    self.assertEqual(
                        (private_root / string_arguments[-1]).read_bytes(),
                        candidate_payload,
                    )
                return subprocess.CompletedProcess(
                    list(arguments), 0, "", ""
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
                status, stdout, stderr = self._run_cli(root, seed)

            self.assertEqual([name for name, _ in calls], ["cupidasm", "cupiddis"])
            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertEqual(output.read_bytes(), candidate_payload)

    def test_cupiddis_rejection_preserves_the_published_trampoline(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-smp-trampoline-reject-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            original = output.read_bytes()
            checked_seed = object()
            private_roots: list[Path] = []

            def run_checked(
                seed_manifest,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
                frozen_seed,
            ):
                del seed_manifest, timeout
                self.assertIs(frozen_seed, checked_seed)
                private_root = Path(working_directory)
                private_roots.append(private_root)
                if tool_name == "cupidasm":
                    candidate = private_root / str(arguments[3])
                    candidate.write_bytes(bytes(4096))
                    return subprocess.CompletedProcess(
                        list(arguments), 0, "", ""
                    )
                self.assertEqual(tool_name, "cupiddis")
                return subprocess.CompletedProcess(
                    list(arguments),
                    9,
                    "",
                    "cupiddis: trampoline has an unknown opcode at 0x8020\n",
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
                status, stdout, stderr = self._run_cli(root, seed)

            self.assertEqual(status, 9)
            self.assertEqual(stdout, "")
            self.assertEqual(
                stderr,
                "cupiddis: trampoline has an unknown opcode at 0x8020\n",
            )
            self.assertEqual(output.read_bytes(), original)
            self.assertTrue(private_roots)
            self.assertTrue(all(not path.exists() for path in private_roots))

    def test_success_diagnostics_and_input_drift_preserve_the_output(self):
        cases = (
            (
                "assembler stdout",
                {"assembler_stdout": "cupidasm notice\n"},
                "checked CupidASM wrote unexpected standard output",
            ),
            (
                "assembler stderr",
                {"assembler_stderr": "cupidasm notice\n"},
                "checked CupidASM wrote unexpected standard error",
            ),
            (
                "disassembler stdout",
                {"disassembler_stdout": "cupiddis notice\n"},
                "checked CupidDis wrote unexpected standard output",
            ),
            (
                "disassembler stderr",
                {"disassembler_stderr": "cupiddis notice\n"},
                "checked CupidDis wrote unexpected standard error",
            ),
            (
                "wrong candidate size",
                {"candidate_size": 4095},
                "trampoline output must be exactly 4096 bytes",
            ),
            (
                "source drift",
                {"drift": "source"},
                "code input changed while CupidASM ran",
            ),
            (
                "seed drift",
                {"drift": "seed"},
                "checked seed inputs changed while checked tools ran",
            ),
            (
                "candidate drift",
                {"drift": "candidate"},
                "checked CupidASM output changed while CupidDis ran",
            ),
        )

        for label, options, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="hostbuild-smp-trampoline-guard-"
            ) as temporary:
                root = Path(temporary)
                seed, source, output = self._write_fixture(root)
                original = output.read_bytes()
                checked_seed = object()

                def run_checked(
                    seed_manifest,
                    working_directory,
                    tool_name,
                    arguments,
                    *,
                    timeout,
                    frozen_seed,
                ):
                    del seed_manifest, timeout
                    self.assertIs(frozen_seed, checked_seed)
                    private_root = Path(working_directory)
                    candidate = private_root / str(arguments[3])
                    if tool_name == "cupidasm":
                        candidate.write_bytes(
                            bytes(options.get("candidate_size", 4096))
                        )
                        if options.get("drift") == "source":
                            source.write_text(
                                "bits 16\norg 0x8000\ncli\n",
                                encoding="utf-8",
                            )
                        if options.get("drift") == "seed":
                            (seed.parent / "cupidasm.elf").write_bytes(
                                b"changed assembler"
                            )
                        return subprocess.CompletedProcess(
                            list(arguments),
                            0,
                            options.get("assembler_stdout", ""),
                            options.get("assembler_stderr", ""),
                        )

                    self.assertEqual(tool_name, "cupiddis")
                    candidate = private_root / str(arguments[-1])
                    if options.get("drift") == "candidate":
                        candidate.write_bytes(bytes([1]) * 4096)
                    return subprocess.CompletedProcess(
                        list(arguments),
                        0,
                        options.get("disassembler_stdout", ""),
                        options.get("disassembler_stderr", ""),
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
                    status, stdout, stderr = self._run_cli(root, seed)

                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(expected, stderr)
                self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()

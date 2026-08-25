import contextlib
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild


SMP_RAW_MAP = (
    b"cupid.raw-map.v2\n"
    b"size 4096\n"
    b"base 0x00008000\n"
    b"edges 6\n"
    b"range 0x00000000 code16\n"
    b"range 0x0000001f data\n"
    b"range 0x00000210 code32\n"
    b"range 0x00000254 data\n"
    b"edge 0x00000017 far local 0x00000210 0x00008210 32 0x00000008\n"
    b"edge 0x0000022f relative local 0x0000023a 0x0000823a 32 0x00000000\n"
    b"edge 0x00000235 relative local 0x00000229 0x00008229 32 0x00000000\n"
    b"edge 0x00000238 relative local 0x00000237 0x00008237 32 0x00000000\n"
    b"edge 0x00000250 indirect unprovable - - unknown -\n"
    b"edge 0x00000252 relative local 0x00000237 0x00008237 32 0x00000000\n"
)


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

    def _write_assembler_outputs(
        self,
        private_root: Path,
        arguments,
        candidate_payload: bytes,
        *,
        map_payload: bytes | None = SMP_RAW_MAP,
    ) -> None:
        string_arguments = tuple(str(argument) for argument in arguments)
        output_index = string_arguments.index("-o") + 1
        (private_root / string_arguments[output_index]).write_bytes(
            candidate_payload
        )
        if map_payload is not None:
            map_index = string_arguments.index("--map") + 1
            (private_root / string_arguments[map_index]).write_bytes(map_payload)

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
                string_arguments = tuple(str(argument) for argument in arguments)
                calls.append((tool_name, string_arguments))
                if tool_name == "cupidasm":
                    self.assertEqual(
                        string_arguments,
                        (
                            "-f",
                            "bin",
                            "--map",
                            ".cupid-output/smp_trampoline.bin.cupidmap",
                            "-o",
                            ".cupid-output/smp_trampoline.bin",
                            "kernel/smp/smp_trampoline.S",
                        ),
                    )
                    self._write_assembler_outputs(
                        private_root,
                        arguments,
                        candidate_payload,
                    )
                else:
                    self.assertEqual(tool_name, "cupiddis")
                    self.assertEqual(
                        string_arguments,
                        (
                            "--raw",
                            "--range-map",
                            ".cupid-output/smp_trampoline.bin.cupidmap",
                            "--require-known",
                            "--require-local-targets",
                            "--require-source-edges",
                            ".cupid-output/smp_trampoline.bin",
                        ),
                    )
                    self.assertEqual(
                        (private_root / string_arguments[2]).read_bytes(),
                        SMP_RAW_MAP,
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
                first = self._run_cli(root, seed)
                second = self._run_cli(root, seed)

            self.assertEqual(
                [name for name, _ in calls],
                ["cupidasm", "cupiddis", "cupidasm", "cupiddis"],
            )
            self.assertEqual(first, (0, "", ""))
            self.assertEqual(second, (0, "", ""))
            self.assertEqual(output.read_bytes(), candidate_payload)
            self.assertTrue(private_roots)
            self.assertTrue(all(not path.exists() for path in private_roots))
            self.assertEqual(list(root.rglob("*.cupidmap")), [])

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
                    self._write_assembler_outputs(
                        private_root,
                        arguments,
                        bytes(4096),
                    )
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

    def test_output_lock_rejects_work_and_preserves_the_trampoline(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-smp-trampoline-lock-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            original = output.read_bytes()
            lock = hostbuild._acquire_disk_publication_lock(output.resolve())
            try:
                with mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "checked tools must not run while the output is locked"
                    ),
                ):
                    status, stdout, stderr = self._run_cli(root, seed)
            finally:
                hostbuild._release_disk_publication_lock(lock)

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("another hostbuild publisher is active", stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_replaced_output_parent_stops_publication_without_leaking_candidates(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-smp-trampoline-parent-swap-"
        ) as temporary:
            root = Path(temporary)
            seed, source, output = self._write_fixture(root)
            original = output.read_bytes()
            displaced = root / "original-kernel"
            replacement = root / "replacement-kernel"
            replacement.mkdir()
            replacement_source = replacement / "smp" / "smp_trampoline.S"
            replacement_source.parent.mkdir()
            replacement_source.write_bytes(source.read_bytes())
            (replacement / "smp_trampoline.bin").write_bytes(
                b"competing trampoline"
            )
            (replacement / "keep.txt").write_bytes(b"keep replacement")
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
                if tool_name == "cupidasm":
                    self._write_assembler_outputs(
                        private_root,
                        arguments,
                        bytes(4096),
                    )
                elif os.name != "nt":
                    output.parent.rename(displaced)
                    replacement.rename(output.parent)
                return subprocess.CompletedProcess(list(arguments), 0, "", "")

            parent_guard = (
                mock.patch(
                    "tools.hostbuild._require_code_output_parent_unchanged",
                    side_effect=hostbuild.CodeValidationError(
                        "code output parent changed while checked tools ran"
                    ),
                )
                if os.name == "nt"
                else contextlib.nullcontext()
            )
            with (
                parent_guard,
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
            self.assertIn(
                "code output parent changed while checked tools ran",
                stderr,
            )
            preserved_replacement = (
                replacement if os.name == "nt" else output.parent
            )
            self.assertEqual(
                (preserved_replacement / "smp_trampoline.bin").read_bytes(),
                b"competing trampoline",
            )
            self.assertEqual(
                (preserved_replacement / "keep.txt").read_bytes(),
                b"keep replacement",
            )
            if os.name == "nt":
                self.assertEqual(output.read_bytes(), original)
            else:
                self.assertEqual(
                    (displaced / "smp_trampoline.bin").read_bytes(),
                    original,
                )
            self.assertFalse(
                any(
                    path.is_dir() and path.name.startswith(".smp-trampoline-")
                    for path in root.rglob("*")
                )
            )
            self.assertEqual(list(root.rglob(".cupid-output")), [])

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
            (
                "missing range map",
                {"map_payload": None},
                "SMP trampoline range map does not exist",
            ),
            (
                "empty range map",
                {"map_payload": b""},
                "SMP trampoline range map may not be empty",
            ),
            (
                "malformed range map",
                {
                    "map_payload": (
                        b"cupid.raw-map.v2\n"
                        b"size 4096\n"
                        b"base 0x00008000\n"
                    )
                },
                "range map does not match the required layout policy",
            ),
            (
                "range boundary drift",
                {
                    "map_payload": SMP_RAW_MAP.replace(
                        b"0x0000001f data", b"0x00000020 data"
                    )
                },
                "range map does not match the required layout policy",
            ),
            (
                "range map drift",
                {"drift": "map"},
                "SMP trampoline range map changed while CupidDis ran",
            ),
            (
                "published output drift",
                {"drift": "output"},
                "code output changed while checked tools ran",
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
                    candidate = (
                        private_root
                        / ".cupid-output"
                        / "smp_trampoline.bin"
                    )
                    range_map = Path(str(candidate) + ".cupidmap")
                    if tool_name == "cupidasm":
                        self._write_assembler_outputs(
                            private_root,
                            arguments,
                            bytes(options.get("candidate_size", 4096)),
                            map_payload=options.get(
                                "map_payload", SMP_RAW_MAP
                            ),
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
                    if options.get("drift") == "candidate":
                        candidate.write_bytes(bytes([1]) * 4096)
                    if options.get("drift") == "map":
                        range_map.write_bytes(
                            SMP_RAW_MAP.replace(
                                b"0x00000254 data", b"0x00000255 data"
                            )
                        )
                    if options.get("drift") == "output":
                        output.write_bytes(b"competing publisher")
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
                expected_output = (
                    b"competing publisher"
                    if options.get("drift") == "output"
                    else original
                )
                self.assertEqual(output.read_bytes(), expected_output)
                self.assertTrue(private_roots)
                self.assertTrue(all(not path.exists() for path in private_roots))
                self.assertEqual(list(root.rglob("*.cupidmap")), [])


if __name__ == "__main__":
    unittest.main()

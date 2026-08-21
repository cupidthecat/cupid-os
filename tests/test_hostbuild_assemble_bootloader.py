import contextlib
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild


class HostbuildAssembleBootloaderTests(unittest.TestCase):
    def _write_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        seed = root / "bootstrap" / "seeds" / "manifest.json"
        source = root / "boot" / "boot.asm"
        output = root / "boot" / "boot.bin"
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
        source.write_text("bits 16\norg 0x7c00\nnop\n", encoding="utf-8")
        output.write_bytes(b"last known good bootloader")
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
                    "assemble-bootloader",
                    "--seed-manifest",
                    str(seed),
                    "--root",
                    str(root),
                    "--source",
                    "boot/boot.asm",
                    "--output",
                    "boot/boot.bin",
                ]
            )
        return status, stdout.getvalue(), stderr.getvalue()

    def test_source_map_and_image_pass_one_strict_private_transaction(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-bootloader-map-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            checked_seed = object()
            candidate_payload = bytes(range(256)) * 10
            map_payload = (
                "cupid.raw-map.v1\n"
                "size 2560\n"
                "base 0x7c00\n"
                "range 0 code16\n"
                "range 512 data\n"
                "range 528 code16\n"
            ).encode("ascii")
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
                            "--map",
                            ".cupid-output/boot.bin.cupidmap",
                            "-o",
                            ".cupid-output/boot.bin",
                            "boot/boot.asm",
                        ),
                    )
                    (private_root / string_arguments[5]).write_bytes(
                        candidate_payload
                    )
                    (private_root / string_arguments[3]).write_bytes(map_payload)
                else:
                    self.assertEqual(tool_name, "cupiddis")
                    self.assertEqual(
                        string_arguments,
                        (
                            "--require-known",
                            "--require-local-targets",
                            "--raw",
                            "--range-map",
                            ".cupid-output/boot.bin.cupidmap",
                            ".cupid-output/boot.bin",
                        ),
                    )
                    self.assertEqual(
                        (private_root / string_arguments[-1]).read_bytes(),
                        candidate_payload,
                    )
                    self.assertEqual(
                        (private_root / string_arguments[-2]).read_bytes(),
                        map_payload,
                    )
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
                status, stdout, stderr = self._run_cli(root, seed)

            self.assertEqual([name for name, _ in calls], ["cupidasm", "cupiddis"])
            self.assertEqual(status, 0)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertEqual(output.read_bytes(), candidate_payload)

    def test_map_failures_and_drift_preserve_the_published_bootloader(self):
        cases = (
            ("missing map", "missing-map", "range map does not exist"),
            ("empty map", "empty-map", "range map may not be empty"),
            (
                "source drift",
                "source-drift",
                "code input changed while CupidASM ran",
            ),
            (
                "seed drift",
                "seed-drift",
                "checked seed inputs changed while checked tools ran",
            ),
            ("map drift", "map-drift", "range map changed while CupidDis ran"),
            (
                "candidate drift",
                "candidate-drift",
                "checked CupidASM output changed while CupidDis ran",
            ),
            (
                "published output drift",
                "output-drift",
                "code output changed while checked tools ran",
            ),
        )
        for name, failure, expected in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix="hostbuild-bootloader-reject-"
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
                    candidate = private_root / ".cupid-output" / "boot.bin"
                    range_map = private_root / ".cupid-output" / "boot.bin.cupidmap"
                    if tool_name == "cupidasm":
                        candidate.write_bytes(bytes(2560))
                        if failure != "missing-map":
                            range_map.write_bytes(
                                b"" if failure == "empty-map" else b"map\n"
                            )
                        if failure == "source-drift":
                            source.write_text(
                                "bits 16\norg 0x7c00\ncli\n",
                                encoding="utf-8",
                            )
                        if failure == "seed-drift":
                            (seed.parent / "cupidasm.elf").write_bytes(
                                b"changed assembler"
                            )
                    elif failure == "map-drift":
                        range_map.write_bytes(b"changed map\n")
                    elif failure == "candidate-drift":
                        candidate.write_bytes(bytes([1]) * 2560)
                    elif failure == "output-drift":
                        output.write_bytes(b"competing publisher")
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

                self.assertEqual(status, 1)
                self.assertEqual(stdout, "")
                self.assertIn(expected, stderr)
                expected_output = (
                    b"competing publisher"
                    if failure == "output-drift"
                    else original
                )
                self.assertEqual(output.read_bytes(), expected_output)

    def test_output_lock_rejects_work_and_preserves_the_bootloader(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-bootloader-lock-"
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
            prefix="hostbuild-bootloader-parent-swap-"
        ) as temporary:
            root = Path(temporary)
            seed, source, output = self._write_fixture(root)
            original = output.read_bytes()
            displaced = root / "original-boot"
            replacement = root / "replacement-boot"
            replacement.mkdir()
            (replacement / "boot.asm").write_bytes(source.read_bytes())
            (replacement / "boot.bin").write_bytes(b"competing bootloader")
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
                    (private_root / ".cupid-output" / "boot.bin").write_bytes(
                        bytes(2560)
                    )
                    (
                        private_root / ".cupid-output" / "boot.bin.cupidmap"
                    ).write_bytes(b"map\n")
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
                (preserved_replacement / "boot.bin").read_bytes(),
                b"competing bootloader",
            )
            self.assertEqual(
                (preserved_replacement / "keep.txt").read_bytes(),
                b"keep replacement",
            )
            if os.name == "nt":
                self.assertEqual(output.read_bytes(), original)
            else:
                self.assertEqual((displaced / "boot.bin").read_bytes(), original)
            self.assertFalse(
                any(
                    path.is_dir() and path.name.startswith(".bootloader-")
                    for path in root.rglob("*")
                )
            )
            self.assertEqual(list(root.rglob(".cupid-output")), [])

    def test_cupiddis_rejection_preserves_the_published_bootloader(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-bootloader-cupiddis-reject-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
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
                if tool_name == "cupidasm":
                    (private_root / ".cupid-output" / "boot.bin").write_bytes(
                        bytes(2560)
                    )
                    (
                        private_root / ".cupid-output" / "boot.bin.cupidmap"
                    ).write_bytes(b"map\n")
                    return subprocess.CompletedProcess(
                        list(arguments), 0, "", ""
                    )
                return subprocess.CompletedProcess(
                    list(arguments),
                    9,
                    "",
                    "cupiddis: boot image has an unknown opcode at 0x7c20\n",
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
                "cupiddis: boot image has an unknown opcode at 0x7c20\n",
            )
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()

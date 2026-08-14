import builtins
import contextlib
import io
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild


def _relocatable_object(
    text: bytes = b"\x90\xc3",
    *,
    text_flags: int = 0x6,
) -> bytes:
    section_names = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    text_offset = 64
    symbol_offset = (text_offset + len(text) + 3) & ~3
    string_offset = symbol_offset + 16
    names_offset = string_offset + 1
    section_offset = (names_offset + len(section_names) + 3) & ~3
    image = bytearray(section_offset + 5 * 40)
    image[0:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        1,
        3,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        5,
        4,
    )
    image[text_offset : text_offset + len(text)] = text
    image[string_offset] = 0
    image[names_offset : names_offset + len(section_names)] = section_names
    sections = (
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, text_flags, 0, text_offset, len(text), 0, 0, 16, 0),
        (7, 2, 0, 0, symbol_offset, 16, 3, 1, 4, 16),
        (15, 3, 0, 0, string_offset, 1, 0, 0, 1, 0),
        (
            23,
            3,
            0,
            0,
            names_offset,
            len(section_names),
            0,
            0,
            1,
            0,
        ),
    )
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


class HostbuildAssembleCupidAsmObjectTests(unittest.TestCase):
    def _write_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        seed = root / "bootstrap" / "seeds" / "manifest.json"
        source = root / "kernel" / "cpu" / "isr.asm"
        output = root / "kernel" / "cpu" / "isr.o"
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
        source.write_text("bits 32\nglobal isr\nisr: ret\n", encoding="utf-8")
        output.write_bytes(b"last known good object")
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
                    "assemble-cupidasm-object",
                    "--seed-manifest",
                    str(seed),
                    "--root",
                    str(root),
                    "--source",
                    "kernel/cpu/isr.asm",
                    "--output",
                    "kernel/cpu/isr.o",
                ]
            )
        return status, stdout.getvalue(), stderr.getvalue()

    def test_checked_assembler_object_passes_structural_and_strict_decode_gates(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-cupidasm-object-success-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            checked_seed = object()
            candidate = _relocatable_object()
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
                            "elf32",
                            "-o",
                            ".cupid-output/isr.o",
                            "kernel/cpu/isr.asm",
                        ),
                    )
                    (private_root / string_arguments[3]).write_bytes(candidate)
                else:
                    self.assertEqual(tool_name, "cupiddis")
                    self.assertEqual(
                        string_arguments,
                        ("--require-known", ".cupid-output/isr.o"),
                    )
                    self.assertEqual(
                        (private_root / string_arguments[-1]).read_bytes(),
                        candidate,
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
            self.assertEqual(output.read_bytes(), candidate)

    def test_malformed_object_is_rejected_before_cupiddis_runs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-cupidasm-object-malformed-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            original = output.read_bytes()
            checked_seed = object()
            calls: list[str] = []

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
                calls.append(tool_name)
                if tool_name == "cupidasm":
                    private_root = Path(working_directory)
                    (private_root / str(arguments[3])).write_bytes(b"not ELF")
                    return subprocess.CompletedProcess(
                        list(arguments), 0, "", ""
                    )
                self.fail("CupidDis must not inspect a malformed object")

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

            self.assertEqual(calls, ["cupidasm"])
            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("relocatable object validation failed", stderr)
            self.assertIn("ELF header is outside the emitted object", stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_validator_load_failure_preserves_the_published_object(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-cupidasm-object-validator-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            original = output.read_bytes()
            real_import = builtins.__import__

            def import_with_broken_validator(name, *args, **kwargs):
                if name == "tools.cupidc_kernel_compile":
                    raise SyntaxError("fixture validator syntax failure")
                return real_import(name, *args, **kwargs)

            with (
                mock.patch(
                    "builtins.__import__",
                    side_effect=import_with_broken_validator,
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "checked tools must not run without the validator"
                    ),
                ),
            ):
                status, stdout, stderr = self._run_cli(root, seed)

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("object validator could not be loaded", stderr)
            self.assertIn("fixture validator syntax failure", stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_object_without_executable_bytes_is_rejected_before_cupiddis_runs(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-cupidasm-object-no-code-"
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
                if tool_name == "cupidasm":
                    private_root = Path(working_directory)
                    (private_root / str(arguments[3])).write_bytes(
                        _relocatable_object(b"data", text_flags=0x2)
                    )
                    return subprocess.CompletedProcess(
                        list(arguments), 0, "", ""
                    )
                self.fail("CupidDis must not accept an object without code")

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
            self.assertIn(
                "has no executable section bytes",
                stderr,
            )
            self.assertEqual(output.read_bytes(), original)

    def test_incomplete_executable_decode_preserves_the_published_object(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-cupidasm-object-decode-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            original = output.read_bytes()
            checked_seed = object()
            diagnostic = (
                "cupiddis: strict decode failed: decoded=1 unknown=1 "
                "invalid=0 truncated=0 executable=2\n"
            )

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
                if tool_name == "cupidasm":
                    private_root = Path(working_directory)
                    (private_root / str(arguments[3])).write_bytes(
                        _relocatable_object(b"\x90\xff")
                    )
                    return subprocess.CompletedProcess(
                        list(arguments), 0, "", ""
                    )
                self.assertEqual(tool_name, "cupiddis")
                return subprocess.CompletedProcess(
                    list(arguments), 23, "", diagnostic
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

            self.assertEqual(status, 23)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, diagnostic)
            self.assertEqual(output.read_bytes(), original)

    def test_input_candidate_and_output_races_stop_publication(self):
        cases = (
            (
                "source drift",
                "source",
                "code input changed while CupidASM ran",
            ),
            (
                "seed drift",
                "seed",
                "checked seed inputs changed while checked tools ran",
            ),
            (
                "candidate drift",
                "candidate",
                "checked CupidASM output changed while CupidDis ran",
            ),
            (
                "output drift",
                "output",
                "code output changed while checked tools ran",
            ),
        )
        for label, drift, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="hostbuild-cupidasm-object-race-"
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
                    candidate = private_root / ".cupid-output" / "isr.o"
                    if tool_name == "cupidasm":
                        candidate.write_bytes(_relocatable_object())
                        if drift == "source":
                            source.write_text(
                                "bits 32\nglobal isr\nisr: cli\n",
                                encoding="utf-8",
                            )
                        return subprocess.CompletedProcess(
                            list(arguments), 0, "", ""
                        )

                    self.assertEqual(tool_name, "cupiddis")
                    if drift == "seed":
                        (seed.parent / "cupiddis.elf").write_bytes(
                            b"changed disassembler"
                        )
                    elif drift == "candidate":
                        candidate.write_bytes(_relocatable_object(b"\xcc\xc3"))
                    elif drift == "output":
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
                    b"competing publisher" if drift == "output" else original
                )
                self.assertEqual(output.read_bytes(), expected_output)
                self.assertEqual(
                    [
                        path
                        for path in root.iterdir()
                        if path.name.startswith(".cupidasm-object-")
                    ],
                    [],
                )

    def test_output_lock_rejects_parallel_object_publication(self):
        with tempfile.TemporaryDirectory(
            prefix="hostbuild-cupidasm-object-lock-"
        ) as temporary:
            root = Path(temporary)
            seed, _, output = self._write_fixture(root)
            original = output.read_bytes()
            lock = hostbuild._acquire_disk_publication_lock(output.resolve())
            try:
                with mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "checked tools must not run while publication is locked"
                    ),
                ):
                    status, stdout, stderr = self._run_cli(root, seed)
            finally:
                hostbuild._release_disk_publication_lock(lock)

            self.assertEqual(status, 1)
            self.assertEqual(stdout, "")
            self.assertIn("another hostbuild publisher is active", stderr)
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()

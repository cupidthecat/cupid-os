## @file
#  @brief Verify production linking and publication of user executables.
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tests.cupidc_production_fixtures import (
    SEED_MANIFEST,
    _minimal_pe64_console_image,
    _run_captured_test_tool,
    _valid_elf32_object,
    _valid_user_executable,
)
from tools import bootstrap_toolchain
from tools import cupidld_user_link as user_link


class FakeLinkRunner:
    def __init__(self, payload=None, result=None, results=None, mutate=None):
        self.payload = payload
        self.result = result or subprocess.CompletedProcess([], 0, "", "")
        self.results = results
        self.mutate = mutate
        self.calls = []

    def run(self, executable, arguments, timeout):
        self.calls.append((executable, tuple(arguments), timeout))
        if self.payload is not None and "-o" in arguments:
            output = Path(arguments[arguments.index("-o") + 1])
            output.write_bytes(self.payload)
        if self.mutate is not None:
            self.mutate()
        if self.results is not None:
            return self.results[len(self.calls) - 1]
        return self.result


class UserLinkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "user/build").mkdir(parents=True)
        self.source = self.root / "user/build/hello.o"
        self.output = self.root / "user/build/hello"
        self.source.write_bytes(_valid_elf32_object())
        self.seed = self.root / "cupidld.elf"
        self.seed.write_bytes(b"seed")
        self.disassembler = self.root / "cupiddis.elf"
        self.disassembler.write_bytes(b"disassembler")
        self.seed_inputs = SimpleNamespace(
            tools={
                "cupidld": self.seed,
                "cupiddis": self.disassembler,
            }
        )
        patcher = mock.patch.object(
            user_link,
            "run_seed_tool",
            side_effect=_run_captured_test_tool,
        )
        self.run_seed_tool = patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        self.temporary.cleanup()

    def test_entry_may_follow_the_external_arena_base(self):
        user_link.validate_user_executable_bytes(
            _valid_user_executable(entry=0x01C00004)
        )

    def test_entry_must_be_in_executable_file_backed_bytes(self):
        with self.assertRaisesRegex(
            user_link.UserLinkError,
            "entry point is not in executable file-backed bytes",
        ):
            user_link.validate_user_executable_bytes(
                _valid_user_executable(entry=0x01C00040)
            )

    def test_program_table_matches_the_kernel_loader_contract(self):
        cases = []

        unsupported = bytearray(_valid_user_executable())
        struct.pack_into("<I", unsupported, 52, 4)
        cases.append(
            ("unsupported type", unsupported, "unsupported program type")
        )

        stack_payload = bytearray(_valid_user_executable())
        struct.pack_into("<I", stack_payload, 52, 0x6474E551)
        cases.append(
            (
                "non-load payload",
                stack_payload,
                "non-load program header has a payload",
            )
        )

        unknown_flags = bytearray(_valid_user_executable())
        struct.pack_into("<I", unknown_flags, 52 + 24, 8)
        cases.append(
            ("unknown flags", unknown_flags, "unknown permission flags")
        )

        incongruent = bytearray(_valid_user_executable())
        struct.pack_into("<I", incongruent, 52 + 28, 0x1000)
        cases.append(
            ("incongruent alignment", incongruent, "alignment is incongruent")
        )

        overlapping = bytearray(_valid_user_executable())
        struct.pack_into("<H", overlapping, 44, 2)
        overlapping[84:116] = overlapping[52:84]
        struct.pack_into("<I", overlapping, 84 + 8, 0x01C00008)
        struct.pack_into("<I", overlapping, 84 + 12, 0x01C00008)
        cases.append(
            ("overlapping loads", overlapping, "load segments overlap")
        )

        empty_load = bytearray(_valid_user_executable())
        struct.pack_into("<I", empty_load, 52 + 16, 0)
        struct.pack_into("<I", empty_load, 52 + 20, 0)
        cases.append(
            ("empty load", empty_load, "no nonempty loadable segment")
        )

        too_many = bytearray(_valid_user_executable())
        struct.pack_into("<H", too_many, 44, 17)
        cases.append(
            ("too many headers", too_many, "more than 16 program headers")
        )

        for label, image, message in cases:
            with self.subTest(label=label):
                with self.assertRaisesRegex(user_link.UserLinkError, message):
                    user_link.validate_user_executable_bytes(bytes(image))

    def test_program_table_cannot_overlap_the_elf_header(self):
        payload_offset = 0x100
        image = bytearray(payload_offset + 16)
        image[0:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into(
            "<HHIIIIIHHHHHH",
            image,
            16,
            2,
            3,
            1,
            0x01C00000,
            32,
            0,
            0,
            52,
            32,
            2,
            0,
            0,
            0,
        )
        struct.pack_into(
            "<IIIIIIII",
            image,
            64,
            1,
            payload_offset,
            0x01C00000,
            0x01C00000,
            16,
            16,
            5,
            1,
        )
        image[payload_offset:] = b"\x90" * 16

        with self.assertRaisesRegex(
            user_link.UserLinkError, "invalid program-header offset"
        ):
            user_link.validate_user_executable_bytes(bytes(image))

    def test_program_table_must_fit_the_loader_signed_seek(self):
        image = bytearray(_valid_user_executable())
        struct.pack_into("<I", image, 28, 0x80000000)

        with self.assertRaisesRegex(
            user_link.UserLinkError, "invalid program-header offset"
        ):
            user_link.validate_user_executable_bytes(bytes(image))

    def test_zero_sized_gnu_stack_header_is_allowed(self):
        image = bytearray(_valid_user_executable())
        struct.pack_into("<H", image, 44, 2)
        struct.pack_into(
            "<IIIIIIII",
            image,
            84,
            0x6474E551,
            0,
            0,
            0,
            0,
            0,
            6,
            16,
        )
        user_link.validate_user_executable_bytes(bytes(image))

    def test_checked_link_is_atomic_and_uses_the_fixed_contract(self):
        runner = FakeLinkRunner(payload=_valid_user_executable())
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                runner=runner,
            )
        self.assertEqual(self.output.read_bytes(), _valid_user_executable())
        self.assertEqual(len(runner.calls), 2)
        link_tool, link_arguments, _timeout = runner.calls[0]
        disassembler, disassemble_arguments, _timeout = runner.calls[1]
        self.assertEqual(link_tool, self.seed)
        self.assertEqual(disassembler, self.disassembler)
        self.assertIn("0x01C00000", link_arguments)
        self.assertIn("_start", link_arguments)
        private_output = link_arguments[link_arguments.index("-o") + 1]
        self.assertNotEqual(private_output, self.output)
        self.assertEqual(
            disassemble_arguments,
            (
                "--require-known",
                "--require-local-targets",
                "--require-code-anchors",
                private_output,
            ),
        )

    def test_auto_mode_uses_the_checked_linker_on_windows(self):
        runner = FakeLinkRunner(payload=_valid_user_executable())
        windows_os = SimpleNamespace(name="nt", replace=os.replace)

        def run_checked_link(
            _manifest,
            _root,
            tool,
            arguments,
            *,
            timeout,
            frozen_seed,
            runner: object,
        ):
            self.assertIsNone(runner)
            return checked_runner.run(
                frozen_seed.tools[tool], arguments, timeout
            )

        checked_runner = runner
        with (
            mock.patch.object(user_link, "os", windows_os),
            mock.patch.object(
                user_link,
                "run_seed_tool",
                side_effect=run_checked_link,
            ) as run_seed,
            mock.patch.object(
                user_link,
                "freeze_seed_inputs",
                return_value=self.seed_inputs,
            ) as freeze_seed,
            mock.patch.object(
                user_link,
                "capture_native_tool",
                side_effect=AssertionError(
                    "auto mode selected the native linker"
                ),
            ),
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
            )

        self.assertEqual(self.output.read_bytes(), _valid_user_executable())
        self.assertEqual(len(runner.calls), 2)
        self.assertEqual(run_seed.call_count, 2)
        expected = (
            self.root
            / "bootstrap/seeds/i386-windows/manifest.json"
        )
        freeze_seed.assert_called_once_with(expected, mock.ANY)
        self.assertTrue(
            all(call.args[0] == expected for call in run_seed.call_args_list)
        )
        self.assertEqual(
            [call.args[2] for call in run_seed.call_args_list],
            ["cupidld", "cupiddis"],
        )
        self.assertEqual(
            run_seed.call_args_list[1].args[3][:-1],
            (
                "--require-known",
                "--require-local-targets",
                "--require-code-anchors",
            ),
        )

    def test_native_link_uses_a_private_tool_snapshot_without_seed_access(self):
        native = self.root / "toolchain/build/cupidld.exe"
        native.parent.mkdir(parents=True)
        native.write_bytes(_minimal_pe64_console_image())
        runner = FakeLinkRunner(payload=_valid_user_executable())
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            side_effect=AssertionError("native link read the Linux seed"),
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                native_linker=native,
                runner=runner,
            )

        invoked = Path(runner.calls[0][0])
        self.assertNotEqual(invoked, native)
        self.assertEqual(
            self.output.read_bytes(), _valid_user_executable()
        )

    def test_native_link_rejects_an_unapproved_tool_path(self):
        self.output.write_bytes(b"previous executable")
        native = self.root / "other/cupidld.exe"
        native.parent.mkdir()
        native.write_bytes(_minimal_pe64_console_image())
        runner = FakeLinkRunner(payload=_valid_user_executable())
        with self.assertRaisesRegex(
            user_link.UserLinkError,
            "native CupidLD must be",
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                native_linker=native,
                runner=runner,
            )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_native_link_rejects_tool_drift_and_preserves_output(self):
        self.output.write_bytes(b"previous executable")
        native = self.root / "toolchain/build/cupidld.exe"
        native.parent.mkdir(parents=True)
        native.write_bytes(_minimal_pe64_console_image())

        def mutate_tool():
            native.write_bytes(_minimal_pe64_console_image() + b"changed")

        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            mutate=mutate_tool,
        )
        with self.assertRaisesRegex(
            user_link.UserLinkError,
            "native CupidLD changed while linking",
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                native_linker=native,
                runner=runner,
            )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_native_link_rejects_a_checked_seed_manifest(self):
        self.output.write_bytes(b"previous executable")
        native = self.root / "toolchain/build/cupidld.exe"
        native.parent.mkdir(parents=True)
        native.write_bytes(_minimal_pe64_console_image())

        with self.assertRaisesRegex(
            user_link.UserLinkError,
            "manifest cannot be combined with a native linker",
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                manifest=self.root / "manifest.json",
                native_linker=native,
                runner=FakeLinkRunner(payload=_valid_user_executable()),
            )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_link_failure_preserves_the_previous_executable(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            result=subprocess.CompletedProcess([], 9, "", "missing entry")
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "status 9: missing entry",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_disassembly_failure_preserves_the_previous_executable(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            results=(
                subprocess.CompletedProcess([], 0, "", ""),
                subprocess.CompletedProcess(
                    [], 7, "", "code check failed"
                ),
            ),
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "CupidDis rejected hello with status 7: code check failed",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )
        self.assertEqual(len(runner.calls), 2)
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_disassembly_output_preserves_the_previous_executable(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            results=(
                subprocess.CompletedProcess([], 0, "", ""),
                subprocess.CompletedProcess(
                    [], 0, "unexpected listing", ""
                ),
            ),
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "CupidDis wrote unexpected standard output for hello",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_disassembly_error_output_preserves_the_previous_executable(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            results=(
                subprocess.CompletedProcess([], 0, "", ""),
                subprocess.CompletedProcess(
                    [], 0, "", "unexpected diagnostic"
                ),
            ),
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "CupidDis wrote unexpected standard error for hello",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_disassembly_candidate_drift_preserves_the_previous_executable(self):
        self.output.write_bytes(b"previous executable")
        runner = mock.Mock()

        def run(_tool, arguments, _timeout):
            if "-o" in arguments:
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(_valid_user_executable())
            else:
                candidate = Path(arguments[-1])
                candidate.write_bytes(_valid_user_executable() + b"changed")
            return subprocess.CompletedProcess([], 0, "", "")

        runner.run.side_effect = run
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "CupidDis candidate changed while it was inspected",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )

        self.assertEqual(runner.run.call_count, 2)
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_live_seed_drift_after_link_preserves_the_previous_executable(self):
        copied_seed = self.root / "bootstrap/seeds/i386-linux"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"
        live_linker = copied_seed / "cupidld.elf"
        self.output.write_bytes(b"previous executable")

        def mutate_seed():
            image = bytearray(live_linker.read_bytes())
            image[-1] ^= 0x01
            live_linker.write_bytes(image)

        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            mutate=mutate_seed,
        )
        with (
            mock.patch.object(
                user_link,
                "run_seed_tool",
                new=bootstrap_toolchain.run_seed_tool,
            ),
            self.assertRaisesRegex(
                user_link.UserLinkError,
                "checked seed inputs changed while CupidLD ran: "
                "SHA-256 differs for cupidld.elf",
            ),
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                manifest=manifest,
                runner=runner,
            )

        self.assertEqual(len(runner.calls), 1)
        self.assertNotEqual(runner.calls[0][0], live_linker)
        link_arguments = runner.calls[0][1]
        link_output = link_arguments[link_arguments.index("-o") + 1]
        link_input = link_arguments[-1]
        self.assertIsInstance(link_output, Path)
        self.assertTrue(link_output.is_absolute())
        self.assertIsInstance(link_input, Path)
        self.assertTrue(link_input.is_absolute())
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_live_seed_drift_during_disassembly_preserves_the_previous_executable(
        self,
    ):
        copied_seed = self.root / "bootstrap/seeds/i386-linux"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"
        live_disassembler = copied_seed / "cupiddis.elf"
        self.output.write_bytes(b"previous executable")
        runner = mock.Mock()

        def run(_tool, arguments, _timeout):
            if "-o" in arguments:
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(_valid_user_executable())
                return subprocess.CompletedProcess([], 0, "", "")
            image = bytearray(live_disassembler.read_bytes())
            image[-1] ^= 0x01
            live_disassembler.write_bytes(image)
            return subprocess.CompletedProcess([], 0, "", "")

        runner.run.side_effect = run
        with (
            mock.patch.object(
                user_link,
                "run_seed_tool",
                new=bootstrap_toolchain.run_seed_tool,
            ),
            self.assertRaisesRegex(
                user_link.UserLinkError,
                "checked seed inputs changed while CupidDis ran: "
                "SHA-256 differs for cupiddis.elf",
            ),
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                manifest=manifest,
                runner=runner,
            )

        self.assertEqual(runner.run.call_count, 2)
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_checked_runner_failures_keep_link_input_context(self):
        copied_seed = self.root / "bootstrap/seeds/i386-linux"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"
        cases = (
            (
                subprocess.TimeoutExpired(["cupidld"], 23),
                "CupidLD timed out after 23 seconds for hello.o",
            ),
            (
                OSError("fixture launch failure"),
                "CupidLD could not run for hello.o: fixture launch failure",
            ),
        )
        for failure, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                self.output.write_bytes(b"previous executable")
                runner = mock.Mock()
                runner.run.side_effect = failure
                with (
                    mock.patch.object(
                        user_link,
                        "run_seed_tool",
                        new=bootstrap_toolchain.run_seed_tool,
                    ),
                    self.assertRaisesRegex(
                        user_link.UserLinkError,
                        re.escape(diagnostic),
                    ),
                ):
                    user_link.link_user_program(
                        self.root,
                        Path("user/build/hello.o"),
                        Path("user/build/hello"),
                        manifest=manifest,
                        runner=runner,
                        timeout=23,
                    )
                runner.run.assert_called_once()
                self.assertEqual(
                    self.output.read_bytes(),
                    b"previous executable",
                )

    def test_checked_disassembler_runner_failures_preserve_output(self):
        copied_seed = self.root / "bootstrap/seeds/i386-linux"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"
        cases = (
            (
                subprocess.TimeoutExpired(["cupiddis"], 23),
                "CupidDis timed out after 23 seconds for hello",
            ),
            (
                OSError("fixture launch failure"),
                "CupidDis could not run for hello: fixture launch failure",
            ),
        )
        for failure, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                self.output.write_bytes(b"previous executable")
                runner = mock.Mock()

                def run(_tool, arguments, _timeout):
                    if "-o" in arguments:
                        candidate = Path(
                            arguments[arguments.index("-o") + 1]
                        )
                        candidate.write_bytes(_valid_user_executable())
                        return subprocess.CompletedProcess([], 0, "", "")
                    raise failure

                runner.run.side_effect = run
                with (
                    mock.patch.object(
                        user_link,
                        "run_seed_tool",
                        new=bootstrap_toolchain.run_seed_tool,
                    ),
                    self.assertRaisesRegex(
                        user_link.UserLinkError,
                        re.escape(diagnostic),
                    ),
                ):
                    user_link.link_user_program(
                        self.root,
                        Path("user/build/hello.o"),
                        Path("user/build/hello"),
                        manifest=manifest,
                        runner=runner,
                        timeout=23,
                    )
                self.assertEqual(runner.run.call_count, 2)
                self.assertEqual(
                    self.output.read_bytes(),
                    b"previous executable",
                )

    def test_link_rejects_a_changed_input_object(self):
        self.output.write_bytes(b"previous executable")
        runner = FakeLinkRunner(
            payload=_valid_user_executable(),
            mutate=lambda: self.source.write_bytes(_valid_elf32_object() + b"x"),
        )
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "input object changed",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
            )
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_disassembly_rejects_a_changed_input_object(self):
        self.output.write_bytes(b"previous executable")
        runner = mock.Mock()

        def run(_tool, arguments, _timeout):
            if "-o" in arguments:
                candidate = Path(arguments[arguments.index("-o") + 1])
                candidate.write_bytes(_valid_user_executable())
            else:
                self.source.write_bytes(_valid_elf32_object() + b"changed")
            return subprocess.CompletedProcess([], 0, "", "")

        runner.run.side_effect = run
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError,
                "input object changed while linking hello.o",
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )

        self.assertEqual(runner.run.call_count, 2)
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_linker_reads_an_immutable_copy_before_rejecting_live_drift(self):
        original = self.source.read_bytes()
        self.output.write_bytes(b"previous executable")

        class MutatingLinkRunner:
            def __init__(self, live_source):
                self.live_source = live_source
                self.link_input = None
                self.link_payload = None

            def run(self, _executable, arguments, _timeout):
                if "-o" not in arguments:
                    return subprocess.CompletedProcess([], 0, "", "")
                self.live_source.write_bytes(original + b"changed")
                self.link_input = Path(arguments[-1])
                self.link_payload = self.link_input.read_bytes()
                output = Path(arguments[arguments.index("-o") + 1])
                output.write_bytes(_valid_user_executable())
                return subprocess.CompletedProcess([], 0, "", "")

        runner = MutatingLinkRunner(self.source)
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            with self.assertRaisesRegex(
                user_link.UserLinkError, "input object changed"
            ):
                user_link.link_user_program(
                    self.root,
                    Path("user/build/hello.o"),
                    Path("user/build/hello"),
                    runner=runner,
                )

        self.assertNotEqual(runner.link_input, self.source)
        self.assertEqual(runner.link_payload, original)
        self.assertEqual(self.output.read_bytes(), b"previous executable")

    def test_restored_live_input_cannot_change_the_linked_snapshot(self):
        original = self.source.read_bytes()

        class RestoringLinkRunner:
            def __init__(self, live_source):
                self.live_source = live_source
                self.link_payload = None

            def run(self, _executable, arguments, _timeout):
                if "-o" not in arguments:
                    return subprocess.CompletedProcess([], 0, "", "")
                self.live_source.write_bytes(original + b"transient")
                try:
                    self.link_payload = Path(arguments[-1]).read_bytes()
                finally:
                    self.live_source.write_bytes(original)
                output = Path(arguments[arguments.index("-o") + 1])
                output.write_bytes(_valid_user_executable())
                return subprocess.CompletedProcess([], 0, "", "")

        runner = RestoringLinkRunner(self.source)
        with mock.patch.object(
            user_link,
            "freeze_seed_inputs",
            return_value=self.seed_inputs,
        ):
            user_link.link_user_program(
                self.root,
                Path("user/build/hello.o"),
                Path("user/build/hello"),
                runner=runner,
            )

        self.assertEqual(runner.link_payload, original)
        self.assertEqual(self.output.read_bytes(), _valid_user_executable())


if __name__ == "__main__":
    unittest.main()

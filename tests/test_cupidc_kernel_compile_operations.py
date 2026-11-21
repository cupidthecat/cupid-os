## @file
#  @brief Check kernel compilation, artifact validation and publication failures.
import contextlib
import io
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.cupidc_kernel_compile_fixtures import (
    GENERATED_KERNEL_SOURCES,
    OPERAND_FREE_SOURCES,
    PORT_IO_SOURCES,
    REPO_ROOT,
    SEED_MANIFEST,
    SMP_SOURCES,
    FakeExecutor,
    _data_only_elf32_object,
    _run_captured_test_tool,
    _valid_elf32_object,
)
from tools import bootstrap_toolchain
from tools import cupidc_kernel_compile as kernel_compile


class KernelCompileOperationTests(unittest.TestCase):
    def setUp(self):
        patcher = mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=_run_captured_test_tool,
        )
        self.run_seed_tool = patcher.start()
        self.addCleanup(patcher.stop)

    def _root_fixture(self):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name).resolve()
        source = root / "kernel" / "crypto" / "ct.cc"
        source.parent.mkdir(parents=True)
        source.write_text("int ct_fixture;\n", encoding="utf-8")
        for relative in kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES["kernel/crypto/ct.cc"]:
            header = root / relative
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_bytes((REPO_ROOT / relative).read_bytes())
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = root / "build" / "ct.o"
        output.parent.mkdir()
        return temporary, root, source, seed, manifest, output

    def test_transient_permission_error_retries_atomic_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staged = root / "staged.o"
            output = root / "kernel.o"
            staged.write_bytes(b"new object")
            output.write_bytes(b"existing object")
            real_replace = kernel_compile.os.replace
            attempts = []

            def replace(source, destination):
                attempts.append((Path(source), Path(destination)))
                if len(attempts) < 3:
                    raise PermissionError(13, "kernel object still busy")
                real_replace(source, destination)

            with (
                mock.patch.object(
                    kernel_compile.os,
                    "replace",
                    side_effect=replace,
                ),
                mock.patch.object(kernel_compile.time, "sleep") as sleep,
            ):
                kernel_compile._replace_with_retry(staged, output)

            self.assertEqual(len(attempts), 3)
            self.assertEqual(
                [call.args[0] for call in sleep.call_args_list],
                list(kernel_compile.PUBLISH_RETRY_DELAYS_SECONDS[:2]),
            )
            self.assertEqual(output.read_bytes(), b"new object")
            self.assertFalse(staged.exists())

    def test_persistent_permission_error_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staged = root / "staged.o"
            output = root / "kernel.o"
            staged.write_bytes(b"new object")
            output.write_bytes(b"existing object")
            denied = PermissionError(13, "kernel object remains busy")

            with (
                mock.patch.object(
                    kernel_compile.os,
                    "replace",
                    side_effect=denied,
                ) as replace,
                mock.patch.object(kernel_compile.time, "sleep") as sleep,
                self.assertRaises(PermissionError),
            ):
                kernel_compile._replace_with_retry(staged, output)

            self.assertEqual(
                replace.call_count,
                len(kernel_compile.PUBLISH_RETRY_DELAYS_SECONDS) + 1,
            )
            self.assertEqual(
                [call.args[0] for call in sleep.call_args_list],
                list(kernel_compile.PUBLISH_RETRY_DELAYS_SECONDS),
            )
            self.assertEqual(output.read_bytes(), b"existing object")
            self.assertEqual(staged.read_bytes(), b"new object")

    def test_manifest_and_seed_are_frozen_before_successful_execution(self):
        temporary, root, source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        events = []
        captured = {}
        relative_paths = (
            "kernel/crypto/ct.cc",
            *kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES["kernel/crypto/ct.cc"],
        )

        class ClosureExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                compiler_root = Path(arguments[arguments.index("--root") + 1])
                for relative in relative_paths:
                    captured[relative] = (compiler_root / relative).read_bytes()
                return super().run(executable, arguments, timeout)

        executor = ClosureExecutor(
            root,
            payload=_valid_elf32_object(),
            events=events,
        )

        def freeze(path, snapshot_directory):
            self.assertEqual(path, manifest)
            frozen_seed = snapshot_directory / "cupidc.elf"
            frozen_seed.write_bytes(seed.read_bytes())
            events.append("freeze")
            return mock.Mock(tools={"cupidc": frozen_seed})

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=freeze,
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=executor,
            )

        self.assertEqual(events, ["freeze", "run"])
        self.assertEqual(output.read_bytes(), _valid_elf32_object())
        self.assertEqual(
            executor.calls[0][2],
            kernel_compile.DEFAULT_TIMEOUT_SECONDS,
        )
        self.assertNotEqual(executor.calls[0][0], seed)
        self.assertEqual(executor.calls[0][0].name, "cupidc.elf")
        arguments = executor.calls[0][1]
        self.assertEqual(arguments[0:2], ("-c", "/kernel/crypto/ct.cc"))
        compiler_root = arguments[arguments.index("--root") + 1]
        self.assertIsInstance(compiler_root, Path)
        self.assertTrue(compiler_root.is_absolute())
        self.assertNotEqual(compiler_root, root)
        self.assertEqual(captured, {
            relative: (root / relative).read_bytes()
            for relative in relative_paths
        })
        self.run_seed_tool.assert_called_once()
        self.assertEqual(self.run_seed_tool.call_args.args[:3], (
            manifest,
            root,
            "cupidc",
        ))
        self.assertIs(
            self.run_seed_tool.call_args.kwargs["runner"],
            executor,
        )

    def test_live_seed_drift_after_compile_preserves_the_existing_object(self):
        temporary, root, source, _seed, _manifest, output = (
            self._root_fixture()
        )
        self.addCleanup(temporary.cleanup)
        copied_seed = root / "checked-seed"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"
        live_compiler = copied_seed / "cupidc.elf"
        output.write_bytes(b"existing object")

        class DriftingRunner(FakeExecutor):
            def run(self, executable, arguments, timeout):
                result = super().run(executable, arguments, timeout)
                image = bytearray(live_compiler.read_bytes())
                image[-1] ^= 0x01
                live_compiler.write_bytes(image)
                return result

        runner = DriftingRunner(root, payload=_valid_elf32_object())
        with (
            mock.patch.object(
                kernel_compile,
                "run_seed_tool",
                new=bootstrap_toolchain.run_seed_tool,
            ),
            self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "checked seed inputs changed while CupidC ran: "
                "SHA-256 differs for cupidc.elf",
            ),
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=runner,
            )

        self.assertEqual(len(runner.calls), 1)
        self.assertNotEqual(runner.calls[0][0], live_compiler)
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_checked_runner_failures_keep_kernel_source_context(self):
        temporary, root, source, _seed, _manifest, output = (
            self._root_fixture()
        )
        self.addCleanup(temporary.cleanup)
        copied_seed = root / "checked-seed"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"

        class FailingRunner(FakeExecutor):
            def __init__(self, repository, failure):
                super().__init__(repository)
                self.failure = failure

            def run(self, executable, arguments, timeout):
                self.calls.append((executable, tuple(arguments), timeout))
                raise self.failure

        cases = (
            (
                subprocess.TimeoutExpired(["cupidc"], 17),
                "CupidC timed out after 17 seconds for "
                "kernel/crypto/ct.cc",
            ),
            (
                OSError("fixture launch failure"),
                "CupidC could not run for kernel/crypto/ct.cc: "
                "fixture launch failure",
            ),
        )
        for failure, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                output.write_bytes(b"existing object")
                runner = FailingRunner(root, failure)
                with (
                    mock.patch.object(
                        kernel_compile,
                        "run_seed_tool",
                        new=bootstrap_toolchain.run_seed_tool,
                    ),
                    self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        re.escape(diagnostic),
                    ),
                ):
                    kernel_compile.compile_kernel_source(
                        root,
                        source,
                        output,
                        manifest=manifest,
                        executor=runner,
                        timeout=17,
                    )
                self.assertEqual(len(runner.calls), 1)
                self.assertEqual(output.read_bytes(), b"existing object")

    def test_unapproved_source_is_rejected_without_execution(self):
        temporary, root, _source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        executor = FakeExecutor(root)

        for relative in (
            "kernel/core/panic.c",
            "kernel/core/kernel.c",
            "kernel/crypto/ct.c",
            "kernel/audio/nuked_opl3.c",
            "kernel/core/string.c",
            "kernel/crypto/new_cipher.c",
            "kernel/gfx/jpeg.c",
            "kernel/gfx/glyph_raster.c",
            "kernel/gui/terminal_ansi.c",
            "kernel/lang/as.c",
            "kernel/mm/paging.c",
            "kernel/cpu/fpu.c",
            "kernel/cpu/libm.c",
            "kernel/cpu/simd.c",
            "kernel/smp/percpu.c",
            "kernel/smp/smp.c",
        ):
            source = root / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("int unapproved;\n", encoding="utf-8")
            with self.subTest(source=relative), self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "source is outside the approved CupidC kernel cohort",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(executor.calls, [])

    def test_approved_source_without_a_closure_cannot_read_live_inputs(self):
        temporary, root, source, _seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        before = output.stat().st_mtime_ns
        executor = FakeExecutor(root, payload=_valid_elf32_object())
        remaining = {
            name: headers
            for name, headers in kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES.items()
            if name != "kernel/crypto/ct.cc"
        }
        with (
            mock.patch.dict(kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES, remaining, clear=True),
            mock.patch.object(kernel_compile, "freeze_seed_inputs") as freeze,
            self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "approved kernel source has no frozen input closure: kernel/crypto/ct.cc",
            ),
        ):
            kernel_compile.compile_kernel_source(
                root, source, output, manifest=manifest, executor=executor,
            )
        freeze.assert_not_called()
        self.assertEqual(executor.calls, [])
        self.assertEqual(output.read_bytes(), b"existing object")
        self.assertEqual(output.stat().st_mtime_ns, before)

    def _source_driven_closure_fixture(self, relative_source):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        source = root / relative_source
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(
            "int source_driven_fixture;\n",
            encoding="utf-8",
        )
        frozen_inputs = {}
        for index, relative in enumerate(
            kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[relative_source]
        ):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                f"/* frozen input {index} */\n",
                encoding="utf-8",
            )
            frozen_inputs[relative] = path
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.with_suffix(".o")
        return (
            root,
            source,
            frozen_inputs,
            seed,
            manifest,
            output,
        )

    def test_source_driven_inputs_are_compiled_from_one_frozen_closure(self):
        for relative_source in kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES:
            if relative_source in GENERATED_KERNEL_SOURCES:
                continue
            with self.subTest(source=relative_source):
                (
                    root,
                    source,
                    frozen_inputs,
                    seed,
                    manifest,
                    output,
                ) = self._source_driven_closure_fixture(relative_source)
                captured = {}
                relative_paths = (
                    relative_source,
                    *kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES[relative_source],
                )

                class ClosureExecutor(FakeExecutor):
                    def run(self, executable, arguments, timeout):
                        compiler_root = Path(
                            arguments[arguments.index("--root") + 1]
                        )
                        for relative in relative_paths:
                            captured[relative] = (
                                compiler_root / relative
                            ).read_bytes()
                        return super().run(executable, arguments, timeout)

                executor = ClosureExecutor(
                    root,
                    payload=_valid_elf32_object(),
                )
                with mock.patch.object(
                    kernel_compile,
                    "freeze_seed_inputs",
                    side_effect=lambda _manifest, snapshot: mock.Mock(
                        tools={
                            "cupidc": shutil.copyfile(
                                seed,
                                snapshot / seed.name,
                            )
                        }
                    ),
                ):
                    kernel_compile.compile_kernel_source(
                        root,
                        source,
                        output,
                        manifest=manifest,
                        executor=executor,
                    )

                expected = {relative_source: source.read_bytes()}
                expected.update(
                    {
                        relative: path.read_bytes()
                        for relative, path in frozen_inputs.items()
                    }
                )
                self.assertEqual(captured, expected)
                self.assertEqual(
                    output.read_bytes(),
                    _valid_elf32_object(),
                )
                compiler_root = Path(
                    executor.calls[0][1][
                        executor.calls[0][1].index("--root") + 1
                    ]
                )
                self.assertNotEqual(compiler_root, root)

    def test_source_driven_input_drift_preserves_the_existing_object(self):
        for relative_source in kernel_compile.FROZEN_KERNEL_INPUT_CLOSURES:
            if relative_source in GENERATED_KERNEL_SOURCES:
                continue
            with self.subTest(source=relative_source):
                (
                    root,
                    source,
                    frozen_inputs,
                    seed,
                    manifest,
                    output,
                ) = self._source_driven_closure_fixture(relative_source)
                drifted_input = next(iter(frozen_inputs.values()), source)
                output.write_bytes(b"existing object")

                class DriftingExecutor(FakeExecutor):
                    def run(self, executable, arguments, timeout):
                        drifted_input.write_text(
                            "int changed;\n",
                            encoding="utf-8",
                        )
                        return super().run(executable, arguments, timeout)

                executor = DriftingExecutor(
                    root,
                    payload=_valid_elf32_object(),
                )
                with mock.patch.object(
                    kernel_compile,
                    "freeze_seed_inputs",
                    side_effect=lambda _manifest, snapshot: mock.Mock(
                        tools={
                            "cupidc": shutil.copyfile(
                                seed,
                                snapshot / seed.name,
                            )
                        }
                    ),
                ):
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        "kernel inputs changed while compiling "
                        + re.escape(relative_source),
                    ):
                        kernel_compile.compile_kernel_source(
                            root,
                            source,
                            output,
                            manifest=manifest,
                            executor=executor,
                        )

                self.assertEqual(
                    output.read_bytes(),
                    b"existing object",
                )

    def test_generated_kernel_symbol_inputs_are_compiled_from_one_frozen_closure(
        self,
    ):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        source = root / "kernel" / "cpu" / "ksyms_data.cc"
        source.parent.mkdir(parents=True)
        source.write_text(
            '#include "ksyms.h"\nconst int generated_symbol = 1;\n',
            encoding="utf-8",
        )
        header = source.parent / "ksyms.h"
        header.write_text('#include "types.h"\n', encoding="utf-8")
        types = root / "kernel" / "core" / "types.h"
        types.parent.mkdir(parents=True)
        types.write_text("typedef unsigned int uint32_t;\n", encoding="utf-8")
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.parent / "ksyms_data.o"
        captured = {}

        class ClosureExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                compiler_root = Path(
                    arguments[arguments.index("--root") + 1]
                )
                for relative in (
                    "kernel/cpu/ksyms_data.cc",
                    "kernel/cpu/ksyms.h",
                    "kernel/core/types.h",
                ):
                    captured[relative] = (
                        compiler_root / relative
                    ).read_bytes()
                return super().run(executable, arguments, timeout)

        executor = ClosureExecutor(root, payload=_data_only_elf32_object())

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=executor,
            )

        self.assertEqual(
            captured,
            {
                "kernel/cpu/ksyms_data.cc": source.read_bytes(),
                "kernel/cpu/ksyms.h": header.read_bytes(),
                "kernel/core/types.h": types.read_bytes(),
            },
        )
        self.assertEqual(output.read_bytes(), _data_only_elf32_object())
        self.assertEqual(
            executor.calls[0][2],
            kernel_compile.GENERATED_KERNEL_TIMEOUT_SECONDS,
        )
        compiler_root = Path(
            executor.calls[0][1][
                executor.calls[0][1].index("--root") + 1
            ]
        )
        self.assertNotEqual(compiler_root, root)

    def test_generated_kernel_symbol_drift_preserves_the_existing_object(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        source = root / "kernel" / "cpu" / "ksyms_data.cc"
        source.parent.mkdir(parents=True)
        source.write_text(
            '#include "ksyms.h"\nconst int generated_symbol = 1;\n',
            encoding="utf-8",
        )
        header = source.parent / "ksyms.h"
        header.write_text('#include "types.h"\n', encoding="utf-8")
        types = root / "kernel" / "core" / "types.h"
        types.parent.mkdir(parents=True)
        types.write_text("typedef unsigned int uint32_t;\n", encoding="utf-8")
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.parent / "ksyms_data.o"
        output.write_bytes(b"existing object")

        class DriftingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                header.write_text(
                    '#include "types.h"\nint changed;\n',
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = DriftingExecutor(
            root,
            payload=_data_only_elf32_object(),
        )
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "kernel inputs changed while compiling "
                "kernel/cpu/ksyms_data.cc",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )

        self.assertEqual(output.read_bytes(), b"existing object")

    def test_output_outside_root_is_rejected(self):
        temporary, root, source, _seed, manifest, _output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        outside = root.parent / "outside.o"

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "output must stay inside repository root",
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                outside,
                manifest=manifest,
                executor=FakeExecutor(root),
            )

    def test_compiler_failure_preserves_existing_output(self):
        temporary, root, source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        executor = FakeExecutor(
            root,
            result=subprocess.CompletedProcess(
                [],
                1,
                "",
                "/kernel/crypto/ct.cc:9: error CTD000006: unsupported",
            ),
        )

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "CupidC failed for kernel/crypto/ct.cc with status 1.*CTD000006",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_manifest_failure_preserves_output_without_running_compiler(self):
        temporary, root, source, _seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        executor = FakeExecutor(root)

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=kernel_compile.BootstrapError(
                "SHA-256 differs for cupidc.elf"
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "checked seed verification failed.*SHA-256 differs",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(executor.calls, [])
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_invalid_object_preserves_existing_output(self):
        temporary, root, source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        executor = FakeExecutor(root, payload=b"not an object")

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "emitted object is invalid",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_public_validator_rejects_a_bad_pc_relative_addend(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "bad.o"
        image = bytearray(_valid_elf32_object())
        struct.pack_into("<i", image, 56, 0)
        path.write_bytes(image)
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "PC-relative relocation addend is 0, expected -4",
        ):
            kernel_compile.validate_i386_relocatable(path)

    def test_public_validator_accepts_a_nonzero_absolute_addend(self):
        image = bytearray(_valid_elf32_object())
        struct.pack_into("<i", image, 52, 4)

        kernel_compile.validate_i386_relocatable_bytes(image)

    def test_public_validator_accepts_a_data_only_relocatable_object(self):
        kernel_compile.validate_i386_relocatable_bytes(
            _data_only_elf32_object()
        )

    def test_public_validator_can_require_executable_section_bytes(self):
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "emitted object has no executable section bytes",
        ):
            kernel_compile.validate_i386_relocatable_bytes(
                _data_only_elf32_object(),
                require_executable=True,
            )

    def test_public_validator_rejects_a_data_symbol_outside_its_section(self):
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "symbol 1 exceeds its section",
        ):
            kernel_compile.validate_i386_relocatable_bytes(
                _data_only_elf32_object(symbol_size=5)
            )


class KernelCompileCliTests(unittest.TestCase):
    def test_cli_reports_a_clear_failure(self):
        error = io.StringIO()
        with mock.patch.object(
            kernel_compile,
            "compile_kernel_source",
            side_effect=kernel_compile.KernelCompileError("fixture failure"),
        ):
            with contextlib.redirect_stderr(error):
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        "kernel/crypto/ct.cc",
                        "--output",
                        "build/ct.o",
                    ]
                )
        self.assertEqual(status, 1)
        self.assertEqual(
            error.getvalue(),
            "CupidC kernel compile failed: fixture failure\n",
        )

    def test_real_checked_seed_compiles_hmac_with_relocations_when_available(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-compile-test-",
            dir=REPO_ROOT,
        ) as temporary:
            output = Path(temporary) / "hmac.o"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        "kernel/crypto/hmac.cc",
                        "--output",
                        str(output),
                    ]
                )
            self.assertEqual(status, 0)
            self.assertIn("CupidC kernel object:", stdout.getvalue())
            kernel_compile.validate_i386_relocatable(output)

    def test_real_checked_seed_compiles_the_approved_smp_sources_when_available(
        self,
    ):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-smp-compile-test-",
            dir=REPO_ROOT,
        ) as temporary:
            for source in SMP_SOURCES:
                output = Path(temporary) / (Path(source).stem + ".o")
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        source,
                        "--output",
                        str(output),
                    ]
                )
                self.assertEqual(status, 0)
                kernel_compile.validate_i386_relocatable(output)

    def test_real_checked_seed_compiles_operand_free_sources_when_available(
        self,
    ):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-operand-free-test-",
            dir=REPO_ROOT,
        ) as temporary:
            for source in OPERAND_FREE_SOURCES:
                output = Path(temporary) / (Path(source).stem + ".o")
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        source,
                        "--output",
                        str(output),
                    ]
                )
                self.assertEqual(status, 0)
                kernel_compile.validate_i386_relocatable(output)

    def test_real_checked_seed_compiles_port_io_sources_when_available(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-port-io-test-",
            dir=REPO_ROOT,
        ) as temporary:
            for source in PORT_IO_SOURCES:
                output_name = (
                    Path(source)
                    .with_suffix(".o")
                    .as_posix()
                    .replace("/", "-")
                )
                output = Path(temporary) / output_name
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        source,
                        "--output",
                        str(output),
                    ]
                )
                self.assertEqual(status, 0, source)
                kernel_compile.validate_i386_relocatable(output)


if __name__ == "__main__":
    unittest.main()

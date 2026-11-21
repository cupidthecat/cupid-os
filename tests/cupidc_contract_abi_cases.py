## @file cupidc_contract_abi_cases.py
#  @brief Check the Windows and Linux paths to user ABI verification.

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tests.cupidc_contract_fixtures import _test_relocatable_elf32
from tools import cupidc_toolchain_contracts
from tools.bootstrap_toolchain import _build_plan_sha256, _candidate_build_plan


class CupidCContractABICases:
    def test_user_abi_operation_compares_the_cupid_report_with_the_oracle(self):
        root = Path("contract-root").resolve()
        output = root / "toolchain/build/cupidc-contracts"
        contract_report = {
            "schema": "cupid.user-syscall-abi.v1",
            "field_count": 103,
            "table_size": 412,
        }
        completed = subprocess.CompletedProcess(
            ["user-syscall-abi-contract.elf"],
            0,
            json.dumps(contract_report) + "\n",
            "",
        )
        publication_report = {"status": "pass"}

        with mock.patch.object(
            cupidc_toolchain_contracts,
            "ensure_contracts",
            return_value=publication_report,
        ) as ensure, mock.patch.object(
            cupidc_toolchain_contracts,
            "_freeze_user_syscall_abi_inputs",
        ) as freeze, mock.patch.object(
            cupidc_toolchain_contracts,
            "run_published_contract",
            return_value=completed,
        ) as run, mock.patch.object(
            cupidc_toolchain_contracts,
            "check_syscall_abi",
            return_value=contract_report,
        ) as oracle, mock.patch.object(
            cupidc_toolchain_contracts,
            "verify_publication_inputs",
        ) as verify_inputs, mock.patch.object(
            cupidc_toolchain_contracts,
            "verify_publication",
            return_value=publication_report,
        ):
            checked = cupidc_toolchain_contracts.run_user_syscall_abi(
                root,
                root / "bootstrap/seeds/i386-linux/manifest.json",
                output,
                workers=3,
                timeout=45,
            )

        self.assertEqual(checked, contract_report)
        ensure.assert_called_once()
        snapshot = freeze.call_args.args[1]
        freeze.assert_called_once_with(root, snapshot, publication_report)
        run.assert_called_once_with(
            root,
            output / "user-syscall-abi-contract.elf",
            ("check-snapshot", snapshot, root),
            45,
            publication_report,
        )
        oracle.assert_called_once_with(snapshot)
        verify_inputs.assert_called_once_with(root, publication_report)

    def test_user_abi_operation_carries_explicit_long_profile(self):
        root = Path("contract-root").resolve()
        output = root / "toolchain/build/cupidc-contracts"
        expected = {"schema": "cupid.user-syscall-abi.v1",
                    "field_count": 103, "table_size": 412}
        publication = {"status": "pass"}
        completed = subprocess.CompletedProcess(
            ["user-syscall-abi-contract.elf"], 0,
            json.dumps(expected) + "\n", "",
        )
        with mock.patch.object(
            cupidc_toolchain_contracts, "ensure_contracts",
            return_value=publication,
        ) as ensure, mock.patch.object(
            cupidc_toolchain_contracts, "_freeze_user_syscall_abi_inputs",
        ), mock.patch.object(
            cupidc_toolchain_contracts, "run_published_contract",
            return_value=completed,
        ), mock.patch.object(
            cupidc_toolchain_contracts, "check_syscall_abi", return_value=expected,
        ), mock.patch.object(
            cupidc_toolchain_contracts, "verify_publication",
            return_value=publication,
        ), mock.patch.object(
            cupidc_toolchain_contracts, "verify_publication_inputs",
        ):
            actual = cupidc_toolchain_contracts.run_user_syscall_abi(
                root, root / "bootstrap/seeds/i386-linux/manifest.json",
                output, workers=3, windows_long_paths=True,
            )
        self.assertEqual(actual, expected)
        ensure.assert_called_once_with(
            root, root / "bootstrap/seeds/i386-linux/manifest.json",
            output, 3, windows_long_paths=True, windows_user_link_aliases=True,
        )

    def test_user_abi_operation_rejects_non_boolean_profile_before_execution(self):
        root = Path("contract-root").resolve()
        with mock.patch.object(
            cupidc_toolchain_contracts, "ensure_contracts",
        ) as ensure, mock.patch.object(
            cupidc_toolchain_contracts, "_run_native_windows_user_syscall_abi",
        ) as native:
            for value in (None, 0, 1, "true"):
                with self.subTest(value=value), self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "Windows long-path selection must be Boolean",
                ):
                    cupidc_toolchain_contracts.run_user_syscall_abi(
                        root, root / "manifest.json", root / "output",
                        windows_manifest=root / "windows.json",
                        windows_long_paths=value,
                    )
        ensure.assert_not_called()
        native.assert_not_called()

    def test_user_abi_cli_carries_explicit_long_profile(self):
        with mock.patch.object(
            cupidc_toolchain_contracts, "run_user_syscall_abi",
            return_value={"status": "pass"},
        ) as operation:
            result = cupidc_toolchain_contracts.main([
                "user-abi", "--root", "root", "--manifest", "manifest",
                "--output", "output", "--windows-long-paths",
            ])
        self.assertEqual(result, 0)
        self.assertTrue(operation.call_args.kwargs["windows_long_paths"])

    def test_linux_user_recipe_selects_explicit_long_profile(self):
        repository = Path(__file__).resolve().parents[1]
        text = (repository / "user/Makefile").read_text(encoding="utf-8")
        linux = text.split("\nelse\n", 1)[1].split("\nendif\n", 1)[0]
        self.assertIn(
            "USER_SYSCALL_ABI_PLATFORM_ARGUMENTS := --windows-long-paths", linux,
        )
        self.assertIn(
            '$(PRODUCTION_SEED_DIRECTORY)cupidbuild.$(PRODUCTION_SEED_SUFFIX) '
            'verify-user-abi --root "$(abspath ..)"', text,
        )
        self.assertEqual(text.count("override USER_SYSCALL_ABI_INPUTS :="), 1)
        gate = text.split("test-syscall-abi: $(USER_SYSCALL_ABI_INPUTS) Makefile\n", 1)[1].split("\n\n", 1)[0]
        self.assertNotIn("$(USER_SYSCALL_ABI)", gate)
        self.assertNotIn("$(PYTHON)", gate)

    def _write_current_user_abi_publication(self, root, *, windows_long_paths=False):
        repository = Path(__file__).resolve().parents[1]
        logical_manifest = "bootstrap/seeds/i386-linux/manifest.json"
        seed = cupidc_toolchain_contracts.verify_seed_inputs(
            repository / logical_manifest
        )
        sources = cupidc_toolchain_contracts.capture_source_snapshot(
            repository, _candidate_build_plan(seed.manifest["build_plan"]),
            windows_utf8=True, windows_long_paths=windows_long_paths,
            windows_user_link_aliases=True,
            windows_observer_workers=True,
        )
        inputs = cupidc_toolchain_contracts._snapshot_contract_inputs(
            repository,
            (*cupidc_toolchain_contracts._contract_input_paths(repository),
             *((repository / "toolchain/hosted/i386-windows/utf8_long_path_start.asm",)
               if windows_long_paths else ())),
        )
        paths = set(inputs) | set(sources) | {logical_manifest}
        paths.update(
            "bootstrap/seeds/i386-linux/" + artifact["file"]
            for artifact in seed.manifest["artifacts"]
        )
        for logical_path in sorted(paths):
            destination = root / logical_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repository / logical_path, destination)
        output = root / "toolchain/build/cupidc-contracts"
        self._write_publication(output)
        self._bind_publication_inputs(output, inputs)
        self._bind_publication_bootstrap(output, {
            "build_plan_sha256": _build_plan_sha256(
                _candidate_build_plan(seed.manifest["build_plan"])
            ),
            "seed_manifest": {
                "path": logical_manifest,
                "sha256": seed.manifest_sha256,
            },
            "source_inputs": {
                "count": len(sources),
                "files": sources,
                "sha256": cupidc_toolchain_contracts._snapshot_sha256(sources),
            },
        })
        return root / logical_manifest, output

    def test_linux_user_abi_consumes_size_and_digest_publication_records(self):
        with tempfile.TemporaryDirectory(prefix="cupid-user-abi-v3-") as td:
            root = Path(td).resolve()
            manifest, output = self._write_current_user_abi_publication(root)
            expected = cupidc_toolchain_contracts.check_syscall_abi(root)

            def run_contract(executable, arguments, timeout):
                self.assertEqual(arguments[0], "check-snapshot")
                snapshot = arguments[1]
                self.assertNotEqual(snapshot, root)
                for logical_path in cupidc_toolchain_contracts.USER_SYSCALL_ABI_INPUTS:
                    self.assertEqual(
                        (snapshot / logical_path).read_bytes(),
                        (root / logical_path).read_bytes(),
                    )
                return subprocess.CompletedProcess(
                    [str(executable)], 0, json.dumps(expected) + "\n", ""
                )

            with mock.patch.object(
                cupidc_toolchain_contracts.ToolRunner, "run",
                side_effect=run_contract,
            ):
                actual = cupidc_toolchain_contracts.run_user_syscall_abi(
                    root, manifest, output
                )
            self.assertEqual(actual, expected)
            self.assertEqual(actual["field_count"], 103)
            self.assertEqual(actual["table_size"], 412)

    def test_linux_user_abi_reuses_long_publication_without_rebuilding(self):
        with tempfile.TemporaryDirectory(prefix="cupid-user-abi-long-") as td:
            root = Path(td).resolve()
            manifest, output = self._write_current_user_abi_publication(
                root, windows_long_paths=True,
            )
            expected = cupidc_toolchain_contracts.check_syscall_abi(root)
            before = (output / "manifest.json").read_bytes()
            publication = cupidc_toolchain_contracts.verify_publication(output)
            self.assertEqual(publication["input_count"], 117)
            self.assertEqual(publication["bootstrap"]["source_inputs"]["count"], 115)
            completed = subprocess.CompletedProcess(
                ["user-syscall-abi-contract.elf"], 0,
                json.dumps(expected) + "\n", "",
            )
            with mock.patch.object(
                cupidc_toolchain_contracts, "build_contracts",
                side_effect=AssertionError("current long publication must be reused"),
            ) as build, mock.patch.object(
                cupidc_toolchain_contracts.ToolRunner, "run", return_value=completed,
            ):
                actual = cupidc_toolchain_contracts.run_user_syscall_abi(
                    root, manifest, output, windows_long_paths=True,
                )
            self.assertEqual(actual, expected)
            build.assert_not_called()
            self.assertEqual((output / "manifest.json").read_bytes(), before)

    def test_linux_user_abi_long_publication_rejects_live_input_drift(self):
        with tempfile.TemporaryDirectory(prefix="cupid-user-abi-long-drift-") as td:
            root = Path(td).resolve()
            manifest, output = self._write_current_user_abi_publication(
                root, windows_long_paths=True,
            )
            expected = cupidc_toolchain_contracts.check_syscall_abi(root)
            before = (output / "manifest.json").read_bytes()
            def change_input(executable, arguments, timeout):
                source = root / "kernel/core/syscall.h"
                source.write_bytes(source.read_bytes() + b"\n/* live drift */\n")
                return subprocess.CompletedProcess(
                    [str(executable)], 0, json.dumps(expected) + "\n", "",
                )
            with mock.patch.object(
                cupidc_toolchain_contracts, "build_contracts",
                side_effect=AssertionError("current long publication must be reused"),
            ) as build, mock.patch.object(
                cupidc_toolchain_contracts.ToolRunner, "run", side_effect=change_input,
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "published contract cohort changed while contract ran",
            ):
                cupidc_toolchain_contracts.run_user_syscall_abi(
                    root, manifest, output, windows_long_paths=True,
                )
            build.assert_not_called()
            self.assertEqual((output / "manifest.json").read_bytes(), before)

    def test_linux_user_abi_rejects_input_drift_during_snapshot_capture(self):
        for change in ("same-size", "size", "after-copy", "private-copy"):
            with self.subTest(change=change), tempfile.TemporaryDirectory(
                prefix="cupid-user-abi-v3-drift-"
            ) as td:
                root = Path(td).resolve()
                manifest, output = self._write_current_user_abi_publication(root)
                original_manifest = (output / "manifest.json").read_bytes()
                first = root / cupidc_toolchain_contracts.USER_SYSCALL_ABI_INPUTS[0]
                last = root / cupidc_toolchain_contracts.USER_SYSCALL_ABI_INPUTS[-1]
                read_bytes = Path.read_bytes
                temporary_directory = tempfile.TemporaryDirectory
                capture_started = False
                snapshot = None

                def create_temporary(*arguments, **keywords):
                    nonlocal capture_started, snapshot
                    directory = temporary_directory(*arguments, **keywords)
                    if keywords.get("prefix") == "cupid-user-syscall-abi-snapshot-":
                        capture_started = True
                        snapshot = Path(directory.name) / "source"
                    return directory

                def read_with_drift(path):
                    data = read_bytes(path)
                    if not capture_started:
                        return data
                    if path == first and change == "same-size":
                        return data[:-1] + b"!"
                    if path == first and change == "size":
                        return data + b"changed"
                    if path == last and change == "after-copy":
                        first.write_bytes(read_bytes(first) + b"changed")
                    if path == last and change == "private-copy":
                        target = snapshot / first.relative_to(root)
                        target.write_bytes(read_bytes(target)[:-1] + b"!")
                    return data

                diagnostic = (
                    "ABI inputs changed while the shared snapshot was frozen"
                    if change in ("after-copy", "private-copy")
                    else "ABI input differs from the publication: kernel/core/types.h"
                )
                with mock.patch.object(
                    Path, "read_bytes", read_with_drift,
                ), mock.patch.object(
                    tempfile, "TemporaryDirectory", create_temporary,
                ), mock.patch.object(
                    cupidc_toolchain_contracts.ToolRunner, "run",
                ) as runner, self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError, diagnostic,
                ):
                    cupidc_toolchain_contracts.run_user_syscall_abi(
                        root, manifest, output
                    )
                runner.assert_not_called()
                self.assertEqual(
                    (output / "manifest.json").read_bytes(), original_manifest
                )

    def test_windows_user_abi_uses_the_native_seed_without_a_publication(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-selection-"
        ) as temporary:
            root = Path(temporary).resolve()
            output = root / "toolchain/build/cupidc-contracts"
            output.mkdir(parents=True)
            sentinel = output / "occupied-publication.txt"
            sentinel.write_bytes(b"leave this publication alone")
            linux_manifest = (
                root / "bootstrap/seeds/i386-linux/manifest.json"
            )
            windows_manifest = (
                root / "bootstrap/seeds/i386-windows/manifest.json"
            )
            expected = {
                "schema": "cupid.user-syscall-abi.v1",
                "field_count": 103,
            }

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "_run_native_windows_user_syscall_abi",
                return_value=expected,
            ) as native, mock.patch.object(
                cupidc_toolchain_contracts,
                "ensure_contracts",
            ) as ensure:
                actual = cupidc_toolchain_contracts.run_user_syscall_abi(
                    root,
                    linux_manifest,
                    output,
                    workers=3,
                    timeout=45,
                    windows_manifest=windows_manifest,
                )

            self.assertEqual(actual, expected)
            self.assertEqual(
                sentinel.read_bytes(), b"leave this publication alone"
            )
            native.assert_called_once_with(root, windows_manifest, 45)
            ensure.assert_not_called()

    def test_native_windows_user_abi_source_snapshot_rejects_live_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-inputs-"
        ) as temporary:
            workspace = Path(temporary)
            root = workspace / "source"
            for logical_path in (
                cupidc_toolchain_contracts.NATIVE_WINDOWS_USER_ABI_BUILD_INPUTS
            ):
                path = root / logical_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    f"checked input: {logical_path}\n", encoding="ascii"
                )
            snapshot = workspace / "snapshot"
            expected = (
                cupidc_toolchain_contracts._freeze_native_windows_user_abi_inputs(
                    root, snapshot
                )
            )

            changed = root / "toolchain/ctool.cc"
            changed.write_text("changed during the build\n", encoding="ascii")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "native Windows user ABI inputs changed",
            ):
                cupidc_toolchain_contracts._require_native_windows_user_abi_inputs_unchanged(
                    root, expected
                )

    def test_native_windows_user_abi_runs_one_pe_against_the_shared_snapshot(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-windows/manifest.json"
        expected = {
            "schema": "cupid.user-syscall-abi.v1",
            "field_count": 103,
        }
        completed = subprocess.CompletedProcess(
            ["user-syscall-abi-contract.exe"],
            0,
            json.dumps(expected) + "\n",
            "",
        )
        seed = SimpleNamespace(
            manifest={"schema": "cupid.execution-seed.v2"},
            tools={},
        )

        with mock.patch.object(
            cupidc_toolchain_contracts,
            "_is_windows_host",
            return_value=True,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "freeze_seed_inputs",
            return_value=seed,
        ) as freeze_seed, mock.patch.object(
            cupidc_toolchain_contracts,
            "_freeze_native_windows_user_abi_inputs",
            return_value={"toolchain/ctool.cc": "a" * 64},
        ) as freeze_source, mock.patch.object(
            cupidc_toolchain_contracts,
            "_build_native_windows_user_abi_contract",
        ) as build, mock.patch.object(
            cupidc_toolchain_contracts,
            "ToolRunner",
        ) as runner_type, mock.patch.object(
            cupidc_toolchain_contracts,
            "check_syscall_abi",
            return_value=expected,
        ) as oracle, mock.patch.object(
            cupidc_toolchain_contracts,
            "require_live_seed_inputs",
        ) as require_seed, mock.patch.object(
            cupidc_toolchain_contracts,
            "_require_native_windows_user_abi_inputs_unchanged",
        ) as require_source:
            runner_type.return_value.run.return_value = completed

            def publish_executable(
                source_root, build_root, frozen_seed, runner, timeout
            ):
                executable = build_root / "user-syscall-abi-contract.exe"
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_bytes(b"MZ")
                return executable

            build.side_effect = publish_executable
            actual = (
                cupidc_toolchain_contracts._run_native_windows_user_syscall_abi(
                    root, manifest, 45
                )
            )

        self.assertEqual(actual, expected)
        frozen_source = freeze_source.call_args.args[1]
        frozen_seed = freeze_seed.call_args.args[1]
        self.assertNotEqual(frozen_source, root)
        self.assertNotEqual(frozen_seed, manifest.parent)
        executable = build.call_args.args[1] / "user-syscall-abi-contract.exe"
        runner_type.return_value.run.assert_called_once_with(
            executable,
            ("check-snapshot", frozen_source, root),
            45,
        )
        oracle.assert_called_once_with(frozen_source)
        require_seed.assert_called_once_with(seed)
        require_source.assert_called_once_with(
            root, {"toolchain/ctool.cc": "a" * 64}
        )

    def test_native_windows_user_abi_build_uses_checked_pe_producers(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-build-"
        ) as temporary:
            source_root = Path(temporary)
            build_root = source_root / "build/user-syscall-abi"
            tools = {
                "cupidc": source_root / "seed/cupidc.exe",
                "cupidasm": source_root / "seed/cupidasm.exe",
                "cupidld": source_root / "seed/cupidld.exe",
            }
            seed = SimpleNamespace(tools=tools)
            calls = []

            class RecordingRunner:
                def run(self, executable, arguments, timeout):
                    arguments = tuple(arguments)
                    calls.append((executable, arguments, timeout))
                    output = arguments[arguments.index("-o") + 1]
                    output_path = (
                        source_root / str(output).lstrip("/")
                        if executable == tools["cupidc"] and str(output).startswith("/")
                        else Path(output)
                    )
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    if executable == tools["cupidld"]:
                        output_path.write_bytes(b"MZchecked")
                    else:
                        output_path.write_bytes(_test_relocatable_elf32())
                    return subprocess.CompletedProcess(
                        [str(executable)], 0, "", ""
                    )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "_validate_static_i386_pe32",
            ) as validate_pe, mock.patch.object(
                cupidc_toolchain_contracts,
                "require_live_seed_inputs",
            ) as require_seed:
                executable = (
                    cupidc_toolchain_contracts._build_native_windows_user_abi_contract(
                        source_root,
                        build_root,
                        seed,
                        RecordingRunner(),
                        45,
                    )
                )

            self.assertEqual(
                executable,
                build_root / "user-syscall-abi-contract.exe",
            )
            self.assertEqual(
                [call[0] for call in calls],
                [
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidasm"],
                    tools["cupidld"],
                ],
            )
            compile_sources = [
                call[1][call[1].index("-c") + 1] for call in calls[:5]
            ]
            self.assertEqual(
                compile_sources,
                [
                    "/toolchain/user_syscall_abi.cc",
                    "/toolchain/tests/user_syscall_abi_contract.cc",
                    "/toolchain/ctool_host.cc",
                    "/toolchain/ctool.cc",
                    "/toolchain/hosted/i386-windows/runtime.cc",
                ],
            )
            link_arguments = calls[-1][1]
            self.assertEqual(link_arguments[:6], (
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ))
            self.assertIn(
                "__imp_VirtualAlloc=KERNEL32.dll:VirtualAlloc",
                link_arguments,
            )
            self.assertEqual(require_seed.call_count, len(calls))
            validate_pe.assert_called_once()

    def test_native_windows_user_abi_rejects_a_malformed_pe(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-malformed-pe-"
        ) as temporary:
            source_root = Path(temporary)
            tools = {
                "cupidc": source_root / "seed/cupidc.exe",
                "cupidasm": source_root / "seed/cupidasm.exe",
                "cupidld": source_root / "seed/cupidld.exe",
            }
            seed = SimpleNamespace(tools=tools)

            class MalformedPeRunner:
                def run(self, executable, arguments, timeout):
                    arguments = tuple(arguments)
                    output = arguments[arguments.index("-o") + 1]
                    output_path = (
                        source_root / str(output).lstrip("/")
                        if executable == tools["cupidc"] and str(output).startswith("/")
                        else Path(output)
                    )
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    output_path.write_bytes(
                        b"MZbroken"
                        if executable == tools["cupidld"]
                        else _test_relocatable_elf32()
                    )
                    return subprocess.CompletedProcess(
                        [str(executable)], 0, "", ""
                    )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "require_live_seed_inputs",
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "invalid PE contract",
            ):
                cupidc_toolchain_contracts._build_native_windows_user_abi_contract(
                    source_root,
                    source_root / "build/user-syscall-abi",
                    seed,
                    MalformedPeRunner(),
                    45,
                )

    def test_native_windows_user_abi_rejects_seed_drift_during_compile(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-seed-drift-"
        ) as temporary:
            source_root = Path(temporary)
            tools = {
                "cupidc": source_root / "seed/cupidc.exe",
                "cupidasm": source_root / "seed/cupidasm.exe",
                "cupidld": source_root / "seed/cupidld.exe",
            }
            seed = SimpleNamespace(tools=tools)

            class OneObjectRunner:
                def run(self, executable, arguments, timeout):
                    arguments = tuple(arguments)
                    output = arguments[arguments.index("-o") + 1]
                    output_path = source_root / str(output).lstrip("/")
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    output_path.write_bytes(_test_relocatable_elf32())
                    return subprocess.CompletedProcess(
                        [str(executable)], 0, "", ""
                    )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "require_live_seed_inputs",
                side_effect=cupidc_toolchain_contracts.BootstrapError(
                    "checked seed artifact changed"
                ),
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "native Windows CupidC.*could not run",
            ):
                cupidc_toolchain_contracts._build_native_windows_user_abi_contract(
                    source_root,
                    source_root / "build/user-syscall-abi",
                    seed,
                    OneObjectRunner(),
                    45,
                )

    def test_native_windows_user_abi_rejects_a_linux_seed_before_building(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        seed = SimpleNamespace(
            manifest={"schema": "cupid.bootstrap-seed.v1"},
            tools={},
        )
        with mock.patch.object(
            cupidc_toolchain_contracts,
            "_is_windows_host",
            return_value=True,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_freeze_native_windows_user_abi_inputs",
            return_value={},
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "freeze_seed_inputs",
            return_value=seed,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_build_native_windows_user_abi_contract",
        ) as build, self.assertRaisesRegex(
            cupidc_toolchain_contracts.ContractError,
            "requires the checked Windows execution seed",
        ):
            cupidc_toolchain_contracts._run_native_windows_user_syscall_abi(
                root, manifest, 45
            )
        build.assert_not_called()

    def test_user_abi_cli_accepts_a_separate_windows_execution_manifest(self):
        arguments = cupidc_toolchain_contracts._build_parser().parse_args(
            [
                "user-abi",
                "--root",
                "source",
                "--manifest",
                "bootstrap/seeds/i386-linux/manifest.json",
                "--windows-manifest",
                "bootstrap/seeds/i386-windows/manifest.json",
                "--output",
                "toolchain/build/cupidc-contracts",
            ]
        )
        self.assertEqual(
            arguments.windows_manifest,
            Path("bootstrap/seeds/i386-windows/manifest.json"),
        )

    def test_user_abi_operation_rejects_oracle_disagreement(self):
        root = Path("contract-root").resolve()
        output = root / "toolchain/build/cupidc-contracts"
        completed = subprocess.CompletedProcess(
            ["user-syscall-abi-contract.elf"],
            0,
            '{"field_count": 103}\n',
            "",
        )
        with mock.patch.object(
            cupidc_toolchain_contracts,
            "ensure_contracts",
            return_value={"status": "pass"},
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_freeze_user_syscall_abi_inputs",
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "run_published_contract",
            return_value=completed,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "check_syscall_abi",
            return_value={"field_count": 102},
        ), self.assertRaisesRegex(
            cupidc_toolchain_contracts.ContractError,
            "differs from the independent oracle",
        ):
            cupidc_toolchain_contracts.run_user_syscall_abi(
                root,
                root / "bootstrap/seeds/i386-linux/manifest.json",
                output,
            )

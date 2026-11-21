## @file
#  @brief Check stage reconstruction, convergence, and failed publications.
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.bootstrap_seed_fixture_profiles import OWNER_SOURCE_NAMES
from tools.bootstrap_toolchain import (
    BootstrapError,
    CANDIDATE_TOOL_NAMES,
    PROMOTED_LINUX_PLAN_SHA256,
    PROMOTED_SOURCE_REVISION,
    PROMOTED_SEED_SCHEMA,
    PROMOTED_WINDOWS_SEED_SCHEMA,
    Stage,
    TOOL_NAMES,
    ToolRunner,
    _compare_stages,
    _compare_windows_stages,
    _build_stage,
    _build_windows_stage,
    _candidate_build_plan,
    _check_executable_code_anchor_behavior,
    _local_target_object_payload,
    _unowned_relocation_object_payload,
    _windows_build_plan,
    bootstrap_from_seed,
    bootstrap_windows_from_seed,
    capture_source_snapshot,
)

from tests.bootstrap_seed_suite_fixtures import (
    BOOTSTRAP_TOOL,
    CURRENT_SOURCE_HEAD_INPUT_COUNT,
    CURRENT_SOURCE_HEAD_SNAPSHOT_SHA256,
    REPO_ROOT,
    SEED_MANIFEST,
    WINDOWS_SEED_MANIFEST,
    _fixture_windows_imports,
)


class BootstrapSeedReconstructionCases:
    def test_native_windows_candidate_plan_has_exact_cupidbuild_profile(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]

        native_plan = _windows_build_plan(
            _candidate_build_plan(checked_plan)
        )

        self.assertEqual(
            [source["name"] for source in native_plan["assembly_sources"]],
            ["start", "publication_start", "cupidbuild_start"],
        )
        native_sources = {
            source["name"]: source for source in native_plan["sources"]
        }
        self.assertEqual(
            native_sources["cupidbuild"]["definitions"], ["_WIN32=1"]
        )
        self.assertEqual(
            native_sources["cupidbuild_host"]["definitions"], ["_WIN32=1"]
        )
        self.assertEqual(native_sources["cupidbuild_main"]["definitions"], [])
        self.assertEqual(
            native_plan["links"]["cupidbuild"],
            [
                "start",
                "publication_start",
                "cupidbuild_start",
                "cupidbuild_main",
                "cupidbuild",
                "cupidbuild_host",
                "ctool_host",
                "ctool",
                "elf32",
                "seed_manifest",
                "seed_release",
                "contract_parse_internal",
                "cupidbuild_artifacts",
                "artifact_size_policy",
                "user_syscall_abi",
                "cupidbuild_user_abi",
                "iso_fixture_bundle",
                "cupidbuild_iso",
                "cupidbuild_iso_capture",
                "cupidbuild_iso_image",
                "cupidbuild_iso_publication",
                "fat16_stage", "fat16_names", "disk_image",
                *OWNER_SOURCE_NAMES,
                "publication_runtime",
                "runtime",
            ],
        )
        self.assertEqual(
            [
                (record["library"], record["procedure"], record["slot"])
                for record in native_plan["imports"]["cupidbuild"]
            ],
            [
                ("KERNEL32.dll", "CloseHandle", "__imp_CloseHandle"),
                (
                    "KERNEL32.dll",
                    "CreateDirectoryA",
                    "__imp_CreateDirectoryA",
                ),
                ("KERNEL32.dll", "CreateFileA", "__imp_CreateFileA"),
                (
                    "KERNEL32.dll",
                    "CreateProcessA",
                    "__imp_CreateProcessA",
                ),
                ("KERNEL32.dll", "DeleteFileA", "__imp_DeleteFileA"),
                (
                    "KERNEL32.dll",
                    "DeleteProcThreadAttributeList",
                    "__imp_DeleteProcThreadAttributeList",
                ),
                ("KERNEL32.dll", "ExitProcess", "__imp_ExitProcess"),
                ("KERNEL32.dll", "FindClose", "__imp_FindClose"),
                (
                    "KERNEL32.dll",
                    "FindFirstFileA",
                    "__imp_FindFirstFileA",
                ),
                (
                    "KERNEL32.dll",
                    "FindNextFileA",
                    "__imp_FindNextFileA",
                ),
                (
                    "KERNEL32.dll",
                    "FlushFileBuffers",
                    "__imp_FlushFileBuffers",
                ),
                (
                    "KERNEL32.dll",
                    "GetCommandLineA",
                    "__imp_GetCommandLineA",
                ),
                (
                    "KERNEL32.dll",
                    "GetCurrentDirectoryA",
                    "__imp_GetCurrentDirectoryA",
                ),
                (
                    "KERNEL32.dll",
                    "GetCurrentProcessId",
                    "__imp_GetCurrentProcessId",
                ),
                (
                    "KERNEL32.dll",
                    "GetExitCodeProcess",
                    "__imp_GetExitCodeProcess",
                ),
                (
                    "KERNEL32.dll",
                    "GetFileAttributesA",
                    "__imp_GetFileAttributesA",
                ),
                (
                    "KERNEL32.dll",
                    "GetFileInformationByHandle",
                    "__imp_GetFileInformationByHandle",
                ),
                (
                    "KERNEL32.dll",
                    "GetFullPathNameA",
                    "__imp_GetFullPathNameA",
                ),
                ("KERNEL32.dll", "GetLastError", "__imp_GetLastError"),
                ("KERNEL32.dll", "GetStdHandle", "__imp_GetStdHandle"),
                (
                    "KERNEL32.dll",
                    "InitializeProcThreadAttributeList",
                    "__imp_InitializeProcThreadAttributeList",
                ),
                ("KERNEL32.dll", "MoveFileExA", "__imp_MoveFileExA"),
                ("KERNEL32.dll", "OpenProcess", "__imp_OpenProcess"),
                ("KERNEL32.dll", "ReadFile", "__imp_ReadFile"),
                (
                    "KERNEL32.dll",
                    "RemoveDirectoryA",
                    "__imp_RemoveDirectoryA",
                ),
                (
                    "KERNEL32.dll",
                    "SetFilePointer",
                    "__imp_SetFilePointer",
                ),
                (
                    "KERNEL32.dll",
                    "SetHandleInformation",
                    "__imp_SetHandleInformation",
                ),
                (
                    "KERNEL32.dll",
                    "TerminateProcess",
                    "__imp_TerminateProcess",
                ),
                (
                    "KERNEL32.dll",
                    "UpdateProcThreadAttribute",
                    "__imp_UpdateProcThreadAttribute",
                ),
                ("KERNEL32.dll", "VirtualAlloc", "__imp_VirtualAlloc"),
                ("KERNEL32.dll", "VirtualFree", "__imp_VirtualFree"),
                (
                    "KERNEL32.dll",
                    "WaitForSingleObject",
                    "__imp_WaitForSingleObject",
                ),
                ("KERNEL32.dll", "WriteFile", "__imp_WriteFile"),
                (
                    "NTDLL.dll",
                    "NtCreateFile",
                    "__imp_NtCreateFile",
                ),
                (
                    "NTDLL.dll",
                    "NtQueryDirectoryFile",
                    "__imp_NtQueryDirectoryFile",
                ),
                (
                    "NTDLL.dll",
                    "NtSetInformationFile",
                    "__imp_NtSetInformationFile",
                ),
            ],
        )

    def test_fixed_point_stages_certify_startups_before_linking(self):
        class RecordingRunner:
            def __init__(self, *, reject_code_anchors: bool = False):
                self.calls: list[tuple[str, tuple[str, ...]]] = []
                self.reject_code_anchors = reject_code_anchors

            def run(self, executable, arguments, _timeout):
                name = Path(executable).name
                rendered = tuple(str(argument) for argument in arguments)
                self.calls.append((name, rendered))
                if name == "cupidasm":
                    Path(arguments[4]).write_bytes(
                        _local_target_object_payload(0)
                    )
                elif name == "cupiddis" and self.reject_code_anchors:
                    return subprocess.CompletedProcess(
                        rendered,
                        1,
                        "",
                        "fixture code anchor rejection\n",
                    )
                elif name == "cupidld":
                    output_index = list(arguments).index("-o") + 1
                    Path(arguments[output_index]).write_bytes(b"linked")
                return subprocess.CompletedProcess(rendered, 0, "", "")

        with tempfile.TemporaryDirectory(
            prefix=".fixed-point-startup-gate-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup = root / "start.asm"
            startup.write_text(
                "bits 32\nglobal _start:function\n_start: ret\n",
                encoding="utf-8",
            )
            producers = {
                name: root / name
                for name in ("cupidc", "cupidasm", "cupiddis", "cupidld")
            }
            plan = {
                "sources": [],
                "include_arguments": [],
                "workers": 1,
                "startup": str(startup),
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
            runner = RecordingRunner()
            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_elf"
            ):
                _build_stage(
                    runner,
                    root,
                    root / "linux-stage",
                    producers,
                    plan,
                    "fixture",
                )

            tool_order = [name for name, _ in runner.calls]
            self.assertEqual(tool_order[:2], ["cupidasm", "cupiddis"])
            self.assertEqual(tool_order[2:], ["cupidld"] * len(TOOL_NAMES))
            self.assertEqual(
                runner.calls[1][1][:-1],
                (
                    "--require-known",
                    "--require-local-targets",
                    "--require-code-anchors",
                ),
            )

            windows_plan = {
                "sources": [],
                "imports": _fixture_windows_imports(TOOL_NAMES),
                "include_arguments": [],
                "workers": 1,
                "assembly_sources": [
                    {"name": "start", "path": str(startup)},
                    {"name": "publication_start", "path": str(startup)},
                ],
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
            windows_runner = RecordingRunner()
            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_pe32"
            ):
                _build_windows_stage(
                    windows_runner,
                    root,
                    root / "windows-stage",
                    producers,
                    windows_plan,
                    "fixture",
                )
            windows_order = [name for name, _ in windows_runner.calls]
            self.assertEqual(
                windows_order[:4],
                ["cupidasm", "cupiddis", "cupidasm", "cupiddis"],
            )
            self.assertEqual(
                windows_order[4:], ["cupidld"] * len(TOOL_NAMES)
            )

    def test_fixed_point_stages_certify_every_cupidc_object_before_linking(
        self,
    ):
        class RecordingRunner:
            def __init__(self, root: Path):
                self.root = root
                self.calls: list[tuple[Path, tuple[str, ...]]] = []

            def run(self, executable, arguments, _timeout):
                executable = Path(executable)
                rendered = tuple(str(argument) for argument in arguments)
                self.calls.append((executable, rendered))
                if executable == producers["cupidc"]:
                    output = self.root / rendered[-1].lstrip("/")
                    output.write_bytes(_local_target_object_payload(0))
                elif executable == producers["cupidasm"]:
                    Path(arguments[4]).write_bytes(
                        _local_target_object_payload(0)
                    )
                elif executable == producers["cupidld"]:
                    output_index = list(arguments).index("-o") + 1
                    Path(arguments[output_index]).write_bytes(b"linked")
                return subprocess.CompletedProcess(rendered, 0, "", "")

        with tempfile.TemporaryDirectory(
            prefix=".fixed-point-c-object-gate-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup = root / "start.asm"
            startup.write_text("bits 32\nret\n", encoding="utf-8")
            producers = {
                name: root / f"preceding-{name}"
                for name in ("cupidc", "cupidasm", "cupiddis", "cupidld")
            }
            sources = [
                {
                    "name": name,
                    "path": f"/{name}.cc",
                    "gnu_extensions": False,
                    "definitions": [],
                }
                for name in ("first", "second")
            ]
            linux_plan = {
                "sources": sources,
                "include_arguments": [],
                "workers": 1,
                "startup": str(startup),
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
            linux_plan["links"]["cupidc"] = ["start", "first", "second"]
            linux_runner = RecordingRunner(root)
            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_elf"
            ):
                _build_stage(
                    linux_runner,
                    root,
                    root / "linux-stage",
                    producers,
                    linux_plan,
                    "fixture",
                )

            windows_plan = {
                "sources": sources,
                "imports": _fixture_windows_imports(TOOL_NAMES),
                "include_arguments": [],
                "workers": 1,
                "assembly_sources": [
                    {"name": "start", "path": str(startup)},
                ],
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
            windows_plan["links"]["cupidc"] = ["start", "first", "second"]
            windows_runner = RecordingRunner(root)
            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_pe32"
            ):
                _build_windows_stage(
                    windows_runner,
                    root,
                    root / "windows-stage",
                    producers,
                    windows_plan,
                    "fixture",
                )

            strict_flags = (
                "--require-known",
                "--require-local-targets",
                "--require-code-anchors",
            )
            for label, runner, stage_name in (
                ("Linux", linux_runner, "linux-stage"),
                ("Windows", windows_runner, "windows-stage"),
            ):
                with self.subTest(platform=label):
                    first_link = next(
                        index
                        for index, (tool, _arguments) in enumerate(runner.calls)
                        if tool == producers["cupidld"]
                    )
                    certifications = [
                        arguments
                        for tool, arguments in runner.calls[:first_link]
                        if tool == producers["cupiddis"]
                    ]
                    expected_objects = {
                        str(root / stage_name / f"{name}.o")
                        for name in ("first", "second")
                    }
                    self.assertEqual(
                        {
                            arguments[-1]
                            for arguments in certifications
                            if Path(arguments[-1]).name
                            in {"first.o", "second.o"}
                        },
                        expected_objects,
                    )
                    self.assertTrue(
                        all(
                            arguments[:-1] == strict_flags
                            for arguments in certifications
                        )
                    )

    def test_fixed_point_stage_rejects_a_cupidc_object_before_linking(self):
        class RejectingRunner:
            def __init__(self, root: Path):
                self.root = root
                self.calls: list[Path] = []

            def run(self, executable, arguments, _timeout):
                executable = Path(executable)
                self.calls.append(executable)
                if executable == producers["cupidc"]:
                    output = self.root / str(arguments[-1]).lstrip("/")
                    output.write_bytes(_unowned_relocation_object_payload())
                    return subprocess.CompletedProcess(arguments, 0, "", "")
                if executable == producers["cupiddis"]:
                    return subprocess.CompletedProcess(
                        arguments,
                        1,
                        "",
                        "code check failed: 1 of 1 executable relocations "
                        "unmatched\n",
                    )
                raise AssertionError(
                    "assembly and linking must not run after CupidC object "
                    "certification fails"
                )

        with tempfile.TemporaryDirectory(
            prefix=".fixed-point-c-object-rejection-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup = root / "start.asm"
            startup.write_text("bits 32\nret\n", encoding="utf-8")
            producers = {
                name: root / f"preceding-{name}"
                for name in ("cupidc", "cupidasm", "cupiddis", "cupidld")
            }
            plan = {
                "sources": [
                    {
                        "name": "unit",
                        "path": "/unit.cc",
                        "gnu_extensions": False,
                    }
                ],
                "include_arguments": [],
                "workers": 1,
                "startup": str(startup),
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
            plan["links"]["cupidc"] = ["start", "unit"]
            runner = RejectingRunner(root)
            with self.assertRaisesRegex(
                BootstrapError,
                "fixture CupidC for /unit.cc CupidDis code anchors failed "
                "with status 1: code check failed: 1 of 1 executable "
                "relocations unmatched",
            ):
                _build_stage(
                    runner,
                    root,
                    root / "stage",
                    producers,
                    plan,
                    "fixture",
                )
            self.assertEqual(
                runner.calls,
                [producers["cupidc"], producers["cupiddis"]],
            )

    def test_candidate_stages_link_all_six_tools(self):
        class RecordingRunner:
            def __init__(self):
                self.calls: list[tuple[str, tuple[str, ...]]] = []

            def run(self, executable, arguments, _timeout):
                name = Path(executable).name
                rendered = tuple(str(argument) for argument in arguments)
                self.calls.append((name, rendered))
                if name == "cupidasm":
                    Path(arguments[4]).write_bytes(
                        _local_target_object_payload(0)
                    )
                elif name == "cupidld":
                    output_index = list(arguments).index("-o") + 1
                    Path(arguments[output_index]).write_bytes(b"linked")
                return subprocess.CompletedProcess(rendered, 0, "", "")

        with tempfile.TemporaryDirectory(
            prefix=".candidate-stage-inventory-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup = root / "start.asm"
            startup.write_text("bits 32\nret\n", encoding="utf-8")
            publication_start = root / "publication_start.asm"
            publication_start.write_text(
                "bits 32\nret\n", encoding="utf-8"
            )
            cupidbuild_start = root / "cupidbuild_start.asm"
            cupidbuild_start.write_text(
                "bits 32\nret\n", encoding="utf-8"
            )
            producers = {
                name: root / name
                for name in ("cupidc", "cupidasm", "cupiddis", "cupidld")
            }
            linux_plan = {
                "sources": [],
                "include_arguments": [],
                "workers": 1,
                "startup": str(startup),
                "links": {
                    name: ["start"] for name in CANDIDATE_TOOL_NAMES
                },
            }
            linux_runner = RecordingRunner()
            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_elf"
            ):
                linux_stage = _build_stage(
                    linux_runner,
                    root,
                    root / "linux-stage",
                    producers,
                    linux_plan,
                    "candidate",
                )

            self.assertEqual(
                tuple(linux_stage.tools), CANDIDATE_TOOL_NAMES
            )

            windows_plan = {
                "sources": [],
                "imports": _fixture_windows_imports(CANDIDATE_TOOL_NAMES),
                "include_arguments": [],
                "workers": 1,
                "assembly_sources": [
                    {"name": "start", "path": "/start.asm"},
                    {
                        "name": "publication_start",
                        "path": "/publication_start.asm",
                    },
                    {
                        "name": "cupidbuild_start",
                        "path": "/cupidbuild_start.asm",
                    },
                ],
                "links": {
                    name: ["start"] for name in CANDIDATE_TOOL_NAMES
                },
            }
            windows_runner = RecordingRunner()
            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_pe32"
            ):
                windows_stage = _build_windows_stage(
                    windows_runner,
                    root,
                    root / "windows-stage",
                    producers,
                    windows_plan,
                    "candidate",
                )

            self.assertEqual(
                tuple(windows_stage.tools), CANDIDATE_TOOL_NAMES
            )
            assembled_sources = [
                arguments[2]
                for executable, arguments in windows_runner.calls
                if executable == "cupidasm"
            ]
            self.assertEqual(
                assembled_sources,
                [
                    str(startup),
                    str(publication_start),
                    str(cupidbuild_start),
                ],
            )
            cupidbuild_link = next(
                arguments
                for executable, arguments in windows_runner.calls
                if executable == "cupidld"
                and any(item.endswith("cupidbuild.exe") for item in arguments)
            )
            import_selectors = [
                cupidbuild_link[index + 1]
                for index, item in enumerate(cupidbuild_link[:-1])
                if item == "--import"
            ]
            self.assertEqual(
                import_selectors,
                [
                    f"__imp_{procedure}={library}:{procedure}"
                    for library, procedures in (
                        (
                            "KERNEL32.dll",
                            (
                                "CloseHandle",
                                "CreateDirectoryA",
                                "CreateFileA",
                                "CreateProcessA",
                                "DeleteFileA",
                                "DeleteProcThreadAttributeList",
                                "ExitProcess",
                                "FindClose",
                                "FindFirstFileA",
                                "FindNextFileA",
                                "FlushFileBuffers",
                                "GetCommandLineA",
                                "GetCurrentDirectoryA",
                                "GetCurrentProcessId",
                                "GetExitCodeProcess",
                                "GetFileAttributesA",
                                "GetFileInformationByHandle",
                                "GetFullPathNameA",
                                "GetLastError",
                                "GetStdHandle",
                                "InitializeProcThreadAttributeList",
                                "MoveFileExA",
                                "OpenProcess",
                                "ReadFile",
                                "RemoveDirectoryA",
                                "SetFilePointer",
                                "SetHandleInformation",
                                "TerminateProcess",
                                "UpdateProcThreadAttribute",
                                "VirtualAlloc",
                                "VirtualFree",
                                "WaitForSingleObject",
                                "WriteFile",
                            ),
                        ),
                        (
                            "NTDLL.dll",
                            (
                                "NtCreateFile",
                                "NtQueryDirectoryFile",
                                "NtSetInformationFile",
                            ),
                        ),
                    )
                    for procedure in procedures
                ],
            )

    def test_fixed_point_stage_stops_before_link_on_anchor_failure(self):
        class RejectingRunner:
            def __init__(self):
                self.calls: list[str] = []

            def run(self, executable, arguments, _timeout):
                name = Path(executable).name
                self.calls.append(name)
                if name == "cupidasm":
                    Path(arguments[4]).write_bytes(
                        _local_target_object_payload(0)
                    )
                    return subprocess.CompletedProcess(arguments, 0, "", "")
                if name == "cupiddis":
                    return subprocess.CompletedProcess(
                        arguments,
                        1,
                        "",
                        "fixture code anchor rejection\n",
                    )
                raise AssertionError(
                    "CupidLD must not run after certification fails"
                )

        with tempfile.TemporaryDirectory(
            prefix=".fixed-point-startup-rejection-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup = root / "start.asm"
            startup.write_text("bits 32\nret\n", encoding="utf-8")
            producers = {
                name: root / name
                for name in ("cupidc", "cupidasm", "cupiddis", "cupidld")
            }
            plan = {
                "sources": [],
                "include_arguments": [],
                "workers": 1,
                "startup": str(startup),
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
            runner = RejectingRunner()
            with self.assertRaisesRegex(
                BootstrapError,
                "fixture startup CupidDis code anchors failed with status 1",
            ):
                _build_stage(
                    runner,
                    root,
                    root / "stage",
                    producers,
                    plan,
                    "fixture",
                )
            self.assertEqual(runner.calls, ["cupidasm", "cupiddis"])

    def test_public_bootstrap_cannot_skip_the_fixed_point(self):
        with self.assertRaisesRegex(
            TypeError, "unexpected keyword argument 'compare_fixed_point'"
        ):
            bootstrap_from_seed(
                SEED_MANIFEST,
                REPO_ROOT,
                REPO_ROOT / "unused-bootstrap-output",
                **{"compare_fixed_point": False},
            )

    def test_code_anchor_diagnostic_uses_native_path_for_pe_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-native-code-anchors-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            stage_two_tool = root / "stage-two-cupiddis.exe"
            stage_three_tool = root / "stage-three-cupiddis.exe"
            stage_two_tool.write_bytes(b"MZ")
            stage_three_tool.write_bytes(b"MZ")
            stage_two = Stage(
                objects={}, tools={"cupiddis": stage_two_tool}
            )
            stage_three = Stage(
                objects={}, tools={"cupiddis": stage_three_tool}
            )
            invalid = root / "invalid-linked-code-anchors.elf"
            expected_stderr = (
                f"cupiddis: {invalid}: code anchor check failed: "
                "1 of 3 code anchors invalid (0 outside file-backed "
                "executable code, 1 mid-instruction)\n"
            )
            results = (
                subprocess.CompletedProcess([], 0, "", ""),
                subprocess.CompletedProcess([], 1, "", expected_stderr),
            )
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=results,
            ):
                _check_executable_code_anchor_behavior(
                    ToolRunner(root),
                    stage_two,
                    stage_three,
                    root,
                    "native Windows ",
                )

    def test_failed_second_stage_keeps_completed_first_stage_private(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-bootstrap-stage-failure-",
            dir=REPO_ROOT,
        ) as temporary:
            root = Path(temporary)
            output = root / "published"
            output.mkdir()
            private_stage_directories: list[Path] = []

            def fail_after_first_stage(
                _runner: ToolRunner,
                _source_root: Path,
                stage_directory: Path,
                _producers: dict[str, Path],
                _plan: dict[str, object],
                stage_name: str,
            ) -> Stage:
                if stage_name == "stage three":
                    raise BootstrapError("forced stage-three failure")
                stage_directory.mkdir()
                marker = stage_directory / "complete.marker"
                marker.write_text("completed first stage")
                private_stage_directories.append(stage_directory)
                return Stage(
                    objects={"marker": marker},
                    tools={name: marker for name in TOOL_NAMES},
                )

            with mock.patch(
                "tools.bootstrap_toolchain._build_stage",
                side_effect=fail_after_first_stage,
            ):
                with self.assertRaisesRegex(
                    BootstrapError,
                    "^forced stage-three failure$",
                ):
                    bootstrap_from_seed(
                        SEED_MANIFEST, REPO_ROOT, output
                    )

            self.assertTrue(output.is_dir())
            self.assertEqual(list(output.iterdir()), [])
            self.assertEqual(
                {path.name for path in root.iterdir()},
                {"published"},
            )
            self.assertEqual(len(private_stage_directories), 1)
            self.assertFalse(private_stage_directories[0].exists())

    def test_failed_fourth_stage_keeps_both_completed_stages_private(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-bootstrap-stage-four-failure-",
            dir=REPO_ROOT,
        ) as temporary:
            root = Path(temporary)
            output = root / "published"
            output.mkdir()
            private_stage_directories: list[Path] = []

            def fail_at_stage_four(
                _runner: ToolRunner,
                _source_root: Path,
                stage_directory: Path,
                _producers: dict[str, Path],
                _plan: dict[str, object],
                stage_name: str,
            ) -> Stage:
                if stage_name == "stage four":
                    raise BootstrapError("forced stage-four failure")
                stage_directory.mkdir()
                marker = stage_directory / "complete.marker"
                marker.write_text(f"completed {stage_name}")
                private_stage_directories.append(stage_directory)
                return Stage(
                    objects={"marker": marker},
                    tools={name: marker for name in TOOL_NAMES},
                )

            with mock.patch(
                "tools.bootstrap_toolchain._build_stage",
                side_effect=fail_at_stage_four,
            ):
                with self.assertRaisesRegex(
                    BootstrapError,
                    "^forced stage-four failure$",
                ):
                    bootstrap_from_seed(
                        SEED_MANIFEST, REPO_ROOT, output
                    )

            self.assertTrue(output.is_dir())
            self.assertEqual(list(output.iterdir()), [])
            self.assertEqual(
                {path.name for path in root.iterdir()},
                {"published"},
            )
            self.assertEqual(len(private_stage_directories), 2)
            self.assertTrue(
                all(
                    not path.exists()
                    for path in private_stage_directories
                )
            )

    def test_linux_fixed_point_rejects_live_seed_drift_before_next_stage(self):
        mutations = {
            "manifest": lambda seed: (seed / "manifest.json").write_bytes(
                (seed / "manifest.json").read_bytes() + b"\n"
            ),
            "artifact": lambda seed: (seed / "cupidc.elf").write_bytes(
                b"changed during bootstrap"
            ),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix=".cupid-linux-live-seed-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                copied_seed = root / "seed"
                shutil.copytree(SEED_MANIFEST.parent, copied_seed)
                output = root / "published"
                output.mkdir()
                sentinel = output / "competing-publisher.txt"
                private_stages: list[Path] = []

                def build_stage(
                    _runner,
                    _source_root,
                    stage_directory,
                    _producers,
                    _plan,
                    stage_name,
                ):
                    self.assertEqual(stage_name, "stage two")
                    stage_directory.mkdir()
                    marker = stage_directory / "complete.marker"
                    marker.write_bytes(b"private stage")
                    private_stages.append(stage_directory)
                    sentinel.write_bytes(b"keep competing output")
                    mutate(copied_seed)
                    return Stage(
                        objects={"marker": marker},
                        tools={name: marker for name in TOOL_NAMES},
                    )

                with mock.patch(
                    "tools.bootstrap_toolchain._build_stage",
                    side_effect=build_stage,
                ), self.assertRaisesRegex(
                    BootstrapError,
                    "^checked seed (manifest|artifact) changed during "
                    "bootstrap:",
                ):
                    bootstrap_from_seed(
                        copied_seed / "manifest.json", REPO_ROOT, output
                    )

                self.assertEqual(
                    sentinel.read_bytes(), b"keep competing output"
                )
                self.assertEqual(list(output.iterdir()), [sentinel])
                self.assertEqual(len(private_stages), 1)
                self.assertFalse(private_stages[0].exists())

    @unittest.skipUnless(os.name == "nt", "native Windows bootstrap")
    def test_windows_fixed_point_rejects_both_live_seed_roles(self):
        cases = (
            ("execution manifest", "execution", "manifest"),
            ("plan manifest", "plan", "manifest"),
            ("execution artifact", "execution", "artifact"),
            ("plan artifact", "plan", "artifact"),
        )
        for label, role, kind in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix=".cupid-windows-live-seed-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                execution_seed = root / "execution-seed"
                plan_seed = root / "plan-seed"
                shutil.copytree(WINDOWS_SEED_MANIFEST.parent, execution_seed)
                shutil.copytree(SEED_MANIFEST.parent, plan_seed)
                target_seed = (
                    execution_seed if role == "execution" else plan_seed
                )
                artifact_file = (
                    "cupiddis.exe"
                    if role == "execution"
                    else "cupiddis.elf"
                )
                target = target_seed / (
                    "manifest.json" if kind == "manifest" else artifact_file
                )
                output = root / "published"
                output.mkdir()
                sentinel = output / "competing-publisher.txt"
                private_stages: list[Path] = []

                def build_stage(
                    _runner,
                    _source_root,
                    stage_directory,
                    _producers,
                    _plan,
                    stage_name,
                ):
                    self.assertEqual(stage_name, "stage two")
                    stage_directory.mkdir()
                    marker = stage_directory / "complete.marker"
                    marker.write_bytes(b"private stage")
                    private_stages.append(stage_directory)
                    sentinel.write_bytes(b"keep competing output")
                    if kind == "manifest":
                        target.write_bytes(target.read_bytes() + b"\n")
                    else:
                        target.write_bytes(b"changed during bootstrap")
                    return Stage(
                        objects={"marker": marker},
                        tools={name: marker for name in TOOL_NAMES},
                    )

                with mock.patch(
                    "tools.bootstrap_toolchain._build_windows_stage",
                    side_effect=build_stage,
                ), self.assertRaisesRegex(
                    BootstrapError,
                    "^checked seed (manifest|artifact) changed during "
                    "bootstrap:",
                ):
                    bootstrap_windows_from_seed(
                        execution_seed / "manifest.json",
                        plan_seed / "manifest.json",
                        REPO_ROOT,
                        output,
                    )

                self.assertEqual(
                    sentinel.read_bytes(), b"keep competing output"
                )
                self.assertEqual(list(output.iterdir()), [sentinel])
                self.assertEqual(len(private_stages), 1)
                self.assertFalse(private_stages[0].exists())

    def test_linux_fixed_point_rejects_a_stage_four_object_mismatch(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-bootstrap-stage-mismatch-",
            dir=REPO_ROOT,
        ) as temporary:
            root = Path(temporary)
            common = root / "common"
            changed = root / "changed"
            common.write_bytes(b"stable")
            changed.write_bytes(b"different")
            stage_three = Stage(
                objects={"source": common, "start": common},
                tools={name: common for name in TOOL_NAMES},
            )
            stage_four = Stage(
                objects={"source": changed, "start": common},
                tools={name: common for name in TOOL_NAMES},
            )

            with self.assertRaisesRegex(
                BootstrapError,
                "^C object differs between stage three and stage four: "
                "source$",
            ):
                _compare_stages(stage_three, stage_four, ["source"])

    def test_candidate_fixed_point_rejects_a_cupidbuild_image_mismatch(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-candidate-stage-mismatch-",
            dir=REPO_ROOT,
        ) as temporary:
            root = Path(temporary)
            common = root / "common"
            changed = root / "changed"
            common.write_bytes(b"stable")
            changed.write_bytes(b"different")
            objects = {"source": common, "start": common}
            stage_three = Stage(
                objects=objects,
                tools={name: common for name in CANDIDATE_TOOL_NAMES},
            )
            stage_four_tools = {
                name: common for name in CANDIDATE_TOOL_NAMES
            }
            stage_four_tools["cupidbuild"] = changed
            stage_four = Stage(objects=objects, tools=stage_four_tools)

            with self.assertRaisesRegex(
                BootstrapError,
                "^tool image differs between stage three and stage four: "
                "cupidbuild$",
            ):
                _compare_stages(stage_three, stage_four, ["source"])

            with self.assertRaisesRegex(
                BootstrapError,
                "^native Windows tool image differs between stage three "
                "and stage four: cupidbuild$",
            ):
                _compare_windows_stages(
                    stage_three,
                    stage_four,
                    ["source"],
                    ["start"],
                    CANDIDATE_TOOL_NAMES,
                )

    def test_candidate_windows_fixed_point_requires_all_six_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-candidate-inventory-",
            dir=REPO_ROOT,
        ) as temporary:
            common = Path(temporary) / "common"
            common.write_bytes(b"stable")
            stage = Stage(
                objects={"source": common, "start": common},
                tools={name: common for name in TOOL_NAMES},
            )

            with self.assertRaisesRegex(
                BootstrapError,
                "^native Windows stage-three tool inventory differs$",
            ):
                _compare_windows_stages(
                    stage,
                    stage,
                    ["source"],
                    ["start"],
                    CANDIDATE_TOOL_NAMES,
                )

    def test_windows_fixed_point_rejects_a_stage_four_tool_mismatch(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-windows-stage-mismatch-",
            dir=REPO_ROOT,
        ) as temporary:
            root = Path(temporary)
            common = root / "common"
            changed = root / "changed"
            common.write_bytes(b"stable")
            changed.write_bytes(b"different")
            objects = {"source": common, "start": common}
            stage_three = Stage(
                objects=objects,
                tools={name: common for name in TOOL_NAMES},
            )
            stage_four_tools = {name: common for name in TOOL_NAMES}
            stage_four_tools["cupiddis"] = changed
            stage_four = Stage(objects=objects, tools=stage_four_tools)

            with self.assertRaisesRegex(
                BootstrapError,
                "^native Windows tool image differs between stage three "
                "and stage four: cupiddis$",
            ):
                _compare_windows_stages(
                    stage_three,
                    stage_four,
                    ["source"],
                    ["start"],
                    TOOL_NAMES,
                )

    def test_native_windows_bootstrap_rejects_a_linux_execution_seed(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-bootstrap-role-"
        ) as temporary:
            output = Path(temporary) / "published"
            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "bootstrap-windows",
                    "--manifest",
                    str(SEED_MANIFEST),
                    "--plan-manifest",
                    str(SEED_MANIFEST),
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "checked Windows bootstrap failed: native Windows bootstrap "
            "requires a Windows execution seed\n",
        )
        self.assertFalse(output.exists())

    def test_native_windows_bootstrap_rejects_an_execution_manifest_as_plan(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-bootstrap-plan-role-"
        ) as temporary:
            output = Path(temporary) / "published"
            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "bootstrap-windows",
                    "--manifest",
                    str(WINDOWS_SEED_MANIFEST),
                    "--plan-manifest",
                    str(WINDOWS_SEED_MANIFEST),
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "checked Windows bootstrap failed: native Windows bootstrap "
            "requires a Linux build-plan seed\n",
        )
        self.assertFalse(output.exists())

    @unittest.skipUnless(os.name == "nt", "native Windows bootstrap")
    def test_native_windows_bootstrap_freezes_both_verified_seed_roles(self):
        observed: dict[str, object] = {}

        def accept_verified_inputs(
            execution_seed, plan_seed, source_root, output_root,
            *, windows_user_link_aliases,
        ):
            self.assertTrue(windows_user_link_aliases)
            observed["execution_schema"] = execution_seed.manifest["schema"]
            observed["plan_schema"] = plan_seed.manifest["schema"]
            observed["source_root"] = source_root
            observed["output_root"] = output_root
            observed["execution_signatures"] = {
                name: path.read_bytes()[:2]
                for name, path in execution_seed.tools.items()
            }
            observed["plan_signatures"] = {
                name: path.read_bytes()[:4]
                for name, path in plan_seed.tools.items()
            }
            return {"status": "pass"}

        with tempfile.TemporaryDirectory(
            prefix=".native-windows-bootstrap-inputs-", dir=REPO_ROOT
        ) as temporary, mock.patch(
            "tools.bootstrap_toolchain._bootstrap_windows_from_frozen_seed",
            side_effect=accept_verified_inputs,
        ), mock.patch(
            "tools.bootstrap_toolchain.shutil.which",
            side_effect=AssertionError(
                "input freezing must not probe WSL"
            ),
        ):
            output = Path(temporary) / "published"
            report = bootstrap_windows_from_seed(
                WINDOWS_SEED_MANIFEST,
                SEED_MANIFEST,
                REPO_ROOT,
                output,
            )

        self.assertEqual(report, {"status": "pass"})
        self.assertEqual(
            observed["execution_schema"], PROMOTED_WINDOWS_SEED_SCHEMA
        )
        self.assertEqual(
            observed["plan_schema"], PROMOTED_SEED_SCHEMA
        )
        self.assertEqual(observed["source_root"], REPO_ROOT)
        self.assertEqual(observed["output_root"], output)
        self.assertEqual(
            observed["execution_signatures"],
            {name: b"MZ" for name in CANDIDATE_TOOL_NAMES},
        )
        self.assertEqual(
            observed["plan_signatures"],
            {name: b"\x7fELF" for name in CANDIDATE_TOOL_NAMES},
        )
        self.assertFalse(output.exists())

    @unittest.skipUnless(os.name == "nt", "native Windows bootstrap")
    def test_native_windows_bootstrap_preserves_an_occupied_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".native-windows-bootstrap-occupied-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "published"
            output.mkdir()
            sentinel = output / "sentinel.txt"
            sentinel.write_bytes(b"keep this output")
            with mock.patch.object(
                ToolRunner,
                "run",
                side_effect=AssertionError(
                    "occupied output must fail before a producer runs"
                ),
            ), self.assertRaisesRegex(
                BootstrapError,
                "^bootstrap output directory is not empty: sentinel.txt$",
            ):
                bootstrap_windows_from_seed(
                    WINDOWS_SEED_MANIFEST,
                    SEED_MANIFEST,
                    REPO_ROOT,
                    output,
                )

            self.assertEqual(sentinel.read_bytes(), b"keep this output")
            self.assertEqual(list(output.iterdir()), [sentinel])

    @unittest.skipUnless(os.name == "nt", "native Windows bootstrap")
    def test_native_windows_stage_four_failure_stays_private(self):
        with tempfile.TemporaryDirectory(
            prefix=".native-windows-stage-four-failure-",
            dir=REPO_ROOT,
        ) as temporary:
            root = Path(temporary)
            output = root / "published"
            output.mkdir()
            private_stage_directories: list[Path] = []
            stage_two_producer_suffixes: dict[str, str] = {}

            def fail_at_stage_four(
                _runner: ToolRunner,
                _source_root: Path,
                stage_directory: Path,
                producers: dict[str, Path],
                _plan: dict[str, object],
                stage_name: str,
            ) -> Stage:
                if stage_name == "stage two":
                    stage_two_producer_suffixes.update(
                        {
                            name: producer.suffix
                            for name, producer in producers.items()
                        }
                    )
                if stage_name == "stage four":
                    raise BootstrapError("forced native stage-four failure")
                stage_directory.mkdir()
                marker = stage_directory / "complete.marker"
                marker.write_text(f"completed {stage_name}")
                private_stage_directories.append(stage_directory)
                return Stage(
                    objects={"marker": marker},
                    tools={name: marker for name in TOOL_NAMES},
                )

            with mock.patch(
                "tools.bootstrap_toolchain._build_windows_stage",
                side_effect=fail_at_stage_four,
            ):
                with self.assertRaisesRegex(
                    BootstrapError,
                    "^forced native stage-four failure$",
                ):
                    bootstrap_windows_from_seed(
                        WINDOWS_SEED_MANIFEST,
                        SEED_MANIFEST,
                        REPO_ROOT,
                        output,
                    )

            self.assertTrue(output.is_dir())
            self.assertEqual(list(output.iterdir()), [])
            self.assertEqual(
                {path.name for path in root.iterdir()},
                {"published"},
            )
            self.assertEqual(len(private_stage_directories), 2)
            self.assertEqual(
                stage_two_producer_suffixes,
                {
                    "cupidasm": ".exe",
                    "cupidc": ".exe",
                    "cupiddis": ".exe",
                    "cupidld": ".exe",
                },
            )
            self.assertTrue(
                all(
                    not path.exists()
                    for path in private_stage_directories
                )
            )

    def test_checked_seed_builds_a_complete_toolchain_fixed_point(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-bootstrap-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary)
            environment = dict(os.environ)
            for name in (
                "CC",
                "CXX",
                "CPP",
                "HOSTCC",
                "HOSTCXX",
                "ASM",
                "AS",
                "LD",
                "AR",
                "NM",
                "OBJCOPY",
            ):
                environment[name] = f"__cupid_host_{name}_must_not_run__"
            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "bootstrap",
                    "--manifest",
                    str(SEED_MANIFEST),
                    "--seed-release",
                    str(REPO_ROOT / "bootstrap/seeds/release.json"),
                    "--windows-long-paths",
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
                timeout=6000,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "checked i386 Linux bootstrap: ok "
                "(stage three equals stage four)\n",
            )
            self.assertEqual(result.stderr, "")
            report = json.loads(
                (output / "bootstrap-report.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(report["schema"], "cupid.bootstrap-report.v1")
            self.assertEqual(report["status"], "pass")
            self.assertEqual(
                report["seed_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertNotIn("source_revision", report)
            self.assertEqual(
                report["build_plan_sha256"],
                PROMOTED_LINUX_PLAN_SHA256,
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                "e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa",
            )
            self.assertEqual(
                report["comparisons"],
                {
                    "all_equal": True,
                    "c_objects": 44,
                    "compared_generations": [
                        "stage-three",
                        "stage-four",
                    ],
                    "startup_objects": 1,
                    "tool_images": 6,
                },
            )
            self.assertEqual(
                report["behavior_generations"],
                ["stage-three", "stage-four"],
            )
            self.assertEqual(
                report["stages"]["stage-two"]["producer_generation"],
                "checked-seed",
            )
            self.assertEqual(
                report["stages"]["stage-three"]["producer_generation"],
                "stage-two",
            )
            self.assertEqual(
                report["stages"]["stage-four"]["producer_generation"],
                "stage-three",
            )
            self.assertEqual(
                report["behavior"],
                {
                    "failure_cases": 66,
                    "help_cases": 7,
                    "success_cases": 73,
                },
            )
            self.assertEqual(
                report["host_execution"],
                {
                    "windows_cupidasm": (
                        {
                            "failure_return_code": 1,
                            "help_return_code": 0,
                            "output_sha256": (
                                "95d76dfca4cb4f279611a6ea7a86202898305a4906c6c822c1bfce2ec9ecf06b"
                            ),
                            "output_size": 6,
                            "return_code": 0,
                            "status": "pass",
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                    "windows_cupidc": (
                        {
                            "failure_return_code": 1,
                            "help_return_code": 0,
                            "output_sha256": (
                                "ad1040837e69e983c89024918d491c48b70c1f03d216e51831647a21c65b0665"
                            ),
                            "output_size": 360,
                            "return_code": 0,
                            "status": "pass",
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                    "windows_cupiddis": (
                        {
                            "help_return_code": 0,
                            "invalid_target_return_code": 1,
                            "local_target_return_code": 0,
                            "missing_return_code": 1,
                            "raw_return_code": 0,
                            "raw_stdout_sha256": (
                                "7730fe73e97c921fae17e167e6960bb0189fee47de4fddc943117520ad82e6ac"
                            ),
                            "raw_stdout_size": 56,
                            "status": "pass",
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                    "windows_cupidld": (
                        {
                            "failure_candidate_count": 0,
                            "failure_return_code": 1,
                            "help_return_code": 0,
                            "occupied_candidate_sha256": (
                                "20323a24be105b1b519962994b8e4e6a7f8e3cd0d005b8ee10c9aeb66da5d40a"
                            ),
                            "output_sha256": (
                                "1e2cf939707b109370ff2cfb80dc467a101dc46ed2be54b7a077f63c131793a9"
                            ),
                            "output_size": 41984,
                            "return_code": 0,
                            "status": "pass",
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                    "windows_loader": (
                        {
                            "return_code": 37,
                            "status": "pass",
                            "stderr": "",
                            "stdout": (
                                "Cupid-built Windows runtime: ok\n"
                            ),
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                    "windows_cupidobj": (
                        {
                            "failure_return_code": 1,
                            "help_return_code": 0,
                            "output_sha256": (
                                "a4950b4f13759a63540da33f08b584e804b6fb4f98afaa97a82e3d0a9191c35a"
                            ),
                            "output_size": 452,
                            "return_code": 0,
                            "status": "pass",
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                    "windows_runtime_contract": (
                        {
                            "failure_return_code": 41,
                            "output_sha256": (
                                "87c2aebe999878ed1c244b6a85d1a2ad0b5c6f0916afed00797c1bc7d6097961"
                            ),
                            "output_size": 8,
                            "return_code": 0,
                            "status": "pass",
                            "stderr": "",
                            "stdout": (
                                "Cupid-built Windows tool runtime: ok\n"
                            ),
                        }
                        if os.name == "nt"
                        else {"status": "not-run"}
                    ),
                },
            )
            windows_runtime = report["windows_runtime"]
            windows_artifacts = windows_runtime["artifacts"]
            self.assertEqual(
                set(windows_artifacts),
                {
                    "stage-four-contract",
                    "stage-four-image",
                    "stage-four-start",
                    "stage-three-contract",
                    "stage-three-image",
                    "stage-three-start",
                },
            )
            for pair in ("contract", "image", "start"):
                self.assertEqual(
                    windows_artifacts[f"stage-three-{pair}"],
                    windows_artifacts[f"stage-four-{pair}"],
                )
            self.assertEqual(
                windows_artifacts["stage-three-image"],
                {
                    "sha256": (
                        "192f060dd5fe3f4ded16cc1423352b4879027086a8052d3eb3c4130ef4a7ec16"
                    ),
                    "size": 2048,
                },
            )
            self.assertEqual(
                windows_runtime["imports"],
                [
                    {
                        "library": "KERNEL32.dll",
                        "procedure": "ExitProcess",
                        "slot": "__imp_ExitProcess",
                    },
                    {
                        "library": "KERNEL32.dll",
                        "procedure": "GetStdHandle",
                        "slot": "__imp_GetStdHandle",
                    },
                    {
                        "library": "KERNEL32.dll",
                        "procedure": "WriteFile",
                        "slot": "__imp_WriteFile",
                    },
                ],
            )
            self.assertEqual(
                windows_runtime["loader"],
                report["host_execution"]["windows_loader"],
            )
            windows_cupiddis = windows_runtime["cupiddis"]
            self.assertEqual(
                set(windows_cupiddis["artifacts"]),
                {
                    "stage-four-image",
                    "stage-four-host-adapter",
                    "stage-four-main",
                    "stage-four-runtime",
                    "stage-four-start",
                    "stage-three-image",
                    "stage-three-host-adapter",
                    "stage-three-main",
                    "stage-three-runtime",
                    "stage-three-start",
                },
            )
            for artifact in (
                "host-adapter",
                "image",
                "main",
                "runtime",
                "start",
            ):
                self.assertEqual(
                    windows_cupiddis["artifacts"][
                        f"stage-three-{artifact}"
                    ],
                    windows_cupiddis["artifacts"][
                        f"stage-four-{artifact}"
                    ],
                )
            for stage_name in ("stage-three", "stage-four"):
                self._assert_file_artifact_identity(
                    windows_cupiddis["artifacts"][f"{stage_name}-image"],
                    output / "behavior" / f"{stage_name}-cupiddis.exe",
                )
            self.assertEqual(
                [
                    imported["procedure"]
                    for imported in windows_cupiddis["imports"]
                ],
                [
                    "CloseHandle",
                    "CreateFileA",
                    "ExitProcess",
                    "GetCommandLineA",
                    "GetCurrentDirectoryA",
                    "GetFileInformationByHandle",
                    "GetLastError",
                    "GetStdHandle",
                    "ReadFile",
                    "SetFilePointer",
                    "VirtualAlloc",
                    "VirtualFree",
                    "WriteFile",
                ],
            )
            self.assertEqual(
                windows_cupiddis["loader"],
                report["host_execution"]["windows_cupiddis"],
            )
            windows_runtime_contract = windows_runtime["runtime_contract"]
            self.assertEqual(
                set(windows_runtime_contract["artifacts"]),
                {
                    "stage-four-contract",
                    "stage-four-image",
                    "stage-three-contract",
                    "stage-three-image",
                },
            )
            for artifact in ("contract", "image"):
                self.assertEqual(
                    windows_runtime_contract["artifacts"][
                        f"stage-three-{artifact}"
                    ],
                    windows_runtime_contract["artifacts"][
                        f"stage-four-{artifact}"
                    ],
                )
            self.assertEqual(
                windows_runtime_contract["loader"],
                report["host_execution"]["windows_runtime_contract"],
            )
            native_tools = windows_runtime["native_tools"]
            self.assertEqual(
                set(native_tools),
                {"cupidasm", "cupidc", "cupidld", "cupidobj"},
            )
            for tool_name, tool_evidence in native_tools.items():
                with self.subTest(windows_tool=tool_name):
                    expected_imports = [
                        item["procedure"] for item in windows_cupiddis["imports"]
                    ]
                    if tool_name in ("cupidasm", "cupidld"):
                        expected_imports = sorted(
                            expected_imports
                            + [
                                "DeleteFileA",
                                "FlushFileBuffers",
                                "GetFullPathNameA",
                                "MoveFileExA",
                            ]
                        )
                    self.assertEqual(
                        [item["procedure"] for item in tool_evidence["imports"]],
                        expected_imports,
                    )
                    self.assertEqual(
                        tool_evidence["loader"],
                        report["host_execution"][f"windows_{tool_name}"],
                    )
                    artifacts = tool_evidence["artifacts"]
                    main_name = f"{tool_name}_main"
                    expected_artifacts = {
                        "stage-four-ctool_host",
                        f"stage-four-{main_name}",
                        "stage-four-image",
                        "stage-three-ctool_host",
                        f"stage-three-{main_name}",
                        "stage-three-image",
                    }
                    if tool_name in ("cupidasm", "cupidld"):
                        expected_artifacts.update(
                            {
                                "stage-four-publication_runtime",
                                "stage-four-publication_start",
                                "stage-three-publication_runtime",
                                "stage-three-publication_start",
                            }
                        )
                    self.assertEqual(
                        set(artifacts),
                        expected_artifacts,
                    )
                    for stage_name in ("stage-three", "stage-four"):
                        self._assert_file_artifact_identity(
                            artifacts[f"{stage_name}-image"],
                            output / "behavior" / f"{stage_name}-{tool_name}.exe",
                        )
                    for artifact_name in tuple(artifacts):
                        if not artifact_name.startswith("stage-three-"):
                            continue
                        stage_three_name = artifact_name.replace(
                            "stage-three-", "stage-four-", 1
                        )
                        self.assertIn(stage_three_name, artifacts)
                        self.assertEqual(
                            artifacts[artifact_name], artifacts[stage_three_name]
                        )
            initial_matches = report["initial_seed_matches_stage_two"]
            self.assertEqual(
                set(initial_matches),
                set(CANDIDATE_TOOL_NAMES),
            )
            candidate_plan = _candidate_build_plan(
                json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))[
                    "build_plan"
                ]
            )
            candidate_inventory = capture_source_snapshot(
                REPO_ROOT, candidate_plan, windows_utf8=True,
                windows_long_paths=True, windows_user_link_aliases=True,
                windows_observer_workers=True,
            )
            source_head_snapshot = hashlib.sha256(
                json.dumps(
                    candidate_inventory,
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=True,
                ).encode("ascii")
            ).hexdigest()
            self.assertEqual(
                report["source_snapshot_sha256"], source_head_snapshot
            )
            self.assertEqual(source_head_snapshot, CURRENT_SOURCE_HEAD_SNAPSHOT_SHA256)
            self.assertEqual(report["source_inputs"]["files"], candidate_inventory)
            self.assertEqual(
                initial_matches,
                {
                    name: (SEED_MANIFEST.parent / f"{name}.elf").read_bytes()
                    == (output / "stage-two" / f"{name}.elf").read_bytes()
                    for name in CANDIDATE_TOOL_NAMES
                },
            )
            self.assertEqual(
                report["source_inputs"]["count"],
                CURRENT_SOURCE_HEAD_INPUT_COUNT,
            )
            self.assertEqual(
                len(report["source_inputs"]["sha256"]),
                64,
            )
            self.assertEqual(
                report["source_snapshot_sha256"],
                report["source_inputs"]["sha256"],
            )
            self.assertEqual(
                len(report["source_inputs"]["files"]),
                CURRENT_SOURCE_HEAD_INPUT_COUNT,
            )
            for tool_name in CANDIDATE_TOOL_NAMES:
                stage_three = output / "stage-three" / f"{tool_name}.elf"
                stage_four = output / "stage-four" / f"{tool_name}.elf"
                self.assertEqual(
                    stage_four.read_bytes(), stage_three.read_bytes()
                )

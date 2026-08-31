import ast
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild
from tools.bootstrap_toolchain import (
    BootstrapError,
    CANDIDATE_TOOL_NAMES,
    PROMOTED_LINUX_MANIFEST_SHA256,
    PROMOTED_LINUX_PLAN_SHA256,
    PROMOTED_SOURCE_INPUT_COUNT,
    PROMOTED_SOURCE_REVISION,
    PROMOTED_SOURCE_SNAPSHOT_SHA256,
    PROMOTED_SEED_SCHEMA,
    PRODUCER_NAMES,
    PROMOTED_WINDOWS_PLAN_SHA256,
    PROMOTED_WINDOWS_MANIFEST_SHA256,
    PROMOTED_WINDOWS_SEED_SCHEMA,
    SEED_SCHEMA,
    SEED_SOURCE_REVISION,
    SEED_SOURCE_SNAPSHOT_SHA256,
    SeedInputs,
    Stage,
    TOOL_NAMES,
    ToolRunner,
    WINDOWS_SEED_SCHEMA,
    WINDOWS_SEED_SOURCE_SNAPSHOT_SHA256,
    WINDOWS_CUPIDBUILD_IMPORT_PROFILES,
    WINDOWS_CUPIDBUILD_IMPORTS,
    WINDOWS_CUPIDBUILD_SEED_IMPORTS,
    WINDOWS_LINKER_IMPORTS,
    WSL_PRIVATE_RUN_SCRIPT,
    _build_plan_sha256,
    _compare_stages,
    _compare_windows_stages,
    _build_stage,
    _build_windows_stage,
    _bootstrap_from_frozen_seed,
    _bootstrap_windows_from_frozen_seed,
    _candidate_build_plan,
    _check_executable_code_anchor_behavior,
    _code_anchor_executable_payload,
    _corrupt_candidate_entry_instruction,
    _local_target_executable_payload,
    _local_target_object_payload,
    _remove_private_tool_directory,
    _require_seed_pair_identity,
    _profile_snapshot_payload,
    _run_behavior_checks,
    _run_native_windows_behavior_checks,
    _run_stage_pair,
    _unowned_relocation_object_payload,
    _windows_build_plan,
    _validate_static_i386_pe32,
    bootstrap_from_seed,
    bootstrap_windows_from_seed,
    capture_source_snapshot,
    freeze_source_inputs,
    freeze_seed_inputs,
    publish_bootstrap_outputs,
    require_frozen_source_snapshot,
    require_source_snapshot,
    main as bootstrap_main,
    run_seed_tool,
    verify_seed_inputs,
)
from tools.cupidc_kernel_compile import (
    KERNEL_I386_ARGUMENTS,
    validate_i386_relocatable_bytes,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP_TOOL = REPO_ROOT / "tools" / "bootstrap_toolchain.py"
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-linux"
    / "manifest.json"
)
WINDOWS_SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-windows"
    / "manifest.json"
)
class ToolchainBootstrapSeedCliTests(unittest.TestCase):
    @staticmethod
    def _legacy_linux_manifest_fixture(
        document: dict[str, object],
    ) -> dict[str, object]:
        manifest = json.loads(json.dumps(document))
        manifest["schema"] = SEED_SCHEMA
        manifest["artifacts"] = [
            artifact
            for artifact in manifest["artifacts"]
            if artifact["name"] != "cupidbuild"
        ]
        plan = manifest["build_plan"]
        plan["sources"] = plan["sources"][:-3]
        del plan["links"]["cupidbuild"]
        manifest["build_plan_sha256"] = _build_plan_sha256(plan)
        manifest["provenance"] = {
            "fixed_point_command": "make bootstrap-from-seed",
            "fixed_point_result": "pass",
            "producer_lineage": manifest["provenance"]["producer_lineage"],
            "seed_generation": "stage-four",
            "source_input_count": 50,
            "source_revision": SEED_SOURCE_REVISION,
            "source_snapshot_sha256": SEED_SOURCE_SNAPSHOT_SHA256,
        }
        return manifest

    @staticmethod
    def _promote_manifest_fixture(
        document: dict[str, object], windows: bool
    ) -> dict[str, object]:
        manifest = json.loads(json.dumps(document))
        artifacts = manifest["artifacts"]
        assert isinstance(artifacts, list)
        suffix = ".exe" if windows else ".elf"
        if not any(artifact.get("name") == "cupidbuild" for artifact in artifacts):
            artifacts.append(
                {
                    "file": f"cupidbuild{suffix}",
                    "name": "cupidbuild",
                    "producer": False,
                    "sha256": "",
                    "size": 0,
                }
            )
        revision = PROMOTED_SOURCE_REVISION
        snapshot = PROMOTED_SOURCE_SNAPSHOT_SHA256
        lineage = manifest["provenance"]["producer_lineage"]
        if windows:
            manifest["schema"] = PROMOTED_WINDOWS_SEED_SCHEMA
            manifest["provenance"] = {
                "artifact_generation": (
                    "paired-stage-four-six-tool-native-windows"
                ),
                "fixed_point_command": (
                    "make bootstrap-windows-from-seed"
                ),
                "fixed_point_result": "pass",
                "linux_candidate_build_plan_sha256": (
                    PROMOTED_LINUX_PLAN_SHA256
                ),
                "native_build_plan_sha256": PROMOTED_WINDOWS_PLAN_SHA256,
                "plan_seed_manifest_sha256": "3" * 64,
                "parent_execution_seed_manifest_sha256": (
                    "bf6147cf2e8249372869a24e5b8477ffb785d9a48eef80209366cfbaff19c7db"
                ),
                "parent_execution_seed_source_revision": (
                    "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
                ),
                "parent_plan_seed_manifest_sha256": (
                    "770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae"
                ),
                "parent_plan_seed_source_revision": (
                    "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
                ),
                "producer_lineage": lineage,
                "source_input_count": PROMOTED_SOURCE_INPUT_COUNT,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        else:
            plan = _candidate_build_plan(manifest["build_plan"])
            manifest["schema"] = PROMOTED_SEED_SCHEMA
            manifest["build_plan"] = plan
            manifest["build_plan_sha256"] = _build_plan_sha256(plan)
            manifest["provenance"] = {
                "artifact_generation": "paired-stage-four-six-tool",
                "fixed_point_command": "make bootstrap-from-seed",
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    "770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae"
                ),
                "parent_seed_source_revision": (
                    "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
                ),
                "producer_lineage": lineage,
                "seed_generation": "stage-four",
                "source_input_count": PROMOTED_SOURCE_INPUT_COUNT,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        return manifest

    def test_stage_pair_mismatch_reports_each_observed_result(self):
        runner = mock.Mock()
        runner.run.side_effect = [
            subprocess.CompletedProcess(
                ["stage-three"], 1, "first out", "first error"
            ),
            subprocess.CompletedProcess(
                ["stage-four"], 2, "second out", "second error"
            ),
        ]
        stage_three = Stage(objects={}, tools={"cupidld": Path("three")})
        stage_four = Stage(objects={}, tools={"cupidld": Path("four")})

        with self.assertRaisesRegex(
            BootstrapError,
            "^cupidld behavior differs across stages: "
            "stage-three status 1, stdout 'first out', stderr "
            "'first error'; stage-four status 2, stdout 'second out', "
            "stderr 'second error'$",
        ):
            _run_stage_pair(
                runner,
                stage_three,
                stage_four,
                "cupidld",
                ["--help"],
            )

    def test_candidate_plan_adds_cupidbuild_without_changing_the_v1_plan(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        checked_plan["sources"] = checked_plan["sources"][:-3]
        del checked_plan["links"]["cupidbuild"]
        original_plan = json.loads(json.dumps(checked_plan))

        candidate_plan = _candidate_build_plan(checked_plan)

        self.assertEqual(
            TOOL_NAMES,
            ("cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc"),
        )
        self.assertEqual(
            CANDIDATE_TOOL_NAMES,
            (
                "cupidasm",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidc",
                "cupidbuild",
            ),
        )
        self.assertEqual(checked_plan, original_plan)
        self.assertEqual(
            candidate_plan["sources"][-3:],
            [
                {
                    "gnu_extensions": False,
                    "name": "cupidbuild",
                    "path": "/toolchain/cupidbuild.cc",
                },
                {
                    "gnu_extensions": False,
                    "name": "cupidbuild_host",
                    "path": "/toolchain/cupidbuild_host.cc",
                },
                {
                    "gnu_extensions": False,
                    "name": "cupidbuild_main",
                    "path": "/toolchain/cupidbuild_main.cc",
                },
            ],
        )
        self.assertEqual(
            candidate_plan["links"]["cupidbuild"],
            [
                "start",
                "cupidbuild_main",
                "cupidbuild",
                "cupidbuild_host",
                "ctool_host",
                "ctool",
                "elf32",
                "runtime",
            ],
        )
        self.assertNotEqual(
            _build_plan_sha256(candidate_plan),
            _build_plan_sha256(checked_plan),
        )
        source_inventory = capture_source_snapshot(REPO_ROOT, candidate_plan)
        self.assertEqual(
            len(source_inventory), PROMOTED_SOURCE_INPUT_COUNT
        )
        for path in (
            "toolchain/cupidbuild.cc",
            "toolchain/cupidbuild_host.cc",
            "toolchain/cupidbuild_main.cc",
        ):
            self.assertIn(path, source_inventory)

    def test_candidate_plan_accepts_an_exact_six_tool_plan(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        candidate_plan = _candidate_build_plan(checked_plan)

        self.assertEqual(
            _candidate_build_plan(candidate_plan), candidate_plan
        )
        self.assertEqual(
            _build_plan_sha256(candidate_plan), PROMOTED_LINUX_PLAN_SHA256
        )

    def test_promoted_linux_seed_verifies_all_six_artifacts(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-promoted-linux-seed-"
        ) as temporary:
            seed = Path(temporary) / "seed"
            shutil.copytree(SEED_MANIFEST.parent, seed)
            manifest = self._promote_manifest_fixture(
                json.loads(
                    (seed / "manifest.json").read_text(encoding="utf-8")
                ),
                False,
            )
            (seed / "manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            verified = verify_seed_inputs(seed / "manifest.json")

        self.assertEqual(tuple(verified.tools), CANDIDATE_TOOL_NAMES)
        self.assertEqual(
            tuple(name for name, _data in verified.artifact_bytes),
            CANDIDATE_TOOL_NAMES,
        )

    def test_promoted_windows_seed_selects_publication_profiles(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-promoted-windows-seed-"
        ) as temporary:
            seed = Path(temporary) / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, seed)
            manifest = self._promote_manifest_fixture(
                json.loads(
                    (seed / "manifest.json").read_text(encoding="utf-8")
                ),
                True,
            )
            (seed / "manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_pe32_bytes"
            ) as validate:
                verified = verify_seed_inputs(seed / "manifest.json")

        self.assertEqual(tuple(verified.tools), CANDIDATE_TOOL_NAMES)
        profiles = {
            call.args[1]: call.args[3] for call in validate.call_args_list
        }
        self.assertEqual(profiles["cupidasm.exe"], WINDOWS_LINKER_IMPORTS)
        self.assertEqual(
            profiles["cupidbuild.exe"], WINDOWS_CUPIDBUILD_SEED_IMPORTS
        )

    def test_windows_cupidbuild_import_profiles_are_exact_pairs(self):
        legacy_kernel = WINDOWS_CUPIDBUILD_SEED_IMPORTS[0][1]
        current_kernel = WINDOWS_CUPIDBUILD_IMPORTS[0][1]
        set_file_pointer = legacy_kernel.index("SetFilePointer")
        self.assertEqual(
            current_kernel,
            (
                *legacy_kernel[: set_file_pointer + 1],
                "SetHandleInformation",
                *legacy_kernel[set_file_pointer + 1 :],
            ),
        )
        self.assertEqual(
            WINDOWS_CUPIDBUILD_SEED_IMPORTS[1],
            ("NTDLL.dll", ("NtSetInformationFile",)),
        )
        self.assertEqual(
            WINDOWS_CUPIDBUILD_IMPORTS[1],
            (
                "NTDLL.dll",
                (
                    "NtCreateFile",
                    "NtQueryDirectoryFile",
                    "NtSetInformationFile",
                ),
            ),
        )
        self.assertEqual(
            WINDOWS_CUPIDBUILD_IMPORT_PROFILES,
            (
                WINDOWS_CUPIDBUILD_SEED_IMPORTS,
                WINDOWS_CUPIDBUILD_IMPORTS,
            ),
        )
        exactly_two_ntdll = (
            "NTDLL.dll",
            WINDOWS_CUPIDBUILD_IMPORTS[1][1][:2],
        )
        rejected_profiles = (
            (
                WINDOWS_CUPIDBUILD_SEED_IMPORTS[0],
                WINDOWS_CUPIDBUILD_IMPORTS[1],
            ),
            (
                WINDOWS_CUPIDBUILD_IMPORTS[0],
                WINDOWS_CUPIDBUILD_SEED_IMPORTS[1],
            ),
            (WINDOWS_CUPIDBUILD_SEED_IMPORTS[0], exactly_two_ntdll),
            (WINDOWS_CUPIDBUILD_IMPORTS[0], exactly_two_ntdll),
        )
        for profile in rejected_profiles:
            self.assertNotIn(profile, WINDOWS_CUPIDBUILD_IMPORT_PROFILES)

    def test_cupidbuild_source_locks_the_paired_import_profiles(self):
        source = (REPO_ROOT / "toolchain" / "cupidbuild.cc").read_text(
            encoding="utf-8"
        )
        for required in (
            "cupidbuild_legacy_imports",
            "cupidbuild_current_imports",
            '"SetFilePointer",\n        "SetHandleInformation",',
            "cupidbuild_legacy_ntdll_imports",
            "cupidbuild_current_ntdll_imports",
            "legacy_count + legacy_ntdll_count",
            "current_count + current_ntdll_count",
            "expected_ntdll_imports[index]",
        ):
            self.assertIn(required, source)
        self.assertNotIn("expected_total_minimum", source)
        self.assertNotIn("expected_total_maximum", source)

        startup = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-windows"
            / "cupidbuild_start.asm"
        ).read_text(encoding="utf-8")
        for required in (
            "extern __imp_SetHandleInformation",
            "extern __imp_NtQueryDirectoryFile",
            "global cupid_windows_set_handle_information:function",
            "global cupid_windows_nt_query_directory_file:function",
            "cupid_windows_set_handle_information:",
            "cupid_windows_nt_query_directory_file:",
            "call dword [__imp_SetHandleInformation]",
            "call dword [__imp_NtQueryDirectoryFile]",
        ):
            self.assertIn(required, startup)
        self.assertNotIn("cupid_windows_set_file_pointer", startup)

    def test_promoted_seed_rejects_provenance_and_artifact_drift(self):
        source = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))

        def change_plan(manifest):
            manifest["build_plan"]["workers"] = 1
            manifest["build_plan_sha256"] = _build_plan_sha256(
                manifest["build_plan"]
            )

        cases = {
            "uppercase revision": (
                lambda manifest: manifest["provenance"].update(
                    {
                        "source_revision": (
                            "ABCDEF0123456789ABCDEF0123456789ABCDEF01"
                        )
                    }
                ),
                "source revision is invalid",
            ),
            "wrong parent": (
                lambda manifest: manifest["provenance"].update(
                    {"parent_seed_manifest_sha256": "0" * 64}
                ),
                "parent seed manifest differs",
            ),
            "wrong producer": (
                lambda manifest: manifest["artifacts"][-1].update(
                    {"producer": True}
                ),
                "artifact producer role differs: cupidbuild",
            ),
            "wrong artifact size": (
                lambda manifest: manifest["artifacts"][-1].update(
                    {"size": 1}
                ),
                "promoted artifact size differs: cupidbuild",
            ),
            "wrong artifact digest": (
                lambda manifest: manifest["artifacts"][-1].update(
                    {"sha256": "0" * 64}
                ),
                "promoted artifact SHA-256 differs: cupidbuild",
            ),
            "missing artifact": (
                lambda manifest: manifest["artifacts"].pop(),
                "manifest must contain six tool artifacts",
            ),
            "wrong plan": (
                change_plan,
                "promoted build plan SHA-256 differs",
            ),
        }
        for label, (mutate, expected) in cases.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="cupid-promoted-seed-drift-"
            ) as temporary:
                seed = Path(temporary) / "seed"
                shutil.copytree(SEED_MANIFEST.parent, seed)
                manifest = self._promote_manifest_fixture(source, False)
                mutate(manifest)
                (seed / "manifest.json").write_text(
                    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )

                with self.assertRaisesRegex(
                    BootstrapError, f"^{re.escape(expected)}$"
                ):
                    verify_seed_inputs(seed / "manifest.json")

    def test_manifest_reader_rejects_json_string_escapes(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-seed-json-escape-"
        ) as temporary:
            manifest = Path(temporary) / "manifest.json"
            encoded = SEED_MANIFEST.read_text(encoding="utf-8").replace(
                PROMOTED_SEED_SCHEMA,
                r"\u0063upid.bootstrap-seed.v2",
                1,
            )
            manifest.write_text(encoded, encoding="utf-8", newline="\n")

            with self.assertRaisesRegex(
                BootstrapError,
                "^manifest strings may not use escapes$",
            ):
                verify_seed_inputs(manifest)

    def test_windows_bootstrap_requires_one_seed_identity(self):
        revision = "abcdef0123456789abcdef0123456789abcdef01"
        snapshot = "2" * 64

        def seed(
            schema,
            current_revision=revision,
            current_snapshot=snapshot,
            manifest_digest="0" * 64,
        ):
            provenance = {
                "source_revision": current_revision,
                "source_snapshot_sha256": current_snapshot,
            }
            if schema == PROMOTED_WINDOWS_SEED_SCHEMA:
                provenance["plan_seed_manifest_sha256"] = "0" * 64
            return SeedInputs(
                manifest={
                    "provenance": provenance,
                    "schema": schema,
                },
                manifest_bytes=b"{}",
                manifest_sha256=manifest_digest,
                live_manifest_path=Path("manifest.json"),
                artifact_bytes=(),
                tools={},
            )

        execution = seed(PROMOTED_WINDOWS_SEED_SCHEMA)
        plan = seed(PROMOTED_SEED_SCHEMA)
        _require_seed_pair_identity(execution, plan)

        cases = {
            "generation": (
                seed(SEED_SCHEMA),
                "native Windows bootstrap seed generation differs",
            ),
            "revision": (
                seed(
                    PROMOTED_SEED_SCHEMA,
                    "0123456789abcdef0123456789abcdef01234567",
                ),
                "native Windows bootstrap seed source revision differs",
            ),
            "snapshot": (
                seed(PROMOTED_SEED_SCHEMA, current_snapshot="3" * 64),
                "native Windows bootstrap seed source snapshot differs",
            ),
            "plan manifest": (
                seed(PROMOTED_SEED_SCHEMA, manifest_digest="1" * 64),
                "native Windows bootstrap plan seed manifest differs",
            ),
        }
        for label, (changed_plan, expected) in cases.items():
            with self.subTest(label=label), self.assertRaisesRegex(
                BootstrapError,
                f"^{expected}$",
            ):
                _require_seed_pair_identity(execution, changed_plan)

    def test_candidate_plan_rejects_a_reserved_cupidbuild_source(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        checked_plan["sources"].append(
            {
                "gnu_extensions": False,
                "name": "cupidbuild",
                "path": "/toolchain/untrusted-cupidbuild.cc",
            }
        )

        with self.assertRaisesRegex(
            BootstrapError,
            "^Linux build plan uses the reserved candidate source name: "
            "cupidbuild$",
        ):
            _candidate_build_plan(checked_plan)

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
                for name in ("cupidasm", "cupiddis", "cupidld")
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
                "include_arguments": [],
                "workers": 1,
                "assembly_sources": [
                    {"name": "start", "path": str(startup)},
                ],
                "links": {name: ["start"] for name in TOOL_NAMES},
            }
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
                for name in ("cupidasm", "cupiddis", "cupidld")
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
                "include_arguments": [],
                "workers": 1,
                "assembly_sources": [
                    {"name": "start", "path": str(startup)},
                    {
                        "name": "publication_start",
                        "path": str(publication_start),
                    },
                    {
                        "name": "cupidbuild_start",
                        "path": str(cupidbuild_start),
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
                                "MoveFileExA",
                                "OpenProcess",
                                "ReadFile",
                                "RemoveDirectoryA",
                                "SetFilePointer",
                                "SetHandleInformation",
                                "TerminateProcess",
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
                for name in ("cupidasm", "cupiddis", "cupidld")
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

    def _assert_checked_seed_local_relative_target_policy(
        self,
        manifest: Path,
        *,
        native_windows: bool = False,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-local-targets-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            valid = root / "valid.bin"
            invalid = root / "invalid.bin"
            valid.write_bytes(bytes([0xEB, 0x00, 0xC3]))
            invalid.write_bytes(bytes([0xEB, 0x7F]))
            arguments = (
                "--require-known",
                "--require-local-targets",
                "--raw",
                "--mode",
                "32",
                "--base",
                "0",
            )
            runner_guard = (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.which",
                    side_effect=AssertionError(
                        "native seed must not probe WSL"
                    ),
                )
                if native_windows
                else contextlib.nullcontext()
            )
            with runner_guard:
                accepted = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, valid),
                    timeout=60,
                )
                rejected = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, invalid),
                    timeout=60,
                )

            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(accepted.stdout, "")
            self.assertEqual(accepted.stderr, "")
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "1 of 1 direct relative targets invalid",
                rejected.stderr,
            )
            self.assertIn(
                "1 outside image, 0 in data, 0 wrong mode, "
                "0 mid-instruction",
                rejected.stderr,
            )

    def _assert_checked_seed_relocatable_local_target_policy(
        self,
        manifest: Path,
        *,
        native_windows: bool = False,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-object-local-targets-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            valid = root / "valid.o"
            invalid = root / "invalid.o"
            valid.write_bytes(_local_target_object_payload(5))
            invalid.write_bytes(_local_target_object_payload(1))
            arguments = (
                "--require-known",
                "--require-local-targets",
            )
            runner_guard = (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.which",
                    side_effect=AssertionError(
                        "native seed must not probe WSL"
                    ),
                )
                if native_windows
                else contextlib.nullcontext()
            )
            with runner_guard:
                accepted = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, valid),
                    timeout=60,
                )
                rejected = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, invalid),
                    timeout=60,
                )

            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(accepted.stdout, "")
            self.assertEqual(accepted.stderr, "")
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "1 of 1 direct relative targets invalid",
                rejected.stderr,
            )
            self.assertIn("1 mid-instruction", rejected.stderr)

    def _assert_checked_seed_linked_local_target_policy(
        self,
        manifest: Path,
        *,
        native_windows: bool = False,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-linked-local-targets-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            valid = root / "valid.elf"
            invalid = root / "invalid.elf"
            valid.write_bytes(_local_target_executable_payload(5))
            invalid.write_bytes(_local_target_executable_payload(1))
            arguments = (
                "--require-known",
                "--require-local-targets",
            )
            runner_guard = (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.which",
                    side_effect=AssertionError(
                        "native seed must not probe WSL"
                    ),
                )
                if native_windows
                else contextlib.nullcontext()
            )
            with runner_guard:
                accepted = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, valid),
                    timeout=60,
                )
                rejected = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, invalid),
                    timeout=60,
                )

            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(accepted.stdout, "")
            self.assertEqual(accepted.stderr, "")
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "1 of 1 direct relative targets invalid",
                rejected.stderr,
            )
            self.assertIn("1 mid-instruction", rejected.stderr)

    def _assert_checked_seed_code_anchor_policy(
        self,
        manifest: Path,
        *,
        native_windows: bool = False,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-code-anchors-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            valid = root / "valid.elf"
            invalid = root / "invalid.elf"
            valid.write_bytes(_code_anchor_executable_payload())
            invalid.write_bytes(
                _code_anchor_executable_payload(entry=0x00400001)
            )
            arguments = (
                "--require-known",
                "--require-code-anchors",
            )
            runner_guard = (
                mock.patch(
                    "tools.bootstrap_toolchain.shutil.which",
                    side_effect=AssertionError(
                        "native seed must not probe WSL"
                    ),
                )
                if native_windows
                else contextlib.nullcontext()
            )
            with runner_guard:
                accepted = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, valid),
                    timeout=60,
                )
                rejected = run_seed_tool(
                    manifest,
                    REPO_ROOT,
                    "cupiddis",
                    (*arguments, invalid),
                    timeout=60,
                )

            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(accepted.stdout, "")
            self.assertEqual(accepted.stderr, "")
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            rejected_path = (
                str(invalid)
                if native_windows or os.name != "nt"
                else ToolRunner(REPO_ROOT)._wsl_path(invalid)
            )
            self.assertEqual(
                rejected.stderr,
                f"cupiddis: {rejected_path}: code anchor check failed: "
                "1 of 3 code anchors invalid (0 outside file-backed "
                "executable code, 1 mid-instruction)\n",
            )

    @staticmethod
    def _committed_source_inventory(
        revision: str,
        logical_paths: dict[str, object],
    ) -> dict[str, dict[str, object]]:
        native_prefix = ["git", "-C", str(REPO_ROOT)]
        try:
            native_probe = subprocess.run(
                [*native_prefix, "rev-parse", "--is-inside-work-tree"],
                text=True,
                capture_output=True,
            )
        except OSError:
            if os.name != "nt":
                raise
            native_probe = None
        if native_probe is not None and native_probe.returncode == 0:
            command_prefix = native_prefix
        elif os.name == "nt":
            runner = ToolRunner(REPO_ROOT)
            wsl_command = runner._wsl_command()
            repository = runner._wsl_path(REPO_ROOT)
            command_prefix = [
                wsl_command,
                "-e",
                "git",
                "-C",
                repository,
            ]
        else:
            raise AssertionError(native_probe.stderr)
        tree = subprocess.run(
            [*command_prefix, "ls-tree", "-r", "--name-only", revision],
            capture_output=True,
        )
        if tree.returncode != 0:
            raise AssertionError(
                tree.stderr.decode("utf-8", errors="replace")
            )
        committed_paths = set(tree.stdout.decode("utf-8").splitlines())
        inventory: dict[str, dict[str, object]] = {}
        for logical_path in sorted(set(logical_paths) & committed_paths):
            result = subprocess.run(
                [*command_prefix, "show", f"{revision}:{logical_path}"],
                capture_output=True,
            )
            if result.returncode != 0:
                raise AssertionError(
                    result.stderr.decode("utf-8", errors="replace")
                )
            inventory[logical_path] = {
                "sha256": hashlib.sha256(result.stdout).hexdigest(),
                "size": len(result.stdout),
            }
        return inventory

    @unittest.skipUnless(os.name == "nt", "Windows Git fallback")
    def test_missing_native_git_falls_back_to_wsl_for_named_revision_inventory(
        self,
    ):
        revision = "a" * 40
        logical_path = "toolchain/ctool.cc"
        committed_source = b"named revision source\n"
        commands: list[list[str]] = []
        wsl_command = str(Path("C:/Windows/System32/wsl.exe"))

        def run(command, *args, **kwargs):
            commands.append(command)
            if command[:3] == ["git", "-C", str(REPO_ROOT)]:
                raise FileNotFoundError("git.exe is unavailable")
            if command[:3] == [wsl_command, "-e", "wslpath"]:
                return subprocess.CompletedProcess(
                    command,
                    0,
                    stdout="/mnt/c/cupid-os\n",
                    stderr="",
                )
            if command[:3] == [wsl_command, "-e", "git"]:
                if "ls-tree" in command:
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout=f"{logical_path}\n".encode("utf-8"),
                        stderr=b"",
                    )
                return subprocess.CompletedProcess(
                    command,
                    0,
                    stdout=committed_source,
                    stderr=b"",
                )
            raise AssertionError(f"unexpected command: {command}")

        with mock.patch.object(
            ToolRunner,
            "_wsl_command",
            return_value=wsl_command,
        ), mock.patch.object(subprocess, "run", side_effect=run):
            inventory = self._committed_source_inventory(
                revision,
                {logical_path: object()},
            )

        self.assertEqual(
            inventory,
            {
                logical_path: {
                    "sha256": (
                        "817260d655079840b5bf05a734ef27c2"
                        "24c4e5e3acfe4825b409b4dbfb375618"
                    ),
                    "size": 22,
                }
            },
        )
        self.assertTrue(
            any(
                command[:3] == [wsl_command, "-e", "git"]
                for command in commands
            )
        )

    @staticmethod
    def _minimal_pe32() -> bytearray:
        image = bytearray(0x400)
        image[:0x80] = bytes.fromhex(
            "4d5a90000300000004000000ffff0000"
            "b8000000000000004000000000000000"
            "00000000000000000000000000000000"
            "00000000000000000000000080000000"
            "0e1fba0e00b409cd21b8014ccd215468"
            "69732070726f6772616d2063616e6e6f"
            "742062652072756e20696e20444f5320"
            "6d6f64652e0d0d0a2400000000000000"
        )
        image[0x80:0x84] = b"PE\0\0"
        struct.pack_into(
            "<HHIIIHH",
            image,
            0x84,
            0x014C,
            1,
            0,
            0,
            0,
            0x00E0,
            0x0103,
        )
        optional = 0x98
        struct.pack_into("<H", image, optional, 0x010B)
        struct.pack_into("<I", image, optional + 4, 0x200)
        struct.pack_into("<I", image, optional + 16, 0x1000)
        struct.pack_into("<I", image, optional + 20, 0x1000)
        struct.pack_into("<I", image, optional + 28, 0x00400000)
        struct.pack_into("<I", image, optional + 32, 0x1000)
        struct.pack_into("<I", image, optional + 36, 0x200)
        struct.pack_into("<HH", image, optional + 40, 6, 0)
        struct.pack_into("<HH", image, optional + 48, 6, 0)
        struct.pack_into("<I", image, optional + 56, 0x2000)
        struct.pack_into("<I", image, optional + 60, 0x200)
        struct.pack_into("<H", image, optional + 68, 3)
        struct.pack_into("<H", image, optional + 70, 0x0100)
        struct.pack_into(
            "<IIII",
            image,
            optional + 72,
            0x100000,
            0x100000,
            0x100000,
            0x1000,
        )
        struct.pack_into("<I", image, optional + 92, 16)
        section = optional + 0xE0
        image[section : section + 8] = b".text\0\0\0"
        struct.pack_into("<IIII", image, section + 8, 1, 0x1000, 0x200, 0x200)
        struct.pack_into("<I", image, section + 36, 0x60000020)
        image[0x200] = 0xC3
        return image

    @classmethod
    def _minimal_import_pe32(cls) -> bytearray:
        image = cls._minimal_pe32()
        image.extend(b"\0" * 0x200)
        optional = 0x98
        struct.pack_into("<H", image, 0x86, 2)
        struct.pack_into("<I", image, optional + 8, 0x200)
        struct.pack_into("<I", image, optional + 24, 0x2000)
        struct.pack_into("<I", image, optional + 56, 0x3000)
        struct.pack_into("<II", image, optional + 96 + 8, 0x2000, 0x28)
        struct.pack_into(
            "<II", image, optional + 96 + 12 * 8, 0x2030, 8
        )
        section = 0x1A0
        image[section : section + 8] = b".idata\0\0"
        struct.pack_into(
            "<IIII", image, section + 8, 0x54, 0x2000, 0x200, 0x400
        )
        struct.pack_into("<I", image, section + 36, 0xC0000040)
        struct.pack_into("<IIIII", image, 0x400, 0x2028, 0, 0, 0x2038, 0x2030)
        struct.pack_into("<II", image, 0x428, 0x2046, 0)
        struct.pack_into("<II", image, 0x430, 0x2046, 0)
        image[0x438:0x445] = b"KERNEL32.dll\0"
        image[0x446:0x454] = b"\0\0ExitProcess\0"
        return image

    @classmethod
    def _minimal_two_library_import_pe32(cls) -> bytearray:
        image = cls._minimal_import_pe32()
        optional = 0x98
        section = 0x1A0
        struct.pack_into("<I", image, section + 8, 0x90)
        struct.pack_into("<II", image, optional + 96 + 8, 0x2000, 0x3C)
        struct.pack_into(
            "<II", image, optional + 96 + 12 * 8, 0x204C, 0x10
        )
        image[0x400:0x490] = b"\0" * 0x90
        struct.pack_into(
            "<IIIII", image, 0x400, 0x203C, 0, 0, 0x205C, 0x204C
        )
        struct.pack_into(
            "<IIIII", image, 0x414, 0x2044, 0, 0, 0x2069, 0x2054
        )
        struct.pack_into("<II", image, 0x43C, 0x2074, 0)
        struct.pack_into("<II", image, 0x444, 0x2082, 0)
        struct.pack_into("<II", image, 0x44C, 0x2074, 0)
        struct.pack_into("<II", image, 0x454, 0x2082, 0)
        image[0x45C:0x469] = b"KERNEL32.dll\0"
        image[0x469:0x474] = b"USER32.dll\0"
        image[0x474:0x482] = b"\0\0ExitProcess\0"
        image[0x482:0x490] = b"\0\0MessageBoxA\0"
        return image

    def test_pe32_fixed_point_validator_rejects_false_layouts(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-pe32-"
        ) as temporary:
            image_path = Path(temporary) / "fixed.exe"
            valid = self._minimal_pe32()
            image_path.write_bytes(valid)
            _validate_static_i386_pe32(image_path, 0x00401000)

            empty_rodata = bytearray(valid)
            struct.pack_into("<H", empty_rodata, 0x86, 2)
            struct.pack_into("<I", empty_rodata, 0x98 + 24, 0x2000)
            empty_rodata[0x1A0:0x1A8] = b".rodata\0"
            struct.pack_into(
                "<IIII", empty_rodata, 0x1A8, 0, 0x2000, 0, 0
            )
            struct.pack_into("<I", empty_rodata, 0x1A0 + 36, 0x40000040)
            image_path.write_bytes(empty_rodata)
            with self.assertRaisesRegex(
                BootstrapError, "empty PE32 section"
            ):
                _validate_static_i386_pe32(image_path, 0x00401000)

            oversized_raw = bytearray(valid)
            struct.pack_into("<I", oversized_raw, 0x98 + 4, 0x2000)
            struct.pack_into("<I", oversized_raw, 0x178 + 16, 0x2000)
            oversized_raw.extend(b"\0" * 0x1E00)
            image_path.write_bytes(oversized_raw)
            with self.assertRaises(BootstrapError):
                _validate_static_i386_pe32(image_path, 0x00401000)

            mutations = (
                ("short image extent", "<I", 0x98 + 56, 0x1000),
                ("raw data overlaps headers", "<I", 0x178 + 20, 0),
                (
                    "writable executable section",
                    "<I",
                    0x178 + 36,
                    0xE0000020,
                ),
                ("nonzero checksum", "<I", 0x98 + 64, 1),
                ("wrong subsystem", "<H", 0x98 + 68, 2),
                ("one-page stack commit", "<I", 0x98 + 76, 0x1000),
                ("nonzero relocation pointer", "<I", 0x178 + 24, 1),
            )
            for label, encoding, offset, value in mutations:
                with self.subTest(label=label):
                    mutated = bytearray(valid)
                    struct.pack_into(encoding, mutated, offset, value)
                    image_path.write_bytes(mutated)
                    with self.assertRaises(BootstrapError):
                        _validate_static_i386_pe32(
                            image_path, 0x00401000
                        )

            for label, offset in (
                ("changed DOS stub", 2),
                ("nonzero header padding", 0x1A0),
                ("nonzero section padding", 0x201),
            ):
                with self.subTest(label=label):
                    mutated = bytearray(valid)
                    mutated[offset] = 1
                    image_path.write_bytes(mutated)
                    with self.assertRaises(BootstrapError):
                        _validate_static_i386_pe32(
                            image_path, 0x00401000
                        )

    def test_pe32_import_validator_rejects_corrupt_tables(self):
        expected = (("KERNEL32.dll", ("ExitProcess",)),)
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-pe32-import-"
        ) as temporary:
            image_path = Path(temporary) / "import.exe"
            valid = self._minimal_import_pe32()
            image_path.write_bytes(valid)
            _validate_static_i386_pe32(
                image_path, 0x00401000, expected
            )

            mutations = (
                ("short import directory", "<I", 0x104, 20),
                ("stateful descriptor", "<I", 0x404, 1),
                ("IAT differs from ILT", "<I", 0x430, 0x2048),
                ("nonzero import hint", "<H", 0x446, 1),
                ("missing null descriptor", "<I", 0x414, 1),
                ("wrong IAT extent", "<I", 0x15C, 4),
            )
            for label, encoding, offset, value in mutations:
                with self.subTest(label=label):
                    mutated = bytearray(valid)
                    struct.pack_into(encoding, mutated, offset, value)
                    image_path.write_bytes(mutated)
                    with self.assertRaises(BootstrapError):
                        _validate_static_i386_pe32(
                            image_path, 0x00401000, expected
                        )

            unterminated_library = bytearray(valid)
            unterminated_library[0x438:0x454] = b"A" * 0x1C
            image_path.write_bytes(unterminated_library)
            with self.assertRaisesRegex(
                BootstrapError, "unterminated import library"
            ):
                _validate_static_i386_pe32(
                    image_path,
                    0x00401000,
                    expected,
                )

            aliased_lookup = bytearray(valid)
            struct.pack_into("<I", aliased_lookup, 0x400, 0x2030)
            image_path.write_bytes(aliased_lookup)
            with self.assertRaisesRegex(
                BootstrapError, "noncanonical PE32 import lookup layout"
            ):
                _validate_static_i386_pe32(
                    image_path, 0x00401000, expected
                )

            nonzero_alignment = bytearray(valid)
            nonzero_alignment[0x445] = 1
            image_path.write_bytes(nonzero_alignment)
            with self.assertRaisesRegex(
                BootstrapError, "nonzero PE32 import alignment"
            ):
                _validate_static_i386_pe32(
                    image_path, 0x00401000, expected
                )

            extended_imports = bytearray(valid)
            struct.pack_into("<I", extended_imports, 0x1A8, 0x58)
            image_path.write_bytes(extended_imports)
            with self.assertRaisesRegex(
                BootstrapError, "noncanonical PE32 import section extent"
            ):
                _validate_static_i386_pe32(
                    image_path, 0x00401000, expected
                )

            displaced_lookup = bytearray(valid)
            struct.pack_into("<I", displaced_lookup, 0x180, 8)
            displaced_lookup[0x200:0x208] = valid[0x428:0x430]
            struct.pack_into("<I", displaced_lookup, 0x400, 0x1000)
            image_path.write_bytes(displaced_lookup)
            with self.assertRaisesRegex(
                BootstrapError, "noncanonical PE32 import lookup layout"
            ):
                _validate_static_i386_pe32(
                    image_path, 0x00401000, expected
                )

            displaced_descriptors = bytearray(valid)
            displaced_descriptors[0x480:0x4A8] = valid[0x400:0x428]
            struct.pack_into("<I", displaced_descriptors, 0x100, 0x2080)
            struct.pack_into("<I", displaced_descriptors, 0x1A8, 0xA8)
            image_path.write_bytes(displaced_descriptors)
            with self.assertRaisesRegex(
                BootstrapError, "noncanonical PE32 import directory"
            ):
                _validate_static_i386_pe32(
                    image_path, 0x00401000, expected
                )

            two_library_expected = (
                ("KERNEL32.dll", ("ExitProcess",)),
                ("USER32.dll", ("MessageBoxA",)),
            )
            two_library = self._minimal_two_library_import_pe32()
            image_path.write_bytes(two_library)
            _validate_static_i386_pe32(
                image_path, 0x00401000, two_library_expected
            )
            gapped_iat = bytearray(two_library)
            struct.pack_into("<I", gapped_iat, 0x424, 0x2090)
            gapped_iat[0x490:0x498] = two_library[0x454:0x45C]
            struct.pack_into("<I", gapped_iat, 0x15C, 0x4C)
            struct.pack_into("<I", gapped_iat, 0x1A8, 0x98)
            image_path.write_bytes(gapped_iat)
            with self.assertRaisesRegex(
                BootstrapError, "noncanonical PE32 import address layout"
            ):
                _validate_static_i386_pe32(
                    image_path, 0x00401000, two_library_expected
                )

    def _write_tiny_source_root(
        self, source_root: Path
    ) -> tuple[dict[str, object], Path]:
        toolchain = source_root / "toolchain"
        include = toolchain / "hosted" / "i386-linux" / "include"
        include.mkdir(parents=True)
        source = toolchain / "tiny.cc"
        source.write_text(
            "int tiny(void) { return 1; }\n",
            encoding="utf-8",
            newline="\n",
        )
        (toolchain / "tiny.h").write_text(
            "int tiny(void);\n",
            encoding="utf-8",
            newline="\n",
        )
        (include / "stddef.h").write_text(
            "typedef unsigned int size_t;\n",
            encoding="utf-8",
            newline="\n",
        )
        (toolchain / "hosted" / "i386-linux" / "start.asm").write_text(
            "bits 32\n",
            encoding="utf-8",
            newline="\n",
        )
        windows = toolchain / "hosted" / "i386-windows"
        windows.mkdir(parents=True)
        (windows / "start.asm").write_text(
            "bits 32\n",
            encoding="utf-8",
            newline="\n",
        )
        (windows / "runtime.cc").write_text(
            "int windows_runtime(void) { return 1; }\n",
            encoding="utf-8",
            newline="\n",
        )
        (windows / "tool_start.asm").write_text(
            "bits 32\n",
            encoding="utf-8",
            newline="\n",
        )
        (windows / "publication_start.asm").write_text(
            "bits 32\n",
            encoding="utf-8",
            newline="\n",
        )
        (windows / "cupidbuild_start.asm").write_text(
            "bits 32\n",
            encoding="utf-8",
            newline="\n",
        )
        (windows / "publication_runtime.cc").write_text(
            "int windows_publication_runtime(void) { return 1; }\n",
            encoding="utf-8",
            newline="\n",
        )
        (toolchain / "tests").mkdir()
        (toolchain / "tests" / "hosted_i386_windows_contract.cc").write_text(
            "int main(void) { return 0; }\n",
            encoding="utf-8",
            newline="\n",
        )
        (
            toolchain
            / "tests"
            / "hosted_i386_windows_runtime_contract.cc"
        ).write_text(
            "int main(void) { return 0; }\n",
            encoding="utf-8",
            newline="\n",
        )
        (source_root / "link.ld").write_text(
            "SECTIONS {}\n",
            encoding="utf-8",
            newline="\n",
        )
        plan: dict[str, object] = {
            "sources": [
                {
                    "gnu_extensions": False,
                    "name": "tiny",
                    "path": "/toolchain/tiny.cc",
                }
            ],
            "startup": "/toolchain/hosted/i386-linux/start.asm",
        }
        return plan, source

    def test_changed_source_is_rejected_by_the_snapshot_guard(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-source-"
        ) as temporary:
            source_root = Path(temporary)
            toolchain = source_root / "toolchain"
            include = toolchain / "hosted" / "i386-linux" / "include"
            include.mkdir(parents=True)
            source = toolchain / "tiny.c"
            startup = toolchain / "hosted" / "i386-linux" / "start.asm"
            linker_script = source_root / "link.ld"
            source.write_text("int tiny(void) { return 1; }\n")
            startup.write_text("bits 32\n")
            windows = toolchain / "hosted" / "i386-windows"
            windows.mkdir(parents=True)
            (windows / "start.asm").write_text("bits 32\n")
            (windows / "runtime.cc").write_text(
                "int windows_runtime(void) { return 1; }\n"
            )
            (windows / "tool_start.asm").write_text("bits 32\n")
            (windows / "publication_start.asm").write_text("bits 32\n")
            (windows / "cupidbuild_start.asm").write_text("bits 32\n")
            (windows / "publication_runtime.cc").write_text(
                "int windows_publication_runtime(void) { return 1; }\n"
            )
            (toolchain / "tests").mkdir()
            (
                toolchain / "tests" / "hosted_i386_windows_contract.cc"
            ).write_text("int main(void) { return 0; }\n")
            (
                toolchain
                / "tests"
                / "hosted_i386_windows_runtime_contract.cc"
            ).write_text("int main(void) { return 0; }\n")
            (toolchain / "tiny.h").write_text("int tiny(void);\n")
            linker_script.write_text("SECTIONS {}\n")
            plan = {
                "sources": [
                    {
                        "gnu_extensions": False,
                        "name": "tiny",
                        "path": "/toolchain/tiny.c",
                    }
                ],
                "startup": "/toolchain/hosted/i386-linux/start.asm",
            }
            snapshot = capture_source_snapshot(source_root, plan)
            source.write_text("int tiny(void) { return 2; }\n")

            with self.assertRaisesRegex(
                BootstrapError,
                "^source inputs changed during bootstrap: "
                "toolchain/tiny.c$",
            ):
                require_source_snapshot(source_root, plan, snapshot)

            source.write_text("int tiny(void) { return 1; }\n")
            snapshot = capture_source_snapshot(source_root, plan)
            linker_script.write_text("SECTIONS { . = 1; }\n")

            with self.assertRaisesRegex(
                BootstrapError,
                "^source inputs changed during bootstrap: link.ld$",
            ):
                require_source_snapshot(source_root, plan, snapshot)

    def test_seed_validation_binds_one_manifest_capture(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-capture-"
        ) as temporary:
            copied_seed = Path(temporary) / "seed"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            original = manifest_path.read_bytes()
            original_read_bytes = Path.read_bytes
            manifest_reads = 0

            def racing_read_bytes(path: Path) -> bytes:
                nonlocal manifest_reads
                captured = original_read_bytes(path)
                if path == manifest_path:
                    manifest_reads += 1
                    if manifest_reads == 1:
                        path.write_bytes(b'{"replacement":true}\n')
                return captured

            with mock.patch.object(
                Path, "read_bytes", racing_read_bytes
            ):
                seed = verify_seed_inputs(manifest_path)

            self.assertEqual(manifest_reads, 1)
            self.assertEqual(
                seed.manifest_sha256,
                hashlib.sha256(original).hexdigest(),
            )
            self.assertEqual(
                seed.manifest["build_plan_sha256"],
                json.loads(original.decode("utf-8"))[
                    "build_plan_sha256"
                ],
            )

    def test_windows_seed_freeze_validates_the_captured_pe_bytes(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-capture-"
        ) as temporary:
            root = Path(temporary)
            copied_seed = root / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
            tool_path = copied_seed / "cupiddis.exe"
            original = tool_path.read_bytes()
            original_read_bytes = Path.read_bytes
            tool_reads = 0

            def racing_read_bytes(path: Path) -> bytes:
                nonlocal tool_reads
                captured = original_read_bytes(path)
                if path == tool_path:
                    tool_reads += 1
                    if tool_reads == 1:
                        path.write_bytes(b"MZ")
                return captured

            with mock.patch.object(
                Path, "read_bytes", racing_read_bytes
            ):
                seed = freeze_seed_inputs(
                    copied_seed / "manifest.json", root / "private"
                )

            self.assertEqual(tool_reads, 1)
            self.assertEqual(seed.tools["cupiddis"].read_bytes(), original)
            self.assertEqual(tool_path.read_bytes(), b"MZ")

    def test_frozen_compiler_root_survives_a_live_change_and_restore(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-source-freeze-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "live"
            plan, source = self._write_tiny_source_root(source_root)
            original = source.read_bytes()
            frozen = freeze_source_inputs(
                source_root, plan, root / "private"
            )
            seed = freeze_seed_inputs(
                SEED_MANIFEST, root / "private-seed"
            )

            source.write_text(
                "int tiny(void) { return 99; }\n",
                encoding="utf-8",
                newline="\n",
            )
            try:
                changed_result = ToolRunner(source_root).run(
                    seed.tools["cupidc"],
                    [
                        "--root",
                        source_root,
                        "-c",
                        "/toolchain/tiny.cc",
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        "/changed.o",
                    ],
                    60,
                )
                compile_result = ToolRunner(frozen.root).run(
                    seed.tools["cupidc"],
                    [
                        "--root",
                        frozen.root,
                        "-c",
                        "/toolchain/tiny.cc",
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        "/tiny.o",
                    ],
                    60,
                )
            finally:
                source.write_bytes(original)

            self.assertEqual(
                compile_result.returncode, 0, compile_result.stderr
            )
            self.assertEqual(
                changed_result.returncode, 0, changed_result.stderr
            )
            self.assertEqual(compile_result.stdout, "")
            self.assertEqual(compile_result.stderr, "")
            self.assertEqual(
                (frozen.root / "tiny.o").read_bytes()[:7],
                b"\x7fELF\x01\x01\x01",
            )
            self.assertNotEqual(
                (frozen.root / "tiny.o").read_bytes(),
                (source_root / "changed.o").read_bytes(),
            )
            require_source_snapshot(
                source_root, plan, frozen.inventory
            )
            require_frozen_source_snapshot(frozen, plan)

    @unittest.skipIf(
        os.name == "nt" and shutil.which("wsl") is None,
        "WSL is required to execute the checked Linux seed",
    )
    def test_checked_seed_compiles_the_public_cupidbuild_size_api(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-cupidbuild-api-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "cupidbuild-api.cc"
            output = root / "cupidbuild-api.o"
            stddef_source = root / "stddef-contract.cc"
            stddef_output = root / "stddef-contract.o"
            source.write_text(
                '#include "cupidbuild.h"\n'
                "size_t cupidbuild_api_size(void) {\n"
                "  return sizeof(cupidbuild_jpeg_request_t);\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            stddef_source.write_text(
                "#include <stddef.h>\n"
                "struct stddef_record { char prefix; int value; };\n"
                "max_align_t stddef_alignment;\n"
                "ptrdiff_t stddef_contract(wchar_t value) {\n"
                "  return (ptrdiff_t)(\n"
                "      offsetof(struct stddef_record, value) +\n"
                "      sizeof(stddef_alignment) + sizeof(value));\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-c",
                    "/" + source.relative_to(REPO_ROOT).as_posix(),
                    "-I",
                    "/toolchain",
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/" + output.relative_to(REPO_ROOT).as_posix(),
                ],
                60,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            validate_i386_relocatable_bytes(output.read_bytes())

            stddef_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "-c",
                    "/" + stddef_source.relative_to(REPO_ROOT).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/" + stddef_output.relative_to(REPO_ROOT).as_posix(),
                ],
                60,
            )
            self.assertEqual(
                stddef_result.returncode, 0, stddef_result.stderr
            )
            self.assertEqual(stddef_result.stdout, "")
            self.assertEqual(stddef_result.stderr, "")
            validate_i386_relocatable_bytes(stddef_output.read_bytes())

    def test_private_source_mutation_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-private-mutation-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "live"
            plan, _source = self._write_tiny_source_root(source_root)
            frozen = freeze_source_inputs(
                source_root, plan, root / "private"
            )
            (frozen.root / "link.ld").write_text(
                "SECTIONS { . = 1; }\n",
                encoding="utf-8",
                newline="\n",
            )

            with self.assertRaisesRegex(
                BootstrapError,
                "^frozen source inputs changed during bootstrap: link.ld$",
            ):
                require_frozen_source_snapshot(frozen, plan)

    def test_symlinked_source_input_is_rejected_before_freeze(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-source-link-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "live"
            plan, source = self._write_tiny_source_root(source_root)
            external = root / "external.cc"
            external.write_text(
                "int tiny(void) { return 7; }\n",
                encoding="utf-8",
                newline="\n",
            )
            source.unlink()
            source.symlink_to(external)

            with self.assertRaisesRegex(
                BootstrapError,
                r"^source input may not be a symlink: .*tiny\.cc$",
            ):
                freeze_source_inputs(
                    source_root, plan, root / "private"
                )

    def test_incomplete_publication_never_exposes_a_partial_stage(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-publication-"
        ) as temporary:
            root = Path(temporary)
            bundle = root / "bundle"
            stage_two = bundle / "stage-two"
            stage_two.mkdir(parents=True)
            (stage_two / "marker").write_text("complete stage two")
            output = root / "published"

            with self.assertRaisesRegex(
                BootstrapError,
                "^bootstrap publication is incomplete: "
                "stage-three, stage-four, behavior, "
                "bootstrap-report.json$",
            ):
                publish_bootstrap_outputs(bundle, output)

            self.assertFalse(output.exists())
            self.assertEqual(
                (stage_two / "marker").read_text(),
                "complete stage two",
            )

    def test_publication_preserves_an_occupied_output(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-publication-"
        ) as temporary:
            root = Path(temporary)
            bundle = root / "bundle"
            for name in (
                "stage-two",
                "stage-three",
                "stage-four",
                "behavior",
            ):
                (bundle / name).mkdir(parents=True)
            (bundle / "bootstrap-report.json").write_text("{}\n")
            output = root / "published"
            output.mkdir()
            sentinel = output / "sentinel.txt"
            sentinel.write_text("keep me")

            with self.assertRaisesRegex(
                BootstrapError,
                "^bootstrap output directory is not empty: sentinel.txt$",
            ):
                publish_bootstrap_outputs(bundle, output)

            self.assertEqual(sentinel.read_text(), "keep me")
            self.assertTrue(bundle.is_dir())

    def test_complete_publication_replaces_an_empty_output(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-publication-"
        ) as temporary:
            root = Path(temporary)
            bundle = root / "bundle"
            for name in (
                "stage-two",
                "stage-three",
                "stage-four",
                "behavior",
            ):
                (bundle / name).mkdir(parents=True)
            (bundle / "bootstrap-report.json").write_text("{}\n")
            output = root / "published"
            output.mkdir()

            publish_bootstrap_outputs(bundle, output)

            self.assertFalse(bundle.exists())
            self.assertEqual(
                {path.name for path in output.iterdir()},
                {
                    "behavior",
                    "bootstrap-report.json",
                    "stage-four",
                    "stage-three",
                    "stage-two",
                },
            )

    def test_failed_directory_replacement_restores_an_empty_output(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-publication-"
        ) as temporary:
            root = Path(temporary)
            bundle = root / "bundle"
            for name in (
                "stage-two",
                "stage-three",
                "stage-four",
                "behavior",
            ):
                (bundle / name).mkdir(parents=True)
            (bundle / "bootstrap-report.json").write_text("{}\n")
            output = root / "published"
            output.mkdir()

            with mock.patch.object(
                Path, "replace", side_effect=OSError("publication blocked")
            ):
                with self.assertRaisesRegex(
                    BootstrapError,
                    "^cannot publish bootstrap output: "
                    "publication blocked$",
                ):
                    publish_bootstrap_outputs(bundle, output)

            self.assertTrue(bundle.is_dir())
            self.assertTrue(output.is_dir())
            self.assertEqual(list(output.iterdir()), [])

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

    def test_candidate_behavior_checks_run_cupidbuild_on_both_targets(self):
        class BehaviorBoundaryReached(Exception):
            pass

        with tempfile.TemporaryDirectory(
            prefix=".candidate-behavior-inventory-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            tool = root / "tool"
            tool.write_bytes(b"tool")
            stage = Stage(
                objects={},
                tools={name: tool for name in CANDIDATE_TOOL_NAMES},
            )
            seed_inputs = SeedInputs(
                manifest={},
                manifest_bytes=b"{}\n",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "manifest.json",
                artifact_bytes=tuple(
                    (name, b"seed") for name in CANDIDATE_TOOL_NAMES
                ),
                tools={
                    name: root / f"{name}.elf"
                    for name in CANDIDATE_TOOL_NAMES
                },
            )
            linux_output = root / "linux"
            linux_output.mkdir()
            windows_output = root / "windows"
            windows_output.mkdir()

            linux_calls: list[tuple[str, tuple[str, ...]]] = []

            def linux_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                arguments = tuple(str(item) for item in stage_two_arguments)
                paired_arguments = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                linux_calls.append((tool_name, arguments))
                if arguments == ("--help",):
                    output = "usage: candidate\n"
                    if tool_name == "cupidobj":
                        output += (
                            "wrap-jpeg disk-template iso-fixture "
                            "profile-manifest\n"
                        )
                    if tool_name == "cupidld":
                        output += "i386pe\n"
                    return subprocess.CompletedProcess([], 0, output, "")
                if arguments[0] == "embed-jpeg":
                    source_name = arguments[arguments.index("--source") + 1]
                    if Path(source_name).name == "progressive.jpg":
                        return subprocess.CompletedProcess(
                            [],
                            1,
                            "",
                            "cupidbuild: checked CupidObj failed\n",
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("--output") + 1]
                        (command_root / output_name).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                if arguments[0] == "run":
                    tool = arguments[arguments.index("--tool") + 1]
                    forwarded = arguments[arguments.index("--") + 1 :]
                    if tool == "cupidobj" and forwarded == (
                        "--definitely-invalid-option",
                    ):
                        return subprocess.CompletedProcess(
                            [], 2, "", "usage: cupidobj\n"
                        )
                    if tool == "cupidc" and forwarded == ("--help",):
                        return subprocess.CompletedProcess(
                            [], 0, "usage: cupidc\n", ""
                        )
                    if (
                        tool == "cupidc"
                        and "/runner-invalid.cc" in forwarded
                    ):
                        return subprocess.CompletedProcess(
                            [], 1, "", "/runner-invalid.cc:1: error\n"
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("-o") + 1]
                        (command_root / output_name.lstrip("/")).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise BehaviorBoundaryReached

            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=linux_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_candidate_image_certification_behavior",
            ), self.assertRaises(BehaviorBoundaryReached):
                _run_behavior_checks(
                    ToolRunner(root),
                    root,
                    linux_output,
                    stage,
                    stage,
                    seed_inputs,
                )

            self.assertEqual(
                linux_calls[: len(CANDIDATE_TOOL_NAMES)],
                [
                    (name, ("--help",))
                    for name in CANDIDATE_TOOL_NAMES
                ],
            )
            linux_runner_root = linux_output / "behavior" / "cupidobj-runner"
            linux_runner_success = linux_calls[len(CANDIDATE_TOOL_NAMES)]
            self.assertEqual(linux_runner_success[0], "cupidbuild")
            self.assertEqual(linux_runner_success[1][0], "run")
            self.assertEqual(
                Path(
                    linux_runner_success[1][
                        linux_runner_success[1].index("--root") + 1
                    ]
                ),
                linux_runner_root,
            )
            self.assertEqual(
                linux_runner_success[1][
                    linux_runner_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "wrap-text",
                    "runner-input.txt",
                    "--identity",
                    "fixed-point-runner.txt",
                    "-o",
                    "stage-three-cupidobj-runner.o",
                ),
            )
            linux_runner_failure = linux_calls[
                len(CANDIDATE_TOOL_NAMES) + 1
            ]
            self.assertEqual(linux_runner_failure[0], "cupidbuild")
            self.assertEqual(
                linux_runner_failure[1][
                    linux_runner_failure[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "--definitely-invalid-option",
                ),
            )
            linux_cupidc_root = linux_output / "behavior" / "cupidc-runner"
            linux_cupidc_help = linux_calls[len(CANDIDATE_TOOL_NAMES) + 2]
            self.assertEqual(
                linux_cupidc_help,
                (
                    "cupidbuild",
                    (
                        "run",
                        "--seed-manifest",
                        str(
                            linux_cupidc_root
                            / "cupidbuild-seed"
                            / "manifest.json"
                        ),
                        "--root",
                        str(linux_cupidc_root),
                        "--tool",
                        "cupidc",
                        "--",
                        "--help",
                    ),
                ),
            )
            linux_cupidc_success = linux_calls[
                len(CANDIDATE_TOOL_NAMES) + 3
            ]
            self.assertEqual(linux_cupidc_success[0], "cupidbuild")
            self.assertEqual(
                linux_cupidc_success[1][
                    linux_cupidc_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidc",
                    "--",
                    "--root",
                    str(linux_cupidc_root),
                    "--freestanding",
                    "-c",
                    "/runner-valid.cc",
                    "-o",
                    "/stage-three-cupidc-runner.o",
                ),
            )
            linux_cupidc_failure = linux_calls[
                len(CANDIDATE_TOOL_NAMES) + 4
            ]
            self.assertIn("/runner-invalid.cc", linux_cupidc_failure[1])
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 5][0],
                "cupidbuild",
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 5][1][0],
                "embed-jpeg",
            )
            linux_jpeg_root = linux_output / "behavior" / "cupidbuild-jpeg"
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 5][1],
                (
                    "embed-jpeg",
                    "--seed-manifest",
                    str(linux_jpeg_root / "cupidbuild-seed" / "manifest.json"),
                    "--root",
                    str(root),
                    "--source",
                    (linux_jpeg_root / "asset.jpg")
                    .relative_to(root)
                    .as_posix(),
                    "--output",
                    (linux_jpeg_root / "stage-three-cupidbuild-jpeg.o")
                    .relative_to(root)
                    .as_posix(),
                ),
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 6][1][0],
                "embed-jpeg",
            )
            self.assertEqual(
                Path(
                    linux_calls[len(CANDIDATE_TOOL_NAMES) + 6][1][
                        linux_calls[len(CANDIDATE_TOOL_NAMES) + 6][1].index(
                            "--source"
                        )
                        + 1
                    ]
                ).name,
                "progressive.jpg",
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 7][0],
                "cupidbuild",
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) + 7][1][0],
                "generate-ksyms",
            )

            windows_calls: list[tuple[str, tuple[str, ...]]] = []

            def windows_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                arguments = tuple(str(item) for item in stage_two_arguments)
                paired_arguments = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                windows_calls.append((tool_name, arguments))
                if arguments == ("--help",):
                    return subprocess.CompletedProcess(
                        [], 0, "usage: candidate\n", ""
                    )
                if arguments == ("--definitely-invalid-option",):
                    return subprocess.CompletedProcess(
                        [], 2, "", "usage: candidate\n"
                    )
                if arguments[0] == "embed-jpeg":
                    source_name = arguments[arguments.index("--source") + 1]
                    if Path(source_name).name == "progressive.jpg":
                        return subprocess.CompletedProcess(
                            [],
                            1,
                            "",
                            "cupidbuild: checked CupidObj failed\n",
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("--output") + 1]
                        (command_root / output_name).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                if arguments[0] == "run":
                    tool = arguments[arguments.index("--tool") + 1]
                    forwarded = arguments[arguments.index("--") + 1 :]
                    if tool == "cupidobj" and forwarded == (
                        "--definitely-invalid-option",
                    ):
                        return subprocess.CompletedProcess(
                            [], 2, "", "usage: cupidobj\n"
                        )
                    if tool == "cupidc" and forwarded == ("--help",):
                        return subprocess.CompletedProcess(
                            [], 0, "usage: cupidc\n", ""
                        )
                    if (
                        tool == "cupidc"
                        and "/runner-invalid.cc" in forwarded
                    ):
                        return subprocess.CompletedProcess(
                            [], 1, "", "/runner-invalid.cc:1: error\n"
                        )
                    for command in (arguments, paired_arguments):
                        command_root = Path(
                            command[command.index("--root") + 1]
                        )
                        output_name = command[command.index("-o") + 1]
                        (command_root / output_name.lstrip("/")).write_bytes(
                            _unowned_relocation_object_payload()
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise BehaviorBoundaryReached

            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=windows_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_candidate_image_certification_behavior",
            ), self.assertRaises(BehaviorBoundaryReached):
                _run_native_windows_behavior_checks(
                    ToolRunner(root),
                    windows_output,
                    stage,
                    stage,
                    {},
                    seed_inputs,
                    seed_inputs,
                )

            expected_windows_calls = []
            for name in CANDIDATE_TOOL_NAMES:
                expected_windows_calls.extend(
                    [
                        (name, ("--help",)),
                        (name, ("--definitely-invalid-option",)),
                    ]
                )
            self.assertEqual(
                windows_calls[: len(expected_windows_calls)],
                expected_windows_calls,
            )
            windows_runner_root = (
                windows_output / "behavior" / "cupidobj-runner"
            )
            windows_runner_success = windows_calls[len(expected_windows_calls)]
            self.assertEqual(windows_runner_success[0], "cupidbuild")
            self.assertEqual(windows_runner_success[1][0], "run")
            self.assertEqual(
                Path(
                    windows_runner_success[1][
                        windows_runner_success[1].index("--root") + 1
                    ]
                ),
                windows_runner_root,
            )
            self.assertEqual(
                windows_runner_success[1][
                    windows_runner_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "wrap-text",
                    "runner-input.txt",
                    "--identity",
                    "fixed-point-runner.txt",
                    "-o",
                    "stage-three-cupidobj-runner.o",
                ),
            )
            windows_runner_failure = windows_calls[
                len(expected_windows_calls) + 1
            ]
            self.assertEqual(windows_runner_failure[0], "cupidbuild")
            self.assertEqual(
                windows_runner_failure[1][
                    windows_runner_failure[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidobj",
                    "--",
                    "--definitely-invalid-option",
                ),
            )
            windows_cupidc_root = (
                windows_output / "behavior" / "cupidc-runner"
            )
            windows_cupidc_help = windows_calls[
                len(expected_windows_calls) + 2
            ]
            self.assertEqual(windows_cupidc_help[0], "cupidbuild")
            self.assertEqual(
                windows_cupidc_help[1][
                    windows_cupidc_help[1].index("--tool") :
                ],
                ("--tool", "cupidc", "--", "--help"),
            )
            windows_cupidc_success = windows_calls[
                len(expected_windows_calls) + 3
            ]
            self.assertEqual(windows_cupidc_success[0], "cupidbuild")
            self.assertEqual(
                windows_cupidc_success[1][
                    windows_cupidc_success[1].index("--tool") :
                ],
                (
                    "--tool",
                    "cupidc",
                    "--",
                    "--root",
                    str(windows_cupidc_root),
                    "--freestanding",
                    "-c",
                    "/runner-valid.cc",
                    "-o",
                    "/stage-three-cupidc-runner.o",
                ),
            )
            windows_cupidc_failure = windows_calls[
                len(expected_windows_calls) + 4
            ]
            self.assertIn("/runner-invalid.cc", windows_cupidc_failure[1])
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 5][0],
                "cupidbuild",
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 5][1][0],
                "embed-jpeg",
            )
            windows_jpeg_root = (
                windows_output / "behavior" / "cupidbuild-jpeg"
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 5][1],
                (
                    "embed-jpeg",
                    "--seed-manifest",
                    str(
                        windows_jpeg_root
                        / "cupidbuild-seed"
                        / "manifest.json"
                    ),
                    "--root",
                    str(windows_output),
                    "--source",
                    (windows_jpeg_root / "asset.jpg")
                    .relative_to(windows_output)
                    .as_posix(),
                    "--output",
                    (windows_jpeg_root / "stage-three-cupidbuild-jpeg.o")
                    .relative_to(windows_output)
                    .as_posix(),
                ),
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 6][1][0],
                "embed-jpeg",
            )
            self.assertEqual(
                Path(
                    windows_calls[len(expected_windows_calls) + 6][1][
                        windows_calls[
                            len(expected_windows_calls) + 6
                        ][1].index("--source")
                        + 1
                    ]
                ).name,
                "progressive.jpg",
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 7][0],
                "cupidbuild",
            )
            self.assertEqual(
                windows_calls[len(expected_windows_calls) + 7][1][0],
                "generate-ksyms",
            )

    def test_candidate_entry_corruption_requires_two_file_backed_pe_bytes(
        self,
    ):
        with self.assertRaisesRegex(
            BootstrapError,
            "^cannot locate file-backed candidate entry: candidate.exe$",
        ):
            _corrupt_candidate_entry_instruction(
                bytes(self._minimal_pe32()), "candidate.exe"
            )

    def test_candidate_behavior_certifies_each_compared_tool_image(self):
        class BehaviorBoundaryReached(Exception):
            pass

        strict_flags = (
            "--require-known",
            "--require-local-targets",
            "--require-code-anchors",
        )
        with tempfile.TemporaryDirectory(
            prefix=".candidate-image-certification-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            linux_tools = {}
            windows_tools = {}
            linux_image = _local_target_executable_payload(5)
            windows_image_buffer = self._minimal_pe32()
            struct.pack_into(
                "<I", windows_image_buffer, 0x98 + 0xE0 + 8, 2
            )
            windows_image = bytes(windows_image_buffer)
            for name in CANDIDATE_TOOL_NAMES:
                linux_path = root / "linux-tools" / f"{name}.elf"
                linux_path.parent.mkdir(exist_ok=True)
                linux_path.write_bytes(linux_image)
                linux_tools[name] = linux_path
                windows_path = root / "windows-tools" / f"{name}.exe"
                windows_path.parent.mkdir(exist_ok=True)
                windows_path.write_bytes(windows_image)
                windows_tools[name] = windows_path
            linux_stage = Stage(objects={}, tools=linux_tools)
            windows_stage = Stage(objects={}, tools=windows_tools)
            seed_inputs = SeedInputs(
                manifest={},
                manifest_bytes=b"{}\n",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "manifest.json",
                artifact_bytes=tuple(
                    (name, b"seed") for name in CANDIDATE_TOOL_NAMES
                ),
                tools={
                    name: root / f"{name}.elf"
                    for name in CANDIDATE_TOOL_NAMES
                },
            )

            linux_calls = []
            linux_strict_timeouts = []

            def linux_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                first = tuple(str(item) for item in stage_two_arguments)
                second = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                linux_calls.append((tool_name, first, second))
                if first == ("--help",):
                    output = "usage: candidate\n"
                    if tool_name == "cupidobj":
                        output += (
                            "wrap-jpeg disk-template iso-fixture "
                            "profile-manifest\n"
                        )
                    if tool_name == "cupidld":
                        output += "i386pe\n"
                    return subprocess.CompletedProcess([], 0, output, "")
                if first[:3] == strict_flags:
                    linux_strict_timeouts.append(_args)
                    if Path(first[-1]).name == "corrupted-cupidbuild.elf":
                        self.assertNotEqual(
                            Path(first[-1]).read_bytes(),
                            linux_tools["cupidbuild"].read_bytes(),
                        )
                        return subprocess.CompletedProcess(
                            [], 1, "", "code check failed\n"
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise AssertionError(f"unexpected Linux call: {first}")

            linux_output = root / "linux"
            linux_output.mkdir()
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=linux_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_cupidbuild_cupidobj_runner_behavior",
                side_effect=BehaviorBoundaryReached,
            ), self.assertRaises(BehaviorBoundaryReached):
                _run_behavior_checks(
                    ToolRunner(root),
                    root,
                    linux_output,
                    linux_stage,
                    linux_stage,
                    seed_inputs,
                )

            expected_linux_certifications = [
                (
                    "cupiddis",
                    (*strict_flags, str(linux_tools[name])),
                    (*strict_flags, str(linux_tools[name])),
                )
                for name in CANDIDATE_TOOL_NAMES
            ]
            corrupted_linux = linux_output / "behavior" / (
                "corrupted-cupidbuild.elf"
            )
            expected_linux_certifications.append(
                (
                    "cupiddis",
                    (*strict_flags, str(corrupted_linux)),
                    (*strict_flags, str(corrupted_linux)),
                )
            )
            self.assertEqual(
                linux_calls[len(CANDIDATE_TOOL_NAMES) :],
                expected_linux_certifications,
            )
            self.assertEqual(
                linux_strict_timeouts,
                [(360,)] * (len(CANDIDATE_TOOL_NAMES) + 1),
            )

            windows_calls = []
            windows_strict_timeouts = []

            def windows_pair(
                _runner,
                _stage_two,
                _stage_three,
                tool_name,
                stage_two_arguments,
                stage_three_arguments=None,
                *_args,
            ):
                first = tuple(str(item) for item in stage_two_arguments)
                second = tuple(
                    str(item)
                    for item in (
                        stage_three_arguments
                        if stage_three_arguments is not None
                        else stage_two_arguments
                    )
                )
                windows_calls.append((tool_name, first, second))
                if first == ("--help",):
                    return subprocess.CompletedProcess(
                        [], 0, "usage: candidate\n", ""
                    )
                if first == ("--definitely-invalid-option",):
                    return subprocess.CompletedProcess(
                        [], 2, "", "usage: candidate\n"
                    )
                if first[:3] == strict_flags:
                    windows_strict_timeouts.append(_args)
                    if Path(first[-1]).name == "corrupted-cupidbuild.exe":
                        self.assertNotEqual(
                            Path(first[-1]).read_bytes(),
                            windows_tools["cupidbuild"].read_bytes(),
                        )
                        return subprocess.CompletedProcess(
                            [], 1, "", "code check failed\n"
                        )
                    return subprocess.CompletedProcess([], 0, "", "")
                raise AssertionError(f"unexpected Windows call: {first}")

            windows_output = root / "windows"
            windows_output.mkdir()
            with mock.patch(
                "tools.bootstrap_toolchain._run_stage_pair",
                side_effect=windows_pair,
            ), mock.patch(
                "tools.bootstrap_toolchain."
                "_check_cupidbuild_cupidobj_runner_behavior",
                side_effect=BehaviorBoundaryReached,
            ), self.assertRaises(BehaviorBoundaryReached):
                _run_native_windows_behavior_checks(
                    ToolRunner(root),
                    windows_output,
                    windows_stage,
                    windows_stage,
                    {},
                    seed_inputs,
                    seed_inputs,
                )

            expected_windows_certifications = [
                (
                    "cupiddis",
                    (*strict_flags, str(windows_tools[name])),
                    (*strict_flags, str(windows_tools[name])),
                )
                for name in CANDIDATE_TOOL_NAMES
            ]
            corrupted_windows = windows_output / "behavior" / (
                "corrupted-cupidbuild.exe"
            )
            expected_windows_certifications.append(
                (
                    "cupiddis",
                    (*strict_flags, str(corrupted_windows)),
                    (*strict_flags, str(corrupted_windows)),
                )
            )
            self.assertEqual(
                windows_calls[len(CANDIDATE_TOOL_NAMES) * 2 :],
                expected_windows_certifications,
            )
            self.assertEqual(
                windows_strict_timeouts,
                [(360,)] * (len(CANDIDATE_TOOL_NAMES) + 1),
            )

    def test_manifest_author_uses_the_six_tool_candidate_plan(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-bootstrap-retained-seed-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            removed_seed = root / "removed-seed"
            removed_seed.mkdir()
            seed_tools = {
                name: removed_seed / f"{name}.elf" for name in TOOL_NAMES
            }
            for path in seed_tools.values():
                path.write_bytes(b"retained seed image")
            checked_plan = json.loads(
                SEED_MANIFEST.read_text(encoding="utf-8")
            )["build_plan"]
            seed_inputs = SeedInputs(
                manifest={
                    "build_plan": checked_plan,
                    "build_plan_sha256": _build_plan_sha256(checked_plan),
                    "provenance": {
                        "source_revision": SEED_SOURCE_REVISION
                    },
                },
                manifest_bytes=b"{}",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "manifest.json",
                artifact_bytes=tuple(
                    (name, b"retained seed image") for name in TOOL_NAMES
                ),
                tools=seed_tools,
            )
            shutil.rmtree(removed_seed)
            observed_plans: list[dict[str, object]] = []
            observed_producers: list[dict[str, Path]] = []

            def freeze_sources(_root, _plan, destination):
                destination.mkdir()
                return mock.Mock(root=destination, inventory={})

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                observed_producers.append(dict(_producers))
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.elf"
                    tool.write_bytes(
                        b"retained seed image"
                        if name in TOOL_NAMES
                        else b"candidate CupidBuild image"
                    )
                    tools[name] = tool
                return Stage(objects={}, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _behavior_root,
                _stage_three,
                _stage_four,
                _seed_inputs,
                evidence,
            ):
                (private_source_root / "behavior").mkdir()
                evidence["windows_runtime"] = {
                    "artifacts": {},
                    "cupiddis": {"loader": {}},
                    "loader": {},
                    "native_tools": {
                        name: {"loader": {}}
                        for name in (
                            "cupidasm",
                            "cupidc",
                            "cupidld",
                            "cupidobj",
                        )
                    },
                    "runtime_contract": {"loader": {}},
                }
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._compare_stages",
                    return_value={"all_equal": True},
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._run_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_from_frozen_seed(
                    seed_inputs,
                    source_root,
                    source_root / "published",
                    compare_fixed_point=False,
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in TOOL_NAMES},
            )
            self.assertEqual(len(observed_plans), 3)
            self.assertEqual(
                observed_producers[0],
                {
                    name: seed_tools[name]
                    for name in (*PRODUCER_NAMES, "cupiddis")
                },
            )
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
                self.assertEqual(len(plan["sources"]), 22)
            self.assertEqual(
                report["build_plan_sha256"], _build_plan_sha256(checked_plan)
            )
            self.assertEqual(
                report["candidate_build_plan"], observed_plans[0]
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                _build_plan_sha256(observed_plans[0]),
            )
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(report["status"], "pending-fixed-point-author")
            self.assertEqual(
                report["seed_source_revision"], SEED_SOURCE_REVISION
            )
            self.assertNotIn("comparisons", report)
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )

    def test_promoted_linux_report_compares_all_six_seed_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-promoted-linux-report-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            seed_root = root / "seed"
            seed_root.mkdir()
            payloads = {
                name: f"promoted Linux {name} image".encode("ascii")
                for name in CANDIDATE_TOOL_NAMES
            }
            seed_tools = {
                name: seed_root / f"{name}.elf"
                for name in CANDIDATE_TOOL_NAMES
            }
            for name, path in seed_tools.items():
                path.write_bytes(payloads[name])

            manifest = self._promote_manifest_fixture(
                json.loads(SEED_MANIFEST.read_text(encoding="utf-8")),
                False,
            )
            for artifact in manifest["artifacts"]:
                payload = payloads[artifact["name"]]
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            manifest_bytes = (
                json.dumps(manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            (seed_root / "manifest.json").write_bytes(manifest_bytes)
            seed_inputs = SeedInputs(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
                live_manifest_path=seed_root / "manifest.json",
                artifact_bytes=tuple(
                    (name, payloads[name]) for name in CANDIDATE_TOOL_NAMES
                ),
                tools=seed_tools,
            )
            observed_plans: list[dict[str, object]] = []

            def freeze_sources(_root, _plan, destination):
                destination.mkdir()
                return mock.Mock(root=destination, inventory={})

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.elf"
                    tool.write_bytes(payloads[name])
                    tools[name] = tool
                return Stage(objects={}, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _behavior_root,
                _stage_three,
                _stage_four,
                _seed_inputs,
                evidence,
            ):
                (private_source_root / "behavior").mkdir()
                evidence["windows_runtime"] = {
                    "artifacts": {},
                    "cupiddis": {"loader": {}},
                    "loader": {},
                    "native_tools": {
                        name: {"loader": {}}
                        for name in (
                            "cupidasm",
                            "cupidc",
                            "cupidld",
                            "cupidobj",
                        )
                    },
                    "runtime_contract": {"loader": {}},
                }
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._compare_stages",
                    return_value={"all_equal": True},
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._run_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_from_frozen_seed(
                    seed_inputs,
                    source_root,
                    source_root / "published",
                    compare_fixed_point=True,
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in CANDIDATE_TOOL_NAMES},
            )
            self.assertEqual(
                report["seed_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["build_plan_sha256"], PROMOTED_LINUX_PLAN_SHA256
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                PROMOTED_LINUX_PLAN_SHA256,
            )
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(report["comparisons"], {"all_equal": True})
            self.assertEqual(report["status"], "pass")
            self.assertEqual(len(observed_plans), 3)
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )

    def test_windows_report_compares_stage_two_with_retained_seed_bytes(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-windows-retained-seed-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            removed_seed = root / "removed-seed"
            removed_seed.mkdir()
            seed_tools = {
                name: removed_seed / f"{name}.exe" for name in TOOL_NAMES
            }
            for path in seed_tools.values():
                path.write_bytes(b"retained Windows seed image")
            plan_compiler = removed_seed / "cupidc.elf"
            plan_compiler.write_bytes(b"retained Linux plan compiler")
            seed_inputs = SeedInputs(
                manifest={
                    "provenance": {
                        "source_revision": SEED_SOURCE_REVISION
                    }
                },
                manifest_bytes=b"{}",
                manifest_sha256="1" * 64,
                live_manifest_path=root / "windows-manifest.json",
                artifact_bytes=tuple(
                    (name, b"retained Windows seed image")
                    for name in TOOL_NAMES
                ),
                tools=seed_tools,
            )
            checked_plan = json.loads(
                SEED_MANIFEST.read_text(encoding="utf-8")
            )["build_plan"]
            plan_inputs = SeedInputs(
                manifest={
                    "build_plan": checked_plan,
                    "provenance": {
                        "source_revision": SEED_SOURCE_REVISION
                    },
                },
                manifest_bytes=b"{}",
                manifest_sha256="2" * 64,
                live_manifest_path=root / "linux-manifest.json",
                artifact_bytes=(
                    ("cupidc", b"retained Linux plan compiler"),
                ),
                tools={"cupidc": plan_compiler},
            )
            shutil.rmtree(removed_seed)
            observed_plans: list[dict[str, object]] = []
            observed_producers: list[dict[str, Path]] = []

            def freeze_sources(_root, _plan, destination):
                destination.mkdir()
                return mock.Mock(root=destination, inventory={})

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                observed_producers.append(dict(_producers))
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.exe"
                    tool.write_bytes(
                        b"retained Windows seed image"
                        if name in TOOL_NAMES
                        else b"candidate Windows CupidBuild image"
                    )
                    tools[name] = tool
                return Stage(objects={}, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _stage_three,
                _stage_four,
                _native_plan,
                _seed_inputs,
                _linux_seed_inputs,
            ):
                (private_source_root / "behavior").mkdir()
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_windows_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._compare_windows_stages",
                    return_value={"all_equal": True},
                ),
                mock.patch(
                    "tools.bootstrap_toolchain."
                    "_run_native_windows_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_windows_from_frozen_seed(
                    seed_inputs,
                    plan_inputs,
                    source_root,
                    source_root / "published",
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in TOOL_NAMES},
            )
            self.assertEqual(len(observed_plans), 3)
            self.assertEqual(
                observed_producers[0],
                {
                    name: seed_tools[name]
                    for name in (*PRODUCER_NAMES, "cupiddis")
                },
            )
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
                self.assertEqual(len(plan["sources"]), 23)
                self.assertEqual(len(plan["assembly_sources"]), 3)
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(
                report["seed_source_revision"], SEED_SOURCE_REVISION
            )
            self.assertEqual(
                report["plan_source_revision"], SEED_SOURCE_REVISION
            )
            self.assertEqual(
                report["stages"]["stage-two"]["producer_generation"],
                "checked-windows-execution-seed",
            )
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )

    def test_promoted_windows_report_compares_all_six_seed_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupid-promoted-windows-report-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            (source_root / "toolchain").mkdir(parents=True)
            execution_root = root / "execution-seed"
            plan_root = root / "plan-seed"
            execution_root.mkdir()
            plan_root.mkdir()
            execution_payloads = {
                name: f"promoted Windows {name} image".encode("ascii")
                for name in CANDIDATE_TOOL_NAMES
            }
            plan_payloads = {
                name: f"promoted Linux {name} image".encode("ascii")
                for name in CANDIDATE_TOOL_NAMES
            }
            execution_tools = {
                name: execution_root / f"{name}.exe"
                for name in CANDIDATE_TOOL_NAMES
            }
            plan_tools = {
                name: plan_root / f"{name}.elf"
                for name in CANDIDATE_TOOL_NAMES
            }
            for name in CANDIDATE_TOOL_NAMES:
                execution_tools[name].write_bytes(execution_payloads[name])
                plan_tools[name].write_bytes(plan_payloads[name])

            execution_manifest = self._promote_manifest_fixture(
                json.loads(
                    WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
                ),
                True,
            )
            plan_manifest = self._promote_manifest_fixture(
                json.loads(SEED_MANIFEST.read_text(encoding="utf-8")),
                False,
            )
            for artifact in execution_manifest["artifacts"]:
                payload = execution_payloads[artifact["name"]]
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            for artifact in plan_manifest["artifacts"]:
                payload = plan_payloads[artifact["name"]]
                artifact["size"] = len(payload)
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
            plan_manifest_bytes = (
                json.dumps(plan_manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            plan_manifest_sha256 = hashlib.sha256(
                plan_manifest_bytes
            ).hexdigest()
            execution_manifest["provenance"][
                "plan_seed_manifest_sha256"
            ] = plan_manifest_sha256
            execution_manifest_bytes = (
                json.dumps(execution_manifest, indent=2, sort_keys=True)
                + "\n"
            ).encode("ascii")
            (execution_root / "manifest.json").write_bytes(
                execution_manifest_bytes
            )
            (plan_root / "manifest.json").write_bytes(plan_manifest_bytes)
            seed_inputs = SeedInputs(
                manifest=execution_manifest,
                manifest_bytes=execution_manifest_bytes,
                manifest_sha256=hashlib.sha256(
                    execution_manifest_bytes
                ).hexdigest(),
                live_manifest_path=execution_root / "manifest.json",
                artifact_bytes=tuple(
                    (name, execution_payloads[name])
                    for name in CANDIDATE_TOOL_NAMES
                ),
                tools=execution_tools,
            )
            plan_inputs = SeedInputs(
                manifest=plan_manifest,
                manifest_bytes=plan_manifest_bytes,
                manifest_sha256=hashlib.sha256(
                    plan_manifest_bytes
                ).hexdigest(),
                live_manifest_path=plan_root / "manifest.json",
                artifact_bytes=tuple(
                    (name, plan_payloads[name])
                    for name in CANDIDATE_TOOL_NAMES
                ),
                tools=plan_tools,
            )
            observed_plans: list[dict[str, object]] = []

            def freeze_sources(_root, _plan, destination):
                destination.mkdir()
                return mock.Mock(root=destination, inventory={})

            def build_stage(
                _runner,
                _source_root,
                stage_directory,
                _producers,
                plan,
                _stage_name,
            ):
                stage_directory.mkdir()
                observed_plans.append(plan)
                objects = {}
                for source in (
                    *plan["sources"],
                    *plan["assembly_sources"],
                ):
                    name = source["name"]
                    object_path = stage_directory / f"{name}.o"
                    object_path.write_bytes(
                        f"converged {name} object".encode("ascii")
                    )
                    objects[name] = object_path
                tools = {}
                for name in CANDIDATE_TOOL_NAMES:
                    tool = stage_directory / f"{name}.exe"
                    tool.write_bytes(execution_payloads[name])
                    tools[name] = tool
                return Stage(objects=objects, tools=tools)

            def run_behavior(
                _runner,
                private_source_root,
                _stage_three,
                _stage_four,
                _native_plan,
                _seed_inputs,
                _linux_seed_inputs,
            ):
                (private_source_root / "behavior").mkdir()
                return {"success_cases": 0}

            with (
                mock.patch(
                    "tools.bootstrap_toolchain.freeze_source_inputs",
                    side_effect=freeze_sources,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_source_closures"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.require_live_seed_inputs"
                ),
                mock.patch(
                    "tools.bootstrap_toolchain._build_windows_stage",
                    side_effect=build_stage,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain."
                    "_run_native_windows_behavior_checks",
                    side_effect=run_behavior,
                ),
                mock.patch(
                    "tools.bootstrap_toolchain.publish_bootstrap_outputs"
                ),
            ):
                report = _bootstrap_windows_from_frozen_seed(
                    seed_inputs,
                    plan_inputs,
                    source_root,
                    source_root / "published",
                )

            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in CANDIDATE_TOOL_NAMES},
            )
            self.assertEqual(
                report["seed_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["plan_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["candidate_build_plan_sha256"],
                PROMOTED_WINDOWS_PLAN_SHA256,
            )
            self.assertEqual(
                report["candidate_tools"], list(CANDIDATE_TOOL_NAMES)
            )
            self.assertEqual(
                report["comparisons"],
                {
                    "all_equal": True,
                    "assembly_objects": 3,
                    "c_objects": 23,
                    "compared_generations": [
                        "stage-three",
                        "stage-four",
                    ],
                    "tool_images": 6,
                },
            )
            self.assertEqual(report["status"], "pass")
            self.assertEqual(len(observed_plans), 3)
            for plan in observed_plans:
                self.assertEqual(set(plan["links"]), set(CANDIDATE_TOOL_NAMES))
            for stage in report["stages"].values():
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
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

    def test_recomputed_digest_cannot_change_the_source_plan(self):
        original = self._legacy_linux_manifest_fixture(
            json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))
        )
        mutations = {
            "substituted source": lambda plan: plan["sources"][1].update(
                {"path": "/toolchain/elf32.cc"}
            ),
            "traversal source": lambda plan: plan["sources"][1].update(
                {"path": "/toolchain/../toolchain/ctool.cc"}
            ),
            "duplicate source path": lambda plan: plan["sources"][1].update(
                {"path": plan["sources"][3]["path"]}
            ),
            "unknown source key": lambda plan: plan["sources"][1].update(
                {"unrecorded": True}
            ),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="cupid-bootstrap-plan-"
            ) as temporary:
                manifest = json.loads(json.dumps(original))
                mutate(manifest["build_plan"])
                encoded_plan = json.dumps(
                    manifest["build_plan"],
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=True,
                ).encode("ascii")
                manifest["build_plan_sha256"] = hashlib.sha256(
                    encoded_plan
                ).hexdigest()
                manifest_path = Path(temporary) / "manifest.json"
                manifest_path.write_text(
                    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )

                result = subprocess.run(
                    [
                        sys.executable,
                        str(BOOTSTRAP_TOOL),
                        "verify",
                        "--manifest",
                        str(manifest_path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertRegex(
                result.stderr,
                "^bootstrap seed verification failed: "
                "(build plan sources differ|build source ctool keys differ)"
                "\n$",
            )

    def test_manifest_schema_rejects_unknown_duplicate_or_wrong_types(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-schema-"
        ) as temporary:
            original_text = SEED_MANIFEST.read_text(encoding="utf-8")
            cases = []

            manifest = json.loads(original_text)
            manifest["unrecorded"] = True
            cases.append(("unknown key", manifest, "manifest keys differ"))

            manifest = json.loads(original_text)
            manifest["build_plan"]["workers"] = 2.0
            cases.append(
                (
                    "floating worker count",
                    manifest,
                    "build plan workers type differs",
                )
            )

            manifest = json.loads(original_text)
            manifest["target"]["elf_class"] = 32.0
            cases.append(
                (
                    "floating target integer",
                    manifest,
                    "manifest target field type differs: elf_class",
                )
            )

            manifest = json.loads(original_text)
            manifest["artifacts"][0]["producer"] = 1
            cases.append(
                (
                    "integer producer flag",
                    manifest,
                    "artifact producer role type is invalid: cupidasm",
                )
            )

            manifest_path = Path(temporary) / "manifest.json"
            for label, manifest, expected in cases:
                with self.subTest(label=label):
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                        newline="\n",
                    )
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(BOOTSTRAP_TOOL),
                            "verify",
                            "--manifest",
                            str(manifest_path),
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(
                        result.stderr,
                        f"bootstrap seed verification failed: {expected}\n",
                    )

            manifest_path.write_text(
                '{"schema":"wrong",' + original_text.lstrip()[1:],
                encoding="utf-8",
                newline="\n",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                result.stderr,
                "bootstrap seed verification failed: "
                "manifest contains duplicate JSON key: schema\n",
            )

    def test_frozen_seed_is_independent_of_later_input_changes(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-freeze-"
        ) as temporary:
            root = Path(temporary)
            copied_seed = root / "i386-linux"
            frozen_directory = root / "frozen"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            original_manifest = (copied_seed / "manifest.json").read_bytes()
            frozen = freeze_seed_inputs(
                copied_seed / "manifest.json", frozen_directory
            )

            (copied_seed / "manifest.json").write_text("{}\n")
            compiler = copied_seed / "cupidc.elf"
            compiler.write_bytes(b"changed after verification")

            self.assertEqual(
                frozen.manifest_sha256,
                hashlib.sha256(original_manifest).hexdigest(),
            )
            self.assertEqual(
                frozen.manifest["build_plan_sha256"],
                PROMOTED_LINUX_PLAN_SHA256,
            )
            self.assertNotEqual(
                frozen.tools["cupidc"].read_bytes(),
                compiler.read_bytes(),
            )
            self.assertEqual(
                hashlib.sha256(
                    frozen.tools["cupidc"].read_bytes()
                ).hexdigest(),
                "e50758041199044e269e6b6dae52065cc2de2153efeb13b6b6983279ee2935c0",
            )

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

    def test_checked_i386_linux_seed_verifies(self):
        self.assertEqual(
            hashlib.sha256(SEED_MANIFEST.read_bytes()).hexdigest(),
            PROMOTED_LINUX_MANIFEST_SHA256,
        )
        result = subprocess.run(
            [
                sys.executable,
                str(BOOTSTRAP_TOOL),
                "verify",
                "--manifest",
                str(SEED_MANIFEST),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "checked i386 Linux seed: ok (6 tools)\n",
        )
        self.assertEqual(result.stderr, "")

    def test_checked_i386_linux_seed_carries_local_relative_target_policy(
        self,
    ):
        self._assert_checked_seed_local_relative_target_policy(SEED_MANIFEST)

    def test_checked_i386_linux_seed_carries_relocatable_local_target_policy(
        self,
    ):
        self._assert_checked_seed_relocatable_local_target_policy(
            SEED_MANIFEST
        )

    def test_checked_i386_linux_seed_carries_linked_local_target_policy(self):
        self._assert_checked_seed_linked_local_target_policy(SEED_MANIFEST)

    def test_checked_i386_linux_seed_carries_code_anchor_policy(self):
        self._assert_checked_seed_code_anchor_policy(SEED_MANIFEST)

    def test_checked_i386_linux_seed_snapshot_matches_its_named_commit(self):
        manifest = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))
        provenance = manifest["provenance"]
        revision = provenance["source_revision"]
        plan = manifest["build_plan"]
        live_inventory = capture_source_snapshot(REPO_ROOT, plan)
        committed_inventory = self._committed_source_inventory(
            revision, live_inventory
        )

        encoded = json.dumps(
            committed_inventory,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
        ).encode("ascii")
        self.assertEqual(
            len(committed_inventory), provenance["source_input_count"]
        )
        self.assertEqual(
            hashlib.sha256(encoded).hexdigest(),
            provenance["source_snapshot_sha256"],
        )

    def test_checked_i386_windows_seed_verifies(self):
        self.assertEqual(
            hashlib.sha256(WINDOWS_SEED_MANIFEST.read_bytes()).hexdigest(),
            PROMOTED_WINDOWS_MANIFEST_SHA256,
        )
        result = subprocess.run(
            [
                sys.executable,
                str(BOOTSTRAP_TOOL),
                "verify",
                "--manifest",
                str(WINDOWS_SEED_MANIFEST),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "checked i386 Windows seed: ok (6 tools)\n",
        )
        self.assertEqual(result.stderr, "")

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_runs_without_wsl(self):
        with mock.patch(
            "tools.bootstrap_toolchain.shutil.which",
            side_effect=AssertionError("native seed must not probe WSL"),
        ):
            result = run_seed_tool(
                WINDOWS_SEED_MANIFEST,
                REPO_ROOT,
                "cupiddis",
                ("--help",),
                timeout=60,
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("usage:", result.stdout.casefold())
        self.assertIn("cupiddis", result.stdout.casefold())
        self.assertEqual(result.stderr, "")

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_local_relative_target_policy(
        self,
    ):
        self._assert_checked_seed_local_relative_target_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_relocatable_local_target_policy(
        self,
    ):
        self._assert_checked_seed_relocatable_local_target_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_linked_local_target_policy(self):
        self._assert_checked_seed_linked_local_target_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_carries_code_anchor_policy(self):
        self._assert_checked_seed_code_anchor_policy(
            WINDOWS_SEED_MANIFEST,
            native_windows=True,
        )

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_seed_runs_all_tool_boundaries(self):
        with mock.patch(
            "tools.bootstrap_toolchain.shutil.which",
            side_effect=AssertionError("native seed must not probe WSL"),
        ):
            for tool in (
                "cupidasm",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidc",
            ):
                with self.subTest(tool=tool, case="help"):
                    result = run_seed_tool(
                        WINDOWS_SEED_MANIFEST,
                        REPO_ROOT,
                        tool,
                        ("--help",),
                        timeout=60,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("usage:", result.stdout.casefold())
                    self.assertEqual(result.stderr, "")
                with self.subTest(tool=tool, case="failure"):
                    result = run_seed_tool(
                        WINDOWS_SEED_MANIFEST,
                        REPO_ROOT,
                        tool,
                        ("--definitely-invalid-option",),
                        timeout=60,
                    )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("usage:", result.stderr.casefold())

    @unittest.skipUnless(os.name == "nt", "native Windows seed")
    def test_checked_i386_windows_cupidc_compiles_large_frame_source(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-windows-large-frame-", dir=REPO_ROOT
        ) as temporary:
            outputs = {
                "windows": Path(temporary) / "keyboard-windows.o",
                "linux": Path(temporary) / "keyboard-linux.o",
            }
            manifests = {
                "windows": WINDOWS_SEED_MANIFEST,
                "linux": SEED_MANIFEST,
            }
            for platform, manifest in manifests.items():
                with self.subTest(platform=platform):
                    output = outputs[platform]
                    logical_output = (
                        "/" + output.relative_to(REPO_ROOT).as_posix()
                    )
                    output.write_bytes(b"previous object")
                    result = run_seed_tool(
                        manifest,
                        REPO_ROOT,
                        "cupidc",
                        (
                            "-c",
                            "/drivers/keyboard.cc",
                            "-o",
                            logical_output,
                            *KERNEL_I386_ARGUMENTS,
                            "--root",
                            REPO_ROOT,
                        ),
                        timeout=120,
                    )

                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertNotEqual(
                        output.read_bytes(), b"previous object"
                    )
                    validate_i386_relocatable_bytes(output.read_bytes())

            self.assertEqual(
                outputs["windows"].read_bytes(),
                outputs["linux"].read_bytes(),
            )

    def test_checked_i386_windows_seed_rejects_unlisted_executable(self):
        for extra_name in ("unlisted.exe", "unlisted.EXE"):
            with self.subTest(
                extra_name=extra_name
            ), tempfile.TemporaryDirectory(
                prefix="cupid-bootstrap-windows-seed-"
            ) as temporary:
                copied_seed = Path(temporary) / "i386-windows"
                shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
                (copied_seed / extra_name).write_bytes(b"MZ")

                with self.assertRaisesRegex(
                    BootstrapError,
                    "seed directory contains an unlisted PE32 file",
                ):
                    verify_seed_inputs(copied_seed / "manifest.json")

    def test_checked_i386_windows_seed_binds_execution_provenance(self):
        original = json.loads(
            WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
        )
        cases = (
            (
                "unknown manifest field",
                lambda manifest: manifest.update({"unrecorded": True}),
                "manifest keys differ",
            ),
            (
                "wrong artifact generation",
                lambda manifest: manifest["provenance"].update(
                    {"artifact_generation": "stage-three"}
                ),
                "Windows seed generation differs",
            ),
            (
                "wrong fixed-point command",
                lambda manifest: manifest["provenance"].update(
                    {"fixed_point_command": "make bootstrap-from-seed"}
                ),
                "fixed-point command differs",
            ),
            (
                "failed fixed-point result",
                lambda manifest: manifest["provenance"].update(
                    {"fixed_point_result": "failed"}
                ),
                "seed lacks passing fixed-point provenance",
            ),
            (
                "wrong source snapshot",
                lambda manifest: manifest["provenance"].update(
                    {"source_snapshot_sha256": "0" * 64}
                ),
                "source snapshot differs",
            ),
            (
                "wrong source revision",
                lambda manifest: manifest["provenance"].update(
                    {"source_revision": "0" * 40}
                ),
                "source revision differs",
            ),
            (
                "wrong source count",
                lambda manifest: manifest["provenance"].update(
                    {"source_input_count": 49}
                ),
                "source input count differs",
            ),
            (
                "floating source count",
                lambda manifest: manifest["provenance"].update(
                    {"source_input_count": 50.0}
                ),
                "source input count differs",
            ),
            (
                "wrong parent execution seed",
                lambda manifest: manifest["provenance"].update(
                    {"parent_execution_seed_manifest_sha256": "0" * 64}
                ),
                "parent execution seed manifest differs",
            ),
            (
                "wrong parent execution source revision",
                lambda manifest: manifest["provenance"].update(
                    {"parent_execution_seed_source_revision": "0" * 40}
                ),
                "parent execution seed revision differs",
            ),
            (
                "wrong producer lineage",
                lambda manifest: manifest["provenance"][
                    "producer_lineage"
                ].update({"c": "unreviewed compiler"}),
                "producer lineage differs",
            ),
            (
                "wrong target type",
                lambda manifest: manifest["target"].update(
                    {"pe_class": 32.0}
                ),
                "manifest target field type differs: pe_class",
            ),
            (
                "wrong producer role",
                lambda manifest: manifest["artifacts"][0].update(
                    {"producer": False}
                ),
                "artifact producer role differs: cupidasm",
            ),
        )
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-provenance-"
        ) as temporary:
            copied_seed = Path(temporary) / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            for label, mutate, expected in cases:
                with self.subTest(label=label):
                    manifest = json.loads(json.dumps(original))
                    mutate(manifest)
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                        newline="\n",
                    )
                    with self.assertRaisesRegex(
                        BootstrapError, f"^{expected}$"
                    ):
                        verify_seed_inputs(manifest_path)

    def test_legacy_windows_seed_accepts_native_producer_provenance(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-stage-four-"
        ) as temporary:
            copied_seed = Path(temporary) / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(
                manifest_path.read_text(encoding="utf-8")
            )
            manifest["schema"] = WINDOWS_SEED_SCHEMA
            manifest["artifacts"] = [
                artifact
                for artifact in manifest["artifacts"]
                if artifact["name"] != "cupidbuild"
            ]
            (copied_seed / "cupidbuild.exe").unlink()
            legacy_assembler = (copied_seed / "cupiddis.exe").read_bytes()
            (copied_seed / "cupidasm.exe").write_bytes(legacy_assembler)
            assembler_artifact = next(
                artifact
                for artifact in manifest["artifacts"]
                if artifact["name"] == "cupidasm"
            )
            assembler_artifact["size"] = len(legacy_assembler)
            assembler_artifact["sha256"] = hashlib.sha256(
                legacy_assembler
            ).hexdigest()
            manifest["provenance"] = {
                "artifact_generation": (
                    "paired-stage-four-native-windows"
                ),
                "fixed_point_command": (
                    "make bootstrap-windows-from-seed"
                ),
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde"
                    "39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_seed_source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "producer_lineage": {
                    "assembly": (
                        "native stage-three CupidASM from the checked "
                        "i386 Windows bootstrap"
                    ),
                    "c": (
                        "native stage-three CupidC from the checked "
                        "i386 Windows bootstrap"
                    ),
                    "link": (
                        "native stage-three CupidLD from the checked "
                        "i386 Windows bootstrap"
                    ),
                },
                "source_input_count": 50,
                "source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "source_snapshot_sha256": (
                    WINDOWS_SEED_SOURCE_SNAPSHOT_SHA256
                ),
            }
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            verified = verify_seed_inputs(manifest_path)

            self.assertEqual(set(verified.tools), set(TOOL_NAMES))

    def test_checked_i386_windows_seed_snapshot_matches_its_named_commit(self):
        manifest = json.loads(
            WINDOWS_SEED_MANIFEST.read_text(encoding="utf-8")
        )
        provenance = manifest["provenance"]
        revision = provenance["source_revision"]
        plan = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))[
            "build_plan"
        ]
        live_inventory = capture_source_snapshot(REPO_ROOT, plan)
        committed_inventory = self._committed_source_inventory(
            revision, live_inventory
        )

        encoded = json.dumps(
            committed_inventory,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
        ).encode("ascii")
        self.assertEqual(
            len(committed_inventory), provenance["source_input_count"]
        )
        self.assertEqual(
            hashlib.sha256(encoded).hexdigest(),
            provenance["source_snapshot_sha256"],
        )

    def test_windows_execution_seed_cannot_drive_the_fixed_point(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-windows-role-"
        ) as temporary:
            output = Path(temporary) / "published"
            with self.assertRaisesRegex(
                BootstrapError,
                "^the native Windows execution seed cannot drive the "
                "Linux fixed-point bootstrap$",
            ):
                bootstrap_from_seed(
                    WINDOWS_SEED_MANIFEST, REPO_ROOT, output
                )
            self.assertFalse(output.exists())

    def test_native_windows_plan_is_derived_from_the_linux_plan(self):
        linux_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        linux_plan["include_arguments"] = [
            *linux_plan["include_arguments"],
            "-I",
            "/toolchain/plan-owned-include",
        ]
        linux_plan["sources"].append(
            {
                "gnu_extensions": False,
                "name": "plan_owned_source",
                "path": "/toolchain/plan_owned_source.cc",
            }
        )
        linux_plan["links"]["cupiddis"].insert(
            -1, "plan_owned_source"
        )

        native_plan = _windows_build_plan(linux_plan)

        self.assertEqual(
            native_plan["include_arguments"],
            linux_plan["include_arguments"],
        )
        self.assertEqual(
            [source["name"] for source in native_plan["sources"]],
            [source["name"] for source in linux_plan["sources"]]
            + ["publication_runtime"],
        )
        native_sources = {
            source["name"]: source
            for source in native_plan["sources"]
        }
        for linux_source in linux_plan["sources"]:
            expected_path = linux_source["path"]
            expected_gnu = linux_source["gnu_extensions"]
            if linux_source["name"] == "runtime":
                expected_path = (
                    "/toolchain/hosted/i386-windows/runtime.cc"
                )
                expected_gnu = True
            self.assertEqual(
                native_sources[linux_source["name"]]["path"],
                expected_path,
            )
            self.assertEqual(
                native_sources[linux_source["name"]][
                    "gnu_extensions"
                ],
                expected_gnu,
            )
        for tool_name in TOOL_NAMES:
            if tool_name in ("cupidasm", "cupidld"):
                continue
            self.assertEqual(
                native_plan["links"][tool_name],
                linux_plan["links"][tool_name],
            )
        self.assertEqual(
            native_plan["links"]["cupidasm"],
            [
                "start",
                "publication_start",
                *linux_plan["links"]["cupidasm"][1:-1],
                "publication_runtime",
                "runtime",
            ],
        )
        self.assertEqual(
            native_plan["links"]["cupidld"],
            [
                "start",
                "publication_start",
                *linux_plan["links"]["cupidld"][1:-1],
                "publication_runtime",
                "runtime",
            ],
        )
        cupidasm_imports = {
            imported["procedure"]
            for imported in native_plan["imports"]["cupidasm"]
        }
        self.assertTrue(
            {
                "DeleteFileA",
                "FlushFileBuffers",
                "GetFullPathNameA",
                "MoveFileExA",
            }.issubset(cupidasm_imports)
        )
        cupiddis_main = next(
            source
            for source in native_plan["sources"]
            if source["name"] == "cupiddis_main"
        )
        self.assertEqual(cupiddis_main["definitions"], ["_WIN32=1"])

    def test_native_windows_relink_uses_the_planned_cupidasm_imports(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_native_windows_behavior_checks"
        )
        validators = [
            node
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_validate_static_i386_pe32"
        ]
        self.assertEqual(len(validators), 1)
        expected_imports = validators[0].args[2]
        self.assertIsInstance(expected_imports, ast.Call)
        self.assertIsInstance(expected_imports.func, ast.Name)
        self.assertEqual(expected_imports.func.id, "_windows_imports")
        self.assertEqual(
            [argument.value for argument in expected_imports.args],
            ["cupidasm"],
        )

    def test_linux_behavior_windows_cupidasm_uses_publication_closure(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_behavior_checks"
        )

        def assigned_dict(name):
            assignment = next(
                node
                for node in behavior.body
                if isinstance(node, ast.Assign)
                and any(
                    isinstance(target, ast.Name) and target.id == name
                    for target in node.targets
                )
            )
            return {
                key.value: value
                for key, value in zip(
                    assignment.value.keys, assignment.value.values
                )
            }

        plans = assigned_dict("windows_native_tool_plans")
        self.assertEqual(
            [item.value for item in plans["cupidasm"].elts],
            [
                "publication_start",
                "cupidasm_main",
                "cupidasm",
                "ctool_host",
                "ctool",
                "elf32",
                "x86",
                "publication_runtime",
            ],
        )
        imports = assigned_dict("windows_native_tool_imports")
        self.assertIsInstance(imports["cupidasm"], ast.Name)
        self.assertEqual(
            imports["cupidasm"].id, "windows_cupidld_imports"
        )
        for extras_name in (
            "windows_native_stage_two_extras",
            "windows_native_stage_three_extras",
        ):
            extras = assigned_dict(extras_name)
            self.assertIn("cupidasm", extras)
            self.assertEqual(
                [key.value for key in extras["cupidasm"].keys],
                ["publication_runtime", "publication_start"],
            )

    def test_native_windows_plan_rejects_an_unplanned_link_object(self):
        linux_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        linux_plan["links"]["cupiddis"].insert(-1, "unplanned_object")

        with self.assertRaisesRegex(
            BootstrapError,
            "^Linux build plan links an unknown object: "
            "cupiddis: unplanned_object$",
        ):
            _windows_build_plan(linux_plan)

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
            execution_seed, plan_seed, source_root, output_root
        ):
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

    @unittest.skipUnless(os.name == "nt", "native Windows bootstrap")
    def test_checked_windows_seed_builds_a_native_producer_fixed_point(self):
        with tempfile.TemporaryDirectory(
            prefix=".checked-windows-bootstrap-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "published"
            environment = dict(os.environ)
            environment["PATH"] = str(Path(temporary) / "no-host-tools")
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
                    "bootstrap-windows",
                    "--manifest",
                    str(WINDOWS_SEED_MANIFEST),
                    "--plan-manifest",
                    str(SEED_MANIFEST),
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
                timeout=2400,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "checked i386 Windows bootstrap: ok "
                "(native stage three equals stage four)\n",
            )
            self.assertEqual(result.stderr, "")
            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                [
                    "behavior",
                    "bootstrap-report.json",
                    "stage-four",
                    "stage-three",
                    "stage-two",
                ],
            )
            report = json.loads(
                (output / "bootstrap-report.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                report["schema"], "cupid.windows-bootstrap-report.v1"
            )
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["platform"], "windows-native")
            self.assertEqual(
                report["seed_manifest_sha256"],
                hashlib.sha256(WINDOWS_SEED_MANIFEST.read_bytes()).hexdigest(),
            )
            self.assertEqual(
                report["plan_manifest_sha256"],
                hashlib.sha256(SEED_MANIFEST.read_bytes()).hexdigest(),
            )
            self.assertEqual(
                report["seed_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["plan_source_revision"],
                PROMOTED_SOURCE_REVISION,
            )
            self.assertEqual(
                report["comparisons"],
                {
                    "all_equal": True,
                    "assembly_objects": 3,
                    "c_objects": 23,
                    "compared_generations": [
                        "stage-three",
                        "stage-four",
                    ],
                    "tool_images": 6,
                },
            )
            self.assertEqual(
                report["behavior_generations"],
                ["stage-three", "stage-four"],
            )
            self.assertEqual(
                report["stages"]["stage-two"]["producer_generation"],
                "checked-windows-execution-seed",
            )
            self.assertEqual(
                report["stages"]["stage-three"]["producer_generation"],
                "native-stage-two",
            )
            self.assertEqual(
                report["stages"]["stage-four"]["producer_generation"],
                "native-stage-three",
            )
            self.assertEqual(
                report["behavior"],
                {
                    "failure_cases": 17,
                    "help_cases": 6,
                    "success_cases": 22,
                },
            )
            candidate_linux_plan = _candidate_build_plan(
                json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))[
                    "build_plan"
                ]
            )
            candidate_inventory = capture_source_snapshot(
                REPO_ROOT, candidate_linux_plan
            )
            candidate_snapshot = hashlib.sha256(
                json.dumps(
                    candidate_inventory,
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=True,
                ).encode("ascii")
            ).hexdigest()
            self.assertEqual(
                report["source_snapshot_sha256"],
                candidate_snapshot,
            )
            self.assertEqual(
                report["source_inputs"]["count"],
                PROMOTED_SOURCE_INPUT_COUNT,
            )
            self.assertEqual(
                report["source_inputs"]["sha256"],
                report["source_snapshot_sha256"],
            )
            self.assertEqual(
                report["initial_seed_matches_stage_two"],
                {name: True for name in CANDIDATE_TOOL_NAMES},
            )
            for stage_name in (
                "stage-two",
                "stage-three",
                "stage-four",
            ):
                stage = report["stages"][stage_name]
                self.assertEqual(len(stage["objects"]), 26)
                self.assertEqual(
                    set(stage["tools"]), set(CANDIDATE_TOOL_NAMES)
                )
                for tool_name in CANDIDATE_TOOL_NAMES:
                    self.assertTrue(
                        (output / stage_name / f"{tool_name}.exe").is_file()
                    )
            for name in report["stages"]["stage-three"]["objects"]:
                self.assertEqual(
                    (output / "stage-three" / f"{name}.o").read_bytes(),
                    (output / "stage-four" / f"{name}.o").read_bytes(),
                )
            for tool_name in CANDIDATE_TOOL_NAMES:
                self.assertEqual(
                    (output / "stage-three" / f"{tool_name}.exe").read_bytes(),
                    (output / "stage-four" / f"{tool_name}.exe").read_bytes(),
                )

    def test_checked_seed_run_forwards_cupidasm_help(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        result = subprocess.run(
            [
                sys.executable,
                str(BOOTSTRAP_TOOL),
                "run",
                "--manifest",
                str(SEED_MANIFEST),
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

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("usage:", result.stdout.casefold())
        self.assertIn("cupidasm", result.stdout.casefold())
        self.assertEqual(result.stderr, "")

    def test_checked_seed_wraps_jpeg_and_preserves_failed_output(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        baseline_jpeg = (
            b"\xff\xd8"
            b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
            b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
            b"\xff\xd9"
        )
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-jpeg-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "asset.jpg"
            progressive_source = root / "progressive.jpg"
            wrapped_output = root / "wrapped.o"
            jpeg_output = root / "jpeg.o"
            failed_output = root / "progressive.o"
            source.write_bytes(baseline_jpeg)
            progressive_source.write_bytes(
                baseline_jpeg[:3] + b"\xc2" + baseline_jpeg[4:]
            )
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            object_options = [
                "--stem",
                "fixed_point_asset",
                "--section",
                ".rodata",
                "--readonly",
            ]

            wrapped = runner.run(
                frozen.tools["cupidobj"],
                [
                    "wrap",
                    source,
                    *object_options,
                    "-o",
                    wrapped_output,
                ],
                60,
            )
            self.assertEqual(wrapped.returncode, 0, wrapped.stderr)
            self.assertEqual(wrapped.stdout, "")
            self.assertEqual(wrapped.stderr, "")

            checked = runner.run(
                frozen.tools["cupidobj"],
                [
                    "wrap-jpeg",
                    source,
                    *object_options,
                    "-o",
                    jpeg_output,
                ],
                60,
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)
            self.assertEqual(checked.stdout, "")
            self.assertEqual(checked.stderr, "")
            self.assertEqual(
                jpeg_output.read_bytes(), wrapped_output.read_bytes()
            )
            self.assertEqual(
                hashlib.sha256(jpeg_output.read_bytes()).hexdigest(),
                "a4950b4f13759a63540da33f08b584e804b6fb4f98afaa97a82e3d0a9191c35a",
            )

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "wrap-jpeg",
                    progressive_source,
                    *object_options,
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "unsupported progressive JPEG frame; "
                "check in a baseline SOF0/SOF1 asset",
                rejected.stderr,
            )
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_builds_disk_template_and_preserves_failed_output(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-disk-template-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            boot = root / "boot.bin"
            kernel = root / "kernel.bin"
            overlapping_kernel = root / "overlapping-kernel.bin"
            output = root / "template.bin"
            failed_output = root / "failed-template.bin"
            boot.write_bytes(
                bytes((index * 37 + 11) & 0xFF for index in range(5 * 512))
            )
            kernel.write_bytes(b"CUPID-OS")
            overlapping_kernel.write_bytes(b"K" * (3 * 512 + 1))
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            generated = runner.run(
                frozen.tools["cupidobj"],
                [
                    "disk-template",
                    boot,
                    "--kernel",
                    kernel,
                    "--image-sectors",
                    "4208",
                    "--fat-start-lba",
                    "8",
                    "-o",
                    output,
                ],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            template = output.read_bytes()
            self.assertEqual(len(template), 38400)
            self.assertEqual(
                hashlib.sha256(template).hexdigest(),
                "a1784fde1833c6cd24f49dff105ff8a70de5b9e619dd8883b4d92d597f241501",
            )

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "disk-template",
                    boot,
                    "--kernel",
                    overlapping_kernel,
                    "--image-sectors",
                    "4208",
                    "--fat-start-lba",
                    "8",
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "overlaps FAT partition at LBA 8",
                rejected.stderr,
            )
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_builds_iso_fixture_and_preserves_failed_output(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-iso-fixture-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            fixtures = REPO_ROOT / "test_iso" / "fixtures"
            manifest = REPO_ROOT / "test_iso" / "fixtures.manifest"
            output = root / "hello.iso"
            failed_manifest = root / "failed.manifest"
            failed_source = root / "payload.bin"
            failed_output = root / "failed.iso"
            failed_manifest.write_text(
                "lost/payload.bin\n", encoding="ascii", newline="\n"
            )
            failed_source.write_bytes(b"payload")
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            generated = runner.run(
                frozen.tools["cupidobj"],
                [
                    "iso-fixture",
                    manifest,
                    "--file",
                    "big.bin",
                    fixtures / "big.bin",
                    "--file",
                    "gen_big.sh",
                    fixtures / "gen_big.sh",
                    "--file",
                    "jpeg_baseline_8x8.jpg",
                    fixtures / "jpeg_baseline_8x8.jpg",
                    "--file",
                    "long_named_file.txt",
                    fixtures / "long_named_file.txt",
                    "--file",
                    "readme.txt",
                    fixtures / "readme.txt",
                    "--directory",
                    "sub",
                    "--file",
                    "sub/nested.txt",
                    fixtures / "sub" / "nested.txt",
                    "-o",
                    output,
                ],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            image = output.read_bytes()
            self.assertEqual(len(image), 61440)
            self.assertEqual(
                hashlib.sha256(image).hexdigest(),
                "40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6",
            )
            self.assertEqual(
                image,
                (REPO_ROOT / "test_iso" / "hello.iso").read_bytes(),
            )

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "iso-fixture",
                    failed_manifest,
                    "--file",
                    "lost/payload.bin",
                    failed_source,
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("directory parent", rejected.stderr)
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_checked_seed_builds_profile_manifest_and_preserves_failed_output(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-profile-manifest-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            snapshot = root / "profile.snapshot"
            output = root / "profile.json"
            failed_snapshot = root / "failed.snapshot"
            failed_output = root / "failed.json"
            schema = "cupid.profile-inputs.v1"
            inputs = (
                ("headers/empty.h", b""),
                ("headers/repeated.h", bytes(range(129))),
            )
            profiles = (
                (
                    "profile-tree",
                    tuple(path for path, _contents in inputs),
                    ("sources/tree.cc",),
                ),
                (
                    "profile-compat",
                    ("headers/repeated.h",),
                    ("sources/compat.cc",),
                ),
            )
            snapshot.write_bytes(
                _profile_snapshot_payload(schema, profiles, inputs)
            )
            failed_snapshot.write_bytes(
                _profile_snapshot_payload(
                    schema,
                    (
                        (
                            "profile-tree",
                            ("../unsafe.h",),
                            ("sources/tree.cc",),
                        ),
                    ),
                    (("../unsafe.h", b"unsafe"),),
                )
            )
            failed_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            generated = runner.run(
                frozen.tools["cupidobj"],
                ["profile-manifest", snapshot, "-o", output],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            expected = (
                json.dumps(
                    {
                        "schema": schema,
                        "profiles": {
                            name: sorted(headers)
                            for name, headers, _sources in profiles
                        },
                        "sources": {
                            name: sorted(sources)
                            for name, _headers, sources in profiles
                        },
                        "inputs": [
                            {
                                "path": path,
                                "bytes": len(contents),
                                "sha256": hashlib.sha256(
                                    contents
                                ).hexdigest(),
                            }
                            for path, contents in sorted(inputs)
                        ],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ).encode("ascii")
            self.assertEqual(output.read_bytes(), expected)

            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "profile-manifest",
                    failed_snapshot,
                    "-o",
                    failed_output,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("repository path is invalid", rejected.stderr)
            self.assertEqual(failed_output.read_bytes(), b"sentinel")

    def test_fixed_point_matrix_keeps_strict_cupiddis_checks(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior_functions = [
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_behavior_checks"
        ]
        self.assertEqual(len(behavior_functions), 1)
        behavior = behavior_functions[0]

        assigned_calls = {}
        for statement in behavior.body:
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and isinstance(statement.value, ast.Call)
            ):
                assigned_calls[statement.targets[0].id] = statement.value

        def stage_pair(name):
            call = assigned_calls[name]
            self.assertIsInstance(call.func, ast.Name)
            self.assertEqual(call.func.id, "_run_stage_pair")
            self.assertEqual(ast.literal_eval(call.args[3]), "cupiddis")
            return call

        def matrix_arguments(argument):
            self.assertIsInstance(argument, ast.List)
            rendered = []
            for item in argument.elts:
                if isinstance(item, ast.Constant):
                    rendered.append(item.value)
                elif isinstance(item, ast.Name):
                    rendered.append(f"<{item.id}>")
                else:
                    self.fail(
                        "strict CupidDis arguments contain an unexpected "
                        f"expression: {ast.dump(item)}"
                    )
            return rendered

        clean = stage_pair("strict_disassembly_result")
        self.assertEqual(
            matrix_arguments(clean.args[4]),
            ["--require-known", "<stage_two_valid>"],
        )
        self.assertEqual(
            matrix_arguments(clean.args[5]),
            ["--require-known", "<stage_three_valid>"],
        )

        truncated = stage_pair("strict_failure_result")
        self.assertEqual(len(truncated.args), 5)
        self.assertEqual(
            matrix_arguments(truncated.args[4]),
            [
                "--require-known",
                "--raw",
                "--mode=32",
                "--base=0",
                "<truncated_code>",
            ],
        )

        local_target = stage_pair("local_target_result")
        self.assertIn(
            "local_target_arguments", ast.unparse(local_target)
        )
        self.assertIn("'--require-local-targets'", ast.unparse(behavior))
        self.assertIn("stage_two_binary", ast.unparse(local_target.args[4]))
        self.assertIn("stage_three_binary", ast.unparse(local_target.args[5]))

        invalid_target = stage_pair("invalid_target_result")
        self.assertEqual(len(invalid_target.args), 5)
        self.assertIn(
            "local_target_arguments", ast.unparse(invalid_target.args[4])
        )
        self.assertIn(
            "invalid_target_binary", ast.unparse(invalid_target.args[4])
        )

        statuses = {}
        for node in ast.walk(behavior):
            if (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_expect_status"
                and len(node.args) >= 2
                and isinstance(node.args[0], ast.Name)
                and isinstance(node.args[1], ast.Constant)
            ):
                statuses.setdefault(node.args[0].id, []).append(
                    node.args[1].value
                )
        self.assertEqual(statuses["strict_disassembly_result"], [0])
        self.assertEqual(statuses["strict_failure_result"], [1])
        self.assertEqual(statuses["local_target_result"], [0])
        self.assertEqual(statuses["invalid_target_result"], [1])

        expected_fixture = ast.dump(
            ast.parse("bytes([0x0F])", mode="eval").body,
            include_attributes=False,
        )
        fixture_payloads = [
            ast.dump(node.args[0], include_attributes=False)
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "write_bytes"
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "truncated_code"
            and len(node.args) == 1
        ]
        self.assertEqual(fixture_payloads, [expected_fixture])

        messages = {
            node.value
            for node in ast.walk(behavior)
            if isinstance(node, ast.Constant)
            and isinstance(node.value, str)
        }
        self.assertIn(
            "truncated-code.bin: code check failed: "
            "0 known, 0 unknown, 0 invalid, 1 truncated",
            messages,
        )

        returns = [
            node
            for node in behavior.body
            if isinstance(node, ast.Return)
        ]
        self.assertEqual(len(returns), 1)
        return_value = returns[0].value
        self.assertIsInstance(return_value, ast.Dict)
        returned = {
            key.value: value
            for key, value in zip(return_value.keys, return_value.values)
        }
        self.assertEqual(
            returned["failure_cases"].value,
            29,
        )
        self.assertEqual(returned["success_cases"].value, 36)
        self.assertIsInstance(returned["help_cases"], ast.BinOp)
        self.assertIsInstance(returned["help_cases"].op, ast.Add)
        self.assertEqual(returned["help_cases"].right.value, 1)
        self.assertEqual(returned["help_cases"].left.func.id, "len")
        self.assertEqual(
            returned["help_cases"].left.args[0].id,
            "tool_names",
        )

        self.assertIn(
            "1 of 1 direct relative targets invalid",
            messages,
        )
        self.assertIn("1 outside image", messages)

    def test_fixed_point_cupidbuild_checks_cupidc_runner_behavior(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_cupidc_runner_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'--tool', 'cupidc', '--'",
            "'--help'",
            "'runner-valid.cc'",
            "'/runner-valid.cc'",
            "'/runner-invalid.cc:1:'",
            "b'preserved CupidBuild checked CupidC output\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
            "_validate_i386_relocatable(stage_two_output)",
        ):
            self.assertIn(expected, rendered)

        stage_pair_calls = [
            node
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_run_stage_pair"
        ]
        self.assertEqual(len(stage_pair_calls), 3)
        statuses = {
            node.args[0].id: node.args[1].value
            for node in ast.walk(behavior)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_expect_status"
        }
        self.assertEqual(
            statuses,
            {"help_result": 0, "success_result": 0, "failure_result": 1},
        )

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_cupidc_runner_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_matrix_checks_executable_relocation_ownership(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def behavior_function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        def assignment(function, name):
            return next(
                statement.value
                for statement in function.body
                if isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and statement.targets[0].id == name
            )

        def assert_relocation_check(function):
            call = assignment(function, "relocation_ownership_result")
            self.assertIsInstance(call, ast.Call)
            self.assertIsInstance(call.func, ast.Name)
            self.assertEqual(call.func.id, "_run_stage_pair")
            self.assertEqual(ast.literal_eval(call.args[3]), "cupiddis")
            for arguments in call.args[4:]:
                self.assertEqual(
                    [
                        item.value
                        if isinstance(item, ast.Constant)
                        else f"<{item.id}>"
                        for item in arguments.elts
                    ],
                    ["--require-known", "<unowned_relocation>"],
                )

            statuses = [
                node.args[1].value
                for node in ast.walk(function)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_expect_status"
                and isinstance(node.args[0], ast.Name)
                and node.args[0].id == "relocation_ownership_result"
            ]
            self.assertEqual(statuses, [1])
            self.assertIn(
                "1 of 1 executable relocations unmatched",
                {
                    node.value
                    for node in ast.walk(function)
                    if isinstance(node, ast.Constant)
                    and isinstance(node.value, str)
                },
            )

        assert_relocation_check(behavior_function("_run_behavior_checks"))
        assert_relocation_check(
            behavior_function("_run_native_windows_behavior_checks")
        )

    def test_fixed_point_cupidbuild_checks_every_guarded_assembly_operation(
        self,
    ):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_check_cupidbuild_guarded_object_behavior"
        )
        rendered = ast.unparse(behavior)

        for operation in (
            "assemble-cupidasm-object",
            "assemble-bootloader",
            "assemble-smp-trampoline",
        ):
            self.assertIn(repr(operation), rendered)
        self.assertIn("('guarded-bootloader.asm', 2560)", rendered)
        self.assertIn("('guarded-smp-trampoline.S', 4096)", rendered)
        self.assertIn("malformed-bootloader.asm", rendered)
        self.assertIn("preserved CupidBuild raw output", rendered)

    def test_fixed_point_cupidbuild_checks_typed_jpeg_transaction(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_embed_jpeg_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'embed-jpeg'",
            "'asset.jpg'",
            "'progressive.jpg'",
            "'checked CupidObj failed'",
            "b'preserved CupidBuild JPEG output\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
        ):
            self.assertIn(expected, rendered)

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_embed_jpeg_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_cupidbuild_checks_typed_kernel_symbol_transaction(
        self,
    ):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_generate_ksyms_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "'generate-ksyms'",
            "'kernel.elf.pass1'",
            "'malformed.elf'",
            "'checked CupidDis failed'",
            "b'preserved CupidBuild kernel symbol source\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
        ):
            self.assertIn(expected, rendered)

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_generate_ksyms_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_cupidbuild_checks_typed_kernel_flatten_transaction(
        self,
    ):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))

        def function(name):
            return next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name == name
            )

        behavior = function("_check_cupidbuild_flatten_kernel_behavior")
        rendered = ast.unparse(behavior)
        for expected in (
            "flatten_root.mkdir(parents=True)",
            "kernel_root = source_root / 'kernel'",
            "'flatten-kernel'",
            "'kernel.elf.pass1'",
            "'kernel.elf'",
            "'code-inputs.txt'",
            "'malformed-code-inputs.txt'",
            "'must end with a newline'",
            "b'preserved CupidBuild flat kernel\\n'",
            "stage_two_output.read_bytes() != "
            "stage_three_output.read_bytes()",
            "stage_two_failure.read_bytes() != sentinel",
            "stage_three_failure.read_bytes() != sentinel",
        ):
            self.assertIn(expected, rendered)

        for matrix_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            matrix = function(matrix_name)
            calls = [
                node
                for node in ast.walk(matrix)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id
                == "_check_cupidbuild_flatten_kernel_behavior"
            ]
            self.assertEqual(len(calls), 1)

    def test_fixed_point_relocation_fixture_is_a_valid_i386_object(self):
        payload = _unowned_relocation_object_payload()
        validate_i386_relocatable_bytes(payload)
        self.assertEqual(payload[64:70], bytes.fromhex("b8 00 00 00 00 c3"))

    def test_fixed_point_linked_local_target_fixture_is_a_static_i386_exec(self):
        valid = _local_target_executable_payload(5)
        invalid = _local_target_executable_payload(1)

        self.assertEqual(valid[:7], b"\x7fELF\x01\x01\x01")
        self.assertEqual(struct.unpack_from("<H", valid, 16)[0], 2)
        self.assertEqual(struct.unpack_from("<H", valid, 18)[0], 3)
        self.assertEqual(struct.unpack_from("<I", valid, 24)[0], 0x00400000)
        self.assertEqual(struct.unpack_from("<I", valid, 52)[0], 1)
        self.assertEqual(struct.unpack_from("<I", valid, 76)[0], 5)
        self.assertEqual(valid[84:], bytes.fromhex("eb 05 b8 00 00 00 00 c3"))
        self.assertEqual(invalid[84:], bytes.fromhex("eb 01 b8 00 00 00 00 c3"))

    def test_fixed_point_code_anchor_fixture_is_a_sectioned_i386_exec(self):
        valid = _code_anchor_executable_payload()
        invalid = _code_anchor_executable_payload(entry=0x00400001)

        self.assertEqual(valid[:7], b"\x7fELF\x01\x01\x01")
        self.assertEqual(struct.unpack_from("<H", valid, 16)[0], 2)
        self.assertEqual(struct.unpack_from("<H", valid, 18)[0], 3)
        self.assertEqual(struct.unpack_from("<I", valid, 24)[0], 0x00400000)
        self.assertEqual(struct.unpack_from("<I", invalid, 24)[0], 0x00400001)
        self.assertEqual(struct.unpack_from("<I", valid, 32)[0], 212)
        self.assertEqual(struct.unpack_from("<H", valid, 48)[0], 5)
        self.assertEqual(valid[84:90], bytes.fromhex("b8 78 56 34 12 c3"))
        self.assertEqual(struct.unpack_from("<I", valid, 132)[0], 0x00400000)
        self.assertEqual(struct.unpack_from("<I", valid, 148)[0], 0x00400000)
        self.assertEqual(valid[140], 0x12)
        self.assertEqual(valid[156], 0x12)

    def test_fixed_point_matrices_call_code_anchor_behavior_once(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        helpers = [
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_check_executable_code_anchor_behavior"
        ]
        self.assertEqual(len(helpers), 1)
        rendered_helper = ast.unparse(helpers[0])
        self.assertIn("'--require-code-anchors'", rendered_helper)
        self.assertIn("expected_invalid_stderr", rendered_helper)
        self.assertIn(
            "1 of 3 code anchors invalid (0 outside file-backed executable "
            "code, 1 mid-instruction)",
            rendered_helper,
        )

        for function_name in (
            "_run_behavior_checks",
            "_run_native_windows_behavior_checks",
        ):
            function = next(
                node
                for node in tree.body
                if isinstance(node, ast.FunctionDef)
                and node.name == function_name
            )
            calls = [
                node
                for node in ast.walk(function)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == "_check_executable_code_anchor_behavior"
            ]
            self.assertEqual(len(calls), 1, function_name)

    def test_linux_fixed_point_rebuilds_windows_cupiddis_main(self):
        tree = ast.parse(BOOTSTRAP_TOOL.read_text(encoding="utf-8"))
        behavior = next(
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_run_behavior_checks"
        )
        assignments = {
            node.targets[0].id: node.value
            for node in behavior.body
            if isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
        }
        compile_call = assignments["windows_cupiddis_main_compile_result"]
        self.assertIsInstance(compile_call, ast.Call)
        self.assertIsInstance(compile_call.func, ast.Name)
        self.assertEqual(compile_call.func.id, "_run_stage_pair")
        self.assertEqual(ast.literal_eval(compile_call.args[3]), "cupidc")
        for command in compile_call.args[4:6]:
            rendered = ast.unparse(command)
            self.assertIn("'_WIN32=1'", rendered)
            self.assertIn("'/toolchain/cupiddis_main.cc'", rendered)

        rendered = ast.unparse(behavior)
        for object_name in (
            "stage_two_windows_cupiddis_main",
            "stage_three_windows_cupiddis_main",
        ):
            self.assertIn(object_name, rendered)
        self.assertNotIn("objects['cupiddis_main']", rendered)

    def test_checked_seed_carries_parity_predicates_and_strict_decode_policy(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-strict-dis-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "parity.asm"
            output = root / "parity.bin"
            rejected_source = root / "parity-alias.asm"
            rejected_output = root / "parity-alias.bin"
            truncated = root / "truncated.bin"
            throughput = root / "strict-throughput.bin"
            source.write_text(
                "bits 32\n"
                "setp al\n"
                "setnp byte [eax + 4]\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "bits 32\nsetpe al\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            truncated.write_bytes(bytes([0x0F]))
            throughput.write_bytes(bytes([0x90]) * (128 * 1024))
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", source, "-o", output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(
                output.read_bytes(), bytes.fromhex("0f9ac00f9b4004")
            )

            strict = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0",
                    output,
                ],
                60,
            )
            self.assertEqual(strict.returncode, 0, strict.stderr)
            self.assertEqual(strict.stdout, "")
            self.assertEqual(strict.stderr, "")

            bounded_strict = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0",
                    throughput,
                ],
                30,
            )
            self.assertEqual(
                bounded_strict.returncode, 0, bounded_strict.stderr
            )
            self.assertEqual(bounded_strict.stdout, "")
            self.assertEqual(bounded_strict.stderr, "")

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                ["--raw", "--mode=32", "--base=0", output],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            rendered = disassembled.stdout.casefold()
            self.assertIn("setp al", rendered)
            self.assertIn("setnp byte", rendered)

            rejected = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", rejected_source, "-o", rejected_output],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "unknown Cupid ASM instruction mnemonic", rejected.stderr
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

            failed_strict = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--require-known",
                    "--raw",
                    "--mode=32",
                    "--base=0",
                    output,
                    truncated,
                ],
                60,
            )
            self.assertEqual(failed_strict.returncode, 1)
            self.assertEqual(failed_strict.stdout, "")
            self.assertIn(
                "truncated.bin: code check failed: "
                "0 known, 0 unknown, 0 invalid, 1 truncated",
                failed_strict.stderr,
            )

    def test_checked_seed_carries_shrd_with_address_overrides(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-shrd-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "shrd.asm"
            output = root / "shrd.bin"
            rejected_source = root / "invalid-shrd.asm"
            rejected_output = root / "invalid-shrd.bin"
            source.write_text(
                "bits 16\n"
                "a32 shrd dword [ebx + 4], esi, 31\n"
                "bits 32\n"
                "a16 shrd word [bx + si + 0x7f], dx, cl\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "bits 32\nshrd eax, edi, dl\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", source, "-o", output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(
                output.read_bytes(),
                bytes(
                    [
                        0x66,
                        0x67,
                        0x0F,
                        0xAC,
                        0x73,
                        0x04,
                        0x1F,
                        0x66,
                        0x67,
                        0x0F,
                        0xAD,
                        0x50,
                        0x7F,
                    ]
                ),
            )

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--raw",
                    "--mode=16",
                    "--range-at=7:32",
                    "--base=0",
                    output,
                ],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            rendered = disassembled.stdout.casefold()
            self.assertEqual(rendered.count("shrd"), 2)
            self.assertIn("esi, 0x1f", rendered)
            self.assertIn("dx, cl", rendered)

            rejected = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", rejected_source, "-o", rejected_output],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "no x86 form matches the instruction", rejected.stderr
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

    def test_checked_seed_carries_forward_x87_stack_subtraction(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-x87-subtraction-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "forward.asm"
            output = root / "forward.bin"
            rejected_source = root / "reversed.asm"
            rejected_output = root / "reversed.bin"
            compiler_source = root / "compiler-carriage.cc"
            compiler_object = root / "compiler-carriage.o"
            source.write_text(
                "bits 32\nsection .text\nfsub st1, st0\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "bits 32\nsection .text\nfsub st0, st1\n",
                encoding="utf-8",
                newline="\n",
            )
            compiler_source.write_text(
                "void corrected(volatile double *out,\n"
                "               const volatile double *x,\n"
                "               const double *log2e) {\n"
                "  __asm__ __volatile__(\n"
                '      "fldl   %[x]\\n\\t"\n'
                '      "fldl   %[log2e]\\n\\t"\n'
                '      "fmulp\\n\\t"\n'
                '      "fld    %%st(0)\\n\\t"\n'
                '      "frndint\\n\\t"\n'
                '      "fsubr  %%st, %%st(1)\\n\\t"\n'
                '      "fxch\\n\\t"\n'
                '      "f2xm1\\n\\t"\n'
                '      "fld1\\n\\t"\n'
                '      "faddp\\n\\t"\n'
                '      "fscale\\n\\t"\n'
                '      "fstp   %%st(1)\\n\\t"\n'
                '      "fstpl  %[out]\\n\\t"\n'
                '      : [out] "=m" (*out)\n'
                '      : [x] "m" (*x), [log2e] "m" (*log2e)\n'
                '      : "memory");\n'
                "}\n"
                "void legacy(volatile double *out,\n"
                "            const volatile double *x,\n"
                "            const double *log2e) {\n"
                "  __asm__ __volatile__(\n"
                '      "fldl   %[x]\\n\\t"\n'
                '      "fldl   %[log2e]\\n\\t"\n'
                '      "fmulp\\n\\t"\n'
                '      "fld    %%st(0)\\n\\t"\n'
                '      "frndint\\n\\t"\n'
                '      "fsub   %%st, %%st(1)\\n\\t"\n'
                '      "fxch\\n\\t"\n'
                '      "f2xm1\\n\\t"\n'
                '      "fld1\\n\\t"\n'
                '      "faddp\\n\\t"\n'
                '      "fscale\\n\\t"\n'
                '      "fstp   %%st(1)\\n\\t"\n'
                '      "fstpl  %[out]\\n\\t"\n'
                '      : [out] "=m" (*out)\n'
                '      : [x] "m" (*x), [log2e] "m" (*log2e)\n'
                '      : "memory");\n'
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", source, "-o", output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(output.read_bytes(), bytes([0xDC, 0xE9]))

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                ["--raw", "--mode=32", "--base=0", output],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            self.assertIn("fsub st1, st0", disassembled.stdout.casefold())

            rejected = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", rejected_source, "-o", rejected_output],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "no x86 form matches the instruction", rejected.stderr
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

            compiled = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + compiler_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + compiler_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(compiled.stdout, "")
            self.assertEqual(compiled.stderr, "")
            compiler_image = compiler_object.read_bytes()
            self.assertEqual(compiler_image.count(bytes([0xDC, 0xE9])), 1)
            self.assertEqual(compiler_image.count(bytes([0xDC, 0xE1])), 1)

            compiler_disassembly = runner.run(
                frozen.tools["cupiddis"],
                ["--disassemble", compiler_object],
                60,
            )
            self.assertEqual(
                compiler_disassembly.returncode,
                0,
                compiler_disassembly.stderr,
            )
            self.assertEqual(compiler_disassembly.stderr, "")
            rendered = compiler_disassembly.stdout.casefold()
            self.assertEqual(rendered.count("fsub st1, st0"), 1)
            self.assertEqual(rendered.count("fsubr st1, st0"), 1)

    def test_checked_seed_carries_x87_integer_and_long_double_frontier(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-x87-integer-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            assembly_source = root / "x87-integer.asm"
            assembly_output = root / "x87-integer.bin"
            compiler_source = root / "long-double-frontier.cc"
            compiler_object = root / "long-double-frontier.o"
            rejected_compiler_source = root / "invalid-long-double.cc"
            rejected_compiler_output = root / "invalid-long-double.o"
            startup_source = root / "start.asm"
            startup_object = root / "start.o"
            executable = root / "long-double-frontier.elf"
            assembly_source.write_text(
                "bits 32\n"
                "fild word [eax]\n"
                "fild dword [eax]\n"
                "fild qword [eax]\n"
                "fistp word [eax]\n"
                "fistp dword [eax]\n"
                "fistp qword [eax]\n"
                "fldcw word [eax]\n",
                encoding="utf-8",
                newline="\n",
            )
            compiler_source.write_text(
                "static long double widened_specials[6] = {\n"
                "  1.0f / 0.0f, -1.0f / 0.0f, 0.0f / 0.0f,\n"
                "  1.0 / 0.0, -1.0 / 0.0, 0.0 / 0.0\n"
                "};\n"
                "static float narrowed_float_specials[3] = {\n"
                "  (long double)(1.0 / 0.0),\n"
                "  (long double)(-1.0 / 0.0),\n"
                "  (long double)(0.0 / 0.0)\n"
                "};\n"
                "static double narrowed_double_specials[3] = {\n"
                "  (long double)(1.0f / 0.0f),\n"
                "  (long double)(-1.0f / 0.0f),\n"
                "  (long double)(0.0f / 0.0f)\n"
                "};\n"
                "static int special_controls[11] = {\n"
                "  !(long double)(1.0f / 0.0f),\n"
                "  !(long double)(-1.0 / 0.0),\n"
                "  !(long double)(0.0f / 0.0f),\n"
                "  (long double)(1.0f / 0.0f) > 1.0L,\n"
                "  (long double)(-1.0 / 0.0) < -1.0L,\n"
                "  (long double)(0.0 / 0.0) < 0.0L,\n"
                "  (long double)(0.0 / 0.0) <= 0.0L,\n"
                "  (long double)(0.0 / 0.0) > 0.0L,\n"
                "  (long double)(0.0 / 0.0) >= 0.0L,\n"
                "  (long double)(0.0 / 0.0) == 0.0L,\n"
                "  (long double)(0.0 / 0.0) != 0.0L\n"
                "};\n"
                "static long double converted_unsigned_max =\n"
                "    18446744073709551615ull;\n"
                "static long double folded_sum = 1.0L + 2.0L;\n"
                "int seed_long_double_frontier(void) {\n"
                "  const unsigned int *special_words =\n"
                "      (const unsigned int *)widened_specials;\n"
                "  const unsigned int *narrowed_float_words =\n"
                "      (const unsigned int *)narrowed_float_specials;\n"
                "  const unsigned int *narrowed_double_words =\n"
                "      (const unsigned int *)narrowed_double_specials;\n"
                "  const unsigned int *maximum_words =\n"
                "      (const unsigned int *)&converted_unsigned_max;\n"
                "  const unsigned int *folded_words =\n"
                "      (const unsigned int *)&folded_sum;\n"
                "  float updated_float = 1.0f;\n"
                "  double updated_double = 2.0;\n"
                "  float old_float = updated_float++;\n"
                "  double old_double = updated_double--;\n"
                "  short narrow = -123;\n"
                "  int word = -456789;\n"
                "  long long wide = -1234567890123ll;\n"
                "  unsigned long long unsigned_wide =\n"
                "      18446744073709551615ull;\n"
                "  long double narrow_extended = (long double)narrow;\n"
                "  long double word_extended = (long double)word;\n"
                "  long double wide_extended = (long double)wide;\n"
                "  long double unsigned_extended =\n"
                "      (long double)unsigned_wide;\n"
                "  unsigned short control = 0x037fu;\n"
                "  __asm__ volatile(\"fldcw %0\" : : \"m\"(control));\n"
                "  if (special_words[0] != 0u ||\n"
                "      special_words[1] != 0x80000000u ||\n"
                "      special_words[2] != 0x00007fffu ||\n"
                "      special_words[3] != 0u ||\n"
                "      special_words[4] != 0x80000000u ||\n"
                "      special_words[5] != 0x0000ffffu ||\n"
                "      special_words[6] != 0u ||\n"
                "      special_words[7] != 0xc0000000u ||\n"
                "      special_words[8] != 0x00007fffu) return 1;\n"
                "  if (special_words[9] != special_words[0] ||\n"
                "      special_words[10] != special_words[1] ||\n"
                "      special_words[11] != special_words[2] ||\n"
                "      special_words[12] != special_words[3] ||\n"
                "      special_words[13] != special_words[4] ||\n"
                "      special_words[14] != special_words[5] ||\n"
                "      special_words[15] != special_words[6] ||\n"
                "      special_words[16] != special_words[7] ||\n"
                "      special_words[17] != special_words[8]) return 2;\n"
                "  if (narrowed_float_words[0] != 0x7f800000u ||\n"
                "      narrowed_float_words[1] != 0xff800000u ||\n"
                "      narrowed_float_words[2] != 0x7fc00000u) return 3;\n"
                "  if (narrowed_double_words[0] != 0u ||\n"
                "      narrowed_double_words[1] != 0x7ff00000u ||\n"
                "      narrowed_double_words[2] != 0u ||\n"
                "      narrowed_double_words[3] != 0xfff00000u ||\n"
                "      narrowed_double_words[4] != 0u ||\n"
                "      narrowed_double_words[5] != 0x7ff80000u) return 4;\n"
                "  if (special_controls[0] != 0 ||\n"
                "      special_controls[1] != 0 ||\n"
                "      special_controls[2] != 0 ||\n"
                "      special_controls[3] != 1 ||\n"
                "      special_controls[4] != 1 ||\n"
                "      special_controls[5] != 0 ||\n"
                "      special_controls[6] != 0 ||\n"
                "      special_controls[7] != 0 ||\n"
                "      special_controls[8] != 0 ||\n"
                "      special_controls[9] != 0 ||\n"
                "      special_controls[10] != 1) return 5;\n"
                "  if (maximum_words[0] != 0xffffffffu ||\n"
                "      maximum_words[1] != 0xffffffffu ||\n"
                "      maximum_words[2] != 0x0000403eu) return 6;\n"
                "  if ((short)narrow_extended != narrow ||\n"
                "      (int)word_extended != word ||\n"
                "      (long long)wide_extended != wide ||\n"
                "      (unsigned long long)unsigned_extended !=\n"
                "          unsigned_wide) return 7;\n"
                "  if (folded_words[0] != 0u ||\n"
                "      folded_words[1] != 0xc0000000u ||\n"
                "      folded_words[2] != 0x00004000u) return 8;\n"
                "  if (old_float != 1.0f || updated_float != 2.0f ||\n"
                "      ++updated_float != 3.0f || old_double != 2.0 ||\n"
                "      updated_double != 1.0 || --updated_double != 0.0)\n"
                "    return 9;\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_compiler_source.write_text(
                "void bad(_Atomic float *value) { (*value)++; }\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_compiler_output.write_bytes(b"sentinel")
            startup_source.write_text(
                "bits 32\n"
                "section .text\n"
                "global _start\n"
                "extern seed_long_double_frontier\n"
                "_start:\n"
                "    call seed_long_double_frontier\n"
                "    mov ebx, eax\n"
                "    mov eax, 1\n"
                "    int 0x80\n",
                encoding="utf-8",
                newline="\n",
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "bin", assembly_source, "-o", assembly_output],
                60,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            self.assertEqual(
                assembly_output.read_bytes(),
                bytes.fromhex("df00db00df28df18db18df38d928"),
            )

            disassembled = runner.run(
                frozen.tools["cupiddis"],
                ["--raw", "--mode=32", "--base=0", assembly_output],
                60,
            )
            self.assertEqual(
                disassembled.returncode, 0, disassembled.stderr
            )
            self.assertEqual(disassembled.stderr, "")
            rendered = disassembled.stdout.casefold()
            for instruction in (
                "fild word [eax]",
                "fild dword [eax]",
                "fild qword [eax]",
                "fistp word [eax]",
                "fistp dword [eax]",
                "fistp qword [eax]",
                "fldcw word [eax]",
            ):
                self.assertEqual(rendered.count(instruction), 1)

            rejected_assembly_cases = (
                ("fild-register", "fild eax"),
                ("fistp-byte", "fistp byte [eax]"),
                ("fldcw-dword", "fldcw dword [eax]"),
            )
            for label, instruction in rejected_assembly_cases:
                rejected_source = root / f"invalid-{label}.asm"
                rejected_output = root / f"invalid-{label}.bin"
                rejected_source.write_text(
                    f"bits 32\n{instruction}\n",
                    encoding="utf-8",
                    newline="\n",
                )
                rejected_output.write_bytes(b"sentinel")
                rejected = runner.run(
                    frozen.tools["cupidasm"],
                    [
                        "-f",
                        "bin",
                        rejected_source,
                        "-o",
                        rejected_output,
                    ],
                    60,
                )
                self.assertEqual(rejected.returncode, 1)
                self.assertEqual(rejected.stdout, "")
                self.assertIn(
                    "no x86 form matches the instruction",
                    rejected.stderr,
                )
                self.assertEqual(rejected_output.read_bytes(), b"sentinel")

            compiled = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + compiler_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + compiler_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(compiled.stdout, "")
            self.assertEqual(compiled.stderr, "")
            self.assertIn(bytes.fromhex("d928"), compiler_object.read_bytes())

            rejected_compile = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/"
                    + rejected_compiler_source.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "-o",
                    "/"
                    + rejected_compiler_output.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                180,
            )
            self.assertEqual(rejected_compile.returncode, 1)
            self.assertEqual(rejected_compile.stdout, "")
            self.assertIn(
                "atomic floating-point updates are not supported",
                rejected_compile.stderr,
            )
            self.assertEqual(
                rejected_compiler_output.read_bytes(), b"sentinel"
            )

            assembled_start = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", startup_source, "-o", startup_object],
                60,
            )
            self.assertEqual(
                assembled_start.returncode, 0, assembled_start.stderr
            )
            self.assertEqual(assembled_start.stdout, "")
            self.assertEqual(assembled_start.stderr, "")
            linked = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x08048000",
                    "--entry",
                    "_start",
                    "-o",
                    executable,
                    startup_object,
                    compiler_object,
                ],
                180,
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertEqual(linked.stdout, "")
            self.assertEqual(linked.stderr, "")
            executed = runner.run(executable, [], 60)
            self.assertEqual(executed.returncode, 0, executed.stderr)
            self.assertEqual(executed.stdout, "")
            self.assertEqual(executed.stderr, "")

    def test_checked_seed_builds_and_runs_imported_pe32(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-imported-pe32-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            startup_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/start.asm"
            )
            contract_source = (
                REPO_ROOT
                / "toolchain/tests/hosted_i386_windows_contract.cc"
            )
            startup_object = root / "start.o"
            contract_object = root / "contract.o"
            image = root / "runtime.exe"
            invalid_source = root / "invalid-import.asm"
            invalid_object = root / "invalid-import.o"
            invalid_image = root / "invalid-import.exe"
            invalid_source.write_text(
                "bits 32\n"
                "extern __imp_ExitProcess\n"
                "global _start\n"
                "section .text\n"
                "_start:\n"
                "    call __imp_ExitProcess\n"
                "    ret\n",
                encoding="utf-8",
                newline="\n",
            )
            invalid_image.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)

            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", startup_source, "-o", startup_object],
                120,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")
            compiled = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--freestanding",
                    "-c",
                    "/" + contract_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + contract_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertEqual(compiled.stdout, "")
            self.assertEqual(compiled.stderr, "")
            imports = (
                "__imp_WriteFile=KERNEL32.dll:WriteFile",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
            )
            linked = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    imports[0],
                    "--import",
                    imports[1],
                    "--import",
                    imports[2],
                    "-o",
                    image,
                    startup_object,
                    contract_object,
                ],
                180,
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertEqual(linked.stdout, "")
            self.assertEqual(linked.stderr, "")
            _validate_static_i386_pe32(
                image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        ("ExitProcess", "GetStdHandle", "WriteFile"),
                    ),
                ),
            )
            if os.name == "nt":
                executed = subprocess.run(
                    [str(image)],
                    cwd=REPO_ROOT,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(executed.returncode, 37)
                self.assertEqual(
                    executed.stdout, b"Cupid-built Windows runtime: ok\n"
                )
                self.assertEqual(executed.stderr, b"")

            assembled_invalid = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", invalid_source, "-o", invalid_object],
                120,
            )
            self.assertEqual(
                assembled_invalid.returncode,
                0,
                assembled_invalid.stderr,
            )
            self.assertEqual(assembled_invalid.stdout, "")
            self.assertEqual(assembled_invalid.stderr, "")
            rejected = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                    "--import",
                    imports[1],
                    "-o",
                    invalid_image,
                    invalid_object,
                ],
                180,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "IAT symbols require an absolute zero-addend relocation",
                rejected.stderr,
            )
            self.assertEqual(invalid_image.read_bytes(), b"sentinel")

    def test_checked_seed_builds_and_runs_native_windows_tool_boundary(self):
        if os.name != "nt":
            self.skipTest("native PE32 execution requires Windows")
        if shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-windows-cupiddis-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            sources = (
                "ctool",
                "ctool_host",
                "elf32",
                "x86",
                "cupiddis",
                "cupiddis_main",
                "cupidld",
            )
            objects: dict[str, Path] = {}
            for name in sources:
                source = REPO_ROOT / "toolchain" / f"{name}.cc"
                output = root / f"{name}.o"
                arguments = ["--root", REPO_ROOT]
                if name in ("ctool_host", "cupiddis_main"):
                    arguments.extend(("-D", "_WIN32=1"))
                arguments.extend(
                    (
                        "-c",
                        "/" + source.relative_to(REPO_ROOT).as_posix(),
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    )
                )
                result = runner.run(
                    frozen.tools["cupidc"], arguments, 360
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                objects[name] = output

            runtime_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/runtime.cc"
            )
            runtime_object = root / "runtime.o"
            compiled_runtime = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "-c",
                    "/" + runtime_source.relative_to(REPO_ROOT).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/" + runtime_object.relative_to(REPO_ROOT).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_runtime.returncode, 0, compiled_runtime.stderr
            )
            self.assertEqual(compiled_runtime.stdout, "")
            self.assertEqual(compiled_runtime.stderr, "")

            runtime_contract_source = (
                REPO_ROOT
                / "toolchain/tests/hosted_i386_windows_runtime_contract.cc"
            )
            runtime_contract_object = root / "runtime-contract.o"
            compiled_contract = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-c",
                    "/"
                    + runtime_contract_source.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/"
                    + runtime_contract_object.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_contract.returncode, 0, compiled_contract.stderr
            )
            self.assertEqual(compiled_contract.stdout, "")
            self.assertEqual(compiled_contract.stderr, "")

            startup_source = (
                REPO_ROOT / "toolchain/hosted/i386-windows/tool_start.asm"
            )
            startup_lines = [
                line.strip()
                for line in startup_source.read_text(
                    encoding="utf-8"
                ).splitlines()
            ]
            entry_start = startup_lines.index("_start:") + 1
            entry_end = startup_lines.index(
                "cupid_windows_close_handle:"
            )
            self.assertEqual(
                startup_lines[entry_start:entry_end],
                [
                    "cld",
                    "and esp, 0xfffffff0",
                    "call dword [__imp_GetCommandLineA]",
                    "sub esp, 12",
                    "push eax",
                    "call cupid_windows_runtime_start",
                    "add esp, 16",
                    "sub esp, 12",
                    "push eax",
                    "call dword [__imp_ExitProcess]",
                    "hlt",
                    "",
                ],
            )
            startup_object = root / "start.o"
            assembled = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", startup_source, "-o", startup_object],
                120,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual(assembled.stdout, "")
            self.assertEqual(assembled.stderr, "")

            imports = (
                "__imp_CloseHandle=KERNEL32.dll:CloseHandle",
                "__imp_CreateFileA=KERNEL32.dll:CreateFileA",
                "__imp_ExitProcess=KERNEL32.dll:ExitProcess",
                "__imp_GetCommandLineA=KERNEL32.dll:GetCommandLineA",
                "__imp_GetCurrentDirectoryA=KERNEL32.dll:GetCurrentDirectoryA",
                "__imp_GetLastError=KERNEL32.dll:GetLastError",
                "__imp_GetStdHandle=KERNEL32.dll:GetStdHandle",
                "__imp_ReadFile=KERNEL32.dll:ReadFile",
                "__imp_SetFilePointer=KERNEL32.dll:SetFilePointer",
                "__imp_VirtualAlloc=KERNEL32.dll:VirtualAlloc",
                "__imp_VirtualFree=KERNEL32.dll:VirtualFree",
                "__imp_WriteFile=KERNEL32.dll:WriteFile",
            )
            image = root / "cupiddis.exe"
            link_arguments: list[str | Path] = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in reversed(imports):
                link_arguments.extend(("--import", imported))
            link_arguments.extend(
                (
                    "-o",
                    image,
                    startup_object,
                    objects["cupiddis_main"],
                    objects["cupiddis"],
                    objects["ctool_host"],
                    objects["ctool"],
                    objects["elf32"],
                    objects["x86"],
                    runtime_object,
                )
            )
            linked = runner.run(
                frozen.tools["cupidld"], link_arguments, 180
            )
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertEqual(linked.stdout, "")
            self.assertEqual(linked.stderr, "")
            _validate_static_i386_pe32(
                image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        tuple(
                            item.rsplit(":", 1)[1] for item in imports
                        ),
                    ),
                ),
            )

            help_result = subprocess.run(
                [str(image), "--help"],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(help_result.returncode, 0)
            self.assertIn(b"usage: cupiddis", help_result.stdout)
            self.assertEqual(help_result.stderr, b"")

            raw_input = root / "quoted input.bin"
            raw_input.write_bytes(b"\x90\xc3")
            arguments = [
                "--raw",
                "--mode",
                "32",
                "--base",
                "0",
                raw_input,
            ]
            oracle = runner.run(
                frozen.tools["cupiddis"], arguments, 60
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)
            native = subprocess.run(
                [str(image), *[str(argument) for argument in arguments]],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(native.returncode, oracle.returncode)
            self.assertEqual(native.stdout, oracle.stdout.encode("utf-8"))
            self.assertEqual(native.stderr, oracle.stderr.encode("utf-8"))

            missing = root / "missing input.bin"
            failed = subprocess.run(
                [
                    str(image),
                    "--raw",
                    "--mode",
                    "32",
                    "--base",
                    "0",
                    str(missing),
                ],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(failed.stdout, b"")
            self.assertIn(b"cannot load", failed.stderr)
            self.assertIn(b"not_found", failed.stderr)

            runtime_contract_image = root / "runtime-contract.exe"
            contract_link_arguments: list[str | Path] = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in imports:
                contract_link_arguments.extend(("--import", imported))
            contract_link_arguments.extend(
                (
                    "-o",
                    runtime_contract_image,
                    startup_object,
                    runtime_contract_object,
                    runtime_object,
                )
            )
            contract_linked = runner.run(
                frozen.tools["cupidld"], contract_link_arguments, 180
            )
            self.assertEqual(
                contract_linked.returncode, 0, contract_linked.stderr
            )
            self.assertEqual(contract_linked.stdout, "")
            self.assertEqual(contract_linked.stderr, "")
            _validate_static_i386_pe32(
                runtime_contract_image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        tuple(
                            item.rsplit(":", 1)[1] for item in imports
                        ),
                    ),
                ),
            )

            contract_output = root / "runtime output.bin"
            contract_missing = root / "runtime missing.bin"
            runtime_result = subprocess.run(
                [
                    str(runtime_contract_image),
                    "plain",
                    "space arg",
                    'quote"arg',
                    "trailing\\",
                    str(contract_output),
                    str(contract_missing),
                ],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(
                runtime_result.returncode, 0, runtime_result.stderr
            )
            self.assertEqual(
                runtime_result.stdout,
                b"Cupid-built Windows tool runtime: ok\n",
            )
            self.assertEqual(runtime_result.stderr, b"")
            self.assertEqual(contract_output.read_bytes(), b"headtail")

            runtime_failure = subprocess.run(
                [str(runtime_contract_image), "wrong"],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            self.assertEqual(runtime_failure.returncode, 41)
            self.assertEqual(runtime_failure.stdout, b"")
            self.assertIn(
                b"windows runtime arguments: bad", runtime_failure.stderr
            )

            cupidld_main_source = REPO_ROOT / "toolchain/cupidld_main.cc"
            cupidld_main_object = root / "cupidld-main-windows.o"
            compiled_cupidld_main = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-D",
                    "_WIN32=1",
                    "-c",
                    "/"
                    + cupidld_main_source.relative_to(REPO_ROOT).as_posix(),
                    "-I",
                    "/toolchain",
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/"
                    + cupidld_main_object.relative_to(REPO_ROOT).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_cupidld_main.returncode,
                0,
                compiled_cupidld_main.stderr,
            )
            self.assertEqual(compiled_cupidld_main.stdout, "")
            self.assertEqual(compiled_cupidld_main.stderr, "")

            publication_runtime_source = (
                REPO_ROOT
                / "toolchain/hosted/i386-windows/publication_runtime.cc"
            )
            publication_runtime_object = root / "publication-runtime.o"
            compiled_publication_runtime = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "-D",
                    "_WIN32=1",
                    "-c",
                    "/"
                    + publication_runtime_source.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "--include-angle",
                    "/toolchain/hosted/i386-linux/include",
                    "-o",
                    "/"
                    + publication_runtime_object.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                360,
            )
            self.assertEqual(
                compiled_publication_runtime.returncode,
                0,
                compiled_publication_runtime.stderr,
            )
            self.assertEqual(compiled_publication_runtime.stdout, "")
            self.assertEqual(compiled_publication_runtime.stderr, "")

            publication_start_source = (
                REPO_ROOT
                / "toolchain/hosted/i386-windows/publication_start.asm"
            )
            publication_start_object = root / "publication-start.o"
            assembled_publication_start = runner.run(
                frozen.tools["cupidasm"],
                [
                    "-f",
                    "elf32",
                    publication_start_source,
                    "-o",
                    publication_start_object,
                ],
                120,
            )
            self.assertEqual(
                assembled_publication_start.returncode,
                0,
                assembled_publication_start.stderr,
            )
            self.assertEqual(assembled_publication_start.stdout, "")
            self.assertEqual(assembled_publication_start.stderr, "")

            publication_imports = (
                "__imp_DeleteFileA=KERNEL32.dll:DeleteFileA",
                "__imp_FlushFileBuffers=KERNEL32.dll:FlushFileBuffers",
                "__imp_GetFullPathNameA=KERNEL32.dll:GetFullPathNameA",
                "__imp_MoveFileExA=KERNEL32.dll:MoveFileExA",
            )
            cupidld_image = root / "cupidld.exe"
            cupidld_link_arguments: list[str | Path] = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in (*imports, *publication_imports):
                cupidld_link_arguments.extend(("--import", imported))
            cupidld_link_arguments.extend(
                (
                    "-o",
                    cupidld_image,
                    startup_object,
                    publication_start_object,
                    cupidld_main_object,
                    objects["cupidld"],
                    objects["ctool_host"],
                    objects["ctool"],
                    objects["elf32"],
                    runtime_object,
                    publication_runtime_object,
                )
            )
            linked_cupidld = runner.run(
                frozen.tools["cupidld"], cupidld_link_arguments, 180
            )
            self.assertEqual(
                linked_cupidld.returncode, 0, linked_cupidld.stderr
            )
            self.assertEqual(linked_cupidld.stdout, "")
            self.assertEqual(linked_cupidld.stderr, "")
            _validate_static_i386_pe32(
                cupidld_image,
                0x00401000,
                (
                    (
                        "KERNEL32.dll",
                        tuple(
                            sorted(
                                item.rsplit(":", 1)[1]
                                for item in (*imports, *publication_imports)
                            )
                        ),
                    ),
                ),
            )

            cupidld_help = subprocess.run(
                [str(cupidld_image), "--help"],
                cwd=root,
                capture_output=True,
                timeout=20,
            )
            cupidld_help_oracle = runner.run(
                frozen.tools["cupidld"], ["--help"], 60
            )
            self.assertEqual(cupidld_help.returncode, 0)
            self.assertEqual(
                cupidld_help.stdout,
                cupidld_help_oracle.stdout.encode("utf-8"),
            )
            self.assertEqual(cupidld_help.stderr, b"")

            native_link_output = root / "native linked output.exe"
            native_link_output.write_bytes(b"sentinel")
            occupied_candidate = root / (
                "native linked output.exe.cupid-tmp-00000000"
            )
            occupied_candidate.write_bytes(b"occupied")
            native_link_arguments = [
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ]
            for imported in imports:
                native_link_arguments.extend(("--import", imported))
            native_link_arguments.extend(
                (
                    "-o",
                    native_link_output.name,
                    startup_object.name,
                    runtime_contract_object.name,
                    runtime_object.name,
                )
            )
            native_link = subprocess.run(
                [str(cupidld_image), *native_link_arguments],
                cwd=root,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(native_link.returncode, 0, native_link.stderr)
            self.assertEqual(native_link.stdout, b"")
            self.assertEqual(native_link.stderr, b"")
            self.assertEqual(
                native_link_output.read_bytes(),
                runtime_contract_image.read_bytes(),
            )
            self.assertEqual(occupied_candidate.read_bytes(), b"occupied")
            self.assertEqual(
                sorted(root.glob("native linked output.exe.cupid-tmp-*")),
                [occupied_candidate],
            )

            blocked_output = root / "blocked-output.exe"
            blocked_output.mkdir()
            blocked_arguments = list(native_link_arguments)
            blocked_arguments[
                blocked_arguments.index(native_link_output.name)
            ] = blocked_output.name
            blocked_link = subprocess.run(
                [str(cupidld_image), *blocked_arguments],
                cwd=root,
                capture_output=True,
                timeout=60,
            )
            self.assertEqual(blocked_link.returncode, 1)
            self.assertEqual(blocked_link.stdout, b"")
            self.assertIn(b"cupidld: link failed (io)", blocked_link.stderr)
            self.assertTrue(blocked_output.is_dir())
            self.assertEqual(
                sorted(root.glob("blocked-output.exe.cupid-tmp-*")), []
            )

    def test_checked_seed_preserves_returns_twice_call_operands(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-returns-twice-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "returns-twice.cc"
            rejected_source = root / "returns-twice-pointer.cc"
            rejected_output = root / "returns-twice-pointer.o"
            source.write_text(
                "extern int seed_restart(unsigned int env[6]) "
                "__attribute__((returns_twice));\n"
                "int add_after_restart(unsigned int env[6], int left) {\n"
                "  return left + seed_restart(env);\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_source.write_text(
                "extern int seed_restart(unsigned int env[6]) "
                "__attribute__((returns_twice));\n"
                "int indirect_restart(unsigned int env[6]) {\n"
                "  int (*saved)(unsigned int env[6]) = seed_restart;\n"
                "  return saved(env);\n"
                "}\n",
                encoding="utf-8",
                newline="\n",
            )
            rejected_output.write_bytes(b"sentinel")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            images = []
            for index in range(2):
                output = root / f"returns-twice-{index}.o"
                compiled = runner.run(
                    frozen.tools["cupidc"],
                    [
                        "--root",
                        REPO_ROOT,
                        "--gnu",
                        "--freestanding",
                        "-c",
                        "/" + source.relative_to(REPO_ROOT).as_posix(),
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    ],
                    180,
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                self.assertEqual(compiled.stdout, "")
                self.assertEqual(compiled.stderr, "")
                images.append(output.read_bytes())

            self.assertEqual(images[0], images[1])
            self.assertEqual(
                (len(images[0]), hashlib.sha256(images[0]).hexdigest()),
                (
                    500,
                    "992a554a6fe0d23cba3f33c0faedcf44004c635a75924e3c61847fd1d2540fb8",
                ),
            )

            rejected = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/"
                    + rejected_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/"
                    + rejected_output.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(
                "CupidC requires returns_twice functions to be called "
                "directly instead of converted to a function pointer",
                rejected.stderr,
            )
            self.assertEqual(rejected_output.read_bytes(), b"sentinel")

    def test_checked_seed_compiles_links_and_runs_floating_truth(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        source_text = (
            "typedef union { float value; unsigned int bits; } float_box;\n"
            "typedef union {\n"
            "  double value;\n"
            "  struct { unsigned int low; unsigned int high; } words;\n"
            "} double_box;\n"
            "typedef union {\n"
            "  long double value;\n"
            "  struct {\n"
            "    unsigned int significand_low;\n"
            "    unsigned int significand_high;\n"
            "    unsigned int sign_exponent_padding;\n"
            "  } words;\n"
            "} long_box;\n"
            "int seed_floating_truth(void) {\n"
            "  float_box narrow;\n"
            "  double_box wide;\n"
            "  long_box extended;\n"
            "  float narrow_negative_zero;\n"
            "  float narrow_nan;\n"
            "  double wide_negative_zero;\n"
            "  double wide_nan;\n"
            "  _Bool truth;\n"
            "  narrow.bits = 0x80000000u;\n"
            "  narrow_negative_zero = narrow.value;\n"
            "  narrow.bits = 0x7fc00001u;\n"
            "  narrow_nan = narrow.value;\n"
            "  wide.words.low = 0u;\n"
            "  wide.words.high = 0x80000000u;\n"
            "  wide_negative_zero = wide.value;\n"
            "  wide.words.low = 1u;\n"
            "  wide.words.high = 0x7ff80000u;\n"
            "  wide_nan = wide.value;\n"
            "  extended.words.significand_low = 0u;\n"
            "  extended.words.significand_high = 0u;\n"
            "  extended.words.sign_exponent_padding = 0x8000u;\n"
            "  if (narrow_negative_zero || wide_negative_zero ||\n"
            "      extended.value) return 1;\n"
            "  extended.words.significand_low = 1u;\n"
            "  extended.words.significand_high = 0u;\n"
            "  extended.words.sign_exponent_padding = 0u;\n"
            "  if (!narrow_nan || !wide_nan || !extended.value) return 2;\n"
            "  if ((!narrow_negative_zero) != 1 || (!wide_nan) != 0 ||\n"
            "      (!extended.value) != 0) return 3;\n"
            "  truth = narrow_negative_zero;\n"
            "  if (truth != 0) return 4;\n"
            "  truth = wide_nan;\n"
            "  if (truth != 1) return 5;\n"
            "  truth = (_Bool)extended.value;\n"
            "  if (truth != 1) return 6;\n"
            "  if (!(narrow_nan && extended.value) ||\n"
            "      wide_negative_zero) return 7;\n"
            "  if ((wide_nan ? 11 : 13) != 11) return 8;\n"
            "  extended.value = 1.0000000000000000001L;\n"
            "  if (extended.words.significand_low != 1u ||\n"
            "      extended.words.significand_high != 0x80000000u ||\n"
            "      extended.words.sign_exponent_padding != 0x3fffu) return 9;\n"
            "  return 0;\n"
            "}\n"
        )
        start_text = (
            "bits 32\n"
            "section .text\n"
            "global _start\n"
            "extern seed_floating_truth\n"
            "_start:\n"
            "    call seed_floating_truth\n"
            "    mov ebx, eax\n"
            "    mov eax, 1\n"
            "    int 0x80\n"
        )
        atomic_source_text = (
            "int bad(_Atomic float value) { return !value; }\n"
        )
        precise_literal_failure_text = (
            "long double bad(void) { "
            "return 1.00000000000000000001L; }\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-floating-truth-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "floating-truth.cc"
            start = root / "start.asm"
            source.write_text(source_text, encoding="utf-8", newline="\n")
            start.write_text(start_text, encoding="utf-8", newline="\n")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            source_object = root / "floating-truth.o"
            start_object = root / "start.o"
            executable = root / "floating-truth.elf"
            compile_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + source_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            self.assertEqual(compile_result.stdout, "")
            self.assertEqual(compile_result.stderr, "")
            assemble_result = runner.run(
                frozen.tools["cupidasm"],
                ["-f", "elf32", start, "-o", start_object],
                120,
            )
            self.assertEqual(
                assemble_result.returncode, 0, assemble_result.stderr
            )
            self.assertEqual(assemble_result.stdout, "")
            self.assertEqual(assemble_result.stderr, "")
            link_result = runner.run(
                frozen.tools["cupidld"],
                [
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x08048000",
                    "--entry",
                    "_start",
                    "-o",
                    executable,
                    start_object,
                    source_object,
                ],
                180,
            )
            self.assertEqual(link_result.returncode, 0, link_result.stderr)
            self.assertEqual(link_result.stdout, "")
            self.assertEqual(link_result.stderr, "")
            run_result = runner.run(executable, [], 60)
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            self.assertEqual(run_result.stdout, "")
            self.assertEqual(run_result.stderr, "")

            atomic_source = root / "atomic-floating-truth.cc"
            atomic_object = root / "atomic-floating-truth.o"
            atomic_source.write_text(
                atomic_source_text, encoding="utf-8", newline="\n"
            )
            atomic_object.write_bytes(b"sentinel")
            atomic_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/" + atomic_source.relative_to(REPO_ROOT).as_posix(),
                    "-o",
                    "/" + atomic_object.relative_to(REPO_ROOT).as_posix(),
                ],
                180,
            )
            self.assertEqual(atomic_result.returncode, 1)
            self.assertEqual(atomic_result.stdout, "")
            self.assertIn(
                "atomic floating logical operands are outside this body slice",
                atomic_result.stderr,
            )
            self.assertEqual(atomic_object.read_bytes(), b"sentinel")

            precise_literal_failure = root / "too-precise.cc"
            precise_literal_output = root / "too-precise.o"
            precise_literal_failure.write_text(
                precise_literal_failure_text,
                encoding="utf-8",
                newline="\n",
            )
            precise_literal_output.write_bytes(b"sentinel")
            precise_literal_result = runner.run(
                frozen.tools["cupidc"],
                [
                    "--root",
                    REPO_ROOT,
                    "--gnu",
                    "--freestanding",
                    "-c",
                    "/"
                    + precise_literal_failure.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                    "-o",
                    "/"
                    + precise_literal_output.relative_to(
                        REPO_ROOT
                    ).as_posix(),
                ],
                180,
            )
            self.assertEqual(precise_literal_result.returncode, 1)
            self.assertEqual(precise_literal_result.stdout, "")
            self.assertIn(
                "decimal floating constant exceeds the supported precision",
                precise_literal_result.stderr,
            )
            self.assertEqual(
                precise_literal_output.read_bytes(), b"sentinel"
            )

    def test_checked_seed_disassembles_typed_raw_ranges_and_legacy_modes(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-raw-ranges-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            mixed = root / "typed-ranges.bin"
            mixed.write_bytes(
                bytes(
                    [
                        0xB8,
                        0x34,
                        0x12,
                        0x00,
                        0x00,
                        0x90,
                        0xC3,
                        0xB8,
                        0x78,
                        0x56,
                        0x34,
                        0x12,
                        0xB8,
                        0xCD,
                        0xAB,
                        0xC3,
                    ]
                )
            )
            code_only = root / "legacy-modes.bin"
            code_only.write_bytes(
                bytes(
                    [
                        0xB8,
                        0x34,
                        0x12,
                        0xB8,
                        0x78,
                        0x56,
                        0x34,
                        0x12,
                        0xB8,
                        0xCD,
                        0xAB,
                        0xC3,
                    ]
                )
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            typed = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--raw",
                    "--mode=16",
                    "--range-at=3:data",
                    "--range-at=7:32",
                    "--range-at=12:16",
                    "--base=0x7c00",
                    mixed,
                ],
                60,
            )
            self.assertEqual(typed.returncode, 0, typed.stderr)
            self.assertEqual(typed.stderr, "")
            self.assertIn("00007C00", typed.stdout)
            self.assertIn("mov ax, 0x1234", typed.stdout)
            self.assertIn("00007C03", typed.stdout)
            self.assertIn("db 0x00, 0x00, 0x90, 0xC3", typed.stdout)
            self.assertNotIn("add byte", typed.stdout)
            self.assertIn("00007C07", typed.stdout)
            self.assertIn("mov eax, 0x12345678", typed.stdout)
            self.assertIn("00007C0C", typed.stdout)
            self.assertIn("mov ax, 0xABCD", typed.stdout)

            legacy = runner.run(
                frozen.tools["cupiddis"],
                [
                    "--raw",
                    "--mode=16",
                    "--mode-at=3:32",
                    "--mode-at=8:16",
                    "--base=0x7c00",
                    code_only,
                ],
                60,
            )
            self.assertEqual(legacy.returncode, 0, legacy.stderr)
            self.assertEqual(legacy.stderr, "")
            self.assertIn("mov eax, 0x12345678", legacy.stdout)
            self.assertIn("00007C08", legacy.stdout)

    def test_checked_seed_generates_canonical_install_sources(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        expected_bin = (
            "/* Auto-generated -- do not edit. */\n"
            "/* Lists all embedded CupidC programs from bin/ directory */\n"
            '#include "ramfs.h"\n'
            '#include "types.h"\n'
            '#include "../drivers/serial.h"\n'
            "extern const char _binary_bin_hello_cc_start[];\n"
            "extern const char _binary_bin_hello_cc_end[];\n"
            "void install_bin_programs(void *fs_private);\n"
            "void install_bin_programs(void *fs_private) {\n"
            "    { uint32_t sz = (uint32_t)(_binary_bin_hello_cc_end - "
            "_binary_bin_hello_cc_start); ramfs_add_file(fs_private, "
            '"bin/hello.cc", _binary_bin_hello_cc_start, sz); '
            'serial_printf("[kernel] Installed /bin/hello.cc (%u bytes)'
            '\\n", sz); }\n'
            "}\n"
        ).encode("utf-8")
        expected_docs = (
            "/* Auto-generated -- do not edit. */\n"
            "/* Lists all embedded CupidDoc files from cupidos-txt/ "
            "directory */\n"
            '#include "homefs.h"\n'
            '#include "ramfs.h"\n'
            '#include "types.h"\n'
            '#include "vfs.h"\n'
            '#include "../drivers/serial.h"\n'
            "extern const char "
            "_binary_cupidos_txt_00INDEX_CTXT_start[];\n"
            "extern const char _binary_cupidos_txt_00INDEX_CTXT_end[];\n"
            "static void install_home_asset(const char *path, const char "
            "*data, uint32_t size) {\n"
            "    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);\n"
            '    if (fd < 0) { serial_printf("[kernel] Failed to open %s '
            '(%d)\\n", path, fd); return; }\n'
            "    uint32_t off = 0;\n"
            "    while (off < size) {\n"
            "        int n = vfs_write(fd, data + off, size - off);\n"
            "        if (n <= 0) break;\n"
            "        off += (uint32_t)n;\n"
            "    }\n"
            "    vfs_close(fd);\n"
            '    serial_printf("[kernel] Installed %s (%u bytes)\\n", '
            "path, off);\n"
            "}\n"
            "void install_docs_programs(void *fs_private);\n"
            "void install_docs_programs(void *fs_private) {\n"
            "    { uint32_t sz = (uint32_t)("
            "_binary_cupidos_txt_00INDEX_CTXT_end - "
            "_binary_cupidos_txt_00INDEX_CTXT_start); "
            'ramfs_add_file(fs_private, "docs/00INDEX.ctxt", '
            "_binary_cupidos_txt_00INDEX_CTXT_start, sz); "
            'serial_printf("[kernel] Installed /docs/00INDEX.ctxt '
            '(%u bytes)\\n", sz); }\n'
            "    homefs_seed_begin();\n"
            "    homefs_seed_end();\n"
            "}\n"
        ).encode("utf-8")
        expected_demos = (
            "/* Auto-generated -- do not edit. */\n"
            "/* Lists all embedded CupidASM demos from demos/ directory */\n"
            '#include "ramfs.h"\n'
            '#include "types.h"\n'
            '#include "../drivers/serial.h"\n'
            "extern const char _binary_demos_hello_asm_start[];\n"
            "extern const char _binary_demos_hello_asm_end[];\n"
            "void install_demo_programs(void *fs_private);\n"
            "void install_demo_programs(void *fs_private) {\n"
            "    { uint32_t sz = (uint32_t)(_binary_demos_hello_asm_end - "
            "_binary_demos_hello_asm_start); ramfs_add_file(fs_private, "
            '"demos/hello.asm", _binary_demos_hello_asm_start, sz); '
            'serial_printf("[kernel] Installed /demos/hello.asm (%u bytes)'
            '\\n", sz); ramfs_add_file(fs_private, '
            '"docs/demos/hello.asm", _binary_demos_hello_asm_start, sz); '
            'serial_printf("[kernel] Installed /docs/demos/hello.asm '
            '(%u bytes)\\n", sz); }\n'
            "}\n"
        ).encode("utf-8")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-install-source-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            (root / "bin").mkdir()
            (root / "cupidos-txt").mkdir()
            (root / "demos").mkdir()
            (root / "bin" / "hello.cc").write_text(
                "int main(void) { return 0; }\n",
                encoding="utf-8",
                newline="\n",
            )
            (root / "cupidos-txt" / "00INDEX.CTXT").write_text(
                "Index\n", encoding="utf-8", newline="\n"
            )
            (root / "demos" / "hello.asm").write_text(
                "ret\n", encoding="utf-8", newline="\n"
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(root)
            cases = (
                (
                    "bin",
                    ["--bin", "bin/hello.cc"],
                    expected_bin,
                ),
                (
                    "docs",
                    ["--ctxt", "cupidos-txt/00INDEX.CTXT"],
                    expected_docs,
                ),
                (
                    "demos",
                    ["--demos", "demos/hello.asm"],
                    expected_demos,
                ),
            )
            for mode, arguments, expected in cases:
                with self.subTest(mode=mode):
                    output = root / f"{mode}-install.cc"
                    result = runner.run(
                        frozen.tools["cupidobj"],
                        [
                            "install-source",
                            mode,
                            *arguments,
                            "-o",
                            output,
                        ],
                        60,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(output.read_bytes(), expected)

            sentinel = root / "invalid-install.cc"
            sentinel.write_bytes(b"sentinel")
            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "demos",
                    "--demos",
                    "bin/hello.cc",
                    "-o",
                    sentinel,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("must match demos/NAME.asm", rejected.stderr)
            self.assertEqual(sentinel.read_bytes(), b"sentinel")

    def test_checked_seed_enforces_install_request_bounds_order_and_symbols(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-install-contract-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            (root / "bin" / "browser").mkdir(parents=True)
            (root / "cupidos-txt").mkdir()
            for name in (
                "first.png",
                "second.jpeg",
                "third.bmp",
                "fourth.jpg",
                "fifth.bmp",
            ):
                (root / name).write_bytes(b"asset")
            boundary_paths = [
                f"bin/program_{index}.cc" for index in range(513)
            ]
            for relative in boundary_paths:
                (root / relative).write_text(
                    "int main(void) { return 0; }\n",
                    encoding="utf-8",
                    newline="\n",
                )
            for relative in (
                "bin/browser_alpha.cc",
                "bin/browser/alpha.cc",
                "cupidos-txt/a-b.CTXT",
                "cupidos-txt/a_b.CTXT",
                "a-b.bmp",
                "a_b.bmp",
                "shared.bmp",
            ):
                path = root / relative
                path.write_bytes(b"fixture")
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(root)

            ordered = root / "ordered-home-install.cc"
            result = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "docs",
                    "--home-assets",
                    "first.png",
                    "second.jpeg",
                    "third.bmp",
                    "fourth.jpg",
                    "fifth.bmp",
                    "-o",
                    ordered,
                ],
                60,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            output = ordered.read_text(encoding="utf-8")
            entries = (
                'install_home_asset("/home/first.png"',
                'install_home_asset("/home/second.jpeg"',
                'install_home_asset("/home/third.bmp"',
                'install_home_asset("/home/fourth.jpg"',
                'install_home_asset("/home/fifth.bmp"',
            )
            positions = [output.index(entry) for entry in entries]
            self.assertEqual(positions, sorted(positions))

            boundary = root / "boundary-install.cc"
            accepted = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "bin",
                    "--bin",
                    *boundary_paths[:512],
                    "-o",
                    boundary,
                ],
                60,
            )
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(accepted.stdout, "")
            self.assertEqual(accepted.stderr, "")
            self.assertGreater(boundary.stat().st_size, 0)

            alias = root / "shared-alias-install.cc"
            shared = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "docs",
                    "--doc-assets",
                    "shared.bmp",
                    "--home-assets",
                    "shared.bmp",
                    "-o",
                    alias,
                ],
                60,
            )
            self.assertEqual(shared.returncode, 0, shared.stderr)
            self.assertEqual(shared.stdout, "")
            self.assertEqual(shared.stderr, "")
            self.assertGreater(alias.stat().st_size, 0)

            collision_cases = (
                (
                    "bin",
                    [
                        "--bin",
                        "bin/browser_alpha.cc",
                        "--browser",
                        "bin/browser/alpha.cc",
                    ],
                ),
                (
                    "docs",
                    [
                        "--ctxt",
                        "cupidos-txt/a-b.CTXT",
                        "cupidos-txt/a_b.CTXT",
                    ],
                ),
                (
                    "docs",
                    [
                        "--doc-assets",
                        "a-b.bmp",
                        "--home-assets",
                        "a_b.bmp",
                    ],
                ),
            )
            collision_output = root / "collision-install.cc"
            for mode, arguments in collision_cases:
                with self.subTest(mode=mode, arguments=arguments):
                    collision_output.write_bytes(b"sentinel")
                    collision = runner.run(
                        frozen.tools["cupidobj"],
                        [
                            "install-source",
                            mode,
                            *arguments,
                            "-o",
                            collision_output,
                        ],
                        60,
                    )
                    self.assertEqual(collision.returncode, 1)
                    self.assertEqual(collision.stdout, "")
                    self.assertIn("same binary symbol", collision.stderr)
                    self.assertEqual(
                        collision_output.read_bytes(), b"sentinel"
                    )

            sentinel = root / "oversized-install.cc"
            sentinel.write_bytes(b"sentinel")
            rejected = runner.run(
                frozen.tools["cupidobj"],
                [
                    "install-source",
                    "bin",
                    "--bin",
                    *boundary_paths,
                    "-o",
                    sentinel,
                ],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn("exceeds 512 paths", rejected.stderr)
            self.assertEqual(sentinel.read_bytes(), b"sentinel")

    def test_checked_seed_generates_kernel_symbol_source_transactionally(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-ksyms-source-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            symbols = root / "kernel.symbols"
            output = root / "ksyms_data.cc"
            symbol_text = (
                "00102000 T second\n"
                "00101000 t first\n"
                "00101000 W duplicate\n"
                "         U unresolved\n"
                "00103000 D data_only\n"
            )
            symbols.write_text(symbol_text, encoding="ascii", newline="\n")
            expected = hostbuild._render_ksyms_source(
                hostbuild.build_ksyms_blob(
                    hostbuild._parse_nm_symbols(symbol_text)
                )
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(root)

            generated = runner.run(
                frozen.tools["cupidobj"],
                ["ksyms-source", str(symbols), "-o", str(output)],
                60,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            self.assertEqual(output.read_bytes(), expected)

            symbols.write_text(
                "00101000 T valid\nnot-an-address T broken\n",
                encoding="ascii",
                newline="\n",
            )
            output.write_bytes(b"existing generated source")
            rejected = runner.run(
                frozen.tools["cupidobj"],
                ["ksyms-source", str(symbols), "-o", str(output)],
                60,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertEqual(rejected.stdout, "")
            self.assertIn(":2:0: error CT8000002:", rejected.stderr)
            self.assertEqual(output.read_bytes(), b"existing generated source")

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

    def _assert_checked_seed_emits_complete_unchanged_kernel_object(
        self,
        source_path: str,
        source_size: int,
        source_newlines: int,
        source_sha256: str,
        object_size: int,
        object_sha256: str,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        source = REPO_ROOT / source_path
        source_bytes = source.read_bytes()
        self.assertEqual(len(source_bytes), source_size)
        self.assertEqual(source_bytes.count(b"\n"), source_newlines)
        self.assertEqual(
            hashlib.sha256(source_bytes).hexdigest(),
            source_sha256,
        )
        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        profile = next(
            item
            for item in audit["contracts"][
                "c_preprocessor_translation_units"
            ]["profiles"]
            if item["name"] == "KERNEL_I386"
        )
        arguments: list[str | Path] = [
            "--root",
            REPO_ROOT,
            "--gnu",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-kernel-source-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            frozen = freeze_seed_inputs(
                SEED_MANIFEST, root / "seed"
            )
            runner = ToolRunner(REPO_ROOT)
            objects = []
            for index in range(2):
                output = root / f"source-{index}.o"
                logical_output = "/" + output.relative_to(
                    REPO_ROOT
                ).as_posix()
                result = runner.run(
                    frozen.tools["cupidc"],
                    [
                        *arguments,
                        "-c",
                        "/" + source_path,
                        "-o",
                        logical_output,
                    ],
                    180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                image = output.read_bytes()
                self.assertEqual(image[:7], b"\x7fELF\x01\x01\x01")
                objects.append(image)
            self.assertEqual(objects[0], objects[1])
            self.assertEqual(
                (
                    len(objects[0]),
                    hashlib.sha256(objects[0]).hexdigest(),
                ),
                (
                    object_size,
                    object_sha256,
                ),
            )
        self.assertEqual(source.read_bytes(), source_bytes)

    def test_checked_seed_emits_complete_active_libm_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/cpu/libm.cc",
            43736,
            1500,
            "baffe801c7573b8500c60251298a753f60732608d58443178be8ce9ab809ef93",
            16164,
            "c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4",
        )

    def test_checked_seed_emits_complete_unchanged_kernel_entry_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/core/kernel.cc",
            31278,
            952,
            "258bb51ea67e3159add45400c2652c2e1674dc61f788f747104a63353404a276",
            25972,
            "90fc64e3e92e2a1fac573c7f983f27270ab5b47c5eba6164b5703ad317003ed6",
        )

    def test_checked_seed_emits_page_aligned_kernel_stack_top(self):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        source_text = (
            "extern unsigned int _kernel_end;\n"
            "extern unsigned int _bss_start;\n"
            "void kmain(void);\n"
            "void _start(void) "
            "__attribute__((section(\".text.start\")));\n"
            "void _start(void) {\n"
            "  asm volatile("
            "\"mov $0x1100000, %%esp\\nmov %%esp, %%ebp\\n"
            "mov $_bss_start, %%edi\\nmov $_kernel_end, %%ecx\\n"
            "sub %%edi, %%ecx\\nshr $2, %%ecx\\n"
            "xor %%eax, %%eax\\ncld\\nrep stosl\\n\""
            " : : : \"eax\", \"ecx\", \"edi\", \"memory\");\n"
            "  kmain();\n"
            "}\n"
        )
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-stack-top-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "kernel-entry.cc"
            source.write_text(
                source_text,
                encoding="utf-8",
                newline="\n",
            )
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            images = []
            for index in range(2):
                output = root / f"kernel-entry-{index}.o"
                result = runner.run(
                    frozen.tools["cupidc"],
                    [
                        "--root",
                        REPO_ROOT,
                        "--gnu",
                        "--freestanding",
                        "-c",
                        "/" + source.relative_to(REPO_ROOT).as_posix(),
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    ],
                    180,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                images.append(output.read_bytes())
            self.assertEqual(images[0], images[1])
            self.assertEqual(images[0][:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(
                images[0].count(b"\xbc\x00\x00\x10\x01"),
                1,
            )
            self.assertNotIn(b"\xbc\x00\x00\xf0\x00", images[0])

    def test_checked_seed_emits_complete_unchanged_simd_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/cpu/simd.cc",
            13971,
            487,
            "5b4c892322d41e901cdeda34817f79a6547139a2ed703fb6a90eb4b06d34692d",
            8768,
            "fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4",
        )

    def test_checked_seed_emits_exact_doom_compatibility_objects_twice(
        self,
    ):
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        profile = next(
            item
            for item in audit["contracts"][
                "c_preprocessor_translation_units"
            ]["profiles"]
            if item["name"] == "DOOM_COMPAT_I386"
        )
        self.assertEqual(profile["tracked_translation_units"], 3)
        self.assertTrue(profile["gnu_extensions"])
        self.assertFalse(profile["hosted_environment"])
        self.assertTrue(profile["implicit_function_declarations"])
        self.assertTrue(profile["compatibility_pointer_conversions"])
        self.assertEqual(profile["forced_includes"], [])
        self.assertEqual(len(profile["include_roots"]), 20)
        self.assertEqual(
            profile["macro_actions"],
            [
                {"name": "__GNUC__", "replacement": "1"},
                {"name": "__SIZEOF_POINTER__", "replacement": "4"},
                {
                    "name": "__ORDER_LITTLE_ENDIAN__",
                    "replacement": "1234",
                },
                {
                    "name": "__ORDER_BIG_ENDIAN__",
                    "replacement": "4321",
                },
                {
                    "name": "__ORDER_PDP_ENDIAN__",
                    "replacement": "3412",
                },
                {
                    "name": "__BYTE_ORDER__",
                    "replacement": "__ORDER_LITTLE_ENDIAN__",
                },
                {"name": "__SSE2__", "replacement": "1"},
            ],
        )
        arguments: list[str | Path] = [
            "--root",
            REPO_ROOT,
            "--gnu",
            "--doom-compat",
            "--freestanding",
        ]
        both_forms = (
            "(CTOOL_C_PP_INCLUDE_QUOTED | "
            "CTOOL_C_PP_INCLUDE_ANGLE)"
        )
        for include_root in profile["include_roots"]:
            self.assertEqual(include_root["forms"], both_forms)
            arguments.extend(["-I", include_root["path"]])
        for action in profile["macro_actions"]:
            if action["name"] == "__SIZEOF_POINTER__":
                self.assertEqual(action["replacement"], "4")
                continue
            arguments.append(
                "-D" + action["name"] + "=" + action["replacement"]
            )

        expected_objects = {
            "/kernel/doom/dglibc.cc": (
                67155,
                2078,
                "6a56616dff23b608260d003b09634c2c2"
                "2e0220d5b31a1332db0859d152babb2",
                93332,
                "e2496b01c93a7858a0c035b53aea0ad8"
                "34d95d2be3f7ae49574d1759ebec34d6",
            ),
            "/kernel/doom/doom_libc_stubs.cc": (
                10516,
                360,
                "c19a5dbcd96fb9dc9e9a6f0fef20bb0"
                "5e18502e2a5d058d4737d85886b7ccbea",
                17084,
                "a2cef82df789e5770dc91bbe5bb7b4a4"
                "1dfcbe788f587eec6fc0f6265433c319",
            ),
            "/kernel/doom/doomgeneric_cupidos.cc": (
                13788,
                409,
                "13c9bdfe659443e227d9cec6a770e9bce"
                "26714fb83a62b2338d8b6a295d4e725",
                10484,
                "8a15d86da5a31e57e9b11f75d47daa90"
                "f6bddb43994ebf6a7c315eae9639fafe",
            ),
        }
        tracked_sources = sorted(
            "/" + transform["inputs"][0]
            for transform in audit["build"]["transforms"]
            if transform["inputs"]
            and transform["recipe"]
            == [
                "$(CUPIDC_KERNEL_COMPILE) --profile doom-compat "
                f"--source {transform['inputs'][0]} "
                f"--output {transform['output']}"
            ]
        )
        self.assertEqual(tracked_sources, sorted(expected_objects))
        with tempfile.TemporaryDirectory(
            prefix=".checked-seed-doom-compat-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            frozen = freeze_seed_inputs(SEED_MANIFEST, root / "seed")
            runner = ToolRunner(REPO_ROOT)
            for source, expected in expected_objects.items():
                source_bytes = (REPO_ROOT / source.lstrip("/")).read_bytes()
                self.assertEqual(
                    (
                        len(source_bytes),
                        source_bytes.count(b"\n"),
                        hashlib.sha256(source_bytes).hexdigest(),
                    ),
                    expected[:3],
                )
                images = []
                for index in range(2):
                    output = root / (
                        Path(source).stem + f"-{index}.o"
                    )
                    logical_output = "/" + output.relative_to(
                        REPO_ROOT
                    ).as_posix()
                    result = runner.run(
                        frozen.tools["cupidc"],
                        [
                            *arguments,
                            "-c",
                            source,
                            "-o",
                            logical_output,
                        ],
                        180,
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        f"{source}: {result.stderr}",
                    )
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    image = output.read_bytes()
                    self.assertEqual(
                        image[:7], b"\x7fELF\x01\x01\x01"
                    )
                    images.append(image)
                self.assertEqual(images[0], images[1])
                self.assertEqual(
                    (
                        len(images[0]),
                        hashlib.sha256(images[0]).hexdigest(),
                    ),
                    expected[3:],
                )

    def test_changed_seed_byte_is_rejected_before_execution(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            compiler = copied_seed / "cupidc.elf"
            image = bytearray(compiler.read_bytes())
            image[-1] ^= 0x01
            compiler.write_bytes(image)

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(copied_seed / "manifest.json"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "bootstrap seed verification failed: "
            "SHA-256 differs for cupidc.elf\n",
        )

    def test_seed_with_a_wrong_elf_entry_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = self._legacy_linux_manifest_fixture(
                json.loads(manifest_path.read_text(encoding="utf-8"))
            )
            (copied_seed / "cupidbuild.elf").unlink()
            assembler = copied_seed / "cupidasm.elf"
            image = bytearray(assembler.read_bytes())
            image[24:28] = (0x08048004).to_bytes(4, "little")
            assembler.write_bytes(image)
            artifact = next(
                item
                for item in manifest["artifacts"]
                if item["name"] == "cupidasm"
            )
            artifact["sha256"] = hashlib.sha256(image).hexdigest()
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "bootstrap seed verification failed: "
            "cupidasm.elf entry is 0x08048004, expected 0x08048000\n",
        )

    def test_seed_with_an_unmapped_elf_entry_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = self._legacy_linux_manifest_fixture(
                json.loads(manifest_path.read_text(encoding="utf-8"))
            )
            (copied_seed / "cupidbuild.elf").unlink()
            assembler = copied_seed / "cupidasm.elf"
            image = bytearray(assembler.read_bytes())
            program_offset = int.from_bytes(image[28:32], "little")
            image[program_offset + 8 : program_offset + 12] = (
                0x09000000
            ).to_bytes(4, "little")
            image[program_offset + 12 : program_offset + 16] = (
                0x09000000
            ).to_bytes(4, "little")
            assembler.write_bytes(image)
            artifact = next(
                item
                for item in manifest["artifacts"]
                if item["name"] == "cupidasm"
            )
            artifact["sha256"] = hashlib.sha256(image).hexdigest()
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "bootstrap seed verification failed: "
            "cupidasm.elf entry is not in executable file bytes\n",
        )

    def test_build_plan_digest_rejects_unlisted_plan_data(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["build_plan"]["unlisted_input"] = "tampered"
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "bootstrap seed verification failed: "
            "build plan SHA-256 differs\n",
        )

    def test_changed_producer_lineage_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["provenance"]["producer_lineage"]["c"] = (
                "unrecorded compiler"
            )
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "bootstrap seed verification failed: "
            "producer lineage differs\n",
        )

    def test_changed_exact_provenance_is_rejected(self):
        cases = (
            (
                "source revision",
                "source_revision",
                "b04c5b5ead1be504669ad8f0f84b3531eda3df9c",
                "source revision differs",
            ),
            (
                "seed generation",
                "seed_generation",
                "generation-one",
                "seed generation differs",
            ),
            (
                "source input count",
                "source_input_count",
                49,
                "source input count differs",
            ),
            (
                "floating source input count",
                "source_input_count",
                50.0,
                "source input count differs",
            ),
            (
                "source snapshot",
                "source_snapshot_sha256",
                "0" * 64,
                "source snapshot differs",
            ),
            (
                "fixed-point result",
                "fixed_point_result",
                "not-run",
                "seed lacks passing fixed-point provenance",
            ),
        )
        for label, field, value, expected in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="cupid-bootstrap-seed-"
            ) as temporary:
                copied_seed = Path(temporary) / "i386-linux"
                shutil.copytree(SEED_MANIFEST.parent, copied_seed)
                manifest_path = copied_seed / "manifest.json"
                manifest = json.loads(
                    manifest_path.read_text(encoding="utf-8")
                )
                manifest["provenance"][field] = value
                manifest_path.write_text(
                    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )

                result = subprocess.run(
                    [
                        sys.executable,
                        str(BOOTSTRAP_TOOL),
                        "verify",
                        "--manifest",
                        str(manifest_path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                result.stderr,
                f"bootstrap seed verification failed: {expected}\n",
            )

    def test_changed_fixed_point_command_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-seed-"
        ) as temporary:
            copied_seed = Path(temporary) / "i386-linux"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["provenance"]["fixed_point_command"] = (
                "unrecorded fixed-point check"
            )
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "bootstrap seed verification failed: "
            "fixed-point command differs\n",
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
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    str(output),
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
                timeout=2400,
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
                report["comparisons"],
                {
                    "all_equal": True,
                    "c_objects": 22,
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
                    "failure_cases": 28,
                    "help_cases": 6,
                    "success_cases": 35,
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
                                "8ba6e2f7ca3af67775dfdd350767e737fcf66dd9a1d8fececbdce756df7ced37"
                            ),
                            "output_size": 364,
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
                                "ef2fbefdcc83482a84d4514e40f078fa72f0d91efedb8ef592f0bd02c9764661"
                            ),
                            "output_size": 32768,
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
                        "edbef4e4ed76489e555d70f23822922c701ab1ef0d9f4c2e18d8f7519c5e5748"
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
            self.assertEqual(
                windows_cupiddis["artifacts"]["stage-three-image"],
                {
                    "sha256": (
                        "588485d496209eecf437e6f6fc9d02474d5c4ac1f236af86bdaad9f3f2d705ce"
                    ),
                    "size": 516608,
                },
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
            expected_native_images = {
                "cupidasm": {
                    "sha256": (
                        "9c50e204262a0b05b12d4fc0924670c66092d053ad12b99134ab79a254ef07ae"
                    ),
                    "size": 479744,
                },
                "cupidc": {
                    "sha256": (
                        "fb7efa82fdcffa6a36a5c44bb83abe5b6a10ce7487c946eb3fab206e436b8522"
                    ),
                    "size": 2620416,
                },
                "cupidld": {
                    "sha256": (
                        "aaa7b51a290646ef1d972f4904b1ed176a4dc912e53c1bc4cbdd8d1e39d8495f"
                    ),
                    "size": 296960,
                },
                "cupidobj": {
                    "sha256": (
                        "b6f6a5b66f8e2bcb4b779a16428d7b77a956113c5ca301344537b35839611572"
                    ),
                    "size": 375808,
                },
            }
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
                    self.assertEqual(
                        artifacts["stage-three-image"],
                        expected_native_images[tool_name],
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
                REPO_ROOT, candidate_plan
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
            self.assertEqual(
                initial_matches,
                {name: True for name in CANDIDATE_TOOL_NAMES},
            )
            self.assertEqual(
                report["source_inputs"]["count"],
                PROMOTED_SOURCE_INPUT_COUNT,
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
                PROMOTED_SOURCE_INPUT_COUNT,
            )
            for tool_name in CANDIDATE_TOOL_NAMES:
                stage_three = output / "stage-three" / f"{tool_name}.elf"
                stage_four = output / "stage-four" / f"{tool_name}.elf"
                self.assertEqual(
                    stage_four.read_bytes(), stage_three.read_bytes()
                )


if __name__ == "__main__":
    unittest.main()

## @file
#  @brief Share manifest, ELF, and PE fixtures across the bootstrap suite.
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import struct
import tempfile
from pathlib import Path
from unittest import mock

from tools.bootstrap_toolchain import (
    CANDIDATE_SOURCES,
    PROMOTED_CUPIDOBJ_LINK,
    EXPECTED_LINKS,
    PROMOTED_LINUX_PLAN_SHA256,
    PROMOTED_SOURCE_INPUT_COUNT,
    PROMOTED_SOURCE_REVISION,
    PROMOTED_SOURCE_SNAPSHOT_SHA256,
    PROMOTED_SOURCES,
    PROMOTED_CUPIDBUILD_LINK,
    PROMOTED_SEED_SCHEMA,
    PROMOTED_WINDOWS_PLAN_SHA256,
    PROMOTED_WINDOWS_SEED_SCHEMA,
    PROMOTION_PARENT_LINUX_MANIFEST_SHA256,
    PROMOTION_PARENT_WINDOWS_MANIFEST_SHA256,
    PROMOTION_PARENT_SOURCE_REVISION,
    SEED_SCHEMA,
    SEED_SOURCE_REVISION,
    SEED_SOURCE_SNAPSHOT_SHA256,
    ToolRunner,
    _build_plan_sha256,
    _code_anchor_executable_payload,
    _local_target_executable_payload,
    _local_target_object_payload,
    _windows_imports,
    _promoted_windows_imports,
    freeze_seed_inputs,
    run_seed_tool,
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


CURRENT_SOURCE_HEAD_INPUT_COUNT = 124
CURRENT_SOURCE_HEAD_SNAPSHOT_SHA256 = (
    "c966ae14e5c020a4aead6f0c6e5832de6a5a2db70b62222a536b49c3238f51f2"
)


def _fixture_windows_imports(names):
    return {
        name: [{"library": library, "procedure": procedure, "slot": "__imp_" + procedure}
               for library, procedures in _windows_imports(name) for procedure in procedures]
        for name in names
    }


def _promoted_windows_profile_flags(plan_sha256, source_input_count):
    coordinator = dict(_promoted_windows_imports("cupidbuild", plan_sha256, source_input_count))
    compiler = dict(_promoted_windows_imports("cupidc", plan_sha256, source_input_count))
    return {
        "long_paths": "GetFullPathNameW" in compiler["KERNEL32.dll"],
        "user_link_aliases": "GetFinalPathNameByHandleW" in coordinator["KERNEL32.dll"],
    }


class BootstrapSeedFixtures:
    def _assert_file_artifact_identity(self, reported, path):
        payload = path.read_bytes()
        self.assertEqual(
            reported,
            {"sha256": hashlib.sha256(payload).hexdigest(), "size": len(payload)},
        )

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
        promoted_names = {name for name, _, _ in CANDIDATE_SOURCES}
        plan["sources"] = [
            source for source in plan["sources"]
            if source["name"] not in promoted_names
        ]
        del plan["links"]["cupidbuild"]
        plan["links"]["cupidobj"] = list(EXPECTED_LINKS["cupidobj"])
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
                    PROMOTION_PARENT_WINDOWS_MANIFEST_SHA256
                ),
                "parent_execution_seed_source_revision": (
                    PROMOTION_PARENT_SOURCE_REVISION
                ),
                "parent_plan_seed_manifest_sha256": (
                    PROMOTION_PARENT_LINUX_MANIFEST_SHA256
                ),
                "parent_plan_seed_source_revision": (
                    PROMOTION_PARENT_SOURCE_REVISION
                ),
                "producer_lineage": lineage,
                "source_input_count": PROMOTED_SOURCE_INPUT_COUNT,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        else:
            plan = manifest["build_plan"]
            if "cupidbuild" not in plan["links"]:
                plan["sources"].extend(
                    {"name": name, "path": path, "gnu_extensions": gnu}
                    for name, path, gnu in PROMOTED_SOURCES
                )
                plan["links"]["cupidbuild"] = list(PROMOTED_CUPIDBUILD_LINK)
                plan["links"]["cupidobj"] = list(
                    PROMOTED_CUPIDOBJ_LINK
                )
            manifest["schema"] = PROMOTED_SEED_SCHEMA
            manifest["build_plan"] = plan
            manifest["build_plan_sha256"] = _build_plan_sha256(plan)
            manifest["provenance"] = {
                "artifact_generation": "paired-stage-four-six-tool",
                "fixed_point_command": "make bootstrap-from-seed",
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    PROMOTION_PARENT_LINUX_MANIFEST_SHA256
                ),
                "parent_seed_source_revision": (
                    PROMOTION_PARENT_SOURCE_REVISION
                ),
                "producer_lineage": lineage,
                "seed_generation": "stage-four",
                "source_input_count": PROMOTED_SOURCE_INPUT_COUNT,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        return manifest

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
            git_link = REPO_ROOT / ".git"
            git_directory = (
                git_link.read_text(encoding="utf-8").strip()
                if git_link.is_file()
                else ""
            )
            windows_link = re.fullmatch(
                r"gitdir: ([A-Za-z]:[/\\].+)", git_directory
            )
            if windows_link is None:
                raise AssertionError(native_probe.stderr)
            translated = subprocess.run(
                ["wslpath", "-u", windows_link.group(1)],
                text=True,
                capture_output=True,
            )
            translated_path = translated.stdout.strip()
            if translated.returncode != 0 or not translated_path.startswith(
                "/"
            ):
                raise AssertionError(
                    translated.stderr or "invalid translated Git directory"
                )
            command_prefix = [
                "git",
                "--git-dir",
                translated_path,
                "--work-tree",
                str(REPO_ROOT),
            ]
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

    @classmethod
    def _minimal_cupidbuild_profile_pe32(
        cls, profile: tuple[tuple[str, tuple[str, ...]], ...]
    ) -> bytearray:
        image = cls._minimal_pe32()
        idata_rva = 0x2000
        idata_offset = 0x400
        descriptor_size = (len(profile) + 1) * 20
        idata = bytearray(descriptor_size)
        lookup_offsets: list[int] = []
        iat_offsets: list[int] = []
        library_offsets: list[int] = []
        procedure_offsets: list[list[int]] = []

        for _library, procedures in profile:
            lookup_offsets.append(len(idata))
            idata.extend(b"\0" * ((len(procedures) + 1) * 4))
        first_iat_offset = len(idata)
        for _library, procedures in profile:
            iat_offsets.append(len(idata))
            idata.extend(b"\0" * ((len(procedures) + 1) * 4))
        iat_size = len(idata) - first_iat_offset
        for library, _procedures in profile:
            library_offsets.append(len(idata))
            idata.extend(library.encode("ascii") + b"\0")
        for _library, procedures in profile:
            offsets: list[int] = []
            for procedure in procedures:
                if len(idata) % 2 != 0:
                    idata.append(0)
                offsets.append(len(idata))
                idata.extend(b"\0\0" + procedure.encode("ascii") + b"\0")
            procedure_offsets.append(offsets)

        for library_index, (_library, procedures) in enumerate(profile):
            struct.pack_into(
                "<IIIII",
                idata,
                library_index * 20,
                idata_rva + lookup_offsets[library_index],
                0,
                0,
                idata_rva + library_offsets[library_index],
                idata_rva + iat_offsets[library_index],
            )
            for procedure_index in range(len(procedures)):
                name_rva = (
                    idata_rva
                    + procedure_offsets[library_index][procedure_index]
                )
                struct.pack_into(
                    "<I",
                    idata,
                    lookup_offsets[library_index] + procedure_index * 4,
                    name_rva,
                )
                struct.pack_into(
                    "<I",
                    idata,
                    iat_offsets[library_index] + procedure_index * 4,
                    name_rva,
                )

        raw_size = (len(idata) + 0x1FF) & ~0x1FF
        image.extend(b"\0" * raw_size)
        optional = 0x98
        struct.pack_into("<H", image, 0x86, 2)
        struct.pack_into("<I", image, optional + 8, raw_size)
        struct.pack_into("<I", image, optional + 24, idata_rva)
        struct.pack_into(
            "<I", image, optional + 56, (idata_rva + len(idata) + 0xFFF) & ~0xFFF
        )
        struct.pack_into(
            "<II", image, optional + 96 + 8, idata_rva, descriptor_size
        )
        struct.pack_into(
            "<II",
            image,
            optional + 96 + 12 * 8,
            idata_rva + first_iat_offset,
            iat_size,
        )
        section = 0x1A0
        image[section : section + 8] = b".idata\0\0"
        struct.pack_into(
            "<IIII",
            image,
            section + 8,
            len(idata),
            idata_rva,
            raw_size,
            idata_offset,
        )
        struct.pack_into("<I", image, section + 36, 0xC0000040)
        image[idata_offset : idata_offset + len(idata)] = idata
        return image

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
        for name in ("path_encoding.cc", "path_encoding.h", "native_utf8.h"):
            (toolchain / name).write_text("/* fixture */\n", encoding="ascii")
        for name in ("utf8_tool_start.asm", "utf8_publication_start.asm",
                     "utf8_cupidbuild_start.asm", "windows_utf8.cc"):
            (windows / name).write_text("; fixture\n", encoding="ascii")
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

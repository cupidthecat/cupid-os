## @file
#  @brief Check frozen sources, publication, and tampered seed artifacts.
import hashlib
import json
import os
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.bootstrap_toolchain import (
    BootstrapError,
    PROMOTED_LINUX_PLAN_SHA256,
    ToolRunner,
    _validate_static_i386_pe32,
    capture_source_snapshot,
    freeze_source_inputs,
    freeze_seed_inputs,
    publish_bootstrap_outputs,
    require_frozen_source_snapshot,
    require_source_snapshot,
    verify_seed_inputs,
)
from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes

from tests.bootstrap_seed_suite_fixtures import (
    BOOTSTRAP_TOOL,
    REPO_ROOT,
    SEED_MANIFEST,
    WINDOWS_SEED_MANIFEST,
)


class BootstrapSeedCustodyCases:
    def test_linux_named_revision_inventory_translates_windows_worktree_link(
        self,
    ):
        revision = "a" * 40
        logical_path = "toolchain/ctool.cc"
        payload = b"committed source\n"
        translated_path = "/mnt/c/cupid-os/.git/worktrees/bootstrap"
        commands = []

        def run(command, *args, **kwargs):
            commands.append(command)
            if "rev-parse" in command:
                return subprocess.CompletedProcess(
                    command, 128, stdout="", stderr="invalid Windows Git path"
                )
            if command[0] == "wslpath":
                return subprocess.CompletedProcess(
                    command, 0, stdout=translated_path + "\n", stderr=""
                )
            self.assertEqual(
                command[:5],
                [
                    "git", "--git-dir", translated_path,
                    "--work-tree", str(REPO_ROOT),
                ],
            )
            return subprocess.CompletedProcess(
                command,
                0,
                stdout=(
                    (logical_path + "\n").encode()
                    if "ls-tree" in command else payload
                ),
                stderr=b"",
            )

        with mock.patch.object(os, "name", "posix"), mock.patch.object(
            Path, "is_file", return_value=True
        ), mock.patch.object(
            Path, "read_text",
            return_value="gitdir: C:/cupid-os/.git/worktrees/bootstrap\n",
        ), mock.patch.object(subprocess, "run", side_effect=run):
            inventory = self._committed_source_inventory(
                revision, {logical_path: None}
            )
        self.assertEqual(
            inventory,
            {logical_path: {
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }},
        )
        self.assertEqual(
            commands[1],
            ["wslpath", "-u", "C:/cupid-os/.git/worktrees/bootstrap"],
        )

    def test_linux_named_revision_inventory_rejects_invalid_worktree_translation(
        self,
    ):
        for link, result in (
            ("gitdir: relative/path", None),
            (
                "gitdir: C:/cupid-os/.git/worktrees/bootstrap",
                (1, "", "translation failed"),
            ),
            (
                "gitdir: C:/cupid-os/.git/worktrees/bootstrap",
                (0, "relative/path", ""),
            ),
        ):
            with self.subTest(link=link, result=result):
                commands = []

                def run(command, *args, **kwargs):
                    commands.append(command)
                    if "rev-parse" in command:
                        return subprocess.CompletedProcess(
                            command, 128, stdout="", stderr="invalid Git directory"
                        )
                    self.assertEqual(command[0], "wslpath")
                    return subprocess.CompletedProcess(
                        command, result[0], stdout=result[1], stderr=result[2]
                    )

                with mock.patch.object(os, "name", "posix"), mock.patch.object(
                    Path, "is_file", return_value=True
                ), mock.patch.object(
                    Path, "read_text", return_value=link
                ), mock.patch.object(
                    subprocess, "run", side_effect=run,
                ), self.assertRaises(AssertionError):
                    self._committed_source_inventory(
                        "a" * 40, {"toolchain/ctool.cc": None}
                    )
                self.assertEqual(len(commands), 1 if result is None else 2)

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

    def test_frozen_seed_is_independent_of_later_input_changes(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-freeze-"
        ) as temporary:
            root = Path(temporary)
            copied_seed = root / "i386-linux"
            frozen_directory = root / "frozen"
            shutil.copytree(SEED_MANIFEST.parent, copied_seed)
            original_manifest = (copied_seed / "manifest.json").read_bytes()
            original_compiler = (copied_seed / "cupidc.elf").read_bytes()
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
                frozen.tools["cupidc"].read_bytes(), original_compiler
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

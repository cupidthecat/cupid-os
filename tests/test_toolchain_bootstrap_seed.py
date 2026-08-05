import contextlib
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import hostbuild
from tools.bootstrap_toolchain import (
    BootstrapError,
    Stage,
    ToolRunner,
    WSL_PRIVATE_RUN_SCRIPT,
    bootstrap_from_seed,
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


REPO_ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP_TOOL = REPO_ROOT / "tools" / "bootstrap_toolchain.py"
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-linux"
    / "manifest.json"
)


class ToolchainBootstrapSeedCliTests(unittest.TestCase):
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
                "stage-three, behavior, bootstrap-report.json$",
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
            for name in ("stage-two", "stage-three", "behavior"):
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
            for name in ("stage-two", "stage-three", "behavior"):
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
            for name in ("stage-two", "stage-three", "behavior"):
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
                    tools={
                        name: marker
                        for name in ("cupidc", "cupidasm", "cupidld")
                    },
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

    def test_recomputed_digest_cannot_change_the_source_plan(self):
        original = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))
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
                "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc",
            )
            self.assertNotEqual(
                frozen.tools["cupidc"].read_bytes(),
                compiler.read_bytes(),
            )
            self.assertEqual(
                hashlib.sha256(
                    frozen.tools["cupidc"].read_bytes()
                ).hexdigest(),
                "03084115bcacb1987db5513c8a8be9b7d884029b03ab4b212bf40d997871ae79",
            )

    def test_wsl_runner_uses_a_private_temporary_directory(self):
        self.assertIn("umask 077", WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn("mktemp -d", WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn('chmod 700 "$private"', WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn('probe="$private/tool"', WSL_PRIVATE_RUN_SCRIPT)
        self.assertIn('rm -rf -- "$private"', WSL_PRIVATE_RUN_SCRIPT)
        self.assertNotIn("$$", WSL_PRIVATE_RUN_SCRIPT)

    def test_checked_i386_linux_seed_verifies(self):
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
            "checked i386 Linux seed: ok (5 tools)\n",
        )
        self.assertEqual(result.stderr, "")

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
            31174,
            950,
            "f882ac45e2fc9a41ce805a22a602fb4839293a755ef5fea3b7e21d159d5bbf83",
            25920,
            "ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0",
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
                13640,
                404,
                "7cc4ef8beba2fdc4664f5c7a5c18a2ef"
                "42d3a2595e78b72eef8fc9801ff175ca",
                10352,
                "53537aabdaaa5de1db63403f569253f6"
                "be829b59387bebbe853347b825050c8a",
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
            assembler = copied_seed / "cupidasm.elf"
            image = bytearray(assembler.read_bytes())
            image[24:28] = (0x08048004).to_bytes(4, "little")
            assembler.write_bytes(image)
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
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
            manifest_path = copied_seed / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
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
            environment["CC"] = "__host_c_compiler_must_not_run__"
            environment["LD"] = "__host_linker_must_not_run__"
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
                timeout=1200,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "checked i386 Linux bootstrap: ok "
                "(stage two equals stage three)\n",
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
                "c31f062fc67c78b553919c2600dd953d252cb58b",
            )
            self.assertNotIn("source_revision", report)
            self.assertEqual(
                report["build_plan_sha256"],
                "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc",
            )
            self.assertEqual(
                report["comparisons"],
                {
                    "all_equal": True,
                    "c_objects": 19,
                    "startup_objects": 1,
                    "tool_images": 5,
                },
            )
            self.assertEqual(
                report["behavior"],
                {
                    "failure_cases": 8,
                    "help_cases": 5,
                    "success_cases": 12,
                },
            )
            initial_matches = report["initial_seed_matches_stage_two"]
            self.assertEqual(
                set(initial_matches),
                {
                    "cupidasm",
                    "cupidc",
                    "cupiddis",
                    "cupidld",
                    "cupidobj",
                },
            )
            promoted_snapshot = (
                "2d2a3253a9559a7e450d3f8755bc66ca2f5e0136d41045c7aeea04949a8d177d"
            )
            self.assertEqual(
                report["source_snapshot_sha256"], promoted_snapshot
            )
            self.assertEqual(
                initial_matches,
                {
                    "cupidasm": True,
                    "cupidc": True,
                    "cupiddis": True,
                    "cupidld": True,
                    "cupidobj": True,
                },
            )
            self.assertEqual(report["source_inputs"]["count"], 41)
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
                41,
            )
            for tool_name in (
                "cupidasm",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidc",
            ):
                stage_two = output / "stage-two" / f"{tool_name}.elf"
                stage_three = output / "stage-three" / f"{tool_name}.elf"
                self.assertEqual(
                    stage_three.read_bytes(), stage_two.read_bytes()
                )


if __name__ == "__main__":
    unittest.main()

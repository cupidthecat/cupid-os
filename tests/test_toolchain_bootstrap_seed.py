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
                "f53989572cd1564a8bf91059552868ee43a1d80905986b58cd97d44949aab3a1",
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

    def test_checked_seed_emits_complete_unchanged_libm_object(self):
        self._assert_checked_seed_emits_complete_unchanged_kernel_object(
            "kernel/cpu/libm.cc",
            43736,
            1500,
            "f1c13c83b758394189cc74ed6addfd9dfa99d42064c349c548476686b26cabce",
            16164,
            "ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac",
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
                22632,
                814,
                "00229885ddcd06c12e476cc47cc24a91"
                "4053d49db9c690c8c8fea7c880b6aa9c",
                27992,
                "54ce387c7eae45d9f4ae379afdaa1109"
                "2d2dd021d4e9ca7696be5da2ff5d3dcd",
            ),
            "/kernel/doom/doom_libc_stubs.cc": (
                8099,
                288,
                "808580d6c35388304fa4a07b7c5e0e91"
                "ad4687e1a189c3959482f51e17a0ecf8",
                14352,
                "8f667113c54fa0b0d27ce83d13424206"
                "5ba5b9258324a809e11e72229752ff3b",
            ),
            "/kernel/doom/doomgeneric_cupidos.cc": (
                13521,
                400,
                "8511fd4035db73fde8147a39a92ff65f"
                "50e8097ab6f27d4ca517b9883ff15a3e",
                10232,
                "5274b91dfa7bac56cd83ff0f8096eb5a"
                "06fef5e61f91ebb3b80efacc8ad2a9cb",
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
                "af4644177c033eebda164d7893074315439df119",
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
                    "failure_cases": 6,
                    "help_cases": 5,
                    "success_cases": 10,
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
            seed_transition_snapshot = (
                "1199072a4415195a83e45c6469c79e066d445d96a884d6b0b9235cc09f035986"
            )
            if report["source_snapshot_sha256"] == seed_transition_snapshot:
                self.assertTrue(all(initial_matches.values()))
            self.assertEqual(report["source_inputs"]["count"], 40)
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
                40,
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

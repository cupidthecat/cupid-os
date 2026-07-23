import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.bootstrap_toolchain import (
    BootstrapError,
    WSL_PRIVATE_RUN_SCRIPT,
    capture_source_snapshot,
    freeze_seed_inputs,
    require_source_snapshot,
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

    def test_recomputed_digest_cannot_change_the_source_plan(self):
        original = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))
        mutations = {
            "substituted source": lambda plan: plan["sources"][1].update(
                {"path": "/toolchain/elf32.c"}
            ),
            "traversal source": lambda plan: plan["sources"][1].update(
                {"path": "/toolchain/../toolchain/ctool.c"}
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
                "7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0",
            )
            self.assertNotEqual(
                frozen.tools["cupidc"].read_bytes(),
                compiler.read_bytes(),
            )
            self.assertEqual(
                hashlib.sha256(
                    frozen.tools["cupidc"].read_bytes()
                ).hexdigest(),
                "29cd222c6e33590932457d36f3728705134c8c6750947e7cfbc4aba3b7c5500b",
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
                "d5e4ed784c54ea8dad581ac736ee8b62553627d8",
            )
            self.assertNotIn("source_revision", report)
            self.assertEqual(
                report["build_plan_sha256"],
                "7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0",
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

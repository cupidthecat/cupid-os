from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import cupidc_toolchain_contracts


EXPECTED_CONTRACTS = {
    "core",
    "cupidasm",
    "cupidasm-demos",
    "cupidasm-kernel-elf",
    "cupidc-frontend",
    "cupidc-ir",
    "cupidc-object",
    "cupidc-pp",
    "cupidc-type",
    "cupiddis",
    "cupidld",
    "cupidobj",
    "elf32",
    "x86",
    "user-syscall-abi",
}


class CupidCToolchainContractPlanTests(unittest.TestCase):
    @staticmethod
    def _write_publication(
        output: Path, payload_prefix: str = "checked"
    ) -> None:
        output.mkdir(parents=True)
        records = []
        comparisons = {}
        comparison_names = {
            plan.artifact: plan.name
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
        }
        comparison_names["cupidc-runtime-contract.elf"] = "runtime"
        object_comparison_names = {
            plan.name
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
        } | {"as_elf", "runtime"}
        bootstrap_files = {
            "toolchain/ctool.cc": {
                "sha256": "4" * 64,
                "size": 1,
            }
        }
        for name in cupidc_toolchain_contracts._expected_artifact_names():
            payload = f"{payload_prefix}:{name}\n".encode("ascii")
            path = output / name
            path.write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()
            records.append(
                {"path": name, "sha256": digest, "size": len(payload)}
            )
            if name in comparison_names:
                comparisons[comparison_names[name]] = digest
        report = {
            "artifacts": sorted(records, key=lambda record: record["path"]),
            "bootstrap": {
                "build_plan_sha256": "1" * 64,
                "seed_manifest": {
                    "path": "bootstrap/seeds/i386-linux/manifest.json",
                    "sha256": "2" * 64,
                },
                "source_inputs": {
                    "count": len(bootstrap_files),
                    "files": bootstrap_files,
                    "sha256": (
                        cupidc_toolchain_contracts._snapshot_sha256(
                            bootstrap_files
                        )
                    ),
                },
            },
            "comparisons": comparisons,
            "input_count": 1,
            "inputs": {"toolchain/ctool.h": "5" * 64},
            "object_comparisons": {
                name: "3" * 64
                for name in object_comparison_names
            },
            "schema": cupidc_toolchain_contracts.REPORT_SCHEMA,
            "status": "pass",
            "target": {
                "architecture": "i386",
                "entry": cupidc_toolchain_contracts.TARGET_ENTRY,
                "linkage": "static",
                "operating_system": "linux",
            },
            "tool_fixed_point": {
                "all_equal": True,
                "c_objects": 19,
                "startup_objects": 1,
                "tool_images": len(cupidc_toolchain_contracts.TOOL_NAMES),
            },
        }
        (output / "manifest.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="ascii",
        )

    @staticmethod
    def _bind_publication_inputs(
        output: Path, inputs: dict[str, str]
    ) -> None:
        manifest = output / "manifest.json"
        report = json.loads(manifest.read_text(encoding="ascii"))
        report["input_count"] = len(inputs)
        report["inputs"] = inputs
        manifest.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="ascii",
        )

    @staticmethod
    def _bind_publication_bootstrap(
        output: Path, bootstrap: dict[str, object]
    ) -> None:
        manifest = output / "manifest.json"
        report = json.loads(manifest.read_text(encoding="ascii"))
        report["bootstrap"] = bootstrap
        manifest.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="ascii",
        )

    @staticmethod
    def _write_minimal_source_root(root: Path) -> dict[str, str]:
        logical_paths = {
            plan.source
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
        } | {
            "kernel/lang/as_elf.cc",
            "kernel/lang/as_elf.h",
            "toolchain/tests/hosted_i386_runtime_contract.cc",
        } | set(cupidc_toolchain_contracts.CONTRACT_CONTROL_INPUTS) | set(
            cupidc_toolchain_contracts.WINDOWS_RUNTIME_INPUTS
        ) | set(
            cupidc_toolchain_contracts.USER_SYSCALL_ABI_INPUTS
        )
        for logical_path in sorted(logical_paths):
            path = root / logical_path
            path.parent.mkdir(parents=True, exist_ok=True)
            marker = (
                f"# checked input: {logical_path}\n"
                if path.suffix == ".py" or path.name == "Makefile"
                else f"/* checked input: {logical_path} */\n"
            )
            path.write_text(marker, encoding="ascii")
        paths = cupidc_toolchain_contracts._contract_input_paths(root)
        return cupidc_toolchain_contracts._snapshot_inputs(root, paths)

    @staticmethod
    def _write_minimal_bootstrap_root(root: Path) -> dict[str, object]:
        logical_inputs = {
            "link.ld": "SECTIONS {}\n",
            "toolchain/ctool.h": "int ctool;\n",
            "toolchain/cupidc_emit.cc": "int emit;\n",
            "toolchain/hosted/i386-linux/include/stdio.h": "int stdio;\n",
            "toolchain/hosted/i386-linux/runtime.cc": "int runtime;\n",
            "toolchain/hosted/i386-linux/start.asm": "ret\n",
        }
        for logical_path, text in logical_inputs.items():
            path = root / logical_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="ascii")
        build_plan = {
            "sources": [
                {"path": "/toolchain/cupidc_emit.cc"},
                {"path": "/toolchain/hosted/i386-linux/runtime.cc"},
            ],
            "startup": "/toolchain/hosted/i386-linux/start.asm",
        }
        build_plan_sha256 = "6" * 64
        seed_data = {
            "build_plan": build_plan,
            "build_plan_sha256": build_plan_sha256,
        }
        manifest = (
            root / "bootstrap/seeds/i386-linux/manifest.json"
        )
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(
            json.dumps(seed_data, indent=2, sort_keys=True) + "\n",
            encoding="ascii",
        )
        files = cupidc_toolchain_contracts.capture_source_snapshot(
            root, build_plan
        )
        return {
            "build_plan_sha256": build_plan_sha256,
            "seed_manifest": {
                "path": manifest.relative_to(root).as_posix(),
                "sha256": cupidc_toolchain_contracts._sha256(manifest),
            },
            "source_inputs": {
                "count": len(files),
                "files": files,
                "sha256": cupidc_toolchain_contracts._snapshot_sha256(
                    files
                ),
            },
        }

    @classmethod
    def _write_verified_source_root(
        cls, root: Path
    ) -> tuple[dict[str, str], dict[str, object]]:
        cls._write_minimal_source_root(root)
        bootstrap = cls._write_minimal_bootstrap_root(root)
        inputs = cupidc_toolchain_contracts._snapshot_inputs(
            root, cupidc_toolchain_contracts._contract_input_paths(root)
        )
        return inputs, bootstrap

    @staticmethod
    def _verified_seed_inputs(root: Path) -> SimpleNamespace:
        manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        encoded = manifest.read_bytes()
        return SimpleNamespace(
            manifest=json.loads(encoded.decode("utf-8")),
            manifest_sha256=hashlib.sha256(encoded).hexdigest(),
            tools={},
        )

    def test_plan_transfers_every_native_contract_source(self):
        plans = cupidc_toolchain_contracts.CONTRACT_PLANS
        self.assertEqual({plan.name for plan in plans}, EXPECTED_CONTRACTS)
        self.assertEqual(len(plans), len(EXPECTED_CONTRACTS))
        for plan in plans:
            with self.subTest(contract=plan.name):
                self.assertTrue(plan.source.endswith("_contract.cc"))
                self.assertFalse(plan.source.endswith(".c"))
                self.assertEqual(plan.link_objects[0], "start")
                self.assertEqual(plan.link_objects[1], "contract")
                self.assertEqual(plan.link_objects[-1], "runtime")

    def test_plan_rejects_a_retired_host_c_source(self):
        first = cupidc_toolchain_contracts.CONTRACT_PLANS[0]
        invalid = replace(first, source=first.source[:-1])
        with self.assertRaisesRegex(
            cupidc_toolchain_contracts.ContractError,
            "Cupid-owned contract source must end in .cc",
        ):
            cupidc_toolchain_contracts.validate_plans((invalid,))

    def test_plan_rejects_an_unknown_link_object(self):
        first = cupidc_toolchain_contracts.CONTRACT_PLANS[0]
        invalid = replace(
            first,
            link_objects=("start", "contract", "ctol", "runtime"),
        )
        with self.assertRaisesRegex(
            cupidc_toolchain_contracts.ContractError,
            "contract link object is unknown: core: ctol",
        ):
            cupidc_toolchain_contracts.validate_plans((invalid,))

    def test_contracts_do_not_open_retired_c_paths(self):
        root = Path(__file__).resolve().parents[1]
        retired_paths = {
            f'"/{plan.source[:-1]}"'
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
        }
        for plan in cupidc_toolchain_contracts.CONTRACT_PLANS:
            text = (root / plan.source).read_text(encoding="utf-8")
            for retired in retired_paths:
                with self.subTest(source=plan.source, retired=retired):
                    self.assertNotIn(retired, text)

    def test_contract_input_inventory_includes_build_control_files(self):
        root = Path(__file__).resolve().parents[1]
        inputs = cupidc_toolchain_contracts._snapshot_inputs(
            root,
            cupidc_toolchain_contracts._contract_input_paths(root),
        )

        self.assertEqual(len(inputs), 58)
        self.assertTrue(
            set(cupidc_toolchain_contracts.CONTRACT_CONTROL_INPUTS)
            <= set(inputs)
        )
        self.assertTrue(
            set(cupidc_toolchain_contracts.WINDOWS_RUNTIME_INPUTS)
            <= set(inputs)
        )
        self.assertTrue(
            set(cupidc_toolchain_contracts.USER_SYSCALL_ABI_INPUTS)
            <= set(inputs)
        )

    def test_kernel_bridge_sources_alone_receive_the_kernel_include_root(self):
        base = (
            "-I",
            "/toolchain",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
        )
        bridge = (
            "-I",
            "/toolchain",
            "-I",
            "/kernel/lang",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
        )
        self.assertEqual(
            cupidc_toolchain_contracts._compile_include_arguments(
                "toolchain/tests/core_contract.cc"
            ),
            base,
        )
        for source in sorted(cupidc_toolchain_contracts.KERNEL_LANG_SOURCES):
            with self.subTest(source=source):
                self.assertEqual(
                    cupidc_toolchain_contracts._compile_include_arguments(
                        source
                    ),
                    bridge,
                )

    def test_runtime_alone_uses_the_gnu_contract_profile(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-profile-"
        ) as temporary:
            root = Path(temporary).resolve()
            output = root / "build/runtime.o"
            output.parent.mkdir()
            compiler = root / "cupidc.elf"
            runner = mock.Mock()
            with mock.patch.object(
                cupidc_toolchain_contracts, "_run_clean"
            ) as run_clean, mock.patch.object(
                cupidc_toolchain_contracts,
                "_validate_i386_relocatable",
            ):
                cupidc_toolchain_contracts._compile_source(
                    runner,
                    compiler,
                    root,
                    "toolchain/tests/hosted_i386_runtime_contract.cc",
                    output,
                    "runtime",
                    45,
                )
                runtime_arguments = run_clean.call_args.args[2]
                run_clean.reset_mock()
                cupidc_toolchain_contracts._compile_source(
                    runner,
                    compiler,
                    root,
                    "toolchain/tests/core_contract.cc",
                    output,
                    "strict",
                    45,
                )
                strict_arguments = run_clean.call_args.args[2]

            self.assertIn("--gnu", runtime_arguments)
            self.assertNotIn("--gnu", strict_arguments)

    def test_build_creates_its_missing_output_parent_before_bootstrap(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-output-parent-"
        ) as temporary:
            root = Path(temporary).resolve()
            (root / "toolchain").mkdir()
            manifest = root / "manifest.json"
            manifest.write_text("{}\n", encoding="ascii")
            output = root / "toolchain/build/cupidc-contracts"

            with (
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_contract_input_paths",
                    return_value=(),
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_snapshot_inputs",
                    return_value={},
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "bootstrap_from_seed",
                    side_effect=cupidc_toolchain_contracts.BootstrapError(
                        "injected stop"
                    ),
                ),
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "checked bootstrap failed: injected stop",
                ):
                    cupidc_toolchain_contracts.build_contracts(
                        root, manifest, output
                    )

            self.assertTrue(output.parent.is_dir())
            self.assertFalse(output.exists())

    def test_contract_object_comparison_checks_exact_stage_bytes(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-object-comparison-"
        ) as temporary:
            root = Path(temporary)
            first = root / "first.o"
            second = root / "second.o"
            first.write_bytes(b"same")
            second.write_bytes(b"same")

            comparisons = cupidc_toolchain_contracts._compare_stage_files(
                {"core": first}, {"core": second}, "contract object"
            )

            self.assertEqual(
                comparisons["core"], hashlib.sha256(b"same").hexdigest()
            )
            second.write_bytes(b"different")
            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "contract object differs across stages: core",
            ):
                cupidc_toolchain_contracts._compare_stage_files(
                    {"core": first},
                    {"core": second},
                    "contract object",
                )

    def test_publication_replaces_a_complete_cohort_together(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-publication-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output, "old")
            staging = root / "staging"
            self._write_publication(staging, "new")

            cupidc_toolchain_contracts.publish_directory(
                staging,
                output,
                cupidc_toolchain_contracts._expected_artifact_names()
                + ("manifest.json",),
                root,
            )

            self.assertFalse(staging.exists())
            self.assertEqual(
                (output / "core-contract.elf").read_bytes(),
                b"new:core-contract.elf\n",
            )
            cupidc_toolchain_contracts.verify_publication(output)

    def test_incomplete_publication_preserves_existing_cohort(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-rollback-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output, "old")
            old_manifest = (output / "manifest.json").read_bytes()
            old_contract = (output / "core-contract.elf").read_bytes()
            staging = root / "staging"
            staging.mkdir()
            (staging / "core-contract.elf").write_bytes(b"incomplete")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "publication is incomplete",
            ):
                cupidc_toolchain_contracts.publish_directory(
                    staging,
                    output,
                    cupidc_toolchain_contracts._expected_artifact_names()
                    + ("manifest.json",),
                    root,
                )

            self.assertEqual(
                (output / "manifest.json").read_bytes(), old_manifest
            )
            self.assertEqual(
                (output / "core-contract.elf").read_bytes(), old_contract
            )
            cupidc_toolchain_contracts.verify_publication(output)
            self.assertTrue(staging.exists())

    def test_failed_promotion_restores_the_previous_cohort(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-promotion-failure-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output, "old")
            staging = root / "staging"
            self._write_publication(staging, "new")
            original_replace = Path.replace

            def fail_staging(source: Path, target: Path) -> Path:
                if source == staging:
                    raise OSError("injected staging rename failure")
                return original_replace(source, target)

            with mock.patch.object(Path, "replace", fail_staging):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "injected staging rename failure",
                ):
                    cupidc_toolchain_contracts.publish_directory(
                        staging,
                        output,
                        cupidc_toolchain_contracts._expected_artifact_names()
                        + ("manifest.json",),
                        root,
                    )

            self.assertTrue(staging.exists())
            self.assertEqual(
                (output / "core-contract.elf").read_bytes(),
                b"old:core-contract.elf\n",
            )
            self.assertFalse(
                output.with_name(
                    f".{output.name}.backup-{os.getpid()}"
                ).exists()
            )
            cupidc_toolchain_contracts.verify_publication(output)

    def test_failed_restoration_reports_the_recoverable_backup(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-restoration-failure-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output, "old")
            staging = root / "staging"
            self._write_publication(staging, "new")
            backup = output.with_name(
                f".{output.name}.backup-{os.getpid()}"
            )
            original_replace = Path.replace

            def fail_staging_and_restore(
                source: Path, target: Path
            ) -> Path:
                if source == staging:
                    raise OSError("injected staging rename failure")
                if source == backup:
                    raise OSError("injected restoration failure")
                return original_replace(source, target)

            with mock.patch.object(
                Path, "replace", fail_staging_and_restore
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "injected staging rename failure.*"
                    "injected restoration failure.*recoverable",
                ):
                    cupidc_toolchain_contracts.publish_directory(
                        staging,
                        output,
                        cupidc_toolchain_contracts._expected_artifact_names()
                        + ("manifest.json",),
                        root,
                    )

            self.assertFalse(output.exists())
            self.assertEqual(
                (backup / "core-contract.elf").read_bytes(),
                b"old:core-contract.elf\n",
            )
            self.assertTrue(staging.exists())

    def test_backup_cleanup_cannot_turn_publication_into_a_failure(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-cleanup-failure-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output, "old")
            staging = root / "staging"
            self._write_publication(staging, "new")
            backup = output.with_name(
                f".{output.name}.backup-{os.getpid()}"
            )

            with mock.patch.object(
                cupidc_toolchain_contracts.shutil,
                "rmtree",
                side_effect=OSError("injected cleanup failure"),
            ):
                cupidc_toolchain_contracts.publish_directory(
                    staging,
                    output,
                    cupidc_toolchain_contracts._expected_artifact_names()
                    + ("manifest.json",),
                    root,
                )

            self.assertEqual(
                (output / "core-contract.elf").read_bytes(),
                b"new:core-contract.elf\n",
            )
            self.assertEqual(
                (backup / "core-contract.elf").read_bytes(),
                b"old:core-contract.elf\n",
            )
            cupidc_toolchain_contracts.verify_publication(output)

    def test_output_target_accepts_a_dedicated_publication_leaf(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-output-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"

            checked = cupidc_toolchain_contracts._validate_output_target(
                root, output
            )

            self.assertEqual(checked, output.resolve())

    def test_output_target_rejects_repository_and_source_directories(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-output-reject-"
        ) as temporary:
            root = Path(temporary)
            toolchain = root / "toolchain"
            toolchain.mkdir()
            source = root / "kernel/cupidc-contracts"
            source.mkdir(parents=True)
            sentinel = source / "source.cc"
            sentinel.write_text("int source;\n", encoding="ascii")

            for output in (root, toolchain):
                with self.subTest(output=output):
                    with self.assertRaisesRegex(
                        cupidc_toolchain_contracts.ContractError,
                        "dedicated cupidc-contracts directory",
                    ):
                        cupidc_toolchain_contracts._validate_output_target(
                            root, output
                        )
            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "existing contract output is not a complete cohort",
            ):
                cupidc_toolchain_contracts._validate_output_target(
                    root, source
                )
            self.assertEqual(
                sentinel.read_text(encoding="ascii"), "int source;\n"
            )

    def test_published_cohort_verifies_every_artifact(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-verify-"
        ) as temporary:
            output = Path(temporary) / "contracts"
            self._write_publication(output)

            report = cupidc_toolchain_contracts.verify_publication(output)

            self.assertEqual(report["status"], "pass")
            self.assertEqual(
                len(report["artifacts"]),
                len(cupidc_toolchain_contracts._expected_artifact_names()),
            )

    def test_published_cohort_rejects_artifact_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-drift-"
        ) as temporary:
            output = Path(temporary) / "contracts"
            self._write_publication(output)
            (output / "core-contract.elf").write_bytes(b"changed")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "artifact differs: core-contract.elf",
            ):
                cupidc_toolchain_contracts.verify_publication(output)

    def test_published_cohort_rejects_a_missing_artifact(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-missing-"
        ) as temporary:
            output = Path(temporary) / "contracts"
            self._write_publication(output)
            (output / "core-contract.elf").unlink()

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "directory is incomplete",
            ):
                cupidc_toolchain_contracts.verify_publication(output)

    def test_published_cohort_verifies_live_source_inputs(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-source-verify-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            inputs, bootstrap = self._write_verified_source_root(source_root)
            output = root / "contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)

            checked = cupidc_toolchain_contracts.verify_publication(output)
            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(source_root),
            ):
                cupidc_toolchain_contracts.verify_publication_inputs(
                    source_root, checked
                )

    def test_published_cohort_rejects_live_source_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-source-drift-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            inputs, bootstrap = self._write_verified_source_root(source_root)
            output = root / "contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            source = (
                source_root
                / cupidc_toolchain_contracts.CONTRACT_PLANS[0].source
            )
            source.write_text("/* drift */\n", encoding="ascii")

            checked = cupidc_toolchain_contracts.verify_publication(output)
            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(source_root),
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "inputs differ from the live source",
                ):
                    cupidc_toolchain_contracts.verify_publication_inputs(
                        source_root, checked
                    )

    def test_run_rejects_backdated_control_input_drift_before_execution(self):
        for logical_path in (
            *cupidc_toolchain_contracts.CONTRACT_CONTROL_INPUTS,
            *cupidc_toolchain_contracts.WINDOWS_RUNTIME_INPUTS,
        ):
            with self.subTest(input=logical_path), tempfile.TemporaryDirectory(
                prefix="cupid-contract-control-drift-"
            ) as temporary:
                root = Path(temporary)
                inputs, bootstrap = self._write_verified_source_root(root)
                output = root / "toolchain/build/cupidc-contracts"
                self._write_publication(output)
                self._bind_publication_inputs(output, inputs)
                self._bind_publication_bootstrap(output, bootstrap)
                executable = output / "core-contract.elf"
                changed = root / logical_path
                stat = changed.stat()
                changed.write_bytes(changed.read_bytes() + b"# drift\n")
                os.utime(
                    changed,
                    ns=(stat.st_atime_ns, stat.st_mtime_ns),
                )

                with mock.patch.object(
                    cupidc_toolchain_contracts,
                    "verify_seed_inputs",
                    return_value=self._verified_seed_inputs(root),
                ), mock.patch.object(
                    cupidc_toolchain_contracts, "ToolRunner"
                ) as runner_type:
                    with self.assertRaisesRegex(
                        cupidc_toolchain_contracts.ContractError,
                        "contract inputs differ from the live source",
                    ):
                        cupidc_toolchain_contracts.run_published_contract(
                            root, executable, (), 45
                        )
                    runner_type.assert_not_called()

    def test_published_cohort_rejects_backdated_bootstrap_drift(self):
        for logical_path in (
            "link.ld",
            "toolchain/cupidc_emit.cc",
            "toolchain/hosted/i386-linux/start.asm",
        ):
            with self.subTest(input=logical_path), tempfile.TemporaryDirectory(
                prefix="cupid-contract-bootstrap-drift-"
            ) as temporary:
                root = Path(temporary)
                inputs, bootstrap = self._write_verified_source_root(root)
                output = root / "contracts"
                self._write_publication(output)
                self._bind_publication_inputs(output, inputs)
                self._bind_publication_bootstrap(output, bootstrap)
                changed = root / logical_path
                stat = changed.stat()
                changed.write_bytes(changed.read_bytes() + b"drift\n")
                os.utime(
                    changed,
                    ns=(stat.st_atime_ns, stat.st_mtime_ns),
                )

                checked = cupidc_toolchain_contracts.verify_publication(output)
                with mock.patch.object(
                    cupidc_toolchain_contracts,
                    "verify_seed_inputs",
                    return_value=self._verified_seed_inputs(root),
                ):
                    with self.assertRaisesRegex(
                        cupidc_toolchain_contracts.ContractError,
                        "bootstrap inputs differ from the live source",
                    ):
                        cupidc_toolchain_contracts.verify_publication_inputs(
                            root, checked
                        )

    def test_run_rejects_bootstrap_drift_before_tool_execution(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-run-bootstrap-drift-"
        ) as temporary:
            root = Path(temporary)
            inputs, bootstrap = self._write_verified_source_root(root)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            executable = output / "core-contract.elf"
            changed = root / "toolchain/cupidc_emit.cc"
            stat = changed.stat()
            changed.write_bytes(changed.read_bytes() + b"drift\n")
            os.utime(changed, ns=(stat.st_atime_ns, stat.st_mtime_ns))

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(root),
            ), mock.patch.object(
                cupidc_toolchain_contracts, "ToolRunner"
            ) as runner_type:
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "bootstrap inputs differ from the live source",
                ):
                    cupidc_toolchain_contracts.run_published_contract(
                        root, executable, (), 45
                    )
                runner_type.assert_not_called()

    def test_published_cohort_rejects_backdated_seed_manifest_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-seed-drift-"
        ) as temporary:
            root = Path(temporary)
            inputs, bootstrap = self._write_verified_source_root(root)
            output = root / "contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            manifest = (
                root / "bootstrap/seeds/i386-linux/manifest.json"
            )
            stat = manifest.stat()
            manifest.write_bytes(manifest.read_bytes() + b" ")
            os.utime(manifest, ns=(stat.st_atime_ns, stat.st_mtime_ns))

            checked = cupidc_toolchain_contracts.verify_publication(output)
            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(root),
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "bootstrap seed differs from the live source",
                ):
                    cupidc_toolchain_contracts.verify_publication_inputs(
                        root, checked
                    )

    def test_publication_uses_one_verified_seed_manifest_capture(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-seed-capture-"
        ) as temporary:
            root = Path(temporary)
            inputs, bootstrap = self._write_verified_source_root(root)
            output = root / "contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            manifest = (
                root / "bootstrap/seeds/i386-linux/manifest.json"
            )
            captured = self._verified_seed_inputs(root)
            checked = cupidc_toolchain_contracts.verify_publication(output)

            def replace_after_capture(path: Path) -> SimpleNamespace:
                self.assertEqual(path, manifest.resolve())
                manifest.write_text(
                    '{"replacement": true}\n', encoding="ascii"
                )
                return captured

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                side_effect=replace_after_capture,
            ) as verifier:
                cupidc_toolchain_contracts.verify_publication_inputs(
                    root, checked
                )

            verifier.assert_called_once_with(manifest.resolve())

    def test_live_input_check_rejects_added_and_removed_headers(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-input-inventory-"
        ) as temporary:
            root = Path(temporary)
            initial = self._write_minimal_source_root(root)
            added = root / "toolchain/added_contract_input.h"
            added.write_text("int added;\n", encoding="ascii")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "inputs changed while the checked build ran",
            ):
                cupidc_toolchain_contracts._require_inputs_unchanged(
                    root, initial
                )

            with_added = cupidc_toolchain_contracts._snapshot_inputs(
                root,
                cupidc_toolchain_contracts._contract_input_paths(root),
            )
            added.unlink()
            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "inputs changed while the checked build ran",
            ):
                cupidc_toolchain_contracts._require_inputs_unchanged(
                    root, with_added
                )

    def test_frozen_input_inventory_matches_the_initial_snapshot(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-input-freeze-"
        ) as temporary:
            workspace = Path(temporary)
            root = workspace / "source"
            expected = self._write_minimal_source_root(root)
            paths = cupidc_toolchain_contracts._contract_input_paths(root)
            frozen = workspace / "frozen"

            cupidc_toolchain_contracts._freeze_contract_inputs(
                root, frozen, paths, expected
            )

            actual = cupidc_toolchain_contracts._snapshot_inputs(
                frozen,
                cupidc_toolchain_contracts._contract_input_paths(frozen),
            )
            self.assertEqual(actual, expected)

    def test_freeze_rejects_bytes_changed_after_the_live_check(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-input-freeze-race-"
        ) as temporary:
            workspace = Path(temporary)
            root = workspace / "source"
            expected = self._write_minimal_source_root(root)
            paths = cupidc_toolchain_contracts._contract_input_paths(root)
            changed = root / cupidc_toolchain_contracts.CONTRACT_PLANS[0].source
            changed.write_text("/* changed during freeze */\n", encoding="ascii")

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "_require_inputs_unchanged",
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "frozen contract inputs differ from the initial snapshot",
                ):
                    cupidc_toolchain_contracts._freeze_contract_inputs(
                        root,
                        workspace / "frozen",
                        paths,
                        expected,
                    )

    def test_run_verifies_the_cohort_before_execution(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-run-"
        ) as temporary:
            root = Path(temporary)
            inputs, bootstrap = self._write_verified_source_root(root)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            executable = output / "core-contract.elf"
            completed = subprocess.CompletedProcess(
                [str(executable)], 0, "contract output\n", ""
            )

            with mock.patch.object(
                cupidc_toolchain_contracts, "ToolRunner"
            ) as runner_type, mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(root),
            ):
                runner_type.return_value.run.return_value = completed
                result = cupidc_toolchain_contracts.run_published_contract(
                    root, executable, ("foundations",), 45
                )

            self.assertIs(result, completed)
            runner_type.assert_called_once_with(
                (root / "toolchain").resolve()
            )
            frozen_executable = runner_type.return_value.run.call_args.args[0]
            self.assertEqual(frozen_executable.name, executable.name)
            self.assertNotEqual(frozen_executable, executable.resolve())
            self.assertFalse(frozen_executable.exists())
            self.assertEqual(
                runner_type.return_value.run.call_args.args[1:],
                (("foundations",), 45),
            )

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

    def test_ensure_reuses_a_current_contract_cohort(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        output = root / "toolchain/build/cupidc-contracts"
        report = {"status": "pass"}

        with mock.patch.object(
            Path, "exists", return_value=True
        ), mock.patch.object(
            Path, "is_dir", return_value=True
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_resolve_manifest",
            return_value=(manifest, "bootstrap/seeds/i386-linux/manifest.json"),
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_validate_output_target",
            return_value=output,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "verify_publication",
            return_value=report,
        ) as verify, mock.patch.object(
            cupidc_toolchain_contracts,
            "verify_publication_inputs",
        ) as verify_inputs, mock.patch.object(
            cupidc_toolchain_contracts,
            "_require_report_manifest",
        ) as require_manifest, mock.patch.object(
            cupidc_toolchain_contracts, "build_contracts"
        ) as build:
            checked = cupidc_toolchain_contracts.ensure_contracts(
                root, manifest, output, workers=3
            )

        self.assertIs(checked, report)
        verify.assert_called_once_with(output)
        verify_inputs.assert_called_once_with(root, report)
        require_manifest.assert_called_once_with(
            report,
            manifest,
            "bootstrap/seeds/i386-linux/manifest.json",
        )
        build.assert_not_called()

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

    def test_run_uses_frozen_bytes_and_rejects_live_replacement(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-run-replacement-"
        ) as temporary:
            root = Path(temporary)
            inputs, bootstrap = self._write_verified_source_root(root)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            executable = output / "core-contract.elf"
            original = executable.read_bytes()

            def replace_live(frozen, arguments, timeout):
                self.assertEqual(frozen.read_bytes(), original)
                executable.write_bytes(b"replacement")
                return subprocess.CompletedProcess(
                    [str(frozen)], 0, "ok\n", ""
                )

            with mock.patch.object(
                cupidc_toolchain_contracts, "ToolRunner"
            ) as runner_type, mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(root),
            ):
                runner_type.return_value.run.side_effect = replace_live
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "changed while contract ran",
                ):
                    cupidc_toolchain_contracts.run_published_contract(
                        root, executable, (), 45
                    )

    def test_run_rejects_arbitrary_and_unpublished_executables(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-run-reject-"
        ) as temporary:
            root = Path(temporary)
            self._write_minimal_source_root(root)
            arbitrary = root / "arbitrary.elf"
            arbitrary.write_bytes(b"not a cohort artifact")
            expected_name = root / "elsewhere/core-contract.elf"
            expected_name.parent.mkdir()
            expected_name.write_bytes(b"not a publication")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "not a published cohort artifact",
            ):
                cupidc_toolchain_contracts.run_published_contract(
                    root, arbitrary, (), 45
                )
            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "published contract manifest is missing",
            ):
                cupidc_toolchain_contracts.run_published_contract(
                    root, expected_name, (), 45
                )

    def test_run_rejects_artifact_and_live_input_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-run-drift-"
        ) as temporary:
            root = Path(temporary)
            inputs, bootstrap = self._write_verified_source_root(root)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output)
            self._bind_publication_inputs(output, inputs)
            self._bind_publication_bootstrap(output, bootstrap)
            executable = output / "core-contract.elf"
            original = executable.read_bytes()
            executable.write_bytes(b"changed")

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(root),
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "artifact differs: core-contract.elf",
                ):
                    cupidc_toolchain_contracts.run_published_contract(
                        root, executable, (), 45
                    )

            executable.write_bytes(original)
            changed = root / cupidc_toolchain_contracts.CONTRACT_PLANS[0].source
            changed.write_text("/* live drift */\n", encoding="ascii")
            with mock.patch.object(
                cupidc_toolchain_contracts,
                "verify_seed_inputs",
                return_value=self._verified_seed_inputs(root),
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "inputs differ from the live source",
                ):
                    cupidc_toolchain_contracts.run_published_contract(
                        root, executable, (), 45
                    )


if __name__ == "__main__":
    unittest.main()

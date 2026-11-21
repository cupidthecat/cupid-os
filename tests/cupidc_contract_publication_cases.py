## @file cupidc_contract_publication_cases.py
#  @brief Check publishing, verifying and replacing a native contract cohort.

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tests.cupidc_contract_fixtures import EXPECTED_CONTRACTS
from tools import cupidc_toolchain_contracts


class CupidCContractPublicationCases:
    def test_build_publishes_the_declared_converged_generation(self):
        self._exercise_publication()

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

    def test_output_target_allows_one_verified_legacy_schema_upgrade(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-legacy-upgrade-"
        ) as temporary:
            root = Path(temporary)
            output = root / "toolchain/build/cupidc-contracts"
            self._write_publication(output)
            manifest = output / "manifest.json"
            report = json.loads(manifest.read_text(encoding="ascii"))
            report["schema"] = "cupid.toolchain-contracts.v2"
            report["inputs"] = {
                path: record["sha256"]
                for path, record in report["inputs"].items()
            }
            report["object_comparisons"] = {
                name: record["sha256"]
                for name, record in report["object_comparisons"].items()
            }
            manifest.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="ascii",
            )

            self.assertEqual(
                cupidc_toolchain_contracts._validate_output_target(
                    root, output
                ),
                output.resolve(),
            )
            (output / "core-contract.elf").write_bytes(b"tampered")
            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "existing contract output is not a complete cohort",
            ):
                cupidc_toolchain_contracts._validate_output_target(
                    root, output
                )

    def test_published_cohort_verifies_every_artifact(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-verify-"
        ) as temporary:
            output = Path(temporary) / "contracts"
            self._write_publication(output)
            manifest = output / "manifest.json"
            report = json.loads(manifest.read_text(encoding="ascii"))
            report["tool_fixed_point"]["compared_generations"] = [
                "stage-three",
                "stage-four",
            ]
            manifest.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="ascii",
            )

            report = cupidc_toolchain_contracts.verify_publication(output)

            self.assertEqual(report["status"], "pass")
            self.assertEqual(
                len(report["artifacts"]),
                len(cupidc_toolchain_contracts._expected_artifact_names()),
            )

    def test_published_cohort_rejects_digest_sizes_outside_uint64(self):
        cases = (
            ("inputs", "published contract input inventory differs"),
            (
                "object_comparisons",
                "published contract object comparison record differs",
            ),
        )
        for field, message in cases:
            with self.subTest(field=field), tempfile.TemporaryDirectory(
                prefix="cupid-contract-size-range-"
            ) as temporary:
                output = Path(temporary) / "contracts"
                self._write_publication(output)
                manifest = output / "manifest.json"
                report = json.loads(manifest.read_text(encoding="ascii"))
                record = next(iter(report[field].values()))
                record["size"] = 1 << 64
                manifest.write_text(
                    json.dumps(report, indent=2, sort_keys=True) + "\n",
                    encoding="ascii",
                )

                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    message,
                ):
                    cupidc_toolchain_contracts.verify_publication(output)

    def test_published_cohort_rejects_the_wrong_convergence_pair(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-convergence-pair-"
        ) as temporary:
            output = Path(temporary) / "contracts"
            self._write_publication(output)
            manifest = output / "manifest.json"
            report = json.loads(manifest.read_text(encoding="ascii"))
            report["tool_fixed_point"]["compared_generations"] = [
                "stage-two",
                "stage-three",
            ]
            manifest.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="ascii",
            )

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "published Toolchain fixed-point record differs",
            ):
                cupidc_toolchain_contracts.verify_publication(output)

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

            with_added = cupidc_toolchain_contracts._snapshot_contract_inputs(
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

            actual = cupidc_toolchain_contracts._snapshot_contract_inputs(
                frozen,
                cupidc_toolchain_contracts._contract_input_paths(frozen),
            )
            self.assertEqual(actual, expected)

    def test_frozen_input_tree_includes_the_bootstrap_author_closure(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-bootstrap-input-freeze-"
        ) as temporary:
            workspace = Path(temporary)
            root = workspace / "source"
            expected = self._write_minimal_source_root(root)
            bootstrap_source = root / "toolchain/bootstrap-only.cc"
            bootstrap_source.write_bytes(b"bootstrap author source\n")
            bootstrap_files = {
                "toolchain/bootstrap-only.cc": {
                    "sha256": (
                        "4114fd04bb1ab2ed167e297c98519a4fadac13bb6eff8a8b0"
                        "433fcd1366c5aaa"
                    ),
                    "size": 24,
                }
            }
            frozen = workspace / "frozen"

            cupidc_toolchain_contracts._freeze_contract_inputs(
                root,
                frozen,
                cupidc_toolchain_contracts._contract_input_paths(root),
                expected,
                bootstrap_files,
            )

            self.assertEqual(
                (frozen / "toolchain/bootstrap-only.cc").read_bytes(),
                b"bootstrap author source\n",
            )

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

    def test_ensure_reuses_a_current_contract_cohort(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        output = root / "toolchain/build/cupidc-contracts"
        report = {"status": "pass", "bootstrap": {"source_inputs": {"files": {"toolchain/hosted/i386-windows/final_path_start.asm": {}}}}}

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

    def test_ensure_rebuilds_a_complete_legacy_contract_cohort(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        output = root / "toolchain/build/cupidc-contracts"
        rebuilt = {"schema": cupidc_toolchain_contracts.REPORT_SCHEMA}

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
            side_effect=cupidc_toolchain_contracts.ContractError(
                "published contract manifest metadata differs"
            ),
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "build_contracts",
            return_value=rebuilt,
        ) as build:
            checked = cupidc_toolchain_contracts.ensure_contracts(
                root, manifest, output, workers=3
            )

        self.assertIs(checked, rebuilt)
        build.assert_called_once_with(root, manifest, output, 3, windows_user_link_aliases=True)

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

    def _exercise_publication(self, *, authority_cases=False, late_authority_drift=False):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-publication-generation-"
        ) as temporary:
            root = Path(temporary).resolve()
            (root / "toolchain").mkdir()
            manifest = root / "manifest.json"
            manifest.write_text("{}\n", encoding="ascii")
            output = root / "toolchain/build/cupidc-contracts"
            built_generations: list[str] = []
            runtime_generations: list[str] = []
            decision_events: list[str] = []
            candidate_plan_fields = [{"candidate_build_plan_sha256": "2" * 64}]
            selected_request = [None]
            drift_release = [False]
            release_path = root / "reviewed-release.json"
            release_bytes = b"retained authority fixture"
            release_path.write_bytes(release_bytes)

            def capture_request(_root, _manifest, path):
                if path is None:
                    return None
                self.assertEqual(path, release_path)
                return selected_request[0]

            bootstrap_files = {
                "toolchain/ctool.cc": {
                    "sha256": "4" * 64,
                    "size": 1,
                }
            }

            def bootstrap(
                seed_manifest: Path,
                source_root: Path,
                bootstrap_output: Path,
                *, windows_user_link_aliases: bool, release_request=None,
            ) -> dict[str, object]:
                self.assertTrue(windows_user_link_aliases)
                self.assertIs(release_request, selected_request[0])
                del seed_manifest, source_root
                for generation in (
                    "stage-two",
                    "stage-three",
                    "stage-four",
                ):
                    stage = bootstrap_output / generation
                    stage.mkdir(parents=True)
                    for object_name in (
                        cupidc_toolchain_contracts.BOOTSTRAP_OBJECT_NAMES
                    ):
                        (stage / f"{object_name}.o").write_bytes(
                            f"{generation}:object:{object_name}".encode(
                                "ascii"
                            )
                        )
                    for tool_name in cupidc_toolchain_contracts.TOOL_NAMES:
                        (stage / f"{tool_name}.elf").write_bytes(
                            f"{generation}:{tool_name}".encode("ascii")
                        )
                return {
                    "build_plan_sha256": "1" * 64,
                    **candidate_plan_fields[0],
                    "status": "pending-fixed-point-author",
                    "seed_manifest_sha256": (
                        cupidc_toolchain_contracts._sha256(manifest)
                    ),
                    "source_inputs": {
                        "count": len(bootstrap_files),
                        "files": bootstrap_files,
                        "sha256": (
                            cupidc_toolchain_contracts._snapshot_sha256(
                                bootstrap_files
                            )
                        ),
                    },
                }

            def build_stage(
                source_root: Path,
                bootstrap_stage: Path,
                stage_output: Path,
                stage_name: str,
                workers: int,
            ) -> tuple[dict[str, Path], dict[str, Path]]:
                del source_root, stage_name, workers
                generation = bootstrap_stage.name
                built_generations.append(generation)
                stage_output.mkdir(parents=True)
                objects: dict[str, Path] = {}
                executables: dict[str, Path] = {}
                for name in EXPECTED_CONTRACTS | {"as_elf", "runtime"}:
                    path = stage_output / f"{name}.o"
                    path.write_bytes(
                        f"{generation}:object:{name}".encode("ascii")
                    )
                    objects[name] = path
                for name in EXPECTED_CONTRACTS | {"runtime"}:
                    path = stage_output / f"{name}.elf"
                    path.write_bytes(
                        f"{generation}:executable:{name}".encode("ascii")
                    )
                    executables[name] = path
                return objects, executables

            def compare_second_stage(
                first: dict[str, Path],
                second: dict[str, Path],
                artifact_kind: str,
            ) -> dict[str, str]:
                decision_events.append(f"compare:{artifact_kind}")
                self.assertEqual(set(first), set(second))
                self.assertTrue(
                    all(
                        first[name].read_bytes()
                        != second[name].read_bytes()
                        for name in first
                    )
                )
                return {
                    name: hashlib.sha256(
                        second[name].read_bytes()
                    ).hexdigest()
                    for name in second
                }

            def run_runtime(
                source_root: Path,
                executable: Path,
                workspace: Path,
            ) -> None:
                del source_root, workspace
                runtime_generations.append(
                    executable.read_bytes().decode("ascii").split(":", 1)[0]
                )

            author_generations: list[str] = []
            author_output_valid = [True]
            author_failure = [False]

            def author_bytes(
                source_root: Path,
                bootstrap_stage_three: Path,
                bootstrap_stage_four: Path,
                workspace: Path,
                seed_manifest: Path,
                manifest_relative: str,
                report: dict[str, object],
                artifacts: list[Path],
                stage_three_objects: dict[str, Path],
                stage_four_objects: dict[str, Path],
                stage_three_executables: dict[str, Path],
                stage_four_executables: dict[str, Path],
            ) -> bytes:
                del (
                    source_root,
                    bootstrap_stage_three,
                    workspace,
                    seed_manifest,
                    manifest_relative,
                    artifacts,
                    stage_three_objects,
                    stage_four_objects,
                    stage_three_executables,
                    stage_four_executables,
                )
                decision_events.append("author")
                author_generations.append(bootstrap_stage_four.name)
                self.assertNotIn("tool_fixed_point", report)
                if author_failure[0]:
                    raise cupidc_toolchain_contracts.ContractError(
                        "injected paired-stage mismatch"
                    )
                if not author_output_valid[0]:
                    return b"not the independently checked manifest\n"
                authored_report = {
                    **report,
                    "bootstrap": {
                        **report["bootstrap"],
                        "build_plan_sha256": "2" * 64,
                    },
                    "tool_fixed_point": (
                        cupidc_toolchain_contracts._tool_fixed_point_record()
                    ),
                }
                if drift_release[0]:
                    release_path.write_bytes(release_bytes + b" \n")
                return (
                    json.dumps(authored_report, indent=2, sort_keys=True)
                    + "\n"
                ).encode("ascii")

            with (
                mock.patch.object(cupidc_toolchain_contracts, "_capture_behavior_request",
                                  side_effect=capture_request),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_contract_input_paths",
                    return_value=(),
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_snapshot_contract_inputs",
                    return_value={},
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_freeze_contract_inputs",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_bootstrap_for_manifest_author",
                    side_effect=bootstrap,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_build_contract_stage",
                    side_effect=build_stage,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_compare_stage_files",
                    side_effect=compare_second_stage,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_run_runtime_contract",
                    side_effect=run_runtime,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_checked_manifest_author_bytes",
                    side_effect=author_bytes,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_require_inputs_unchanged",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "verify_publication",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "verify_publication_inputs",
                ),
            ):
                report = cupidc_toolchain_contracts.build_contracts(
                    root, manifest, output, workers=8
                )
                self.assertEqual(report["bootstrap"]["build_plan_sha256"], "2" * 64)
                published_manifest = (output / "manifest.json").read_bytes()
                author_output_valid[0] = False
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "author output differs from the independent Python oracle",
                ):
                    cupidc_toolchain_contracts.build_contracts(
                        root, manifest, output, workers=8
                    )
                self.assertEqual(
                    (output / "manifest.json").read_bytes(),
                    published_manifest,
                )
                self.assertEqual(
                    list(
                        output.parent.glob(
                            f".{output.name}-build-*"
                        )
                    ),
                    [],
                )

                author_output_valid[0] = True
                author_failure[0] = True
                event_count = len(decision_events)
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "injected paired-stage mismatch",
                ):
                    cupidc_toolchain_contracts.build_contracts(
                        root, manifest, output, workers=8
                    )
                self.assertEqual(
                    decision_events[event_count:], ["author"]
                )
                self.assertEqual(
                    (output / "manifest.json").read_bytes(),
                    published_manifest,
                )
                self.assertEqual(
                    list(
                        output.parent.glob(
                            f".{output.name}-build-*"
                        )
                    ),
                    [],
                )
                author_failure[0] = False
                recovered_report = (
                    cupidc_toolchain_contracts.build_contracts(
                        root, manifest, output, workers=8
                    )
                )
                self.assertEqual(recovered_report, report)
                for fields in ({}, {"candidate_build_plan_sha256": "malformed"}):
                    candidate_plan_fields[0] = fields
                    with self.assertRaisesRegex(
                        cupidc_toolchain_contracts.ContractError,
                        "published bootstrap build plan differs",
                    ):
                        cupidc_toolchain_contracts.build_contracts(
                            root, manifest, output, workers=8
                        )
                    self.assertEqual(
                        (output / "manifest.json").read_bytes(), published_manifest
                    )
                self.assertEqual(
                    list(
                        output.parent.glob(
                            f".{output.name}-build-*"
                        )
                    ),
                    [],
                )

                if authority_cases:
                    candidate_plan_fields[0] = {"candidate_build_plan_sha256": "2" * 64}
                    request = mock.Mock()
                    request.linux_seed.artifact_bytes = tuple(
                        (name, f"stage-four:{name}".encode("ascii"))
                        for name in cupidc_toolchain_contracts.TOOL_NAMES)

                    def require_live():
                        if release_path.read_bytes() != release_bytes:
                            raise cupidc_toolchain_contracts.BootstrapError("caller seed release changed during behavior")

                    request.require_live.side_effect = require_live
                    selected_request[0] = request
                    self.assertEqual(cupidc_toolchain_contracts.build_contracts(
                        root, manifest, output, workers=8, behavior_release=release_path), report)
                    drift_release[0] = not late_authority_drift
                    validate_output = cupidc_toolchain_contracts._validate_output_target
                    validation_count = [0]

                    def validate_with_drift(*args):
                        result = validate_output(*args)
                        validation_count[0] += 1
                        if late_authority_drift and validation_count[0] == 2:
                            release_path.write_bytes(release_bytes + b" ")
                        return result

                    with mock.patch.object(cupidc_toolchain_contracts, "_validate_output_target",
                                           side_effect=validate_with_drift), \
                            self.assertRaisesRegex(cupidc_toolchain_contracts.ContractError,
                                                   "behavior release changed before publication"):
                        cupidc_toolchain_contracts.build_contracts(
                            root, manifest, output, workers=8, behavior_release=release_path)
                    self.assertEqual((output / "manifest.json").read_bytes(), published_manifest)
                    self.assertEqual(list(output.parent.glob(f".{output.name}-build-*")), [])
                    drift_release[0] = False
                    release_path.write_bytes(release_bytes)
                    self.assertEqual(cupidc_toolchain_contracts.build_contracts(
                        root, manifest, output, workers=8, behavior_release=release_path), report)

            self.assertEqual(
                built_generations,
                ["stage-three", "stage-four"] * (7 if authority_cases else 4),
            )
            self.assertEqual(runtime_generations, ["stage-four"] * (7 if authority_cases else 4))
            self.assertEqual(author_generations, ["stage-four"] * (7 if authority_cases else 4))
            successful_decision = [
                "author",
                "compare:contract object",
                "compare:contract executable",
                "compare:bootstrap object",
                "compare:bootstrap tool",
            ]
            self.assertEqual(
                decision_events,
                successful_decision * 2
                + ["author"]
                + successful_decision
                + (successful_decision * 3 if authority_cases else []),
            )
            self.assertEqual(
                report["tool_fixed_point"]["compared_generations"],
                ["stage-three", "stage-four"],
            )
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS:
                self.assertEqual(
                    (output / plan.artifact).read_bytes(),
                    f"stage-four:executable:{plan.name}".encode("ascii"),
                )
            self.assertEqual(
                (output / "cupidc-runtime-contract.elf").read_bytes(),
                b"stage-four:executable:runtime",
            )
            for tool_name in cupidc_toolchain_contracts.TOOL_NAMES:
                self.assertEqual(
                    (
                        output
                        / cupidc_toolchain_contracts.TOOL_PUBLIC_NAMES[
                            tool_name
                        ]
                    ).read_bytes(),
                    f"stage-four:{tool_name}".encode("ascii"),
                )

    def test_behavior_release_drift_after_author_preserves_publication_and_recovers(self):
        self._exercise_publication(authority_cases=True)

    def test_behavior_release_drift_during_output_validation_preserves_publication_and_recovers(self):
        self._exercise_publication(authority_cases=True, late_authority_drift=True)

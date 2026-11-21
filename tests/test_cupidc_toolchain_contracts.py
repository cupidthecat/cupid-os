## @file test_cupidc_toolchain_contracts.py
#  @brief Verify native contract plans and captured seed context.

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
import threading
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tests.cupidc_contract_abi_cases import CupidCContractABICases
from tests.cupidc_contract_fixtures import (
    EXPECTED_CONTRACTS,
    _ContractStageRunner,
    _test_relocatable_elf32,
    _test_static_elf32,
)
from tests.cupidc_contract_publication_cases import CupidCContractPublicationCases
from tools import bootstrap_toolchain, cupidc_toolchain_contracts
from tools.bootstrap_toolchain import _build_plan_sha256, _candidate_build_plan


class CupidCToolchainContractPlanTests(
    CupidCContractABICases, CupidCContractPublicationCases, unittest.TestCase
):
    def test_active_manifest_matches_checked_runtime_profile_guards(self):
        import re
        from collections import Counter

        root = Path(__file__).resolve().parents[1]
        manifest = (root / "toolchain/tests/cupidc_pp_active_cases.inc").read_text()
        caller = (root / "toolchain/tests/cupidc_pp_contract.cc").read_text()
        table = caller.split("active_expected_profiles[] = {", 1)[1].split(";", 1)[0]
        rows = re.findall(r'\{"([A-Z0-9_]+)",\s*([^{}]+)\}', table)
        profiles = dict(re.findall(r"^CUPIDC_PP_PROFILE\((\w+), (.+)\)$", manifest, re.M))
        self.assertEqual(len(rows), len(profiles))
        self.assertEqual({name for name, _ in rows}, set(profiles))
        counts = {
            kind: Counter(re.findall(rf"^CUPIDC_PP_{kind}\((\w+),", manifest, re.M))
            for kind in ("ACTIVE_CASE", "INCLUDE_ROOT", "MACRO", "FORCED_INCLUDE")
        }
        for name, fields in rows:
            with self.subTest(profile=name):
                values = [value.strip() for value in fields.split(",")]
                self.assertEqual(len(values), 9)
                self.assertEqual(values[:5], profiles[name].split(", "))
                for index, (kind, actual) in enumerate(counts.items(), 5):
                    with self.subTest(kind=kind):
                        self.assertRegex(values[index], r"^\d+u$")
                        self.assertEqual(int(values[index][:-1]), actual[name])

    def test_active_manifest_matches_checked_runtime_count_guards(self):
        import re

        root = Path(__file__).resolve().parents[1]
        manifest = (root / "toolchain/tests/cupidc_pp_active_cases.inc").read_text()
        caller = (root / "toolchain/tests/cupidc_pp_contract.cc").read_text()
        for kind in (
            "PROFILE", "CASE", "GENERATED_CASE", "INCLUDE_ONLY", "NON_ROOT",
            "DEFERRED_HOSTED",
        ):
            with self.subTest(kind=kind):
                guard = re.findall(
                    rf"kind_counts\[ACTIVE_ROW_{kind}\] != (\d+)u", caller
                )
                self.assertEqual(len(guard), 1)
                macro = "ACTIVE_CASE" if kind == "CASE" else kind
                actual = len(re.findall(rf"^CUPIDC_PP_{macro}\(", manifest, re.M))
                self.assertEqual(int(guard[0]), actual)
        execution_counts = re.findall(
            r"expected_count = generated == CTOOL_TRUE \? (\d+)u : (\d+)u;",
            caller,
        )
        self.assertEqual(len(execution_counts), 1)
        for expected, macro in zip(
            execution_counts[0], ("GENERATED_CASE", "ACTIVE_CASE")
        ):
            with self.subTest(executed=macro):
                actual = len(re.findall(rf"^CUPIDC_PP_{macro}\(", manifest, re.M))
                self.assertEqual(int(expected), actual)

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
            "inputs": {
                "toolchain/ctool.h": {"sha256": "5" * 64, "size": 1}
            },
            "object_comparisons": {
                name: {"sha256": "3" * 64, "size": 1}
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
                "c_objects": 44,
                "compared_generations": list(
                    cupidc_toolchain_contracts.CONVERGED_GENERATIONS
                ),
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
        output: Path, inputs: dict[str, dict[str, object]]
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
    def _write_minimal_source_root(
        root: Path,
    ) -> dict[str, dict[str, object]]:
        logical_paths = {
            plan.source
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
        } | {
            "kernel/lang/as_elf.cc",
            "kernel/lang/as_elf.h",
            "toolchain/x86.cc",
            "toolchain/path_encoding.h",
            "toolchain/native_utf8.h",
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
        return cupidc_toolchain_contracts._snapshot_contract_inputs(
            root, paths
        )

    @staticmethod
    def _write_minimal_bootstrap_root(root: Path) -> dict[str, object]:
        logical_inputs = {
            "link.ld": "SECTIONS {}\n",
            "toolchain/ctool.h": "int ctool;\n",
            "toolchain/cupidbuild.cc": "int cupidbuild;\n",
            "toolchain/cupidbuild_host.cc": "int cupidbuild_host;\n",
            "toolchain/cupidbuild_main.cc": "int cupidbuild_main;\n",
            "toolchain/seed_manifest.cc": "int seed_manifest;\n",
            "toolchain/seed_release.cc": "int seed_release;\n",
            "toolchain/contract_parse_internal.cc": "int contract_parse_internal;\n",
            "toolchain/cupidbuild_artifacts.cc": "int cupidbuild_artifacts;\n",
            "toolchain/cupidbuild_artifacts.h": "int cupidbuild_artifacts_header;\n",
            "toolchain/artifact_size_policy.cc": "int artifact_size_policy;\n",
            "toolchain/user_syscall_abi.cc": "int user_syscall_abi;\n",
            "toolchain/user_syscall_abi.h": "int user_syscall_abi_header;\n",
            "toolchain/cupidbuild_user_abi.cc": "int cupidbuild_user_abi;\n",
            "toolchain/cupidbuild_user_abi.h": "int cupidbuild_user_abi_header;\n",
            "toolchain/iso_fixture_bundle.cc": "int iso_fixture_bundle;\n",
            "toolchain/iso_fixture_bundle.h": "int iso_fixture_bundle_header;\n",
            "toolchain/cupidbuild_iso.cc": "int cupidbuild_iso;\n",
            "toolchain/cupidbuild_iso_capture.cc": "int cupidbuild_iso_capture;\n",
            "toolchain/cupidbuild_iso_image.cc": "int cupidbuild_iso_image;\n",
            "toolchain/cupidbuild_iso_publication.cc": "int cupidbuild_iso_publication;\n",
            "toolchain/cupidbuild_iso_publication.h": "int cupidbuild_iso_publication_header;\n",
            "toolchain/fat16_stage.cc": "int fat16_stage;\n",
            "toolchain/fat16_stage.h": "int fat16_stage_header;\n",
            "toolchain/fat16_names.cc": "int fat16_names;\n",
            "toolchain/fat16_names.h": "int fat16_names_header;\n",
            "toolchain/fat16_name_profiles.inc": "/* captured Unicode profile */\n",
            "toolchain/disk_image.cc": "int disk_image;\n",
            "toolchain/disk_image.h": "int disk_image_header;\n",
            "toolchain/cupidc_emit.cc": "int emit;\n",
            "toolchain/hosted/i386-linux/include/stdio.h": "int stdio;\n",
            "toolchain/hosted/i386-linux/runtime.cc": "int runtime;\n",
            "toolchain/hosted/i386-linux/start.asm": "ret\n",
        }
        for name, logical, _gnu in bootstrap_toolchain.SOURCE_OWNER_SOURCES:
            logical_inputs.setdefault(logical.lstrip('/'), 'int ' + name + ';\n')
            if name != 'path_encoding':
                logical_inputs['toolchain/' + name + '.h'] = 'int ' + name + '_header;\n'
        logical_inputs['toolchain/cupidbuild_disk_integer_tables.inc'] = '/* captured digits */\n'
        for logical_path, text in logical_inputs.items():
            path = root / logical_path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="ascii")
        build_plan = {
            "sources": [
                {
                    "gnu_extensions": False,
                    "name": "cupidc_emit",
                    "path": "/toolchain/cupidc_emit.cc",
                },
                {
                    "gnu_extensions": True,
                    "name": "runtime",
                    "path": "/toolchain/hosted/i386-linux/runtime.cc",
                },
            ],
            "startup": "/toolchain/hosted/i386-linux/start.asm",
            "links": {
                name: []
                for name in (
                    "cupidasm",
                    "cupiddis",
                    "cupidld",
                    "cupidobj",
                    "cupidc",
                )
            },
        }
        build_plan["links"]["cupidobj"] = [
            "start", "cupidobj_main", "cupidobj", "ctool_host", "ctool", "elf32", "runtime"
        ]
        build_plan_sha256 = _build_plan_sha256(build_plan)
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
            root, _candidate_build_plan(build_plan), windows_utf8=True, windows_observer_workers=True
        )
        return {
            "build_plan_sha256": _build_plan_sha256(_candidate_build_plan(build_plan)),
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
    ) -> tuple[dict[str, dict[str, object]], dict[str, object]]:
        cls._write_minimal_source_root(root)
        bootstrap = cls._write_minimal_bootstrap_root(root)
        inputs = cupidc_toolchain_contracts._snapshot_contract_inputs(
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

    def test_each_contract_plan_carries_its_compile_timeout(self):
        for plan in cupidc_toolchain_contracts.CONTRACT_PLANS:
            expected = 1800 if plan.name == "cupidc-object" else 900
            with self.subTest(contract=plan.name):
                self.assertEqual(plan.compile_timeout, expected)
                self.assertEqual(
                    plan.exclusive_compile,
                    plan.name == "cupidc-object",
                )

    def test_kernel_elf_contract_carries_its_native_link_closure(self):
        plan = next(
            plan
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
            if plan.name == "cupidasm-kernel-elf"
        )
        self.assertEqual(
            plan.link_objects,
            (
                "start",
                "contract",
                "as_elf",
                "cupidld",
                "cupidasm",
                "x86",
                "elf32",
                "ctool_host",
                "ctool",
                "runtime",
            ),
        )

    def test_contract_stage_uses_each_plan_compile_timeout(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-stage-timeout-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            source_root.mkdir()
            bootstrap_stage = root / "bootstrap"
            bootstrap_stage.mkdir()
            for name in cupidc_toolchain_contracts.CONTRACT_LINK_OBJECT_KEYS:
                (bootstrap_stage / f"{name}.o").write_bytes(b"checked")
            runner = _ContractStageRunner(source_root)

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "ToolRunner",
                return_value=runner,
            ):
                cupidc_toolchain_contracts._build_contract_stage(
                    source_root,
                    bootstrap_stage,
                    source_root / "contract-stage",
                    "stage two",
                    8,
                )

            heavy_source = "toolchain/tests/cupidc_object_contract.cc"
            self.assertEqual(
                set(runner.contract_compile_timeouts),
                {
                    plan.source
                    for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
                },
            )
            self.assertEqual(
                runner.contract_compile_timeouts[heavy_source], 1800
            )
            self.assertEqual(
                {
                    timeout
                    for source, timeout in (
                        runner.contract_compile_timeouts.items()
                    )
                    if source != heavy_source
                },
                {900},
            )

    def test_heavy_contract_compiles_after_parallel_normal_cohort(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-stage-schedule-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            source_root.mkdir()
            bootstrap_stage = root / "bootstrap"
            bootstrap_stage.mkdir()
            for name in cupidc_toolchain_contracts.CONTRACT_LINK_OBJECT_KEYS:
                (bootstrap_stage / f"{name}.o").write_bytes(b"checked")
            runner = _ContractStageRunner(
                source_root, hold_normal_compiles=True
            )
            results: list[
                tuple[dict[str, Path], dict[str, Path]]
            ] = []
            errors: list[Exception] = []

            def build_stage() -> None:
                try:
                    results.append(
                        cupidc_toolchain_contracts._build_contract_stage(
                            source_root,
                            bootstrap_stage,
                            source_root / "contract-stage",
                            "stage two",
                            8,
                        )
                    )
                except Exception as error:
                    errors.append(error)

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "ToolRunner",
                return_value=runner,
            ):
                build_thread = threading.Thread(target=build_stage)
                build_thread.start()
                normal_cohort_ready = runner.normal_cohort_ready.wait(5)
                runner.release_normal_cohort.set()
                build_thread.join(10)

            self.assertTrue(normal_cohort_ready)
            self.assertFalse(build_thread.is_alive())
            if errors:
                raise errors[0]
            self.assertEqual(len(results), 1)
            objects, executables = results[0]
            expected_names = EXPECTED_CONTRACTS | {"runtime"}
            self.assertEqual(set(executables), expected_names)
            self.assertEqual(set(objects), expected_names | {"as_elf"})
            self.assertGreaterEqual(runner.max_active_normal_compiles, 8)
            self.assertFalse(runner.heavy_compile_overlapped)
            self.assertTrue(runner.heavy_started_after_normal_cohort)
            self.assertGreaterEqual(runner.max_active_links, 2)

    def test_extended_compile_budget_requires_exclusive_admission(self):
        heavy = next(
            plan
            for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
            if plan.exclusive_compile
        )
        for invalid in (
            replace(heavy, exclusive_compile=False),
            replace(heavy, compile_timeout=900),
        ):
            with self.subTest(plan=invalid):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "extended contract compile budget must be exclusive",
                ):
                    cupidc_toolchain_contracts.validate_plans(
                        tuple(
                            invalid if plan.name == heavy.name else plan
                            for plan in (
                                cupidc_toolchain_contracts.CONTRACT_PLANS
                            )
                        )
                    )

    def test_heavy_compile_timeout_names_the_source_and_budget(self):
        heavy_source = "toolchain/tests/cupidc_object_contract.cc"
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-stage-timeout-error-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            source_root.mkdir()
            bootstrap_stage = root / "bootstrap"
            bootstrap_stage.mkdir()
            for name in cupidc_toolchain_contracts.CONTRACT_LINK_OBJECT_KEYS:
                (bootstrap_stage / f"{name}.o").write_bytes(b"checked")
            runner = _ContractStageRunner(
                source_root, timeout_source=heavy_source
            )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "ToolRunner",
                return_value=runner,
            ):
                with self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "stage two CupidC for "
                    "toolchain/tests/cupidc_object_contract[.]cc "
                    "timed out after 1800 seconds",
                ):
                    cupidc_toolchain_contracts._build_contract_stage(
                        source_root,
                        bootstrap_stage,
                        source_root / "contract-stage",
                        "stage two",
                        8,
                    )

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

    def test_source_head_publication_inventory_includes_cupidbuild(self):
        makefile = (
            Path(__file__).resolve().parents[1] / "toolchain/Makefile"
        ).read_text(encoding="utf-8")
        hosted_artifacts = makefile.split(
            "CUPIDC_HOSTED_I386_ARTIFACTS :=", 1
        )[1].split("CUPIDC_CONTRACT_ARTIFACTS :=", 1)[0]

        self.assertIn(
            "$(CONTRACT_DIR)/cupidc-cupidbuild.elf", hosted_artifacts
        )
        self.assertEqual(
            cupidc_toolchain_contracts.TOOL_NAMES,
            (
                "cupidasm",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidc",
                "cupidbuild",
            ),
        )
        self.assertEqual(
            cupidc_toolchain_contracts.BOOTSTRAP_OBJECT_NAMES[-21:],
            (
                "cupidbuild",
                "cupidbuild_host",
                "cupidbuild_main",
                "seed_manifest",
                "seed_release",
                "contract_parse_internal",
                "cupidbuild_artifacts",
                "artifact_size_policy",
                "user_syscall_abi",
                "cupidbuild_user_abi",
                "fat16_stage",
                "fat16_names",
                "disk_image",
                "cupidbuild_source_path", "cupidbuild_source_file", "cupidbuild_source_stage",
                "cupidbuild_disk_integer", "cupidbuild_disk_options", "cupidbuild_disk_input_discovery",
                "path_encoding",
                "start",
            ),
        )
        self.assertEqual(
            len(cupidc_toolchain_contracts._expected_artifact_names()), 22
        )
        self.assertEqual(
            cupidc_toolchain_contracts._tool_fixed_point_record(),
            {
                "all_equal": True,
                "c_objects": 44,
                "compared_generations": ["stage-three", "stage-four"],
                "startup_objects": 1,
                "tool_images": 6,
            },
        )

    def test_contract_input_inventory_includes_build_control_files(self):
        root = Path(__file__).resolve().parents[1]
        inputs = cupidc_toolchain_contracts._snapshot_contract_inputs(
            root,
            cupidc_toolchain_contracts._contract_input_paths(root),
        )

        self.assertEqual(len(inputs), 117)
        self.assertIn("toolchain/cupidbuild_jpeg.h", inputs)
        self.assertIn("toolchain/tests/cupidld_pe32_cases.inc", inputs)
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
        self.assertIn(
            "toolchain/tests/x86_catalogue_contract.inc", inputs
        )
        self.assertIn("toolchain/tests/x86_inline_cases.inc", inputs)
        self.assertIn("toolchain/tests/x86_integer_contract.inc", inputs)
        self.assertIn("toolchain/tests/x86_operand_contract.inc", inputs)
        self.assertIn("toolchain/x86.cc", inputs)

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

    def test_x86_contract_receives_its_sibling_catalogue_include_root(self):
        arguments = cupidc_toolchain_contracts._compile_include_arguments(
            "toolchain/tests/x86_contract.cc"
        )
        self.assertEqual(
            arguments,
            (
                "-I",
                "/toolchain",
                "-I",
                "/toolchain/tests",
                "--include-angle",
                "/toolchain/hosted/i386-linux/include",
            ),
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

    def test_manifest_author_build_uses_the_converged_cupid_tools(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-manifest-author-build-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            startup = (
                source_root / "toolchain/hosted/i386-linux/start.asm"
            )
            startup.parent.mkdir(parents=True)
            startup.write_text("ret\n", encoding="ascii")
            stage_three = root / "stage-three"
            stage_three.mkdir()
            stage_four = root / "stage-four"
            stage_four.mkdir()
            for name in ("ctool_host.o", "ctool.o", "runtime.o"):
                (stage_four / name).write_bytes(_test_relocatable_elf32())
            compiled_sources = []
            calls: list[
                tuple[Path, tuple[str | Path, ...], str, int]
            ] = []

            def compile_source(
                runner,
                compiler: Path,
                checked_root: Path,
                logical_source: str,
                output: Path,
                label: str,
                timeout: int,
            ) -> None:
                del runner, label, timeout
                self.assertEqual(compiler, stage_four / "cupidc.elf")
                self.assertEqual(checked_root, source_root)
                compiled_sources.append(logical_source)
                output.write_bytes(_test_relocatable_elf32())

            def run_clean(
                runner,
                executable: Path,
                arguments: tuple[str | Path, ...],
                label: str,
                timeout: int,
            ) -> None:
                del runner
                calls.append((executable, arguments, label, timeout))
                output = arguments[arguments.index("-o") + 1]
                self.assertIsInstance(output, Path)
                output.write_bytes(
                    _test_relocatable_elf32()
                    if "-f" in arguments
                    else _test_static_elf32()
                )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "_is_windows_host",
                return_value=False,
            ), mock.patch.object(
                cupidc_toolchain_contracts,
                "_compile_source",
                side_effect=compile_source,
            ), mock.patch.object(
                cupidc_toolchain_contracts,
                "_run_clean",
                side_effect=run_clean,
            ):
                executable = (
                    cupidc_toolchain_contracts._build_manifest_author(
                        source_root,
                        stage_four,
                        source_root / "manifest-author-build",
                    )
                )

            self.assertEqual(compiled_sources, [
                "toolchain/tests/toolchain_manifest_contract.cc",
                "toolchain/contract_parse_internal.cc",
            ])
            self.assertEqual(
                [call[0] for call in calls],
                [
                    stage_four / "cupidasm.elf",
                    stage_four / "cupidld.elf",
                ],
            )
            self.assertEqual(calls[0][1][:3], ("-f", "elf32", startup))
            self.assertEqual(
                calls[1][1][0:6],
                (
                    "-m",
                    "elf_i386",
                    "--text-address",
                    "0x08048000",
                    "--entry",
                    "_start",
                ),
            )
            self.assertEqual(
                calls[1][1][-6:],
                (
                    source_root
                    / (
                        "manifest-author-build/"
                        "toolchain-manifest-author-start.o"
                    ),
                    source_root
                    / "manifest-author-build/toolchain-manifest-author.o",
                    source_root / "manifest-author-build/toolchain-manifest-parser.o",
                    stage_four / "ctool_host.o",
                    stage_four / "ctool.o",
                    stage_four / "runtime.o",
                ),
            )
            self.assertEqual(
                executable,
                source_root
                / "manifest-author-build/toolchain-manifest-author.elf",
            )

    def test_windows_manifest_author_builds_a_validated_native_pe(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-manifest-author-windows-build-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            startup = (
                source_root
                / "toolchain/hosted/i386-windows/tool_start.asm"
            )
            startup.parent.mkdir(parents=True)
            startup.write_text("ret\n", encoding="ascii")
            runtime = (
                source_root / "toolchain/hosted/i386-windows/runtime.cc"
            )
            runtime.write_text("int runtime;\n", encoding="ascii")
            stage_four = root / "stage-four"
            stage_four.mkdir()
            compile_calls: list[
                tuple[Path, str, Path, tuple[str, ...], bool | None]
            ] = []
            tool_calls: list[
                tuple[Path, tuple[str | Path, ...], str, int]
            ] = []

            def compile_source(
                runner,
                compiler: Path,
                checked_root: Path,
                logical_source: str,
                output: Path,
                label: str,
                timeout: int,
                *,
                definitions: tuple[str, ...] = (),
                gnu_extensions: bool | None = None,
            ) -> None:
                del runner, label, timeout
                self.assertEqual(checked_root, source_root)
                compile_calls.append(
                    (
                        compiler,
                        logical_source,
                        output,
                        definitions,
                        gnu_extensions,
                    )
                )
                output.write_bytes(_test_relocatable_elf32())

            def run_clean(
                runner,
                executable: Path,
                arguments: tuple[str | Path, ...],
                label: str,
                timeout: int,
            ) -> None:
                del runner
                arguments = tuple(arguments)
                tool_calls.append((executable, arguments, label, timeout))
                output = arguments[arguments.index("-o") + 1]
                self.assertIsInstance(output, Path)
                output.write_bytes(
                    _test_relocatable_elf32()
                    if "-f" in arguments
                    else b"MZchecked author"
                )

            with (
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_is_windows_host",
                    return_value=True,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_compile_source",
                    side_effect=compile_source,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_run_clean",
                    side_effect=run_clean,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_validate_i386_relocatable",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_validate_static_i386_pe32",
                ) as validate_pe,
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_validate_static_i386_elf",
                ) as validate_elf,
            ):
                executable = (
                    cupidc_toolchain_contracts._build_manifest_author(
                        source_root,
                        stage_four,
                        source_root / "manifest-author-build",
                    )
                )

            self.assertEqual(
                [
                    (compiler, source, definitions, gnu_extensions)
                    for (
                        compiler,
                        source,
                        _output,
                        definitions,
                        gnu_extensions,
                    ) in compile_calls
                ],
                [
                    (
                        stage_four / "cupidc.elf",
                        "toolchain/tests/toolchain_manifest_contract.cc",
                        (),
                        None,
                    ),
                    (
                        stage_four / "cupidc.elf",
                        "toolchain/contract_parse_internal.cc",
                        (),
                        None,
                    ),
                    (
                        stage_four / "cupidc.elf",
                        "toolchain/hosted/i386-windows/runtime.cc",
                        ("_WIN32=1",),
                        True,
                    ),
                ],
            )
            self.assertEqual(
                [call[0] for call in tool_calls],
                [
                    stage_four / "cupidasm.elf",
                    stage_four / "cupidld.elf",
                ],
            )
            self.assertEqual(
                tool_calls[0][1][:3], ("-f", "elf32", startup)
            )
            link_arguments = tool_calls[1][1]
            self.assertEqual(
                link_arguments[:6],
                (
                    "-m",
                    "i386pe",
                    "--text-address",
                    "0x00401000",
                    "--entry",
                    "_start",
                ),
            )
            expected_import_arguments = tuple(
                argument
                for library, procedures in (
                    cupidc_toolchain_contracts.WINDOWS_TOOL_IMPORTS
                )
                for procedure in procedures
                for argument in (
                    "--import",
                    f"__imp_{procedure}={library}:{procedure}",
                )
            )
            self.assertEqual(
                link_arguments[6 : 6 + len(expected_import_arguments)],
                expected_import_arguments,
            )
            self.assertEqual(
                link_arguments[-4:],
                (
                    source_root
                    / "manifest-author-build/toolchain-manifest-author-start.o",
                    source_root
                    / "manifest-author-build/toolchain-manifest-author.o",
                    source_root
                    / "manifest-author-build/toolchain-manifest-parser.o",
                    source_root
                    / "manifest-author-build/toolchain-manifest-author-runtime.o",
                ),
            )
            self.assertEqual(
                executable,
                source_root
                / "manifest-author-build/toolchain-manifest-author.exe",
            )
            validate_pe.assert_called_once_with(
                executable,
                int(
                    cupidc_toolchain_contracts.EXPECTED_WINDOWS_TARGET[
                        "entry"
                    ]
                ),
                cupidc_toolchain_contracts.WINDOWS_TOOL_IMPORTS,
            )
            validate_elf.assert_not_called()

    def test_windows_manifest_author_rejects_invalid_pe_before_execution(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="cupid-manifest-author-windows-validation-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            startup = (
                source_root
                / "toolchain/hosted/i386-windows/tool_start.asm"
            )
            startup.parent.mkdir(parents=True)
            startup.write_text("ret\n", encoding="ascii")
            runtime = (
                source_root / "toolchain/hosted/i386-windows/runtime.cc"
            )
            runtime.write_text("int runtime;\n", encoding="ascii")
            stage_four = root / "stage-four"
            stage_four.mkdir()

            def compile_source(
                runner,
                compiler: Path,
                checked_root: Path,
                logical_source: str,
                output: Path,
                label: str,
                timeout: int,
                **configuration,
            ) -> None:
                del (
                    runner,
                    compiler,
                    checked_root,
                    logical_source,
                    label,
                    timeout,
                    configuration,
                )
                output.write_bytes(_test_relocatable_elf32())

            def run_clean(
                runner,
                executable: Path,
                arguments: tuple[str | Path, ...],
                label: str,
                timeout: int,
            ) -> None:
                del runner, executable, label, timeout
                arguments = tuple(arguments)
                output = arguments[arguments.index("-o") + 1]
                self.assertIsInstance(output, Path)
                output.write_bytes(
                    _test_relocatable_elf32()
                    if "-f" in arguments
                    else b"MZinvalid author"
                )

            with (
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_is_windows_host",
                    return_value=True,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_compile_source",
                    side_effect=compile_source,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_run_clean",
                    side_effect=run_clean,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_validate_i386_relocatable",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_validate_static_i386_pe32",
                    side_effect=cupidc_toolchain_contracts.BootstrapError(
                        "injected invalid PE"
                    ),
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts.ToolRunner,
                    "run",
                ) as run_tool,
                self.assertRaisesRegex(
                    cupidc_toolchain_contracts.ContractError,
                    "invalid Windows Toolchain manifest author",
                ),
            ):
                cupidc_toolchain_contracts._build_manifest_author(
                    source_root,
                    stage_four,
                    source_root / "manifest-author-build",
                )
            run_tool.assert_not_called()

    def test_manifest_author_rehashes_and_rechecks_the_frozen_source(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-manifest-author-source-"
        ) as temporary:
            root = Path(temporary)
            source_root = root / "source"
            source = source_root / "toolchain/source.cc"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"frozen author input\n")
            bootstrap_source = source_root / "toolchain/bootstrap.cc"
            bootstrap_source.write_bytes(b"frozen bootstrap input\n")
            stage_three = root / "stage-three"
            stage_three.mkdir()
            stage_four = root / "stage-four"
            stage_four.mkdir()
            workspace = root / "workspace"
            workspace.mkdir()
            manifest = root / "manifest.json"
            manifest.write_bytes(b"seed manifest\n")
            manifest_relative = "seed/manifest.json"
            oracle_inputs = {
                "toolchain/source.cc": {"sha256": "0" * 64, "size": 999}
            }
            report = {
                "bootstrap": {
                    "seed_manifest": {
                        "path": manifest_relative,
                        "sha256": "a" * 64,
                    },
                    "source_inputs": {
                        "files": {
                            "toolchain/bootstrap.cc": {
                                "sha256": "c" * 64,
                                "size": 999,
                            }
                        },
                        "sha256": "b" * 64,
                    },
                },
                "inputs": oracle_inputs,
                "tool_fixed_point": {},
            }
            seed = SimpleNamespace(
                manifest_sha256="a" * 64,
                manifest_bytes=b"seed manifest\n",
                artifact_bytes=(),
                tools={},
            )
            captured: dict[str, object] = {}

            def author_request(
                artifact_observations,
                input_observations,
                bootstrap_observations,
                bootstrap_snapshot_sha256,
                seed_path,
                seed_manifest,
                seed_observations,
                object_pairs,
                executable_pairs,
                bootstrap_object_pairs,
                bootstrap_tool_pairs,
                *, seed_context,
            ):
                del (
                    artifact_observations,
                    seed_path,
                    seed_manifest,
                    seed_observations,
                    object_pairs,
                    executable_pairs,
                    bootstrap_object_pairs,
                    bootstrap_tool_pairs,
                )
                captured["inputs"] = input_observations
                captured["bootstrap"] = bootstrap_observations
                captured["bootstrap_sha256"] = (
                    bootstrap_snapshot_sha256
                )
                captured["seed_context"] = seed_context
                return b"author request"

            with (
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "freeze_seed_inputs",
                    return_value=seed,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_manifest_seed_context",
                    return_value=b"checked captured seed context",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_manifest_author_request",
                    side_effect=author_request,
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_build_manifest_author",
                    return_value=workspace / "author.elf",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_capture_stage_pairs",
                    return_value=(),
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "ToolRunner",
                ) as runner_type,
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "require_live_seed_inputs",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_require_inputs_unchanged",
                ) as require_inputs,
            ):
                runner_type.return_value.run.return_value = (
                    subprocess.CompletedProcess([], 0, "authored\n", "")
                )

                result = (
                    cupidc_toolchain_contracts._checked_manifest_author_bytes(
                        source_root,
                        stage_three,
                        stage_four,
                        workspace,
                        manifest,
                        manifest_relative,
                        report,
                        (),
                        {},
                        {},
                        {},
                        {},
                    )
                )

            self.assertEqual(result, b"authored\n")
            self.assertEqual(captured["seed_context"], b"checked captured seed context")
            self.assertEqual(
                captured["inputs"],
                (
                    (
                        "toolchain/source.cc",
                        1,
                        len(b"frozen author input\n"),
                        hashlib.sha256(b"frozen author input\n").hexdigest(),
                    ),
                ),
            )
            self.assertEqual(
                captured["bootstrap"],
                (
                    (
                        "toolchain/bootstrap.cc",
                        1,
                        23,
                        "a5e50d5a07183f751154345eb1cfdcdb00eb5af47942c405"
                        "88561fec202d642f",
                    ),
                ),
            )
            self.assertEqual(
                captured["bootstrap_sha256"],
                "633989f6d686744936dd39811e588a4a353530cd15c638d328d0f41e"
                "09937fc9",
            )
            self.assertEqual(
                require_inputs.call_args_list,
                [
                    mock.call(source_root, oracle_inputs),
                    mock.call(source_root, oracle_inputs),
                ],
            )

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
                    "_snapshot_contract_inputs",
                    return_value={},
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "_bootstrap_for_manifest_author",
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

    def test_manifest_author_pair_capture_rejects_symlinks(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-stage-link-"
        ) as temporary:
            root = Path(temporary)
            target = root / "target.o"
            target.write_bytes(b"object")
            linked = root / "linked.o"
            try:
                linked.symlink_to(target)
            except OSError:
                self.skipTest("file symlinks are unavailable")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "contract object stage file is not a regular file",
            ):
                cupidc_toolchain_contracts._capture_stage_pairs(
                    {"sample": linked},
                    {"sample": target},
                    "contract object",
                )

    def test_manifest_author_pair_capture_rejects_identity_races_and_recovers(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-stage-race-"
        ) as temporary:
            root = Path(temporary)
            stage_file = root / "stage.o"

            stage_file.write_bytes(b"before-open")
            replacement = root / "open-replacement.o"
            replacement.write_bytes(b"replacement-before-open")
            real_open = os.open

            def replace_before_open(path: Path, flags: int) -> int:
                replacement.replace(stage_file)
                return real_open(path, flags)

            with mock.patch.object(
                cupidc_toolchain_contracts.os,
                "open",
                side_effect=replace_before_open,
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "stage file identity changed",
            ):
                cupidc_toolchain_contracts._capture_regular_stage_file(
                    stage_file, "contract object", "sample"
                )
            self.assertEqual(
                cupidc_toolchain_contracts._capture_regular_stage_file(
                    stage_file, "contract object", "sample"
                ),
                (1, b"replacement-before-open"),
            )

            stage_file.write_bytes(b"during-read")
            real_fstat = os.fstat
            fstat_calls = 0

            def change_final_descriptor_status(
                descriptor: int,
            ) -> os.stat_result | SimpleNamespace:
                nonlocal fstat_calls
                status = real_fstat(descriptor)
                fstat_calls += 1
                if fstat_calls != 2:
                    return status
                return SimpleNamespace(
                    st_dev=status.st_dev,
                    st_ino=status.st_ino,
                    st_mode=status.st_mode,
                    st_size=status.st_size + 1,
                    st_mtime_ns=status.st_mtime_ns,
                )

            with mock.patch.object(
                cupidc_toolchain_contracts.os,
                "fstat",
                side_effect=change_final_descriptor_status,
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "stage file changed during capture",
            ):
                cupidc_toolchain_contracts._capture_regular_stage_file(
                    stage_file, "contract object", "sample"
                )
            self.assertEqual(
                cupidc_toolchain_contracts._capture_regular_stage_file(
                    stage_file, "contract object", "sample"
                ),
                (1, b"during-read"),
            )

            stage_file.write_bytes(b"before-close")
            replacement = root / "close-replacement.o"
            replacement.write_bytes(b"replacement-after-close")
            real_close = os.close

            def replace_after_close(descriptor: int) -> None:
                real_close(descriptor)
                replacement.replace(stage_file)

            with mock.patch.object(
                cupidc_toolchain_contracts.os,
                "close",
                side_effect=replace_after_close,
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "stage file changed during capture",
            ):
                cupidc_toolchain_contracts._capture_regular_stage_file(
                    stage_file, "contract object", "sample"
                )
            self.assertEqual(
                cupidc_toolchain_contracts._capture_regular_stage_file(
                    stage_file, "contract object", "sample"
                ),
                (1, b"replacement-after-close"),
            )

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

    def test_manifest_author_captures_both_stage_streams_before_comparison(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="cupid-contract-pair-capture-"
        ) as temporary:
            root = Path(temporary)
            stage_three = root / "stage-three.o"
            stage_four = root / "stage-four.o"
            stage_three.write_bytes(b"stage three bytes")
            stage_four.write_bytes(b"stage four bytes")

            pairs = cupidc_toolchain_contracts._capture_stage_pairs(
                {"core": stage_three},
                {"core": stage_four},
                "contract object",
            )

            self.assertEqual(
                pairs,
                ((
                    "core",
                    1,
                    b"stage three bytes",
                    1,
                    b"stage four bytes",
                ),),
            )


class ManifestSeedContextTests(unittest.TestCase):
    def setUp(self):
        self.manifest = (Path(__file__).resolve().parents[1] /
                         "bootstrap/seeds/i386-linux/manifest.json")
        self.seed = bootstrap_toolchain.verify_seed_inputs(self.manifest)
        self.artifacts = tuple((self.seed.tools[role].name, payload)
                               for role, payload in self.seed.artifact_bytes)

    def test_captured_cohort_matches_the_live_shared_reader(self):
        context = cupidc_toolchain_contracts._manifest_seed_context(self.seed)
        captured = cupidc_toolchain_contracts._manifest_seed_context_from_captured(
            self.seed.manifest_bytes, self.artifacts)
        self.assertEqual(captured, context)
        self.assertIn(self.seed.manifest_sha256.encode("ascii"), context)
        for row in self.seed.manifest["artifacts"]:
            self.assertIn(row["sha256"].encode("ascii"), context)

    def test_captured_cohort_rejects_missing_duplicate_and_extra_tools(self):
        for artifacts in (self.artifacts[:-1],
                          (*self.artifacts[:-1], self.artifacts[0]),
                          (*self.artifacts, ("unexpected.elf", b"image"))):
            with self.subTest(names=[name for name, _payload in artifacts]):
                with self.assertRaisesRegex(cupidc_toolchain_contracts.ContractError,
                                            "tool inventory differs"):
                    cupidc_toolchain_contracts._manifest_seed_context_from_captured(
                        self.seed.manifest_bytes, artifacts)

    def test_captured_cohort_rejects_changed_manifest_before_framing(self):
        changed = json.loads(self.seed.manifest_bytes)
        changed["provenance"]["source_revision"] = "0" * 40
        with self.assertRaises(cupidc_toolchain_contracts.ContractError):
            cupidc_toolchain_contracts._manifest_seed_context_from_captured(
                json.dumps(changed).encode("ascii"), self.artifacts)

    def test_captured_context_binds_valid_manifest_formatting(self):
        payload = b"\n" + self.seed.manifest_bytes
        context = cupidc_toolchain_contracts._manifest_seed_context_from_captured(
            payload, self.artifacts)
        self.assertIn(hashlib.sha256(payload).hexdigest().encode("ascii"), context)
        self.assertNotEqual(context,
                            cupidc_toolchain_contracts._manifest_seed_context(self.seed))

    def test_captured_cohort_rejects_image_drift_and_recovers(self):
        name, payload = self.artifacts[0]
        changed = bytes([payload[0] ^ 1]) + payload[1:]
        with self.assertRaises(cupidc_toolchain_contracts.ContractError):
            cupidc_toolchain_contracts._manifest_seed_context_from_captured(
                self.seed.manifest_bytes, ((name, changed), *self.artifacts[1:]))
        self.assertEqual(
            cupidc_toolchain_contracts._manifest_seed_context_from_captured(
                self.seed.manifest_bytes, self.artifacts),
            cupidc_toolchain_contracts._manifest_seed_context(self.seed))

    def test_captured_context_rejects_live_seed_drift_and_recovers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "manifest.json"
            manifest.write_bytes(self.seed.manifest_bytes)
            for name, payload in self.artifacts:
                (root / name).write_bytes(payload)
            captured = bootstrap_toolchain.freeze_seed_inputs(manifest, root / "frozen")
            expected = cupidc_toolchain_contracts._manifest_seed_context(captured)
            name, payload = self.artifacts[0]
            (root / name).write_bytes(payload + b"changed")
            with self.assertRaises(cupidc_toolchain_contracts.ContractError):
                cupidc_toolchain_contracts._manifest_seed_context(captured)
            (root / name).write_bytes(payload)
            self.assertEqual(cupidc_toolchain_contracts._manifest_seed_context(captured),
                             expected)

    def test_windows_execution_seed_cannot_supply_linux_policy_facts(self):
        manifest = self.manifest.parent.parent / "i386-windows/manifest.json"
        captured = bootstrap_toolchain.verify_seed_inputs(manifest)
        with self.assertRaisesRegex(cupidc_toolchain_contracts.ContractError,
                                    "checked cohort"):
            cupidc_toolchain_contracts._manifest_seed_context(captured)


if __name__ == "__main__":
    unittest.main()

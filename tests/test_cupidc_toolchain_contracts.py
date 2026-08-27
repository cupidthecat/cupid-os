from __future__ import annotations

import hashlib
import json
import os
import struct
import subprocess
import tempfile
import threading
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import cupidc_toolchain_contracts
from tools.bootstrap_toolchain import _candidate_build_plan


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


def _test_relocatable_elf32() -> bytes:
    image = bytearray(52)
    image[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", image, 16, 1, 3, 1)
    return bytes(image)


def _test_static_elf32() -> bytes:
    image = bytearray(85)
    identity = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<16sHHIIIIIHHHHHH",
        image,
        0,
        identity,
        2,
        3,
        1,
        cupidc_toolchain_contracts.TARGET_ENTRY,
        52,
        0,
        0,
        52,
        32,
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        "<IIIIIIII",
        image,
        52,
        1,
        84,
        cupidc_toolchain_contracts.TARGET_ENTRY,
        cupidc_toolchain_contracts.TARGET_ENTRY,
        1,
        1,
        5,
        1,
    )
    image[84] = 0xC3
    return bytes(image)


class _ContractStageRunner:
    def __init__(
        self,
        source_root: Path,
        *,
        hold_normal_compiles: bool = False,
        timeout_source: str | None = None,
    ) -> None:
        self.source_root = source_root
        self.hold_normal_compiles = hold_normal_compiles
        self.timeout_source = timeout_source
        self.contract_compile_timeouts: dict[str, int] = {}
        self.normal_cohort_ready = threading.Event()
        self.release_normal_cohort = threading.Event()
        self.heavy_compile_overlapped = False
        self.heavy_started_after_normal_cohort = False
        self.completed_normal_compiles: set[str] = set()
        self.max_active_normal_compiles = 0
        self.max_active_links = 0
        self._active_normal_compiles = 0
        self._active_links = 0
        self._link_pair_ready = threading.Event()
        self._lock = threading.Lock()

    def _output_path(self, argument: str | Path) -> Path:
        if isinstance(argument, Path):
            return argument
        return self.source_root / argument.removeprefix("/")

    def run(
        self,
        executable: Path,
        arguments: tuple[str | Path, ...],
        timeout: int,
    ) -> subprocess.CompletedProcess[str]:
        del executable
        arguments = tuple(arguments)
        output = self._output_path(arguments[arguments.index("-o") + 1])
        output.parent.mkdir(parents=True, exist_ok=True)
        if "-c" in arguments:
            logical_source = str(arguments[arguments.index("-c") + 1])
            logical_source = logical_source.removeprefix("/")
            contract_sources = {
                plan.source
                for plan in cupidc_toolchain_contracts.CONTRACT_PLANS
            }
            is_contract = logical_source in contract_sources
            is_heavy = logical_source == (
                "toolchain/tests/cupidc_object_contract.cc"
            )
            if is_contract:
                with self._lock:
                    self.contract_compile_timeouts[logical_source] = timeout
                    if is_heavy:
                        self.heavy_compile_overlapped = (
                            self._active_normal_compiles > 0
                        )
                        self.heavy_started_after_normal_cohort = (
                            self.completed_normal_compiles
                            == contract_sources - {logical_source}
                        )
                    elif self.hold_normal_compiles:
                        self._active_normal_compiles += 1
                        self.max_active_normal_compiles = max(
                            self.max_active_normal_compiles,
                            self._active_normal_compiles,
                        )
                        if self._active_normal_compiles >= 8:
                            self.normal_cohort_ready.set()
                if (
                    self.hold_normal_compiles
                    and not is_heavy
                    and not self.release_normal_cohort.wait(5)
                ):
                    raise AssertionError(
                        "normal contract compiles were not released"
                    )
                if self.hold_normal_compiles and not is_heavy:
                    with self._lock:
                        self._active_normal_compiles -= 1
            if logical_source == self.timeout_source:
                raise subprocess.TimeoutExpired(logical_source, timeout)
            output.write_bytes(_test_relocatable_elf32())
            if is_contract and not is_heavy:
                with self._lock:
                    self.completed_normal_compiles.add(logical_source)
        else:
            with self._lock:
                self._active_links += 1
                self.max_active_links = max(
                    self.max_active_links, self._active_links
                )
                if self._active_links >= 2:
                    self._link_pair_ready.set()
            if not self._link_pair_ready.wait(5):
                raise AssertionError("contract links did not overlap")
            output.write_bytes(_test_static_elf32())
            with self._lock:
                self._active_links -= 1
        return subprocess.CompletedProcess([], 0, "", "")


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
                "c_objects": 22,
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
            root, _candidate_build_plan(build_plan)
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
            cupidc_toolchain_contracts.BOOTSTRAP_OBJECT_NAMES[-4:],
            (
                "cupidbuild",
                "cupidbuild_host",
                "cupidbuild_main",
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
                "c_objects": 22,
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

        self.assertEqual(len(inputs), 75)
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
                self.assertEqual(
                    logical_source,
                    "toolchain/tests/toolchain_manifest_contract.cc",
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
                calls[1][1][-5:],
                (
                    source_root
                    / (
                        "manifest-author-build/"
                        "toolchain-manifest-author-start.o"
                    ),
                    source_root
                    / "manifest-author-build/toolchain-manifest-author.o",
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
                link_arguments[-3:],
                (
                    source_root
                    / "manifest-author-build/toolchain-manifest-author-start.o",
                    source_root
                    / "manifest-author-build/toolchain-manifest-author.o",
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
                return b"author request"

            with (
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "freeze_seed_inputs",
                    return_value=seed,
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

    def test_build_publishes_the_declared_converged_generation(self):
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
            ) -> dict[str, object]:
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
                    "tool_fixed_point": (
                        cupidc_toolchain_contracts._tool_fixed_point_record()
                    ),
                }
                return (
                    json.dumps(authored_report, indent=2, sort_keys=True)
                    + "\n"
                ).encode("ascii")

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
                self.assertEqual(
                    list(
                        output.parent.glob(
                            f".{output.name}-build-*"
                        )
                    ),
                    [],
                )

            self.assertEqual(
                built_generations,
                ["stage-three", "stage-four"] * 4,
            )
            self.assertEqual(runtime_generations, ["stage-four"] * 4)
            self.assertEqual(author_generations, ["stage-four"] * 4)
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
                + successful_decision,
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

    def test_windows_user_abi_uses_the_native_seed_without_a_publication(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-selection-"
        ) as temporary:
            root = Path(temporary).resolve()
            output = root / "toolchain/build/cupidc-contracts"
            output.mkdir(parents=True)
            sentinel = output / "occupied-publication.txt"
            sentinel.write_bytes(b"leave this publication alone")
            linux_manifest = (
                root / "bootstrap/seeds/i386-linux/manifest.json"
            )
            windows_manifest = (
                root / "bootstrap/seeds/i386-windows/manifest.json"
            )
            expected = {
                "schema": "cupid.user-syscall-abi.v1",
                "field_count": 103,
            }

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "_run_native_windows_user_syscall_abi",
                return_value=expected,
            ) as native, mock.patch.object(
                cupidc_toolchain_contracts,
                "ensure_contracts",
            ) as ensure:
                actual = cupidc_toolchain_contracts.run_user_syscall_abi(
                    root,
                    linux_manifest,
                    output,
                    workers=3,
                    timeout=45,
                    windows_manifest=windows_manifest,
                )

            self.assertEqual(actual, expected)
            self.assertEqual(
                sentinel.read_bytes(), b"leave this publication alone"
            )
            native.assert_called_once_with(root, windows_manifest, 45)
            ensure.assert_not_called()

    def test_native_windows_user_abi_source_snapshot_rejects_live_drift(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-inputs-"
        ) as temporary:
            workspace = Path(temporary)
            root = workspace / "source"
            for logical_path in (
                cupidc_toolchain_contracts.NATIVE_WINDOWS_USER_ABI_BUILD_INPUTS
            ):
                path = root / logical_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    f"checked input: {logical_path}\n", encoding="ascii"
                )
            snapshot = workspace / "snapshot"
            expected = (
                cupidc_toolchain_contracts._freeze_native_windows_user_abi_inputs(
                    root, snapshot
                )
            )

            changed = root / "toolchain/ctool.cc"
            changed.write_text("changed during the build\n", encoding="ascii")

            with self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "native Windows user ABI inputs changed",
            ):
                cupidc_toolchain_contracts._require_native_windows_user_abi_inputs_unchanged(
                    root, expected
                )

    def test_native_windows_user_abi_runs_one_pe_against_the_shared_snapshot(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-windows/manifest.json"
        expected = {
            "schema": "cupid.user-syscall-abi.v1",
            "field_count": 103,
        }
        completed = subprocess.CompletedProcess(
            ["user-syscall-abi-contract.exe"],
            0,
            json.dumps(expected) + "\n",
            "",
        )
        seed = SimpleNamespace(
            manifest={"schema": "cupid.execution-seed.v2"},
            tools={},
        )

        with mock.patch.object(
            cupidc_toolchain_contracts,
            "_is_windows_host",
            return_value=True,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "freeze_seed_inputs",
            return_value=seed,
        ) as freeze_seed, mock.patch.object(
            cupidc_toolchain_contracts,
            "_freeze_native_windows_user_abi_inputs",
            return_value={"toolchain/ctool.cc": "a" * 64},
        ) as freeze_source, mock.patch.object(
            cupidc_toolchain_contracts,
            "_build_native_windows_user_abi_contract",
        ) as build, mock.patch.object(
            cupidc_toolchain_contracts,
            "ToolRunner",
        ) as runner_type, mock.patch.object(
            cupidc_toolchain_contracts,
            "check_syscall_abi",
            return_value=expected,
        ) as oracle, mock.patch.object(
            cupidc_toolchain_contracts,
            "require_live_seed_inputs",
        ) as require_seed, mock.patch.object(
            cupidc_toolchain_contracts,
            "_require_native_windows_user_abi_inputs_unchanged",
        ) as require_source:
            runner_type.return_value.run.return_value = completed

            def publish_executable(
                source_root, build_root, frozen_seed, runner, timeout
            ):
                executable = build_root / "user-syscall-abi-contract.exe"
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_bytes(b"MZ")
                return executable

            build.side_effect = publish_executable
            actual = (
                cupidc_toolchain_contracts._run_native_windows_user_syscall_abi(
                    root, manifest, 45
                )
            )

        self.assertEqual(actual, expected)
        frozen_source = freeze_source.call_args.args[1]
        frozen_seed = freeze_seed.call_args.args[1]
        self.assertNotEqual(frozen_source, root)
        self.assertNotEqual(frozen_seed, manifest.parent)
        executable = build.call_args.args[1] / "user-syscall-abi-contract.exe"
        runner_type.return_value.run.assert_called_once_with(
            executable,
            ("check-snapshot", frozen_source, root),
            45,
        )
        oracle.assert_called_once_with(frozen_source)
        require_seed.assert_called_once_with(seed)
        require_source.assert_called_once_with(
            root, {"toolchain/ctool.cc": "a" * 64}
        )

    def test_native_windows_user_abi_build_uses_checked_pe_producers(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-build-"
        ) as temporary:
            source_root = Path(temporary)
            build_root = source_root / "build/user-syscall-abi"
            tools = {
                "cupidc": source_root / "seed/cupidc.exe",
                "cupidasm": source_root / "seed/cupidasm.exe",
                "cupidld": source_root / "seed/cupidld.exe",
            }
            seed = SimpleNamespace(tools=tools)
            calls = []

            class RecordingRunner:
                def run(self, executable, arguments, timeout):
                    arguments = tuple(arguments)
                    calls.append((executable, arguments, timeout))
                    output = arguments[arguments.index("-o") + 1]
                    output_path = (
                        source_root / str(output).lstrip("/")
                        if str(output).startswith("/")
                        else Path(output)
                    )
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    if executable == tools["cupidld"]:
                        output_path.write_bytes(b"MZchecked")
                    else:
                        output_path.write_bytes(_test_relocatable_elf32())
                    return subprocess.CompletedProcess(
                        [str(executable)], 0, "", ""
                    )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "_validate_static_i386_pe32",
            ) as validate_pe, mock.patch.object(
                cupidc_toolchain_contracts,
                "require_live_seed_inputs",
            ) as require_seed:
                executable = (
                    cupidc_toolchain_contracts._build_native_windows_user_abi_contract(
                        source_root,
                        build_root,
                        seed,
                        RecordingRunner(),
                        45,
                    )
                )

            self.assertEqual(
                executable,
                build_root / "user-syscall-abi-contract.exe",
            )
            self.assertEqual(
                [call[0] for call in calls],
                [
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidc"],
                    tools["cupidasm"],
                    tools["cupidld"],
                ],
            )
            compile_sources = [
                call[1][call[1].index("-c") + 1] for call in calls[:4]
            ]
            self.assertEqual(
                compile_sources,
                [
                    "/toolchain/tests/user_syscall_abi_contract.cc",
                    "/toolchain/ctool_host.cc",
                    "/toolchain/ctool.cc",
                    "/toolchain/hosted/i386-windows/runtime.cc",
                ],
            )
            link_arguments = calls[-1][1]
            self.assertEqual(link_arguments[:6], (
                "-m",
                "i386pe",
                "--text-address",
                "0x00401000",
                "--entry",
                "_start",
            ))
            self.assertIn(
                "__imp_VirtualAlloc=KERNEL32.dll:VirtualAlloc",
                link_arguments,
            )
            self.assertEqual(require_seed.call_count, len(calls))
            validate_pe.assert_called_once()

    def test_native_windows_user_abi_rejects_a_malformed_pe(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-malformed-pe-"
        ) as temporary:
            source_root = Path(temporary)
            tools = {
                "cupidc": source_root / "seed/cupidc.exe",
                "cupidasm": source_root / "seed/cupidasm.exe",
                "cupidld": source_root / "seed/cupidld.exe",
            }
            seed = SimpleNamespace(tools=tools)

            class MalformedPeRunner:
                def run(self, executable, arguments, timeout):
                    arguments = tuple(arguments)
                    output = arguments[arguments.index("-o") + 1]
                    output_path = (
                        source_root / str(output).lstrip("/")
                        if str(output).startswith("/")
                        else Path(output)
                    )
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    output_path.write_bytes(
                        b"MZbroken"
                        if executable == tools["cupidld"]
                        else _test_relocatable_elf32()
                    )
                    return subprocess.CompletedProcess(
                        [str(executable)], 0, "", ""
                    )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "require_live_seed_inputs",
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "invalid PE contract",
            ):
                cupidc_toolchain_contracts._build_native_windows_user_abi_contract(
                    source_root,
                    source_root / "build/user-syscall-abi",
                    seed,
                    MalformedPeRunner(),
                    45,
                )

    def test_native_windows_user_abi_rejects_seed_drift_during_compile(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-seed-drift-"
        ) as temporary:
            source_root = Path(temporary)
            tools = {
                "cupidc": source_root / "seed/cupidc.exe",
                "cupidasm": source_root / "seed/cupidasm.exe",
                "cupidld": source_root / "seed/cupidld.exe",
            }
            seed = SimpleNamespace(tools=tools)

            class OneObjectRunner:
                def run(self, executable, arguments, timeout):
                    arguments = tuple(arguments)
                    output = arguments[arguments.index("-o") + 1]
                    output_path = source_root / str(output).lstrip("/")
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    output_path.write_bytes(_test_relocatable_elf32())
                    return subprocess.CompletedProcess(
                        [str(executable)], 0, "", ""
                    )

            with mock.patch.object(
                cupidc_toolchain_contracts,
                "require_live_seed_inputs",
                side_effect=cupidc_toolchain_contracts.BootstrapError(
                    "checked seed artifact changed"
                ),
            ), self.assertRaisesRegex(
                cupidc_toolchain_contracts.ContractError,
                "native Windows CupidC.*could not run",
            ):
                cupidc_toolchain_contracts._build_native_windows_user_abi_contract(
                    source_root,
                    source_root / "build/user-syscall-abi",
                    seed,
                    OneObjectRunner(),
                    45,
                )

    def test_native_windows_user_abi_rejects_a_linux_seed_before_building(self):
        root = Path("contract-root").resolve()
        manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        seed = SimpleNamespace(
            manifest={"schema": "cupid.bootstrap-seed.v1"},
            tools={},
        )
        with mock.patch.object(
            cupidc_toolchain_contracts,
            "_is_windows_host",
            return_value=True,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_freeze_native_windows_user_abi_inputs",
            return_value={},
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "freeze_seed_inputs",
            return_value=seed,
        ), mock.patch.object(
            cupidc_toolchain_contracts,
            "_build_native_windows_user_abi_contract",
        ) as build, self.assertRaisesRegex(
            cupidc_toolchain_contracts.ContractError,
            "requires the checked Windows execution seed",
        ):
            cupidc_toolchain_contracts._run_native_windows_user_syscall_abi(
                root, manifest, 45
            )
        build.assert_not_called()

    def test_user_abi_cli_accepts_a_separate_windows_execution_manifest(self):
        arguments = cupidc_toolchain_contracts._build_parser().parse_args(
            [
                "user-abi",
                "--root",
                "source",
                "--manifest",
                "bootstrap/seeds/i386-linux/manifest.json",
                "--windows-manifest",
                "bootstrap/seeds/i386-windows/manifest.json",
                "--output",
                "toolchain/build/cupidc-contracts",
            ]
        )
        self.assertEqual(
            arguments.windows_manifest,
            Path("bootstrap/seeds/i386-windows/manifest.json"),
        )

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
        build.assert_called_once_with(root, manifest, output, 3)

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

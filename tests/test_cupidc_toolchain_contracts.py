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

    def test_contract_input_inventory_includes_build_control_files(self):
        root = Path(__file__).resolve().parents[1]
        inputs = cupidc_toolchain_contracts._snapshot_inputs(
            root,
            cupidc_toolchain_contracts._contract_input_paths(root),
        )

        self.assertEqual(len(inputs), 68)
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
                    for tool_name in cupidc_toolchain_contracts.TOOL_NAMES:
                        (stage / f"{tool_name}.elf").write_bytes(
                            f"{generation}:{tool_name}".encode("ascii")
                        )
                return {
                    "build_plan_sha256": "1" * 64,
                    "comparisons": {
                        "all_equal": True,
                        "c_objects": 19,
                        "compared_generations": [
                            "stage-three",
                            "stage-four",
                        ],
                        "startup_objects": 1,
                        "tool_images": len(
                            cupidc_toolchain_contracts.TOOL_NAMES
                        ),
                    },
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
                del artifact_kind
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
                    "_freeze_contract_inputs",
                ),
                mock.patch.object(
                    cupidc_toolchain_contracts,
                    "bootstrap_from_seed",
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

            self.assertEqual(
                built_generations, ["stage-three", "stage-four"]
            )
            self.assertEqual(runtime_generations, ["stage-four"])
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
            manifest={"schema": "cupid.execution-seed.v1"},
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

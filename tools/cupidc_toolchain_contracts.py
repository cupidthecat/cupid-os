#!/usr/bin/env python3
"""Build Cupid-owned toolchain contracts across the checked fixed point."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Sequence

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_SOURCES,
        ToolRunner,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        bootstrap_from_seed,
        capture_source_snapshot,
        verify_seed_inputs,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_SOURCES,
        ToolRunner,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        bootstrap_from_seed,
        capture_source_snapshot,
        verify_seed_inputs,
    )

try:
    from tools.user_syscall_abi import (
        UserSyscallAbiError,
        check_syscall_abi,
    )
except ModuleNotFoundError:
    from user_syscall_abi import UserSyscallAbiError, check_syscall_abi


REPORT_SCHEMA = "cupid.toolchain-contracts.v2"
TARGET_ENTRY = 0x08048000
TOOL_NAMES = ("cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc")
TOOL_PUBLIC_NAMES = {
    name: f"cupidc-{name}.elf" for name in TOOL_NAMES
}
KERNEL_LANG_SOURCES = frozenset(
    {
        "kernel/lang/as_elf.cc",
        "toolchain/tests/cupidasm_kernel_elf_contract.cc",
    }
)
GNU_CONTRACT_SOURCES = frozenset(
    {"toolchain/tests/hosted_i386_runtime_contract.cc"}
)
CONTRACT_CONTROL_INPUTS = (
    "toolchain/Makefile",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
)
WINDOWS_RUNTIME_INPUTS = (
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
)
USER_SYSCALL_ABI_INPUTS = (
    "kernel/core/types.h",
    "kernel/core/syscall.h",
    "kernel/core/syscall.cc",
    "kernel/fs/vfs.h",
    "kernel/network/socket.h",
    "user/cupid.h",
)
CONTRACT_LINK_OBJECT_KEYS = frozenset(
    {
        name
        for name, _source, _gnu_extensions in EXPECTED_SOURCES
        if not name.endswith("_main")
    }
    | {"as_elf", "contract", "start"}
)


class ContractError(RuntimeError):
    """A checked contract build or publication failed."""


def _announce(message: str) -> None:
    print(f"CupidC toolchain contracts: {message}", flush=True)


@dataclass(frozen=True)
class ContractPlan:
    name: str
    source: str
    link_objects: tuple[str, ...]

    @property
    def artifact(self) -> str:
        return f"{self.name}-contract.elf"


CONTRACT_PLANS = (
    ContractPlan(
        "core",
        "toolchain/tests/core_contract.cc",
        ("start", "contract", "ctool_host", "ctool", "runtime"),
    ),
    ContractPlan(
        "user-syscall-abi",
        "toolchain/tests/user_syscall_abi_contract.cc",
        ("start", "contract", "ctool_host", "ctool", "runtime"),
    ),
    ContractPlan(
        "cupidc-pp",
        "toolchain/tests/cupidc_pp_contract.cc",
        ("start", "contract", "cupidc_pp", "ctool_host", "ctool", "runtime"),
    ),
    ContractPlan(
        "cupidc-type",
        "toolchain/tests/cupidc_type_contract.cc",
        (
            "start",
            "contract",
            "cupidc_type",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidc-frontend",
        "toolchain/tests/cupidc_frontend_contract.cc",
        (
            "start",
            "contract",
            "cupidc_frontend",
            "cupidc_type",
            "cupidc_pp",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidc-ir",
        "toolchain/tests/cupidc_ir_contract.cc",
        (
            "start",
            "contract",
            "cupidc_ir",
            "cupidc_frontend",
            "cupidc_type",
            "cupidc_pp",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidc-object",
        "toolchain/tests/cupidc_object_contract.cc",
        (
            "start",
            "contract",
            "cupidc_emit",
            "cupidc_ir",
            "cupidc_frontend",
            "cupidc_type",
            "cupidc_pp",
            "cupidasm",
            "cupidld",
            "x86",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "elf32",
        "toolchain/tests/elf32_contract.cc",
        ("start", "contract", "elf32", "ctool_host", "ctool", "runtime"),
    ),
    ContractPlan(
        "x86",
        "toolchain/tests/x86_contract.cc",
        ("start", "contract", "x86", "ctool_host", "ctool", "runtime"),
    ),
    ContractPlan(
        "cupiddis",
        "toolchain/tests/cupiddis_contract.cc",
        (
            "start",
            "contract",
            "cupiddis",
            "x86",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidasm",
        "toolchain/tests/cupidasm_contract.cc",
        (
            "start",
            "contract",
            "cupidasm",
            "x86",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidasm-demos",
        "toolchain/tests/cupidasm_demos_contract.cc",
        (
            "start",
            "contract",
            "cupidasm",
            "x86",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidasm-kernel-elf",
        "toolchain/tests/cupidasm_kernel_elf_contract.cc",
        (
            "start",
            "contract",
            "as_elf",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidobj",
        "toolchain/tests/cupidobj_contract.cc",
        (
            "start",
            "contract",
            "cupidobj",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
    ContractPlan(
        "cupidld",
        "toolchain/tests/cupidld_contract.cc",
        (
            "start",
            "contract",
            "cupidld",
            "elf32",
            "ctool_host",
            "ctool",
            "runtime",
        ),
    ),
)


def validate_plans(plans: Sequence[ContractPlan]) -> None:
    names: set[str] = set()
    sources: set[str] = set()
    for plan in plans:
        if not plan.source.endswith(".cc"):
            raise ContractError(
                "Cupid-owned contract source must end in .cc: "
                f"{plan.source}"
            )
        if not plan.source.startswith("toolchain/tests/"):
            raise ContractError(
                f"contract source leaves toolchain/tests: {plan.source}"
            )
        if plan.name in names or plan.source in sources:
            raise ContractError(f"contract plan is duplicated: {plan.name}")
        unknown_objects = sorted(
            set(plan.link_objects) - CONTRACT_LINK_OBJECT_KEYS
        )
        if unknown_objects:
            raise ContractError(
                f"contract link object is unknown: {plan.name}: "
                f"{', '.join(unknown_objects)}"
            )
        if (
            len(plan.link_objects) < 4
            or plan.link_objects[0] != "start"
            or plan.link_objects[1] != "contract"
            or plan.link_objects[-1] != "runtime"
            or len(set(plan.link_objects)) != len(plan.link_objects)
        ):
            raise ContractError(
                f"contract link order is invalid: {plan.name}"
            )
        names.add(plan.name)
        sources.add(plan.source)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _contract_input_paths(root: Path) -> tuple[Path, ...]:
    paths = {
        root / plan.source for plan in CONTRACT_PLANS
    }
    paths.update(root / path for path in CONTRACT_CONTROL_INPUTS)
    paths.update(root / path for path in WINDOWS_RUNTIME_INPUTS)
    paths.update(root / path for path in USER_SYSCALL_ABI_INPUTS)
    paths.add(root / "toolchain/tests/hosted_i386_runtime_contract.cc")
    paths.add(root / "kernel/lang/as_elf.cc")
    paths.add(root / "kernel/lang/as_elf.h")
    paths.update((root / "toolchain").glob("*.h"))
    paths.update((root / "toolchain/tests").glob("*.inc"))
    paths.update((root / "toolchain/tests").glob("*.h"))
    paths.update(
        (root / "toolchain/hosted/i386-linux/include").glob("*.h")
    )
    missing = sorted(
        path.relative_to(root).as_posix()
        for path in paths
        if not path.is_file() or path.is_symlink()
    )
    if missing:
        raise ContractError(
            "contract input is missing or is a symlink: "
            + ", ".join(missing)
        )
    return tuple(
        sorted(paths, key=lambda path: path.relative_to(root).as_posix())
    )


def _snapshot_inputs(root: Path, paths: Sequence[Path]) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): _sha256(path)
        for path in paths
    }


def _require_inputs_unchanged(
    root: Path, expected: dict[str, str]
) -> None:
    actual = _snapshot_inputs(root, _contract_input_paths(root))
    if actual != expected:
        raise ContractError(
            "contract inputs changed while the checked build ran"
        )


def _freeze_contract_inputs(
    root: Path,
    destination: Path,
    paths: Sequence[Path],
    expected: dict[str, str],
) -> None:
    relative_paths = {
        path.relative_to(root).as_posix()
        for path in paths
    }
    if relative_paths != set(expected):
        raise ContractError(
            "contract input paths differ from the initial snapshot"
        )
    _require_inputs_unchanged(root, expected)
    destination.mkdir()
    for source in paths:
        relative = source.relative_to(root)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if _sha256(source) != _sha256(target):
            raise ContractError(
                f"could not freeze contract input: {relative.as_posix()}"
            )
    frozen = _snapshot_inputs(
        destination, _contract_input_paths(destination)
    )
    if frozen != expected:
        raise ContractError(
            "frozen contract inputs differ from the initial snapshot"
        )


def _run_clean(
    runner: ToolRunner,
    executable: Path,
    arguments: Sequence[str | Path],
    label: str,
    timeout: int,
) -> None:
    try:
        result = runner.run(executable, arguments, timeout)
    except (BootstrapError, OSError) as error:
        raise ContractError(f"{label} could not run: {error}") from error
    except subprocess.TimeoutExpired as error:
        raise ContractError(f"{label} timed out") from error
    if result.returncode != 0 or result.stdout or result.stderr:
        detail = result.stderr.strip() or result.stdout.strip()
        suffix = f": {detail}" if detail else ""
        raise ContractError(
            f"{label} failed with status {result.returncode}{suffix}"
        )


def _compile_include_arguments(logical_source: str) -> tuple[str, ...]:
    arguments = ["-I", "/toolchain"]
    if logical_source in KERNEL_LANG_SOURCES:
        arguments.extend(("-I", "/kernel/lang"))
    arguments.extend(
        (
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
        )
    )
    return tuple(arguments)


def _compile_source(
    runner: ToolRunner,
    compiler: Path,
    source_root: Path,
    logical_source: str,
    output: Path,
    label: str,
    timeout: int,
) -> None:
    logical_output = "/" + output.relative_to(source_root).as_posix()
    _run_clean(
        runner,
        compiler,
        (
            "--root",
            source_root,
            *(("--gnu",) if logical_source in GNU_CONTRACT_SOURCES else ()),
            "-c",
            "/" + logical_source,
            *_compile_include_arguments(logical_source),
            "-o",
            logical_output,
        ),
        label,
        timeout,
    )
    _validate_i386_relocatable(output)


def _build_contract_stage(
    source_root: Path,
    bootstrap_stage: Path,
    output: Path,
    stage_name: str,
    workers: int,
) -> tuple[dict[str, Path], dict[str, Path]]:
    _announce(f"{stage_name} compile started")
    output.mkdir()
    runner = ToolRunner(source_root)
    compiler = bootstrap_stage / "cupidc.elf"
    linker = bootstrap_stage / "cupidld.elf"
    shared = {
        path.stem: path
        for path in bootstrap_stage.glob("*.o")
    }
    if "start" not in shared or "runtime" not in shared:
        raise ContractError(f"{stage_name} bootstrap objects are incomplete")

    as_elf = output / "as_elf.o"
    _compile_source(
        runner,
        compiler,
        source_root,
        "kernel/lang/as_elf.cc",
        as_elf,
        f"{stage_name} CupidC for as_elf.cc",
        360,
    )
    shared["as_elf"] = as_elf
    _announce(f"{stage_name} compiled kernel/lang/as_elf.cc")

    def compile_contract(plan: ContractPlan) -> tuple[str, Path]:
        contract_object = output / f"{plan.name}-contract.o"
        _compile_source(
            runner,
            compiler,
            source_root,
            plan.source,
            contract_object,
            f"{stage_name} CupidC for {plan.source}",
            900,
        )
        _announce(f"{stage_name} compiled {plan.source}")
        return plan.name, contract_object

    with ThreadPoolExecutor(max_workers=workers) as executor:
        contract_objects = dict(executor.map(compile_contract, CONTRACT_PLANS))

    runtime_contract = output / "runtime-contract.o"
    _compile_source(
        runner,
        compiler,
        source_root,
        "toolchain/tests/hosted_i386_runtime_contract.cc",
        runtime_contract,
        f"{stage_name} CupidC for hosted runtime contract",
        360,
    )
    _announce(f"{stage_name} compiled the hosted runtime contract")

    objects = {
        "as_elf": as_elf,
        **contract_objects,
        "runtime": runtime_contract,
    }
    executables: dict[str, Path] = {}

    def link_contract(plan: ContractPlan) -> tuple[str, Path]:
        objects = {
            **shared,
            "contract": contract_objects[plan.name],
        }
        missing = [
            name for name in plan.link_objects if name not in objects
        ]
        if missing:
            raise ContractError(
                f"{plan.name} link object is absent: {', '.join(missing)}"
            )
        executable = output / plan.artifact
        _run_clean(
            runner,
            linker,
            (
                "-m",
                "elf_i386",
                "--text-address",
                "0x08048000",
                "--entry",
                "_start",
                "-o",
                executable,
                *[objects[name] for name in plan.link_objects],
            ),
            f"{stage_name} CupidLD for {plan.name}",
            360,
        )
        _validate_static_i386_elf(executable, TARGET_ENTRY)
        _announce(f"{stage_name} linked {plan.artifact}")
        return plan.name, executable

    with ThreadPoolExecutor(max_workers=workers) as executor:
        executables.update(executor.map(link_contract, CONTRACT_PLANS))

    runtime_executable = output / "cupidc-runtime-contract.elf"
    _run_clean(
        runner,
        linker,
        (
            "-m",
            "elf_i386",
            "--text-address",
            "0x08048000",
            "--entry",
            "_start",
            "-o",
            runtime_executable,
            shared["start"],
            runtime_contract,
            shared["runtime"],
        ),
        f"{stage_name} CupidLD for hosted runtime contract",
        360,
    )
    _validate_static_i386_elf(runtime_executable, TARGET_ENTRY)
    _announce(f"{stage_name} linked {runtime_executable.name}")
    executables["runtime"] = runtime_executable
    _announce(f"{stage_name} completed")
    return objects, executables


def _compare_stage_files(
    first: dict[str, Path],
    second: dict[str, Path],
    artifact_kind: str,
) -> dict[str, str]:
    if set(first) != set(second):
        raise ContractError(
            f"{artifact_kind} stage inventories differ"
        )
    comparisons: dict[str, str] = {}
    for name in sorted(first):
        first_bytes = first[name].read_bytes()
        second_bytes = second[name].read_bytes()
        if first_bytes != second_bytes:
            raise ContractError(
                f"{artifact_kind} differs across stages: {name}"
            )
        comparisons[name] = hashlib.sha256(first_bytes).hexdigest()
    return comparisons


def _run_runtime_contract(
    root: Path, executable: Path, workspace: Path
) -> None:
    runner = ToolRunner(root)
    output = workspace / "runtime-contract.txt"
    missing = workspace / "missing.txt"
    result = runner.run(executable, (output, missing), 60)
    if (
        result.returncode != 0
        or result.stdout !=
        "printf-ok 7\nputs-ok\nfputs-ok\nruntime-ok\n"
        or result.stderr
        or output.read_text(encoding="ascii") != "ok -12 0000002A\n"
        or missing.exists()
    ):
        raise ContractError(
            "hosted runtime contract failed after checked linking"
        )


def _artifact_record(path: Path) -> dict[str, object]:
    return {
        "path": path.name,
        "sha256": _sha256(path),
        "size": path.stat().st_size,
    }


def _expected_artifact_names() -> tuple[str, ...]:
    return tuple(plan.artifact for plan in CONTRACT_PLANS) + (
        "cupidc-runtime-contract.elf",
        *tuple(TOOL_PUBLIC_NAMES[name] for name in TOOL_NAMES),
    )


def _valid_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def _snapshot_sha256(inventory: dict[str, dict[str, object]]) -> str:
    encoded = json.dumps(
        inventory,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=True,
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _valid_logical_path(value: object) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return (
        not path.is_absolute()
        and path.as_posix() == value
        and all(part not in {"", ".", ".."} for part in path.parts)
    )


def _validate_bootstrap_record(value: object) -> dict[str, object]:
    if not isinstance(value, dict) or set(value) != {
        "build_plan_sha256",
        "seed_manifest",
        "source_inputs",
    }:
        raise ContractError("published bootstrap record differs")
    if not _valid_sha256(value.get("build_plan_sha256")):
        raise ContractError("published bootstrap build plan differs")

    seed_manifest = value.get("seed_manifest")
    if (
        not isinstance(seed_manifest, dict)
        or set(seed_manifest) != {"path", "sha256"}
        or not _valid_logical_path(seed_manifest.get("path"))
        or not _valid_sha256(seed_manifest.get("sha256"))
    ):
        raise ContractError("published bootstrap seed record differs")

    source_inputs = value.get("source_inputs")
    if (
        not isinstance(source_inputs, dict)
        or set(source_inputs) != {"count", "files", "sha256"}
    ):
        raise ContractError("published bootstrap input inventory differs")
    count = source_inputs.get("count")
    files = source_inputs.get("files")
    if (
        isinstance(count, bool)
        or not isinstance(count, int)
        or count < 1
        or not isinstance(files, dict)
        or count != len(files)
        or any(
            not _valid_logical_path(path)
            or not isinstance(record, dict)
            or set(record) != {"sha256", "size"}
            or not _valid_sha256(record.get("sha256"))
            or isinstance(record.get("size"), bool)
            or not isinstance(record.get("size"), int)
            or record["size"] < 0
            for path, record in files.items()
        )
        or not _valid_sha256(source_inputs.get("sha256"))
        or _snapshot_sha256(files) != source_inputs["sha256"]
    ):
        raise ContractError("published bootstrap input inventory differs")
    return value


def verify_publication(output: Path) -> dict[str, object]:
    expected_names = set(_expected_artifact_names())
    if output.is_symlink() or not output.is_dir():
        raise ContractError("published contract cohort is not a directory")
    manifest = output / "manifest.json"
    if manifest.is_symlink() or not manifest.is_file():
        raise ContractError("published contract manifest is missing")
    try:
        report = json.loads(manifest.read_text(encoding="ascii"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ContractError(
            f"published contract manifest is invalid: {error}"
        ) from error
    if not isinstance(report, dict):
        raise ContractError("published contract manifest is not an object")
    expected_report_keys = {
        "artifacts",
        "bootstrap",
        "comparisons",
        "input_count",
        "inputs",
        "object_comparisons",
        "schema",
        "status",
        "target",
        "tool_fixed_point",
    }
    if (
        set(report) != expected_report_keys
        or report.get("schema") != REPORT_SCHEMA
        or report.get("status") != "pass"
        or report.get("target")
        != {
            "architecture": "i386",
            "entry": TARGET_ENTRY,
            "linkage": "static",
            "operating_system": "linux",
        }
    ):
        raise ContractError("published contract manifest metadata differs")
    _validate_bootstrap_record(report.get("bootstrap"))
    inputs = report.get("inputs")
    input_count = report.get("input_count")
    if (
        not isinstance(inputs, dict)
        or isinstance(input_count, bool)
        or not isinstance(input_count, int)
        or input_count != len(inputs)
        or any(
            not isinstance(path, str)
            or not path
            or not _valid_sha256(digest)
            for path, digest in inputs.items()
        )
    ):
        raise ContractError("published contract input inventory differs")
    fixed_point = report.get("tool_fixed_point")
    if fixed_point != {
        "all_equal": True,
        "c_objects": 19,
        "startup_objects": 1,
        "tool_images": len(TOOL_NAMES),
    }:
        raise ContractError("published Toolchain fixed-point record differs")

    records = report.get("artifacts")
    if not isinstance(records, list):
        raise ContractError("published contract artifact inventory is absent")
    records_by_name: dict[str, dict[str, object]] = {}
    for record in records:
        if (
            not isinstance(record, dict)
            or set(record) != {"path", "sha256", "size"}
        ):
            raise ContractError("published contract artifact record differs")
        name = record.get("path")
        size = record.get("size")
        if (
            not isinstance(name, str)
            or name != Path(name).name
            or name in records_by_name
            or not _valid_sha256(record.get("sha256"))
            or isinstance(size, bool)
            or not isinstance(size, int)
            or size < 0
        ):
            raise ContractError("published contract artifact record differs")
        records_by_name[name] = record
    if set(records_by_name) != expected_names:
        raise ContractError("published contract artifact inventory differs")

    actual_names = {path.name for path in output.iterdir()}
    if actual_names != expected_names | {"manifest.json"}:
        raise ContractError("published contract directory is incomplete")
    for name in sorted(expected_names):
        path = output / name
        record = records_by_name[name]
        if (
            path.is_symlink()
            or not path.is_file()
            or path.stat().st_size != record["size"]
            or _sha256(path) != record["sha256"]
        ):
            raise ContractError(
                f"published contract artifact differs: {name}"
            )

    comparisons = report.get("comparisons")
    expected_comparisons = {
        plan.name: plan.artifact for plan in CONTRACT_PLANS
    }
    expected_comparisons["runtime"] = "cupidc-runtime-contract.elf"
    if (
        not isinstance(comparisons, dict)
        or set(comparisons) != set(expected_comparisons)
        or any(
            not _valid_sha256(digest) or
            digest != records_by_name[expected_comparisons[name]]["sha256"]
            for name, digest in comparisons.items()
        )
    ):
        raise ContractError("published contract comparison record differs")

    object_comparisons = report.get("object_comparisons")
    expected_object_comparisons = {
        plan.name for plan in CONTRACT_PLANS
    } | {"as_elf", "runtime"}
    if (
        not isinstance(object_comparisons, dict)
        or set(object_comparisons) != expected_object_comparisons
        or any(
            not isinstance(name, str) or not _valid_sha256(digest)
            for name, digest in object_comparisons.items()
        )
    ):
        raise ContractError(
            "published contract object comparison record differs"
        )
    return report


def verify_publication_inputs(
    root: Path, report: dict[str, object]
) -> None:
    root = root.resolve()
    expected = report.get("inputs")
    if not isinstance(expected, dict):
        raise ContractError("published contract input inventory differs")
    actual = _snapshot_inputs(root, _contract_input_paths(root))
    if actual != expected:
        raise ContractError(
            "published contract inputs differ from the live source"
        )

    bootstrap = _validate_bootstrap_record(report.get("bootstrap"))
    seed_record = bootstrap["seed_manifest"]
    if not isinstance(seed_record, dict):
        raise ContractError("published bootstrap seed record differs")
    logical_manifest = seed_record["path"]
    if not isinstance(logical_manifest, str):
        raise ContractError("published bootstrap seed record differs")
    manifest = root.joinpath(*PurePosixPath(logical_manifest).parts)
    if manifest.is_symlink():
        raise ContractError("published bootstrap seed is now a symlink")
    try:
        resolved_manifest = manifest.resolve(strict=True)
        resolved_manifest.relative_to(root)
    except (OSError, ValueError) as error:
        raise ContractError(
            "published bootstrap seed is unavailable"
        ) from error
    if not resolved_manifest.is_file():
        raise ContractError(
            "published bootstrap seed is unavailable"
        )
    try:
        seed_inputs = verify_seed_inputs(resolved_manifest)
        if seed_inputs.manifest_sha256 != seed_record["sha256"]:
            raise ContractError(
                "published bootstrap seed differs from the live source"
            )
        seed_data = seed_inputs.manifest
        build_plan = (
            seed_data.get("build_plan")
            if isinstance(seed_data, dict)
            else None
        )
        if (
            not isinstance(build_plan, dict)
            or seed_data.get("build_plan_sha256")
            != bootstrap["build_plan_sha256"]
        ):
            raise ContractError(
                "published bootstrap build plan differs from the live seed"
            )
        live_bootstrap_inputs = capture_source_snapshot(root, build_plan)
    except (
        BootstrapError,
        OSError,
        UnicodeError,
        json.JSONDecodeError,
    ) as error:
        raise ContractError(
            f"published bootstrap inputs could not be verified: {error}"
        ) from error

    source_inputs = bootstrap["source_inputs"]
    if not isinstance(source_inputs, dict):
        raise ContractError("published bootstrap input inventory differs")
    expected_bootstrap_inputs = source_inputs.get("files")
    if live_bootstrap_inputs != expected_bootstrap_inputs:
        raise ContractError(
            "published bootstrap inputs differ from the live source"
        )


def _validate_output_target(root: Path, output: Path) -> Path:
    root = root.resolve()
    if output.is_symlink():
        raise ContractError("contract output path is a symlink")
    output = output.resolve()
    try:
        relative = output.relative_to(root)
    except ValueError as error:
        raise ContractError(
            "contract output must stay inside the source root"
        ) from error
    if (
        len(relative.parts) < 2
        or relative.name != "cupidc-contracts"
    ):
        raise ContractError(
            "contract output must be a dedicated cupidc-contracts directory"
        )
    if output.exists():
        try:
            verify_publication(output)
        except ContractError as error:
            raise ContractError(
                "existing contract output is not a complete cohort"
            ) from error
    return output


def _resolve_manifest(root: Path, manifest: Path) -> tuple[Path, str]:
    root = root.resolve()
    if manifest.is_symlink():
        raise ContractError("bootstrap seed manifest is a symlink")
    try:
        resolved = manifest.resolve(strict=True)
        relative = resolved.relative_to(root).as_posix()
    except (OSError, ValueError) as error:
        raise ContractError(
            "bootstrap seed manifest must stay inside the source root"
        ) from error
    if not resolved.is_file():
        raise ContractError("bootstrap seed manifest is not a file")
    return resolved, relative


def _require_report_manifest(
    report: dict[str, object], manifest: Path, logical_manifest: str
) -> None:
    bootstrap = _validate_bootstrap_record(report.get("bootstrap"))
    seed_record = bootstrap.get("seed_manifest")
    if (
        not isinstance(seed_record, dict)
        or seed_record.get("path") != logical_manifest
        or seed_record.get("sha256") != _sha256(manifest)
    ):
        raise ContractError(
            "published cohort belongs to a different bootstrap seed"
        )


def publish_directory(
    staging: Path,
    output: Path,
    required_names: Sequence[str],
    source_root: Path,
) -> None:
    required = set(required_names)
    if staging.is_symlink() or not staging.is_dir():
        raise ContractError("contract publication is not a directory")
    actual = {path.name for path in staging.iterdir()}
    if actual != required or any(
        path.is_symlink() or not path.is_file()
        for path in staging.iterdir()
    ):
        raise ContractError("contract publication is incomplete")
    output = _validate_output_target(source_root, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink() or (output.exists() and not output.is_dir()):
        raise ContractError("contract output path is invalid")
    backup = output.with_name(
        f".{output.name}.backup-{os.getpid()}"
    )
    if backup.exists() or backup.is_symlink():
        raise ContractError("contract publication backup already exists")
    moved_old = False
    try:
        if output.exists():
            output.replace(backup)
            moved_old = True
        staging.replace(output)
    except OSError as error:
        restore_error: OSError | None = None
        if moved_old and not output.exists() and backup.exists():
            try:
                backup.replace(output)
            except OSError as caught:
                restore_error = caught
        if restore_error is not None:
            raise ContractError(
                "could not publish contract cohort "
                f"({error}); the previous cohort could not be restored "
                f"({restore_error}) and remains recoverable at {backup}"
            ) from error
        if moved_old and backup.exists():
            raise ContractError(
                "could not publish contract cohort "
                f"({error}); the previous cohort remains at {backup}"
            ) from error
        raise ContractError(
            f"could not publish contract cohort: {error}"
        ) from error
    if moved_old:
        try:
            shutil.rmtree(backup)
        except OSError as error:
            print(
                "CupidC toolchain contracts: published the new cohort, "
                f"but the previous backup remains at {backup}: {error}",
                file=sys.stderr,
                flush=True,
            )


def build_contracts(
    root: Path,
    manifest: Path,
    output: Path,
    workers: int = 2,
) -> dict[str, object]:
    validate_plans(CONTRACT_PLANS)
    root = root.resolve()
    manifest, manifest_relative = _resolve_manifest(root, manifest)
    if workers < 1 or workers > 8:
        raise ContractError("contract worker count must be from 1 through 8")
    if not (root / "toolchain").is_dir():
        raise ContractError(f"source root has no toolchain: {root}")
    output = _validate_output_target(root, output)

    inputs = _contract_input_paths(root)
    snapshot = _snapshot_inputs(root, inputs)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{output.name}-build-", dir=output.parent
    ) as temporary:
        workspace = Path(temporary)
        bootstrap_output = workspace / "bootstrap"
        _announce("checked-seed bootstrap started")
        try:
            bootstrap_report = bootstrap_from_seed(
                manifest, root, bootstrap_output
            )
        except BootstrapError as error:
            raise ContractError(
                f"checked bootstrap failed: {error}"
            ) from error
        _announce("checked-seed bootstrap completed")

        private_root = workspace / "source"
        _freeze_contract_inputs(
            root, private_root, inputs, snapshot
        )
        stage_two_objects, stage_two_executables = _build_contract_stage(
            private_root,
            bootstrap_output / "stage-two",
            private_root / "contract-stage-two",
            "stage two",
            workers,
        )
        stage_three_objects, stage_three_executables = _build_contract_stage(
            private_root,
            bootstrap_output / "stage-three",
            private_root / "contract-stage-three",
            "stage three",
            workers,
        )
        object_comparisons = _compare_stage_files(
            stage_two_objects,
            stage_three_objects,
            "contract object",
        )
        comparisons = _compare_stage_files(
            stage_two_executables,
            stage_three_executables,
            "contract executable",
        )
        _announce(
            "stage-two and stage-three objects and executables match"
        )
        _run_runtime_contract(
            private_root, stage_two_executables["runtime"], workspace
        )
        _announce("hosted runtime contract passed")
        _require_inputs_unchanged(root, snapshot)
        _announce("live inputs still match the frozen build")

        publication = workspace / "publication"
        publication.mkdir()
        artifacts: list[Path] = []
        for plan in CONTRACT_PLANS:
            target = publication / plan.artifact
            shutil.copyfile(stage_two_executables[plan.name], target)
            artifacts.append(target)
        runtime_target = publication / "cupidc-runtime-contract.elf"
        shutil.copyfile(stage_two_executables["runtime"], runtime_target)
        artifacts.append(runtime_target)
        for tool_name in TOOL_NAMES:
            target = publication / TOOL_PUBLIC_NAMES[tool_name]
            shutil.copyfile(
                bootstrap_output / "stage-two" / f"{tool_name}.elf",
                target,
            )
            artifacts.append(target)

        bootstrap_inputs = bootstrap_report.get("source_inputs")
        if (
            not isinstance(bootstrap_inputs, dict)
            or bootstrap_report.get("seed_manifest_sha256")
            != _sha256(manifest)
        ):
            raise ContractError(
                "checked bootstrap report lacks its verified input inventory"
            )
        bootstrap_record: dict[str, object] = {
            "build_plan_sha256": bootstrap_report.get("build_plan_sha256"),
            "seed_manifest": {
                "path": manifest_relative,
                "sha256": bootstrap_report.get("seed_manifest_sha256"),
            },
            "source_inputs": bootstrap_inputs,
        }
        _validate_bootstrap_record(bootstrap_record)

        report: dict[str, object] = {
            "artifacts": [
                _artifact_record(path)
                for path in sorted(artifacts, key=lambda path: path.name)
            ],
            "bootstrap": bootstrap_record,
            "comparisons": comparisons,
            "input_count": len(snapshot),
            "inputs": snapshot,
            "object_comparisons": object_comparisons,
            "schema": REPORT_SCHEMA,
            "status": "pass",
            "target": {
                "architecture": "i386",
                "entry": TARGET_ENTRY,
                "linkage": "static",
                "operating_system": "linux",
            },
            "tool_fixed_point": bootstrap_report["comparisons"],
        }
        report_path = publication / "manifest.json"
        report_path.write_bytes(
            (
                json.dumps(report, indent=2, sort_keys=True)
                + "\n"
            ).encode("ascii")
        )
        verify_publication(publication)
        verify_publication_inputs(root, report)
        required_names = _expected_artifact_names() + ("manifest.json",)
        publish_directory(
            publication, output, required_names, root
        )
        _announce("published the complete contract cohort")
        return report


def ensure_contracts(
    root: Path,
    manifest: Path,
    output: Path,
    workers: int = 2,
) -> dict[str, object]:
    root = root.resolve()
    if workers < 1 or workers > 8:
        raise ContractError("contract worker count must be from 1 through 8")
    if not (root / "toolchain").is_dir():
        raise ContractError(f"source root has no toolchain: {root}")
    manifest, manifest_relative = _resolve_manifest(root, manifest)
    output = _validate_output_target(root, output)
    if output.exists() or output.is_symlink():
        report = verify_publication(output)
        try:
            verify_publication_inputs(root, report)
            _require_report_manifest(report, manifest, manifest_relative)
        except ContractError:
            _announce("the published cohort is stale and will be rebuilt")
        else:
            _announce("the published cohort is current")
            return report
    return build_contracts(root, manifest, output, workers)


def run_published_contract(
    root: Path,
    executable: Path,
    arguments: Sequence[str | Path],
    timeout: int,
    expected_report: dict[str, object] | None = None,
) -> subprocess.CompletedProcess[str]:
    root = root.resolve()
    executable = executable.resolve()
    if not (root / "toolchain").is_dir():
        raise ContractError(f"source root has no toolchain: {root}")
    if executable.name not in _expected_artifact_names():
        raise ContractError(
            "requested executable is not a published cohort artifact"
        )
    cohort = executable.parent
    report = verify_publication(cohort)
    if expected_report is not None and report != expected_report:
        raise ContractError(
            "published contract cohort changed before execution"
        )
    verify_publication_inputs(root, report)
    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{cohort.name}-run-", dir=cohort.parent
        ) as temporary:
            frozen = Path(temporary)
            for path in cohort.iterdir():
                if path.is_symlink() or not path.is_file():
                    raise ContractError(
                        "published contract cohort changed while freezing"
                    )
                shutil.copyfile(path, frozen / path.name)
            frozen_report = verify_publication(frozen)
            if frozen_report != report:
                raise ContractError(
                    "published contract cohort changed while freezing"
                )
            result = ToolRunner(root / "toolchain").run(
                frozen / executable.name, arguments, timeout
            )
    except (OSError, subprocess.TimeoutExpired, BootstrapError) as error:
        raise ContractError(
            f"published contract could not run: {error}"
        ) from error
    try:
        live_report = verify_publication(cohort)
        if live_report != report:
            raise ContractError(
                "published contract cohort changed while contract ran"
            )
        verify_publication_inputs(root, report)
    except ContractError as error:
        raise ContractError(
            "published contract cohort changed while contract ran"
        ) from error
    return result


def _freeze_user_syscall_abi_inputs(
    root: Path,
    destination: Path,
    report: dict[str, object],
) -> None:
    root = root.resolve()
    expected = report.get("inputs")
    if not isinstance(expected, dict):
        raise ContractError("published contract input inventory differs")
    destination.mkdir(mode=0o700)
    for logical_path in USER_SYSCALL_ABI_INPUTS:
        digest = expected.get(logical_path)
        if not isinstance(digest, str):
            raise ContractError(
                f"published cohort omits ABI input: {logical_path}"
            )
        source = root.joinpath(*PurePosixPath(logical_path).parts)
        if source.is_symlink():
            raise ContractError(
                f"ABI input is a symlink: {logical_path}"
            )
        try:
            resolved = source.resolve(strict=True)
            resolved.relative_to(root)
            data = resolved.read_bytes()
        except (OSError, ValueError) as error:
            raise ContractError(
                f"ABI input is unavailable: {logical_path}"
            ) from error
        if not resolved.is_file() or hashlib.sha256(data).hexdigest() != digest:
            raise ContractError(
                f"ABI input differs from the publication: {logical_path}"
            )
        target = destination.joinpath(*PurePosixPath(logical_path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    for logical_path in USER_SYSCALL_ABI_INPUTS:
        digest = expected[logical_path]
        if (
            _sha256(root.joinpath(*PurePosixPath(logical_path).parts))
            != digest
            or _sha256(
                destination.joinpath(*PurePosixPath(logical_path).parts)
            )
            != digest
        ):
            raise ContractError(
                "ABI inputs changed while the shared snapshot was frozen"
            )


def run_user_syscall_abi(
    root: Path,
    manifest: Path,
    output: Path,
    workers: int = 2,
    timeout: int = 60,
) -> dict[str, object]:
    root = root.resolve()
    report = ensure_contracts(root, manifest, output, workers)
    executable = output / "user-syscall-abi-contract.elf"
    with tempfile.TemporaryDirectory(
        prefix="cupid-user-syscall-abi-snapshot-"
    ) as temporary:
        snapshot = Path(temporary) / "source"
        _freeze_user_syscall_abi_inputs(root, snapshot, report)
        result = run_published_contract(
            root,
            executable,
            ("check-snapshot", snapshot, root),
            timeout,
            report,
        )
        try:
            oracle_report = check_syscall_abi(snapshot)
        except UserSyscallAbiError as error:
            raise ContractError(
                f"independent user syscall ABI oracle failed: {error}"
            ) from error
    if result.returncode != 0 or result.stderr:
        detail = (result.stderr or result.stdout).strip()
        suffix = f": {detail}" if detail else ""
        raise ContractError(
            "Cupid-built user syscall ABI contract failed"
            f" with status {result.returncode}{suffix}"
        )
    try:
        contract_report = json.loads(result.stdout)
    except (TypeError, json.JSONDecodeError) as error:
        raise ContractError(
            "Cupid-built user syscall ABI contract returned invalid JSON"
        ) from error
    if not isinstance(contract_report, dict):
        raise ContractError(
            "Cupid-built user syscall ABI contract returned a non-object"
        )
    if contract_report != oracle_report:
        raise ContractError(
            "Cupid-built user syscall ABI report differs from the "
            "independent oracle"
        )
    try:
        if verify_publication(output) != report:
            raise ContractError("published contract cohort changed")
        verify_publication_inputs(root, report)
    except ContractError as error:
        raise ContractError(
            "published contract cohort changed while the ABI check ran"
        ) from error
    return contract_report


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build and run Cupid-owned toolchain contracts."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser(
        "build", help="build the complete checked contract cohort"
    )
    build.add_argument("--root", required=True, type=Path)
    build.add_argument("--manifest", required=True, type=Path)
    build.add_argument("--output", required=True, type=Path)
    build.add_argument("--workers", type=int, default=2)
    ensure = subparsers.add_parser(
        "ensure", help="build the checked cohort when it is not current"
    )
    ensure.add_argument("--root", required=True, type=Path)
    ensure.add_argument("--manifest", required=True, type=Path)
    ensure.add_argument("--output", required=True, type=Path)
    ensure.add_argument("--workers", type=int, default=2)
    user_abi = subparsers.add_parser(
        "user-abi", help="check the user syscall ABI with the Cupid contract"
    )
    user_abi.add_argument("--root", required=True, type=Path)
    user_abi.add_argument("--manifest", required=True, type=Path)
    user_abi.add_argument("--output", required=True, type=Path)
    user_abi.add_argument("--workers", type=int, default=2)
    user_abi.add_argument("--timeout", type=int, default=60)
    run = subparsers.add_parser(
        "run", help="run one published static i386 contract"
    )
    run.add_argument("--root", required=True, type=Path)
    run.add_argument("--executable", required=True, type=Path)
    run.add_argument("--timeout", type=int, default=900)
    run.add_argument("arguments", nargs=argparse.REMAINDER)
    verify = subparsers.add_parser(
        "verify", help="verify one published checked contract cohort"
    )
    verify.add_argument("--root", required=True, type=Path)
    verify.add_argument("--output", required=True, type=Path)
    subparsers.add_parser("list", help="list the owned contract names")
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        if arguments.command == "list":
            validate_plans(CONTRACT_PLANS)
            for plan in CONTRACT_PLANS:
                print(plan.name)
            return 0
        if arguments.command == "build":
            report = build_contracts(
                arguments.root,
                arguments.manifest,
                arguments.output,
                arguments.workers,
            )
            print(
                "CupidC toolchain contracts: ok "
                f"({len(report['artifacts'])} artifacts)"
            )
            return 0
        if arguments.command == "ensure":
            report = ensure_contracts(
                arguments.root,
                arguments.manifest,
                arguments.output,
                arguments.workers,
            )
            print(
                "CupidC toolchain contracts: ready "
                f"({len(report['artifacts'])} artifacts)"
            )
            return 0
        if arguments.command == "user-abi":
            report = run_user_syscall_abi(
                arguments.root,
                arguments.manifest,
                arguments.output,
                arguments.workers,
                arguments.timeout,
            )
            print(json.dumps(report, sort_keys=True))
            return 0
        if arguments.command == "verify":
            report = verify_publication(arguments.output)
            verify_publication_inputs(arguments.root, report)
            print(
                "CupidC toolchain contracts: verified "
                f"({len(report['artifacts'])} artifacts)"
            )
            return 0
        tool_arguments = list(arguments.arguments)
        if tool_arguments[:1] == ["--"]:
            tool_arguments = tool_arguments[1:]
        result = run_published_contract(
            arguments.root,
            arguments.executable,
            tool_arguments,
            arguments.timeout,
        )
        sys.stdout.write(result.stdout or "")
        sys.stderr.write(result.stderr or "")
        return result.returncode
    except (BootstrapError, ContractError, OSError) as error:
        print(f"CupidC toolchain contracts failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build Cupid-owned toolchain contracts across the checked fixed point."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import struct
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
        EXPECTED_WINDOWS_TARGET,
        WINDOWS_TOOL_IMPORTS,
        WINDOWS_SEED_SCHEMA,
        ToolRunner,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        _validate_static_i386_pe32,
        _bootstrap_for_manifest_author,
        capture_source_snapshot,
        freeze_seed_inputs,
        require_live_seed_inputs,
        verify_seed_inputs,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_SOURCES,
        EXPECTED_WINDOWS_TARGET,
        WINDOWS_TOOL_IMPORTS,
        WINDOWS_SEED_SCHEMA,
        ToolRunner,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        _validate_static_i386_pe32,
        _bootstrap_for_manifest_author,
        capture_source_snapshot,
        freeze_seed_inputs,
        require_live_seed_inputs,
        verify_seed_inputs,
    )

try:
    from tools.user_syscall_abi import (
        UserSyscallAbiError,
        check_syscall_abi,
    )
except ModuleNotFoundError:
    from user_syscall_abi import UserSyscallAbiError, check_syscall_abi


REPORT_SCHEMA = "cupid.toolchain-contracts.v3"
LEGACY_REPORT_SCHEMAS = ("cupid.toolchain-contracts.v2",)
TARGET_ENTRY = 0x08048000
ORDINARY_COMPILE_TIMEOUT = 900
CONVERGED_GENERATIONS = ("stage-three", "stage-four")
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
CONTRACT_QUOTED_INCLUDE_ROOTS = {
    "toolchain/tests/x86_contract.cc": ("/toolchain/tests",),
}
CONTRACT_CONTROL_INPUTS = (
    "toolchain/Makefile",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
)
MANIFEST_AUTHOR_SOURCE = "toolchain/tests/toolchain_manifest_contract.cc"
MANIFEST_AUTHOR_MAGIC = b"CUPMAN4\0"
BOOTSTRAP_OBJECT_NAMES = (
    "runtime",
    "ctool",
    "ctool_host",
    "elf32",
    "x86",
    "cupidasm",
    "cupidasm_main",
    "cupiddis",
    "cupiddis_main",
    "cupidobj",
    "cupidobj_main",
    "cupidld",
    "cupidld_main",
    "cupidc_pp",
    "cupidc_type",
    "cupidc_frontend",
    "cupidc_ir",
    "cupidc_emit",
    "cupidc_main",
    "start",
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
NATIVE_WINDOWS_USER_ABI_BUILD_INPUTS = tuple(
    sorted(
        {
            *USER_SYSCALL_ABI_INPUTS,
            "toolchain/ctool.cc",
            "toolchain/ctool.h",
            "toolchain/ctool_host.cc",
            "toolchain/ctool_host.h",
            "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
            "toolchain/hosted/i386-linux/include/direct.h",
            "toolchain/hosted/i386-linux/include/errno.h",
            "toolchain/hosted/i386-linux/include/stdint.h",
            "toolchain/hosted/i386-linux/include/stdio.h",
            "toolchain/hosted/i386-linux/include/stdlib.h",
            "toolchain/hosted/i386-linux/include/string.h",
            "toolchain/hosted/i386-linux/include/unistd.h",
            "toolchain/hosted/i386-linux/include/windows.h",
            "toolchain/hosted/i386-linux/runtime.cc",
            "toolchain/hosted/i386-windows/runtime.cc",
            "toolchain/hosted/i386-windows/tool_start.asm",
            "toolchain/tests/user_syscall_abi_contract.cc",
            "tools/bootstrap_toolchain.py",
            "tools/cupidc_toolchain_contracts.py",
            "tools/user_syscall_abi.py",
        }
    )
)
NATIVE_WINDOWS_USER_ABI_COMPILE_PLAN = (
    (
        "contract",
        "/toolchain/tests/user_syscall_abi_contract.cc",
        (),
        False,
    ),
    (
        "ctool_host",
        "/toolchain/ctool_host.cc",
        ("_WIN32=1",),
        False,
    ),
    ("ctool", "/toolchain/ctool.cc", (), False),
    (
        "runtime",
        "/toolchain/hosted/i386-windows/runtime.cc",
        ("_WIN32=1",),
        True,
    ),
)
NATIVE_WINDOWS_USER_ABI_LINK_ORDER = (
    "start",
    "contract",
    "ctool_host",
    "ctool",
    "runtime",
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
    compile_timeout: int = ORDINARY_COMPILE_TIMEOUT
    exclusive_compile: bool = False

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
        compile_timeout=1800,
        exclusive_compile=True,
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
            "cupidld",
            "cupidasm",
            "x86",
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
    exclusive_count = 0
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
        if type(plan.compile_timeout) is not int or plan.compile_timeout < 1:
            raise ContractError(
                f"contract compile timeout is invalid: {plan.name}"
            )
        if type(plan.exclusive_compile) is not bool:
            raise ContractError(
                f"contract compile admission is invalid: {plan.name}"
            )
        has_extended_timeout = (
            plan.compile_timeout > ORDINARY_COMPILE_TIMEOUT
        )
        if plan.exclusive_compile != has_extended_timeout:
            raise ContractError(
                "extended contract compile budget must be exclusive: "
                f"{plan.name}"
            )
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
        exclusive_count += int(plan.exclusive_compile)
    if exclusive_count != 1:
        raise ContractError("contract compile admission policy differs")


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
    paths.add(root / "toolchain/x86.cc")
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


def _snapshot_contract_inputs(
    root: Path, paths: Sequence[Path]
) -> dict[str, dict[str, object]]:
    snapshot: dict[str, dict[str, object]] = {}
    for path in paths:
        payload = path.read_bytes()
        snapshot[path.relative_to(root).as_posix()] = {
            "sha256": hashlib.sha256(payload).hexdigest(),
            "size": len(payload),
        }
    return snapshot


def _native_windows_user_abi_input_paths(root: Path) -> tuple[Path, ...]:
    root = root.resolve()
    paths: list[Path] = []
    for logical_path in NATIVE_WINDOWS_USER_ABI_BUILD_INPUTS:
        path = root.joinpath(*PurePosixPath(logical_path).parts)
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(root)
        except (OSError, ValueError) as error:
            raise ContractError(
                "native Windows user ABI input is unavailable: "
                f"{logical_path}"
            ) from error
        if path.is_symlink() or not resolved.is_file():
            raise ContractError(
                "native Windows user ABI input is not a regular file: "
                f"{logical_path}"
            )
        paths.append(path)
    return tuple(paths)


def _require_native_windows_user_abi_inputs_unchanged(
    root: Path, expected: dict[str, str]
) -> None:
    actual = _snapshot_inputs(
        root.resolve(), _native_windows_user_abi_input_paths(root)
    )
    if actual != expected:
        raise ContractError(
            "native Windows user ABI inputs changed while the check ran"
        )


def _freeze_native_windows_user_abi_inputs(
    root: Path, destination: Path
) -> dict[str, str]:
    root = root.resolve()
    paths = _native_windows_user_abi_input_paths(root)
    expected = _snapshot_inputs(root, paths)
    if destination.is_symlink():
        raise ContractError(
            "native Windows user ABI snapshot may not be a symlink"
        )
    if destination.exists():
        if not destination.is_dir() or any(destination.iterdir()):
            raise ContractError(
                "native Windows user ABI snapshot is not empty"
            )
    else:
        destination.mkdir(mode=0o700)
    for source in paths:
        relative = source.relative_to(root)
        target = destination / relative
        target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    frozen = _snapshot_inputs(
        destination.resolve(),
        _native_windows_user_abi_input_paths(destination),
    )
    if frozen != expected:
        raise ContractError(
            "native Windows user ABI snapshot differs from its source"
        )
    _require_native_windows_user_abi_inputs_unchanged(root, expected)
    return expected


def _require_inputs_unchanged(
    root: Path, expected: dict[str, dict[str, object]]
) -> None:
    actual = _snapshot_contract_inputs(root, _contract_input_paths(root))
    if actual != expected:
        raise ContractError(
            "contract inputs changed while the checked build ran"
        )


def _freeze_contract_inputs(
    root: Path,
    destination: Path,
    paths: Sequence[Path],
    expected: dict[str, dict[str, object]],
    bootstrap_files: dict[str, object] | None = None,
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
    bootstrap_snapshot: dict[str, dict[str, object]] = {}
    copy_paths = set(paths)
    if bootstrap_files is not None:
        bootstrap_snapshot = _manifest_author_bootstrap_snapshot(
            root, bootstrap_files
        )
        if bootstrap_snapshot != bootstrap_files:
            raise ContractError(
                "bootstrap author inputs differ from the checked bootstrap"
            )
        copy_paths.update(
            root.joinpath(*PurePosixPath(logical_path).parts)
            for logical_path in bootstrap_snapshot
        )
    destination.mkdir()
    for source in sorted(
        copy_paths, key=lambda path: path.relative_to(root).as_posix()
    ):
        relative = source.relative_to(root)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if _sha256(source) != _sha256(target):
            raise ContractError(
                f"could not freeze contract input: {relative.as_posix()}"
            )
    frozen = _snapshot_contract_inputs(
        destination, _contract_input_paths(destination)
    )
    if frozen != expected:
        raise ContractError(
            "frozen contract inputs differ from the initial snapshot"
        )
    if bootstrap_files is not None:
        frozen_bootstrap = _manifest_author_bootstrap_snapshot(
            destination, bootstrap_files
        )
        live_bootstrap = _manifest_author_bootstrap_snapshot(
            root, bootstrap_files
        )
        if (
            frozen_bootstrap != bootstrap_snapshot
            or live_bootstrap != bootstrap_snapshot
        ):
            raise ContractError(
                "frozen bootstrap author inputs differ from their snapshot"
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
        raise ContractError(
            f"{label} timed out after {timeout} seconds"
        ) from error
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
    for include_root in CONTRACT_QUOTED_INCLUDE_ROOTS.get(
        logical_source, ()
    ):
        arguments.extend(("-I", include_root))
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
    *,
    definitions: Sequence[str] = (),
    gnu_extensions: bool | None = None,
) -> None:
    logical_output = "/" + output.relative_to(source_root).as_posix()
    arguments: list[str | Path] = ["--root", source_root]
    for definition in definitions:
        arguments.extend(("-D", definition))
    use_gnu_extensions = (
        logical_source in GNU_CONTRACT_SOURCES
        if gnu_extensions is None
        else gnu_extensions
    )
    if use_gnu_extensions:
        arguments.append("--gnu")
    arguments.extend(
        (
            "-c",
            "/" + logical_source,
            *_compile_include_arguments(logical_source),
            "-o",
            logical_output,
        )
    )
    _run_clean(
        runner,
        compiler,
        arguments,
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
            plan.compile_timeout,
        )
        _announce(f"{stage_name} compiled {plan.source}")
        return plan.name, contract_object

    parallel_plans = tuple(
        plan
        for plan in CONTRACT_PLANS
        if not plan.exclusive_compile
    )
    exclusive_plans = tuple(
        plan
        for plan in CONTRACT_PLANS
        if plan.exclusive_compile
    )
    if len(exclusive_plans) != 1:
        raise ContractError(
            f"{stage_name} exclusive contract compile plan differs"
        )
    with ThreadPoolExecutor(max_workers=workers) as executor:
        contract_objects = dict(executor.map(compile_contract, parallel_plans))
    exclusive_name, exclusive_object = compile_contract(exclusive_plans[0])
    contract_objects[exclusive_name] = exclusive_object

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


def _windows_tool_import_arguments() -> tuple[str, ...]:
    return tuple(
        argument
        for library, procedures in WINDOWS_TOOL_IMPORTS
        for procedure in procedures
        for argument in (
            "--import",
            f"__imp_{procedure}={library}:{procedure}",
        )
    )


def _build_manifest_author(
    source_root: Path,
    stage_four: Path,
    output: Path,
) -> Path:
    output.mkdir()
    runner = ToolRunner(source_root)
    contract_object = output / "toolchain-manifest-author.o"
    _compile_source(
        runner,
        stage_four / "cupidc.elf",
        source_root,
        MANIFEST_AUTHOR_SOURCE,
        contract_object,
        "stage four CupidC for the Toolchain manifest author",
        ORDINARY_COMPILE_TIMEOUT,
    )

    windows = _is_windows_host()
    if windows:
        runtime_object = output / "toolchain-manifest-author-runtime.o"
        _compile_source(
            runner,
            stage_four / "cupidc.elf",
            source_root,
            "toolchain/hosted/i386-windows/runtime.cc",
            runtime_object,
            "stage four CupidC for the Windows manifest author runtime",
            ORDINARY_COMPILE_TIMEOUT,
            definitions=("_WIN32=1",),
            gnu_extensions=True,
        )

    startup_object = output / "toolchain-manifest-author-start.o"
    startup_source = source_root / (
        "toolchain/hosted/i386-windows/tool_start.asm"
        if windows
        else "toolchain/hosted/i386-linux/start.asm"
    )
    _run_clean(
        runner,
        stage_four / "cupidasm.elf",
        (
            "-f",
            "elf32",
            startup_source,
            "-o",
            startup_object,
        ),
        "stage four CupidASM for the Toolchain manifest author",
        120,
    )
    _validate_i386_relocatable(startup_object)

    if windows:
        executable = output / "toolchain-manifest-author.exe"
        link_arguments: list[str | Path] = [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
        ]
        link_arguments.extend(_windows_tool_import_arguments())
        link_arguments.extend(
            (
                "-o",
                executable,
                startup_object,
                contract_object,
                runtime_object,
            )
        )
        _run_clean(
            runner,
            stage_four / "cupidld.elf",
            link_arguments,
            "stage four CupidLD for the Windows Toolchain manifest author",
            360,
        )
        try:
            _validate_static_i386_pe32(
                executable,
                int(EXPECTED_WINDOWS_TARGET["entry"]),
                WINDOWS_TOOL_IMPORTS,
            )
        except (BootstrapError, OSError) as error:
            raise ContractError(
                "stage four CupidLD produced an invalid Windows "
                "Toolchain manifest author"
            ) from error
        return executable

    shared_objects = tuple(
        stage_four / name
        for name in ("ctool_host.o", "ctool.o", "runtime.o")
    )
    for shared_object in shared_objects:
        _validate_i386_relocatable(shared_object)
    executable = output / "toolchain-manifest-author.elf"
    _run_clean(
        runner,
        stage_four / "cupidld.elf",
        (
            "-m",
            "elf_i386",
            "--text-address",
            "0x08048000",
            "--entry",
            "_start",
            "-o",
            executable,
            startup_object,
            contract_object,
            *shared_objects,
        ),
        "stage four CupidLD for the Toolchain manifest author",
        360,
    )
    _validate_static_i386_elf(executable, TARGET_ENTRY)
    return executable


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


def _append_author_bytes(payload: bytearray, value: bytes) -> None:
    if len(value) > 0xFFFFFFFF:
        raise ContractError("manifest author fact exceeds its framing limit")
    payload.extend(struct.pack("<I", len(value)))
    payload.extend(value)


def _append_author_observations(
    payload: bytearray,
    observations: Sequence[tuple[str, int, int, str]],
) -> None:
    if len(observations) > 0xFFFFFFFF:
        raise ContractError(
            "manifest author observation inventory exceeds its framing limit"
        )
    payload.extend(struct.pack("<I", len(observations)))
    for name, kind, size, digest in observations:
        if (
            isinstance(kind, bool)
            or not isinstance(kind, int)
            or kind < 0
            or kind > 0xFFFFFFFF
            or isinstance(size, bool)
            or not isinstance(size, int)
            or size < 0
            or size > 0xFFFFFFFFFFFFFFFF
            or not _valid_sha256(digest)
        ):
            raise ContractError("manifest author observation differs")
        try:
            encoded_name = name.encode("ascii")
            encoded_digest = digest.encode("ascii")
        except (AttributeError, UnicodeEncodeError) as error:
            raise ContractError(
                "manifest author observation is not ASCII"
            ) from error
        _append_author_bytes(payload, encoded_name)
        payload.extend(struct.pack("<IQ", kind, size))
        _append_author_bytes(payload, encoded_digest)


def _append_author_pairs(
    payload: bytearray,
    pairs: Sequence[tuple[str, int, bytes, int, bytes]],
) -> None:
    if len(pairs) > 0xFFFFFFFF:
        raise ContractError(
            "manifest author pair inventory exceeds its framing limit"
        )
    payload.extend(struct.pack("<I", len(pairs)))
    for name, first_kind, first_bytes, second_kind, second_bytes in pairs:
        if (
            isinstance(first_kind, bool)
            or not isinstance(first_kind, int)
            or first_kind < 0
            or first_kind > 0xFFFFFFFF
            or isinstance(second_kind, bool)
            or not isinstance(second_kind, int)
            or second_kind < 0
            or second_kind > 0xFFFFFFFF
            or not isinstance(first_bytes, bytes)
            or not isinstance(second_bytes, bytes)
        ):
            raise ContractError("manifest author pair evidence differs")
        try:
            encoded_name = name.encode("ascii")
        except (AttributeError, UnicodeEncodeError) as error:
            raise ContractError(
                "manifest author pair name is not ASCII"
            ) from error
        _append_author_bytes(payload, encoded_name)
        payload.extend(struct.pack("<I", first_kind))
        _append_author_bytes(payload, first_bytes)
        payload.extend(struct.pack("<I", second_kind))
        _append_author_bytes(payload, second_bytes)


def _manifest_author_request(
    artifact_observations: Sequence[tuple[str, int, int, str]],
    input_observations: Sequence[tuple[str, int, int, str]],
    bootstrap_observations: Sequence[tuple[str, int, int, str]],
    bootstrap_snapshot_sha256: str,
    seed_path: str,
    seed_manifest: bytes,
    seed_observations: Sequence[tuple[str, int, int, str]],
    object_pairs: Sequence[tuple[str, int, bytes, int, bytes]],
    executable_pairs: Sequence[tuple[str, int, bytes, int, bytes]],
    bootstrap_object_pairs: Sequence[tuple[str, int, bytes, int, bytes]],
    bootstrap_tool_pairs: Sequence[tuple[str, int, bytes, int, bytes]],
) -> bytes:
    payload = bytearray(MANIFEST_AUTHOR_MAGIC)
    _append_author_observations(payload, artifact_observations)
    _append_author_observations(payload, input_observations)
    _append_author_observations(payload, bootstrap_observations)
    if not _valid_sha256(bootstrap_snapshot_sha256):
        raise ContractError("manifest author bootstrap snapshot differs")
    _append_author_bytes(payload, bootstrap_snapshot_sha256.encode("ascii"))
    try:
        encoded_seed_path = seed_path.encode("ascii")
    except (AttributeError, UnicodeEncodeError) as error:
        raise ContractError("manifest author seed path is not ASCII") from error
    _append_author_bytes(payload, encoded_seed_path)
    _append_author_bytes(payload, seed_manifest)
    _append_author_observations(payload, seed_observations)
    _append_author_pairs(payload, object_pairs)
    _append_author_pairs(payload, executable_pairs)
    _append_author_pairs(payload, bootstrap_object_pairs)
    _append_author_pairs(payload, bootstrap_tool_pairs)
    return bytes(payload)


def _manifest_author_bootstrap_snapshot(
    source_root: Path, files: dict[str, object]
) -> dict[str, dict[str, object]]:
    snapshot: dict[str, dict[str, object]] = {}
    source_root = source_root.resolve()
    for logical_path in sorted(files):
        if not _valid_logical_path(logical_path):
            raise ContractError(
                "manifest author bootstrap input path differs"
            )
        source = source_root.joinpath(*PurePosixPath(logical_path).parts)
        if source.is_symlink():
            raise ContractError(
                "manifest author bootstrap input is a symlink: "
                f"{logical_path}"
            )
        try:
            resolved = source.resolve(strict=True)
            resolved.relative_to(source_root)
            contents = resolved.read_bytes()
        except (OSError, ValueError) as error:
            raise ContractError(
                "manifest author bootstrap input is unavailable: "
                f"{logical_path}"
            ) from error
        if not resolved.is_file():
            raise ContractError(
                "manifest author bootstrap input is not a regular file: "
                f"{logical_path}"
            )
        snapshot[logical_path] = {
            "sha256": hashlib.sha256(contents).hexdigest(),
            "size": len(contents),
        }
    return snapshot


def _capture_stage_pairs(
    first: dict[str, Path],
    second: dict[str, Path],
    artifact_kind: str,
) -> tuple[tuple[str, int, bytes, int, bytes], ...]:
    if set(first) != set(second):
        raise ContractError(
            f"{artifact_kind} stage inventories differ"
        )
    return tuple(
        (
            name,
            *_capture_regular_stage_file(
                first[name], artifact_kind, name
            ),
            *_capture_regular_stage_file(
                second[name], artifact_kind, name
            ),
        )
        for name in sorted(first)
    )


def _capture_regular_stage_file(
    path: Path, artifact_kind: str, name: str
) -> tuple[int, bytes]:
    try:
        path_status = path.lstat()
    except OSError as error:
        raise ContractError(
            f"{artifact_kind} stage file is unavailable: {name}: {error}"
        ) from error
    if not stat.S_ISREG(path_status.st_mode):
        raise ContractError(
            f"{artifact_kind} stage file is not a regular file: {name}"
        )

    flags = os.O_RDONLY
    flags |= getattr(os, "O_BINARY", 0)
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    descriptor = -1
    try:
        descriptor = os.open(path, flags)
        opened_status = os.fstat(descriptor)
        if (
            not stat.S_ISREG(opened_status.st_mode)
            or _stage_file_identity(opened_status)
            != _stage_file_identity(path_status)
        ):
            raise ContractError(
                f"{artifact_kind} stage file identity changed: {name}"
            )
        payload = bytearray()
        while True:
            block = os.read(descriptor, 1024 * 1024)
            if not block:
                break
            payload.extend(block)
        final_status = os.fstat(descriptor)
    except ContractError:
        raise
    except OSError as error:
        raise ContractError(
            f"{artifact_kind} stage file could not be captured: "
            f"{name}: {error}"
        ) from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)

    try:
        live_status = path.lstat()
    except OSError as error:
        raise ContractError(
            f"{artifact_kind} stage file changed during capture: "
            f"{name}: {error}"
        ) from error
    if (
        not stat.S_ISREG(live_status.st_mode)
        or _stage_file_identity(opened_status)
        != _stage_file_identity(final_status)
        or _stage_file_identity(path_status)
        != _stage_file_identity(live_status)
    ):
        raise ContractError(
            f"{artifact_kind} stage file changed during capture: {name}"
        )
    return 1, bytes(payload)


def _stage_file_identity(value: os.stat_result) -> tuple[int, ...]:
    return (
        value.st_dev,
        value.st_ino,
        stat.S_IFMT(value.st_mode),
        value.st_size,
        value.st_mtime_ns,
    )


def _bootstrap_stage_paths(
    stage: Path,
) -> tuple[dict[str, Path], dict[str, Path]]:
    objects = {
        name: stage / f"{name}.o" for name in BOOTSTRAP_OBJECT_NAMES
    }
    tools = {name: stage / f"{name}.elf" for name in TOOL_NAMES}
    return objects, tools


def _checked_manifest_author_bytes(
    source_root: Path,
    bootstrap_stage_three: Path,
    bootstrap_stage_four: Path,
    workspace: Path,
    manifest: Path,
    manifest_relative: str,
    report: dict[str, object],
    artifacts: Sequence[Path],
    stage_three_objects: dict[str, Path],
    stage_four_objects: dict[str, Path],
    stage_three_executables: dict[str, Path],
    stage_four_executables: dict[str, Path],
) -> bytes:
    try:
        seed = freeze_seed_inputs(
            manifest, workspace / "manifest-author-seed"
        )
    except (BootstrapError, OSError) as error:
        raise ContractError(
            f"manifest author seed capture failed: {error}"
        ) from error

    bootstrap = report.get("bootstrap")
    if not isinstance(bootstrap, dict):
        raise ContractError("manifest author bootstrap evidence differs")
    seed_record = bootstrap.get("seed_manifest")
    source_inputs = bootstrap.get("source_inputs")
    inputs = report.get("inputs")
    if (
        not isinstance(seed_record, dict)
        or seed_record.get("path") != manifest_relative
        or seed_record.get("sha256") != seed.manifest_sha256
        or not isinstance(source_inputs, dict)
        or not isinstance(source_inputs.get("files"), dict)
        or not isinstance(inputs, dict)
    ):
        raise ContractError("manifest author evidence differs")

    _require_inputs_unchanged(source_root, inputs)

    artifact_observations = tuple(
        (
            path.name,
            1,
            path.stat().st_size,
            _sha256(path),
        )
        for path in sorted(artifacts, key=lambda value: value.name)
    )
    input_observations = tuple(
        (
            logical_path,
            1,
            source_root.joinpath(
                *PurePosixPath(logical_path).parts
            ).stat().st_size,
            _sha256(
                source_root.joinpath(*PurePosixPath(logical_path).parts)
            ),
        )
        for logical_path, record in sorted(inputs.items())
        if isinstance(logical_path, str) and isinstance(record, dict)
    )
    bootstrap_files = source_inputs["files"]
    bootstrap_snapshot = _manifest_author_bootstrap_snapshot(
        source_root, bootstrap_files
    )
    bootstrap_observations = tuple(
        (
            logical_path,
            1,
            record["size"],
            record["sha256"],
        )
        for logical_path, record in bootstrap_snapshot.items()
    )
    seed_observations = tuple(
        (
            seed.tools[name].name,
            1,
            len(contents),
            hashlib.sha256(contents).hexdigest(),
        )
        for name, contents in seed.artifact_bytes
    )
    bootstrap_stage_three_objects, bootstrap_stage_three_tools = (
        _bootstrap_stage_paths(bootstrap_stage_three)
    )
    bootstrap_stage_four_objects, bootstrap_stage_four_tools = (
        _bootstrap_stage_paths(bootstrap_stage_four)
    )
    object_pairs = _capture_stage_pairs(
        stage_three_objects,
        stage_four_objects,
        "contract object",
    )
    executable_pairs = _capture_stage_pairs(
        stage_three_executables,
        stage_four_executables,
        "contract executable",
    )
    bootstrap_object_pairs = _capture_stage_pairs(
        bootstrap_stage_three_objects,
        bootstrap_stage_four_objects,
        "bootstrap object",
    )
    bootstrap_tool_pairs = _capture_stage_pairs(
        bootstrap_stage_three_tools,
        bootstrap_stage_four_tools,
        "bootstrap tool",
    )
    request = _manifest_author_request(
        artifact_observations,
        input_observations,
        bootstrap_observations,
        _snapshot_sha256(bootstrap_snapshot),
        manifest_relative,
        seed.manifest_bytes,
        seed_observations,
        object_pairs,
        executable_pairs,
        bootstrap_object_pairs,
        bootstrap_tool_pairs,
    )
    request_path = workspace / "toolchain-manifest-author-request.bin"
    request_path.write_bytes(request)
    executable = _build_manifest_author(
        source_root,
        bootstrap_stage_four,
        source_root / "manifest-author-build",
    )
    try:
        require_live_seed_inputs(seed)
        result = ToolRunner(source_root).run(
            executable, ("author", request_path), 360
        )
        require_live_seed_inputs(seed)
        _require_inputs_unchanged(source_root, inputs)
    except (BootstrapError, OSError, subprocess.TimeoutExpired) as error:
        raise ContractError(
            f"Toolchain manifest author could not run: {error}"
        ) from error
    if result.returncode != 0 or result.stderr:
        detail = result.stderr.strip() or result.stdout.strip()
        suffix = f": {detail}" if detail else ""
        raise ContractError(
            "Toolchain manifest author failed with status "
            f"{result.returncode}{suffix}"
        )
    try:
        return result.stdout.encode("ascii")
    except UnicodeEncodeError as error:
        raise ContractError(
            "Toolchain manifest author output is not ASCII"
        ) from error


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


def _valid_digest_size_record(
    value: object, *, require_nonzero: bool = False
) -> bool:
    if not isinstance(value, dict) or set(value) != {"sha256", "size"}:
        return False
    size = value.get("size")
    return (
        _valid_sha256(value.get("sha256"))
        and not isinstance(size, bool)
        and isinstance(size, int)
        and size >= (1 if require_nonzero else 0)
        and size <= 0xFFFFFFFFFFFFFFFF
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


def _tool_fixed_point_record() -> dict[str, object]:
    return {
        "all_equal": True,
        "c_objects": len(BOOTSTRAP_OBJECT_NAMES) - 1,
        "compared_generations": list(CONVERGED_GENERATIONS),
        "startup_objects": 1,
        "tool_images": len(TOOL_NAMES),
    }


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
            or not _valid_digest_size_record(record)
            for path, record in inputs.items()
        )
    ):
        raise ContractError("published contract input inventory differs")
    fixed_point = report.get("tool_fixed_point")
    if fixed_point != _tool_fixed_point_record():
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
            not isinstance(name, str)
            or not _valid_digest_size_record(record, require_nonzero=True)
            for name, record in object_comparisons.items()
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
    actual = _snapshot_contract_inputs(root, _contract_input_paths(root))
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


def _validate_replaceable_legacy_publication(output: Path) -> None:
    expected_names = set(_expected_artifact_names())
    expected_members = expected_names | {"manifest.json"}
    if output.is_symlink() or not output.is_dir():
        raise ContractError("legacy contract output is not a directory")
    members = tuple(output.iterdir())
    if (
        {path.name for path in members} != expected_members
        or any(path.is_symlink() or not path.is_file() for path in members)
    ):
        raise ContractError("legacy contract output is incomplete")
    try:
        report = json.loads(
            (output / "manifest.json").read_text(encoding="ascii")
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ContractError("legacy contract manifest is invalid") from error
    if (
        not isinstance(report, dict)
        or report.get("schema") not in LEGACY_REPORT_SCHEMAS
    ):
        raise ContractError("legacy contract manifest schema differs")
    records = report.get("artifacts")
    if not isinstance(records, list):
        raise ContractError("legacy contract artifact inventory is absent")
    records_by_name: dict[str, dict[str, object]] = {}
    for record in records:
        if (
            not isinstance(record, dict)
            or set(record) != {"path", "sha256", "size"}
        ):
            raise ContractError("legacy contract artifact record differs")
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
            raise ContractError("legacy contract artifact record differs")
        records_by_name[name] = record
    if set(records_by_name) != expected_names:
        raise ContractError("legacy contract artifact inventory differs")
    for name, record in records_by_name.items():
        artifact = output / name
        if (
            artifact.stat().st_size != record["size"]
            or _sha256(artifact) != record["sha256"]
        ):
            raise ContractError(f"legacy contract artifact differs: {name}")


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
        except ContractError:
            try:
                _validate_replaceable_legacy_publication(output)
            except (ContractError, OSError) as legacy_error:
                raise ContractError(
                    "existing contract output is not a complete cohort"
                ) from legacy_error
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
    snapshot = _snapshot_contract_inputs(root, inputs)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f".{output.name}-build-", dir=output.parent
    ) as temporary:
        workspace = Path(temporary)
        bootstrap_output = workspace / "bootstrap"
        _announce("checked-seed bootstrap started")
        try:
            bootstrap_report = _bootstrap_for_manifest_author(
                manifest,
                root,
                bootstrap_output,
            )
        except BootstrapError as error:
            raise ContractError(
                f"checked bootstrap failed: {error}"
            ) from error
        _announce("checked-seed bootstrap completed")

        bootstrap_inputs = bootstrap_report.get("source_inputs")
        if (
            not isinstance(bootstrap_inputs, dict)
            or bootstrap_report.get("status")
            != "pending-fixed-point-author"
            or bootstrap_report.get("seed_manifest_sha256")
            != _sha256(manifest)
            or "comparisons" in bootstrap_report
        ):
            raise ContractError(
                "checked bootstrap report differs before author decision"
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

        private_root = workspace / "source"
        _freeze_contract_inputs(
            root,
            private_root,
            inputs,
            snapshot,
            bootstrap_inputs["files"],
        )
        stage_three_objects, stage_three_executables = _build_contract_stage(
            private_root,
            bootstrap_output / CONVERGED_GENERATIONS[0],
            private_root / "contract-stage-three",
            "stage three",
            workers,
        )
        stage_four_objects, stage_four_executables = _build_contract_stage(
            private_root,
            bootstrap_output / CONVERGED_GENERATIONS[1],
            private_root / "contract-stage-four",
            "stage four",
            workers,
        )
        object_comparisons = {
            name: {
                "sha256": _sha256(path),
                "size": path.stat().st_size,
            }
            for name, path in sorted(stage_four_objects.items())
        }
        comparisons = {
            name: _sha256(path)
            for name, path in sorted(stage_four_executables.items())
        }
        _run_runtime_contract(
            private_root, stage_four_executables["runtime"], workspace
        )
        _announce("hosted runtime contract passed")
        _require_inputs_unchanged(root, snapshot)
        _announce("live inputs still match the frozen build")

        publication = workspace / "publication"
        publication.mkdir()
        artifacts: list[Path] = []
        for plan in CONTRACT_PLANS:
            target = publication / plan.artifact
            shutil.copyfile(stage_four_executables[plan.name], target)
            artifacts.append(target)
        runtime_target = publication / "cupidc-runtime-contract.elf"
        shutil.copyfile(stage_four_executables["runtime"], runtime_target)
        artifacts.append(runtime_target)
        for tool_name in TOOL_NAMES:
            target = publication / TOOL_PUBLIC_NAMES[tool_name]
            shutil.copyfile(
                bootstrap_output
                / CONVERGED_GENERATIONS[1]
                / f"{tool_name}.elf",
                target,
            )
            artifacts.append(target)

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
        }
        authored_report = _checked_manifest_author_bytes(
            private_root,
            bootstrap_output / CONVERGED_GENERATIONS[0],
            bootstrap_output / CONVERGED_GENERATIONS[1],
            workspace,
            manifest,
            manifest_relative,
            report,
            artifacts,
            stage_three_objects,
            stage_four_objects,
            stage_three_executables,
            stage_four_executables,
        )
        object_digests = _compare_stage_files(
            stage_three_objects,
            stage_four_objects,
            "contract object",
        )
        oracle_object_comparisons = {
            name: {
                "sha256": digest,
                "size": stage_four_objects[name].stat().st_size,
            }
            for name, digest in object_digests.items()
        }
        oracle_comparisons = _compare_stage_files(
            stage_three_executables,
            stage_four_executables,
            "contract executable",
        )
        bootstrap_stage_three_objects, bootstrap_stage_three_tools = (
            _bootstrap_stage_paths(
                bootstrap_output / CONVERGED_GENERATIONS[0]
            )
        )
        bootstrap_stage_four_objects, bootstrap_stage_four_tools = (
            _bootstrap_stage_paths(
                bootstrap_output / CONVERGED_GENERATIONS[1]
            )
        )
        _compare_stage_files(
            bootstrap_stage_three_objects,
            bootstrap_stage_four_objects,
            "bootstrap object",
        )
        _compare_stage_files(
            bootstrap_stage_three_tools,
            bootstrap_stage_four_tools,
            "bootstrap tool",
        )
        report["tool_fixed_point"] = _tool_fixed_point_record()
        if (
            object_comparisons != oracle_object_comparisons
            or comparisons != oracle_comparisons
        ):
            raise ContractError(
                "Toolchain manifest evidence differs from the "
                "independent Python comparison"
            )
        _announce(
            "Cupid author and Python oracle agree on all 58 stage pairs"
        )
        oracle_report = (
            json.dumps(report, indent=2, sort_keys=True) + "\n"
        ).encode("ascii")
        if authored_report != oracle_report:
            raise ContractError(
                "Toolchain manifest author output differs from the "
                "independent Python oracle"
            )
        _require_inputs_unchanged(root, snapshot)
        report_path = publication / "manifest.json"
        report_path.write_bytes(authored_report)
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
        try:
            report = verify_publication(output)
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


def _is_windows_host() -> bool:
    return os.name == "nt"


def _checked_user_syscall_abi_report(
    result: subprocess.CompletedProcess[str],
    oracle_report: dict[str, object],
) -> dict[str, object]:
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
    return contract_report


def _native_windows_user_abi_output_path(
    source_root: Path, output: Path
) -> str:
    try:
        relative = output.resolve().relative_to(source_root.resolve())
    except ValueError as error:
        raise ContractError(
            "native Windows user ABI output leaves its private source root"
        ) from error
    return "/" + relative.as_posix()


def _run_native_windows_seed_tool(
    seed: object,
    runner: ToolRunner,
    tool_name: str,
    arguments: Sequence[str | Path],
    label: str,
    timeout: int,
) -> None:
    try:
        tools = getattr(seed, "tools")
        executable = tools[tool_name]
        result = runner.run(executable, arguments, timeout)
        require_live_seed_inputs(seed)
    except (KeyError, TypeError, AttributeError) as error:
        raise ContractError(
            f"native Windows user ABI seed omits {tool_name}"
        ) from error
    except (BootstrapError, OSError, subprocess.TimeoutExpired) as error:
        raise ContractError(f"{label} could not run: {error}") from error
    if result.returncode != 0 or result.stdout or result.stderr:
        detail = result.stderr.strip() or result.stdout.strip()
        suffix = f": {detail}" if detail else ""
        raise ContractError(
            f"{label} failed with status {result.returncode}{suffix}"
        )


def _build_native_windows_user_abi_contract(
    source_root: Path,
    build_root: Path,
    seed: object,
    runner: ToolRunner,
    timeout: int,
) -> Path:
    if timeout <= 0:
        raise ContractError(
            "native Windows user ABI timeout must be positive"
        )
    source_root = source_root.resolve()
    build_root.mkdir(mode=0o700, parents=True)
    objects: dict[str, Path] = {}
    for name, logical_source, definitions, gnu_extensions in (
        NATIVE_WINDOWS_USER_ABI_COMPILE_PLAN
    ):
        output = build_root / f"{name}.o"
        arguments: list[str | Path] = ["--root", source_root]
        for definition in definitions:
            arguments.extend(("-D", definition))
        arguments.extend(
            (
                "-c",
                logical_source,
                "-I",
                "/toolchain",
                "--include-angle",
                "/toolchain/hosted/i386-linux/include",
            )
        )
        if gnu_extensions:
            arguments.append("--gnu")
        arguments.extend(
            (
                "-o",
                _native_windows_user_abi_output_path(
                    source_root, output
                ),
            )
        )
        _run_native_windows_seed_tool(
            seed,
            runner,
            "cupidc",
            arguments,
            f"native Windows CupidC for {logical_source}",
            timeout,
        )
        try:
            _validate_i386_relocatable(output)
        except (BootstrapError, OSError) as error:
            raise ContractError(
                f"native Windows CupidC produced an invalid object: {name}"
            ) from error
        objects[name] = output

    start = build_root / "start.o"
    _run_native_windows_seed_tool(
        seed,
        runner,
        "cupidasm",
        (
            "-f",
            "elf32",
            source_root / "toolchain/hosted/i386-windows/tool_start.asm",
            "-o",
            start,
        ),
        "native Windows CupidASM for the ABI contract startup",
        timeout,
    )
    try:
        _validate_i386_relocatable(start)
    except (BootstrapError, OSError) as error:
        raise ContractError(
            "native Windows CupidASM produced an invalid startup object"
        ) from error
    objects["start"] = start

    executable = build_root / "user-syscall-abi-contract.exe"
    link_arguments: list[str | Path] = [
        "-m",
        "i386pe",
        "--text-address",
        "0x00401000",
        "--entry",
        "_start",
    ]
    link_arguments.extend(_windows_tool_import_arguments())
    link_arguments.extend(("-o", executable))
    link_arguments.extend(
        objects[name] for name in NATIVE_WINDOWS_USER_ABI_LINK_ORDER
    )
    _run_native_windows_seed_tool(
        seed,
        runner,
        "cupidld",
        link_arguments,
        "native Windows CupidLD for the ABI contract",
        timeout,
    )
    try:
        _validate_static_i386_pe32(
            executable,
            int(EXPECTED_WINDOWS_TARGET["entry"]),
            WINDOWS_TOOL_IMPORTS,
        )
    except (BootstrapError, OSError) as error:
        raise ContractError(
            "native Windows CupidLD produced an invalid PE contract"
        ) from error
    return executable


def _run_native_windows_user_syscall_abi(
    root: Path, manifest: Path, timeout: int
) -> dict[str, object]:
    if not _is_windows_host():
        raise ContractError(
            "the native Windows user syscall ABI check requires Windows"
        )
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupid-native-windows-user-abi-"
        ) as temporary:
            private = Path(temporary)
            source_root = private / "source"
            source_snapshot = _freeze_native_windows_user_abi_inputs(
                root, source_root
            )
            seed = freeze_seed_inputs(manifest, private / "seed")
            if seed.manifest.get("schema") != WINDOWS_SEED_SCHEMA:
                raise ContractError(
                    "native Windows user ABI requires the checked Windows "
                    "execution seed"
                )
            runner = ToolRunner(source_root)
            executable = _build_native_windows_user_abi_contract(
                source_root,
                source_root / "build/user-syscall-abi",
                seed,
                runner,
                timeout,
            )
            result = runner.run(
                executable,
                ("check-snapshot", source_root, root),
                timeout,
            )
            try:
                oracle_report = check_syscall_abi(source_root)
            except UserSyscallAbiError as error:
                raise ContractError(
                    "independent user syscall ABI oracle failed: "
                    f"{error}"
                ) from error
            require_live_seed_inputs(seed)
            _require_native_windows_user_abi_inputs_unchanged(
                root, source_snapshot
            )
            return _checked_user_syscall_abi_report(
                result, oracle_report
            )
    except ContractError:
        raise
    except (BootstrapError, OSError, subprocess.TimeoutExpired) as error:
        raise ContractError(
            f"native Windows user syscall ABI check failed: {error}"
        ) from error


def run_user_syscall_abi(
    root: Path,
    manifest: Path,
    output: Path,
    workers: int = 2,
    timeout: int = 60,
    windows_manifest: Path | None = None,
) -> dict[str, object]:
    root = root.resolve()
    if windows_manifest is not None:
        return _run_native_windows_user_syscall_abi(
            root, windows_manifest, timeout
        )
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
    contract_report = _checked_user_syscall_abi_report(
        result, oracle_report
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
    user_abi.add_argument("--windows-manifest", type=Path)
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
                arguments.windows_manifest,
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

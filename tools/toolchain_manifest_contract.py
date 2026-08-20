#!/usr/bin/env python3
"""Check a Toolchain publication manifest with a Cupid-built contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import struct
import subprocess
import sys
import tempfile
from contextlib import nullcontext
from pathlib import Path, PurePosixPath
from typing import Sequence

try:
    from tools import artifact_size_policy, cupidc_toolchain_contracts
    from tools.bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_WINDOWS_TARGET,
        SEED_SCHEMA,
        TOOL_NAMES,
        WINDOWS_SEED_SCHEMA,
        WINDOWS_TOOL_IMPORTS,
        ToolRunner,
        _validate_build_plan,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        _validate_static_i386_pe32,
        freeze_seed_inputs,
        require_live_seed_inputs,
    )
except ModuleNotFoundError:
    import artifact_size_policy
    import cupidc_toolchain_contracts
    from bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_WINDOWS_TARGET,
        SEED_SCHEMA,
        TOOL_NAMES,
        WINDOWS_SEED_SCHEMA,
        WINDOWS_TOOL_IMPORTS,
        ToolRunner,
        _validate_build_plan,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        _validate_static_i386_pe32,
        freeze_seed_inputs,
        require_live_seed_inputs,
    )


REPORT_SCHEMA = "cupid.toolchain-manifest-verification.v1"
REQUEST_MAGIC = b"CUPMAN2\0"
REGULAR_FILE = 1
LINUX_ENTRY = 0x08048000
WINDOWS_ENTRY = int(EXPECTED_WINDOWS_TARGET["entry"])
DEFAULT_TIMEOUT = 900
BUILD_INPUTS = (
    "toolchain/Makefile",
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
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "tools/artifact_size_policy.py",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/toolchain_manifest_contract.py",
)


class ToolchainManifestContractError(RuntimeError):
    """The checked contract could not prove a Toolchain publication."""


def _pack_bytes(value: bytes) -> bytes:
    if len(value) > 0xFFFFFFFF:
        raise ToolchainManifestContractError(
            "contract request field is too large"
        )
    return struct.pack("<I", len(value)) + value


def _append_observations(
    output: bytearray,
    observations: Sequence[tuple[str, int, int, str]],
) -> None:
    if len(observations) > 0xFFFFFFFF:
        raise ToolchainManifestContractError(
            "contract observation count is too large"
        )
    output.extend(struct.pack("<I", len(observations)))
    for name, kind, size, digest in observations:
        if (
            kind < 0
            or kind > 0xFFFFFFFF
            or size < 0
            or size > 0xFFFFFFFFFFFFFFFF
        ):
            raise ToolchainManifestContractError(
                "contract observation is out of range"
            )
        output.extend(_pack_bytes(name.encode("utf-8")))
        output.extend(struct.pack("<IQ", kind, size))
        output.extend(_pack_bytes(digest.encode("ascii")))


def _encode_request(
    manifest_bytes: bytes,
    artifact_observations: Sequence[tuple[str, int, int, str]],
    input_observations: Sequence[tuple[str, int, int, str]],
    bootstrap_observations: Sequence[tuple[str, int, int, str]],
    seed_manifest_path: str,
    seed_manifest_bytes: bytes,
    seed_observations: Sequence[tuple[str, int, int, str]],
) -> bytes:
    output = bytearray(REQUEST_MAGIC)
    output.extend(_pack_bytes(manifest_bytes))
    _append_observations(output, artifact_observations)
    _append_observations(output, input_observations)
    _append_observations(output, bootstrap_observations)
    output.extend(_pack_bytes(seed_manifest_path.encode("utf-8")))
    output.extend(_pack_bytes(seed_manifest_bytes))
    _append_observations(output, seed_observations)
    return bytes(output)


def _resolve_repository_path(
    root: Path, argument: Path, label: str
) -> tuple[str, Path]:
    logical = artifact_size_policy._repository_argument(root, argument, label)
    return logical, root.joinpath(*PurePosixPath(logical).parts)


def _expected_report(report: dict[str, object]) -> dict[str, object]:
    artifacts = report.get("artifacts")
    bootstrap = report.get("bootstrap")
    inputs = report.get("inputs")
    if (
        not isinstance(artifacts, list)
        or not isinstance(bootstrap, dict)
        or not isinstance(inputs, dict)
    ):
        raise ToolchainManifestContractError(
            "Python Toolchain manifest oracle returned an invalid report"
        )
    source_inputs = bootstrap.get("source_inputs")
    if not isinstance(source_inputs, dict):
        raise ToolchainManifestContractError(
            "Python Toolchain manifest oracle returned an invalid report"
        )
    source_files = source_inputs.get("files")
    if not isinstance(source_files, dict):
        raise ToolchainManifestContractError(
            "Python Toolchain manifest oracle returned an invalid report"
        )
    total = 0
    for artifact in artifacts:
        if not isinstance(artifact, dict):
            raise ToolchainManifestContractError(
                "Python Toolchain manifest oracle returned an invalid report"
            )
        size = artifact.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or size < 0:
            raise ToolchainManifestContractError(
                "Python Toolchain manifest oracle returned an invalid report"
            )
        total += size
    return {
        "artifact_count": len(artifacts),
        "artifact_total_bytes": total,
        "bootstrap_source_input_count": len(source_files),
        "input_count": len(inputs),
        "schema": REPORT_SCHEMA,
    }


def _validate_contract_report(report: object) -> dict[str, object]:
    expected_fields = {
        "artifact_count",
        "artifact_total_bytes",
        "bootstrap_source_input_count",
        "input_count",
        "schema",
    }
    if not isinstance(report, dict):
        raise ToolchainManifestContractError(
            "Cupid-built Toolchain manifest contract returned a non-object"
        )
    if set(report) != expected_fields:
        raise ToolchainManifestContractError(
            "Cupid-built Toolchain manifest contract returned unexpected fields"
        )
    if report["schema"] != REPORT_SCHEMA:
        raise ToolchainManifestContractError(
            "Cupid-built Toolchain manifest contract returned the wrong schema"
        )
    for field in expected_fields - {"schema"}:
        value = report[field]
        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or value < 0
        ):
            raise ToolchainManifestContractError(
                "Cupid-built Toolchain manifest contract returned invalid counts"
            )
    return report


def _capture_request(
    reader: artifact_size_policy._PinnedRepository,
    logical_output: str,
) -> tuple[
    bytes,
    tuple[tuple[str, int, int, str], ...],
    tuple[str, ...],
    os.stat_result,
    tuple[tuple[str, bytes], ...],
]:
    try:
        directory_status, names = reader.directory_snapshot(logical_output)
    except (OSError, artifact_size_policy.SizePolicyError) as error:
        raise ToolchainManifestContractError(
            f"cannot inspect Toolchain publication: {error}"
        ) from error
    expected_names = {
        *cupidc_toolchain_contracts._expected_artifact_names(),
        "manifest.json",
    }
    if set(names) != expected_names:
        raise ToolchainManifestContractError(
            "Toolchain publication artifact membership differs"
        )
    manifest_logical = f"{logical_output}/manifest.json"
    manifest_bytes = artifact_size_policy._required_capture(
        reader, manifest_logical, "Toolchain publication manifest"
    )
    snapshots = [(manifest_logical, manifest_bytes)]
    observations: list[tuple[str, int, int, str]] = []
    for name in names:
        if name == "manifest.json":
            continue
        logical = f"{logical_output}/{name}"
        capture, issue = reader.capture(logical, read_payload=True)
        if (
            issue is not None
            or capture is None
            or capture.payload is None
        ):
            raise ToolchainManifestContractError(
                f"Toolchain publication artifact {issue or 'is unavailable'}"
            )
        observations.append(
            (
                name,
                REGULAR_FILE,
                capture.status.st_size,
                hashlib.sha256(capture.payload).hexdigest(),
            )
        )
        snapshots.append((logical, capture.payload))
    return (
        manifest_bytes,
        tuple(observations),
        names,
        directory_status,
        tuple(snapshots),
    )


def _verify_captured_publication(
    snapshots: Sequence[tuple[str, bytes]],
) -> dict[str, object]:
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupid-toolchain-manifest-oracle-"
        ) as temporary:
            publication = Path(temporary)
            for logical, payload in snapshots:
                name = PurePosixPath(logical).name
                target = publication / name
                if not name or target.exists():
                    raise ToolchainManifestContractError(
                        "captured Toolchain publication has duplicate members"
                    )
                target.write_bytes(payload)
            return cupidc_toolchain_contracts.verify_publication(publication)
    except ToolchainManifestContractError:
        raise
    except (cupidc_toolchain_contracts.ContractError, OSError) as error:
        raise ToolchainManifestContractError(str(error)) from error


def _decode_json_object(payload: bytes, label: str) -> dict[str, object]:
    def reject_duplicate_keys(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ToolchainManifestContractError(
                    f"{label} contains a duplicate JSON key: {key}"
                )
            value[key] = item
        return value

    try:
        decoded = json.loads(
            payload.decode("ascii"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ToolchainManifestContractError(
            f"{label} is invalid: {error}"
        ) from error
    if not isinstance(decoded, dict):
        raise ToolchainManifestContractError(f"{label} is not an object")
    return decoded


def _require_logical_path(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or "\0" in value
        or "\\" in value
    ):
        raise ToolchainManifestContractError(f"{label} differs")
    logical = PurePosixPath(value)
    if (
        logical.is_absolute()
        or logical.as_posix() != value
        or any(part in {"", ".", ".."} for part in logical.parts)
    ):
        raise ToolchainManifestContractError(f"{label} differs")
    return value


def _seed_artifact_files(
    manifest: dict[str, object],
    *,
    suffix: str,
    label: str,
) -> tuple[str, ...]:
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != len(TOOL_NAMES):
        raise ToolchainManifestContractError(f"{label} artifacts differ")
    files: dict[str, str] = {}
    for artifact in artifacts:
        if not isinstance(artifact, dict):
            raise ToolchainManifestContractError(f"{label} artifact differs")
        name = artifact.get("name")
        file_name = artifact.get("file")
        if (
            not isinstance(name, str)
            or name not in TOOL_NAMES
            or name in files
            or file_name != f"{name}.{suffix}"
        ):
            raise ToolchainManifestContractError(f"{label} artifact differs")
        files[name] = file_name
    if set(files) != set(TOOL_NAMES):
        raise ToolchainManifestContractError(f"{label} artifacts differ")
    return tuple(files[name] for name in sorted(files))


def _capture_regular_observations(
    reader: artifact_size_policy._PinnedRepository,
    logical_paths: Sequence[str],
    label: str,
) -> tuple[
    tuple[tuple[str, int, int, str], ...],
    tuple[tuple[str, bytes], ...],
]:
    observations = []
    snapshots = []
    for logical in sorted(logical_paths):
        capture, issue = reader.capture(logical, read_payload=True)
        if issue is not None or capture is None or capture.payload is None:
            raise ToolchainManifestContractError(f"{label} {issue}")
        observations.append(
            (
                logical,
                REGULAR_FILE,
                capture.status.st_size,
                hashlib.sha256(capture.payload).hexdigest(),
            )
        )
        snapshots.append((logical, capture.payload))
    return tuple(observations), tuple(snapshots)


def _directory_members_with_suffix(
    reader: artifact_size_policy._PinnedRepository,
    logical_directory: str,
    suffix: str,
) -> tuple[str, ...]:
    _status, names = reader.directory_snapshot(logical_directory)
    return tuple(
        f"{logical_directory}/{name}"
        for name in names
        if PurePosixPath(name).suffix == suffix
    )


def _contract_input_logical_paths(
    reader: artifact_size_policy._PinnedRepository,
) -> tuple[str, ...]:
    paths = {plan.source for plan in cupidc_toolchain_contracts.CONTRACT_PLANS}
    paths.update(cupidc_toolchain_contracts.CONTRACT_CONTROL_INPUTS)
    paths.update(cupidc_toolchain_contracts.WINDOWS_RUNTIME_INPUTS)
    paths.update(cupidc_toolchain_contracts.USER_SYSCALL_ABI_INPUTS)
    paths.update(
        {
            "toolchain/tests/hosted_i386_runtime_contract.cc",
            "kernel/lang/as_elf.cc",
            "kernel/lang/as_elf.h",
            "toolchain/x86.cc",
        }
    )
    for directory, suffix in (
        ("toolchain", ".h"),
        ("toolchain/tests", ".inc"),
        ("toolchain/tests", ".h"),
        ("toolchain/hosted/i386-linux/include", ".h"),
    ):
        paths.update(_directory_members_with_suffix(reader, directory, suffix))
    return tuple(sorted(paths))


def _bootstrap_input_logical_paths(
    reader: artifact_size_policy._PinnedRepository,
    plan: dict[str, object],
) -> tuple[str, ...]:
    raw_sources = plan.get("sources")
    startup = plan.get("startup")
    if not isinstance(raw_sources, list) or not isinstance(startup, str):
        raise ToolchainManifestContractError(
            "Toolchain publication seed build plan differs"
        )
    paths = []
    for raw_source in raw_sources:
        if not isinstance(raw_source, dict):
            raise ToolchainManifestContractError(
                "Toolchain publication seed build plan differs"
            )
        source = raw_source.get("path")
        if not isinstance(source, str):
            raise ToolchainManifestContractError(
                "Toolchain publication seed build plan differs"
            )
        paths.append(
            _require_logical_path(
                source.lstrip("/"), "Toolchain bootstrap source path"
            )
        )
    paths.extend(
        (
            _require_logical_path(
                startup.lstrip("/"), "Toolchain bootstrap startup path"
            ),
            "link.ld",
            "toolchain/hosted/i386-windows/start.asm",
            "toolchain/hosted/i386-windows/runtime.cc",
            "toolchain/hosted/i386-windows/tool_start.asm",
            "toolchain/hosted/i386-windows/publication_runtime.cc",
            "toolchain/hosted/i386-windows/publication_start.asm",
            "toolchain/tests/hosted_i386_windows_contract.cc",
            "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
        )
    )
    paths.extend(_directory_members_with_suffix(reader, "toolchain", ".h"))
    paths.extend(
        _directory_members_with_suffix(
            reader, "toolchain/hosted/i386-linux/include", ".h"
        )
    )
    if len(paths) != len(set(paths)):
        raise ToolchainManifestContractError(
            "Toolchain bootstrap source input is duplicated"
        )
    return tuple(sorted(paths))


def _capture_live_manifest_closure(
    reader: artifact_size_policy._PinnedRepository,
    _root: Path,
    report: dict[str, object],
) -> tuple[
    tuple[tuple[str, int, int, str], ...],
    tuple[tuple[str, int, int, str], ...],
    str,
    bytes,
    tuple[tuple[str, int, int, str], ...],
    tuple[tuple[str, bytes], ...],
]:
    input_paths = _contract_input_logical_paths(reader)
    input_observations, input_snapshots = _capture_regular_observations(
        reader,
        input_paths,
        "Toolchain publication input",
    )
    bootstrap = report.get("bootstrap")
    if not isinstance(bootstrap, dict):
        raise ToolchainManifestContractError(
            "Toolchain publication bootstrap record differs"
        )
    seed_record = bootstrap.get("seed_manifest")
    if not isinstance(seed_record, dict):
        raise ToolchainManifestContractError(
            "Toolchain publication seed record differs"
        )
    seed_path = seed_record.get("path")
    seed_path = _require_logical_path(
        seed_path, "Toolchain publication seed path"
    )
    seed_bytes = artifact_size_policy._required_capture(
        reader,
        seed_path,
        "Toolchain publication seed manifest",
    )
    seed_manifest = _decode_json_object(
        seed_bytes,
        "Toolchain publication seed manifest",
    )
    if seed_manifest.get("schema") != SEED_SCHEMA:
        raise ToolchainManifestContractError(
            "Toolchain publication seed schema differs"
        )
    _validate_build_plan(seed_manifest)
    build_plan = seed_manifest.get("build_plan")
    if not isinstance(build_plan, dict):
        raise ToolchainManifestContractError(
            "Toolchain publication seed build plan differs"
        )
    bootstrap_paths = _bootstrap_input_logical_paths(reader, build_plan)
    bootstrap_observations, bootstrap_snapshots = (
        _capture_regular_observations(
            reader,
            bootstrap_paths,
            "Toolchain bootstrap source input",
        )
    )
    seed_parent = PurePosixPath(seed_path).parent
    seed_files = [
        (seed_parent / name).as_posix()
        for name in _seed_artifact_files(
            seed_manifest,
            suffix="elf",
            label="Toolchain publication seed",
        )
    ]
    seed_observations, seed_snapshots = _capture_regular_observations(
        reader,
        seed_files,
        "Toolchain publication seed artifact",
    )
    seed_observations = tuple(
        (PurePosixPath(path).name, kind, size, digest)
        for path, kind, size, digest in seed_observations
    )
    snapshots = (
        *input_snapshots,
        *bootstrap_snapshots,
        (seed_path, seed_bytes),
        *seed_snapshots,
    )
    return (
        input_observations,
        bootstrap_observations,
        seed_path,
        seed_bytes,
        seed_observations,
        snapshots,
    )


def _require_publication_unchanged(
    reader: artifact_size_policy._PinnedRepository,
    logical_output: str,
    output: Path,
    names: tuple[str, ...],
    directory_status: os.stat_result,
    snapshots: Sequence[tuple[str, bytes]],
) -> None:
    try:
        for logical, expected_payload in snapshots:
            capture, issue = reader.capture(logical, read_payload=True)
            if (
                issue is not None
                or capture is None
                or capture.payload != expected_payload
            ):
                raise ToolchainManifestContractError(
                    "Toolchain publication changed while the contract ran"
                )
        reader.require_unchanged()
        pinned_status, current_names = reader.directory_snapshot(logical_output)
        current = output.lstat()
    except (OSError, artifact_size_policy.SizePolicyError) as error:
        raise ToolchainManifestContractError(
            f"Toolchain publication changed while the contract ran: {error}"
        ) from error
    if (
        artifact_size_policy._is_link_or_reparse(current)
        or not stat.S_ISDIR(current.st_mode)
        or artifact_size_policy._stable_file_fields(current)
        != artifact_size_policy._stable_file_fields(directory_status)
        or artifact_size_policy._stable_file_fields(pinned_status)
        != artifact_size_policy._stable_file_fields(directory_status)
        or current_names != names
    ):
        raise ToolchainManifestContractError(
            "Toolchain publication changed while the contract ran"
        )


def _require_live_closure_membership(
    reader: artifact_size_policy._PinnedRepository,
    input_observations: Sequence[tuple[str, int, int, str]],
    bootstrap_observations: Sequence[tuple[str, int, int, str]],
    seed_bytes: bytes,
) -> None:
    current_inputs = _contract_input_logical_paths(reader)
    expected_inputs = tuple(
        sorted(path for path, _kind, _size, _digest in input_observations)
    )
    seed_manifest = _decode_json_object(
        seed_bytes, "Toolchain publication seed manifest"
    )
    _validate_build_plan(seed_manifest)
    build_plan = seed_manifest.get("build_plan")
    if not isinstance(build_plan, dict):
        raise ToolchainManifestContractError(
            "Toolchain publication seed build plan differs"
        )
    current_bootstrap = _bootstrap_input_logical_paths(reader, build_plan)
    expected_bootstrap = tuple(
        sorted(
            path
            for path, _kind, _size, _digest in bootstrap_observations
        )
    )
    if (
        current_inputs != expected_inputs
        or current_bootstrap != expected_bootstrap
    ):
        raise ToolchainManifestContractError(
            "Toolchain publication input membership changed while the "
            "contract ran"
        )


def _require_root_unchanged(
    root: Path, expected_status: os.stat_result
) -> None:
    try:
        current = root.lstat()
    except OSError as error:
        raise ToolchainManifestContractError(
            f"repository root changed while the contract ran: {error}"
        ) from error
    if (
        artifact_size_policy._is_link_or_reparse(current)
        or not stat.S_ISDIR(current.st_mode)
        or artifact_size_policy._stable_file_fields(current)
        != artifact_size_policy._stable_file_fields(expected_status)
    ):
        raise ToolchainManifestContractError(
            "repository root changed while the contract ran"
        )


def _freeze_build_inputs(
    reader: artifact_size_policy._PinnedRepository,
    source_root: Path,
) -> tuple[tuple[str, bytes], ...]:
    snapshots = []
    for logical in BUILD_INPUTS:
        payload = artifact_size_policy._required_capture(
            reader, logical, "contract build input"
        )
        target = source_root.joinpath(*PurePosixPath(logical).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        snapshots.append((logical, payload))
    return tuple(snapshots)


def _require_payloads_unchanged(
    reader: artifact_size_policy._PinnedRepository,
    snapshots: Sequence[tuple[str, bytes]],
    label: str,
) -> None:
    for logical, expected in snapshots:
        capture, issue = reader.capture(logical, read_payload=True)
        if (
            issue is not None
            or capture is None
            or capture.payload != expected
        ):
            raise ToolchainManifestContractError(
                f"{label} changed while the Cupid contract ran"
            )


def _seed_directory_membership(
    reader: artifact_size_policy._PinnedRepository,
    logical_manifest: str,
    suffix: str,
    expected_files: Sequence[str],
) -> tuple[str, os.stat_result, tuple[str, ...]]:
    parent = PurePosixPath(logical_manifest).parent
    logical_directory = "" if parent == PurePosixPath(".") else parent.as_posix()
    try:
        status, members = reader.directory_snapshot(logical_directory)
        names = tuple(
            sorted(
                name
                for name in members
                if PurePosixPath(name).suffix.casefold() == f".{suffix}"
            )
        )
    except (OSError, artifact_size_policy.SizePolicyError) as error:
        raise ToolchainManifestContractError(
            f"cannot inspect execution seed directory: {error}"
        ) from error
    if (
        artifact_size_policy._is_link_or_reparse(status)
        or not stat.S_ISDIR(status.st_mode)
        or names != tuple(sorted(expected_files))
    ):
        raise ToolchainManifestContractError(
            "execution seed artifact membership differs"
        )
    return logical_directory, status, names


def _require_seed_directory_unchanged(
    reader: artifact_size_policy._PinnedRepository,
    membership: tuple[str, os.stat_result, tuple[str, ...]],
    suffix: str,
) -> None:
    logical_directory, expected_status, expected_names = membership
    try:
        current, members = reader.directory_snapshot(logical_directory)
        names = tuple(
            sorted(
                name
                for name in members
                if PurePosixPath(name).suffix.casefold() == f".{suffix}"
            )
        )
        live_path = reader.root.joinpath(
            *PurePosixPath(logical_directory).parts
        )
        live = live_path.lstat()
    except (OSError, artifact_size_policy.SizePolicyError) as error:
        raise ToolchainManifestContractError(
            f"execution seed changed while the Cupid contract ran: {error}"
        ) from error
    if (
        artifact_size_policy._is_link_or_reparse(current)
        or not stat.S_ISDIR(current.st_mode)
        or artifact_size_policy._stable_file_fields(current)
        != artifact_size_policy._stable_file_fields(expected_status)
        or artifact_size_policy._stable_file_fields(live)
        != artifact_size_policy._stable_file_fields(expected_status)
        or names != expected_names
    ):
        raise ToolchainManifestContractError(
            "execution seed changed while the Cupid contract ran"
        )


def _freeze_execution_seed(
    reader: artifact_size_policy._PinnedRepository,
    logical_manifest: str,
    private: Path,
) -> tuple[
    object,
    tuple[tuple[str, bytes], ...],
    tuple[str, os.stat_result, tuple[str, ...]],
    str,
]:
    manifest_bytes = artifact_size_policy._required_capture(
        reader, logical_manifest, "execution seed manifest"
    )
    manifest = _decode_json_object(manifest_bytes, "execution seed manifest")
    schema = manifest.get("schema")
    if schema == WINDOWS_SEED_SCHEMA:
        suffix = "exe"
    elif schema == SEED_SCHEMA:
        suffix = "elf"
    else:
        raise ToolchainManifestContractError(
            "execution seed manifest schema differs"
        )
    file_names = _seed_artifact_files(
        manifest, suffix=suffix, label="execution seed manifest"
    )
    membership = _seed_directory_membership(
        reader, logical_manifest, suffix, file_names
    )
    manifest_parent = PurePosixPath(logical_manifest).parent
    source = private / "execution-seed-source"
    source.mkdir(mode=0o700)
    private_manifest = source / "manifest.json"
    private_manifest.write_bytes(manifest_bytes)
    snapshots = [(logical_manifest, manifest_bytes)]
    for file_name in file_names:
        logical = (manifest_parent / file_name).as_posix()
        payload = artifact_size_policy._required_capture(
            reader, logical, "execution seed artifact"
        )
        (source / file_name).write_bytes(payload)
        snapshots.append((logical, payload))
    seed = freeze_seed_inputs(private_manifest, private / "seed")
    return seed, tuple(snapshots), membership, suffix


def _run_checked_tool(
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
        raise ToolchainManifestContractError(
            f"execution seed omits {tool_name}"
        ) from error
    except subprocess.TimeoutExpired as error:
        raise ToolchainManifestContractError(
            f"{label} timed out after {timeout} seconds"
        ) from error
    except (BootstrapError, OSError) as error:
        raise ToolchainManifestContractError(
            f"{label} could not run: {error}"
        ) from error
    if result.returncode != 0 or result.stdout or result.stderr:
        detail = result.stderr.strip() or result.stdout.strip()
        suffix = f": {detail}" if detail else ""
        raise ToolchainManifestContractError(
            f"{label} failed with status {result.returncode}{suffix}"
        )


def _logical_output(source_root: Path, output: Path) -> str:
    try:
        return "/" + output.relative_to(source_root).as_posix()
    except ValueError as error:
        raise ToolchainManifestContractError(
            "contract build output leaves its private source root"
        ) from error


def _compile_source(
    seed: object,
    runner: ToolRunner,
    source_root: Path,
    logical_source: str,
    output: Path,
    definitions: Sequence[str],
    gnu_extensions: bool,
    timeout: int,
) -> None:
    arguments: list[str | Path] = ["--root", source_root]
    for definition in definitions:
        arguments.extend(("-D", definition))
    if gnu_extensions:
        arguments.append("--gnu")
    arguments.extend(
        (
            "-c",
            "/" + logical_source,
            "-I",
            "/toolchain",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_output(source_root, output),
        )
    )
    _run_checked_tool(
        seed,
        runner,
        "cupidc",
        arguments,
        f"CupidC for {logical_source}",
        timeout,
    )
    try:
        _validate_i386_relocatable(output)
    except (BootstrapError, OSError) as error:
        raise ToolchainManifestContractError(
            f"CupidC produced an invalid object for {logical_source}"
        ) from error


def _link_contract(
    seed: object,
    runner: ToolRunner,
    objects: Sequence[Path],
    executable: Path,
    windows: bool,
    timeout: int,
) -> None:
    if windows:
        arguments: list[str | Path] = [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
        ]
        for library, procedures in WINDOWS_TOOL_IMPORTS:
            for procedure in procedures:
                arguments.extend(
                    ("--import", f"__imp_{procedure}={library}:{procedure}")
                )
    else:
        arguments = [
            "-m",
            "elf_i386",
            "--text-address",
            "0x08048000",
            "--entry",
            "_start",
        ]
    arguments.extend(("-o", executable, *objects))
    _run_checked_tool(
        seed,
        runner,
        "cupidld",
        arguments,
        "CupidLD for the Toolchain manifest contract",
        timeout,
    )
    try:
        if windows:
            _validate_static_i386_pe32(
                executable, WINDOWS_ENTRY, WINDOWS_TOOL_IMPORTS
            )
        else:
            _validate_static_i386_elf(executable, LINUX_ENTRY)
    except (BootstrapError, OSError) as error:
        raise ToolchainManifestContractError(
            "CupidLD produced an invalid Toolchain manifest contract"
        ) from error


def _build_and_run_contract(
    reader: artifact_size_policy._PinnedRepository,
    root: Path,
    logical_execution: str,
    request: bytes,
    timeout: int,
) -> dict[str, object]:
    if timeout <= 0:
        raise ToolchainManifestContractError(
            "contract timeout must be positive"
        )
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupid-toolchain-manifest-contract-"
        ) as temporary:
            private = Path(temporary)
            source_root = private / "source"
            source_root.mkdir()
            with nullcontext(reader):
                build_snapshots = _freeze_build_inputs(reader, source_root)
                (
                    seed,
                    seed_snapshots,
                    seed_membership,
                    seed_suffix,
                ) = _freeze_execution_seed(
                    reader, logical_execution, private
                )
                windows = os.name == "nt"
                schema = seed.manifest.get("schema")
                if windows and schema != WINDOWS_SEED_SCHEMA:
                    raise ToolchainManifestContractError(
                        "Windows requires the checked native execution seed"
                    )
                if not windows and schema != artifact_size_policy.SEED_SCHEMA:
                    raise ToolchainManifestContractError(
                        "Linux requires the checked bootstrap seed"
                    )

                build_root = source_root / "build/toolchain-manifest-contract"
                build_root.mkdir(parents=True)
                runner = ToolRunner(source_root)
                contract_object = build_root / "contract.o"
                runtime_object = build_root / "runtime.o"
                start_object = build_root / "start.o"
                runtime_source = (
                    "toolchain/hosted/i386-windows/runtime.cc"
                    if windows
                    else "toolchain/hosted/i386-linux/runtime.cc"
                )
                startup_source = (
                    "toolchain/hosted/i386-windows/tool_start.asm"
                    if windows
                    else "toolchain/hosted/i386-linux/start.asm"
                )
                _compile_source(
                    seed,
                    runner,
                    source_root,
                    "toolchain/tests/toolchain_manifest_contract.cc",
                    contract_object,
                    (),
                    False,
                    timeout,
                )
                _compile_source(
                    seed,
                    runner,
                    source_root,
                    runtime_source,
                    runtime_object,
                    ("_WIN32=1",) if windows else (),
                    True,
                    timeout,
                )
                _run_checked_tool(
                    seed,
                    runner,
                    "cupidasm",
                    (
                        "-f",
                        "elf32",
                        source_root.joinpath(
                            *PurePosixPath(startup_source).parts
                        ),
                        "-o",
                        start_object,
                    ),
                    "CupidASM for the Toolchain manifest contract startup",
                    timeout,
                )
                try:
                    _validate_i386_relocatable(start_object)
                except (BootstrapError, OSError) as error:
                    raise ToolchainManifestContractError(
                        "CupidASM produced an invalid Toolchain manifest "
                        "contract startup object"
                    ) from error
                executable = build_root / (
                    "toolchain-manifest-contract.exe"
                    if windows
                    else "toolchain-manifest-contract.elf"
                )
                _link_contract(
                    seed,
                    runner,
                    (start_object, contract_object, runtime_object),
                    executable,
                    windows,
                    timeout,
                )
                request_path = source_root / "toolchain-manifest-request.bin"
                request_path.write_bytes(request)
                try:
                    result = runner.run(
                        executable, ("check", request_path), timeout
                    )
                    require_live_seed_inputs(seed)
                except subprocess.TimeoutExpired as error:
                    raise ToolchainManifestContractError(
                        "Toolchain manifest contract timed out after "
                        f"{timeout} seconds"
                    ) from error
                except (BootstrapError, OSError) as error:
                    raise ToolchainManifestContractError(
                        "Toolchain manifest contract could not run: "
                        f"{error}"
                    ) from error
                if result.returncode != 0 or result.stderr:
                    detail = result.stderr.strip() or result.stdout.strip()
                    suffix = f": {detail}" if detail else ""
                    raise ToolchainManifestContractError(
                        "Cupid-built Toolchain manifest contract failed"
                        f" with status {result.returncode}{suffix}"
                    )
                try:
                    report = json.loads(result.stdout)
                except (TypeError, json.JSONDecodeError) as error:
                    raise ToolchainManifestContractError(
                        "Cupid-built Toolchain manifest contract returned "
                        "invalid JSON"
                    ) from error
                report = _validate_contract_report(report)
                canonical = json.dumps(
                    report, separators=(",", ":"), sort_keys=True
                ) + "\n"
                if result.stdout != canonical:
                    raise ToolchainManifestContractError(
                        "Cupid-built Toolchain manifest contract report is "
                        "not canonical"
                    )
                _require_payloads_unchanged(
                    reader,
                    (*build_snapshots, *seed_snapshots),
                    "contract trust input",
                )
                _require_seed_directory_unchanged(
                    reader, seed_membership, seed_suffix
                )
                reader.require_unchanged()
                return report
    except ToolchainManifestContractError:
        raise
    except artifact_size_policy.SizePolicyError as error:
        raise ToolchainManifestContractError(
            "contract build input changed while the Cupid contract ran: "
            f"{error}"
        ) from error
    except (BootstrapError, OSError) as error:
        raise ToolchainManifestContractError(
            f"Toolchain manifest contract build failed: {error}"
        ) from error


def verify_with_contract(
    root_argument: Path,
    output_argument: Path,
    execution_manifest_argument: Path,
    *,
    timeout: int = DEFAULT_TIMEOUT,
) -> dict[str, object]:
    root = Path(os.path.abspath(root_argument))
    try:
        logical_output, output = _resolve_repository_path(
            root, output_argument, "Toolchain publication"
        )
        logical_execution, _execution_manifest = _resolve_repository_path(
            root, execution_manifest_argument, "execution seed manifest"
        )
        with artifact_size_policy._PinnedRepository(root) as reader:
            root_status, _root_names = reader.directory_snapshot("")
            (
                manifest_bytes,
                artifact_observations,
                names,
                directory_status,
                publication_snapshots,
            ) = _capture_request(reader, logical_output)
            oracle = _verify_captured_publication(publication_snapshots)
            (
                input_observations,
                bootstrap_observations,
                seed_path,
                seed_bytes,
                seed_observations,
                closure_snapshots,
            ) = _capture_live_manifest_closure(reader, root, oracle)
            snapshots = (*publication_snapshots, *closure_snapshots)
            request = _encode_request(
                manifest_bytes,
                artifact_observations,
                input_observations,
                bootstrap_observations,
                seed_path,
                seed_bytes,
                seed_observations,
            )
            cupidc_toolchain_contracts.verify_publication_inputs(root, oracle)
            oracle_report = _expected_report(oracle)
            contract_report = _build_and_run_contract(
                reader, root, logical_execution, request, timeout
            )
            contract_report = _validate_contract_report(contract_report)
            if contract_report != oracle_report:
                raise ToolchainManifestContractError(
                    "Cupid-built Toolchain manifest report differs from the "
                    "independent Python oracle"
                )
            cupidc_toolchain_contracts.verify_publication_inputs(root, oracle)
            _require_live_closure_membership(
                reader,
                input_observations,
                bootstrap_observations,
                seed_bytes,
            )
            _require_publication_unchanged(
                reader,
                logical_output,
                output,
                names,
                directory_status,
                snapshots,
            )
            _require_root_unchanged(root, root_status)
            return contract_report
    except ToolchainManifestContractError:
        raise
    except (
        artifact_size_policy.SizePolicyError,
        BootstrapError,
        cupidc_toolchain_contracts.ContractError,
        OSError,
        UnicodeError,
    ) as error:
        raise ToolchainManifestContractError(str(error)) from error


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--root", required=True, type=Path)
    verify.add_argument("--output", required=True, type=Path)
    verify.add_argument("--execution-manifest", required=True, type=Path)
    verify.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        report = verify_with_contract(
            args.root,
            args.output,
            args.execution_manifest,
            timeout=args.timeout,
        )
    except ToolchainManifestContractError as error:
        sys.stderr.write(
            f"Toolchain manifest verification failed: {error}\n"
        )
        return 1
    sys.stdout.write(
        "Cupid Toolchain manifest: ok "
        f"({report['artifact_count']} artifacts)\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

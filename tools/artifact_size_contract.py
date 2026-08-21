#!/usr/bin/env python3
"""Run the artifact-size policy through a checked CupidC contract."""

from __future__ import annotations

import argparse
import json
import os
import stat
import struct
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Sequence

try:
    from tools import artifact_size_policy
    from tools.bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_WINDOWS_TARGET,
        WINDOWS_SEED_SCHEMA,
        WINDOWS_TOOL_IMPORTS,
        ToolRunner,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        _validate_static_i386_pe32,
        freeze_seed_inputs,
        require_live_seed_inputs,
        verify_seed_inputs,
    )
except ModuleNotFoundError:
    import artifact_size_policy
    from bootstrap_toolchain import (
        BootstrapError,
        EXPECTED_WINDOWS_TARGET,
        WINDOWS_SEED_SCHEMA,
        WINDOWS_TOOL_IMPORTS,
        ToolRunner,
        _validate_i386_relocatable,
        _validate_static_i386_elf,
        _validate_static_i386_pe32,
        freeze_seed_inputs,
        require_live_seed_inputs,
        verify_seed_inputs,
    )


REPORT_SCHEMA = "cupid.artifact-size-verification.v1"
REQUEST_MAGIC = b"CUPSIZE1"
REGULAR_FILE = 1
LINUX_ENTRY = 0x08048000
WINDOWS_ENTRY = int(EXPECTED_WINDOWS_TARGET["entry"])
DEFAULT_TIMEOUT = 900
WINDOWS_CHECKED_MANIFEST = "bootstrap/seeds/i386-windows/manifest.json"
WINDOWS_CHECKED_FILES = (
    "cupidasm.exe",
    "cupidc.exe",
    "cupiddis.exe",
    "cupidld.exe",
    "cupidobj.exe",
)
LINUX_EXECUTION_FILES = (
    "cupidasm.elf",
    "cupidc.elf",
    "cupiddis.elf",
    "cupidld.elf",
    "cupidobj.elf",
)

BUILD_INPUTS = (
    "Makefile",
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
    "tools/artifact_size_contract.py",
    "tools/artifact_size_policy.py",
    "tools/bootstrap_toolchain.py",
)


class ArtifactSizeContractError(RuntimeError):
    """A checked artifact-size contract could not prove the policy."""


def _pack_bytes(value: bytes) -> bytes:
    if len(value) > 0xFFFFFFFF:
        raise ArtifactSizeContractError("contract request field is too large")
    return struct.pack("<I", len(value)) + value


def _encode_request(
    policy_bytes: bytes,
    manifest_logical: str,
    manifest_bytes: bytes,
    observations: Sequence[tuple[str, int, int]],
) -> bytes:
    if len(observations) > 0xFFFFFFFF:
        raise ArtifactSizeContractError("contract observation count is too large")
    output = bytearray(REQUEST_MAGIC)
    output.extend(_pack_bytes(policy_bytes))
    output.extend(_pack_bytes(manifest_logical.encode("utf-8")))
    output.extend(_pack_bytes(manifest_bytes))
    output.extend(struct.pack("<I", len(observations)))
    for logical, kind, size in observations:
        if kind < 0 or kind > 0xFFFFFFFF or size < 0 or size > 0xFFFFFFFFFFFFFFFF:
            raise ArtifactSizeContractError("contract observation is out of range")
        output.extend(_pack_bytes(logical.encode("utf-8")))
        output.extend(struct.pack("<IQ", kind, size))
    return bytes(output)


def _resolve_repository_file(root: Path, argument: Path, label: str) -> tuple[str, Path]:
    logical = artifact_size_policy._repository_argument(root, argument, label)
    return logical, root.joinpath(*PurePosixPath(logical).parts)


def _require_root_unchanged(root: Path, expected: os.stat_result) -> None:
    try:
        current = root.lstat()
    except OSError as error:
        raise ArtifactSizeContractError(
            "repository root changed while the Cupid contract ran"
        ) from error
    if (
        artifact_size_policy._is_link_or_reparse(current)
        or not stat.S_ISDIR(current.st_mode)
        or artifact_size_policy._stable_file_fields(current)
        != artifact_size_policy._stable_file_fields(expected)
    ):
        raise ArtifactSizeContractError(
            "repository root changed while the Cupid contract ran"
        )


def _expected_report(entries: Sequence[dict[str, object]]) -> dict[str, object]:
    return {
        "artifact_count": len(entries),
        "schema": REPORT_SCHEMA,
        "total_exact_bytes": sum(
            int(entry["exact_bytes"]) for entry in entries
        ),
    }


def _validate_contract_report(report: object) -> dict[str, object]:
    if not isinstance(report, dict):
        raise ArtifactSizeContractError(
            "Cupid-built artifact-size contract returned a non-object"
        )
    if set(report) != {"artifact_count", "schema", "total_exact_bytes"}:
        raise ArtifactSizeContractError(
            "Cupid-built artifact-size contract returned unexpected fields"
        )
    if report["schema"] != REPORT_SCHEMA:
        raise ArtifactSizeContractError(
            "Cupid-built artifact-size contract returned the wrong schema"
        )
    for field in ("artifact_count", "total_exact_bytes"):
        value = report[field]
        if not isinstance(value, int) or isinstance(value, bool):
            raise ArtifactSizeContractError(
                "Cupid-built artifact-size contract returned invalid field types"
            )
    return report


def _capture_verification_request(
    reader: artifact_size_policy._PinnedRepository,
    logical_policy: str,
    logical_manifest: str,
) -> tuple[bytes, dict[str, object]]:
    policy_bytes = artifact_size_policy._required_capture(
        reader, logical_policy, "policy file"
    )
    manifest_bytes = artifact_size_policy._required_capture(
        reader, logical_manifest, "seed manifest"
    )
    seed_owners, seed_sizes = artifact_size_policy._decode_seed_manifest(
        manifest_bytes, logical_manifest
    )
    expected_owners = dict(artifact_size_policy.FIXED_ARTIFACT_OWNERS)
    expected_owners.update(seed_owners)
    if len(expected_owners) != artifact_size_policy.ARTIFACT_COUNT:
        raise artifact_size_policy.SizePolicyError(
            "selected seed artifacts overlap the fixed output cohort"
        )
    entries = artifact_size_policy._decode_policy(
        policy_bytes, expected_owners, seed_sizes
    )
    observations: list[tuple[str, int, int]] = []
    failures: list[str] = []
    for entry in entries:
        logical = str(entry["path"])
        capture, issue = reader.capture(logical, read_payload=False)
        if issue is not None or capture is None:
            failures.append(issue or f"cannot inspect {logical}")
            continue
        observations.append((logical, REGULAR_FILE, capture.status.st_size))
        expected = int(entry["exact_bytes"])
        if capture.status.st_size != expected:
            failures.append(
                f"{logical} has {capture.status.st_size} "
                f"{artifact_size_policy._plural_bytes(capture.status.st_size)}; "
                f"expected exactly {expected} "
                f"{artifact_size_policy._plural_bytes(expected)}"
            )
    if failures:
        raise artifact_size_policy.SizePolicyError(
            "\n- " + "\n- ".join(failures)
        )
    request = _encode_request(
        policy_bytes,
        logical_manifest,
        manifest_bytes,
        observations,
    )
    return request, _expected_report(entries)


def _capture_seed(
    reader: artifact_size_policy._PinnedRepository,
    logical_manifest: str,
    file_names: Sequence[str],
    expected_schema: str,
    label: str,
) -> tuple[tuple[str, bytes], ...]:
    directory = str(PurePosixPath(logical_manifest).parent)
    _status, names = reader.directory_snapshot(directory)
    expected_names = ("manifest.json", *file_names)
    if names != tuple(sorted(expected_names)):
        raise ArtifactSizeContractError(
            f"{label} directory inventory differs"
        )
    logical_paths = (
        logical_manifest,
        *tuple(f"{directory}/{name}" for name in file_names),
    )
    captures = tuple(
        (
            logical,
            artifact_size_policy._required_capture(
                reader, logical, f"{label} input"
            ),
        )
        for logical in logical_paths
    )
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupid-checked-execution-seed-"
        ) as temporary:
            private = Path(temporary)
            for logical, payload in captures:
                target = private / PurePosixPath(logical).name
                target.write_bytes(payload)
            seed = verify_seed_inputs(private / "manifest.json")
            if seed.manifest.get("schema") != expected_schema:
                raise ArtifactSizeContractError(
                    f"{label} schema differs"
                )
    except ArtifactSizeContractError:
        raise
    except (BootstrapError, OSError) as error:
        raise ArtifactSizeContractError(
            f"{label} could not be verified: {error}"
        ) from error
    return captures


def _capture_checked_seed(
    reader: artifact_size_policy._PinnedRepository,
    logical_manifest: str,
) -> tuple[tuple[str, bytes], ...]:
    if logical_manifest != WINDOWS_CHECKED_MANIFEST:
        raise ArtifactSizeContractError(
            "checked seed manifest is not the production Windows manifest"
        )
    return _capture_seed(
        reader,
        logical_manifest,
        WINDOWS_CHECKED_FILES,
        WINDOWS_SEED_SCHEMA,
        "checked Windows seed",
    )


def _capture_execution_seed(
    reader: artifact_size_policy._PinnedRepository,
    logical_manifest: str,
) -> tuple[tuple[str, bytes], ...]:
    if os.name == "nt":
        return _capture_seed(
            reader,
            logical_manifest,
            WINDOWS_CHECKED_FILES,
            WINDOWS_SEED_SCHEMA,
            "Windows execution seed",
        )
    return _capture_seed(
        reader,
        logical_manifest,
        LINUX_EXECUTION_FILES,
        artifact_size_policy.SEED_SCHEMA,
        "Linux execution seed",
    )


def _select_execution_seed(
    reader: artifact_size_policy._PinnedRepository,
    logical_checked_manifest: str,
    checked_seed: Sequence[tuple[str, bytes]],
    logical_execution_manifest: str,
    *,
    windows: bool,
) -> Sequence[tuple[str, bytes]]:
    if windows:
        if logical_execution_manifest != logical_checked_manifest:
            raise ArtifactSizeContractError(
                "Windows execution seed is not the checked Windows seed"
            )
        return checked_seed
    return _capture_execution_seed(reader, logical_execution_manifest)


def _require_captures_unchanged(
    reader: artifact_size_policy._PinnedRepository,
    captures: Sequence[tuple[str, bytes]],
    label: str,
) -> None:
    for logical, expected in captures:
        actual = artifact_size_policy._required_capture(
            reader, logical, f"{label} input"
        )
        if actual != expected:
            raise ArtifactSizeContractError(
                f"{label} changed while the Cupid contract ran"
            )


def _materialize_seed(
    captures: Sequence[tuple[str, bytes]], destination: Path
) -> Path:
    destination.mkdir()
    manifest: Path | None = None
    for logical, payload in captures:
        target = destination / PurePosixPath(logical).name
        target.write_bytes(payload)
        if PurePosixPath(logical).name == "manifest.json":
            manifest = target
    if manifest is None:
        raise ArtifactSizeContractError(
            "captured execution seed omits its manifest"
        )
    return manifest


def _capture_build_inputs(
    reader: artifact_size_policy._PinnedRepository,
) -> tuple[tuple[str, bytes], ...]:
    return tuple(
        (
            logical,
            artifact_size_policy._required_capture(
                reader, logical, "contract build input"
            ),
        )
        for logical in BUILD_INPUTS
    )


def _materialize_build_inputs(
    captures: Sequence[tuple[str, bytes]], source_root: Path
) -> None:
    for logical, payload in captures:
        target = source_root.joinpath(*PurePosixPath(logical).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)


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
        raise ArtifactSizeContractError(
            f"execution seed omits {tool_name}"
        ) from error
    except subprocess.TimeoutExpired as error:
        raise ArtifactSizeContractError(
            f"{label} timed out after {timeout} seconds"
        ) from error
    except (BootstrapError, OSError) as error:
        raise ArtifactSizeContractError(f"{label} could not run: {error}") from error
    if result.returncode != 0 or result.stdout or result.stderr:
        detail = result.stderr.strip() or result.stdout.strip()
        suffix = f": {detail}" if detail else ""
        raise ArtifactSizeContractError(
            f"{label} failed with status {result.returncode}{suffix}"
        )


def _logical_output(source_root: Path, output: Path) -> str:
    try:
        return "/" + output.relative_to(source_root).as_posix()
    except ValueError as error:
        raise ArtifactSizeContractError(
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
        raise ArtifactSizeContractError(
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
        "CupidLD for the artifact-size contract",
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
        raise ArtifactSizeContractError(
            "CupidLD produced an invalid artifact-size contract"
        ) from error


def _build_and_run_contract(
    request: bytes,
    timeout: int,
    build_inputs: Sequence[tuple[str, bytes]],
    execution_seed: Sequence[tuple[str, bytes]],
) -> dict[str, object]:
    if timeout <= 0:
        raise ArtifactSizeContractError("contract timeout must be positive")
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupid-artifact-size-contract-"
        ) as temporary:
            private = Path(temporary)
            source_root = private / "source"
            source_root.mkdir()
            _materialize_build_inputs(build_inputs, source_root)
            captured_manifest = _materialize_seed(
                execution_seed, private / "execution-seed"
            )
            seed = freeze_seed_inputs(captured_manifest, private / "seed")
            windows = os.name == "nt"
            schema = seed.manifest.get("schema")
            if windows and schema != WINDOWS_SEED_SCHEMA:
                raise ArtifactSizeContractError(
                    "Windows requires the checked native execution seed"
                )
            if not windows and schema != artifact_size_policy.SEED_SCHEMA:
                raise ArtifactSizeContractError(
                    "Linux requires the checked bootstrap seed"
                )
            build_root = source_root / "build/artifact-size-contract"
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
                "toolchain/tests/artifact_size_policy_contract.cc",
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
                "CupidASM for the artifact-size contract startup",
                timeout,
            )
            _validate_i386_relocatable(start_object)
            executable = build_root / (
                "artifact-size-policy-contract.exe"
                if windows
                else "artifact-size-policy-contract.elf"
            )
            _link_contract(
                seed,
                runner,
                (start_object, contract_object, runtime_object),
                executable,
                windows,
                timeout,
            )
            request_path = source_root / "artifact-size-request.bin"
            request_path.write_bytes(request)
            try:
                result = runner.run(
                    executable,
                    ("check", request_path),
                    timeout,
                )
                require_live_seed_inputs(seed)
            except subprocess.TimeoutExpired as error:
                raise ArtifactSizeContractError(
                    f"artifact-size contract timed out after {timeout} seconds"
                ) from error
            except (BootstrapError, OSError) as error:
                raise ArtifactSizeContractError(
                    f"artifact-size contract could not run: {error}"
                ) from error
            if result.returncode != 0 or result.stderr:
                detail = result.stderr.strip() or result.stdout.strip()
                suffix = f": {detail}" if detail else ""
                raise ArtifactSizeContractError(
                    "Cupid-built artifact-size contract failed"
                    f" with status {result.returncode}{suffix}"
                )
            try:
                report = json.loads(result.stdout)
            except (TypeError, json.JSONDecodeError) as error:
                raise ArtifactSizeContractError(
                    "Cupid-built artifact-size contract returned invalid JSON"
                ) from error
            report = _validate_contract_report(report)
            canonical = json.dumps(
                report, separators=(",", ":"), sort_keys=True
            ) + "\n"
            if result.stdout != canonical:
                raise ArtifactSizeContractError(
                    "Cupid-built artifact-size contract report is not canonical"
                )
            return report
    except ArtifactSizeContractError:
        raise
    except artifact_size_policy.SizePolicyError as error:
        raise ArtifactSizeContractError(
            f"contract build input changed while the Cupid contract ran: {error}"
        ) from error
    except (BootstrapError, OSError) as error:
        raise ArtifactSizeContractError(
            f"artifact-size contract build failed: {error}"
        ) from error


def verify_with_contract(
    root_argument: Path,
    policy_argument: Path,
    seed_manifest_argument: Path,
    checked_manifest_argument: Path,
    execution_manifest_argument: Path,
    *,
    timeout: int = DEFAULT_TIMEOUT,
) -> dict[str, object]:
    root = Path(os.path.abspath(root_argument))
    try:
        root_info = root.lstat()
    except OSError as error:
        raise ArtifactSizeContractError(
            f"cannot inspect repository root: {error}"
        ) from error
    if artifact_size_policy._is_link_or_reparse(root_info) or not stat.S_ISDIR(
        root_info.st_mode
    ):
        raise ArtifactSizeContractError(
            "repository root is not a regular directory"
        )
    logical_policy, _ = _resolve_repository_file(
        root, policy_argument, "policy"
    )
    logical_manifest, _ = _resolve_repository_file(
        root, seed_manifest_argument, "seed manifest"
    )
    logical_checked_manifest, _ = _resolve_repository_file(
        root, checked_manifest_argument, "checked seed manifest"
    )
    logical_execution_manifest, _ = _resolve_repository_file(
        root, execution_manifest_argument, "execution seed manifest"
    )
    try:
        with artifact_size_policy._PinnedRepository(root) as reader:
            pinned_root, _ = reader.directory_snapshot("")
            if artifact_size_policy._stable_file_fields(
                pinned_root
            ) != artifact_size_policy._stable_file_fields(root_info):
                raise ArtifactSizeContractError(
                    "repository root changed before it could be pinned"
                )
            request, oracle_report = _capture_verification_request(
                reader, logical_policy, logical_manifest
            )
            checked_seed = _capture_checked_seed(
                reader, logical_checked_manifest
            )
            build_inputs = _capture_build_inputs(reader)
            execution_seed = _select_execution_seed(
                reader,
                logical_checked_manifest,
                checked_seed,
                logical_execution_manifest,
                windows=os.name == "nt",
            )
            contract_report = _build_and_run_contract(
                request, timeout, build_inputs, execution_seed
            )
            contract_report = _validate_contract_report(contract_report)
            if contract_report != oracle_report:
                raise ArtifactSizeContractError(
                    "Cupid-built artifact-size report differs from the "
                    "independent Python oracle"
                )
            _require_captures_unchanged(
                reader, checked_seed, "checked Windows seed"
            )
            _require_captures_unchanged(
                reader, build_inputs, "contract build input"
            )
            if execution_seed is not checked_seed:
                _require_captures_unchanged(
                    reader, execution_seed, "execution seed"
                )
            try:
                reader.require_unchanged()
            except artifact_size_policy.SizePolicyError as error:
                raise ArtifactSizeContractError(
                    f"artifact changed while the Cupid contract ran: {error}"
                ) from error
            _require_root_unchanged(root, root_info)
            return contract_report
    except ArtifactSizeContractError:
        raise
    except artifact_size_policy.SizePolicyError as error:
        raise ArtifactSizeContractError(str(error)) from error
    except OSError as error:
        raise ArtifactSizeContractError(
            f"cannot inspect repository root: {error}"
        ) from error


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--root", required=True, type=Path)
    verify.add_argument("--policy", required=True, type=Path)
    verify.add_argument("--seed-manifest", required=True, type=Path)
    verify.add_argument("--checked-manifest", required=True, type=Path)
    verify.add_argument("--execution-manifest", required=True, type=Path)
    verify.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        report = verify_with_contract(
            args.root,
            args.policy,
            args.seed_manifest,
            args.checked_manifest,
            args.execution_manifest,
            timeout=args.timeout,
        )
    except (ArtifactSizeContractError, artifact_size_policy.SizePolicyError) as error:
        message = str(error)
        if message.startswith("\n-"):
            sys.stderr.write("artifact size verification failed:" + message + "\n")
        else:
            sys.stderr.write(f"artifact size verification failed: {message}\n")
        return 1
    sys.stdout.write(
        "Cupid artifact sizes: ok "
        f"({report['artifact_count']} exact artifacts)\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Capture reproducible host-toolchain evidence for Cupid OS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Callable, Iterable


class BaselineError(RuntimeError):
    """A baseline could not be captured or verified."""


@dataclass(frozen=True)
class CheckSpec:
    """One named command in the baseline workflow."""

    name: str
    command: tuple[str, ...]


@dataclass(frozen=True)
class CommandResult:
    """Observable result returned by a command adapter."""

    returncode: int
    elapsed_seconds: float
    output: str


@dataclass(frozen=True)
class ToolSpec:
    """How one required host tool is selected and queried."""

    default: str
    environment: str
    version_args: tuple[str, ...]


@dataclass(frozen=True)
class BuildRootSpec:
    """One supported Make root and its evidence-facing target names."""

    name: str
    path: str
    clean_target: str

    def check_name(self, action: str) -> str:
        return action if self.name == "root" else f"{self.name}-{action}"

    def directory_arguments(self) -> tuple[str, ...]:
        return ("-C", self.path) if self.path else ()


SCHEMA = "cupid.bootstrap-baseline.v2"
COMPARISON_SCHEMA = "cupid.bootstrap-host-comparison.v1"
BUILD_ENVIRONMENT = ("CC_TARGET", "EXTRA_CFLAGS")
SUPPORTED_BUILD_ROOTS = (
    BuildRootSpec("root", "", "distclean"),
    BuildRootSpec("user", "user", "clean"),
    BuildRootSpec("toolchain", "toolchain", "clean"),
)
SUPPORTED_ROOT_NAMES = tuple(root.name for root in SUPPORTED_BUILD_ROOTS)
SUPPORTED_ROOT_ACTIONS = ("clean", "build", "artifact-list")
SUPPORTED_ROOT_CHECKS = tuple(
    root.check_name(action)
    for root in SUPPORTED_BUILD_ROOTS
    for action in SUPPORTED_ROOT_ACTIONS
)
VALIDATION_CHECKS = (
    "host-unit-tests",
    "cupidc-gui-smoke",
    "cupidasm-gui-smoke",
)
REQUIRED_BASELINE_CHECKS = SUPPORTED_ROOT_CHECKS + VALIDATION_CHECKS
REQUIRED_HOST_TOOLS = (
    "git",
    "make",
    "python",
    "c_compiler",
    "symbol_reader",
    "qemu",
)
QUALITY_METRICS = (
    "kernel_text_bytes",
    "kernel_elf_bytes",
    "kernel_binary_bytes",
    "disk_image_bytes",
)
REQUIRED_ROOT_ARTIFACTS = (
    "cupidos.img",
    "kernel/kernel.elf",
    "kernel/kernel.bin",
)


def host_metadata(os_release: Path = Path("/etc/os-release")) -> dict[str, object]:
    """Describe the host and, on Linux, its user-space distribution."""
    metadata: dict[str, object] = {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python_version": platform.python_version(),
    }
    if metadata["system"] == "Linux" and os_release.is_file():
        values: dict[str, str] = {}
        for line in os_release.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#") or "=" not in line:
                continue
            name, value = line.split("=", 1)
            values[name] = value.strip().strip('"')
        metadata["distribution"] = {
            "id": values.get("ID", ""),
            "version_id": values.get("VERSION_ID", ""),
            "pretty_name": values.get("PRETTY_NAME", ""),
        }
        if "WSL_DISTRO_NAME" in os.environ:
            metadata["wsl_distribution"] = os.environ["WSL_DISTRO_NAME"]
    return metadata


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _artifact_digest(files: Iterable[dict[str, object]]) -> str:
    aggregate = hashlib.sha256()
    for entry in files:
        aggregate.update(
            f"{entry['path']}\0{entry['size']}\0{entry['sha256']}\n".encode("utf-8")
        )
    return aggregate.hexdigest()


def hash_artifacts(root: Path, paths: Iterable[str]) -> dict[str, object]:
    """Return a stable, content-addressed manifest for relative artifact paths."""
    root = root.resolve()
    files: list[dict[str, object]] = []
    for relative in sorted(set(paths)):
        path = (root / relative).resolve()
        try:
            normalized = path.relative_to(root).as_posix()
        except ValueError as exc:
            raise BaselineError(f"artifact escapes baseline root: {relative}") from exc
        if not path.is_file():
            raise BaselineError(f"missing baseline artifact: {normalized}")
        files.append(
            {
                "path": normalized,
                "size": path.stat().st_size,
                "sha256": _sha256_file(path),
            }
        )

    return {"digest": _artifact_digest(files), "files": files}


def _logical_artifact_id(root_name: str, relative_path: str) -> str:
    logical_path = relative_path
    if root_name == "toolchain" and logical_path.lower().endswith(".exe"):
        logical_path = logical_path[:-4]
    return f"{root_name}:{logical_path}"


def _validate_artifact_manifest(
    label: str, manifest: object
) -> tuple[list[str], set[str]]:
    failures: list[str] = []
    if not isinstance(manifest, dict):
        return [f"{label} artifact manifest is not an object"], set()
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        return [f"{label} artifact manifest has no files"], set()
    paths: list[str] = []
    valid_entries: list[dict[str, object]] = []
    for index, entry in enumerate(files):
        if not isinstance(entry, dict):
            failures.append(f"{label} artifact {index} is not an object")
            continue
        path = entry.get("path")
        size = entry.get("size")
        sha256 = entry.get("sha256")
        if not isinstance(path, str) or not path:
            failures.append(f"{label} artifact {index} has an invalid path")
            continue
        logical_path = PurePosixPath(path)
        if (
            logical_path.is_absolute()
            or path == "."
            or ".." in logical_path.parts
            or logical_path.as_posix() != path
            or "\0" in path
        ):
            failures.append(f"{label} artifact {index} has an unsafe path")
            continue
        if not isinstance(size, int) or isinstance(size, bool) or size < 0:
            failures.append(f"{label} artifact {path} has an invalid size")
            continue
        if not isinstance(sha256, str) or re.fullmatch(r"[0-9a-f]{64}", sha256) is None:
            failures.append(f"{label} artifact {path} has an invalid SHA-256")
            continue
        paths.append(path)
        valid_entries.append(entry)
    if len(set(paths)) != len(paths):
        failures.append(f"{label} artifact paths are not unique")
    if paths != sorted(paths):
        failures.append(f"{label} artifact paths are not sorted")
    digest = manifest.get("digest")
    if (
        len(valid_entries) != len(files)
        or not isinstance(digest, str)
        or digest != _artifact_digest(valid_entries)
    ):
        failures.append(f"{label} artifact digest is invalid")
    return failures, set(paths)


def compare_artifact_manifests(
    first: dict[str, object], second: dict[str, object]
) -> dict[str, object]:
    """Compare two build manifests and localize every differing artifact."""
    first_files = {
        str(entry["path"]): {
            "size": entry["size"],
            "sha256": entry["sha256"],
        }
        for entry in first["files"]
    }
    second_files = {
        str(entry["path"]): {
            "size": entry["size"],
            "sha256": entry["sha256"],
        }
        for entry in second["files"]
    }
    mismatches = []
    for path in sorted(set(first_files) | set(second_files)):
        first_entry = first_files.get(path)
        second_entry = second_files.get(path)
        if first_entry != second_entry:
            mismatches.append(
                {"path": path, "first": first_entry, "second": second_entry}
            )
    return {"matched": not mismatches, "mismatches": mismatches}


def _build_artifact_evidence(
    label: str, build: dict[str, object]
) -> tuple[
    list[str],
    dict[str, object] | None,
    set[str],
    set[str],
]:
    failures, combined_paths = _validate_artifact_manifest(
        f"{label} combined", build.get("artifacts")
    )
    combined = build.get("artifacts")
    valid_combined = combined if not failures and isinstance(combined, dict) else None

    roots = build.get("supported_roots")
    if not isinstance(roots, dict) or set(roots) != set(SUPPORTED_ROOT_NAMES):
        failures.append(f"{label} does not cover all supported roots")
        roots = {}
    expected_physical_paths: set[str] = set()
    expected_cohort: dict[str, str] = {}
    expected_combined_entries: dict[str, tuple[int, str]] = {}
    for root in SUPPORTED_BUILD_ROOTS:
        evidence = roots.get(root.name)
        if not isinstance(evidence, dict):
            failures.append(f"{label} has no {root.name} root evidence")
            continue
        expected_root_path = root.path or "."
        if evidence.get("path") != expected_root_path:
            failures.append(f"{label} {root.name} root path is invalid")
        root_manifest = evidence.get("artifacts")
        root_failures, root_paths = _validate_artifact_manifest(
            f"{label} {root.name}", root_manifest
        )
        failures.extend(root_failures)
        if root.name == "root" and not set(REQUIRED_ROOT_ARTIFACTS).issubset(
            root_paths
        ):
            failures.append(f"{label} root manifest lacks required boundaries")
        artifact_order = evidence.get("artifact_order")
        if (
            not isinstance(artifact_order, list)
            or any(not isinstance(path, str) for path in artifact_order)
            or len(set(artifact_order)) != len(artifact_order)
            or set(artifact_order) != root_paths
        ):
            failures.append(f"{label} {root.name} artifact order is invalid")
        for relative_path in root_paths:
            physical_path = (
                f"{root.path}/{relative_path}" if root.path else relative_path
            )
            expected_physical_paths.add(physical_path)
            expected_cohort[physical_path] = _logical_artifact_id(
                root.name, relative_path
            )
        if isinstance(root_manifest, dict):
            root_files = root_manifest.get("files")
            if isinstance(root_files, list):
                for entry in root_files:
                    if isinstance(entry, dict) and isinstance(entry.get("path"), str):
                        physical_path = (
                            f"{root.path}/{entry['path']}"
                            if root.path
                            else str(entry["path"])
                        )
                        size = entry.get("size")
                        sha256 = entry.get("sha256")
                        if isinstance(size, int) and isinstance(sha256, str):
                            expected_combined_entries[physical_path] = (size, sha256)
    if combined_paths != expected_physical_paths:
        failures.append(f"{label} combined manifest differs from its root manifests")
    combined_entries: dict[str, tuple[int, str]] = {}
    if isinstance(combined, dict):
        combined_files = combined.get("files")
        if isinstance(combined_files, list):
            for entry in combined_files:
                if isinstance(entry, dict) and isinstance(entry.get("path"), str):
                    size = entry.get("size")
                    sha256 = entry.get("sha256")
                    if isinstance(size, int) and isinstance(sha256, str):
                        combined_entries[str(entry["path"])] = (size, sha256)
    if combined_entries != expected_combined_entries:
        failures.append(f"{label} combined bytes differ from its root manifests")

    cohort = build.get("artifact_cohort")
    cohort_paths: set[str] = set()
    cohort_ids: set[str] = set()
    if not isinstance(cohort, list) or not cohort:
        failures.append(f"{label} has no logical artifact cohort")
    else:
        for index, entry in enumerate(cohort):
            if not isinstance(entry, dict):
                failures.append(f"{label} cohort entry {index} is not an object")
                continue
            artifact_id = entry.get("id")
            path = entry.get("path")
            if not isinstance(artifact_id, str) or not artifact_id:
                failures.append(f"{label} cohort entry {index} has an invalid id")
                continue
            if not isinstance(path, str) or not path:
                failures.append(f"{label} cohort entry {index} has an invalid path")
                continue
            cohort_ids.add(artifact_id)
            cohort_paths.add(path)
            if expected_cohort.get(path) != artifact_id:
                failures.append(f"{label} cohort entry {artifact_id} is inconsistent")
        if len(cohort_ids) != len(cohort) or len(cohort_paths) != len(cohort):
            failures.append(f"{label} logical artifact cohort is not unique")
        if cohort_paths != combined_paths:
            failures.append(f"{label} logical cohort paths differ from its manifest")
    return failures, valid_combined, cohort_ids, combined_paths


def _baseline_gate_failures(label: str, baseline: dict[str, object]) -> list[str]:
    failures: list[str] = []
    if baseline.get("schema") != SCHEMA:
        failures.append(f"{label} baseline has an unsupported schema")
    if baseline.get("status") != "pass":
        failures.append(f"{label} baseline status is not pass")
    source = baseline.get("source")
    revision = source.get("revision") if isinstance(source, dict) else None
    if not isinstance(revision, str) or re.fullmatch(r"[0-9a-f]{40}", revision) is None:
        failures.append(f"{label} baseline source revision is invalid")
    tools = baseline.get("tools")
    tool_evidence = tools if isinstance(tools, dict) else {}
    for name in REQUIRED_HOST_TOOLS:
        evidence = tool_evidence.get(name)
        if not isinstance(evidence, dict) or evidence.get("status") != "pass":
            failures.append(f"{label} required tool did not pass: {name}")
            continue
        command = evidence.get("command")
        executable = evidence.get("executable")
        executable_sha256 = evidence.get("executable_sha256")
        version = evidence.get("version")
        if (
            not isinstance(command, list)
            or not command
            or any(not isinstance(argument, str) or not argument for argument in command)
            or not isinstance(executable, str)
            or not executable
            or not isinstance(executable_sha256, str)
            or re.fullmatch(r"[0-9a-f]{64}", executable_sha256) is None
            or not isinstance(version, str)
            or not version
            or evidence.get("version_returncode") != 0
        ):
            failures.append(f"{label} required tool fingerprint is invalid: {name}")
    compiler = tool_evidence.get("c_compiler")
    capabilities = compiler.get("capabilities") if isinstance(compiler, dict) else None
    freestanding = (
        capabilities.get("freestanding_i386")
        if isinstance(capabilities, dict)
        else None
    )
    if (
        not isinstance(freestanding, dict)
        or freestanding.get("status") != "pass"
        or freestanding.get("returncode") != 0
        or freestanding.get("produced_object") is not True
        or freestanding.get("valid_i386_elf32_relocatable") is not True
        or not isinstance(freestanding.get("command"), list)
        or not freestanding.get("command")
    ):
        failures.append(f"{label} freestanding i386 compiler probe did not pass")
    host = baseline.get("host")
    compiler_identity = ""
    if isinstance(compiler, dict):
        compiler_identity = " ".join(
            str(compiler.get(field, ""))
            for field in ("executable", "version")
        ).lower()
    symbol_reader = tool_evidence.get("symbol_reader")
    symbol_identity = ""
    if isinstance(symbol_reader, dict):
        symbol_identity = " ".join(
            str(symbol_reader.get(field, ""))
            for field in ("executable", "version")
        ).lower()
    if label.lower() == "linux":
        if not isinstance(host, dict) or not isinstance(
            host.get("distribution"), dict
        ):
            failures.append("Linux baseline does not record distribution identity")
        if "gcc" not in compiler_identity:
            failures.append("Linux baseline compiler is not GCC")
        if "gnu nm" not in symbol_identity and "gnu binutils" not in symbol_identity:
            failures.append("Linux baseline symbol reader is not GNU binutils nm")
    elif label.lower() == "windows":
        if "clang" not in compiler_identity:
            failures.append("Windows baseline compiler is not Clang")
        if "llvm" not in symbol_identity:
            failures.append("Windows baseline symbol reader is not LLVM nm")
    builds = baseline.get("builds")
    if not isinstance(builds, list) or len(builds) < 2:
        failures.append(f"{label} baseline does not contain two builds")
        return failures
    if any(
        not isinstance(build, dict) or build.get("status") != "pass"
        for build in builds
    ):
        failures.append(f"{label} baseline contains a failed build")
    build_manifests: list[dict[str, object]] = []
    for index, build in enumerate(builds):
        if not isinstance(build, dict):
            continue
        if build.get("run") != index + 1:
            failures.append(f"{label} baseline run number is invalid: {index + 1}")
        checks = build.get("checks")
        check_status = (
            {
                str(check.get("name")): check.get("status")
                for check in checks
                if isinstance(check, dict)
            }
            if isinstance(checks, list)
            else {}
        )
        required_checks = SUPPORTED_ROOT_CHECKS + (
            VALIDATION_CHECKS if index == 0 else ()
        )
        for name in required_checks:
            if check_status.get(name) != "pass":
                failures.append(
                    f"{label} baseline run {index + 1} check did not pass: {name}"
                )
        artifact_failures, manifest, _, _ = _build_artifact_evidence(
            f"{label} baseline run {index + 1}", build
        )
        failures.extend(artifact_failures)
        if manifest is not None:
            build_manifests.append(manifest)
    computed_reproducibility = len(build_manifests) == len(builds) and all(
        compare_artifact_manifests(build_manifests[0], manifest)["matched"]
        for manifest in build_manifests[1:]
    )
    reproducibility = baseline.get("reproducibility")
    recorded_reproducibility = (
        reproducibility.get("matched")
        if isinstance(reproducibility, dict)
        else None
    )
    expected_compared_runs = list(range(2, len(builds) + 1))
    reproducibility_shape_valid = (
        isinstance(reproducibility, dict)
        and reproducibility.get("reference_run") == 1
        and reproducibility.get("compared_runs") == expected_compared_runs
        and reproducibility.get("mismatches") == []
    )
    if (
        recorded_reproducibility is not True
        or not reproducibility_shape_valid
        or not computed_reproducibility
    ):
        failures.append(f"{label} baseline reproducibility failed")
    quality = baseline.get("quality")
    if not isinstance(quality, dict):
        failures.append(f"{label} baseline has no quality metrics")
    else:
        for name in QUALITY_METRICS:
            value = quality.get(name)
            if (
                not isinstance(value, (int, float))
                or isinstance(value, bool)
                or value < 0
            ):
                failures.append(f"{label} baseline quality metric is invalid: {name}")
        if quality.get("disk_image_bytes") != 209715200:
            failures.append(f"{label} baseline disk geometry differs from 200 MiB")
        if build_manifests:
            files = build_manifests[0].get("files")
            artifact_sizes = (
                {
                    str(entry.get("path")): entry.get("size")
                    for entry in files
                    if isinstance(entry, dict)
                }
                if isinstance(files, list)
                else {}
            )
            for metric, path in (
                ("kernel_elf_bytes", "kernel/kernel.elf"),
                ("kernel_binary_bytes", "kernel/kernel.bin"),
                ("disk_image_bytes", "cupidos.img"),
            ):
                if quality.get(metric) != artifact_sizes.get(path):
                    failures.append(
                        f"{label} baseline quality metric disagrees with artifact: {metric}"
                    )
    return failures


def _first_build_artifacts(baseline: dict[str, object]) -> dict[str, object]:
    builds = baseline.get("builds")
    if not isinstance(builds, list) or not builds or not isinstance(builds[0], dict):
        return {"digest": "", "files": []}
    artifacts = builds[0].get("artifacts")
    return artifacts if isinstance(artifacts, dict) else {"digest": "", "files": []}


def _first_build_evidence(
    label: str, baseline: dict[str, object]
) -> tuple[set[str], set[str]]:
    builds = baseline.get("builds")
    if not isinstance(builds, list) or not builds or not isinstance(builds[0], dict):
        return set(), set()
    _, _, cohort_ids, physical_paths = _build_artifact_evidence(
        f"{label} baseline run 1", builds[0]
    )
    return cohort_ids, physical_paths


def compare_host_baselines(
    first: dict[str, object], second: dict[str, object]
) -> dict[str, object]:
    """Gate comparable host evidence without requiring cross-toolchain bytes."""
    first_label = str(
        first.get("host", {}).get("system", "first")
        if isinstance(first.get("host"), dict)
        else "first"
    )
    second_label = str(
        second.get("host", {}).get("system", "second")
        if isinstance(second.get("host"), dict)
        else "second"
    )
    failures = [
        *_baseline_gate_failures(first_label, first),
        *_baseline_gate_failures(second_label, second),
    ]
    if {first_label.lower(), second_label.lower()} != {"windows", "linux"}:
        failures.append("comparison requires one Windows and one Linux baseline")
    first_source = first.get("source")
    second_source = second.get("source")
    first_revision = (
        first_source.get("revision") if isinstance(first_source, dict) else None
    )
    second_revision = (
        second_source.get("revision") if isinstance(second_source, dict) else None
    )
    if not first_revision or first_revision != second_revision:
        failures.append("source revisions differ")

    first_artifacts = _first_build_artifacts(first)
    second_artifacts = _first_build_artifacts(second)
    first_ids, first_physical_paths = _first_build_evidence(first_label, first)
    second_ids, second_physical_paths = _first_build_evidence(second_label, second)
    if not first_ids or first_ids != second_ids:
        failures.append("logical artifact cohorts differ")

    quality_comparison: dict[str, dict[str, object]] = {}
    first_quality = first.get("quality")
    second_quality = second.get("quality")
    if isinstance(first_quality, dict) and isinstance(second_quality, dict):
        for name in QUALITY_METRICS:
            first_value = first_quality.get(name)
            second_value = second_quality.get(name)
            if isinstance(first_value, (int, float)) and isinstance(
                second_value, (int, float)
            ):
                delta = second_value - first_value
                quality_comparison[name] = {
                    "first": first_value,
                    "second": second_value,
                    "delta": delta,
                    "percent_from_first": (
                        round(delta * 100.0 / first_value, 6)
                        if first_value != 0
                        else None
                    ),
                }

    passed = not failures
    return {
        "schema": COMPARISON_SCHEMA,
        "status": "pass" if passed else "fail",
        "gate": {"passed": passed, "failures": failures},
        "source_revision": first_revision if first_revision == second_revision else None,
        "hosts": [first.get("host", {}), second.get("host", {})],
        "artifacts": {
            "logical_artifact_count": (
                len(first_ids) if first_ids == second_ids else None
            ),
            "physical_paths_equal": first_physical_paths == second_physical_paths,
            "first_digest": first_artifacts.get("digest"),
            "second_digest": second_artifacts.get("digest"),
            "digests_equal": first_artifacts.get("digest")
            == second_artifacts.get("digest"),
            "hash_equality_gating": False,
        },
        "quality": quality_comparison,
    }


def elf32_section_size(path: Path, section_name: str) -> int:
    """Read one section size directly from a little-endian ELF32 file."""
    image = path.read_bytes()
    if len(image) < 52 or image[:7] != b"\x7fELF\x01\x01\x01":
        raise BaselineError(f"not a little-endian ELF32 file: {path}")

    section_offset = struct.unpack_from("<I", image, 32)[0]
    section_entry_size = struct.unpack_from("<H", image, 46)[0]
    section_count = struct.unpack_from("<H", image, 48)[0]
    string_index = struct.unpack_from("<H", image, 50)[0]
    table_end = section_offset + section_entry_size * section_count
    if (
        section_entry_size < 40
        or string_index >= section_count
        or table_end > len(image)
    ):
        raise BaselineError(f"invalid ELF32 section table: {path}")

    string_header = section_offset + string_index * section_entry_size
    strings_offset, strings_size = struct.unpack_from("<II", image, string_header + 16)
    strings_end = strings_offset + strings_size
    if strings_end > len(image):
        raise BaselineError(f"invalid ELF32 section-name table: {path}")
    strings = image[strings_offset:strings_end]

    for index in range(section_count):
        header = section_offset + index * section_entry_size
        name_offset = struct.unpack_from("<I", image, header)[0]
        if name_offset >= len(strings):
            continue
        name_end = strings.find(b"\0", name_offset)
        if name_end < 0:
            continue
        name = strings[name_offset:name_end].decode("ascii", errors="replace")
        if name == section_name:
            return struct.unpack_from("<I", image, header + 20)[0]
    raise BaselineError(f"ELF32 section not found: {section_name}")


def run_check_sequence(
    checks: Iterable[CheckSpec], invoke: Callable[[CheckSpec], CommandResult]
) -> list[dict[str, object]]:
    """Run ordered checks, preserving the first failure and every skipped check."""
    results: list[dict[str, object]] = []
    failed_name: str | None = None
    for check in checks:
        if failed_name is not None:
            results.append(
                {
                    "name": check.name,
                    "command": list(check.command),
                    "status": "skipped",
                    "reason": f"blocked by failed check: {failed_name}",
                }
            )
            continue

        command_result = invoke(check)
        status = "pass" if command_result.returncode == 0 else "fail"
        output_lines = command_result.output.rstrip().splitlines()
        results.append(
            {
                "name": check.name,
                "command": list(check.command),
                "status": status,
                "returncode": command_result.returncode,
                "elapsed_seconds": round(command_result.elapsed_seconds, 3),
                "output_sha256": hashlib.sha256(
                    command_result.output.encode("utf-8", errors="replace")
                ).hexdigest(),
                "output_tail": output_lines[-20:],
            }
        )
        if status == "fail":
            failed_name = check.name
    return results


def _split_command(value: str) -> tuple[str, ...]:
    parts = shlex.split(value, posix=os.name != "nt")
    if os.name == "nt":
        parts = [
            part[1:-1]
            if len(part) >= 2 and part[0] == part[-1] and part[0] in {'"', "'"}
            else part
            for part in parts
        ]
    if not parts:
        raise BaselineError("empty tool command")
    return tuple(parts)


def _tool_specs() -> dict[str, ToolSpec]:
    windows = os.name == "nt"
    return {
        "git": ToolSpec("git", "GIT", ("--version",)),
        "make": ToolSpec("make", "MAKE", ("--version",)),
        "python": ToolSpec("python" if windows else "python3", "PYTHON", ("--version",)),
        "c_compiler": ToolSpec("clang" if windows else "gcc", "CC", ("--version",)),
        "symbol_reader": ToolSpec(
            "llvm-nm" if windows else "nm", "NM", ("--version",)
        ),
        "qemu": ToolSpec("qemu-system-i386", "QEMU", ("--version",)),
    }


def _cc_target_arguments() -> tuple[str, ...]:
    value = os.environ.get("CC_TARGET")
    if value is None:
        return ("--target=i386-unknown-elf",) if os.name == "nt" else ()
    if not value.strip():
        return ()
    return _split_command(value)


def _optional_oracle_tool_specs() -> dict[str, ToolSpec]:
    return {
        "nasm": ToolSpec("nasm", "NASM", ("-v",)),
    }


def optional_oracle_commands() -> dict[str, tuple[str, ...]]:
    """Return the exact configured commands used by optional oracle tests."""
    return {
        name: _split_command(os.environ.get(spec.environment, spec.default))
        for name, spec in _optional_oracle_tool_specs().items()
    }


def _tool_commands() -> dict[str, tuple[tuple[str, ...], tuple[str, ...]]]:
    specs = _tool_specs()
    commands = {
        name: _split_command(os.environ.get(spec.environment, spec.default))
        for name, spec in specs.items()
    }
    return {
        name: (command, specs[name].version_args)
        for name, command in commands.items()
    }


def resolve_tool_command(command: tuple[str, ...]) -> tuple[str, ...] | None:
    """Resolve one configured executable while preserving its arguments."""
    executable = shutil.which(command[0])
    if executable is None and Path(command[0]).is_file():
        executable = str(Path(command[0]).resolve())
    if executable is None:
        return None
    return (str(Path(executable).resolve()), *command[1:])


def _probe_tool(
    command: tuple[str, ...], version_args: tuple[str, ...]
) -> dict[str, object]:
    resolved = resolve_tool_command(command)
    if resolved is None:
        return {
            "status": "missing",
            "command": list(command),
            "error": f"executable not found: {command[0]}",
        }
    try:
        completed = subprocess.run(
            [*resolved, *version_args],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as exc:
        return {
            "status": "error",
            "command": list(command),
            "executable": resolved[0],
            "error": str(exc),
        }
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    result: dict[str, object] = {
        "status": "pass" if completed.returncode == 0 else "error",
        "command": list(command),
        "executable": resolved[0],
        "executable_sha256": _sha256_file(Path(resolved[0])),
        "version": lines[0] if lines else "",
        "version_returncode": completed.returncode,
    }
    return result


def _is_i386_elf32_relocatable(path: Path) -> bool:
    try:
        image = path.read_bytes()
    except OSError:
        return False
    return (
        len(image) >= 52
        and image[:7] == b"\x7fELF\x01\x01\x01"
        and struct.unpack_from("<H", image, 16)[0] == 1
        and struct.unpack_from("<H", image, 18)[0] == 3
        and struct.unpack_from("<I", image, 20)[0] == 1
        and struct.unpack_from("<H", image, 40)[0] == 52
    )


def _probe_freestanding_i386(
    command: tuple[str, ...], target_arguments: tuple[str, ...]
) -> dict[str, object]:
    """Prove that the configured compiler can emit a freestanding i386 object."""
    with tempfile.TemporaryDirectory(prefix="cupid-cc-probe-") as directory:
        root = Path(directory)
        source = root / "probe.c"
        output = root / "probe.o"
        source.write_text("void cupid_bootstrap_probe(void) {}\n", encoding="utf-8")
        invocation = (
            *command,
            *target_arguments,
            "-m32",
            "-ffreestanding",
            "-fno-pie",
            "-fno-stack-protector",
            "-nostdlib",
            "-c",
            str(source),
            "-o",
            str(output),
        )
        try:
            completed = subprocess.run(
                list(invocation),
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except OSError as exc:
            return {
                "status": "fail",
                "command": [*command, *target_arguments, "-m32", "<probe>"],
                "error": str(exc),
            }
        output_lines = completed.stdout.rstrip().splitlines()
        produced_object = output.is_file() and output.stat().st_size > 0
        valid_object = produced_object and _is_i386_elf32_relocatable(output)
        status = "pass" if completed.returncode == 0 and valid_object else "fail"
        return {
            "status": status,
            "command": [
                *command,
                *target_arguments,
                "-m32",
                "-ffreestanding",
                "-fno-pie",
                "-fno-stack-protector",
                "-nostdlib",
                "-c",
                "<TEMP>/probe.c",
                "-o",
                "<TEMP>/probe.o",
            ],
            "returncode": completed.returncode,
            "produced_object": produced_object,
            "valid_i386_elf32_relocatable": valid_object,
            "output_tail": output_lines[-20:],
        }


def probe_tools() -> tuple[dict[str, dict[str, object]], dict[str, tuple[str, ...]]]:
    """Resolve and fingerprint every external tool used by the oracle baseline."""
    definitions = _tool_commands()
    evidence = {
        name: _probe_tool(command, version_args)
        for name, (command, version_args) in definitions.items()
    }
    compiler = evidence["c_compiler"]
    if compiler["status"] == "pass":
        definition = definitions["c_compiler"][0]
        resolved_compiler = (
            str(compiler["executable"]),
            *definition[1:],
        )
        capability = _probe_freestanding_i386(
            resolved_compiler, _cc_target_arguments()
        )
        compiler["capabilities"] = {"freestanding_i386": capability}
        if capability["status"] != "pass":
            compiler["status"] = "error"
            compiler["error"] = "compiler cannot emit freestanding i386 objects"
    commands = {
        name: (str(evidence[name]["executable"]), *command[1:])
        for name, (command, _) in definitions.items()
        if evidence[name]["status"] == "pass"
    }
    return evidence, commands


def probe_optional_oracle_tools() -> dict[str, dict[str, object]]:
    """Record comparison tools without making them normal-build requirements."""
    commands = optional_oracle_commands()
    return {
        name: _probe_tool(commands[name], spec.version_args)
        for name, spec in _optional_oracle_tool_specs().items()
    }


def probe_optional_jpeg_tools() -> dict[str, dict[str, object]]:
    """Record optional JPEG tools that can influence embedded object bytes."""
    definitions = {
        "jpegtran": (("jpegtran",), ("-version",)),
        "djpeg": (("djpeg",), ("-version",)),
        "cjpeg": (("cjpeg",), ("-version",)),
        "ffmpeg": (("ffmpeg",), ("-version",)),
    }
    return {
        name: _probe_tool(command, version_args)
        for name, (command, version_args) in definitions.items()
    }


def _run_command(check: CheckSpec, cwd: Path) -> CommandResult:
    cwd = cwd.resolve()
    print(f"[baseline] {check.name}: {shlex.join(check.command)}", flush=True)
    started = time.perf_counter()
    completed = subprocess.run(
        list(check.command),
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    elapsed = time.perf_counter() - started
    output = completed.stdout
    root_text = str(cwd)
    output = output.replace(root_text, "<WORKTREE>")
    output = output.replace(root_text.replace("\\", "/"), "<WORKTREE>")
    temp_text = re.escape(str(Path(tempfile.gettempdir()).resolve()))
    output = re.sub(
        temp_text + r"[\\/](?:tmp|cupid-baseline-)[^\\/\s]+",
        "<TEMP>",
        output,
        flags=re.IGNORECASE,
    )
    status = "pass" if completed.returncode == 0 else "FAIL"
    print(f"[baseline] {check.name}: {status} ({elapsed:.3f}s)", flush=True)
    if completed.returncode != 0:
        for line in output.rstrip().splitlines()[-40:]:
            print(line, flush=True)
    return CommandResult(completed.returncode, elapsed, output)


def _git_output(
    repo_root: Path, git_command: tuple[str, ...], *args: str
) -> str:
    completed = subprocess.run(
        [*git_command, "-C", str(repo_root), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise BaselineError(completed.stdout.strip() or f"git {' '.join(args)} failed")
    return completed.stdout.strip()


@contextmanager
def isolated_worktree(
    repo_root: Path,
    revision: str,
    run_index: int,
    git_command: tuple[str, ...] = ("git",),
):
    """Check out one committed revision in a verified disposable directory."""
    temp_root = Path(tempfile.gettempdir()).resolve()
    base = Path(tempfile.mkdtemp(prefix=f"cupid-baseline-{run_index}-")).resolve()
    if temp_root != base and temp_root not in base.parents:
        raise BaselineError(f"unsafe baseline temporary directory: {base}")
    checkout = base / "checkout"
    added = False
    try:
        print(f"[baseline] creating isolated build {run_index}", flush=True)
        completed = subprocess.run(
            [
                *git_command,
                "-C",
                str(repo_root),
                "worktree",
                "add",
                "--detach",
                str(checkout),
                revision,
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if completed.returncode != 0:
            raise BaselineError(completed.stdout.strip())
        added = True
        yield checkout
    finally:
        if added:
            subprocess.run(
                [
                    *git_command,
                    "-C",
                    str(repo_root),
                    "worktree",
                    "remove",
                    "--force",
                    str(checkout),
                ],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            subprocess.run(
                [*git_command, "-C", str(repo_root), "worktree", "prune"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        if base.exists():
            shutil.rmtree(base)


def _last_json_array(lines: Iterable[str]) -> list[str]:
    for line in reversed(list(lines)):
        candidate = line.strip()
        if candidate.startswith("["):
            try:
                value = json.loads(candidate)
            except json.JSONDecodeError:
                continue
            if isinstance(value, list) and all(isinstance(item, str) for item in value):
                return value
    raise BaselineError("Make did not emit the bootstrap artifact list")


def capture_build(
    checkout: Path,
    run_index: int,
    jobs: int,
    tool_commands: dict[str, tuple[str, ...]],
    invoke: Callable[[CheckSpec], CommandResult] | None = None,
) -> dict[str, object]:
    """Build and hash one worktree before optional runtime verification."""
    checkout = checkout.resolve()
    make = tool_commands["make"]
    python = tool_commands["python"]
    qemu = tool_commands["qemu"]
    make_config = (
        "WAD_SRCS=",
        "HDD_MB=200",
        "FAT_START_LBA=16384",
    )
    build_checks = []
    for root in SUPPORTED_BUILD_ROOTS:
        directory = root.directory_arguments()
        configuration = make_config if root.name == "root" else ()
        build_checks.extend(
            (
                CheckSpec(
                    root.check_name("clean"),
                    (*make, *directory, *configuration, root.clean_target),
                ),
                CheckSpec(
                    root.check_name("build"),
                    (*make, *directory, f"-j{jobs}", *configuration, "all"),
                ),
                CheckSpec(
                    root.check_name("artifact-list"),
                    (
                        *make,
                        *directory,
                        "-s",
                        *configuration,
                        "print-bootstrap-artifacts",
                    ),
                ),
            )
        )
    if invoke is None:
        invoke = lambda check: _run_command(check, checkout)
    results = run_check_sequence(build_checks, invoke)
    build: dict[str, object] = {
        "run": run_index,
        "status": "pass" if all(item["status"] == "pass" for item in results) else "fail",
        "checks": results,
    }
    if build["status"] != "pass":
        return build

    supported_roots: dict[str, object] = {}
    combined_artifacts: list[str] = []
    artifact_cohort: list[dict[str, str]] = []
    for root in SUPPORTED_BUILD_ROOTS:
        name = root.name
        prefix = root.path
        check_name = root.check_name("artifact-list")
        artifact_check = next(item for item in results if item["name"] == check_name)
        artifacts = _last_json_array(artifact_check["output_tail"])
        artifact_check["output_tail"] = [
            f"<artifact list: {len(artifacts)} paths>"
        ]
        prefixed = [f"{prefix}/{path}" if prefix else path for path in artifacts]
        combined_artifacts.extend(prefixed)
        artifact_cohort.extend(
            {
                "id": _logical_artifact_id(name, path),
                "path": f"{prefix}/{path}" if prefix else path,
            }
            for path in artifacts
        )
        supported_roots[name] = {
            "path": prefix or ".",
            "artifact_order": list(dict.fromkeys(artifacts)),
            "artifacts": hash_artifacts(
                checkout / prefix if prefix else checkout,
                artifacts,
            ),
        }
    build["supported_roots"] = supported_roots
    build["artifact_cohort"] = artifact_cohort
    build["artifact_order"] = list(dict.fromkeys(combined_artifacts))
    build["artifacts"] = hash_artifacts(checkout, build["artifact_order"])
    build["quality"] = {
        "kernel_text_bytes": elf32_section_size(checkout / "kernel/kernel.elf", ".text"),
        "kernel_elf_bytes": (checkout / "kernel/kernel.elf").stat().st_size,
        "kernel_binary_bytes": (checkout / "kernel/kernel.bin").stat().st_size,
        "disk_image_bytes": (checkout / "cupidos.img").stat().st_size,
    }

    if run_index == 1:
        log_dir = checkout / "build" / "bootstrap" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        validation_checks = [
            CheckSpec(
                "host-unit-tests",
                (
                    *python,
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "tests",
                    "-p",
                    "test_*.py",
                ),
            ),
            CheckSpec(
                "cupidc-gui-smoke",
                (
                    *python,
                    "tools/gui_terminal_smoke.py",
                    "--qemu",
                    qemu[0],
                    "--command",
                    "/bin/ls.cc",
                    "--key-pause",
                    "0.60",
                    "--log",
                    "build/bootstrap/logs/cupidc.log",
                    "--timeout",
                    "60",
                ),
            ),
            CheckSpec(
                "cupidasm-gui-smoke",
                (
                    *python,
                    "tools/gui_terminal_smoke.py",
                    "--qemu",
                    qemu[0],
                    "--command",
                    "as /demos/hello.asm",
                    "--key-pause",
                    "0.60",
                    "--log",
                    "build/bootstrap/logs/cupidasm.log",
                    "--timeout",
                    "60",
                ),
            ),
        ]
        validation_results = run_check_sequence(validation_checks, invoke)
        results.extend(validation_results)
        if any(item["status"] != "pass" for item in validation_results):
            build["status"] = "fail"
    return build


def _write_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    temporary.replace(path)


def _read_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BaselineError(f"cannot read baseline evidence {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise BaselineError(f"baseline evidence is not a JSON object: {path}")
    return value


def _json_matches(path: Path, value: dict[str, object]) -> bool:
    expected = json.dumps(value, indent=2, sort_keys=True) + "\n"
    try:
        return path.read_text(encoding="utf-8") == expected
    except (OSError, UnicodeDecodeError):
        return False


def capture_baseline(
    repo_root: Path,
    revision: str,
    output: Path,
    jobs: int,
    runs: int,
) -> tuple[dict[str, object], int]:
    """Capture build, runtime, quality, and two-run reproducibility evidence."""
    if jobs < 1:
        raise BaselineError("--jobs must be at least 1")
    if runs < 2:
        raise BaselineError("--runs must be at least 2")
    repo_root = repo_root.resolve()
    tools, tool_commands = probe_tools()
    oracle_tools = probe_optional_oracle_tools()
    jpeg_tools = probe_optional_jpeg_tools()
    missing_tools = sorted(name for name, item in tools.items() if item["status"] != "pass")
    source: dict[str, object] = {
        "requested_revision": revision,
        "isolated_worktree": True,
    }
    if not missing_tools:
        source_revision = _git_output(
            repo_root,
            tool_commands["git"],
            "rev-parse",
            "--verify",
            f"{revision}^{{commit}}",
        )
        source = {
            "revision": source_revision,
            "isolated_worktree": True,
        }
    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "source": source,
        "host": host_metadata(),
        "configuration": {
            "runs": runs,
            "jobs": jobs,
            "hdd_mb": 200,
            "fat_start_lba": 16384,
            "optional_wads": False,
            "environment_overrides": {
                name: os.environ[name]
                for name in (
                    *(spec.environment for spec in _tool_specs().values()),
                    *(
                        spec.environment
                        for spec in _optional_oracle_tool_specs().values()
                    ),
                    *BUILD_ENVIRONMENT,
                )
                if name in os.environ
            },
        },
        "tools": tools,
        "optional_oracle_tools": oracle_tools,
        "optional_jpeg_tools": jpeg_tools,
        "builds": [],
    }
    if missing_tools:
        manifest["status"] = "fail"
        manifest["error"] = f"tool preflight failed: {', '.join(missing_tools)}"
        _write_json(output, manifest)
        return manifest, 1

    builds = []
    try:
        for run_index in range(1, runs + 1):
            with isolated_worktree(
                repo_root,
                source_revision,
                run_index,
                tool_commands["git"],
            ) as checkout:
                build = capture_build(
                    checkout,
                    run_index,
                    jobs,
                    tool_commands,
                )
                builds.append(build)
                if build["status"] != "pass":
                    break
    except (BaselineError, OSError, subprocess.SubprocessError) as exc:
        manifest["builds"] = builds
        manifest["status"] = "fail"
        manifest["error"] = str(exc)
        _write_json(output, manifest)
        return manifest, 1

    manifest["builds"] = builds
    if len(builds) != runs or any(build["status"] != "pass" for build in builds):
        manifest["status"] = "fail"
        manifest["error"] = "baseline check failed before reproducibility comparison"
        _write_json(output, manifest)
        return manifest, 1

    comparisons = [
        compare_artifact_manifests(builds[0]["artifacts"], build["artifacts"])
        for build in builds[1:]
    ]
    mismatches = [
        {"run": index + 2, "files": comparison["mismatches"]}
        for index, comparison in enumerate(comparisons)
        if not comparison["matched"]
    ]
    manifest["reproducibility"] = {
        "matched": not mismatches,
        "reference_run": 1,
        "compared_runs": list(range(2, runs + 1)),
        "mismatches": mismatches,
    }
    build_seconds = []
    for build in builds:
        build_check = next(item for item in build["checks"] if item["name"] == "build")
        build_seconds.append(build_check["elapsed_seconds"])
    manifest["performance"] = {
        "build_seconds": build_seconds,
        "supported_root_build_seconds": {
            root: [
                next(
                    item["elapsed_seconds"]
                    for item in build["checks"]
                    if item["name"] == check_name
                )
                for build in builds
            ]
            for root, check_name in (
                (root.name, root.check_name("build"))
                for root in SUPPORTED_BUILD_ROOTS
            )
        },
        "runtime_and_test_seconds": {
            item["name"]: item["elapsed_seconds"]
            for item in builds[0]["checks"]
            if item["name"] in {
                "host-unit-tests",
                "cupidc-gui-smoke",
                "cupidasm-gui-smoke",
            }
        },
        "gating": False,
        "note": "Host wall-clock observations are frozen evidence, not the future 20% guest-performance gate.",
    }
    manifest["quality"] = builds[0]["quality"]
    manifest["status"] = "pass" if not mismatches else "fail"
    if mismatches:
        manifest["error"] = "isolated builds produced different artifacts"
    _write_json(output, manifest)
    return manifest, 0 if manifest["status"] == "pass" else 1


def _default_output(repo_root: Path) -> Path:
    host = f"{platform.system()}-{platform.machine()}".lower().replace(" ", "-")
    return repo_root / "build" / "bootstrap" / f"{host}.json"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build a committed Cupid OS revision twice and record baseline evidence."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--runs", type=int, default=2)
    parser.add_argument(
        "--compare-hosts",
        nargs=2,
        type=Path,
        metavar=("FIRST", "SECOND"),
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="check that --compare-hosts output is current instead of rewriting it",
    )
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    if args.compare_hosts:
        comparison_paths = [
            path if path.is_absolute() else repo_root / path
            for path in args.compare_hosts
        ]
        comparison_output = args.output or (
            repo_root / "build/bootstrap/host-comparison.json"
        )
        if not comparison_output.is_absolute():
            comparison_output = repo_root / comparison_output
        try:
            comparison = compare_host_baselines(
                _read_json(comparison_paths[0].resolve()),
                _read_json(comparison_paths[1].resolve()),
            )
        except BaselineError as exc:
            print(f"baseline comparison: {exc}", file=sys.stderr)
            return 2
        current = True
        if args.check:
            current = _json_matches(comparison_output.resolve(), comparison)
            if not current:
                print(
                    f"baseline comparison: stale evidence: {comparison_output.resolve()}",
                    file=sys.stderr,
                )
        else:
            _write_json(comparison_output.resolve(), comparison)
        print(f"[baseline] comparison: {comparison_output.resolve()}", flush=True)
        print(f"[baseline] status: {comparison['status']}", flush=True)
        return 0 if comparison["status"] == "pass" and current else 1
    if args.check:
        parser.error("--check requires --compare-hosts")
    output = args.output or _default_output(repo_root)
    if not output.is_absolute():
        output = repo_root / output
    try:
        manifest, returncode = capture_baseline(
            repo_root,
            args.revision,
            output.resolve(),
            args.jobs,
            args.runs,
        )
    except BaselineError as exc:
        print(f"baseline: {exc}", file=sys.stderr)
        return 2
    print(f"[baseline] evidence: {output.resolve()}", flush=True)
    print(f"[baseline] status: {manifest['status']}", flush=True)
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())

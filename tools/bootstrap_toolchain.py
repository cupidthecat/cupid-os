#!/usr/bin/env python3
"""Verify and consume the checked Cupid Toolchain bootstrap seed."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


SEED_SCHEMA = "cupid.bootstrap-seed.v1"
SEED_SOURCE_REVISION = "d5e4ed784c54ea8dad581ac736ee8b62553627d8"
TOOL_NAMES = ("cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc")
PRODUCER_NAMES = ("cupidc", "cupidasm", "cupidld")
EXPECTED_TARGET = {
    "abi": "linux-int80",
    "architecture": "i386",
    "byte_order": "little",
    "elf_class": 32,
    "entry": 0x08048000,
    "linkage": "static",
    "operating_system": "linux",
}
EXPECTED_PRODUCER_LINEAGE = {
    "assembly": "host-built CupidASM core",
    "c": "host-built CupidC core",
    "link": "host-built CupidLD core",
}
EXPECTED_FIXED_POINT_COMMAND = "make test-toolchain-fixed-point"
EXPECTED_INCLUDE_ARGUMENTS = (
    "-I",
    "/toolchain",
    "--include-angle",
    "/toolchain/hosted/i386-linux/include",
)
EXPECTED_STARTUP = "/toolchain/hosted/i386-linux/start.asm"
EXPECTED_SOURCES = (
    ("runtime", "/toolchain/hosted/i386-linux/runtime.c", True),
    ("ctool", "/toolchain/ctool.c", False),
    ("ctool_host", "/toolchain/ctool_host.c", False),
    ("elf32", "/toolchain/elf32.c", False),
    ("x86", "/toolchain/x86.c", False),
    ("cupidasm", "/toolchain/cupidasm.c", False),
    ("cupidasm_main", "/toolchain/cupidasm_main.c", False),
    ("cupiddis", "/toolchain/cupiddis.c", False),
    ("cupiddis_main", "/toolchain/cupiddis_main.c", False),
    ("cupidobj", "/toolchain/cupidobj.c", False),
    ("cupidobj_main", "/toolchain/cupidobj_main.c", False),
    ("cupidld", "/toolchain/cupidld.c", False),
    ("cupidld_main", "/toolchain/cupidld_main.c", False),
    ("cupidc_pp", "/toolchain/cupidc_pp.c", False),
    ("cupidc_type", "/toolchain/cupidc_type.c", False),
    ("cupidc_frontend", "/toolchain/cupidc_frontend.c", False),
    ("cupidc_ir", "/toolchain/cupidc_ir.c", False),
    ("cupidc_emit", "/toolchain/cupidc_emit.c", False),
    ("cupidc_main", "/toolchain/cupidc_main.c", False),
)
EXPECTED_LINKS = {
    "cupidasm": (
        "start",
        "cupidasm_main",
        "cupidasm",
        "ctool_host",
        "ctool",
        "elf32",
        "x86",
        "runtime",
    ),
    "cupiddis": (
        "start",
        "cupiddis_main",
        "cupiddis",
        "ctool_host",
        "ctool",
        "elf32",
        "x86",
        "runtime",
    ),
    "cupidld": (
        "start",
        "cupidld_main",
        "cupidld",
        "ctool_host",
        "ctool",
        "elf32",
        "runtime",
    ),
    "cupidobj": (
        "start",
        "cupidobj_main",
        "cupidobj",
        "ctool_host",
        "ctool",
        "elf32",
        "runtime",
    ),
    "cupidc": (
        "start",
        "cupidc_main",
        "cupidc_emit",
        "cupidc_ir",
        "cupidc_frontend",
        "cupidc_type",
        "cupidc_pp",
        "ctool_host",
        "ctool",
        "elf32",
        "x86",
        "runtime",
    ),
}
REPORT_SCHEMA = "cupid.bootstrap-report.v1"
WSL_PRIVATE_RUN_SCRIPT = (
    "umask 077; "
    'private="$(mktemp -d '
    '"${TMPDIR:-/tmp}/cupid-bootstrap-tool.XXXXXX")" || exit 125; '
    'chmod 700 "$private" || exit 125; '
    'probe="$private/tool"; '
    "trap 'rm -rf -- \"$private\"' EXIT HUP INT TERM; "
    'cd "$1" || exit 125; '
    'cp "$2" "$probe" || exit 125; '
    'chmod 700 "$probe" || exit 125; '
    'shift 2; "$probe" "$@"'
)


@dataclass(frozen=True)
class Stage:
    objects: dict[str, Path]
    tools: dict[str, Path]


@dataclass(frozen=True)
class SeedInputs:
    manifest: dict[str, object]
    manifest_sha256: str
    tools: dict[str, Path]


class ToolRunner:
    """Run static i386 Linux tools directly or through WSL."""

    def __init__(self, working_directory: Path):
        self.working_directory = working_directory.resolve()
        self.uses_wsl = os.name == "nt"
        if self.uses_wsl and shutil.which("wsl") is None:
            raise BootstrapError(
                "WSL is required to run the i386 Linux seed on Windows"
            )

    @property
    def platform_name(self) -> str:
        return "windows-wsl" if self.uses_wsl else "linux"

    def _wsl_path(self, path: Path) -> str:
        result = subprocess.run(
            ["wsl", "-e", "wslpath", "-a", str(path.resolve())],
            text=True,
            capture_output=True,
        )
        if result.returncode != 0 or not result.stdout.strip():
            raise BootstrapError(
                f"WSL could not translate {path}: {result.stderr.strip()}"
            )
        return result.stdout.strip()

    def _wsl_argument(self, argument: str | Path) -> str:
        if isinstance(argument, Path):
            return self._wsl_path(argument)
        if re.match(r"^[A-Za-z]:[\\/]", argument):
            return self._wsl_path(Path(argument))
        return argument

    def run(
        self,
        executable: Path,
        arguments: Sequence[str | Path],
        timeout: int,
    ) -> subprocess.CompletedProcess[str]:
        if self.uses_wsl:
            linux_executable = self._wsl_path(executable)
            linux_working_directory = self._wsl_path(
                self.working_directory
            )
            linux_arguments = [
                self._wsl_argument(argument) for argument in arguments
            ]
            return subprocess.run(
                [
                    "wsl",
                    "-e",
                    "sh",
                    "-c",
                    WSL_PRIVATE_RUN_SCRIPT,
                    "sh",
                    linux_working_directory,
                    linux_executable,
                    *linux_arguments,
                ],
                text=True,
                capture_output=True,
                timeout=timeout,
            )

        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-tool-"
        ) as temporary:
            staged_executable = Path(temporary) / executable.name
            shutil.copyfile(executable, staged_executable)
            staged_executable.chmod(0o700)
            return subprocess.run(
                [
                    str(staged_executable),
                    *[str(argument) for argument in arguments],
                ],
                cwd=self.working_directory,
                text=True,
                capture_output=True,
                timeout=timeout,
            )


class BootstrapError(RuntimeError):
    """A checked bootstrap input or operation failed validation."""


def _require_object(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise BootstrapError(f"{label} must be an object")
    return value


def _require_list(value: object, label: str) -> list[object]:
    if not isinstance(value, list):
        raise BootstrapError(f"{label} must be an array")
    return value


def _require_exact_keys(
    value: dict[str, object],
    expected: set[str] | frozenset[str],
    label: str,
) -> None:
    if set(value) != set(expected):
        raise BootstrapError(f"{label} keys differ")


def _manifest_object(
    pairs: list[tuple[str, object]],
) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise BootstrapError(
                f"manifest contains duplicate JSON key: {key}"
            )
        value[key] = item
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _build_plan_sha256(plan: dict[str, object]) -> str:
    encoded = json.dumps(
        plan, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _source_input_paths(
    source_root: Path,
    plan: dict[str, object],
) -> dict[str, Path]:
    source_root = source_root.resolve()
    paths: list[Path] = []
    raw_sources = _require_list(plan.get("sources"), "build_plan.sources")
    for raw_source in raw_sources:
        source = _require_object(raw_source, "build source")
        logical_path = str(source["path"])
        paths.append(source_root / logical_path.lstrip("/"))

    startup = str(plan["startup"])
    paths.append(source_root / startup.lstrip("/"))
    paths.append(source_root / "link.ld")

    paths.extend(sorted((source_root / "toolchain").glob("*.h")))
    paths.extend(
        sorted(
            (
                source_root
                / "toolchain"
                / "hosted"
                / "i386-linux"
                / "include"
            ).glob("*.h")
        )
    )

    resolved: dict[str, Path] = {}
    for path in paths:
        if path.is_symlink():
            raise BootstrapError(
                f"source input may not be a symlink: {path}"
            )
        try:
            relative = path.resolve(strict=True).relative_to(source_root)
        except (OSError, ValueError) as error:
            raise BootstrapError(
                f"cannot resolve source input {path}: {error}"
            ) from error
        name = relative.as_posix()
        if name in resolved:
            raise BootstrapError(f"source input is duplicated: {name}")
        if not path.is_file():
            raise BootstrapError(f"source input is not a file: {name}")
        resolved[name] = path
    return resolved


def capture_source_snapshot(
    source_root: Path,
    plan: dict[str, object],
) -> dict[str, dict[str, object]]:
    """Hash every active toolchain source input in a build plan."""
    inventory: dict[str, dict[str, object]] = {}
    for name, path in sorted(_source_input_paths(source_root, plan).items()):
        try:
            data = path.read_bytes()
        except OSError as error:
            raise BootstrapError(
                f"cannot read source input {name}: {error}"
            ) from error
        inventory[name] = {
            "sha256": hashlib.sha256(data).hexdigest(),
            "size": len(data),
        }
    return inventory


def _source_snapshot_sha256(
    inventory: dict[str, dict[str, object]],
) -> str:
    encoded = json.dumps(
        inventory, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def require_source_snapshot(
    source_root: Path,
    plan: dict[str, object],
    expected: dict[str, dict[str, object]],
) -> None:
    """Reject a build whose active source inputs changed after capture."""
    current = capture_source_snapshot(source_root, plan)
    if current == expected:
        return
    changed = sorted(
        name
        for name in set(expected) | set(current)
        if expected.get(name) != current.get(name)
    )
    visible = ", ".join(changed[:3])
    if len(changed) > 3:
        visible += f", and {len(changed) - 3} more"
    raise BootstrapError(
        f"source inputs changed during bootstrap: {visible}"
    )


def _validate_static_i386_elf_bytes(
    data: bytes,
    name: str,
    expected_entry: int,
) -> None:
    if len(data) < 52:
        raise BootstrapError(f"{name} has a truncated ELF32 header")
    if data[:7] != b"\x7fELF\x01\x01\x01":
        raise BootstrapError(f"{name} is not little-endian ELF32")
    (
        _identity,
        file_type,
        machine,
        version,
        entry,
        program_offset,
        _section_offset,
        _flags,
        header_size,
        program_entry_size,
        program_count,
        _section_entry_size,
        _section_count,
        _section_names,
    ) = struct.unpack_from("<16sHHIIIIIHHHHHH", data, 0)
    if file_type != 2 or machine != 3 or version != 1:
        raise BootstrapError(f"{name} is not an i386 executable")
    if entry != expected_entry:
        raise BootstrapError(
            f"{name} entry is 0x{entry:08x}, expected "
            f"0x{expected_entry:08x}"
        )
    if header_size != 52 or program_entry_size != 32 or program_count == 0:
        raise BootstrapError(f"{name} has an invalid program header table")
    program_bytes = program_entry_size * program_count
    if (
        program_offset > len(data)
        or program_bytes > len(data) - program_offset
    ):
        raise BootstrapError(f"{name} has a truncated program header table")
    load_count = 0
    entry_is_file_backed_executable = False
    for index in range(program_count):
        offset = program_offset + index * program_entry_size
        (
            segment_type,
            file_offset,
            virtual_address,
            _physical_address,
            file_size,
            memory_size,
            segment_flags,
            _alignment,
        ) = struct.unpack_from("<IIIIIIII", data, offset)
        if segment_type in (2, 3):
            raise BootstrapError(f"{name} is not statically linked")
        if segment_type == 1:
            load_count += 1
            if file_size > memory_size:
                raise BootstrapError(
                    f"{name} has a load segment smaller than its file data"
                )
            if (
                file_offset > len(data)
                or file_size > len(data) - file_offset
            ):
                raise BootstrapError(
                    f"{name} has a truncated load segment"
                )
            if segment_flags & 0x3 == 0x3:
                raise BootstrapError(
                    f"{name} has a writable executable segment"
                )
            if (
                segment_flags & 0x1
                and entry >= virtual_address
                and entry - virtual_address < file_size
            ):
                entry_is_file_backed_executable = True
    if load_count == 0:
        raise BootstrapError(f"{name} has no loadable segment")
    if not entry_is_file_backed_executable:
        raise BootstrapError(
            f"{name} entry is not in executable file bytes"
        )


def _validate_static_i386_elf(path: Path, expected_entry: int) -> None:
    _validate_static_i386_elf_bytes(
        path.read_bytes(), path.name, expected_entry
    )


def _validate_i386_relocatable(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise BootstrapError(f"{path.name} is not little-endian ELF32")
    file_type, machine, version = struct.unpack_from("<HHI", data, 16)
    if file_type != 1 or machine != 3 or version != 1:
        raise BootstrapError(f"{path.name} is not an i386 relocatable object")


def _run_clean(
    runner: ToolRunner,
    executable: Path,
    arguments: Sequence[str | Path],
    label: str,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    try:
        result = runner.run(executable, arguments, timeout)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BootstrapError(f"{label} could not run: {error}") from error
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip()
        suffix = f": {details}" if details else ""
        raise BootstrapError(
            f"{label} failed with status {result.returncode}{suffix}"
        )
    if result.stdout != "" or result.stderr != "":
        raise BootstrapError(f"{label} produced unexpected command output")
    return result


def _logical_path(root: Path, path: Path) -> str:
    try:
        relative = path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise BootstrapError(
            f"bootstrap output must stay under the source root: {path}"
        ) from error
    return "/" + relative.as_posix()


def _validate_build_plan(manifest: dict[str, object]) -> None:
    plan = _require_object(manifest.get("build_plan"), "build_plan")
    _require_exact_keys(
        plan,
        {
            "include_arguments",
            "links",
            "producer_tools",
            "sources",
            "startup",
            "workers",
        },
        "build plan",
    )
    includes = _require_list(
        plan.get("include_arguments"), "build_plan.include_arguments"
    )
    if tuple(includes) != EXPECTED_INCLUDE_ARGUMENTS:
        raise BootstrapError("build plan include arguments differ")
    if plan.get("startup") != EXPECTED_STARTUP:
        raise BootstrapError("build plan startup source differs")
    workers = plan.get("workers")
    if type(workers) is not int:
        raise BootstrapError("build plan workers type differs")
    if workers != 2:
        raise BootstrapError("build plan must use two workers")
    producers = _require_list(
        plan.get("producer_tools"), "build_plan.producer_tools"
    )
    if tuple(producers) != PRODUCER_NAMES:
        raise BootstrapError("build plan producer tools differ")

    sources = _require_list(plan.get("sources"), "build_plan.sources")
    if len(sources) != len(EXPECTED_SOURCES):
        raise BootstrapError("build plan must contain 19 C sources")
    actual_sources: list[tuple[str, str, bool]] = []
    for index, raw_source in enumerate(sources):
        source = _require_object(
            raw_source, f"build_plan.sources[{index}]"
        )
        _require_exact_keys(
            source,
            {"gnu_extensions", "name", "path"},
            f"build source {source.get('name', index)}",
        )
        name = source.get("name")
        logical_path = source.get("path")
        extensions = source.get("gnu_extensions")
        if not isinstance(name, str) or not name:
            raise BootstrapError(f"build source {index} has no name")
        if (
            not isinstance(logical_path, str)
            or not logical_path.startswith("/toolchain/")
            or not logical_path.endswith(".c")
        ):
            raise BootstrapError(f"build source path is invalid: {name}")
        if not isinstance(extensions, bool):
            raise BootstrapError(
                f"build source GNU mode is invalid: {name}"
            )
        actual_sources.append((name, logical_path, extensions))
    if tuple(actual_sources) != EXPECTED_SOURCES:
        raise BootstrapError("build plan sources differ")

    links = _require_object(plan.get("links"), "build_plan.links")
    if set(links) != set(EXPECTED_LINKS):
        raise BootstrapError("build plan tool links differ")
    for name, expected in EXPECTED_LINKS.items():
        raw_order = _require_list(
            links.get(name), f"build_plan.links.{name}"
        )
        if tuple(raw_order) != expected:
            raise BootstrapError(f"build plan link order differs: {name}")


def _read_manifest_capture(
    manifest_path: Path,
) -> tuple[dict[str, object], bytes]:
    if manifest_path.is_symlink():
        raise BootstrapError("manifest may not be a symlink")
    try:
        encoded = manifest_path.read_bytes()
        raw_manifest = json.loads(
            encoded.decode("utf-8"),
            object_pairs_hook=_manifest_object,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BootstrapError(f"cannot read {manifest_path}: {error}") from error
    return _require_object(raw_manifest, "manifest"), encoded


def _verify_seed_manifest_data(
    manifest: dict[str, object],
    seed_directory: Path,
    snapshot_directory: Path | None,
) -> dict[str, Path]:
    _require_exact_keys(
        manifest,
        {
            "artifacts",
            "build_plan",
            "build_plan_sha256",
            "provenance",
            "schema",
            "target",
        },
        "manifest",
    )
    if manifest.get("schema") != SEED_SCHEMA:
        raise BootstrapError("manifest schema is unsupported")
    target = _require_object(manifest.get("target"), "target")
    _require_exact_keys(target, set(EXPECTED_TARGET), "manifest target")
    for field, expected in EXPECTED_TARGET.items():
        actual = target.get(field)
        if type(actual) is not type(expected):
            raise BootstrapError(
                f"manifest target field type differs: {field}"
            )
        if actual != expected:
            raise BootstrapError("manifest target differs")

    provenance = _require_object(
        manifest.get("provenance"), "provenance"
    )
    _require_exact_keys(
        provenance,
        {
            "fixed_point_command",
            "fixed_point_result",
            "producer_lineage",
            "seed_generation",
            "source_revision",
        },
        "provenance",
    )
    revision = provenance.get("source_revision")
    if revision != SEED_SOURCE_REVISION:
        raise BootstrapError("source revision differs")
    if provenance.get("seed_generation") != "generation-one":
        raise BootstrapError("seed generation differs")
    if provenance.get("fixed_point_result") != "pass":
        raise BootstrapError("seed lacks passing fixed-point provenance")
    if provenance.get("fixed_point_command") != EXPECTED_FIXED_POINT_COMMAND:
        raise BootstrapError("fixed-point command differs")
    if provenance.get("producer_lineage") != EXPECTED_PRODUCER_LINEAGE:
        raise BootstrapError("producer lineage differs")

    plan = _require_object(manifest.get("build_plan"), "build_plan")
    if type(plan.get("workers")) is not int:
        raise BootstrapError("build plan workers type differs")
    expected_plan_digest = manifest.get("build_plan_sha256")
    if (
        not isinstance(expected_plan_digest, str)
        or re.fullmatch(r"[0-9a-f]{64}", expected_plan_digest) is None
    ):
        raise BootstrapError("build plan SHA-256 is invalid")
    if _build_plan_sha256(plan) != expected_plan_digest:
        raise BootstrapError("build plan SHA-256 differs")
    _validate_build_plan(manifest)
    artifacts = _require_list(manifest.get("artifacts"), "artifacts")
    if len(artifacts) != len(TOOL_NAMES):
        raise BootstrapError("manifest must contain five tool artifacts")
    resolved: dict[str, Path] = {}
    seen_files: set[str] = set()
    for index, raw_artifact in enumerate(artifacts):
        artifact = _require_object(raw_artifact, f"artifacts[{index}]")
        _require_exact_keys(
            artifact,
            {"file", "name", "producer", "sha256", "size"},
            f"artifact {index}",
        )
        name = artifact.get("name")
        file_name = artifact.get("file")
        size = artifact.get("size")
        digest = artifact.get("sha256")
        if not isinstance(name, str) or name not in TOOL_NAMES:
            raise BootstrapError(f"artifact {index} has an unknown tool name")
        if name in resolved:
            raise BootstrapError(f"tool artifact is duplicated: {name}")
        if (
            not isinstance(file_name, str)
            or Path(file_name).name != file_name
            or file_name != f"{name}.elf"
        ):
            raise BootstrapError(f"artifact path is invalid: {name}")
        if file_name in seen_files:
            raise BootstrapError(f"artifact file is duplicated: {file_name}")
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise BootstrapError(f"artifact size is invalid: {name}")
        if (
            not isinstance(digest, str)
            or re.fullmatch(r"[0-9a-f]{64}", digest) is None
        ):
            raise BootstrapError(f"artifact SHA-256 is invalid: {name}")
        producer = artifact.get("producer")
        if not isinstance(producer, bool):
            raise BootstrapError(
                f"artifact producer role type is invalid: {name}"
            )
        if producer != (name in PRODUCER_NAMES):
            raise BootstrapError(f"artifact producer role differs: {name}")
        path = seed_directory / file_name
        if path.is_symlink():
            raise BootstrapError(f"seed artifact may not be a symlink: {name}")
        try:
            data = path.read_bytes()
        except OSError as error:
            raise BootstrapError(
                f"cannot read seed artifact {file_name}: {error}"
            ) from error
        if not path.is_file():
            raise BootstrapError(f"seed artifact is not a file: {name}")
        actual_size = len(data)
        if actual_size != size:
            raise BootstrapError(
                f"size differs for {file_name}: expected {size}, "
                f"found {actual_size}"
            )
        actual_digest = hashlib.sha256(data).hexdigest()
        if actual_digest != digest:
            raise BootstrapError(f"SHA-256 differs for {file_name}")
        _validate_static_i386_elf_bytes(
            data, file_name, EXPECTED_TARGET["entry"]
        )
        if snapshot_directory is None:
            resolved_path = path
        else:
            resolved_path = snapshot_directory / file_name
            resolved_path.write_bytes(data)
            resolved_path.chmod(0o700)
            if (
                resolved_path.stat().st_size != size
                or _sha256(resolved_path) != digest
            ):
                raise BootstrapError(
                    f"frozen seed artifact differs: {file_name}"
                )
        resolved[name] = resolved_path
        seen_files.add(file_name)
    if set(resolved) != set(TOOL_NAMES):
        raise BootstrapError("manifest tool set differs")
    actual_elf_files = {path.name for path in seed_directory.glob("*.elf")}
    if actual_elf_files != seen_files:
        raise BootstrapError("seed directory contains an unlisted ELF file")
    return resolved


def _load_seed_inputs(
    manifest_path: Path,
    snapshot_directory: Path | None,
) -> SeedInputs:
    manifest, encoded_manifest = _read_manifest_capture(manifest_path)
    seed_directory = manifest_path.parent.resolve()
    tools = _verify_seed_manifest_data(
        manifest, seed_directory, snapshot_directory
    )
    return SeedInputs(
        manifest=manifest,
        manifest_sha256=hashlib.sha256(encoded_manifest).hexdigest(),
        tools=tools,
    )


def verify_seed_manifest(manifest_path: Path) -> dict[str, Path]:
    return _load_seed_inputs(manifest_path, None).tools


def freeze_seed_inputs(
    manifest_path: Path,
    snapshot_directory: Path,
) -> SeedInputs:
    if snapshot_directory.is_symlink():
        raise BootstrapError("frozen seed directory may not be a symlink")
    if snapshot_directory.exists():
        if not snapshot_directory.is_dir():
            raise BootstrapError("frozen seed path is not a directory")
        if any(snapshot_directory.iterdir()):
            raise BootstrapError("frozen seed directory is not empty")
    else:
        snapshot_directory.mkdir(mode=0o700)
    snapshot_directory.chmod(0o700)
    return _load_seed_inputs(manifest_path, snapshot_directory)


def _build_stage(
    runner: ToolRunner,
    source_root: Path,
    stage_directory: Path,
    producers: dict[str, Path],
    plan: dict[str, object],
    stage_name: str,
) -> Stage:
    stage_directory.mkdir()
    raw_sources = _require_list(plan.get("sources"), "build_plan.sources")
    include_arguments = [
        str(argument)
        for argument in _require_list(
            plan.get("include_arguments"),
            "build_plan.include_arguments",
        )
    ]

    def compile_source(raw_source: object) -> tuple[str, Path]:
        source = _require_object(raw_source, "build source")
        name = str(source["name"])
        logical_source = str(source["path"])
        object_path = stage_directory / f"{name}.o"
        arguments: list[str | Path] = [
            "--root",
            source_root,
            "-c",
            logical_source,
            *include_arguments,
        ]
        if source["gnu_extensions"]:
            arguments.append("--gnu")
        arguments.extend(
            ["-o", _logical_path(source_root, object_path)]
        )
        _run_clean(
            runner,
            producers["cupidc"],
            arguments,
            f"{stage_name} CupidC for {logical_source}",
            360,
        )
        _validate_i386_relocatable(object_path)
        return name, object_path

    workers = int(plan["workers"])
    with ThreadPoolExecutor(max_workers=workers) as executor:
        compiled = list(executor.map(compile_source, raw_sources))
    objects = dict(compiled)

    startup_logical = str(plan["startup"])
    startup_source = source_root / startup_logical.lstrip("/")
    startup_object = stage_directory / "start.o"
    _run_clean(
        runner,
        producers["cupidasm"],
        [
            "-f",
            "elf32",
            startup_source,
            "-o",
            startup_object,
        ],
        f"{stage_name} CupidASM startup",
        120,
    )
    _validate_i386_relocatable(startup_object)
    objects["start"] = startup_object

    raw_links = _require_object(plan.get("links"), "build_plan.links")
    tools: dict[str, Path] = {}
    for tool_name in TOOL_NAMES:
        order = [
            str(name)
            for name in _require_list(
                raw_links.get(tool_name),
                f"build_plan.links.{tool_name}",
            )
        ]
        executable = stage_directory / f"{tool_name}.elf"
        _run_clean(
            runner,
            producers["cupidld"],
            [
                "-m",
                "elf_i386",
                "--text-address",
                "0x08048000",
                "--entry",
                "_start",
                "-o",
                executable,
                *[objects[name] for name in order],
            ],
            f"{stage_name} CupidLD for {tool_name}",
            180,
        )
        _validate_static_i386_elf(
            executable, int(EXPECTED_TARGET["entry"])
        )
        tools[tool_name] = executable
    return Stage(objects=objects, tools=tools)


def _compare_stages(
    stage_two: Stage,
    stage_three: Stage,
    source_names: Sequence[str],
) -> dict[str, object]:
    for name in source_names:
        if (
            stage_two.objects[name].read_bytes()
            != stage_three.objects[name].read_bytes()
        ):
            raise BootstrapError(f"C object differs across stages: {name}")
    if (
        stage_two.objects["start"].read_bytes()
        != stage_three.objects["start"].read_bytes()
    ):
        raise BootstrapError("startup object differs across stages")
    for name in TOOL_NAMES:
        if (
            stage_two.tools[name].read_bytes()
            != stage_three.tools[name].read_bytes()
        ):
            raise BootstrapError(f"tool image differs across stages: {name}")
    return {
        "all_equal": True,
        "c_objects": len(source_names),
        "startup_objects": 1,
        "tool_images": len(TOOL_NAMES),
    }


def _run_stage_pair(
    runner: ToolRunner,
    stage_two: Stage,
    stage_three: Stage,
    tool_name: str,
    stage_two_arguments: Sequence[str | Path],
    stage_three_arguments: Sequence[str | Path] | None = None,
    timeout: int = 60,
) -> subprocess.CompletedProcess[str]:
    if stage_three_arguments is None:
        stage_three_arguments = stage_two_arguments
    try:
        stage_two_result = runner.run(
            stage_two.tools[tool_name], stage_two_arguments, timeout
        )
        stage_three_result = runner.run(
            stage_three.tools[tool_name], stage_three_arguments, timeout
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise BootstrapError(
            f"{tool_name} behavior check could not run: {error}"
        ) from error
    if (
        stage_three_result.returncode != stage_two_result.returncode
        or stage_three_result.stdout != stage_two_result.stdout
        or stage_three_result.stderr != stage_two_result.stderr
    ):
        raise BootstrapError(
            f"{tool_name} behavior differs across stages"
        )
    return stage_two_result


def _expect_status(
    result: subprocess.CompletedProcess[str],
    expected: int,
    label: str,
) -> None:
    if result.returncode != expected:
        details = result.stderr.strip() or result.stdout.strip()
        suffix = f": {details}" if details else ""
        raise BootstrapError(
            f"{label} returned {result.returncode}, expected "
            f"{expected}{suffix}"
        )


def _run_behavior_checks(
    runner: ToolRunner,
    source_root: Path,
    output_root: Path,
    stage_two: Stage,
    stage_three: Stage,
) -> dict[str, int]:
    behavior_root = output_root / "behavior"
    behavior_root.mkdir()
    for tool_name in TOOL_NAMES:
        help_result = _run_stage_pair(
            runner,
            stage_two,
            stage_three,
            tool_name,
            ["--help"],
        )
        _expect_status(help_result, 0, f"{tool_name} help")
        if not help_result.stdout or help_result.stderr:
            raise BootstrapError(f"{tool_name} help output differs")

    valid_source = behavior_root / "valid.c"
    invalid_source = behavior_root / "invalid.c"
    valid_source.write_text(
        "int fixed_point_value(int value) { return value + 17; }\n",
        encoding="utf-8",
        newline="\n",
    )
    invalid_source.write_text(
        "int fixed_point_broken( {\n",
        encoding="utf-8",
        newline="\n",
    )
    stage_two_valid = behavior_root / "stage-two-valid.o"
    stage_three_valid = behavior_root / "stage-three-valid.o"
    compiler_root_arguments: list[str | Path] = [
        "--root",
        behavior_root,
        "-c",
        "/valid.c",
    ]
    valid_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [*compiler_root_arguments, "-o", "/stage-two-valid.o"],
        [*compiler_root_arguments, "-o", "/stage-three-valid.o"],
    )
    _expect_status(valid_result, 0, "CupidC valid source")
    if valid_result.stdout or valid_result.stderr:
        raise BootstrapError("CupidC valid-source output differs")
    if stage_two_valid.read_bytes() != stage_three_valid.read_bytes():
        raise BootstrapError("CupidC behavior object differs across stages")
    _validate_i386_relocatable(stage_two_valid)

    sentinel = b"fixed-point-failure-sentinel"
    stage_two_failure = behavior_root / "stage-two-failure.o"
    stage_three_failure = behavior_root / "stage-three-failure.o"
    stage_two_failure.write_bytes(sentinel)
    stage_three_failure.write_bytes(sentinel)
    invalid_root_arguments: list[str | Path] = [
        "--root",
        behavior_root,
        "-c",
        "/invalid.c",
    ]
    invalid_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [*invalid_root_arguments, "-o", "/stage-two-failure.o"],
        [*invalid_root_arguments, "-o", "/stage-three-failure.o"],
    )
    _expect_status(invalid_result, 1, "CupidC invalid source")
    if (
        invalid_result.stdout
        or "/invalid.c:1:" not in invalid_result.stderr
        or stage_two_failure.read_bytes() != sentinel
        or stage_three_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidC failure behavior differs")

    assembly_source = behavior_root / "fixed-point.asm"
    stage_two_binary = behavior_root / "stage-two.bin"
    stage_three_binary = behavior_root / "stage-three.bin"
    assembly_source.write_text(
        "BITS 16\n"
        "ORG 0x7c00\n"
        "start:\n"
        "    mov ax, 0x1234\n"
        "    ret\n",
        encoding="utf-8",
        newline="\n",
    )
    assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        ["-f", "bin", assembly_source, "-o", stage_two_binary],
        ["-f", "bin", assembly_source, "-o", stage_three_binary],
    )
    _expect_status(assembly_result, 0, "CupidASM raw assembly")
    if (
        assembly_result.stdout
        or assembly_result.stderr
        or stage_two_binary.read_bytes() != b"\xb8\x34\x12\xc3"
        or stage_three_binary.read_bytes() != stage_two_binary.read_bytes()
    ):
        raise BootstrapError("CupidASM raw output differs")

    disassembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        [
            "--raw",
            "--mode",
            "16",
            "--base",
            "0x7c00",
            stage_two_binary,
        ],
        [
            "--raw",
            "--mode",
            "16",
            "--base",
            "0x7c00",
            stage_three_binary,
        ],
    )
    _expect_status(disassembly_result, 0, "CupidDis raw inspection")
    if (
        "mov ax, 0x1234" not in disassembly_result.stdout
        or disassembly_result.stderr
    ):
        raise BootstrapError("CupidDis raw report differs")

    asset = behavior_root / "asset.bin"
    stage_two_wrapped = behavior_root / "stage-two-wrapped.o"
    stage_three_wrapped = behavior_root / "stage-three-wrapped.o"
    asset.write_bytes(b"Cupid fixed point\x00")
    wrap_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [
            "wrap",
            asset,
            "--stem",
            "fixed_point_asset",
            "--section",
            ".rodata",
            "--readonly",
            "-o",
            stage_two_wrapped,
        ],
        [
            "wrap",
            asset,
            "--stem",
            "fixed_point_asset",
            "--section",
            ".rodata",
            "--readonly",
            "-o",
            stage_three_wrapped,
        ],
    )
    _expect_status(wrap_result, 0, "CupidObj binary wrap")
    if (
        wrap_result.stdout
        or wrap_result.stderr
        or stage_two_wrapped.read_bytes()
        != stage_three_wrapped.read_bytes()
    ):
        raise BootstrapError("CupidObj binary wrap differs")
    _validate_i386_relocatable(stage_two_wrapped)

    link_source = behavior_root / "start.asm"
    stage_two_link_object = behavior_root / "stage-two-start.o"
    stage_three_link_object = behavior_root / "stage-three-start.o"
    link_source.write_text(
        "BITS 32\n"
        "global _start\n"
        "section .text\n"
        "_start:\n"
        "    mov eax, 1\n"
        "    xor ebx, ebx\n"
        "    int 0x80\n",
        encoding="utf-8",
        newline="\n",
    )
    link_assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        ["-f", "elf32", link_source, "-o", stage_two_link_object],
        ["-f", "elf32", link_source, "-o", stage_three_link_object],
    )
    _expect_status(
        link_assembly_result, 0, "CupidASM relocatable assembly"
    )
    if (
        link_assembly_result.stdout
        or link_assembly_result.stderr
        or stage_two_link_object.read_bytes()
        != stage_three_link_object.read_bytes()
    ):
        raise BootstrapError("CupidASM relocatable output differs")
    _validate_i386_relocatable(stage_two_link_object)

    stage_two_linked = behavior_root / "stage-two-linked.elf"
    stage_three_linked = behavior_root / "stage-three-linked.elf"
    link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "elf_i386",
            "--text-address",
            "0x00600000",
            "--entry",
            "_start",
            "-o",
            stage_two_linked,
            stage_two_link_object,
        ],
        [
            "-m",
            "elf_i386",
            "--text-address",
            "0x00600000",
            "--entry",
            "_start",
            "-o",
            stage_three_linked,
            stage_three_link_object,
        ],
    )
    _expect_status(link_result, 0, "CupidLD fixed-address link")
    if (
        link_result.stdout
        or link_result.stderr
        or stage_two_linked.read_bytes()
        != stage_three_linked.read_bytes()
    ):
        raise BootstrapError("CupidLD fixed-address output differs")
    _validate_static_i386_elf(stage_two_linked, 0x00600000)

    symbol_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        ["--nm", stage_two_linked],
        ["--nm", stage_three_linked],
    )
    _expect_status(symbol_result, 0, "CupidDis symbol listing")
    if " T _start\n" not in symbol_result.stdout or symbol_result.stderr:
        raise BootstrapError("CupidDis symbol listing differs")

    stage_two_script = behavior_root / "stage-two-script.elf"
    stage_three_script = behavior_root / "stage-three-script.elf"
    script_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "elf_i386",
            "-T",
            source_root / "link.ld",
            "-o",
            stage_two_script,
            stage_two_link_object,
        ],
        [
            "-m",
            "elf_i386",
            "-T",
            source_root / "link.ld",
            "-o",
            stage_three_script,
            stage_three_link_object,
        ],
    )
    _expect_status(script_result, 0, "CupidLD script link")
    if (
        script_result.stdout
        or script_result.stderr
        or stage_two_script.read_bytes() != stage_three_script.read_bytes()
    ):
        raise BootstrapError("CupidLD script output differs")
    _validate_static_i386_elf(stage_two_script, 0x00100000)

    text_asset = behavior_root / "text.txt"
    stage_two_text = behavior_root / "stage-two-text.o"
    stage_three_text = behavior_root / "stage-three-text.o"
    text_asset.write_bytes(b"first\r\nsecond\r\n")
    text_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [
            "wrap-text",
            text_asset,
            "--identity",
            "fixed-point.txt",
            "-o",
            stage_two_text,
        ],
        [
            "wrap-text",
            text_asset,
            "--identity",
            "fixed-point.txt",
            "-o",
            stage_three_text,
        ],
    )
    _expect_status(text_result, 0, "CupidObj text wrap")
    if (
        text_result.stdout
        or text_result.stderr
        or stage_two_text.read_bytes() != stage_three_text.read_bytes()
    ):
        raise BootstrapError("CupidObj text wrap differs")
    _validate_i386_relocatable(stage_two_text)

    stage_two_flat = behavior_root / "stage-two-flat.bin"
    stage_three_flat = behavior_root / "stage-three-flat.bin"
    flat_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        ["flat", stage_two_linked, "-o", stage_two_flat],
        ["flat", stage_three_linked, "-o", stage_three_flat],
    )
    _expect_status(flat_result, 0, "CupidObj executable flatten")
    if (
        flat_result.stdout
        or flat_result.stderr
        or not stage_two_flat.read_bytes()
        or stage_two_flat.read_bytes() != stage_three_flat.read_bytes()
    ):
        raise BootstrapError("CupidObj flat output differs")

    invalid_assembly = behavior_root / "invalid.asm"
    invalid_assembly.write_text(
        "BITS 16\nthis_is_not_an_instruction ax\n",
        encoding="utf-8",
        newline="\n",
    )
    stage_two_invalid_assembly = behavior_root / "stage-two-invalid.bin"
    stage_three_invalid_assembly = behavior_root / "stage-three-invalid.bin"
    stage_two_invalid_assembly.write_bytes(sentinel)
    stage_three_invalid_assembly.write_bytes(sentinel)
    invalid_assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        [
            "-f",
            "bin",
            invalid_assembly,
            "-o",
            stage_two_invalid_assembly,
        ],
        [
            "-f",
            "bin",
            invalid_assembly,
            "-o",
            stage_three_invalid_assembly,
        ],
    )
    _expect_status(
        invalid_assembly_result, 1, "CupidASM invalid source"
    )
    if (
        invalid_assembly_result.stdout
        or "unknown Cupid ASM instruction mnemonic"
        not in invalid_assembly_result.stderr
        or stage_two_invalid_assembly.read_bytes() != sentinel
        or stage_three_invalid_assembly.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidASM failure behavior differs")

    missing_input = behavior_root / "missing.bin"
    missing_disassembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        ["--raw", "--mode", "16", "--base", "0", missing_input],
    )
    _expect_status(
        missing_disassembly_result, 1, "CupidDis missing input"
    )
    if (
        missing_disassembly_result.stdout
        or "cupiddis: cannot load "
        not in missing_disassembly_result.stderr
        or "(not_found)" not in missing_disassembly_result.stderr
    ):
        raise BootstrapError("CupidDis missing-input behavior differs")

    malformed_object = behavior_root / "malformed.o"
    malformed_object.write_bytes(b"\x7fELF")
    malformed_disassembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        ["--all", malformed_object],
    )
    _expect_status(
        malformed_disassembly_result, 1, "CupidDis malformed input"
    )
    if (
        "ELF32 header is truncated"
        not in malformed_disassembly_result.stderr
    ):
        raise BootstrapError("CupidDis malformed-input behavior differs")

    stage_two_link_failure = behavior_root / "stage-two-link-failure.elf"
    stage_three_link_failure = behavior_root / "stage-three-link-failure.elf"
    stage_two_link_failure.write_bytes(sentinel)
    stage_three_link_failure.write_bytes(sentinel)
    malformed_link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "elf_i386",
            "--text-address",
            "0x00600000",
            "--entry",
            "_start",
            "-o",
            stage_two_link_failure,
            malformed_object,
        ],
        [
            "-m",
            "elf_i386",
            "--text-address",
            "0x00600000",
            "--entry",
            "_start",
            "-o",
            stage_three_link_failure,
            malformed_object,
        ],
    )
    _expect_status(malformed_link_result, 1, "CupidLD malformed input")
    if (
        "ELF32 header is truncated" not in malformed_link_result.stderr
        or stage_two_link_failure.read_bytes() != sentinel
        or stage_three_link_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidLD malformed-input behavior differs")

    stage_two_obj_failure = behavior_root / "stage-two-obj-failure.o"
    stage_three_obj_failure = behavior_root / "stage-three-obj-failure.o"
    stage_two_obj_failure.write_bytes(sentinel)
    stage_three_obj_failure.write_bytes(sentinel)
    missing_obj_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [
            "wrap",
            missing_input,
            "--stem",
            "missing",
            "-o",
            stage_two_obj_failure,
        ],
        [
            "wrap",
            missing_input,
            "--stem",
            "missing",
            "-o",
            stage_three_obj_failure,
        ],
    )
    _expect_status(missing_obj_result, 1, "CupidObj missing input")
    if (
        missing_obj_result.stdout
        or "cupidobj: cannot load " not in missing_obj_result.stderr
        or "(not_found)" not in missing_obj_result.stderr
        or stage_two_obj_failure.read_bytes() != sentinel
        or stage_three_obj_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidObj missing-input behavior differs")

    return {
        "failure_cases": 6,
        "help_cases": 5,
        "success_cases": 10,
    }


def _artifact_inventory(paths: dict[str, Path]) -> dict[str, object]:
    return {
        name: {
            "sha256": _sha256(path),
            "size": path.stat().st_size,
        }
        for name, path in sorted(paths.items())
    }


def _bootstrap_from_frozen_seed(
    seed_inputs: SeedInputs,
    source_root: Path,
    output_root: Path,
) -> dict[str, object]:
    seed_tools = seed_inputs.tools
    manifest = seed_inputs.manifest
    plan = _require_object(manifest.get("build_plan"), "build_plan")
    source_root = source_root.resolve()
    output_root = output_root.resolve()
    if not (source_root / "toolchain").is_dir():
        raise BootstrapError(f"source root has no toolchain: {source_root}")
    _logical_path(source_root, output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    stage_two_directory = output_root / "stage-two"
    stage_three_directory = output_root / "stage-three"
    behavior_directory = output_root / "behavior"
    report_path = output_root / "bootstrap-report.json"
    occupied = [
        path
        for path in (
            stage_two_directory,
            stage_three_directory,
            behavior_directory,
            report_path,
        )
        if path.exists()
    ]
    if occupied:
        names = ", ".join(path.name for path in occupied)
        raise BootstrapError(f"bootstrap output already exists: {names}")

    source_snapshot = capture_source_snapshot(source_root, plan)
    source_snapshot_digest = _source_snapshot_sha256(source_snapshot)
    runner = ToolRunner(source_root)
    seed_producers = {
        name: seed_tools[name] for name in PRODUCER_NAMES
    }
    stage_two = _build_stage(
        runner,
        source_root,
        stage_two_directory,
        seed_producers,
        plan,
        "stage two",
    )
    require_source_snapshot(source_root, plan, source_snapshot)
    stage_two_producers = {
        name: stage_two.tools[name] for name in PRODUCER_NAMES
    }
    stage_three = _build_stage(
        runner,
        source_root,
        stage_three_directory,
        stage_two_producers,
        plan,
        "stage three",
    )
    require_source_snapshot(source_root, plan, source_snapshot)
    raw_sources = _require_list(plan.get("sources"), "build_plan.sources")
    source_names = [
        str(_require_object(source, "build source")["name"])
        for source in raw_sources
    ]
    comparisons = _compare_stages(
        stage_two, stage_three, source_names
    )
    behavior = _run_behavior_checks(
        runner,
        source_root,
        output_root,
        stage_two,
        stage_three,
    )
    require_source_snapshot(source_root, plan, source_snapshot)
    seed_matches_stage_two = {
        name: seed_tools[name].read_bytes()
        == stage_two.tools[name].read_bytes()
        for name in TOOL_NAMES
    }
    report: dict[str, object] = {
        "behavior": behavior,
        "build_plan_sha256": manifest["build_plan_sha256"],
        "comparisons": comparisons,
        "initial_seed_matches_stage_two": seed_matches_stage_two,
        "platform": runner.platform_name,
        "schema": REPORT_SCHEMA,
        "seed_manifest_sha256": seed_inputs.manifest_sha256,
        "seed_source_revision": SEED_SOURCE_REVISION,
        "source_inputs": {
            "count": len(source_snapshot),
            "files": source_snapshot,
            "sha256": source_snapshot_digest,
        },
        "source_snapshot_sha256": source_snapshot_digest,
        "stages": {
            "stage-three": {
                "objects": _artifact_inventory(stage_three.objects),
                "producer_generation": "stage-two",
                "tools": _artifact_inventory(stage_three.tools),
            },
            "stage-two": {
                "objects": _artifact_inventory(stage_two.objects),
                "producer_generation": "checked-seed",
                "tools": _artifact_inventory(stage_two.tools),
            },
        },
        "status": "pass",
        "target": EXPECTED_TARGET,
    }
    encoded_report = (
        json.dumps(report, indent=2, sort_keys=True, ensure_ascii=True)
        + "\n"
    ).encode("ascii")
    temporary_report = output_root / "bootstrap-report.json.tmp"
    temporary_report.write_bytes(encoded_report)
    temporary_report.replace(report_path)
    return report


def bootstrap_from_seed(
    manifest_path: Path,
    source_root: Path,
    output_root: Path,
) -> dict[str, object]:
    with tempfile.TemporaryDirectory(
        prefix="cupid-bootstrap-seed-inputs-"
    ) as temporary:
        seed_inputs = freeze_seed_inputs(
            manifest_path, Path(temporary)
        )
        return _bootstrap_from_frozen_seed(
            seed_inputs, source_root, output_root
        )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Verify and consume the checked Cupid Toolchain seed."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser(
        "verify", help="verify the checked seed without executing it"
    )
    verify.add_argument("--manifest", required=True, type=Path)
    bootstrap = subparsers.add_parser(
        "bootstrap",
        help="build and compare stage two and stage three from the seed",
    )
    bootstrap.add_argument("--manifest", required=True, type=Path)
    bootstrap.add_argument("--root", required=True, type=Path)
    bootstrap.add_argument("--output", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        if arguments.command == "verify":
            tools = verify_seed_manifest(arguments.manifest)
            print(f"checked i386 Linux seed: ok ({len(tools)} tools)")
            return 0
        if arguments.command == "bootstrap":
            bootstrap_from_seed(
                arguments.manifest, arguments.root, arguments.output
            )
            print(
                "checked i386 Linux bootstrap: ok "
                "(stage two equals stage three)"
            )
            return 0
    except BootstrapError as error:
        if arguments.command == "verify":
            prefix = "bootstrap seed verification failed"
        else:
            prefix = "checked bootstrap failed"
        print(f"{prefix}: {error}", file=sys.stderr)
        return 1
    raise AssertionError(f"unhandled command: {arguments.command}")


if __name__ == "__main__":
    raise SystemExit(main())

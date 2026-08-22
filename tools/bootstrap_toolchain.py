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
WINDOWS_SEED_SCHEMA = "cupid.execution-seed.v1"
SEED_SOURCE_REVISION = "30aaf1b7cd398e6b47a395661a33d20d00363158"
SEED_SOURCE_INPUT_COUNT = 50
SEED_SOURCE_SNAPSHOT_SHA256 = (
    "2b56c849dd203b386c93fab3a07def099c49c9a6464e342ee55e9641281788f9"
)
TOOL_NAMES = ("cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc")
TOOL_DISPLAY_NAMES = {
    "cupidasm": "CupidASM",
    "cupiddis": "CupidDis",
    "cupidld": "CupidLD",
    "cupidobj": "CupidObj",
    "cupidc": "CupidC",
}
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
EXPECTED_WINDOWS_TARGET = {
    "abi": "windows-stdcall-imports",
    "architecture": "i386",
    "byte_order": "little",
    "entry": 0x00401000,
    "linkage": "kernel32-imports",
    "operating_system": "windows",
    "pe_class": 32,
}
EXPECTED_PRODUCER_LINEAGE = {
    "assembly": "stage-three CupidASM from the checked-seed bootstrap",
    "c": "stage-three CupidC from the checked-seed bootstrap",
    "link": "stage-three CupidLD from the checked-seed bootstrap",
}
EXPECTED_WINDOWS_PRODUCER_LINEAGE = {
    "assembly": (
        "native stage-three CupidASM from the checked i386 Windows bootstrap"
    ),
    "c": "native stage-three CupidC from the checked i386 Windows bootstrap",
    "link": (
        "native stage-three CupidLD from the checked i386 Windows bootstrap"
    ),
}
WINDOWS_SEED_SOURCE_REVISION = (
    "30aaf1b7cd398e6b47a395661a33d20d00363158"
)
WINDOWS_SEED_SOURCE_SNAPSHOT_SHA256 = (
    "2b56c849dd203b386c93fab3a07def099c49c9a6464e342ee55e9641281788f9"
)
WINDOWS_SEED_PARENT_MANIFEST_SHA256 = (
    "afc56e3654ad7fe4447b31c87f1a010d9c13e89b824357db60b8a73648ad009c"
)
WINDOWS_SEED_PARENT_SOURCE_REVISION = (
    "30aaf1b7cd398e6b47a395661a33d20d00363158"
)
WINDOWS_TOOL_IMPORTS = (
    (
        "KERNEL32.dll",
        (
            "CloseHandle",
            "CreateFileA",
            "ExitProcess",
            "GetCommandLineA",
            "GetCurrentDirectoryA",
            "GetLastError",
            "GetStdHandle",
            "ReadFile",
            "SetFilePointer",
            "VirtualAlloc",
            "VirtualFree",
            "WriteFile",
        ),
    ),
)
WINDOWS_LINKER_IMPORTS = (
    (
        "KERNEL32.dll",
        (
            "CloseHandle",
            "CreateFileA",
            "DeleteFileA",
            "ExitProcess",
            "FlushFileBuffers",
            "GetCommandLineA",
            "GetCurrentDirectoryA",
            "GetFullPathNameA",
            "GetLastError",
            "GetStdHandle",
            "MoveFileExA",
            "ReadFile",
            "SetFilePointer",
            "VirtualAlloc",
            "VirtualFree",
            "WriteFile",
        ),
    ),
)
EXPECTED_LINUX_FIXED_POINT_COMMAND = "make bootstrap-from-seed"
EXPECTED_WINDOWS_FIXED_POINT_COMMAND = "make bootstrap-windows-from-seed"
EXPECTED_INCLUDE_ARGUMENTS = (
    "-I",
    "/toolchain",
    "--include-angle",
    "/toolchain/hosted/i386-linux/include",
)
EXPECTED_STARTUP = "/toolchain/hosted/i386-linux/start.asm"
EXPECTED_SOURCES = (
    ("runtime", "/toolchain/hosted/i386-linux/runtime.cc", True),
    ("ctool", "/toolchain/ctool.cc", False),
    ("ctool_host", "/toolchain/ctool_host.cc", False),
    ("elf32", "/toolchain/elf32.cc", False),
    ("x86", "/toolchain/x86.cc", False),
    ("cupidasm", "/toolchain/cupidasm.cc", False),
    ("cupidasm_main", "/toolchain/cupidasm_main.cc", False),
    ("cupiddis", "/toolchain/cupiddis.cc", False),
    ("cupiddis_main", "/toolchain/cupiddis_main.cc", False),
    ("cupidobj", "/toolchain/cupidobj.cc", False),
    ("cupidobj_main", "/toolchain/cupidobj_main.cc", False),
    ("cupidld", "/toolchain/cupidld.cc", False),
    ("cupidld_main", "/toolchain/cupidld_main.cc", False),
    ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
    ("cupidc_type", "/toolchain/cupidc_type.cc", False),
    ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
    ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
    ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
    ("cupidc_main", "/toolchain/cupidc_main.cc", False),
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
WINDOWS_REPORT_SCHEMA = "cupid.windows-bootstrap-report.v1"
BOOTSTRAP_PUBLICATION_NAMES = (
    "stage-two",
    "stage-three",
    "stage-four",
    "behavior",
    "bootstrap-report.json",
)
WINDOWS_COMPILE_DEFINES = frozenset(
    {
        "ctool_host",
        "cupidasm_main",
        "cupidc_main",
        "cupiddis_main",
        "cupidld_main",
        "cupidobj_main",
        "publication_runtime",
    }
)
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
    manifest_bytes: bytes
    manifest_sha256: str
    live_manifest_path: Path
    artifact_bytes: tuple[tuple[str, bytes], ...]
    tools: dict[str, Path]


@dataclass(frozen=True)
class SourceInputs:
    root: Path
    inventory: dict[str, dict[str, object]]


class ToolRunner:
    """Run checked i386 tools through their native host boundary."""

    def __init__(self, working_directory: Path):
        self.working_directory = working_directory.resolve()
        self.uses_wsl = os.name == "nt"

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
        try:
            signature = executable.read_bytes()[:4]
        except OSError as error:
            raise BootstrapError(
                f"cannot read checked tool {executable}: {error}"
            ) from error
        is_linux_elf = signature == b"\x7fELF"
        is_windows_pe = signature[:2] == b"MZ"
        if not is_linux_elf and not is_windows_pe:
            raise BootstrapError(
                f"checked tool has an unsupported executable format: "
                f"{executable.name}"
            )
        if os.name != "nt" and is_windows_pe:
            raise BootstrapError(
                "the i386 Windows seed requires a Windows host"
            )
        if os.name == "nt" and is_linux_elf:
            if shutil.which("wsl") is None:
                raise BootstrapError(
                    "WSL is required to run the i386 Linux seed on Windows"
                )
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
    paths.append(
        source_root / "toolchain/hosted/i386-windows/start.asm"
    )
    paths.append(
        source_root / "toolchain/hosted/i386-windows/runtime.cc"
    )
    paths.append(
        source_root / "toolchain/hosted/i386-windows/tool_start.asm"
    )
    paths.append(
        source_root
        / "toolchain/hosted/i386-windows/publication_runtime.cc"
    )
    paths.append(
        source_root
        / "toolchain/hosted/i386-windows/publication_start.asm"
    )
    paths.append(
        source_root / "toolchain/tests/hosted_i386_windows_contract.cc"
    )
    paths.append(
        source_root
        / "toolchain/tests/hosted_i386_windows_runtime_contract.cc"
    )

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


def freeze_source_inputs(
    source_root: Path,
    plan: dict[str, object],
    snapshot_directory: Path,
) -> SourceInputs:
    """Copy one exact source closure into a private compiler root."""
    if snapshot_directory.is_symlink():
        raise BootstrapError("frozen source directory may not be a symlink")
    if snapshot_directory.exists():
        if not snapshot_directory.is_dir():
            raise BootstrapError("frozen source path is not a directory")
        if any(snapshot_directory.iterdir()):
            raise BootstrapError("frozen source directory is not empty")
    else:
        snapshot_directory.mkdir(mode=0o700)
    snapshot_directory.chmod(0o700)
    snapshot_root = snapshot_directory.resolve()

    inventory: dict[str, dict[str, object]] = {}
    for name, path in sorted(_source_input_paths(source_root, plan).items()):
        try:
            data = path.read_bytes()
        except OSError as error:
            raise BootstrapError(
                f"cannot read source input {name}: {error}"
            ) from error
        destination = snapshot_root / name
        try:
            destination.parent.mkdir(
                mode=0o700, parents=True, exist_ok=True
            )
            destination.write_bytes(data)
            destination.chmod(0o600)
            frozen_data = destination.read_bytes()
        except OSError as error:
            raise BootstrapError(
                f"cannot freeze source input {name}: {error}"
            ) from error
        if frozen_data != data:
            raise BootstrapError(f"frozen source input differs: {name}")
        inventory[name] = {
            "sha256": hashlib.sha256(data).hexdigest(),
            "size": len(data),
        }
    frozen = SourceInputs(root=snapshot_root, inventory=inventory)
    require_frozen_source_snapshot(frozen, plan)
    return frozen


def _source_snapshot_sha256(
    inventory: dict[str, dict[str, object]],
) -> str:
    encoded = json.dumps(
        inventory, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _require_source_snapshot(
    source_root: Path,
    plan: dict[str, object],
    expected: dict[str, dict[str, object]],
    label: str,
) -> None:
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
        f"{label} changed during bootstrap: {visible}"
    )


def require_source_snapshot(
    source_root: Path,
    plan: dict[str, object],
    expected: dict[str, dict[str, object]],
) -> None:
    """Reject a build whose live source inputs changed after capture."""
    _require_source_snapshot(source_root, plan, expected, "source inputs")


def require_frozen_source_snapshot(
    source_inputs: SourceInputs,
    plan: dict[str, object],
) -> None:
    """Reject mutation of the private source closure."""
    _require_source_snapshot(
        source_inputs.root,
        plan,
        source_inputs.inventory,
        "frozen source inputs",
    )


def require_source_closures(
    source_inputs: SourceInputs,
    live_source_root: Path,
    plan: dict[str, object],
) -> None:
    """Rehash the private compiler root and the live source closure."""
    require_frozen_source_snapshot(source_inputs, plan)
    require_source_snapshot(
        live_source_root, plan, source_inputs.inventory
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


_FIXED_PE32_DOS_STUB = bytes.fromhex(
    "4d5a90000300000004000000ffff0000"
    "b8000000000000004000000000000000"
    "00000000000000000000000000000000"
    "00000000000000000000000080000000"
    "0e1fba0e00b409cd21b8014ccd215468"
    "69732070726f6772616d2063616e6e6f"
    "742062652072756e20696e20444f5320"
    "6d6f64652e0d0d0a2400000000000000"
)


def _validate_static_i386_pe32_bytes(
    data: bytes,
    name: str,
    expected_entry: int,
    expected_imports: Sequence[tuple[str, Sequence[str]]] = (),
) -> None:
    has_imports = bool(expected_imports)

    def require_range(offset: int, size: int, label: str) -> None:
        if offset < 0 or size < 0 or offset > len(data) - size:
            raise BootstrapError(f"{name} has a truncated {label}")

    def read_u16(offset: int, label: str) -> int:
        require_range(offset, 2, label)
        return struct.unpack_from("<H", data, offset)[0]

    def read_u32(offset: int, label: str) -> int:
        require_range(offset, 4, label)
        return struct.unpack_from("<I", data, offset)[0]

    require_range(0, len(_FIXED_PE32_DOS_STUB), "DOS header")
    if data[: len(_FIXED_PE32_DOS_STUB)] != _FIXED_PE32_DOS_STUB:
        raise BootstrapError(f"{name} has a noncanonical DOS stub")
    pe_offset = read_u32(0x3C, "DOS PE offset")
    require_range(pe_offset, 24, "PE and COFF headers")
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise BootstrapError(f"{name} has no PE signature")
    (
        machine,
        section_count,
        timestamp,
        symbol_table,
        symbol_count,
        optional_size,
        characteristics,
    ) = struct.unpack_from("<HHIIIHH", data, pe_offset + 4)
    if (
        machine != 0x014C
        or section_count == 0
        or section_count > (5 if has_imports else 4)
        or timestamp != 0
        or symbol_table != 0
        or symbol_count != 0
        or optional_size != 0x00E0
        or characteristics != 0x0103
    ):
        raise BootstrapError(f"{name} has an invalid PE32 COFF header")
    optional_offset = pe_offset + 24
    require_range(optional_offset, optional_size, "PE32 optional header")
    if read_u16(optional_offset, "PE32 magic") != 0x010B:
        raise BootstrapError(f"{name} is not PE32")
    linker_major = data[optional_offset + 2]
    linker_minor = data[optional_offset + 3]
    code_size = read_u32(optional_offset + 4, "PE32 code size")
    initialized_size = read_u32(
        optional_offset + 8, "PE32 initialized data size"
    )
    uninitialized_size = read_u32(
        optional_offset + 12, "PE32 uninitialized data size"
    )
    entry_rva = read_u32(optional_offset + 16, "PE32 entry RVA")
    base_of_code = read_u32(optional_offset + 20, "PE32 code base")
    base_of_data = read_u32(optional_offset + 24, "PE32 data base")
    image_base = read_u32(optional_offset + 28, "PE32 image base")
    section_alignment = read_u32(
        optional_offset + 32, "PE32 section alignment"
    )
    file_alignment = read_u32(
        optional_offset + 36, "PE32 file alignment"
    )
    image_size = read_u32(optional_offset + 56, "PE32 image size")
    headers_size = read_u32(optional_offset + 60, "PE32 header size")
    checksum = read_u32(optional_offset + 64, "PE32 checksum")
    subsystem = read_u16(optional_offset + 68, "PE32 subsystem")
    dll_characteristics = read_u16(
        optional_offset + 70, "PE32 DLL characteristics"
    )
    stack_reserve = read_u32(optional_offset + 72, "PE32 stack reserve")
    stack_commit = read_u32(optional_offset + 76, "PE32 stack commit")
    heap_reserve = read_u32(optional_offset + 80, "PE32 heap reserve")
    heap_commit = read_u32(optional_offset + 84, "PE32 heap commit")
    loader_flags = read_u32(optional_offset + 88, "PE32 loader flags")
    directory_count = read_u32(
        optional_offset + 92, "PE32 directory count"
    )
    if (
        linker_major != 0
        or linker_minor != 0
        or image_base != 0x00400000
        or section_alignment != 0x1000
        or file_alignment != 0x0200
        or read_u16(optional_offset + 40, "PE32 OS major version") != 6
        or read_u16(optional_offset + 42, "PE32 OS minor version") != 0
        or read_u16(optional_offset + 44, "PE32 image major version") != 0
        or read_u16(optional_offset + 46, "PE32 image minor version") != 0
        or read_u16(optional_offset + 48, "PE32 subsystem major version")
        != 6
        or read_u16(optional_offset + 50, "PE32 subsystem minor version")
        != 0
        or read_u32(optional_offset + 52, "PE32 Win32 version") != 0
        or image_size == 0
        or image_size % section_alignment != 0
        or headers_size == 0
        or headers_size % file_alignment != 0
        or headers_size > len(data)
        or checksum != 0
        or subsystem != 3
        or dll_characteristics != 0x0100
        or stack_reserve != 0x00100000
        or stack_commit != 0x00100000
        or heap_reserve != 0x00100000
        or heap_commit != 0x00001000
        or loader_flags != 0
        or directory_count != 16
        or expected_entry < image_base
        or entry_rva != expected_entry - image_base
    ):
        raise BootstrapError(f"{name} has an invalid PE32 image layout")
    directories = []
    for directory in range(directory_count):
        offset = optional_offset + 96 + directory * 8
        entry = (
            read_u32(offset, "PE32 directory RVA"),
            read_u32(offset + 4, "PE32 directory size"),
        )
        directories.append(entry)
        if directory not in ((1, 12) if has_imports else ()) and entry != (
            0,
            0,
        ):
            raise BootstrapError(
                f"{name} has an unexpected PE32 data directory"
            )
    if has_imports and (directories[1] == (0, 0) or directories[12] == (0, 0)):
        raise BootstrapError(f"{name} omits its PE32 import directories")
    section_offset = optional_offset + optional_size
    require_range(
        section_offset, section_count * 40, "PE32 section table"
    )
    section_table_end = section_offset + section_count * 40
    expected_headers_size = (
        (section_table_end + file_alignment - 1)
        // file_alignment
        * file_alignment
    )
    if headers_size != expected_headers_size or any(
        data[section_table_end:headers_size]
    ):
        raise BootstrapError(
            f"{name} has a noncanonical PE32 header extent"
        )
    expected_sections = {
        ".text": (0, 0x60000020),
        ".rodata": (1, 0x40000040),
        ".data": (2, 0xC0000040),
        ".bss": (3, 0xC0000080),
    }
    if has_imports:
        expected_sections[".idata"] = (4, 0xC0000040)
    entry_is_file_backed_executable = False
    previous_section_rank = -1
    expected_virtual_address = section_alignment
    expected_raw_offset = headers_size
    greatest_virtual_end = headers_size
    expected_code_size = 0
    expected_initialized_size = 0
    expected_uninitialized_size = 0
    expected_base_of_code = 0
    expected_base_of_data = 0
    sections = {}
    for index in range(section_count):
        offset = section_offset + index * 40
        raw_name = data[offset : offset + 8]
        name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
        virtual_size, virtual_address, raw_size, raw_offset = (
            struct.unpack_from("<IIII", data, offset + 8)
        )
        sections[name] = (
            virtual_address,
            virtual_size,
            raw_offset,
            raw_size,
        )
        section_characteristics = read_u32(
            offset + 36, "PE32 section characteristics"
        )
        expected = expected_sections.get(name)
        if (
            expected is None
            or expected[0] <= previous_section_rank
            or section_characteristics != expected[1]
            or raw_name != name.encode("ascii").ljust(8, b"\0")
            or read_u32(offset + 24, "PE32 relocation offset") != 0
            or read_u32(offset + 28, "PE32 line offset") != 0
            or read_u16(offset + 32, "PE32 relocation count") != 0
            or read_u16(offset + 34, "PE32 line count") != 0
        ):
            raise BootstrapError(
                f"{name} has an invalid PE32 section profile"
            )
        previous_section_rank = expected[0]
        if virtual_size == 0:
            raise BootstrapError(f"{name} has an empty PE32 section")
        if virtual_address != expected_virtual_address:
            raise BootstrapError(
                f"{name} has a noncanonical PE32 section address"
            )
        virtual_end = virtual_address + virtual_size
        if virtual_end > image_size:
            raise BootstrapError(
                f"{name} has a PE32 section outside its image"
            )
        greatest_virtual_end = max(greatest_virtual_end, virtual_end)
        expected_virtual_address = (
            (virtual_address + virtual_size + section_alignment - 1)
            // section_alignment
            * section_alignment
        )
        if raw_size != 0:
            expected_section_raw_size = (
                (virtual_size + file_alignment - 1)
                // file_alignment
                * file_alignment
            )
            if (
                name == ".bss"
                or raw_offset != expected_raw_offset
                or raw_offset < headers_size
                or raw_size != expected_section_raw_size
                or virtual_size > raw_size
            ):
                raise BootstrapError(
                    f"{name} has an invalid PE32 file section"
                )
            require_range(raw_offset, raw_size, "PE32 section data")
            if any(data[raw_offset + virtual_size : raw_offset + raw_size]):
                raise BootstrapError(
                    f"{name} has nonzero PE32 section padding"
                )
            expected_raw_offset = raw_offset + raw_size
        elif raw_offset != 0 or name != ".bss":
            raise BootstrapError(
                f"{name} has an invalid empty PE32 section"
            )
        if name == ".text":
            expected_code_size += raw_size
            expected_base_of_code = virtual_address
        elif name == ".bss":
            expected_uninitialized_size += virtual_size
            if expected_base_of_data == 0:
                expected_base_of_data = virtual_address
        else:
            expected_initialized_size += raw_size
            if expected_base_of_data == 0:
                expected_base_of_data = virtual_address
        if (
            section_characteristics & 0x20000000
            and entry_rva >= virtual_address
            and entry_rva - virtual_address < min(virtual_size, raw_size)
        ):
            entry_is_file_backed_executable = True
    expected_image_size = (
        (greatest_virtual_end + section_alignment - 1)
        // section_alignment
        * section_alignment
    )
    if (
        previous_section_rank < 0
        or expected_raw_offset != len(data)
        or expected_image_size != image_size
        or code_size != expected_code_size
        or initialized_size != expected_initialized_size
        or uninitialized_size != expected_uninitialized_size
        or base_of_code != expected_base_of_code
        or base_of_data != expected_base_of_data
    ):
        raise BootstrapError(f"{name} has an invalid PE32 image extent")
    if not entry_is_file_backed_executable:
        raise BootstrapError(
            f"{name} entry is not file-backed PE32 executable code"
        )
    if has_imports:
        if ".idata" not in sections:
            raise BootstrapError(f"{name} omits its PE32 import section")

        (
            idata_virtual_address,
            idata_virtual_size,
            idata_raw_offset,
            idata_raw_size,
        ) = sections[".idata"]

        def rva_extent(
            rva: int, size: int, label: str
        ) -> tuple[int, int]:
            relative_rva = rva - idata_virtual_address
            if (
                idata_raw_size
                and relative_rva >= 0
                and size <= idata_raw_size - relative_rva
                and size <= idata_virtual_size - relative_rva
            ):
                return (
                    idata_raw_offset + relative_rva,
                    idata_raw_offset
                    + min(idata_raw_size, idata_virtual_size),
                )
            raise BootstrapError(f"{name} has an invalid {label} RVA")

        def rva_offset(rva: int, size: int, label: str) -> int:
            return rva_extent(rva, size, label)[0]

        def rva_string(rva: int, label: str) -> str:
            offset, section_end = rva_extent(rva, 1, label)
            end = data.find(b"\0", offset, section_end)
            if end < 0:
                raise BootstrapError(
                    f"{name} has an unterminated {label}"
                )
            try:
                return data[offset:end].decode("ascii")
            except UnicodeDecodeError as error:
                raise BootstrapError(
                    f"{name} has a non-ASCII {label}"
                ) from error

        import_rva, import_size = directories[1]
        expected_import_size = (len(expected_imports) + 1) * 20
        if (
            import_rva != idata_virtual_address
            or import_size != expected_import_size
        ):
            raise BootstrapError(
                f"{name} has a noncanonical PE32 import directory"
            )
        descriptor_offset = rva_offset(
            import_rva, expected_import_size, "import directory"
        )
        descriptors: list[tuple[int, int, int]] = []
        for library_index, (library, procedures) in enumerate(
            expected_imports
        ):
            descriptor = struct.unpack_from(
                "<IIIII", data, descriptor_offset + library_index * 20
            )
            lookup_rva, timestamp, forwarder, name_rva, iat_rva = descriptor
            if timestamp != 0 or forwarder != 0:
                raise BootstrapError(
                    f"{name} has a stateful PE32 import descriptor"
                )
            descriptors.append((lookup_rva, name_rva, iat_rva))

        if any(
            data[
                descriptor_offset + len(expected_imports) * 20 :
                descriptor_offset + (len(expected_imports) + 1) * 20
            ]
        ):
            raise BootstrapError(
                f"{name} has no null PE32 import descriptor"
            )

        cursor = expected_import_size
        lookup_offsets: list[int] = []
        for (lookup_rva, _name_rva, _iat_rva), (
            _library,
            procedures,
        ) in zip(descriptors, expected_imports):
            thunk_size = (len(procedures) + 1) * 4
            if lookup_rva != idata_virtual_address + cursor:
                raise BootstrapError(
                    f"{name} has a noncanonical PE32 import lookup layout"
                )
            lookup_offsets.append(
                rva_offset(lookup_rva, thunk_size, "import lookup table")
            )
            cursor += thunk_size

        first_iat = idata_virtual_address + cursor
        iat_offsets: list[int] = []
        for (_lookup_rva, _name_rva, iat_rva), (
            _library,
            procedures,
        ) in zip(descriptors, expected_imports):
            thunk_size = (len(procedures) + 1) * 4
            if iat_rva != idata_virtual_address + cursor:
                raise BootstrapError(
                    f"{name} has a noncanonical PE32 import address layout"
                )
            iat_offsets.append(
                rva_offset(iat_rva, thunk_size, "import address table")
            )
            cursor += thunk_size
        iat_end = idata_virtual_address + cursor

        for (_lookup_rva, name_rva, _iat_rva), (
            library,
            _procedures,
        ) in zip(descriptors, expected_imports):
            encoded_library = library.encode("ascii") + b"\0"
            if name_rva != idata_virtual_address + cursor:
                raise BootstrapError(
                    f"{name} has a noncanonical PE32 import name layout"
                )
            library_offset = rva_offset(
                name_rva, len(encoded_library), "import library"
            )
            if rva_string(name_rva, "import library") != library or data[
                library_offset : library_offset + len(encoded_library)
            ] != encoded_library:
                raise BootstrapError(
                    f"{name} has an unexpected PE32 import library"
                )
            cursor += len(encoded_library)

        for library_index, (_library, procedures) in enumerate(
            expected_imports
        ):
            lookup_offset = lookup_offsets[library_index]
            iat_offset = iat_offsets[library_index]
            for procedure_index, procedure in enumerate(procedures):
                lookup = read_u32(
                    lookup_offset + procedure_index * 4,
                    "PE32 import lookup entry",
                )
                iat = read_u32(
                    iat_offset + procedure_index * 4,
                    "PE32 import address entry",
                )
                if cursor & 1:
                    alignment_offset = rva_offset(
                        idata_virtual_address + cursor,
                        1,
                        "import name alignment",
                    )
                    if data[alignment_offset] != 0:
                        raise BootstrapError(
                            f"{name} has nonzero PE32 import alignment"
                        )
                    cursor += 1
                expected_hint_rva = idata_virtual_address + cursor
                encoded_procedure = procedure.encode("ascii") + b"\0"
                hint_offset = rva_offset(
                    expected_hint_rva,
                    2 + len(encoded_procedure),
                    "import hint and name",
                )
                if (
                    lookup != expected_hint_rva
                    or iat != lookup
                    or read_u16(hint_offset, "PE32 import hint") != 0
                    or data[
                        hint_offset + 2 :
                        hint_offset + 2 + len(encoded_procedure)
                    ]
                    != encoded_procedure
                    or rva_string(expected_hint_rva + 2, "import procedure")
                    != procedure
                ):
                    raise BootstrapError(
                        f"{name} has an unexpected PE32 import procedure"
                    )
                cursor += 2 + len(encoded_procedure)
            if (
                read_u32(
                    lookup_offset + len(procedures) * 4,
                    "PE32 import lookup terminator",
                )
                != 0
                or read_u32(
                    iat_offset + len(procedures) * 4,
                    "PE32 import address terminator",
                )
                != 0
            ):
                raise BootstrapError(
                    f"{name} has an unterminated PE32 import thunk table"
                )

        if cursor != idata_virtual_size:
            raise BootstrapError(
                f"{name} has a noncanonical PE32 import section extent"
            )
        if directories[12] != (first_iat, iat_end - first_iat):
            raise BootstrapError(
                f"{name} has a noncanonical PE32 IAT directory"
            )


def _validate_static_i386_pe32(
    path: Path,
    expected_entry: int,
    expected_imports: Sequence[tuple[str, Sequence[str]]] = (),
) -> None:
    _validate_static_i386_pe32_bytes(
        path.read_bytes(), path.name, expected_entry, expected_imports
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
            or not logical_path.endswith(".cc")
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
) -> tuple[dict[str, Path], dict[str, bytes]]:
    schema = manifest.get("schema")
    is_windows_seed = schema == WINDOWS_SEED_SCHEMA
    _require_exact_keys(
        manifest,
        (
            {"artifacts", "provenance", "schema", "target"}
            if is_windows_seed
            else {
                "artifacts",
                "build_plan",
                "build_plan_sha256",
                "provenance",
                "schema",
                "target",
            }
        ),
        "manifest",
    )
    if schema not in (SEED_SCHEMA, WINDOWS_SEED_SCHEMA):
        raise BootstrapError("manifest schema is unsupported")
    expected_target = (
        EXPECTED_WINDOWS_TARGET if is_windows_seed else EXPECTED_TARGET
    )
    target = _require_object(manifest.get("target"), "target")
    _require_exact_keys(target, set(expected_target), "manifest target")
    for field, expected in expected_target.items():
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
    if is_windows_seed:
        _require_exact_keys(
            provenance,
            {
                "artifact_generation",
                "fixed_point_command",
                "fixed_point_result",
                "parent_seed_manifest_sha256",
                "parent_seed_source_revision",
                "producer_lineage",
                "source_input_count",
                "source_revision",
                "source_snapshot_sha256",
            },
            "provenance",
        )
        if provenance.get("artifact_generation") != (
            "paired-stage-four-native-windows"
        ):
            raise BootstrapError("Windows seed generation differs")
        if provenance.get("source_revision") != (
            WINDOWS_SEED_SOURCE_REVISION
        ):
            raise BootstrapError("source revision differs")
        if provenance.get("source_snapshot_sha256") != (
            WINDOWS_SEED_SOURCE_SNAPSHOT_SHA256
        ):
            raise BootstrapError("source snapshot differs")
        source_input_count = provenance.get("source_input_count")
        if type(source_input_count) is not int or source_input_count != 50:
            raise BootstrapError("source input count differs")
        if provenance.get("parent_seed_source_revision") != (
            WINDOWS_SEED_PARENT_SOURCE_REVISION
        ):
            raise BootstrapError("parent seed source revision differs")
        if provenance.get("parent_seed_manifest_sha256") != (
            WINDOWS_SEED_PARENT_MANIFEST_SHA256
        ):
            raise BootstrapError("parent seed manifest differs")
        if provenance.get("producer_lineage") != (
            EXPECTED_WINDOWS_PRODUCER_LINEAGE
        ):
            raise BootstrapError("producer lineage differs")
    else:
        _require_exact_keys(
            provenance,
            {
                "fixed_point_command",
                "fixed_point_result",
                "producer_lineage",
                "seed_generation",
                "source_input_count",
                "source_revision",
                "source_snapshot_sha256",
            },
            "provenance",
        )
        revision = provenance.get("source_revision")
        if revision != SEED_SOURCE_REVISION:
            raise BootstrapError("source revision differs")
        if provenance.get("seed_generation") != "stage-four":
            raise BootstrapError("seed generation differs")
        source_input_count = provenance.get("source_input_count")
        if (
            type(source_input_count) is not int
            or source_input_count != SEED_SOURCE_INPUT_COUNT
        ):
            raise BootstrapError("source input count differs")
        if provenance.get("source_snapshot_sha256") != (
            SEED_SOURCE_SNAPSHOT_SHA256
        ):
            raise BootstrapError("source snapshot differs")
        if provenance.get("producer_lineage") != EXPECTED_PRODUCER_LINEAGE:
            raise BootstrapError("producer lineage differs")

    if provenance.get("fixed_point_result") != "pass":
        raise BootstrapError("seed lacks passing fixed-point provenance")
    expected_fixed_point_command = (
        EXPECTED_WINDOWS_FIXED_POINT_COMMAND
        if is_windows_seed
        else EXPECTED_LINUX_FIXED_POINT_COMMAND
    )
    if provenance.get("fixed_point_command") != expected_fixed_point_command:
        raise BootstrapError("fixed-point command differs")

    if not is_windows_seed:
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
    captured_artifacts: dict[str, bytes] = {}
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
        expected_file_name = f"{name}{'.exe' if is_windows_seed else '.elf'}"
        if (
            not isinstance(file_name, str)
            or Path(file_name).name != file_name
            or file_name != expected_file_name
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
        if is_windows_seed:
            imports = (
                WINDOWS_LINKER_IMPORTS
                if name == "cupidld"
                else WINDOWS_TOOL_IMPORTS
            )
            _validate_static_i386_pe32_bytes(
                data,
                file_name,
                int(EXPECTED_WINDOWS_TARGET["entry"]),
                imports,
            )
        else:
            _validate_static_i386_elf_bytes(
                data, file_name, int(EXPECTED_TARGET["entry"])
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
        captured_artifacts[name] = data
        seen_files.add(file_name)
    if set(resolved) != set(TOOL_NAMES):
        raise BootstrapError("manifest tool set differs")
    seed_suffix = ".exe" if is_windows_seed else ".elf"
    actual_seed_files = {
        path.name
        for path in seed_directory.iterdir()
        if path.suffix.casefold() == seed_suffix
    }
    if actual_seed_files != seen_files:
        format_name = "PE32" if is_windows_seed else "ELF"
        raise BootstrapError(
            f"seed directory contains an unlisted {format_name} file"
        )
    return resolved, captured_artifacts


def _load_seed_inputs(
    manifest_path: Path,
    snapshot_directory: Path | None,
) -> SeedInputs:
    manifest, encoded_manifest = _read_manifest_capture(manifest_path)
    seed_directory = manifest_path.parent.resolve()
    tools, captured_artifacts = _verify_seed_manifest_data(
        manifest, seed_directory, snapshot_directory
    )
    return SeedInputs(
        manifest=manifest,
        manifest_bytes=encoded_manifest,
        manifest_sha256=hashlib.sha256(encoded_manifest).hexdigest(),
        live_manifest_path=manifest_path.absolute(),
        artifact_bytes=tuple(
            (name, captured_artifacts[name]) for name in TOOL_NAMES
        ),
        tools=tools,
    )


def verify_seed_inputs(manifest_path: Path) -> SeedInputs:
    """Verify one captured manifest and its listed seed tools."""
    return _load_seed_inputs(manifest_path, None)


def verify_seed_manifest(manifest_path: Path) -> dict[str, Path]:
    return verify_seed_inputs(manifest_path).tools


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


def require_live_seed_inputs(*seed_inputs: SeedInputs) -> None:
    """Require each live seed cohort to match its frozen capture."""
    for captured in seed_inputs:
        manifest_path = captured.live_manifest_path
        try:
            manifest_bytes = manifest_path.read_bytes()
        except OSError:
            manifest_bytes = b""
        manifest_changed = (
            manifest_path.is_symlink()
            or not manifest_path.is_file()
            or manifest_bytes != captured.manifest_bytes
        )

        changed_artifacts: list[str] = []
        for tool_name, expected_bytes in captured.artifact_bytes:
            file_name = captured.tools[tool_name].name
            live_path = manifest_path.parent / file_name
            try:
                live_bytes = live_path.read_bytes()
            except OSError:
                live_bytes = b""
            if (
                live_path.is_symlink()
                or not live_path.is_file()
                or live_bytes != expected_bytes
            ):
                changed_artifacts.append(file_name)

        if manifest_changed:
            raise BootstrapError(
                "checked seed manifest changed during bootstrap: "
                f"{manifest_path.name}"
            )
        if changed_artifacts:
            raise BootstrapError(
                "checked seed artifact changed during bootstrap: "
                f"{changed_artifacts[0]}"
            )


def _compare_seed_with_stage_two(
    seed_inputs: SeedInputs, stage_two: Stage
) -> dict[str, bool]:
    captured_artifacts = dict(seed_inputs.artifact_bytes)
    return {
        name: captured_artifacts[name] == stage_two.tools[name].read_bytes()
        for name in TOOL_NAMES
    }


def run_seed_tool(
    manifest_path: Path,
    working_directory: Path,
    tool_name: str,
    arguments: Sequence[str | Path],
    *,
    timeout: int = 300,
    frozen_seed: SeedInputs | None = None,
    runner: ToolRunner | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run one checked-seed tool from a new or caller-owned frozen capture."""
    if tool_name not in TOOL_NAMES:
        raise BootstrapError(
            f"checked seed has no tool named {tool_name}"
        )
    if timeout <= 0:
        raise BootstrapError("tool timeout must be positive")
    try:
        root = working_directory.resolve(strict=True)
    except OSError as error:
        raise BootstrapError(
            f"tool working directory cannot be resolved: "
            f"{working_directory}: {error}"
        ) from error
    if not root.is_dir():
        raise BootstrapError(
            f"tool working directory is not a directory: {root}"
        )

    display_name = TOOL_DISPLAY_NAMES[tool_name]

    def run_frozen(seed_inputs: SeedInputs) -> subprocess.CompletedProcess[str]:
        try:
            executable = seed_inputs.tools[tool_name]
        except KeyError as error:
            raise BootstrapError(
                f"frozen seed has no tool named {tool_name}"
            ) from error
        active_runner = runner if runner is not None else ToolRunner(root)
        try:
            result = active_runner.run(executable, arguments, timeout)
        except subprocess.TimeoutExpired as error:
            unit = "second" if timeout == 1 else "seconds"
            raise BootstrapError(
                f"{display_name} timed out after {timeout} {unit}"
            ) from error
        except OSError as error:
            raise BootstrapError(
                f"{display_name} could not run: {error}"
            ) from error
        try:
            live_seed = _load_seed_inputs(manifest_path, None)
        except BootstrapError as error:
            raise BootstrapError(
                f"checked seed inputs changed while "
                f"{display_name} ran: {error}"
            ) from error
        if live_seed.manifest_sha256 != seed_inputs.manifest_sha256:
            raise BootstrapError(
                f"checked seed inputs changed while {display_name} ran: "
                "manifest content differs"
            )
        return result

    if frozen_seed is not None:
        return run_frozen(frozen_seed)
    with tempfile.TemporaryDirectory(
        prefix=f"cupid-{tool_name}-seed-"
    ) as temporary:
        return run_frozen(
            freeze_seed_inputs(
                manifest_path,
                Path(temporary),
            )
        )


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


def _windows_imports(
    tool_name: str,
) -> tuple[tuple[str, tuple[str, ...]], ...]:
    return (
        WINDOWS_LINKER_IMPORTS
        if tool_name == "cupidld"
        else WINDOWS_TOOL_IMPORTS
    )


def _windows_import_selectors(tool_name: str) -> tuple[str, ...]:
    return tuple(
        f"__imp_{procedure}={library}:{procedure}"
        for library, procedures in _windows_imports(tool_name)
        for procedure in procedures
    )


def _windows_build_plan(
    linux_plan: dict[str, object],
) -> dict[str, object]:
    raw_sources = _require_list(
        linux_plan.get("sources"), "build_plan.sources"
    )
    sources: list[dict[str, object]] = []
    source_names: set[str] = set()
    for raw_source in raw_sources:
        source = _require_object(raw_source, "build source")
        name = str(source["name"])
        if name in source_names:
            raise BootstrapError(
                f"Linux build plan repeats a source name: {name}"
            )
        if name == "publication_runtime":
            raise BootstrapError(
                "Linux build plan uses the reserved Windows source name: "
                "publication_runtime"
            )
        source_names.add(name)
        path = str(source["path"])
        gnu_extensions = bool(source["gnu_extensions"])
        if name == "runtime":
            path = "/toolchain/hosted/i386-windows/runtime.cc"
            gnu_extensions = True
        sources.append(
            {
                "definitions": (
                    ["_WIN32=1"]
                    if name in WINDOWS_COMPILE_DEFINES
                    else []
                ),
                "gnu_extensions": gnu_extensions,
                "name": name,
                "path": path,
            }
        )
    sources.append(
        {
            "definitions": ["_WIN32=1"],
            "gnu_extensions": False,
            "name": "publication_runtime",
            "path": (
                "/toolchain/hosted/i386-windows/"
                "publication_runtime.cc"
            ),
        }
    )
    if "runtime" not in source_names:
        raise BootstrapError("Linux build plan omits its runtime source")

    raw_links = _require_object(linux_plan.get("links"), "build_plan.links")
    if set(raw_links) != set(TOOL_NAMES):
        raise BootstrapError("Linux build plan tool links differ")
    known_linux_objects = {"start", *source_names}
    links: dict[str, list[str]] = {}
    for tool_name in TOOL_NAMES:
        linux_order = [
            str(name)
            for name in _require_list(
                raw_links.get(tool_name),
                f"build_plan.links.{tool_name}",
            )
        ]
        for name in linux_order:
            if name not in known_linux_objects:
                raise BootstrapError(
                    "Linux build plan links an unknown object: "
                    f"{tool_name}: {name}"
                )
        if linux_order.count("start") != 1:
            raise BootstrapError(
                f"Linux build plan must link one startup: {tool_name}"
            )
        if linux_order.count("runtime") != 1:
            raise BootstrapError(
                f"Linux build plan must link one runtime: {tool_name}"
            )
        native_order = list(linux_order)
        if tool_name == "cupidld":
            native_order.insert(
                native_order.index("start") + 1,
                "publication_start",
            )
            native_order.insert(
                native_order.index("runtime"),
                "publication_runtime",
            )
        links[tool_name] = native_order

    include_arguments = [
        str(argument)
        for argument in _require_list(
            linux_plan.get("include_arguments"),
            "build_plan.include_arguments",
        )
    ]
    return {
        "assembly_sources": [
            {
                "name": "start",
                "path": (
                    "/toolchain/hosted/i386-windows/tool_start.asm"
                ),
            },
            {
                "name": "publication_start",
                "path": (
                    "/toolchain/hosted/i386-windows/"
                    "publication_start.asm"
                ),
            },
        ],
        "include_arguments": include_arguments,
        "imports": {
            name: [
                {
                    "library": library,
                    "procedure": procedure,
                    "slot": f"__imp_{procedure}",
                }
                for library, procedures in _windows_imports(name)
                for procedure in procedures
            ]
            for name in TOOL_NAMES
        },
        "links": links,
        "producer_tools": list(PRODUCER_NAMES),
        "sources": sources,
        "workers": int(linux_plan["workers"]),
    }


def _windows_link_arguments(
    tool_name: str,
    output: Path,
    objects: dict[str, Path],
    link_order: Sequence[str],
) -> list[str | Path]:
    arguments: list[str | Path] = [
        "-m",
        "i386pe",
        "--text-address",
        "0x00401000",
        "--entry",
        "_start",
    ]
    for selector in _windows_import_selectors(tool_name):
        arguments.extend(("--import", selector))
    arguments.extend(("-o", output))
    arguments.extend(objects[name] for name in link_order)
    return arguments


def _build_windows_stage(
    runner: ToolRunner,
    source_root: Path,
    stage_directory: Path,
    producers: dict[str, Path],
    native_plan: dict[str, object],
    stage_name: str,
) -> Stage:
    stage_directory.mkdir()
    raw_sources = _require_list(
        native_plan.get("sources"), "Windows build plan sources"
    )
    include_arguments = [
        str(argument)
        for argument in _require_list(
            native_plan.get("include_arguments"),
            "Windows build plan include arguments",
        )
    ]

    def compile_source(raw_source: object) -> tuple[str, Path]:
        source = _require_object(raw_source, "Windows build source")
        name = str(source["name"])
        logical_source = str(source["path"])
        object_path = stage_directory / f"{name}.o"
        arguments: list[str | Path] = ["--root", source_root]
        for definition in _require_list(
            source.get("definitions"),
            f"Windows build source definitions: {name}",
        ):
            arguments.extend(("-D", str(definition)))
        arguments.extend(
            ("-c", logical_source, *include_arguments)
        )
        if source["gnu_extensions"]:
            arguments.append("--gnu")
        arguments.extend(
            ("-o", _logical_path(source_root, object_path))
        )
        _run_clean(
            runner,
            producers["cupidc"],
            arguments,
            f"{stage_name} native CupidC for {logical_source}",
            360,
        )
        _validate_i386_relocatable(object_path)
        return name, object_path

    workers = int(native_plan["workers"])
    with ThreadPoolExecutor(max_workers=workers) as executor:
        compiled = list(executor.map(compile_source, raw_sources))
    objects = dict(compiled)

    raw_assembly = _require_list(
        native_plan.get("assembly_sources"),
        "Windows build plan assembly sources",
    )
    for raw_source in raw_assembly:
        source = _require_object(raw_source, "Windows assembly source")
        name = str(source["name"])
        source_path = source_root / str(source["path"]).lstrip("/")
        object_path = stage_directory / f"{name}.o"
        _run_clean(
            runner,
            producers["cupidasm"],
            ["-f", "elf32", source_path, "-o", object_path],
            f"{stage_name} native CupidASM for {source['path']}",
            120,
        )
        _validate_i386_relocatable(object_path)
        objects[name] = object_path

    tools: dict[str, Path] = {}
    raw_links = _require_object(
        native_plan.get("links"), "Windows build plan links"
    )
    if set(raw_links) != set(TOOL_NAMES):
        raise BootstrapError("Windows build plan tool links differ")
    for tool_name in TOOL_NAMES:
        link_order = [
            str(name)
            for name in _require_list(
                raw_links.get(tool_name),
                f"Windows build plan links: {tool_name}",
            )
        ]
        for name in link_order:
            if name not in objects:
                raise BootstrapError(
                    "Windows build plan links an unknown object: "
                    f"{tool_name}: {name}"
                )
        executable = stage_directory / f"{tool_name}.exe"
        _run_clean(
            runner,
            producers["cupidld"],
            _windows_link_arguments(
                tool_name, executable, objects, link_order
            ),
            f"{stage_name} native CupidLD for {tool_name}",
            180,
        )
        _validate_static_i386_pe32(
            executable,
            int(EXPECTED_WINDOWS_TARGET["entry"]),
            _windows_imports(tool_name),
        )
        tools[tool_name] = executable
    return Stage(objects=objects, tools=tools)


def _compare_windows_stages(
    stage_three: Stage,
    stage_four: Stage,
    c_source_names: Sequence[str],
    assembly_names: Sequence[str],
) -> dict[str, object]:
    for name in (*c_source_names, *assembly_names):
        if (
            stage_three.objects[name].read_bytes()
            != stage_four.objects[name].read_bytes()
        ):
            kind = "C" if name in c_source_names else "assembly"
            raise BootstrapError(
                f"native Windows {kind} object differs between stage "
                "three and stage four: "
                f"{name}"
            )
    for name in TOOL_NAMES:
        if (
            stage_three.tools[name].read_bytes()
            != stage_four.tools[name].read_bytes()
        ):
            raise BootstrapError(
                "native Windows tool image differs between stage three "
                f"and stage four: {name}"
            )
    return {
        "all_equal": True,
        "assembly_objects": len(assembly_names),
        "c_objects": len(c_source_names),
        "compared_generations": ["stage-three", "stage-four"],
        "tool_images": len(TOOL_NAMES),
    }


def _compare_stages(
    stage_three: Stage,
    stage_four: Stage,
    source_names: Sequence[str],
) -> dict[str, object]:
    for name in source_names:
        if (
            stage_three.objects[name].read_bytes()
            != stage_four.objects[name].read_bytes()
        ):
            raise BootstrapError(
                f"C object differs between stage three and stage four: {name}"
            )
    if (
        stage_three.objects["start"].read_bytes()
        != stage_four.objects["start"].read_bytes()
    ):
        raise BootstrapError(
            "startup object differs between stage three and stage four"
        )
    for name in TOOL_NAMES:
        if (
            stage_three.tools[name].read_bytes()
            != stage_four.tools[name].read_bytes()
        ):
            raise BootstrapError(
                "tool image differs between stage three and stage four: "
                f"{name}"
            )
    return {
        "all_equal": True,
        "c_objects": len(source_names),
        "compared_generations": ["stage-three", "stage-four"],
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


def _run_native_windows_behavior_checks(
    runner: ToolRunner,
    output_root: Path,
    stage_two: Stage,
    stage_three: Stage,
    native_plan: dict[str, object],
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
        _expect_status(
            help_result, 0, f"native Windows {tool_name} help"
        )
        if (
            "usage:" not in help_result.stdout.casefold()
            or help_result.stderr
        ):
            raise BootstrapError(
                f"native Windows {tool_name} help output differs"
            )

        failure_result = _run_stage_pair(
            runner,
            stage_two,
            stage_three,
            tool_name,
            ["--definitely-invalid-option"],
        )
        _expect_status(
            failure_result,
            2,
            f"native Windows {tool_name} invalid option",
        )
        if (
            failure_result.stdout
            or "usage:" not in failure_result.stderr.casefold()
        ):
            raise BootstrapError(
                f"native Windows {tool_name} failure output differs"
            )

    valid_source = behavior_root / "valid.cc"
    valid_source.write_text(
        "int native_fixed_point_value(int value) { return value + 17; }\n",
        encoding="ascii",
        newline="\n",
    )
    stage_two_object = behavior_root / "stage-three-valid.o"
    stage_three_object = behavior_root / "stage-four-valid.o"
    compile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        ["-c", valid_source, "-o", stage_two_object],
        ["-c", valid_source, "-o", stage_three_object],
        120,
    )
    _expect_status(compile_result, 0, "native Windows CupidC source")
    if (
        compile_result.stdout
        or compile_result.stderr
        or stage_two_object.read_bytes()
        != stage_three_object.read_bytes()
    ):
        raise BootstrapError("native Windows CupidC output differs")
    _validate_i386_relocatable(stage_two_object)

    assembly_source = behavior_root / "valid.asm"
    assembly_source.write_text(
        "BITS 16\nORG 0x7c00\nmov ax, 0x1234\nret\n",
        encoding="ascii",
        newline="\n",
    )
    stage_two_binary = behavior_root / "stage-three-valid.bin"
    stage_three_binary = behavior_root / "stage-four-valid.bin"
    assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        ["-f", "bin", assembly_source, "-o", stage_two_binary],
        ["-f", "bin", assembly_source, "-o", stage_three_binary],
        60,
    )
    _expect_status(assembly_result, 0, "native Windows CupidASM source")
    if (
        assembly_result.stdout
        or assembly_result.stderr
        or stage_two_binary.read_bytes() != b"\xb8\x34\x12\xc3"
        or stage_three_binary.read_bytes()
        != stage_two_binary.read_bytes()
    ):
        raise BootstrapError("native Windows CupidASM output differs")

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
        60,
    )
    _expect_status(
        disassembly_result, 0, "native Windows CupidDis input"
    )
    if (
        "mov ax, 0x1234" not in disassembly_result.stdout
        or disassembly_result.stderr
    ):
        raise BootstrapError("native Windows CupidDis output differs")

    unowned_relocation = behavior_root / "unowned-relocation.o"
    unowned_relocation.write_bytes(_unowned_relocation_object_payload())
    relocation_ownership_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        ["--require-known", unowned_relocation],
        ["--require-known", unowned_relocation],
    )
    _expect_status(
        relocation_ownership_result,
        1,
        "native Windows CupidDis relocation ownership",
    )
    if (
        relocation_ownership_result.stdout
        or "1 of 1 executable relocations unmatched"
        not in relocation_ownership_result.stderr
    ):
        raise BootstrapError(
            "native Windows CupidDis relocation ownership differs"
        )

    _check_relocatable_local_target_behavior(
        runner,
        stage_two,
        stage_three,
        behavior_root,
        "native Windows ",
    )
    _check_executable_local_target_behavior(
        runner,
        stage_two,
        stage_three,
        behavior_root,
        "native Windows ",
    )

    asset = behavior_root / "asset.bin"
    asset.write_bytes(b"native-windows-fixed-point")
    stage_two_wrapped = behavior_root / "stage-three-wrapped.o"
    stage_three_wrapped = behavior_root / "stage-four-wrapped.o"
    wrap_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [
            "wrap",
            asset,
            "--stem",
            "native_windows_fixed_point",
            "-o",
            stage_two_wrapped,
        ],
        [
            "wrap",
            asset,
            "--stem",
            "native_windows_fixed_point",
            "-o",
            stage_three_wrapped,
        ],
        60,
    )
    _expect_status(wrap_result, 0, "native Windows CupidObj input")
    if (
        wrap_result.stdout
        or wrap_result.stderr
        or stage_two_wrapped.read_bytes()
        != stage_three_wrapped.read_bytes()
    ):
        raise BootstrapError("native Windows CupidObj output differs")
    _validate_i386_relocatable(stage_two_wrapped)

    stage_two_linked = behavior_root / "stage-three-relinked.exe"
    stage_three_linked = behavior_root / "stage-four-relinked.exe"
    raw_links = _require_object(
        native_plan.get("links"), "Windows build plan links"
    )
    link_order = [
        str(name)
        for name in _require_list(
            raw_links.get("cupidasm"),
            "Windows build plan links: cupidasm",
        )
    ]
    link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        _windows_link_arguments(
            "cupidasm",
            stage_two_linked,
            stage_two.objects,
            link_order,
        ),
        _windows_link_arguments(
            "cupidasm",
            stage_three_linked,
            stage_three.objects,
            link_order,
        ),
        180,
    )
    _expect_status(link_result, 0, "native Windows CupidLD input")
    if (
        link_result.stdout
        or link_result.stderr
        or stage_two_linked.read_bytes()
        != stage_two.tools["cupidasm"].read_bytes()
        or stage_three_linked.read_bytes()
        != stage_three.tools["cupidasm"].read_bytes()
    ):
        raise BootstrapError("native Windows CupidLD output differs")
    _validate_static_i386_pe32(
        stage_two_linked,
        int(EXPECTED_WINDOWS_TARGET["entry"]),
        WINDOWS_TOOL_IMPORTS,
    )

    return {
        "failure_cases": len(TOOL_NAMES) + 3,
        "help_cases": len(TOOL_NAMES),
        "success_cases": len(TOOL_NAMES) + 2,
    }


def _build_windows_tool_image(
    runner: ToolRunner,
    source_root: Path,
    behavior_root: Path,
    stage_two: Stage,
    stage_three: Stage,
    tool_name: str,
    link_objects: Sequence[str],
    windows_imports: Sequence[tuple[str, str, str]],
    stage_two_start: Path,
    stage_three_start: Path,
    stage_two_runtime: Path,
    stage_three_runtime: Path,
    stage_two_host_adapter: Path,
    stage_three_host_adapter: Path,
    stage_two_extra_objects: dict[str, Path],
    stage_three_extra_objects: dict[str, Path],
) -> tuple[Path, Path, dict[str, object]]:
    windows_sources = {
        "cupidasm": ("cupidasm_main",),
        "cupidc": ("cupidc_main",),
        "cupidld": ("cupidld_main",),
        "cupidobj": ("cupidobj_main",),
    }
    replacement_names = windows_sources.get(tool_name, ())
    stage_two_replacements = {
        "ctool_host": stage_two_host_adapter,
        **stage_two_extra_objects,
    }
    stage_three_replacements = {
        "ctool_host": stage_three_host_adapter,
        **stage_three_extra_objects,
    }
    compile_artifacts: dict[str, Path] = {
        "stage-three-ctool_host": stage_two_host_adapter,
        "stage-four-ctool_host": stage_three_host_adapter,
        **{
            f"stage-three-{name}": path
            for name, path in stage_two_extra_objects.items()
        },
        **{
            f"stage-four-{name}": path
            for name, path in stage_three_extra_objects.items()
        },
    }
    for source_name in replacement_names:
        stage_two_object = behavior_root / (
            f"stage-three-windows-{tool_name}-{source_name}.o"
        )
        stage_three_object = behavior_root / (
            f"stage-four-windows-{tool_name}-{source_name}.o"
        )
        compile_result = _run_stage_pair(
            runner,
            stage_two,
            stage_three,
            "cupidc",
            [
                "--root",
                source_root,
                "-D",
                "_WIN32=1",
                "-c",
                f"/toolchain/{source_name}.cc",
                "-I",
                "/toolchain",
                "--include-angle",
                "/toolchain/hosted/i386-linux/include",
                "-o",
                _logical_path(source_root, stage_two_object),
            ],
            [
                "--root",
                source_root,
                "-D",
                "_WIN32=1",
                "-c",
                f"/toolchain/{source_name}.cc",
                "-I",
                "/toolchain",
                "--include-angle",
                "/toolchain/hosted/i386-linux/include",
                "-o",
                _logical_path(source_root, stage_three_object),
            ],
            360,
        )
        _expect_status(
            compile_result, 0, f"CupidC Windows {tool_name} {source_name}"
        )
        if (
            compile_result.stdout
            or compile_result.stderr
            or stage_two_object.read_bytes() != stage_three_object.read_bytes()
        ):
            raise BootstrapError(
                f"CupidC Windows {tool_name} {source_name} differs"
            )
        _validate_i386_relocatable(stage_two_object)
        stage_two_replacements[source_name] = stage_two_object
        stage_three_replacements[source_name] = stage_three_object
        compile_artifacts[f"stage-three-{source_name}"] = stage_two_object
        compile_artifacts[f"stage-four-{source_name}"] = stage_three_object

    import_selectors = tuple(
        f"{slot}={library}:{procedure}"
        for slot, library, procedure in windows_imports
    )
    stage_two_image = behavior_root / f"stage-three-{tool_name}.exe"
    stage_three_image = behavior_root / f"stage-four-{tool_name}.exe"

    def link_arguments(
        image: Path,
        stage: Stage,
        start: Path,
        runtime: Path,
        replacements: dict[str, Path],
        selectors: Sequence[str],
    ) -> list[str | Path]:
        arguments: list[str | Path] = [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
        ]
        for selector in selectors:
            arguments.extend(("--import", selector))
        arguments.extend(("-o", image, start))
        for name in link_objects:
            arguments.append(
                replacements[name]
                if name in replacements
                else stage.objects[name]
            )
        arguments.append(runtime)
        return arguments

    link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        link_arguments(
            stage_two_image,
            stage_two,
            stage_two_start,
            stage_two_runtime,
            stage_two_replacements,
            tuple(reversed(import_selectors)),
        ),
        link_arguments(
            stage_three_image,
            stage_three,
            stage_three_start,
            stage_three_runtime,
            stage_three_replacements,
            import_selectors,
        ),
        180,
    )
    _expect_status(link_result, 0, f"CupidLD Windows {tool_name}")
    if (
        link_result.stdout
        or link_result.stderr
        or stage_two_image.read_bytes() != stage_three_image.read_bytes()
    ):
        raise BootstrapError(f"Cupid-built Windows {tool_name} differs")
    _validate_static_i386_pe32(
        stage_two_image,
        0x00401000,
        (("KERNEL32.dll", tuple(item[2] for item in windows_imports)),),
    )
    compile_artifacts["stage-three-image"] = stage_two_image
    compile_artifacts["stage-four-image"] = stage_three_image
    return stage_two_image, stage_three_image, _artifact_inventory(
        compile_artifacts
    )


def _profile_snapshot_payload(
    schema: str,
    profiles: Sequence[tuple[str, Sequence[str], Sequence[str]]],
    inputs: Sequence[tuple[str, bytes]],
) -> bytes:
    payload = bytearray(b"CUPROF1\0")

    def append_u32(value: int) -> None:
        payload.extend(struct.pack("<I", value))

    def append_bytes(value: bytes) -> None:
        append_u32(len(value))
        payload.extend(value)

    def append_text(value: str) -> None:
        append_bytes(value.encode("ascii"))

    append_text(schema)
    append_u32(len(profiles))
    for name, headers, sources in profiles:
        append_text(name)
        append_u32(len(headers))
        for path in headers:
            append_text(path)
        append_u32(len(sources))
        for path in sources:
            append_text(path)
    append_u32(len(inputs))
    for path, contents in inputs:
        append_text(path)
        append_bytes(contents)
    return bytes(payload)


def _unowned_relocation_object_payload() -> bytes:
    section_names = (
        b"\0.text\0.rel.text\0.symtab\0.strtab\0.shstrtab\0"
    )
    symbol_names = b"\0target\0"
    text = bytes.fromhex("b8 00 00 00 00 c3")
    text_offset = 64
    relocation_offset = (text_offset + len(text) + 3) & ~3
    symbol_offset = relocation_offset + 8
    string_offset = symbol_offset + 32
    names_offset = string_offset + len(symbol_names)
    section_offset = (names_offset + len(section_names) + 3) & ~3
    image = bytearray(section_offset + 6 * 40)
    image[0:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        1,
        3,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        6,
        5,
    )
    image[text_offset : text_offset + len(text)] = text
    struct.pack_into("<II", image, relocation_offset, 0, (1 << 8) | 1)
    struct.pack_into(
        "<IIIBBH", image, symbol_offset + 16, 1, 0, 0, 0x10, 0, 0
    )
    image[string_offset : string_offset + len(symbol_names)] = symbol_names
    image[names_offset : names_offset + len(section_names)] = section_names
    sections = (
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 0x6, 0, text_offset, len(text), 0, 0, 16, 0),
        (7, 9, 0, 0, relocation_offset, 8, 3, 1, 4, 8),
        (17, 2, 0, 0, symbol_offset, 32, 4, 1, 4, 16),
        (25, 3, 0, 0, string_offset, len(symbol_names), 0, 0, 1, 0),
        (
            33,
            3,
            0,
            0,
            names_offset,
            len(section_names),
            0,
            0,
            1,
            0,
        ),
    )
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


def _local_target_object_payload(displacement: int) -> bytes:
    section_names = (
        b"\0.text\0.rel.text\0.symtab\0.strtab\0.shstrtab\0"
    )
    symbol_names = b"\0external\0"
    text = bytes.fromhex("e8 fc ff ff ff eb") + bytes(
        (displacement & 0xFF,)
    ) + bytes.fromhex("b8 00 00 00 00 c3")
    text_offset = 64
    relocation_offset = (text_offset + len(text) + 3) & ~3
    symbol_offset = relocation_offset + 8
    string_offset = symbol_offset + 32
    names_offset = string_offset + len(symbol_names)
    section_offset = (names_offset + len(section_names) + 3) & ~3
    image = bytearray(section_offset + 6 * 40)
    image[0:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        1,
        3,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        6,
        5,
    )
    image[text_offset : text_offset + len(text)] = text
    struct.pack_into(
        "<II", image, relocation_offset, 1, (1 << 8) | 2
    )
    struct.pack_into(
        "<IIIBBH", image, symbol_offset + 16, 1, 0, 0, 0x10, 0, 0
    )
    image[string_offset : string_offset + len(symbol_names)] = symbol_names
    image[names_offset : names_offset + len(section_names)] = section_names
    sections = (
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 0x6, 0, text_offset, len(text), 0, 0, 16, 0),
        (7, 9, 0, 0, relocation_offset, 8, 3, 1, 4, 8),
        (17, 2, 0, 0, symbol_offset, 32, 4, 1, 4, 16),
        (
            25,
            3,
            0,
            0,
            string_offset,
            len(symbol_names),
            0,
            0,
            1,
            0,
        ),
        (
            33,
            3,
            0,
            0,
            names_offset,
            len(section_names),
            0,
            0,
            1,
            0,
        ),
    )
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


def _local_target_executable_payload(displacement: int) -> bytes:
    text = bytes.fromhex("eb") + bytes((displacement & 0xFF,)) + bytes.fromhex(
        "b8 00 00 00 00 c3"
    )
    text_offset = 84
    image = bytearray(text_offset + len(text))
    image[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        2,
        3,
        1,
        0x00400000,
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
        text_offset,
        0x00400000,
        0x00400000,
        len(text),
        len(text),
        5,
        1,
    )
    image[text_offset:] = text
    return bytes(image)


def _check_relocatable_local_target_behavior(
    runner: ToolRunner,
    stage_two: Stage,
    stage_three: Stage,
    behavior_root: Path,
    label_prefix: str,
) -> None:
    local_target_object = behavior_root / "valid-local-target.o"
    local_target_object.write_bytes(_local_target_object_payload(5))
    local_target_arguments: list[str | Path] = [
        "--require-known",
        "--require-local-targets",
        local_target_object,
    ]
    object_local_target_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        local_target_arguments,
        local_target_arguments,
    )
    _expect_status(
        object_local_target_result,
        0,
        f"{label_prefix}CupidDis relocatable local target",
    )
    if object_local_target_result.stdout or object_local_target_result.stderr:
        raise BootstrapError(
            f"{label_prefix}CupidDis relocatable local target output differs"
        )

    invalid_local_target_object = behavior_root / "invalid-local-target.o"
    invalid_local_target_object.write_bytes(_local_target_object_payload(1))
    invalid_object_local_target_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        [
            "--require-known",
            "--require-local-targets",
            invalid_local_target_object,
        ],
    )
    _expect_status(
        invalid_object_local_target_result,
        1,
        f"{label_prefix}CupidDis invalid relocatable local target",
    )
    if (
        invalid_object_local_target_result.stdout
        or "1 of 1 direct relative targets invalid"
        not in invalid_object_local_target_result.stderr
        or "1 mid-instruction"
        not in invalid_object_local_target_result.stderr
    ):
        raise BootstrapError(
            f"{label_prefix}CupidDis invalid relocatable target output differs"
        )


def _check_executable_local_target_behavior(
    runner: ToolRunner,
    stage_two: Stage,
    stage_three: Stage,
    behavior_root: Path,
    label_prefix: str,
) -> None:
    local_target_executable = behavior_root / "valid-linked-local-target.elf"
    local_target_executable.write_bytes(_local_target_executable_payload(5))
    local_target_arguments: list[str | Path] = [
        "--require-known",
        "--require-local-targets",
        local_target_executable,
    ]
    executable_local_target_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        local_target_arguments,
        local_target_arguments,
    )
    _expect_status(
        executable_local_target_result,
        0,
        f"{label_prefix}CupidDis linked local target",
    )
    if (
        executable_local_target_result.stdout
        or executable_local_target_result.stderr
    ):
        raise BootstrapError(
            f"{label_prefix}CupidDis linked local target output differs"
        )

    invalid_local_target_executable = (
        behavior_root / "invalid-linked-local-target.elf"
    )
    invalid_local_target_executable.write_bytes(
        _local_target_executable_payload(1)
    )
    invalid_executable_local_target_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        [
            "--require-known",
            "--require-local-targets",
            invalid_local_target_executable,
        ],
    )
    _expect_status(
        invalid_executable_local_target_result,
        1,
        f"{label_prefix}CupidDis invalid linked local target",
    )
    if (
        invalid_executable_local_target_result.stdout
        or "1 of 1 direct relative targets invalid"
        not in invalid_executable_local_target_result.stderr
        or "1 mid-instruction"
        not in invalid_executable_local_target_result.stderr
    ):
        raise BootstrapError(
            f"{label_prefix}CupidDis invalid linked target output differs"
        )


def _run_behavior_checks(
    runner: ToolRunner,
    source_root: Path,
    output_root: Path,
    stage_two: Stage,
    stage_three: Stage,
    evidence_out: dict[str, object] | None = None,
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
        if tool_name == "cupidobj":
            for operation in (
                "wrap-jpeg",
                "disk-template",
                "iso-fixture",
                "profile-manifest",
            ):
                if operation not in help_result.stdout:
                    raise BootstrapError(
                        f"cupidobj help omits {operation}"
                    )
        if tool_name == "cupidld" and "i386pe" not in help_result.stdout:
            raise BootstrapError("cupidld help omits i386pe")

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
    stage_two_valid = behavior_root / "stage-three-valid.o"
    stage_three_valid = behavior_root / "stage-four-valid.o"
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
        [*compiler_root_arguments, "-o", "/stage-three-valid.o"],
        [*compiler_root_arguments, "-o", "/stage-four-valid.o"],
    )
    _expect_status(valid_result, 0, "CupidC valid source")
    if valid_result.stdout or valid_result.stderr:
        raise BootstrapError("CupidC valid-source output differs")
    if stage_two_valid.read_bytes() != stage_three_valid.read_bytes():
        raise BootstrapError("CupidC behavior object differs across stages")
    _validate_i386_relocatable(stage_two_valid)

    sentinel = b"fixed-point-failure-sentinel"
    stage_two_failure = behavior_root / "stage-three-failure.o"
    stage_three_failure = behavior_root / "stage-four-failure.o"
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
        [*invalid_root_arguments, "-o", "/stage-three-failure.o"],
        [*invalid_root_arguments, "-o", "/stage-four-failure.o"],
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
    stage_two_binary = behavior_root / "stage-three.bin"
    stage_three_binary = behavior_root / "stage-four.bin"
    assembly_source.write_text(
        "BITS 16\n"
        "ORG 0x7c00\n"
        "start:\n"
        "    jmp finished\n"
        "    mov ax, 0x1234\n"
        "finished:\n"
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
        or stage_two_binary.read_bytes() != b"\xeb\x03\xb8\x34\x12\xc3"
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

    local_target_arguments: list[str | Path] = [
        "--require-known",
        "--require-local-targets",
        "--raw",
        "--mode",
        "16",
        "--base",
        "0x7c00",
    ]
    local_target_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        [*local_target_arguments, stage_two_binary],
        [*local_target_arguments, stage_three_binary],
    )
    _expect_status(local_target_result, 0, "CupidDis local target inspection")
    if local_target_result.stdout or local_target_result.stderr:
        raise BootstrapError("CupidDis local target success output differs")

    invalid_target_binary = behavior_root / "invalid-target.bin"
    invalid_target_binary.write_bytes(b"\xeb\x7f")
    invalid_target_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        [*local_target_arguments, invalid_target_binary],
    )
    _expect_status(invalid_target_result, 1, "CupidDis invalid local target")
    if (
        invalid_target_result.stdout
        or "1 of 1 direct relative targets invalid"
        not in invalid_target_result.stderr
        or "1 outside image" not in invalid_target_result.stderr
    ):
        raise BootstrapError("CupidDis local target failure output differs")

    jpeg_payload = (
        b"\xff\xd8"
        b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
        b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
        b"\xff\xd9"
    )
    asset = behavior_root / "asset.jpg"
    stage_two_wrapped = behavior_root / "stage-three-wrapped.o"
    stage_three_wrapped = behavior_root / "stage-four-wrapped.o"
    asset.write_bytes(jpeg_payload)
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

    stage_two_jpeg = behavior_root / "stage-three-jpeg.o"
    stage_three_jpeg = behavior_root / "stage-four-jpeg.o"
    jpeg_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [
            "wrap-jpeg",
            asset,
            "--stem",
            "fixed_point_asset",
            "--section",
            ".rodata",
            "--readonly",
            "-o",
            stage_two_jpeg,
        ],
        [
            "wrap-jpeg",
            asset,
            "--stem",
            "fixed_point_asset",
            "--section",
            ".rodata",
            "--readonly",
            "-o",
            stage_three_jpeg,
        ],
    )
    _expect_status(jpeg_result, 0, "CupidObj JPEG wrap")
    if (
        jpeg_result.stdout
        or jpeg_result.stderr
        or stage_two_jpeg.read_bytes() != stage_three_jpeg.read_bytes()
        or stage_two_jpeg.read_bytes() != stage_two_wrapped.read_bytes()
    ):
        raise BootstrapError("CupidObj JPEG wrap differs")
    _validate_i386_relocatable(stage_two_jpeg)

    progressive_asset = behavior_root / "progressive.jpg"
    stage_two_jpeg_failure = behavior_root / "stage-three-jpeg-failure.o"
    stage_three_jpeg_failure = behavior_root / "stage-four-jpeg-failure.o"
    progressive_asset.write_bytes(
        jpeg_payload[:3] + b"\xc2" + jpeg_payload[4:]
    )
    stage_two_jpeg_failure.write_bytes(sentinel)
    stage_three_jpeg_failure.write_bytes(sentinel)
    progressive_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [
            "wrap-jpeg",
            progressive_asset,
            "--stem",
            "fixed_point_asset",
            "--section",
            ".rodata",
            "--readonly",
            "-o",
            stage_two_jpeg_failure,
        ],
        [
            "wrap-jpeg",
            progressive_asset,
            "--stem",
            "fixed_point_asset",
            "--section",
            ".rodata",
            "--readonly",
            "-o",
            stage_three_jpeg_failure,
        ],
    )
    _expect_status(progressive_result, 1, "CupidObj progressive JPEG")
    if (
        progressive_result.stdout
        or "unsupported progressive JPEG frame; "
        "check in a baseline SOF0/SOF1 asset"
        not in progressive_result.stderr
        or stage_two_jpeg_failure.read_bytes() != sentinel
        or stage_three_jpeg_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidObj JPEG failure behavior differs")

    disk_boot = behavior_root / "disk-boot.bin"
    disk_kernel = behavior_root / "disk-kernel.bin"
    stage_two_template = behavior_root / "stage-three-disk-template.bin"
    stage_three_template = behavior_root / "stage-four-disk-template.bin"
    disk_boot.write_bytes(
        bytes((index * 37 + 11) & 0xFF for index in range(5 * 512))
    )
    disk_kernel.write_bytes(b"CUPID-OS")
    disk_arguments: list[str | Path] = [
        "disk-template",
        disk_boot,
        "--kernel",
        disk_kernel,
        "--image-sectors",
        "4208",
        "--fat-start-lba",
        "8",
    ]
    disk_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [*disk_arguments, "-o", stage_two_template],
        [*disk_arguments, "-o", stage_three_template],
    )
    _expect_status(disk_result, 0, "CupidObj disk template")
    stage_two_template_bytes = stage_two_template.read_bytes()
    if (
        disk_result.stdout
        or disk_result.stderr
        or stage_three_template.read_bytes() != stage_two_template_bytes
        or len(stage_two_template_bytes) != 38400
        or hashlib.sha256(stage_two_template_bytes).hexdigest()
        != "a1784fde1833c6cd24f49dff105ff8a70de5b9e619dd8883b4d92d597f241501"
    ):
        raise BootstrapError("CupidObj disk template differs")

    overlapping_kernel = behavior_root / "overlapping-kernel.bin"
    stage_two_template_failure = (
        behavior_root / "stage-three-disk-template-failure.bin"
    )
    stage_three_template_failure = (
        behavior_root / "stage-four-disk-template-failure.bin"
    )
    overlapping_kernel.write_bytes(b"K" * (3 * 512 + 1))
    stage_two_template_failure.write_bytes(sentinel)
    stage_three_template_failure.write_bytes(sentinel)
    overlapping_arguments: list[str | Path] = [
        "disk-template",
        disk_boot,
        "--kernel",
        overlapping_kernel,
        "--image-sectors",
        "4208",
        "--fat-start-lba",
        "8",
    ]
    overlap_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [*overlapping_arguments, "-o", stage_two_template_failure],
        [*overlapping_arguments, "-o", stage_three_template_failure],
    )
    _expect_status(overlap_result, 1, "CupidObj overlapping kernel")
    if (
        overlap_result.stdout
        or "overlaps FAT partition at LBA 8" not in overlap_result.stderr
        or stage_two_template_failure.read_bytes() != sentinel
        or stage_three_template_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidObj disk-template failure differs")

    iso_manifest = behavior_root / "iso-fixture.manifest"
    iso_alpha = behavior_root / "iso-alpha.bin"
    iso_empty = behavior_root / "iso-empty.bin"
    iso_long_name = behavior_root / "iso-long-name.bin"
    iso_nested = behavior_root / "iso-nested.bin"
    iso_value = behavior_root / "iso-value.bin"
    stage_two_iso = behavior_root / "stage-three-iso-fixture.iso"
    stage_three_iso = behavior_root / "stage-four-iso-fixture.iso"
    iso_manifest.write_text(
        "alpha.txt\n"
        "empty.bin\n"
        "long_named_file.txt\n"
        "sub\n"
        "sub/nested.txt\n"
        "sub/nested\n"
        "sub/nested/value.bin\n",
        encoding="ascii",
        newline="\n",
    )
    iso_alpha.write_bytes(b"Cupid ISO fixed point\n")
    iso_empty.write_bytes(b"")
    iso_long_name.write_bytes(b"long logical name\n")
    iso_nested.write_bytes(b"nested\n")
    iso_value.write_bytes(
        bytes((index * 29 + 7) & 0xFF for index in range(4096))
    )
    iso_arguments: list[str | Path] = [
        "iso-fixture",
        iso_manifest,
        "--file",
        "alpha.txt",
        iso_alpha,
        "--file",
        "empty.bin",
        iso_empty,
        "--file",
        "long_named_file.txt",
        iso_long_name,
        "--directory",
        "sub",
        "--file",
        "sub/nested.txt",
        iso_nested,
        "--directory",
        "sub/nested",
        "--file",
        "sub/nested/value.bin",
        iso_value,
    ]
    iso_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [*iso_arguments, "-o", stage_two_iso],
        [*iso_arguments, "-o", stage_three_iso],
    )
    _expect_status(iso_result, 0, "CupidObj ISO fixture")
    stage_two_iso_bytes = stage_two_iso.read_bytes()
    if (
        iso_result.stdout
        or iso_result.stderr
        or stage_three_iso.read_bytes() != stage_two_iso_bytes
        or len(stage_two_iso_bytes) != 59392
        or hashlib.sha256(stage_two_iso_bytes).hexdigest()
        != "1b733dd65ee099c2499802302f486d2716de8afda8bd2fa9f44ff0a7d0699dd4"
    ):
        raise BootstrapError("CupidObj ISO fixture differs")

    missing_parent_manifest = behavior_root / "missing-parent.manifest"
    missing_parent_source = behavior_root / "missing-parent.bin"
    stage_two_iso_failure = behavior_root / "stage-three-iso-failure.iso"
    stage_three_iso_failure = behavior_root / "stage-four-iso-failure.iso"
    missing_parent_manifest.write_text(
        "lost/payload.bin\n", encoding="ascii", newline="\n"
    )
    missing_parent_source.write_bytes(b"payload")
    stage_two_iso_failure.write_bytes(sentinel)
    stage_three_iso_failure.write_bytes(sentinel)
    missing_parent_arguments: list[str | Path] = [
        "iso-fixture",
        missing_parent_manifest,
        "--file",
        "lost/payload.bin",
        missing_parent_source,
    ]
    missing_parent_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        [*missing_parent_arguments, "-o", stage_two_iso_failure],
        [*missing_parent_arguments, "-o", stage_three_iso_failure],
    )
    _expect_status(
        missing_parent_result, 1, "CupidObj missing ISO parent"
    )
    if (
        missing_parent_result.stdout
        or "directory parent" not in missing_parent_result.stderr
        or stage_two_iso_failure.read_bytes() != sentinel
        or stage_three_iso_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidObj ISO failure behavior differs")

    profile_snapshot = behavior_root / "profile-manifest.snapshot"
    stage_two_profile = behavior_root / "stage-three-profile-manifest.json"
    stage_three_profile = behavior_root / "stage-four-profile-manifest.json"
    profile_schema = "cupid.doom-profile-inputs.v1"
    profile_sizes = (129, 0, 65, 3, 64, 55, 63, 56)
    profile_inputs = tuple(
        (
            f"hash/length-{size:03d}.h",
            bytes(index % 251 for index in range(size)),
        )
        for size in profile_sizes
    )
    profile_headers = tuple(path for path, _contents in profile_inputs)
    profile_membership = (
        (
            "doom-tree",
            profile_headers,
            ("kernel/doom/z.cc", "kernel/doom/a.cc"),
        ),
        (
            "doom-compat",
            profile_headers[::2],
            ("kernel/doom/d.cc",),
        ),
    )
    profile_payload = _profile_snapshot_payload(
        profile_schema,
        profile_membership,
        profile_inputs,
    )
    profile_snapshot.write_bytes(profile_payload)
    profile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        ["profile-manifest", profile_snapshot, "-o", stage_two_profile],
        ["profile-manifest", profile_snapshot, "-o", stage_three_profile],
    )
    _expect_status(profile_result, 0, "CupidObj profile manifest")
    expected_profile = (
        json.dumps(
            {
                "schema": profile_schema,
                "profiles": {
                    name: sorted(headers)
                    for name, headers, _sources in profile_membership
                },
                "sources": {
                    name: sorted(sources)
                    for name, _headers, sources in profile_membership
                },
                "inputs": [
                    {
                        "path": path,
                        "bytes": len(contents),
                        "sha256": hashlib.sha256(contents).hexdigest(),
                    }
                    for path, contents in sorted(profile_inputs)
                ],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("ascii")
    if (
        profile_result.stdout
        or profile_result.stderr
        or stage_two_profile.read_bytes() != expected_profile
        or stage_three_profile.read_bytes() != expected_profile
    ):
        raise BootstrapError("CupidObj profile manifest differs")

    profile_failure_cases = (
        ("truncated", profile_payload[:-1], "snapshot is truncated"),
        (
            "unsafe-path",
            _profile_snapshot_payload(
                profile_schema,
                (
                    (
                        "doom-tree",
                        ("../unsafe.h",),
                        ("kernel/doom/a.cc",),
                    ),
                ),
                (("../unsafe.h", b"unsafe"),),
            ),
            "repository path is invalid",
        ),
        (
            "case-collision",
            _profile_snapshot_payload(
                profile_schema,
                (
                    (
                        "doom-tree",
                        ("a.h", "A.h"),
                        ("kernel/doom/a.cc",),
                    ),
                ),
                (("a.h", b"a"),),
            ),
            "header path has a case collision",
        ),
    )
    for failure_name, failure_payload, failure_message in profile_failure_cases:
        failure_snapshot = behavior_root / f"{failure_name}-profile.snapshot"
        stage_two_profile_failure = (
            behavior_root
            / f"stage-three-{failure_name}-profile-manifest-failure.json"
        )
        stage_three_profile_failure = (
            behavior_root
            / f"stage-four-{failure_name}-profile-manifest-failure.json"
        )
        failure_snapshot.write_bytes(failure_payload)
        stage_two_profile_failure.write_bytes(sentinel)
        stage_three_profile_failure.write_bytes(sentinel)
        failure_result = _run_stage_pair(
            runner,
            stage_two,
            stage_three,
            "cupidobj",
            [
                "profile-manifest",
                failure_snapshot,
                "-o",
                stage_two_profile_failure,
            ],
            [
                "profile-manifest",
                failure_snapshot,
                "-o",
                stage_three_profile_failure,
            ],
        )
        _expect_status(
            failure_result,
            1,
            f"CupidObj {failure_name} profile manifest",
        )
        if (
            failure_result.stdout
            or failure_message not in failure_result.stderr
            or stage_two_profile_failure.read_bytes() != sentinel
            or stage_three_profile_failure.read_bytes() != sentinel
        ):
            raise BootstrapError(
                f"CupidObj {failure_name} profile failure differs"
            )

    link_source = behavior_root / "start.asm"
    stage_two_link_object = behavior_root / "stage-three-start.o"
    stage_three_link_object = behavior_root / "stage-four-start.o"
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

    stage_two_linked = behavior_root / "stage-three-linked.elf"
    stage_three_linked = behavior_root / "stage-four-linked.elf"
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

    stage_two_pe32 = behavior_root / "stage-three-fixed.exe"
    stage_three_pe32 = behavior_root / "stage-four-fixed.exe"
    pe32_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            "-o",
            stage_two_pe32,
            stage_two_link_object,
        ],
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            "-o",
            stage_three_pe32,
            stage_three_link_object,
        ],
    )
    _expect_status(pe32_result, 0, "CupidLD PE32 fixed image")
    if (
        pe32_result.stdout
        or pe32_result.stderr
        or stage_two_pe32.read_bytes()
        != stage_three_pe32.read_bytes()
    ):
        raise BootstrapError("CupidLD PE32 fixed-image output differs")
    _validate_static_i386_pe32(
        stage_two_pe32,
        0x00401000,
    )

    windows_start = source_root / "toolchain/hosted/i386-windows/start.asm"
    windows_contract = (
        source_root / "toolchain/tests/hosted_i386_windows_contract.cc"
    )
    stage_two_windows_start = behavior_root / "stage-three-windows-start.o"
    stage_three_windows_start = behavior_root / "stage-four-windows-start.o"
    windows_assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        ["-f", "elf32", windows_start, "-o", stage_two_windows_start],
        ["-f", "elf32", windows_start, "-o", stage_three_windows_start],
    )
    _expect_status(
        windows_assembly_result, 0, "CupidASM Windows startup"
    )
    if (
        windows_assembly_result.stdout
        or windows_assembly_result.stderr
        or stage_two_windows_start.read_bytes()
        != stage_three_windows_start.read_bytes()
    ):
        raise BootstrapError("CupidASM Windows startup output differs")
    _validate_i386_relocatable(stage_two_windows_start)

    local_windows_contract = behavior_root / "windows-contract.cc"
    local_windows_contract.write_bytes(windows_contract.read_bytes())
    stage_two_windows_contract = behavior_root / "stage-three-windows-contract.o"
    stage_three_windows_contract = behavior_root / "stage-four-windows-contract.o"
    windows_compiler_arguments: list[str | Path] = [
        "--root",
        behavior_root,
        "--freestanding",
        "-c",
        "/windows-contract.cc",
    ]
    windows_compile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [
            *windows_compiler_arguments,
            "-o",
            "/stage-three-windows-contract.o",
        ],
        [
            *windows_compiler_arguments,
            "-o",
            "/stage-four-windows-contract.o",
        ],
    )
    _expect_status(
        windows_compile_result, 0, "CupidC Windows runtime contract"
    )
    if (
        windows_compile_result.stdout
        or windows_compile_result.stderr
        or stage_two_windows_contract.read_bytes()
        != stage_three_windows_contract.read_bytes()
    ):
        raise BootstrapError("CupidC Windows runtime object differs")
    _validate_i386_relocatable(stage_two_windows_contract)

    windows_imports = (
        ("__imp_ExitProcess", "KERNEL32.dll", "ExitProcess"),
        ("__imp_GetStdHandle", "KERNEL32.dll", "GetStdHandle"),
        ("__imp_WriteFile", "KERNEL32.dll", "WriteFile"),
    )
    windows_import_selectors = tuple(
        f"{slot}={library}:{procedure}"
        for slot, library, procedure in windows_imports
    )
    stage_two_windows_image = behavior_root / "stage-three-windows.exe"
    stage_three_windows_image = behavior_root / "stage-four-windows.exe"
    windows_link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            "--import",
            windows_import_selectors[2],
            "--import",
            windows_import_selectors[0],
            "--import",
            windows_import_selectors[1],
            "-o",
            stage_two_windows_image,
            stage_two_windows_start,
            stage_two_windows_contract,
        ],
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            "--import",
            windows_import_selectors[1],
            "--import",
            windows_import_selectors[2],
            "--import",
            windows_import_selectors[0],
            "-o",
            stage_three_windows_image,
            stage_three_windows_start,
            stage_three_windows_contract,
        ],
    )
    _expect_status(
        windows_link_result, 0, "CupidLD imported Windows image"
    )
    if (
        windows_link_result.stdout
        or windows_link_result.stderr
        or stage_two_windows_image.read_bytes()
        != stage_three_windows_image.read_bytes()
    ):
        raise BootstrapError("Cupid-built Windows image differs")
    _validate_static_i386_pe32(
        stage_two_windows_image,
        0x00401000,
        (("KERNEL32.dll", ("ExitProcess", "GetStdHandle", "WriteFile")),),
    )
    windows_loader: dict[str, object] = {"status": "not-run"}
    if os.name == "nt":
        try:
            native_result = subprocess.run(
                [str(stage_two_windows_image)],
                cwd=source_root,
                capture_output=True,
                timeout=10,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise BootstrapError(
                f"Cupid-built Windows image could not run: {error}"
            ) from error
        if (
            native_result.returncode != 37
            or native_result.stdout != b"Cupid-built Windows runtime: ok\n"
            or native_result.stderr
        ):
            raise BootstrapError("Cupid-built Windows runtime differs")
        windows_loader = {
            "return_code": native_result.returncode,
            "status": "pass",
            "stderr": native_result.stderr.decode("ascii"),
            "stdout": native_result.stdout.decode("ascii"),
        }

    windows_tool_start = (
        source_root / "toolchain/hosted/i386-windows/tool_start.asm"
    )
    stage_two_windows_tool_runtime = (
        behavior_root / "stage-three-windows-tool-runtime.o"
    )
    stage_three_windows_tool_runtime = (
        behavior_root / "stage-four-windows-tool-runtime.o"
    )
    windows_tool_compile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [
            "--root",
            source_root,
            "--gnu",
            "-c",
            "/toolchain/hosted/i386-windows/runtime.cc",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(source_root, stage_two_windows_tool_runtime),
        ],
        [
            "--root",
            source_root,
            "--gnu",
            "-c",
            "/toolchain/hosted/i386-windows/runtime.cc",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(source_root, stage_three_windows_tool_runtime),
        ],
        360,
    )
    _expect_status(
        windows_tool_compile_result, 0, "CupidC Windows tool runtime"
    )
    if (
        windows_tool_compile_result.stdout
        or windows_tool_compile_result.stderr
        or stage_two_windows_tool_runtime.read_bytes()
        != stage_three_windows_tool_runtime.read_bytes()
    ):
        raise BootstrapError("CupidC Windows tool runtime differs")
    _validate_i386_relocatable(stage_two_windows_tool_runtime)

    stage_two_windows_tool_start = (
        behavior_root / "stage-three-windows-tool-start.o"
    )
    stage_three_windows_tool_start = (
        behavior_root / "stage-four-windows-tool-start.o"
    )
    windows_tool_assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        [
            "-f",
            "elf32",
            windows_tool_start,
            "-o",
            stage_two_windows_tool_start,
        ],
        [
            "-f",
            "elf32",
            windows_tool_start,
            "-o",
            stage_three_windows_tool_start,
        ],
        120,
    )
    _expect_status(
        windows_tool_assembly_result, 0, "CupidASM Windows tool startup"
    )
    if (
        windows_tool_assembly_result.stdout
        or windows_tool_assembly_result.stderr
        or stage_two_windows_tool_start.read_bytes()
        != stage_three_windows_tool_start.read_bytes()
    ):
        raise BootstrapError("CupidASM Windows tool startup differs")
    _validate_i386_relocatable(stage_two_windows_tool_start)

    stage_two_windows_host_adapter = (
        behavior_root / "stage-three-windows-ctool-host.o"
    )
    stage_three_windows_host_adapter = (
        behavior_root / "stage-four-windows-ctool-host.o"
    )
    windows_host_adapter_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [
            "--root",
            source_root,
            "-D",
            "_WIN32=1",
            "-c",
            "/toolchain/ctool_host.cc",
            "-I",
            "/toolchain",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(source_root, stage_two_windows_host_adapter),
        ],
        [
            "--root",
            source_root,
            "-D",
            "_WIN32=1",
            "-c",
            "/toolchain/ctool_host.cc",
            "-I",
            "/toolchain",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(source_root, stage_three_windows_host_adapter),
        ],
        360,
    )
    _expect_status(
        windows_host_adapter_result, 0, "CupidC Windows host adapter"
    )
    if (
        windows_host_adapter_result.stdout
        or windows_host_adapter_result.stderr
        or stage_two_windows_host_adapter.read_bytes()
        != stage_three_windows_host_adapter.read_bytes()
    ):
        raise BootstrapError("CupidC Windows host adapter differs")
    _validate_i386_relocatable(stage_two_windows_host_adapter)

    stage_two_windows_publication_runtime = (
        behavior_root / "stage-three-windows-publication-runtime.o"
    )
    stage_three_windows_publication_runtime = (
        behavior_root / "stage-four-windows-publication-runtime.o"
    )
    windows_publication_compile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [
            "--root",
            source_root,
            "-D",
            "_WIN32=1",
            "-c",
            "/toolchain/hosted/i386-windows/publication_runtime.cc",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(
                source_root, stage_two_windows_publication_runtime
            ),
        ],
        [
            "--root",
            source_root,
            "-D",
            "_WIN32=1",
            "-c",
            "/toolchain/hosted/i386-windows/publication_runtime.cc",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(
                source_root, stage_three_windows_publication_runtime
            ),
        ],
        360,
    )
    _expect_status(
        windows_publication_compile_result,
        0,
        "CupidC Windows publication runtime",
    )
    if (
        windows_publication_compile_result.stdout
        or windows_publication_compile_result.stderr
        or stage_two_windows_publication_runtime.read_bytes()
        != stage_three_windows_publication_runtime.read_bytes()
    ):
        raise BootstrapError("CupidC Windows publication runtime differs")
    _validate_i386_relocatable(stage_two_windows_publication_runtime)

    windows_publication_start = (
        source_root / "toolchain/hosted/i386-windows/publication_start.asm"
    )
    stage_two_windows_publication_start = (
        behavior_root / "stage-three-windows-publication-start.o"
    )
    stage_three_windows_publication_start = (
        behavior_root / "stage-four-windows-publication-start.o"
    )
    windows_publication_assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        [
            "-f",
            "elf32",
            windows_publication_start,
            "-o",
            stage_two_windows_publication_start,
        ],
        [
            "-f",
            "elf32",
            windows_publication_start,
            "-o",
            stage_three_windows_publication_start,
        ],
        120,
    )
    _expect_status(
        windows_publication_assembly_result,
        0,
        "CupidASM Windows publication startup",
    )
    if (
        windows_publication_assembly_result.stdout
        or windows_publication_assembly_result.stderr
        or stage_two_windows_publication_start.read_bytes()
        != stage_three_windows_publication_start.read_bytes()
    ):
        raise BootstrapError("CupidASM Windows publication startup differs")
    _validate_i386_relocatable(stage_two_windows_publication_start)

    windows_cupiddis_imports = (
        ("__imp_CloseHandle", "KERNEL32.dll", "CloseHandle"),
        ("__imp_CreateFileA", "KERNEL32.dll", "CreateFileA"),
        ("__imp_ExitProcess", "KERNEL32.dll", "ExitProcess"),
        ("__imp_GetCommandLineA", "KERNEL32.dll", "GetCommandLineA"),
        (
            "__imp_GetCurrentDirectoryA",
            "KERNEL32.dll",
            "GetCurrentDirectoryA",
        ),
        ("__imp_GetLastError", "KERNEL32.dll", "GetLastError"),
        ("__imp_GetStdHandle", "KERNEL32.dll", "GetStdHandle"),
        ("__imp_ReadFile", "KERNEL32.dll", "ReadFile"),
        ("__imp_SetFilePointer", "KERNEL32.dll", "SetFilePointer"),
        ("__imp_VirtualAlloc", "KERNEL32.dll", "VirtualAlloc"),
        ("__imp_VirtualFree", "KERNEL32.dll", "VirtualFree"),
        ("__imp_WriteFile", "KERNEL32.dll", "WriteFile"),
    )
    windows_cupidld_imports = tuple(
        sorted(
            windows_cupiddis_imports
            + (
                ("__imp_DeleteFileA", "KERNEL32.dll", "DeleteFileA"),
                (
                    "__imp_FlushFileBuffers",
                    "KERNEL32.dll",
                    "FlushFileBuffers",
                ),
                (
                    "__imp_GetFullPathNameA",
                    "KERNEL32.dll",
                    "GetFullPathNameA",
                ),
                ("__imp_MoveFileExA", "KERNEL32.dll", "MoveFileExA"),
            ),
            key=lambda item: item[2],
        )
    )
    windows_cupiddis_import_selectors = tuple(
        f"{slot}={library}:{procedure}"
        for slot, library, procedure in windows_cupiddis_imports
    )
    stage_two_windows_cupiddis_main = (
        behavior_root / "stage-three-windows-cupiddis-main.o"
    )
    stage_three_windows_cupiddis_main = (
        behavior_root / "stage-four-windows-cupiddis-main.o"
    )
    windows_cupiddis_main_compile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [
            "--root",
            source_root,
            "-D",
            "_WIN32=1",
            "-c",
            "/toolchain/cupiddis_main.cc",
            "-I",
            "/toolchain",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(source_root, stage_two_windows_cupiddis_main),
        ],
        [
            "--root",
            source_root,
            "-D",
            "_WIN32=1",
            "-c",
            "/toolchain/cupiddis_main.cc",
            "-I",
            "/toolchain",
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(source_root, stage_three_windows_cupiddis_main),
        ],
        360,
    )
    _expect_status(
        windows_cupiddis_main_compile_result,
        0,
        "CupidC Windows CupidDis driver",
    )
    if (
        windows_cupiddis_main_compile_result.stdout
        or windows_cupiddis_main_compile_result.stderr
        or stage_two_windows_cupiddis_main.read_bytes()
        != stage_three_windows_cupiddis_main.read_bytes()
    ):
        raise BootstrapError("CupidC Windows CupidDis driver differs")
    _validate_i386_relocatable(stage_two_windows_cupiddis_main)
    stage_two_windows_cupiddis_replacements = {
        "cupiddis_main": stage_two_windows_cupiddis_main,
        "ctool_host": stage_two_windows_host_adapter,
    }
    stage_three_windows_cupiddis_replacements = {
        "cupiddis_main": stage_three_windows_cupiddis_main,
        "ctool_host": stage_three_windows_host_adapter,
    }
    stage_two_windows_cupiddis_link_objects = [
        (
            stage_two_windows_cupiddis_replacements[name]
            if name in stage_two_windows_cupiddis_replacements
            else stage_two.objects[name]
        )
        for name in EXPECTED_LINKS["cupiddis"][1:-1]
    ]
    stage_three_windows_cupiddis_link_objects = [
        (
            stage_three_windows_cupiddis_replacements[name]
            if name in stage_three_windows_cupiddis_replacements
            else stage_three.objects[name]
        )
        for name in EXPECTED_LINKS["cupiddis"][1:-1]
    ]

    stage_two_windows_cupiddis = (
        behavior_root / "stage-three-cupiddis.exe"
    )
    stage_three_windows_cupiddis = (
        behavior_root / "stage-four-cupiddis.exe"
    )
    stage_two_windows_cupiddis_arguments: list[str | Path] = [
        "-m",
        "i386pe",
        "--text-address",
        "0x00401000",
        "--entry",
        "_start",
    ]
    for selector in reversed(windows_cupiddis_import_selectors):
        stage_two_windows_cupiddis_arguments.extend(
            ("--import", selector)
        )
    stage_two_windows_cupiddis_arguments.extend(
        (
            "-o",
            stage_two_windows_cupiddis,
            stage_two_windows_tool_start,
            *stage_two_windows_cupiddis_link_objects,
            stage_two_windows_tool_runtime,
        )
    )
    stage_three_windows_cupiddis_arguments: list[str | Path] = [
        "-m",
        "i386pe",
        "--text-address",
        "0x00401000",
        "--entry",
        "_start",
    ]
    for selector in windows_cupiddis_import_selectors:
        stage_three_windows_cupiddis_arguments.extend(
            ("--import", selector)
        )
    stage_three_windows_cupiddis_arguments.extend(
        (
            "-o",
            stage_three_windows_cupiddis,
            stage_three_windows_tool_start,
            *stage_three_windows_cupiddis_link_objects,
            stage_three_windows_tool_runtime,
        )
    )
    windows_cupiddis_link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        stage_two_windows_cupiddis_arguments,
        stage_three_windows_cupiddis_arguments,
        180,
    )
    _expect_status(
        windows_cupiddis_link_result, 0, "CupidLD Windows CupidDis"
    )
    if (
        windows_cupiddis_link_result.stdout
        or windows_cupiddis_link_result.stderr
        or stage_two_windows_cupiddis.read_bytes()
        != stage_three_windows_cupiddis.read_bytes()
    ):
        raise BootstrapError("Cupid-built Windows CupidDis differs")
    _validate_static_i386_pe32(
        stage_two_windows_cupiddis,
        0x00401000,
        (
            (
                "KERNEL32.dll",
                tuple(
                    procedure
                    for _slot, _library, procedure
                    in windows_cupiddis_imports
                ),
            ),
        ),
    )

    windows_runtime_contract_source = (
        "/toolchain/tests/hosted_i386_windows_runtime_contract.cc"
    )
    stage_two_windows_runtime_contract = (
        behavior_root / "stage-three-windows-runtime-contract.o"
    )
    stage_three_windows_runtime_contract = (
        behavior_root / "stage-four-windows-runtime-contract.o"
    )
    windows_runtime_contract_compile_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidc",
        [
            "--root",
            source_root,
            "-c",
            windows_runtime_contract_source,
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(
                source_root, stage_two_windows_runtime_contract
            ),
        ],
        [
            "--root",
            source_root,
            "-c",
            windows_runtime_contract_source,
            "--include-angle",
            "/toolchain/hosted/i386-linux/include",
            "-o",
            _logical_path(
                source_root, stage_three_windows_runtime_contract
            ),
        ],
        360,
    )
    _expect_status(
        windows_runtime_contract_compile_result,
        0,
        "CupidC Windows runtime contract",
    )
    if (
        windows_runtime_contract_compile_result.stdout
        or windows_runtime_contract_compile_result.stderr
        or stage_two_windows_runtime_contract.read_bytes()
        != stage_three_windows_runtime_contract.read_bytes()
    ):
        raise BootstrapError("CupidC Windows runtime contract differs")
    _validate_i386_relocatable(stage_two_windows_runtime_contract)

    stage_two_windows_runtime_contract_image = (
        behavior_root / "stage-three-windows-runtime-contract.exe"
    )
    stage_three_windows_runtime_contract_image = (
        behavior_root / "stage-four-windows-runtime-contract.exe"
    )
    stage_two_windows_runtime_contract_arguments: list[str | Path] = [
        "-m",
        "i386pe",
        "--text-address",
        "0x00401000",
        "--entry",
        "_start",
    ]
    for selector in windows_cupiddis_import_selectors:
        stage_two_windows_runtime_contract_arguments.extend(
            ("--import", selector)
        )
    stage_two_windows_runtime_contract_arguments.extend(
        (
            "-o",
            stage_two_windows_runtime_contract_image,
            stage_two_windows_tool_start,
            stage_two_windows_runtime_contract,
            stage_two_windows_tool_runtime,
        )
    )
    stage_three_windows_runtime_contract_arguments: list[str | Path] = [
        "-m",
        "i386pe",
        "--text-address",
        "0x00401000",
        "--entry",
        "_start",
    ]
    for selector in reversed(windows_cupiddis_import_selectors):
        stage_three_windows_runtime_contract_arguments.extend(
            ("--import", selector)
        )
    stage_three_windows_runtime_contract_arguments.extend(
        (
            "-o",
            stage_three_windows_runtime_contract_image,
            stage_three_windows_tool_start,
            stage_three_windows_runtime_contract,
            stage_three_windows_tool_runtime,
        )
    )
    windows_runtime_contract_link_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        stage_two_windows_runtime_contract_arguments,
        stage_three_windows_runtime_contract_arguments,
        180,
    )
    _expect_status(
        windows_runtime_contract_link_result,
        0,
        "CupidLD Windows runtime contract",
    )
    if (
        windows_runtime_contract_link_result.stdout
        or windows_runtime_contract_link_result.stderr
        or stage_two_windows_runtime_contract_image.read_bytes()
        != stage_three_windows_runtime_contract_image.read_bytes()
    ):
        raise BootstrapError("Cupid-built Windows runtime contract differs")
    _validate_static_i386_pe32(
        stage_two_windows_runtime_contract_image,
        0x00401000,
        (
            (
                "KERNEL32.dll",
                tuple(
                    procedure
                    for _slot, _library, procedure
                    in windows_cupiddis_imports
                ),
            ),
        ),
    )

    windows_native_tool_plans = {
        "cupidasm": (
            "cupidasm_main",
            "cupidasm",
            "ctool_host",
            "ctool",
            "elf32",
            "x86",
        ),
        "cupidc": (
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
        ),
        "cupidld": (
            "publication_start",
            "cupidld_main",
            "cupidld",
            "ctool_host",
            "ctool",
            "elf32",
            "publication_runtime",
        ),
        "cupidobj": (
            "cupidobj_main",
            "cupidobj",
            "ctool_host",
            "ctool",
            "elf32",
        ),
    }
    windows_native_tool_imports = {
        "cupidasm": windows_cupiddis_imports,
        "cupidc": windows_cupiddis_imports,
        "cupidld": windows_cupidld_imports,
        "cupidobj": windows_cupiddis_imports,
    }
    windows_native_stage_two_extras = {
        "cupidld": {
            "publication_runtime": stage_two_windows_publication_runtime,
            "publication_start": stage_two_windows_publication_start,
        },
    }
    windows_native_stage_three_extras = {
        "cupidld": {
            "publication_runtime": stage_three_windows_publication_runtime,
            "publication_start": stage_three_windows_publication_start,
        },
    }
    windows_native_tool_images: dict[str, Path] = {}
    windows_native_tool_artifacts: dict[str, dict[str, object]] = {}
    for tool_name, link_objects in windows_native_tool_plans.items():
        stage_two_native_tool, _stage_three_native_tool, artifacts = (
            _build_windows_tool_image(
                runner,
                source_root,
                behavior_root,
                stage_two,
                stage_three,
                tool_name,
                link_objects,
                windows_native_tool_imports[tool_name],
                stage_two_windows_tool_start,
                stage_three_windows_tool_start,
                stage_two_windows_tool_runtime,
                stage_three_windows_tool_runtime,
                stage_two_windows_host_adapter,
                stage_three_windows_host_adapter,
                windows_native_stage_two_extras.get(tool_name, {}),
                windows_native_stage_three_extras.get(tool_name, {}),
            )
        )
        windows_native_tool_images[tool_name] = stage_two_native_tool
        windows_native_tool_artifacts[tool_name] = artifacts

    windows_native_tool_loaders: dict[str, dict[str, object]] = {
        name: {"status": "not-run"} for name in windows_native_tool_plans
    }
    if os.name == "nt":
        windows_invalid_assembly = behavior_root / "windows-invalid.asm"
        windows_invalid_assembly.write_text(
            "BITS 16\nthis_is_not_an_instruction ax\n",
            encoding="ascii",
            newline="\n",
        )
        missing_native_input = behavior_root / "missing-native-input.bin"
        native_checks = {
            "cupidasm": (
                [
                    "-f",
                    "bin",
                    str(assembly_source),
                    "-o",
                    str(behavior_root / "native-cupidasm.bin"),
                ],
                [
                    "-f",
                    "bin",
                    str(windows_invalid_assembly),
                    "-o",
                    str(behavior_root / "native-cupidasm-failure.bin"),
                ],
                behavior_root / "native-cupidasm.bin",
                behavior_root / "native-cupidasm-failure.bin",
                stage_two_binary,
                "unknown Cupid ASM instruction mnemonic",
            ),
            "cupidc": (
                [
                    "-c",
                    str(valid_source),
                    "-o",
                    str(behavior_root / "native-cupidc.o"),
                ],
                [
                    "-c",
                    str(invalid_source),
                    "-o",
                    str(behavior_root / "native-cupidc-failure.o"),
                ],
                behavior_root / "native-cupidc.o",
                behavior_root / "native-cupidc-failure.o",
                stage_two_valid,
                "error CT",
            ),
            "cupidobj": (
                [
                    "wrap",
                    str(asset),
                    "--stem",
                    "fixed_point_asset",
                    "--section",
                    ".rodata",
                    "--readonly",
                    "-o",
                    str(behavior_root / "native-cupidobj.o"),
                ],
                [
                    "wrap",
                    str(missing_native_input),
                    "--stem",
                    "missing_native_input",
                    "-o",
                    str(behavior_root / "native-cupidobj-failure.o"),
                ],
                behavior_root / "native-cupidobj.o",
                behavior_root / "native-cupidobj-failure.o",
                stage_two_wrapped,
                "not_found",
            ),
        }
        for tool_name, (
            success_arguments,
            failure_arguments,
            native_output,
            failure_output,
            reference_output,
            failure_diagnostic,
        ) in native_checks.items():
            failure_output.write_bytes(sentinel)
            reference_help = runner.run(
                stage_two.tools[tool_name], ["--help"], 60
            )
            try:
                native_help = subprocess.run(
                    [str(windows_native_tool_images[tool_name]), "--help"],
                    cwd=behavior_root,
                    capture_output=True,
                    text=True,
                    timeout=20,
                )
                native_success = subprocess.run(
                    [
                        str(windows_native_tool_images[tool_name]),
                        *success_arguments,
                    ],
                    cwd=behavior_root,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                native_failure = subprocess.run(
                    [
                        str(windows_native_tool_images[tool_name]),
                        *failure_arguments,
                    ],
                    cwd=behavior_root,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
            except (OSError, subprocess.TimeoutExpired) as error:
                raise BootstrapError(
                    f"Cupid-built Windows {tool_name} could not run: {error}"
                ) from error
            if (
                reference_help.returncode != 0
                or reference_help.stderr
                or native_help.returncode != 0
                or native_help.stdout != reference_help.stdout
                or native_help.stderr
                or native_success.returncode != 0
                or native_success.stdout
                or native_success.stderr
                or native_output.read_bytes() != reference_output.read_bytes()
                or native_failure.returncode != 1
                or native_failure.stdout
                or failure_diagnostic not in native_failure.stderr
                or failure_output.read_bytes() != sentinel
            ):
                raise BootstrapError(
                    f"Cupid-built Windows {tool_name} behavior differs"
                )
            windows_native_tool_loaders[tool_name] = {
                "failure_return_code": native_failure.returncode,
                "help_return_code": native_help.returncode,
                "output_sha256": _sha256(native_output),
                "output_size": native_output.stat().st_size,
                "return_code": native_success.returncode,
                "status": "pass",
            }

        native_cupidld_output = behavior_root / "native linked output.exe"
        native_cupidld_output.write_bytes(sentinel)
        occupied_cupidld_candidate = behavior_root / (
            "native linked output.exe.cupid-tmp-00000000"
        )
        occupied_cupidld_candidate.write_bytes(b"occupied")
        native_cupidld_arguments: list[str] = [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            *(
                value
                for selector in windows_cupiddis_import_selectors
                for value in ("--import", selector)
            ),
        ]
        native_cupidld_arguments.extend(
            (
                "-o",
                native_cupidld_output.name,
                stage_two_windows_tool_start.name,
                stage_two_windows_runtime_contract.name,
                stage_two_windows_tool_runtime.name,
            )
        )
        blocked_cupidld_output = behavior_root / "blocked-output.exe"
        blocked_cupidld_output.mkdir()
        blocked_cupidld_arguments = list(native_cupidld_arguments)
        blocked_cupidld_arguments[
            blocked_cupidld_arguments.index(native_cupidld_output.name)
        ] = blocked_cupidld_output.name
        reference_cupidld_help = runner.run(
            stage_two.tools["cupidld"], ["--help"], 60
        )
        try:
            native_cupidld_help = subprocess.run(
                [str(windows_native_tool_images["cupidld"]), "--help"],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
            native_cupidld_success = subprocess.run(
                [
                    str(windows_native_tool_images["cupidld"]),
                    *native_cupidld_arguments,
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=60,
            )
            native_cupidld_failure = subprocess.run(
                [
                    str(windows_native_tool_images["cupidld"]),
                    *blocked_cupidld_arguments,
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise BootstrapError(
                f"Cupid-built Windows CupidLD could not run: {error}"
            ) from error
        remaining_cupidld_candidates = sorted(
            behavior_root.glob("native linked output.exe.cupid-tmp-*")
        )
        remaining_blocked_candidates = sorted(
            behavior_root.glob("blocked-output.exe.cupid-tmp-*")
        )
        if (
            reference_cupidld_help.returncode != 0
            or reference_cupidld_help.stderr
            or native_cupidld_help.returncode != 0
            or native_cupidld_help.stdout != reference_cupidld_help.stdout
            or native_cupidld_help.stderr
            or native_cupidld_success.returncode != 0
            or native_cupidld_success.stdout
            or native_cupidld_success.stderr
            or native_cupidld_output.read_bytes()
            != stage_two_windows_runtime_contract_image.read_bytes()
            or occupied_cupidld_candidate.read_bytes() != b"occupied"
            or remaining_cupidld_candidates != [occupied_cupidld_candidate]
            or native_cupidld_failure.returncode != 1
            or native_cupidld_failure.stdout
            or "cupidld: link failed (io)" not in native_cupidld_failure.stderr
            or not blocked_cupidld_output.is_dir()
            or remaining_blocked_candidates
        ):
            raise BootstrapError(
                "Cupid-built Windows CupidLD publication behavior differs"
            )
        windows_native_tool_loaders["cupidld"] = {
            "failure_candidate_count": len(remaining_blocked_candidates),
            "failure_return_code": native_cupidld_failure.returncode,
            "help_return_code": native_cupidld_help.returncode,
            "occupied_candidate_sha256": _sha256(
                occupied_cupidld_candidate
            ),
            "output_sha256": _sha256(native_cupidld_output),
            "output_size": native_cupidld_output.stat().st_size,
            "return_code": native_cupidld_success.returncode,
            "status": "pass",
        }

    windows_cupiddis_loader: dict[str, object] = {"status": "not-run"}
    if os.name == "nt":
        windows_cupiddis_input = behavior_root / "quoted input.bin"
        windows_cupiddis_input.write_bytes(bytes([0x90, 0xC3]))
        raw_arguments: list[str | Path] = [
            "--raw",
            "--mode",
            "32",
            "--base",
            "0",
            windows_cupiddis_input,
        ]
        try:
            reference_disassembly = runner.run(
                stage_two.tools["cupiddis"], raw_arguments, 60
            )
            native_help = subprocess.run(
                [str(stage_two_windows_cupiddis), "--help"],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
            native_disassembly = subprocess.run(
                [
                    str(stage_two_windows_cupiddis),
                    *[str(argument) for argument in raw_arguments],
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
            valid_target_input = behavior_root / "valid target.bin"
            valid_target_input.write_bytes(bytes([0xEB, 0x00, 0xC3]))
            valid_target_arguments: list[str | Path] = [
                "--require-known",
                "--require-local-targets",
                "--raw",
                "--mode",
                "32",
                "--base",
                "0",
                valid_target_input,
            ]
            reference_valid_target = runner.run(
                stage_two.tools["cupiddis"], valid_target_arguments, 60
            )
            native_valid_target = subprocess.run(
                [
                    str(stage_two_windows_cupiddis),
                    *[str(argument) for argument in valid_target_arguments],
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
            invalid_target_input = behavior_root / "invalid target.bin"
            invalid_target_input.write_bytes(bytes([0xEB, 0x7F]))
            invalid_target_arguments: list[str | Path] = [
                "--require-known",
                "--require-local-targets",
                "--raw",
                "--mode",
                "32",
                "--base",
                "0",
                invalid_target_input,
            ]
            reference_invalid_target = runner.run(
                stage_two.tools["cupiddis"], invalid_target_arguments, 60
            )
            native_invalid_target = subprocess.run(
                [
                    str(stage_two_windows_cupiddis),
                    *[str(argument) for argument in invalid_target_arguments],
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
            missing_windows_input = behavior_root / "missing quoted input.bin"
            native_missing = subprocess.run(
                [
                    str(stage_two_windows_cupiddis),
                    "--raw",
                    "--mode",
                    "32",
                    "--base",
                    "0",
                    str(missing_windows_input),
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise BootstrapError(
                f"Cupid-built Windows CupidDis could not run: {error}"
            ) from error
        if (
            reference_disassembly.returncode != 0
            or reference_disassembly.stderr
            or native_help.returncode != 0
            or "usage: cupiddis" not in native_help.stdout
            or native_help.stderr
            or native_disassembly.returncode != 0
            or native_disassembly.stdout != reference_disassembly.stdout
            or native_disassembly.stderr
            or reference_valid_target.returncode != 0
            or reference_valid_target.stdout
            or reference_valid_target.stderr
            or native_valid_target.returncode != 0
            or native_valid_target.stdout
            or native_valid_target.stderr
            or reference_invalid_target.returncode != 1
            or reference_invalid_target.stdout
            or "1 of 1 direct relative targets invalid"
            not in reference_invalid_target.stderr
            or "1 outside image" not in reference_invalid_target.stderr
            or native_invalid_target.returncode != 1
            or native_invalid_target.stdout
            or "1 of 1 direct relative targets invalid"
            not in native_invalid_target.stderr
            or "1 outside image" not in native_invalid_target.stderr
            or native_missing.returncode != 1
            or native_missing.stdout
            or "cannot load" not in native_missing.stderr
            or "not_found" not in native_missing.stderr
        ):
            raise BootstrapError("Cupid-built Windows CupidDis behavior differs")
        windows_cupiddis_loader = {
            "help_return_code": native_help.returncode,
            "invalid_target_return_code": native_invalid_target.returncode,
            "local_target_return_code": native_valid_target.returncode,
            "missing_return_code": native_missing.returncode,
            "raw_return_code": native_disassembly.returncode,
            "raw_stdout_sha256": hashlib.sha256(
                native_disassembly.stdout.encode("utf-8")
            ).hexdigest(),
            "raw_stdout_size": len(
                native_disassembly.stdout.encode("utf-8")
            ),
            "status": "pass",
        }

    windows_runtime_contract_loader: dict[str, object] = {
        "status": "not-run"
    }
    if os.name == "nt":
        contract_output = behavior_root / "runtime contract output.bin"
        contract_missing = behavior_root / "runtime missing input.bin"
        contract_arguments = [
            "plain",
            "space arg",
            'quote"arg',
            "trailing\\",
            str(contract_output),
            str(contract_missing),
        ]
        try:
            native_contract = subprocess.run(
                [
                    str(stage_two_windows_runtime_contract_image),
                    *contract_arguments,
                ],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
            native_contract_failure = subprocess.run(
                [str(stage_two_windows_runtime_contract_image), "wrong"],
                cwd=behavior_root,
                capture_output=True,
                text=True,
                timeout=20,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise BootstrapError(
                "Cupid-built Windows runtime contract could not run: "
                f"{error}"
            ) from error
        if (
            native_contract.returncode != 0
            or native_contract.stdout
            != "Cupid-built Windows tool runtime: ok\n"
            or native_contract.stderr
            or not contract_output.is_file()
            or contract_output.read_bytes() != b"headtail"
            or native_contract_failure.returncode != 41
            or native_contract_failure.stdout
            or "windows runtime arguments: bad"
            not in native_contract_failure.stderr
        ):
            raise BootstrapError(
                "Cupid-built Windows runtime contract behavior differs"
            )
        windows_runtime_contract_loader = {
            "failure_return_code": native_contract_failure.returncode,
            "output_sha256": _sha256(contract_output),
            "output_size": contract_output.stat().st_size,
            "return_code": native_contract.returncode,
            "status": "pass",
            "stderr": native_contract.stderr,
            "stdout": native_contract.stdout,
        }

    invalid_import_source = behavior_root / "invalid-import.asm"
    invalid_import_source.write_text(
        "BITS 32\n"
        "extern __imp_ExitProcess\n"
        "global _start\n"
        "section .text\n"
        "_start:\n"
        "    call __imp_ExitProcess\n"
        "    ret\n",
        encoding="utf-8",
        newline="\n",
    )
    stage_two_invalid_import_object = (
        behavior_root / "stage-three-invalid-import.o"
    )
    stage_three_invalid_import_object = (
        behavior_root / "stage-four-invalid-import.o"
    )
    invalid_import_assembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidasm",
        [
            "-f",
            "elf32",
            invalid_import_source,
            "-o",
            stage_two_invalid_import_object,
        ],
        [
            "-f",
            "elf32",
            invalid_import_source,
            "-o",
            stage_three_invalid_import_object,
        ],
    )
    _expect_status(
        invalid_import_assembly_result, 0, "CupidASM invalid import fixture"
    )
    if (
        invalid_import_assembly_result.stdout
        or invalid_import_assembly_result.stderr
        or stage_two_invalid_import_object.read_bytes()
        != stage_three_invalid_import_object.read_bytes()
    ):
        raise BootstrapError("CupidASM invalid import fixture differs")
    invalid_import_object = behavior_root / "invalid-import.o"
    invalid_import_object.write_bytes(
        stage_two_invalid_import_object.read_bytes()
    )
    stage_two_invalid_import_image = (
        behavior_root / "stage-three-invalid-import.exe"
    )
    stage_three_invalid_import_image = (
        behavior_root / "stage-four-invalid-import.exe"
    )
    stage_two_invalid_import_image.write_bytes(sentinel)
    stage_three_invalid_import_image.write_bytes(sentinel)
    invalid_import_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            "--import",
            windows_import_selectors[0],
            "-o",
            stage_two_invalid_import_image,
            invalid_import_object,
        ],
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00401000",
            "--entry",
            "_start",
            "--import",
            windows_import_selectors[0],
            "-o",
            stage_three_invalid_import_image,
            invalid_import_object,
        ],
    )
    _expect_status(
        invalid_import_result, 1, "CupidLD direct IAT call"
    )
    if (
        invalid_import_result.stdout
        or "IAT symbols require an absolute zero-addend relocation"
        not in invalid_import_result.stderr
        or stage_two_invalid_import_image.read_bytes() != sentinel
        or stage_three_invalid_import_image.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidLD import failure behavior differs")

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

    if evidence_out is not None:
        evidence_out["windows_runtime"] = {
            "artifacts": _artifact_inventory(
                {
                    "stage-four-contract": stage_three_windows_contract,
                    "stage-four-image": stage_three_windows_image,
                    "stage-four-start": stage_three_windows_start,
                    "stage-three-contract": stage_two_windows_contract,
                    "stage-three-image": stage_two_windows_image,
                    "stage-three-start": stage_two_windows_start,
                }
            ),
            "imports": [
                {
                    "library": library,
                    "procedure": procedure,
                    "slot": slot,
                }
                for slot, library, procedure in windows_imports
            ],
            "loader": windows_loader,
            "cupiddis": {
                "artifacts": _artifact_inventory(
                    {
                        "stage-four-image": stage_three_windows_cupiddis,
                        "stage-four-host-adapter": (
                            stage_three_windows_host_adapter
                        ),
                        "stage-four-main": (
                            stage_three_windows_cupiddis_main
                        ),
                        "stage-four-runtime": stage_three_windows_tool_runtime,
                        "stage-four-start": stage_three_windows_tool_start,
                        "stage-three-image": stage_two_windows_cupiddis,
                        "stage-three-host-adapter": (
                            stage_two_windows_host_adapter
                        ),
                        "stage-three-main": stage_two_windows_cupiddis_main,
                        "stage-three-runtime": stage_two_windows_tool_runtime,
                        "stage-three-start": stage_two_windows_tool_start,
                    }
                ),
                "imports": [
                    {
                        "library": library,
                        "procedure": procedure,
                        "slot": slot,
                    }
                    for slot, library, procedure in windows_cupiddis_imports
                ],
                "loader": windows_cupiddis_loader,
            },
            "runtime_contract": {
                "artifacts": _artifact_inventory(
                    {
                        "stage-four-contract": (
                            stage_three_windows_runtime_contract
                        ),
                        "stage-four-image": (
                            stage_three_windows_runtime_contract_image
                        ),
                        "stage-three-contract": (
                            stage_two_windows_runtime_contract
                        ),
                        "stage-three-image": (
                            stage_two_windows_runtime_contract_image
                        ),
                    }
                ),
                "loader": windows_runtime_contract_loader,
            },
            "native_tools": {
                tool_name: {
                    "artifacts": windows_native_tool_artifacts[tool_name],
                    "imports": [
                        {
                            "library": library,
                            "procedure": procedure,
                            "slot": slot,
                        }
                        for slot, library, procedure
                        in windows_native_tool_imports[tool_name]
                    ],
                    "loader": windows_native_tool_loaders[tool_name],
                }
                for tool_name in sorted(windows_native_tool_plans)
            },
        }

    ksyms_symbols = behavior_root / "kernel.symbols"
    stage_two_ksyms = behavior_root / "stage-three-ksyms.cc"
    stage_three_ksyms = behavior_root / "stage-four-ksyms.cc"
    ksyms_symbols.write_text(
        "00002000 T second\n"
        "00001000 T first\n"
        "00001000 T duplicate\n"
        "         U unresolved\n"
        "00003000 D data_only\n",
        encoding="ascii",
        newline="\n",
    )
    expected_ksyms = (
        "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
        '#include "ksyms.h"\n\n'
        "/* i386 words preserve the blob bytes with fewer initializers. */\n"
        "const unsigned int\n"
        '__attribute__((section(".ksyms"), used, aligned(4)))\n'
        "ksym_blob[] = {\n"
        "  0x4d59534bu, 0x00000002u, 0x00000020u, 0x0000002du, "
        "0x00001000u, 0x00000000u, 0x00002000u, 0x00000006u,\n"
        "  0x73726966u, 0x65730074u, 0x646e6f63u, 0x00000000u,\n"
        "};\n\n"
        "const unsigned int ksym_blob_size = 45u;\n"
    ).encode("ascii")
    ksyms_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        ["ksyms-source", ksyms_symbols, "-o", stage_two_ksyms],
        ["ksyms-source", ksyms_symbols, "-o", stage_three_ksyms],
    )
    _expect_status(ksyms_result, 0, "CupidObj kernel symbol source")
    if (
        ksyms_result.stdout
        or ksyms_result.stderr
        or stage_two_ksyms.read_bytes() != expected_ksyms
        or stage_three_ksyms.read_bytes() != expected_ksyms
    ):
        raise BootstrapError("CupidObj kernel symbol source differs")

    stage_two_script = behavior_root / "stage-three-script.elf"
    stage_three_script = behavior_root / "stage-four-script.elf"
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
    stage_two_text = behavior_root / "stage-three-text.o"
    stage_three_text = behavior_root / "stage-four-text.o"
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

    stage_two_flat = behavior_root / "stage-three-flat.bin"
    stage_three_flat = behavior_root / "stage-four-flat.bin"
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

    invalid_ksyms = behavior_root / "invalid.symbols"
    stage_two_invalid_ksyms = behavior_root / "stage-three-invalid-ksyms.cc"
    stage_three_invalid_ksyms = behavior_root / "stage-four-invalid-ksyms.cc"
    invalid_ksyms.write_text(
        "100000000 T too_wide\n", encoding="ascii", newline="\n"
    )
    stage_two_invalid_ksyms.write_bytes(sentinel)
    stage_three_invalid_ksyms.write_bytes(sentinel)
    invalid_ksyms_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidobj",
        ["ksyms-source", invalid_ksyms, "-o", stage_two_invalid_ksyms],
        ["ksyms-source", invalid_ksyms, "-o", stage_three_invalid_ksyms],
    )
    _expect_status(
        invalid_ksyms_result, 1, "CupidObj invalid kernel symbols"
    )
    if (
        invalid_ksyms_result.stdout
        or "address is outside i386" not in invalid_ksyms_result.stderr
        or stage_two_invalid_ksyms.read_bytes() != sentinel
        or stage_three_invalid_ksyms.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidObj kernel symbol failure differs")

    invalid_assembly = behavior_root / "invalid.asm"
    invalid_assembly.write_text(
        "BITS 16\nthis_is_not_an_instruction ax\n",
        encoding="utf-8",
        newline="\n",
    )
    stage_two_invalid_assembly = behavior_root / "stage-three-invalid.bin"
    stage_three_invalid_assembly = behavior_root / "stage-four-invalid.bin"
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

    strict_disassembly_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        ["--require-known", stage_two_valid],
        ["--require-known", stage_three_valid],
    )
    _expect_status(
        strict_disassembly_result, 0, "CupidDis strict clean input"
    )
    if strict_disassembly_result.stdout or strict_disassembly_result.stderr:
        raise BootstrapError("CupidDis strict clean behavior differs")

    unowned_relocation = behavior_root / "unowned-relocation.o"
    unowned_relocation.write_bytes(_unowned_relocation_object_payload())
    relocation_ownership_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        ["--require-known", unowned_relocation],
        ["--require-known", unowned_relocation],
    )
    _expect_status(
        relocation_ownership_result,
        1,
        "CupidDis relocation ownership",
    )
    if (
        relocation_ownership_result.stdout
        or "1 of 1 executable relocations unmatched"
        not in relocation_ownership_result.stderr
    ):
        raise BootstrapError("CupidDis relocation ownership differs")

    _check_relocatable_local_target_behavior(
        runner,
        stage_two,
        stage_three,
        behavior_root,
        "",
    )
    _check_executable_local_target_behavior(
        runner,
        stage_two,
        stage_three,
        behavior_root,
        "",
    )

    truncated_code = behavior_root / "truncated-code.bin"
    truncated_code.write_bytes(bytes([0x0F]))
    strict_failure_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupiddis",
        [
            "--require-known",
            "--raw",
            "--mode=32",
            "--base=0",
            truncated_code,
        ],
    )
    _expect_status(
        strict_failure_result, 1, "CupidDis strict truncated input"
    )
    if (
        strict_failure_result.stdout
        or "truncated-code.bin: code check failed: "
        "0 known, 0 unknown, 0 invalid, 1 truncated"
        not in strict_failure_result.stderr
    ):
        raise BootstrapError("CupidDis strict failure behavior differs")

    stage_two_pe32_failure = behavior_root / "stage-three-invalid-pe32.exe"
    stage_three_pe32_failure = behavior_root / "stage-four-invalid-pe32.exe"
    stage_two_pe32_failure.write_bytes(sentinel)
    stage_three_pe32_failure.write_bytes(sentinel)
    invalid_pe32_result = _run_stage_pair(
        runner,
        stage_two,
        stage_three,
        "cupidld",
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00402000",
            "--entry",
            "_start",
            "-o",
            stage_two_pe32_failure,
            stage_two_link_object,
        ],
        [
            "-m",
            "i386pe",
            "--text-address",
            "0x00402000",
            "--entry",
            "_start",
            "-o",
            stage_three_pe32_failure,
            stage_three_link_object,
        ],
    )
    _expect_status(
        invalid_pe32_result, 1, "CupidLD invalid PE32 text address"
    )
    if (
        invalid_pe32_result.stdout
        or "CupidLD PE32 requires text address 0x00401000"
        not in invalid_pe32_result.stderr
        or stage_two_pe32_failure.read_bytes() != sentinel
        or stage_three_pe32_failure.read_bytes() != sentinel
    ):
        raise BootstrapError("CupidLD PE32 failure behavior differs")

    stage_two_link_failure = behavior_root / "stage-three-link-failure.elf"
    stage_three_link_failure = behavior_root / "stage-four-link-failure.elf"
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

    stage_two_obj_failure = behavior_root / "stage-three-obj-failure.o"
    stage_three_obj_failure = behavior_root / "stage-four-obj-failure.o"
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
        "failure_cases": 20,
        "help_cases": 5,
        "success_cases": 21,
    }


def _artifact_inventory(paths: dict[str, Path]) -> dict[str, object]:
    return {
        name: {
            "sha256": _sha256(path),
            "size": path.stat().st_size,
        }
        for name, path in sorted(paths.items())
    }


def _require_bootstrap_output_available(output_root: Path) -> None:
    if output_root.is_symlink():
        raise BootstrapError("bootstrap output may not be a symlink")
    if not output_root.exists():
        return
    if not output_root.is_dir():
        raise BootstrapError("bootstrap output path is not a directory")
    occupied = sorted(path.name for path in output_root.iterdir())
    if occupied:
        visible = ", ".join(occupied[:3])
        if len(occupied) > 3:
            visible += f", and {len(occupied) - 3} more"
        raise BootstrapError(
            f"bootstrap output directory is not empty: {visible}"
        )


def _require_complete_publication(publication_root: Path) -> None:
    if publication_root.is_symlink() or not publication_root.is_dir():
        raise BootstrapError("bootstrap publication is not a directory")
    missing = [
        name
        for name in BOOTSTRAP_PUBLICATION_NAMES
        if not (publication_root / name).exists()
    ]
    if missing:
        raise BootstrapError(
            "bootstrap publication is incomplete: " + ", ".join(missing)
        )
    for name in ("stage-two", "stage-three", "stage-four", "behavior"):
        path = publication_root / name
        if path.is_symlink() or not path.is_dir():
            raise BootstrapError(
                f"bootstrap publication directory is invalid: {name}"
            )
    report_path = publication_root / "bootstrap-report.json"
    if report_path.is_symlink() or not report_path.is_file():
        raise BootstrapError("bootstrap publication report is invalid")
    unexpected = sorted(
        path.name
        for path in publication_root.iterdir()
        if path.name not in BOOTSTRAP_PUBLICATION_NAMES
    )
    if unexpected:
        raise BootstrapError(
            "bootstrap publication contains unexpected entries: "
            + ", ".join(unexpected)
        )


def publish_bootstrap_outputs(
    publication_root: Path,
    output_root: Path,
) -> None:
    """Publish one complete fixed-point evidence bundle."""
    _require_complete_publication(publication_root)
    output_root.parent.mkdir(parents=True, exist_ok=True)
    _require_bootstrap_output_available(output_root)
    restore_empty_output = output_root.exists()
    if restore_empty_output:
        try:
            output_root.rmdir()
        except OSError as error:
            raise BootstrapError(
                f"cannot prepare bootstrap output publication: {error}"
            ) from error
    try:
        publication_root.replace(output_root)
    except OSError as error:
        if restore_empty_output and not output_root.exists():
            try:
                output_root.mkdir()
            except OSError as restore_error:
                raise BootstrapError(
                    "cannot publish bootstrap output and cannot restore "
                    f"its empty directory: {restore_error}"
                ) from error
        raise BootstrapError(
            f"cannot publish bootstrap output: {error}"
        ) from error


def _bootstrap_from_frozen_seed(
    seed_inputs: SeedInputs,
    source_root: Path,
    output_root: Path,
    *,
    compare_fixed_point: bool,
) -> dict[str, object]:
    seed_tools = seed_inputs.tools
    manifest = seed_inputs.manifest
    plan = _require_object(manifest.get("build_plan"), "build_plan")
    source_root = source_root.resolve()
    if output_root.is_symlink():
        raise BootstrapError("bootstrap output may not be a symlink")
    output_root = output_root.resolve()
    if not (source_root / "toolchain").is_dir():
        raise BootstrapError(f"source root has no toolchain: {source_root}")
    _logical_path(source_root, output_root)
    if output_root == source_root:
        raise BootstrapError(
            "bootstrap output may not replace the source root"
        )
    output_root.parent.mkdir(parents=True, exist_ok=True)
    _require_bootstrap_output_available(output_root)

    with tempfile.TemporaryDirectory(
        prefix=f".{output_root.name}-private-",
        dir=output_root.parent,
    ) as temporary:
        private_workspace = Path(temporary)
        source_inputs = freeze_source_inputs(
            source_root,
            plan,
            private_workspace / "source",
        )
        private_source_root = source_inputs.root
        require_source_closures(source_inputs, source_root, plan)
        require_live_seed_inputs(seed_inputs)
        source_snapshot = source_inputs.inventory
        source_snapshot_digest = _source_snapshot_sha256(
            source_snapshot
        )
        runner = ToolRunner(private_source_root)
        seed_producers = {
            name: seed_tools[name] for name in PRODUCER_NAMES
        }
        stage_two = _build_stage(
            runner,
            private_source_root,
            private_source_root / "stage-two",
            seed_producers,
            plan,
            "stage two",
        )
        require_source_closures(source_inputs, source_root, plan)
        require_live_seed_inputs(seed_inputs)
        stage_two_producers = {
            name: stage_two.tools[name] for name in PRODUCER_NAMES
        }
        stage_three = _build_stage(
            runner,
            private_source_root,
            private_source_root / "stage-three",
            stage_two_producers,
            plan,
            "stage three",
        )
        require_source_closures(source_inputs, source_root, plan)
        require_live_seed_inputs(seed_inputs)
        stage_three_producers = {
            name: stage_three.tools[name] for name in PRODUCER_NAMES
        }
        stage_four = _build_stage(
            runner,
            private_source_root,
            private_source_root / "stage-four",
            stage_three_producers,
            plan,
            "stage four",
        )
        require_source_closures(source_inputs, source_root, plan)
        require_live_seed_inputs(seed_inputs)
        raw_sources = _require_list(
            plan.get("sources"), "build_plan.sources"
        )
        source_names = [
            str(_require_object(source, "build source")["name"])
            for source in raw_sources
        ]
        comparisons = (
            _compare_stages(stage_three, stage_four, source_names)
            if compare_fixed_point
            else None
        )
        behavior_evidence: dict[str, object] = {}
        behavior = _run_behavior_checks(
            runner,
            private_source_root,
            private_source_root,
            stage_three,
            stage_four,
            behavior_evidence,
        )
        windows_runtime = behavior_evidence.get("windows_runtime")
        if not isinstance(windows_runtime, dict):
            raise BootstrapError("Windows runtime evidence is absent")
        windows_runtime_artifacts = windows_runtime.get("artifacts")
        windows_loader = windows_runtime.get("loader")
        windows_cupiddis = windows_runtime.get("cupiddis")
        windows_runtime_contract = windows_runtime.get("runtime_contract")
        windows_native_tools = windows_runtime.get("native_tools")
        if not isinstance(windows_runtime_artifacts, dict) or not isinstance(
            windows_loader, dict
        ):
            raise BootstrapError("Windows runtime evidence is malformed")
        if not isinstance(windows_cupiddis, dict) or not isinstance(
            windows_cupiddis.get("loader"), dict
        ):
            raise BootstrapError("Windows CupidDis evidence is malformed")
        if not isinstance(windows_runtime_contract, dict) or not isinstance(
            windows_runtime_contract.get("loader"), dict
        ):
            raise BootstrapError(
                "Windows runtime contract evidence is malformed"
            )
        if not isinstance(windows_native_tools, dict) or set(
            windows_native_tools
        ) != {"cupidasm", "cupidc", "cupidld", "cupidobj"}:
            raise BootstrapError("Windows native-tool evidence is malformed")
        for tool_name in ("cupidasm", "cupidc", "cupidld", "cupidobj"):
            tool_evidence = windows_native_tools.get(tool_name)
            if not isinstance(tool_evidence, dict) or not isinstance(
                tool_evidence.get("loader"), dict
            ):
                raise BootstrapError(
                    f"Windows {tool_name} evidence is malformed"
                )
        seed_matches_stage_two = _compare_seed_with_stage_two(
            seed_inputs, stage_two
        )
        report: dict[str, object] = {
            "behavior": behavior,
            "behavior_generations": ["stage-three", "stage-four"],
            "build_plan_sha256": manifest["build_plan_sha256"],
            "host_execution": {
                "windows_cupidasm": windows_native_tools["cupidasm"][
                    "loader"
                ],
                "windows_cupidc": windows_native_tools["cupidc"]["loader"],
                "windows_cupiddis": windows_cupiddis["loader"],
                "windows_cupidld": windows_native_tools["cupidld"]["loader"],
                "windows_loader": windows_loader,
                "windows_cupidobj": windows_native_tools["cupidobj"][
                    "loader"
                ],
                "windows_runtime_contract": (
                    windows_runtime_contract["loader"]
                ),
            },
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
                "stage-four": {
                    "objects": _artifact_inventory(stage_four.objects),
                    "producer_generation": "stage-three",
                    "tools": _artifact_inventory(stage_four.tools),
                },
                "stage-three": {
                    "objects": _artifact_inventory(
                        stage_three.objects
                    ),
                    "producer_generation": "stage-two",
                    "tools": _artifact_inventory(stage_three.tools),
                },
                "stage-two": {
                    "objects": _artifact_inventory(stage_two.objects),
                    "producer_generation": "checked-seed",
                    "tools": _artifact_inventory(stage_two.tools),
                },
            },
            "status": (
                "pass"
                if compare_fixed_point
                else "pending-fixed-point-author"
            ),
            "target": EXPECTED_TARGET,
            "windows_runtime": windows_runtime,
        }
        if comparisons is not None:
            report["comparisons"] = comparisons
        encoded_report = (
            json.dumps(
                report, indent=2, sort_keys=True, ensure_ascii=True
            )
            + "\n"
        ).encode("ascii")
        report_path = private_source_root / "bootstrap-report.json"
        temporary_report = (
            private_source_root / "bootstrap-report.json.tmp"
        )
        temporary_report.write_bytes(encoded_report)
        temporary_report.replace(report_path)

        publication_root = private_workspace / "publication"
        publication_root.mkdir(mode=0o755)
        for name in BOOTSTRAP_PUBLICATION_NAMES:
            (private_source_root / name).replace(
                publication_root / name
            )
        require_source_closures(source_inputs, source_root, plan)
        require_live_seed_inputs(seed_inputs)
        publish_bootstrap_outputs(publication_root, output_root)
        return report


def _bootstrap_windows_from_frozen_seed(
    seed_inputs: SeedInputs,
    plan_inputs: SeedInputs,
    source_root: Path,
    output_root: Path,
) -> dict[str, object]:
    seed_tools = seed_inputs.tools
    linux_plan = _require_object(
        plan_inputs.manifest.get("build_plan"), "build_plan"
    )
    native_plan = _windows_build_plan(linux_plan)
    source_root = source_root.resolve()
    if output_root.is_symlink():
        raise BootstrapError("bootstrap output may not be a symlink")
    output_root = output_root.resolve()
    if not (source_root / "toolchain").is_dir():
        raise BootstrapError(f"source root has no toolchain: {source_root}")
    _logical_path(source_root, output_root)
    if output_root == source_root:
        raise BootstrapError(
            "bootstrap output may not replace the source root"
        )
    output_root.parent.mkdir(parents=True, exist_ok=True)
    _require_bootstrap_output_available(output_root)

    with tempfile.TemporaryDirectory(
        prefix=f".{output_root.name}-private-",
        dir=output_root.parent,
    ) as temporary:
        private_workspace = Path(temporary)
        source_inputs = freeze_source_inputs(
            source_root,
            linux_plan,
            private_workspace / "source",
        )
        private_source_root = source_inputs.root
        require_source_closures(
            source_inputs, source_root, linux_plan
        )
        require_live_seed_inputs(seed_inputs, plan_inputs)
        source_snapshot = source_inputs.inventory
        source_snapshot_digest = _source_snapshot_sha256(
            source_snapshot
        )
        runner = ToolRunner(private_source_root)
        seed_producers = {
            name: seed_tools[name] for name in PRODUCER_NAMES
        }
        stage_two = _build_windows_stage(
            runner,
            private_source_root,
            private_source_root / "stage-two",
            seed_producers,
            native_plan,
            "stage two",
        )
        require_source_closures(
            source_inputs, source_root, linux_plan
        )
        require_live_seed_inputs(seed_inputs, plan_inputs)
        stage_two_producers = {
            name: stage_two.tools[name] for name in PRODUCER_NAMES
        }
        stage_three = _build_windows_stage(
            runner,
            private_source_root,
            private_source_root / "stage-three",
            stage_two_producers,
            native_plan,
            "stage three",
        )
        require_source_closures(
            source_inputs, source_root, linux_plan
        )
        require_live_seed_inputs(seed_inputs, plan_inputs)
        stage_three_producers = {
            name: stage_three.tools[name] for name in PRODUCER_NAMES
        }
        stage_four = _build_windows_stage(
            runner,
            private_source_root,
            private_source_root / "stage-four",
            stage_three_producers,
            native_plan,
            "stage four",
        )
        require_source_closures(
            source_inputs, source_root, linux_plan
        )
        require_live_seed_inputs(seed_inputs, plan_inputs)
        raw_sources = _require_list(
            native_plan.get("sources"), "Windows build plan sources"
        )
        c_source_names = [
            str(_require_object(source, "Windows build source")["name"])
            for source in raw_sources
        ]
        raw_assembly = _require_list(
            native_plan.get("assembly_sources"),
            "Windows build plan assembly sources",
        )
        assembly_names = [
            str(
                _require_object(
                    source, "Windows assembly source"
                )["name"]
            )
            for source in raw_assembly
        ]
        comparisons = _compare_windows_stages(
            stage_three,
            stage_four,
            c_source_names,
            assembly_names,
        )
        behavior = _run_native_windows_behavior_checks(
            runner,
            private_source_root,
            stage_three,
            stage_four,
            native_plan,
        )
        seed_matches_stage_two = _compare_seed_with_stage_two(
            seed_inputs, stage_two
        )
        report: dict[str, object] = {
            "behavior": behavior,
            "behavior_generations": ["stage-three", "stage-four"],
            "build_plan": native_plan,
            "build_plan_sha256": _build_plan_sha256(native_plan),
            "comparisons": comparisons,
            "initial_seed_matches_stage_two": seed_matches_stage_two,
            "plan_manifest_sha256": plan_inputs.manifest_sha256,
            "platform": "windows-native",
            "schema": WINDOWS_REPORT_SCHEMA,
            "seed_manifest_sha256": seed_inputs.manifest_sha256,
            "source_inputs": {
                "count": len(source_snapshot),
                "files": source_snapshot,
                "sha256": source_snapshot_digest,
            },
            "source_snapshot_sha256": source_snapshot_digest,
            "stages": {
                "stage-four": {
                    "objects": _artifact_inventory(stage_four.objects),
                    "producer_generation": "native-stage-three",
                    "tools": _artifact_inventory(stage_four.tools),
                },
                "stage-three": {
                    "objects": _artifact_inventory(
                        stage_three.objects
                    ),
                    "producer_generation": "native-stage-two",
                    "tools": _artifact_inventory(stage_three.tools),
                },
                "stage-two": {
                    "objects": _artifact_inventory(stage_two.objects),
                    "producer_generation": (
                        "checked-windows-execution-seed"
                    ),
                    "tools": _artifact_inventory(stage_two.tools),
                },
            },
            "status": "pass",
            "target": EXPECTED_WINDOWS_TARGET,
        }
        encoded_report = (
            json.dumps(
                report, indent=2, sort_keys=True, ensure_ascii=True
            )
            + "\n"
        ).encode("ascii")
        report_path = private_source_root / "bootstrap-report.json"
        temporary_report = (
            private_source_root / "bootstrap-report.json.tmp"
        )
        temporary_report.write_bytes(encoded_report)
        temporary_report.replace(report_path)

        publication_root = private_workspace / "publication"
        publication_root.mkdir(mode=0o755)
        for name in BOOTSTRAP_PUBLICATION_NAMES:
            (private_source_root / name).replace(
                publication_root / name
            )
        require_source_closures(
            source_inputs, source_root, linux_plan
        )
        require_live_seed_inputs(seed_inputs, plan_inputs)
        publish_bootstrap_outputs(publication_root, output_root)
        return report


def bootstrap_windows_from_seed(
    manifest_path: Path,
    plan_manifest_path: Path,
    source_root: Path,
    output_root: Path,
) -> dict[str, object]:
    """Build three native PE generations from the checked Windows seed."""
    with tempfile.TemporaryDirectory(
        prefix="cupid-windows-bootstrap-inputs-"
    ) as temporary:
        temporary_root = Path(temporary)
        seed_inputs = freeze_seed_inputs(
            manifest_path, temporary_root / "execution-seed"
        )
        if seed_inputs.manifest.get("schema") != WINDOWS_SEED_SCHEMA:
            raise BootstrapError(
                "native Windows bootstrap requires a Windows execution seed"
            )
        plan_inputs = freeze_seed_inputs(
            plan_manifest_path, temporary_root / "plan-seed"
        )
        if plan_inputs.manifest.get("schema") != SEED_SCHEMA:
            raise BootstrapError(
                "native Windows bootstrap requires a Linux build-plan seed"
            )
        if os.name != "nt":
            raise BootstrapError(
                "native Windows bootstrap requires a Windows host"
            )
        return _bootstrap_windows_from_frozen_seed(
            seed_inputs,
            plan_inputs,
            source_root,
            output_root,
        )


def _bootstrap_from_seed_with_policy(
    manifest_path: Path,
    source_root: Path,
    output_root: Path,
    *,
    compare_fixed_point: bool,
) -> dict[str, object]:
    with tempfile.TemporaryDirectory(
        prefix="cupid-bootstrap-seed-inputs-"
    ) as temporary:
        seed_inputs = freeze_seed_inputs(
            manifest_path, Path(temporary)
        )
        if seed_inputs.manifest.get("schema") != SEED_SCHEMA:
            raise BootstrapError(
                "the native Windows execution seed cannot drive the "
                "Linux fixed-point bootstrap"
            )
        return _bootstrap_from_frozen_seed(
            seed_inputs,
            source_root,
            output_root,
            compare_fixed_point=compare_fixed_point,
        )


def bootstrap_from_seed(
    manifest_path: Path,
    source_root: Path,
    output_root: Path,
) -> dict[str, object]:
    return _bootstrap_from_seed_with_policy(
        manifest_path,
        source_root,
        output_root,
        compare_fixed_point=True,
    )


def _bootstrap_for_manifest_author(
    manifest_path: Path,
    source_root: Path,
    output_root: Path,
) -> dict[str, object]:
    return _bootstrap_from_seed_with_policy(
        manifest_path,
        source_root,
        output_root,
        compare_fixed_point=False,
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
        help="build through stage four and compare the last two stages",
    )
    bootstrap.add_argument("--manifest", required=True, type=Path)
    bootstrap.add_argument("--root", required=True, type=Path)
    bootstrap.add_argument("--output", required=True, type=Path)
    windows_bootstrap = subparsers.add_parser(
        "bootstrap-windows",
        help=(
            "build through native Windows stage four and compare the last "
            "two stages"
        ),
    )
    windows_bootstrap.add_argument(
        "--manifest", required=True, type=Path
    )
    windows_bootstrap.add_argument(
        "--plan-manifest", required=True, type=Path
    )
    windows_bootstrap.add_argument("--root", required=True, type=Path)
    windows_bootstrap.add_argument("--output", required=True, type=Path)
    run = subparsers.add_parser(
        "run",
        help="verify, freeze, and run one checked-seed tool",
    )
    run.add_argument("--manifest", required=True, type=Path)
    run.add_argument("--root", required=True, type=Path)
    run.add_argument("--tool", required=True, choices=TOOL_NAMES)
    run.add_argument("--timeout", type=int, default=300)
    run.add_argument("tool_arguments", nargs=argparse.REMAINDER)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        if arguments.command == "verify":
            seed = verify_seed_inputs(arguments.manifest)
            target = _require_object(seed.manifest.get("target"), "target")
            operating_system = str(target["operating_system"]).title()
            print(
                f"checked i386 {operating_system} seed: ok "
                f"({len(seed.tools)} tools)"
            )
            return 0
        if arguments.command == "bootstrap":
            bootstrap_from_seed(
                arguments.manifest, arguments.root, arguments.output
            )
            print(
                "checked i386 Linux bootstrap: ok "
                "(stage three equals stage four)"
            )
            return 0
        if arguments.command == "bootstrap-windows":
            bootstrap_windows_from_seed(
                arguments.manifest,
                arguments.plan_manifest,
                arguments.root,
                arguments.output,
            )
            print(
                "checked i386 Windows bootstrap: ok "
                "(native stage three equals stage four)"
            )
            return 0
        if arguments.command == "run":
            tool_arguments = list(arguments.tool_arguments)
            if tool_arguments[:1] == ["--"]:
                tool_arguments = tool_arguments[1:]
            result = run_seed_tool(
                arguments.manifest,
                arguments.root,
                arguments.tool,
                tool_arguments,
                timeout=arguments.timeout,
            )
            sys.stdout.write(result.stdout or "")
            sys.stderr.write(result.stderr or "")
            return result.returncode
    except BootstrapError as error:
        if arguments.command == "verify":
            prefix = "bootstrap seed verification failed"
        elif arguments.command == "run":
            prefix = "checked seed tool failed"
        elif arguments.command == "bootstrap-windows":
            prefix = "checked Windows bootstrap failed"
        else:
            prefix = "checked bootstrap failed"
        print(f"{prefix}: {error}", file=sys.stderr)
        return 1
    raise AssertionError(f"unhandled command: {arguments.command}")


if __name__ == "__main__":
    raise SystemExit(main())

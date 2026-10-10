#!/usr/bin/env python3
"""Freeze and run the native Windows Cupid tools used by user builds."""

from __future__ import annotations

import hashlib
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


NATIVE_TOOL_PATHS = {
    "cupidc": "toolchain/build/cupidc.exe",
    "cupidld": "toolchain/build/cupidld.exe",
}

NATIVE_USER_TOOL_SOURCES = (
    "toolchain/Makefile",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/cupidc_emit.cc",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.cc",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.cc",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_main.cc",
    "toolchain/cupidc_pp.cc",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.cc",
    "toolchain/cupidc_type.h",
    "toolchain/cupidld.cc",
    "toolchain/cupidld.h",
    "toolchain/cupidld_main.cc",
    "toolchain/elf32.cc",
    "toolchain/elf32.h",
    "toolchain/x86.cc",
    "toolchain/x86.h",
)

PE_MACHINE_AMD64 = 0x8664
PE32_PLUS_MAGIC = 0x020B
PE_SUBSYSTEM_WINDOWS_CUI = 3


class NativeToolError(RuntimeError):
    """A native Cupid tool did not meet the Windows user-build contract."""


def validate_native_tool_bytes(payload: bytes, label: str) -> None:
    """Require a 64-bit Windows console executable."""
    if len(payload) < 64 or payload[0:2] != b"MZ":
        raise NativeToolError(f"native {label} is not a PE executable")
    pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
    if pe_offset > len(payload) - 24:
        raise NativeToolError(f"native {label} has a truncated PE header")
    if payload[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise NativeToolError(f"native {label} has no PE signature")
    machine = struct.unpack_from("<H", payload, pe_offset + 4)[0]
    optional_bytes = struct.unpack_from("<H", payload, pe_offset + 20)[0]
    optional_offset = pe_offset + 24
    if machine != PE_MACHINE_AMD64:
        raise NativeToolError(
            f"native {label} does not target Windows AMD64"
        )
    if (
        optional_bytes < 70
        or optional_offset > len(payload)
        or optional_bytes > len(payload) - optional_offset
    ):
        raise NativeToolError(
            f"native {label} has a truncated optional header"
        )
    magic = struct.unpack_from("<H", payload, optional_offset)[0]
    subsystem = struct.unpack_from("<H", payload, optional_offset + 68)[0]
    if magic != PE32_PLUS_MAGIC:
        raise NativeToolError(
            f"native {label} does not use the PE32+ format"
        )
    if subsystem != PE_SUBSYSTEM_WINDOWS_CUI:
        raise NativeToolError(
            f"native {label} is not a Windows console executable"
        )


def _resolved_root(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise NativeToolError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise NativeToolError(
            f"repository root is not a directory: {resolved}"
        )
    return resolved


@dataclass(frozen=True)
class NativeToolSnapshot:
    """One captured native tool with a checked live-file identity."""

    label: str
    live_path: Path
    payload: bytes
    sha256: str

    def stage(self, directory: Path) -> Path:
        destination = directory / self.live_path.name
        try:
            destination.write_bytes(self.payload)
            destination.chmod(0o700)
        except OSError as error:
            raise NativeToolError(
                f"native {self.label} snapshot could not be staged: {error}"
            ) from error
        return destination

    def require_unchanged(self, operation: str) -> None:
        try:
            payload = self.live_path.read_bytes()
        except OSError as error:
            raise NativeToolError(
                f"native {self.label} changed while {operation}: {error}"
            ) from error
        if (
            len(payload) != len(self.payload)
            or hashlib.sha256(payload).hexdigest() != self.sha256
            or payload != self.payload
        ):
            raise NativeToolError(
                f"native {self.label} changed while {operation}"
            )


def capture_native_tool(
    root: Path,
    name: str,
    requested: Path | None = None,
) -> NativeToolSnapshot:
    """Capture one approved native user-build tool."""
    root = _resolved_root(root)
    relative = NATIVE_TOOL_PATHS.get(name)
    if relative is None:
        raise NativeToolError(f"unknown native Cupid tool: {name}")
    label = "CupidC" if name == "cupidc" else "CupidLD"
    expected = root / relative
    candidate = expected if requested is None else requested
    if not candidate.is_absolute():
        candidate = root / candidate
    if candidate.is_symlink():
        raise NativeToolError(f"native {label} may not be a symlink")
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise NativeToolError(
            f"native {label} is unavailable: {error}"
        ) from error
    if resolved != expected.resolve(strict=False):
        raise NativeToolError(
            f"native {label} must be {relative}"
        )
    if expected.is_symlink():
        raise NativeToolError(f"native {label} may not be a symlink")
    if not resolved.is_file():
        raise NativeToolError(
            f"native {label} is not a regular file: {resolved}"
        )
    try:
        payload = resolved.read_bytes()
    except OSError as error:
        raise NativeToolError(
            f"native {label} cannot be read: {error}"
        ) from error
    validate_native_tool_bytes(payload, label)
    return NativeToolSnapshot(
        label=label,
        live_path=resolved,
        payload=payload,
        sha256=hashlib.sha256(payload).hexdigest(),
    )


class NativeToolExecutor:
    """Run a captured Windows Cupid tool without a Linux execution bridge."""

    def __init__(self, root: Path):
        self.root = _resolved_root(root)

    def compiler_root_for(self, path: Path) -> str:
        return str(path.resolve())

    def run(
        self,
        executable: Path,
        arguments: Sequence[str | Path],
        timeout: int,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(executable), *[str(argument) for argument in arguments]],
            cwd=self.root,
            text=True,
            capture_output=True,
            timeout=timeout,
        )

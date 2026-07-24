#!/usr/bin/env python3
"""Compile the approved kernel crypto cohort with the checked CupidC seed."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        WSL_PRIVATE_RUN_SCRIPT,
        freeze_seed_inputs,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        WSL_PRIVATE_RUN_SCRIPT,
        freeze_seed_inputs,
    )


APPROVED_CRYPTO_SOURCES = (
    "kernel/crypto/aes.c",
    "kernel/crypto/aes_gcm.c",
    "kernel/crypto/asn1.c",
    "kernel/crypto/bigint.c",
    "kernel/crypto/chacha20.c",
    "kernel/crypto/chacha20poly1305.c",
    "kernel/crypto/csprng.c",
    "kernel/crypto/ct.c",
    "kernel/crypto/ecdsa.c",
    "kernel/crypto/ed25519.c",
    "kernel/crypto/hkdf.c",
    "kernel/crypto/hmac.c",
    "kernel/crypto/p256.c",
    "kernel/crypto/poly1305.c",
    "kernel/crypto/rsa.c",
    "kernel/crypto/sha256.c",
    "kernel/crypto/sha512.c",
    "kernel/crypto/x25519.c",
    "kernel/crypto/x509.c",
    "kernel/crypto/x509_chain.c",
)
BLOCKED_CRYPTO_SOURCES = ()
KERNEL_CRYPTO_C_SOURCES = tuple(
    sorted(APPROVED_CRYPTO_SOURCES + BLOCKED_CRYPTO_SOURCES)
)

KERNEL_I386_ARGUMENTS = (
    "--gnu",
    "--freestanding",
    "-D",
    "__GNUC__=1",
    "-D",
    "__ORDER_LITTLE_ENDIAN__=1234",
    "-D",
    "__ORDER_BIG_ENDIAN__=4321",
    "-D",
    "__ORDER_PDP_ENDIAN__=3412",
    "-D",
    "__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__",
    "-D",
    "__SSE2__=1",
    "-D",
    "DEBUG=1",
    "-I",
    "/kernel",
    "-I",
    "/kernel/audio",
    "-I",
    "/kernel/core",
    "-I",
    "/kernel/cpu",
    "-I",
    "/kernel/crypto",
    "-I",
    "/kernel/doom",
    "-I",
    "/kernel/fs",
    "-I",
    "/kernel/gfx",
    "-I",
    "/kernel/gui",
    "-I",
    "/kernel/lang",
    "-I",
    "/kernel/mm",
    "-I",
    "/kernel/network",
    "-I",
    "/kernel/smp",
    "-I",
    "/kernel/tls",
    "-I",
    "/kernel/usb",
    "-I",
    "/kernel/util",
    "-I",
    "/drivers",
    "-I",
    "/toolchain",
)

DEFAULT_TIMEOUT_SECONDS = 180


class KernelCompileError(RuntimeError):
    """A checked kernel compilation could not publish an object."""


def build_compile_arguments(
    logical_source: str,
    logical_output: str,
    compiler_root: str,
) -> tuple[str, ...]:
    """Build the complete, fixed KERNEL_I386 CupidC argument vector."""
    return (
        "-c",
        logical_source,
        "-o",
        logical_output,
        *KERNEL_I386_ARGUMENTS,
        "--root",
        compiler_root,
    )


def build_wsl_invocation(
    linux_root: str,
    linux_seed: str,
    arguments: Sequence[str],
) -> tuple[str, ...]:
    """Build a WSL command that runs a private copy of the checked seed."""
    return (
        "wsl",
        "-e",
        "sh",
        "-c",
        WSL_PRIVATE_RUN_SCRIPT,
        "sh",
        linux_root,
        linux_seed,
        *arguments,
    )


class SeedExecutor:
    """Run the static Linux seed natively or through private WSL staging."""

    def __init__(self, root: Path):
        self.root = root.resolve()
        self.uses_wsl = os.name == "nt"
        if self.uses_wsl:
            if shutil.which("wsl") is None:
                raise KernelCompileError(
                    "WSL is required to run the checked i386 Linux seed"
                )
            self.compiler_root = self._wsl_path(self.root)
        else:
            self.compiler_root = str(self.root)

    @staticmethod
    def _wsl_path(path: Path) -> str:
        try:
            result = subprocess.run(
                ["wsl", "-e", "wslpath", "-a", str(path.resolve())],
                text=True,
                capture_output=True,
            )
        except OSError as error:
            raise KernelCompileError(
                f"WSL could not translate {path}: {error}"
            ) from error
        if result.returncode != 0 or not result.stdout.strip():
            details = result.stderr.strip() or f"status {result.returncode}"
            raise KernelCompileError(
                f"WSL could not translate {path}: {details}"
            )
        return result.stdout.strip()

    def run(
        self,
        executable: Path,
        arguments: Sequence[str],
        timeout: int,
    ) -> subprocess.CompletedProcess[str]:
        if not self.uses_wsl:
            return subprocess.run(
                [str(executable), *arguments],
                cwd=self.root,
                text=True,
                capture_output=True,
                timeout=timeout,
            )

        command = build_wsl_invocation(
            self.compiler_root,
            self._wsl_path(executable),
            arguments,
        )
        return subprocess.run(
            command,
            text=True,
            capture_output=True,
            timeout=timeout,
        )


def _read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise KernelCompileError(
            f"cannot read emitted object {path}: {error}"
        ) from error


def validate_i386_relocatable_bytes(image: bytes) -> None:
    """Validate the ELF32 structure and relocation contract CupidLD consumes."""
    if len(image) < 52:
        raise KernelCompileError("ELF header is outside the emitted object")
    if image[0:7] != b"\x7fELF\x01\x01\x01":
        raise KernelCompileError(
            "emitted object is not little-endian ELF32 version 1"
        )
    (
        object_type,
        machine,
        version,
        _entry,
        program_offset,
        section_offset,
        _flags,
        header_size,
        program_entry_size,
        program_count,
        section_entry_size,
        section_count,
        section_name_index,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", image, 16)
    if object_type != 1 or machine != 3 or version != 1:
        raise KernelCompileError(
            "emitted object is not an i386 ELF32 relocatable object"
        )
    if header_size != 52 or section_entry_size != 40:
        raise KernelCompileError(
            "emitted object has an invalid ELF or section header size"
        )
    if program_count != 0 or program_offset != 0 or program_entry_size != 0:
        raise KernelCompileError(
            "emitted relocatable object unexpectedly has program headers"
        )
    if section_count == 0 or section_name_index >= section_count:
        raise KernelCompileError(
            "emitted object has an invalid section table"
        )
    section_bytes = section_count * section_entry_size
    if (
        section_offset > len(image)
        or section_bytes > len(image) - section_offset
    ):
        raise KernelCompileError(
            "emitted object has a truncated section header table"
        )

    sections = []
    for index in range(section_count):
        section = struct.unpack_from(
            "<IIIIIIIIII",
            image,
            section_offset + index * section_entry_size,
        )
        (
            _name,
            section_type,
            _section_flags,
            _section_address,
            payload_offset,
            payload_size,
            _link,
            _info,
            alignment,
            _entry_size,
        ) = section
        if (
            section_type != 8
            and (
                payload_offset > len(image)
                or payload_size > len(image) - payload_offset
            )
        ):
            raise KernelCompileError(
                f"emitted object section {index} payload is outside the file"
            )
        if alignment != 0 and alignment & (alignment - 1):
            raise KernelCompileError(
                f"emitted object section {index} alignment is not a power of two"
            )
        if (
            section_type != 8
            and alignment > 1
            and payload_offset % alignment != 0
        ):
            raise KernelCompileError(
                f"emitted object section {index} payload is misaligned"
            )
        sections.append(section)

    name_section = sections[section_name_index]
    if name_section[1] != 3:
        raise KernelCompileError(
            "emitted object section name table is not a string table"
        )
    name_data = image[name_section[4] : name_section[4] + name_section[5]]
    section_names = []
    for index, section in enumerate(sections):
        name_offset = section[0]
        if name_offset >= len(name_data):
            raise KernelCompileError(
                f"emitted object section {index} name is outside the string table"
            )
        terminator = name_data.find(b"\0", name_offset)
        if terminator < 0:
            raise KernelCompileError(
                f"emitted object section {index} name is not terminated"
            )
        try:
            section_names.append(
                name_data[name_offset:terminator].decode("ascii")
            )
        except UnicodeDecodeError as error:
            raise KernelCompileError(
                f"emitted object section {index} name is not ASCII"
            ) from error

    required_sections = {".text", ".symtab", ".strtab", ".shstrtab"}
    missing_sections = sorted(required_sections - set(section_names))
    if missing_sections:
        raise KernelCompileError(
            "emitted object is missing required section "
            + ", ".join(missing_sections)
        )

    symbol_counts = {}
    for section_index, section in enumerate(sections):
        if section[1] != 2:
            continue
        payload_offset = section[4]
        payload_size = section[5]
        string_index = section[6]
        first_nonlocal = section[7]
        entry_size = section[9]
        if entry_size != 16 or payload_size % entry_size != 0:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has invalid entries"
            )
        if string_index >= len(sections) or sections[string_index][1] != 3:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has no string table"
            )
        symbol_count = payload_size // entry_size
        symbol_counts[section_index] = symbol_count
        if symbol_count == 0 or first_nonlocal > symbol_count:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has an invalid boundary"
            )
        string_section = sections[string_index]
        string_data = image[
            string_section[4] : string_section[4] + string_section[5]
        ]
        for symbol_index in range(symbol_count):
            (
                symbol_name,
                symbol_value,
                symbol_size,
                _symbol_info,
                _symbol_other,
                symbol_section,
            ) = struct.unpack_from(
                "<IIIBBH",
                image,
                payload_offset + symbol_index * entry_size,
            )
            if (
                symbol_name >= len(string_data)
                or string_data.find(b"\0", symbol_name) < 0
            ):
                raise KernelCompileError(
                    f"emitted object symbol {symbol_index} has an invalid name"
                )
            if symbol_section < 0xFF00:
                if symbol_section >= len(sections):
                    raise KernelCompileError(
                        f"emitted object symbol {symbol_index} has an invalid section"
                    )
                if symbol_section != 0:
                    target_size = sections[symbol_section][5]
                    if (
                        symbol_value > target_size
                        or symbol_size > target_size - symbol_value
                    ):
                        raise KernelCompileError(
                            f"emitted object symbol {symbol_index} exceeds its section"
                        )
            elif symbol_section not in (0xFFF1, 0xFFF2):
                raise KernelCompileError(
                    f"emitted object symbol {symbol_index} has an unsupported section"
                )
    if not symbol_counts:
        raise KernelCompileError("emitted object has no symbol table")

    for section_index, section in enumerate(sections):
        if section[1] == 4:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} uses RELA"
            )
        if section[1] != 9:
            continue
        payload_offset = section[4]
        payload_size = section[5]
        symbol_table_index = section[6]
        target_index = section[7]
        entry_size = section[9]
        if entry_size != 8 or payload_size % entry_size != 0:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has invalid entries"
            )
        if symbol_table_index not in symbol_counts:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has no symbol table"
            )
        if target_index == 0 or target_index >= len(sections):
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has no target"
            )
        target = sections[target_index]
        if target[1] == 8:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} targets NOBITS"
            )
        relocation_count = payload_size // entry_size
        for relocation_index in range(relocation_count):
            relocation_offset, relocation_info = struct.unpack_from(
                "<II",
                image,
                payload_offset + relocation_index * entry_size,
            )
            if relocation_offset + 4 > target[5]:
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} "
                    "is outside its target"
                )
            symbol_index = relocation_info >> 8
            if symbol_index >= symbol_counts[symbol_table_index]:
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} "
                    "has an invalid symbol"
                )
            relocation_type = relocation_info & 0xFF
            if relocation_type not in (1, 2):
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} uses "
                    f"unsupported i386 type {relocation_type}"
                )
            addend = struct.unpack_from(
                "<i",
                image,
                target[4] + relocation_offset,
            )[0]
            expected_addend = 0 if relocation_type == 1 else -4
            if addend != expected_addend:
                description = (
                    "absolute"
                    if relocation_type == 1
                    else "PC-relative"
                )
                raise KernelCompileError(
                    f"{description} relocation addend is {addend}, "
                    f"expected {expected_addend}"
                )


def validate_i386_relocatable(path: Path) -> None:
    validate_i386_relocatable_bytes(_read_bytes(path))


def _root_path(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise KernelCompileError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise KernelCompileError(
            f"repository root is not a directory: {resolved}"
        )
    return resolved


def _source_path(root: Path, source: Path) -> tuple[Path, str]:
    candidate = source if source.is_absolute() else root / source
    if candidate.is_symlink():
        raise KernelCompileError("approved source may not be a symlink")
    try:
        resolved = candidate.resolve(strict=True)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise KernelCompileError(
            f"source must resolve inside repository root: {source}"
        ) from error
    relative_name = relative.as_posix()
    if relative_name not in APPROVED_CRYPTO_SOURCES:
        raise KernelCompileError(
            "source is outside the approved kernel crypto cohort: "
            f"{relative_name}"
        )
    if not resolved.is_file():
        raise KernelCompileError(f"approved source is not a file: {relative_name}")
    return resolved, "/" + relative_name


def _output_path(root: Path, output: Path) -> tuple[Path, str]:
    candidate = output if output.is_absolute() else root / output
    if candidate.is_symlink():
        raise KernelCompileError("output may not be a symlink")
    try:
        parent = candidate.parent.resolve(strict=True)
        resolved = (parent / candidate.name).resolve(strict=False)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise KernelCompileError(
            f"output must stay inside repository root: {output}"
        ) from error
    if not parent.is_dir():
        raise KernelCompileError(f"output parent is not a directory: {parent}")
    if not relative.parts or resolved == root:
        raise KernelCompileError("output must name a file inside repository root")
    if resolved.exists() and not resolved.is_file():
        raise KernelCompileError(f"output is not a regular file: {resolved}")
    if resolved.suffix != ".o":
        raise KernelCompileError("kernel compiler output must use the .o suffix")
    return resolved, "/" + relative.as_posix()


def compile_kernel_crypto(
    root: Path,
    source: Path,
    output: Path,
    *,
    manifest: Path | None = None,
    executor: SeedExecutor | None = None,
    timeout: int = DEFAULT_TIMEOUT_SECONDS,
) -> None:
    """Compile one approved source and atomically publish a checked object."""
    root = _root_path(root)
    source, logical_source = _source_path(root, source)
    output, _logical_output = _output_path(root, output)
    if source == output:
        raise KernelCompileError("output may not replace an approved source")
    if timeout <= 0:
        raise KernelCompileError("compiler timeout must be positive")

    manifest_path = (
        manifest.resolve()
        if manifest is not None
        else root
        / "bootstrap"
        / "seeds"
        / "i386-linux"
        / "manifest.json"
    )
    active_executor = executor if executor is not None else SeedExecutor(root)
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupidc-kernel-seed-"
        ) as seed_temporary:
            try:
                seed_inputs = freeze_seed_inputs(
                    manifest_path, Path(seed_temporary)
                )
            except BootstrapError as error:
                raise KernelCompileError(
                    f"checked seed verification failed: {error}"
                ) from error
            seed = seed_inputs.tools.get("cupidc")
            if seed is None:
                raise KernelCompileError(
                    "checked seed verification did not return CupidC"
                )

            with tempfile.TemporaryDirectory(
                prefix=f".{output.name}.cupidc-",
                dir=output.parent,
            ) as temporary:
                temporary_output = Path(temporary) / output.name
                logical_temporary = (
                    "/" + temporary_output.relative_to(root).as_posix()
                )
                arguments = build_compile_arguments(
                    logical_source,
                    logical_temporary,
                    active_executor.compiler_root,
                )
                try:
                    result = active_executor.run(seed, arguments, timeout)
                except subprocess.TimeoutExpired as error:
                    raise KernelCompileError(
                        f"CupidC timed out after {timeout} seconds for "
                        f"{logical_source.lstrip('/')}"
                    ) from error
                except OSError as error:
                    raise KernelCompileError(
                        f"CupidC could not run for "
                        f"{logical_source.lstrip('/')}: {error}"
                    ) from error
                if result.returncode != 0:
                    details = (result.stderr or "").strip()
                    if not details:
                        details = (result.stdout or "").strip()
                    suffix = f": {details}" if details else ""
                    raise KernelCompileError(
                        f"CupidC failed for {logical_source.lstrip('/')} "
                        f"with status {result.returncode}{suffix}"
                    )
                if (
                    temporary_output.is_symlink()
                    or not temporary_output.is_file()
                ):
                    raise KernelCompileError(
                        f"CupidC did not publish an object for "
                        f"{logical_source.lstrip('/')}"
                    )
                try:
                    validate_i386_relocatable(temporary_output)
                except KernelCompileError as error:
                    raise KernelCompileError(
                        f"emitted object is invalid for "
                        f"{logical_source.lstrip('/')}: {error}"
                    ) from error
                os.replace(temporary_output, output)
    except OSError as error:
        raise KernelCompileError(
            f"could not publish kernel object {output}: {error}"
        ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compile one approved kernel crypto source with the checked "
            "CupidC seed."
        )
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        compile_kernel_crypto(
            arguments.root,
            arguments.source,
            arguments.output,
            manifest=arguments.manifest,
            timeout=arguments.timeout,
        )
    except KernelCompileError as error:
        print(f"CupidC kernel compile failed: {error}", file=sys.stderr)
        return 1
    print(f"CupidC kernel object: {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

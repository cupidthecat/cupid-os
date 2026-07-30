#!/usr/bin/env python3
"""Link approved Cupid OS user programs with a frozen CupidLD tool."""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
from contextlib import ExitStack
from pathlib import Path

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        ToolRunner,
        freeze_seed_inputs,
    )
    from tools.cupidc_kernel_compile import validate_i386_relocatable
    from tools.native_user_toolchain import (
        NativeToolError,
        NativeToolExecutor,
        capture_native_tool,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import BootstrapError, ToolRunner, freeze_seed_inputs
    from cupidc_kernel_compile import validate_i386_relocatable
    from native_user_toolchain import (
        NativeToolError,
        NativeToolExecutor,
        capture_native_tool,
    )


USER_PROGRAMS = ("cat", "hello", "ls")
USER_TEXT_ADDRESS = 0x01C00000
USER_ARENA_END = 0x01E00000
USER_MAX_PROGRAM_HEADERS = 16
ELF_PT_NULL = 0
ELF_PT_LOAD = 1
ELF_PT_GNU_STACK = 0x6474E551
ELF_PF_KNOWN = 7
DEFAULT_TIMEOUT_SECONDS = 60
TOOL_MODES = ("auto", "checked-seed", "native-windows")


class UserLinkError(RuntimeError):
    """A user-program link could not publish an executable."""


def _checked_add(left: int, right: int, label: str) -> int:
    result = left + right
    if result > 0xFFFFFFFF:
        raise UserLinkError(f"{label} overflows the i386 address space")
    return result


def validate_user_executable_bytes(image: bytes) -> None:
    """Check the external ELF contract enforced by the Cupid OS loader."""
    if len(image) < 52:
        raise UserLinkError("ELF header is outside the linked executable")
    if image[0:7] != b"\x7fELF\x01\x01\x01":
        raise UserLinkError(
            "linked executable is not little-endian ELF32 version 1"
        )
    (
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
        _section_name_index,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", image, 16)
    if file_type != 2 or machine != 3 or version != 1:
        raise UserLinkError("linked executable is not an i386 ELF32 executable")
    if header_size != 52 or program_entry_size != 32 or program_count == 0:
        raise UserLinkError("linked executable has an invalid program table")
    if program_count > USER_MAX_PROGRAM_HEADERS:
        raise UserLinkError(
            f"linked executable has more than "
            f"{USER_MAX_PROGRAM_HEADERS} program headers"
        )
    if program_offset < 52 or program_offset > 0x7FFFFFFF:
        raise UserLinkError(
            "linked executable has an invalid program-header offset"
        )
    program_bytes = program_count * program_entry_size
    if (
        program_offset > len(image)
        or program_bytes > len(image) - program_offset
    ):
        raise UserLinkError("linked executable has a truncated program table")

    load_count = 0
    entry_is_executable = False
    load_ranges: list[tuple[int, int]] = []
    for index in range(program_count):
        (
            segment_type,
            file_offset,
            virtual_address,
            _physical_address,
            file_size,
            memory_size,
            flags,
            alignment,
        ) = struct.unpack_from(
            "<IIIIIIII",
            image,
            program_offset + index * program_entry_size,
        )
        if segment_type not in (ELF_PT_NULL, ELF_PT_LOAD, ELF_PT_GNU_STACK):
            raise UserLinkError(
                f"program header {index} has an unsupported program type"
            )
        if flags & ~ELF_PF_KNOWN:
            raise UserLinkError(
                f"program header {index} has unknown permission flags"
            )
        if alignment != 0 and alignment & (alignment - 1):
            raise UserLinkError(
                f"program header {index} alignment is not a power of two"
            )
        if segment_type != ELF_PT_LOAD:
            if file_size != 0 or memory_size != 0:
                raise UserLinkError(
                    f"non-load program header has a payload at index {index}"
                )
            continue
        if (
            alignment > 1
            and file_offset & (alignment - 1)
            != virtual_address & (alignment - 1)
        ):
            raise UserLinkError(
                f"load segment {index} alignment is incongruent"
            )
        if file_size > memory_size:
            raise UserLinkError(
                f"load segment {index} has more file bytes than memory bytes"
            )
        file_end = _checked_add(file_offset, file_size, "load file range")
        memory_end = _checked_add(
            virtual_address, memory_size, "load memory range"
        )
        if file_end > len(image):
            raise UserLinkError(
                f"load segment {index} extends beyond the executable"
            )
        if file_size > 0 and file_offset > 0x7FFFFFFF:
            raise UserLinkError(
                f"load segment {index} cannot be reached by the loader"
            )
        if memory_size == 0:
            continue
        if (
            virtual_address < USER_TEXT_ADDRESS
            or memory_end > USER_ARENA_END
        ):
            raise UserLinkError(
                f"load segment {index} is outside the external executable arena"
            )
        if any(
            virtual_address < previous_end
            and previous_start < memory_end
            for previous_start, previous_end in load_ranges
        ):
            raise UserLinkError("linked executable load segments overlap")
        load_ranges.append((virtual_address, memory_end))
        load_count += 1
        if (
            flags & 1
            and virtual_address <= entry
            and entry - virtual_address < file_size
        ):
            entry_is_executable = True
    if load_count == 0:
        raise UserLinkError(
            "linked executable has no nonempty loadable segment"
        )
    if not entry_is_executable:
        raise UserLinkError(
            "entry point is not in executable file-backed bytes"
        )


def validate_user_executable(path: Path) -> None:
    try:
        image = path.read_bytes()
    except OSError as error:
        raise UserLinkError(
            f"cannot read linked executable {path}: {error}"
        ) from error
    validate_user_executable_bytes(image)


def _root_path(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise UserLinkError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise UserLinkError(f"repository root is not a directory: {resolved}")
    return resolved


def _inside_root_file(root: Path, path: Path, label: str) -> Path:
    candidate = path if path.is_absolute() else root / path
    if candidate.is_symlink():
        raise UserLinkError(f"{label} may not be a symlink")
    try:
        resolved = candidate.resolve(strict=True)
        resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise UserLinkError(
            f"{label} must resolve inside the repository: {path}"
        ) from error
    if not resolved.is_file():
        raise UserLinkError(f"{label} is not a regular file: {resolved}")
    return resolved


def _output_path(root: Path, output: Path) -> Path:
    candidate = output if output.is_absolute() else root / output
    if candidate.is_symlink():
        raise UserLinkError("output may not be a symlink")
    try:
        parent = candidate.parent.resolve(strict=True)
        resolved = (parent / candidate.name).resolve(strict=False)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise UserLinkError(
            f"output must stay inside the repository: {output}"
        ) from error
    if not parent.is_dir() or not relative.parts:
        raise UserLinkError(f"output parent is not a directory: {parent}")
    if resolved.exists() and not resolved.is_file():
        raise UserLinkError(f"output is not a regular file: {resolved}")
    return resolved


def _approved_pair(root: Path, source: Path, output: Path) -> tuple[Path, Path]:
    source = _inside_root_file(root, source, "input object")
    output = _output_path(root, output)
    try:
        source_relative = source.relative_to(root)
        output_relative = output.relative_to(root)
    except ValueError as error:
        raise UserLinkError("user link paths left the repository") from error
    if (
        len(source_relative.parts) < 3
        or source_relative.parts[0] != "user"
        or output_relative.parts[:-1] != source_relative.parts[:-1]
        or source.name != output.name + ".o"
        or output.name not in USER_PROGRAMS
    ):
        raise UserLinkError(
            "user link must pair an approved build object and executable"
        )
    return source, output


def link_user_program(
    root: Path,
    source: Path,
    output: Path,
    *,
    text_address: int = USER_TEXT_ADDRESS,
    entry: str = "_start",
    manifest: Path | None = None,
    native_linker: Path | None = None,
    tool_mode: str = "auto",
    runner: ToolRunner | NativeToolExecutor | None = None,
    timeout: int = DEFAULT_TIMEOUT_SECONDS,
) -> None:
    """Link one approved user object and publish its executable atomically."""
    root = _root_path(root)
    source, output = _approved_pair(root, source, output)
    if text_address != USER_TEXT_ADDRESS:
        raise UserLinkError(
            f"user text address must be 0x{USER_TEXT_ADDRESS:08X}"
        )
    if entry != "_start":
        raise UserLinkError("user entry symbol must be _start")
    if timeout <= 0:
        raise UserLinkError("linker timeout must be positive")
    if tool_mode not in TOOL_MODES:
        raise UserLinkError(f"unknown user linker mode: {tool_mode}")
    if native_linker is not None and tool_mode == "checked-seed":
        raise UserLinkError(
            "a native linker cannot be used in checked-seed mode"
        )
    if manifest is not None and (
        native_linker is not None or tool_mode == "native-windows"
    ):
        raise UserLinkError(
            "a checked-seed manifest cannot be combined with a native linker"
        )
    use_native = (
        native_linker is not None
        or tool_mode == "native-windows"
        or (
            tool_mode == "auto"
            and runner is None
            and manifest is None
            and os.name == "nt"
        )
    )
    try:
        source_payload = source.read_bytes()
    except OSError as error:
        raise UserLinkError(f"input object is invalid: {error}") from error
    before = hashlib.sha256(source_payload).hexdigest()

    try:
        with ExitStack() as stack:
            native_snapshot = None
            if use_native:
                try:
                    native_snapshot = capture_native_tool(
                        root, "cupidld", native_linker
                    )
                    tool_directory = Path(
                        stack.enter_context(
                            tempfile.TemporaryDirectory(
                                prefix="cupidld-user-native-"
                            )
                        )
                    )
                    linker = native_snapshot.stage(tool_directory)
                    active_runner = (
                        runner
                        if runner is not None
                        else NativeToolExecutor(root)
                    )
                except NativeToolError as error:
                    raise UserLinkError(str(error)) from error
            else:
                manifest_path = (
                    manifest.resolve()
                    if manifest is not None
                    else root
                    / "bootstrap"
                    / "seeds"
                    / "i386-linux"
                    / "manifest.json"
                )
                try:
                    active_runner = (
                        runner
                        if runner is not None
                        else ToolRunner(root)
                    )
                except BootstrapError as error:
                    raise UserLinkError(
                        f"checked seed runner is unavailable: {error}"
                    ) from error
                seed_directory = Path(
                    stack.enter_context(
                        tempfile.TemporaryDirectory(
                            prefix="cupidld-user-seed-"
                        )
                    )
                )
                try:
                    seed_inputs = freeze_seed_inputs(
                        manifest_path, seed_directory
                    )
                except (BootstrapError, OSError) as error:
                    raise UserLinkError(
                        f"checked seed verification failed: {error}"
                    ) from error
                linker = seed_inputs.tools.get("cupidld")
                if linker is None:
                    raise UserLinkError(
                        "checked seed verification did not return CupidLD"
                    )

            temporary = stack.enter_context(
                tempfile.TemporaryDirectory(
                    prefix=f".{output.name}.cupidld-",
                    dir=output.parent,
                )
            )
            temporary_root = Path(temporary)
            temporary_input = temporary_root / source.name
            temporary_output = temporary_root / output.name
            temporary_input.write_bytes(source_payload)
            try:
                validate_i386_relocatable(temporary_input)
            except Exception as error:
                raise UserLinkError(
                    f"input object is invalid: {error}"
                ) from error

            arguments: tuple[str | Path, ...] = (
                "-m",
                "elf_i386",
                "--text-address",
                f"0x{text_address:08X}",
                "--entry",
                entry,
                "-o",
                temporary_output,
                temporary_input,
            )
            try:
                result = active_runner.run(linker, arguments, timeout)
            except subprocess.TimeoutExpired as error:
                raise UserLinkError(
                    f"CupidLD timed out after {timeout} seconds for "
                    f"{source.name}"
                ) from error
            except OSError as error:
                raise UserLinkError(
                    f"CupidLD could not run for {source.name}: {error}"
                ) from error
            if result.returncode != 0:
                details = (result.stderr or result.stdout or "").strip()
                suffix = f": {details}" if details else ""
                raise UserLinkError(
                    f"CupidLD failed for {source.name} with status "
                    f"{result.returncode}{suffix}"
                )
            if temporary_output.is_symlink() or not temporary_output.is_file():
                raise UserLinkError(
                    f"CupidLD did not write an executable for {source.name}"
                )
            validate_user_executable(temporary_output)
            if native_snapshot is not None:
                try:
                    native_snapshot.require_unchanged(
                        "linking with native CupidLD"
                    )
                except NativeToolError as error:
                    raise UserLinkError(str(error)) from error
            try:
                after = hashlib.sha256(source.read_bytes()).hexdigest()
            except OSError as error:
                raise UserLinkError(
                    f"input object changed while linking {source.name}: "
                    f"{error}"
                ) from error
            if after != before:
                raise UserLinkError(
                    f"input object changed while linking {source.name}"
                )
            os.replace(temporary_output, output)
    except OSError as error:
        raise UserLinkError(
            f"could not publish user executable {output}: {error}"
        ) from error


def _parse_address(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"invalid i386 address: {value}"
        ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Link an approved user program with CupidLD."
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--text-address",
        type=_parse_address,
        default=USER_TEXT_ADDRESS,
    )
    parser.add_argument("--entry", default="_start")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument(
        "--tool-mode",
        choices=TOOL_MODES,
        default="auto",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        link_user_program(
            arguments.root,
            arguments.input,
            arguments.output,
            text_address=arguments.text_address,
            entry=arguments.entry,
            manifest=arguments.manifest,
            tool_mode=arguments.tool_mode,
            timeout=arguments.timeout,
        )
    except UserLinkError as error:
        print(f"CupidLD user link failed: {error}", file=sys.stderr)
        return 1
    print(f"CupidLD user executable: {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

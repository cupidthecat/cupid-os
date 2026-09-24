#!/usr/bin/env python3
"""Compile approved Cupid OS production sources with a frozen CupidC tool."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
import stat
import subprocess
import sys
import tempfile
from contextlib import ExitStack
from pathlib import Path
from typing import Sequence

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        ToolRunner,
        freeze_seed_inputs,
        run_seed_tool,
    )
    from tools.cupidc_kernel_compile import (
        KERNEL_I386_ARGUMENTS,
        validate_i386_relocatable,
    )
    from tools.native_user_toolchain import (
        NativeToolError,
        NativeToolExecutor,
        capture_native_tool,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        ToolRunner,
        freeze_seed_inputs,
        run_seed_tool,
    )
    from cupidc_kernel_compile import (
        KERNEL_I386_ARGUMENTS,
        validate_i386_relocatable,
    )
    from native_user_toolchain import (
        NativeToolError,
        NativeToolExecutor,
        capture_native_tool,
    )


USER_SOURCES = (
    "user/examples/cat.cc",
    "user/examples/hello.cc",
    "user/examples/ls.cc",
)
GENERATED_INSTALL_SOURCES = (
    "kernel/util/bin_programs_gen.cc",
    "kernel/util/demos_programs_gen.cc",
    "kernel/util/docs_programs_gen.cc",
)
APPROVED_SOURCES = {
    "user": USER_SOURCES,
    "generated-install": GENERATED_INSTALL_SOURCES,
}

USER_I386_ARGUMENTS = (
    "--freestanding",
    "-I",
    "/user",
)

USER_INCLUDE_CLOSURE = (
    "user/cupid.h",
)
GENERATED_INCLUDE_CLOSURE = (
    "drivers/serial.h",
    "kernel/core/types.h",
    "kernel/fs/homefs.h",
    "kernel/fs/ramfs.h",
    "kernel/fs/vfs.h",
)

DEFAULT_TIMEOUT_SECONDS = 180
TOOL_MODES = ("auto", "checked-seed", "native-windows")
_HOST_IS_WINDOWS = os.name == "nt"
_POSIX_DIRECTORY_WALK_SUPPORTED = (
    getattr(os, "O_DIRECTORY", 0) != 0
    and getattr(os, "O_NOFOLLOW", 0) != 0
    and os.open in os.supports_dir_fd
    and os.mkdir in os.supports_dir_fd
)
_WINDOWS_SYNCHRONIZE = 0x00100000
_WINDOWS_FILE_READ_ATTRIBUTES = 0x0080
_WINDOWS_FILE_TRAVERSE = 0x0020
_WINDOWS_DIRECTORY_ACCESS = (
    _WINDOWS_SYNCHRONIZE
    | _WINDOWS_FILE_READ_ATTRIBUTES
    | _WINDOWS_FILE_TRAVERSE
)
_WINDOWS_FILE_SHARE_READ = 0x0001
_WINDOWS_FILE_SHARE_WRITE = 0x0002
_WINDOWS_DIRECTORY_SHARE = (
    _WINDOWS_FILE_SHARE_READ | _WINDOWS_FILE_SHARE_WRITE
)
_WINDOWS_OPEN_EXISTING = 3
_WINDOWS_FILE_OPEN_IF = 3
_WINDOWS_FILE_ATTRIBUTE_DIRECTORY = 0x0010
_WINDOWS_FILE_ATTRIBUTE_REPARSE_POINT = 0x0400
_WINDOWS_FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
_WINDOWS_FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
_WINDOWS_FILE_DIRECTORY_FILE = 0x00000001
_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
_WINDOWS_OBJECT_CASE_INSENSITIVE = 0x0040
_WINDOWS_OBJECT_DONT_REPARSE = 0x1000
_WINDOWS_FILE_ATTRIBUTE_TAG_INFO_CLASS = 9
_WINDOWS_ERROR_DIRECTORY = 267
_WINDOWS_ERROR_REPARSE_POINT_ENCOUNTERED = 4395


def _default_seed_manifest(root: Path) -> Path:
    platform = "i386-windows" if _HOST_IS_WINDOWS else "i386-linux"
    return root / "bootstrap" / "seeds" / platform / "manifest.json"


class ProductionCompileError(RuntimeError):
    """A production compilation could not publish an object."""


def profile_arguments(cohort: str) -> tuple[str, ...]:
    if cohort == "user":
        return USER_I386_ARGUMENTS
    if cohort == "generated-install":
        return KERNEL_I386_ARGUMENTS
    raise ProductionCompileError(f"unknown production cohort: {cohort}")


def build_compile_arguments(
    cohort: str,
    logical_source: str,
    logical_output: str,
    compiler_root: str | Path,
) -> tuple[str | Path, ...]:
    return (
        "-c",
        logical_source,
        "-o",
        logical_output,
        *profile_arguments(cohort),
        "--root",
        compiler_root,
    )


def _root_path(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise ProductionCompileError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise ProductionCompileError(
            f"repository root is not a directory: {resolved}"
        )
    return resolved


def _approved_source(
    root: Path, cohort: str, source: Path
) -> tuple[Path, str]:
    if cohort not in APPROVED_SOURCES:
        raise ProductionCompileError(f"unknown production cohort: {cohort}")
    candidate = source if source.is_absolute() else root / source
    if candidate.is_symlink():
        raise ProductionCompileError("production source may not be a symlink")
    try:
        resolved = candidate.resolve(strict=True)
        relative = resolved.relative_to(root).as_posix()
    except (OSError, ValueError) as error:
        raise ProductionCompileError(
            f"source must resolve inside the repository: {source}"
        ) from error
    if relative not in APPROVED_SOURCES[cohort]:
        raise ProductionCompileError(
            f"source is outside the approved {cohort} cohort: {relative}"
        )
    if not resolved.is_file():
        raise ProductionCompileError(
            f"approved source is not a file: {relative}"
        )
    return resolved, "/" + relative


def _output_path(root: Path, output: Path) -> Path:
    candidate = output if output.is_absolute() else root / output
    if candidate.is_symlink():
        raise ProductionCompileError("output may not be a symlink")
    try:
        parent = candidate.parent.resolve(strict=True)
        resolved = (parent / candidate.name).resolve(strict=False)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise ProductionCompileError(
            f"output must stay inside the repository: {output}"
        ) from error
    if not parent.is_dir() or not relative.parts:
        raise ProductionCompileError(
            f"output parent is not a repository directory: {parent}"
        )
    if resolved.exists() and not resolved.is_file():
        raise ProductionCompileError(
            f"output is not a regular file: {resolved}"
        )
    if resolved.suffix != ".o":
        raise ProductionCompileError("compiler output must use the .o suffix")
    return resolved


def _pin_posix_directory(
    path: str | Path,
    stack: ExitStack,
    *,
    parent_fd: int | None = None,
) -> int:
    directory_flag = getattr(os, "O_DIRECTORY", 0)
    no_follow_flag = getattr(os, "O_NOFOLLOW", 0)
    if not _POSIX_DIRECTORY_WALK_SUPPORTED:
        raise ProductionCompileError(
            "this host cannot safely prepare user output directories"
        )
    try:
        descriptor = os.open(
            path,
            os.O_RDONLY | directory_flag | no_follow_flag,
            dir_fd=parent_fd,
        )
    except OSError as error:
        raise ProductionCompileError(
            "approved user output directory is not a repository directory"
        ) from error
    stack.callback(os.close, descriptor)
    try:
        mode = os.fstat(descriptor).st_mode
    except OSError as error:
        raise ProductionCompileError(
            f"approved user output directory cannot be inspected: {error}"
        ) from error
    if not stat.S_ISDIR(mode):
        raise ProductionCompileError(
            "approved user output directory is not a repository directory"
        )
    return descriptor


def _prepare_posix_user_directories(
    root: Path,
    parts: Sequence[str],
    stack: ExitStack,
) -> None:
    if not _POSIX_DIRECTORY_WALK_SUPPORTED:
        raise ProductionCompileError(
            "this host cannot safely prepare user output directories"
        )
    parent_fd = _pin_posix_directory(root, stack)
    for part in parts:
        try:
            child_fd = _pin_posix_directory(
                part, stack, parent_fd=parent_fd
            )
        except ProductionCompileError as open_error:
            cause = open_error.__cause__
            if not isinstance(cause, FileNotFoundError):
                raise
            try:
                os.mkdir(part, dir_fd=parent_fd)
            except FileExistsError:
                pass
            except OSError as error:
                raise ProductionCompileError(
                    "approved user output directory cannot be created: "
                    f"{error}"
                ) from error
            child_fd = _pin_posix_directory(
                part, stack, parent_fd=parent_fd
            )
        parent_fd = child_fd


def _windows_directory_api():
    from ctypes import wintypes

    class FileAttributeTagInfo(ctypes.Structure):
        _fields_ = (
            ("attributes", wintypes.DWORD),
            ("reparse_tag", wintypes.DWORD),
        )

    class UnicodeString(ctypes.Structure):
        _fields_ = (
            ("length", wintypes.USHORT),
            ("maximum_length", wintypes.USHORT),
            ("buffer", wintypes.LPWSTR),
        )

    class ObjectAttributes(ctypes.Structure):
        _fields_ = (
            ("length", wintypes.ULONG),
            ("root_directory", wintypes.HANDLE),
            ("object_name", ctypes.POINTER(UnicodeString)),
            ("attributes", wintypes.ULONG),
            ("security_descriptor", wintypes.LPVOID),
            ("security_quality_of_service", wintypes.LPVOID),
        )

    class IoStatusBlock(ctypes.Structure):
        _fields_ = (
            ("status", wintypes.LONG),
            ("information", ctypes.c_size_t),
        )

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.argtypes = (
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    )
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.GetFileInformationByHandleEx.argtypes = (
        wintypes.HANDLE,
        ctypes.c_int,
        wintypes.LPVOID,
        wintypes.DWORD,
    )
    kernel32.GetFileInformationByHandleEx.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.CloseHandle.restype = wintypes.BOOL
    ntdll = ctypes.WinDLL("ntdll", use_last_error=True)
    ntdll.NtCreateFile.argtypes = (
        ctypes.POINTER(wintypes.HANDLE),
        wintypes.DWORD,
        ctypes.POINTER(ObjectAttributes),
        ctypes.POINTER(IoStatusBlock),
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
    )
    ntdll.NtCreateFile.restype = wintypes.LONG
    ntdll.RtlNtStatusToDosError.argtypes = (wintypes.LONG,)
    ntdll.RtlNtStatusToDosError.restype = wintypes.ULONG
    return (
        kernel32,
        ntdll,
        FileAttributeTagInfo,
        UnicodeString,
        ObjectAttributes,
        IoStatusBlock,
    )


def _validate_windows_directory_handle(
    kernel32,
    info_type,
    handle,
) -> None:
    information = info_type()
    if not kernel32.GetFileInformationByHandleEx(
        handle,
        _WINDOWS_FILE_ATTRIBUTE_TAG_INFO_CLASS,
        ctypes.byref(information),
        ctypes.sizeof(information),
    ):
        error_code = ctypes.get_last_error()
        raise ProductionCompileError(
            "approved user output directory cannot be inspected"
        ) from ctypes.WinError(error_code)
    if (
        not information.attributes & _WINDOWS_FILE_ATTRIBUTE_DIRECTORY
        or information.attributes & _WINDOWS_FILE_ATTRIBUTE_REPARSE_POINT
    ):
        raise ProductionCompileError(
            "approved user output directory is not a repository directory"
        )


def _pin_windows_directory(path: Path, stack: ExitStack):
    kernel32, _, info_type, _, _, _ = _windows_directory_api()
    handle = kernel32.CreateFileW(
        str(path),
        _WINDOWS_DIRECTORY_ACCESS,
        _WINDOWS_DIRECTORY_SHARE,
        None,
        _WINDOWS_OPEN_EXISTING,
        (
            _WINDOWS_FILE_FLAG_BACKUP_SEMANTICS
            | _WINDOWS_FILE_FLAG_OPEN_REPARSE_POINT
        ),
        None,
    )
    invalid_handle = ctypes.c_void_p(-1).value
    if handle == invalid_handle:
        error_code = ctypes.get_last_error()
        raise ProductionCompileError(
            "approved user output directory is not a repository directory"
        ) from ctypes.WinError(error_code, str(path))
    try:
        _validate_windows_directory_handle(kernel32, info_type, handle)
    except BaseException:
        kernel32.CloseHandle(handle)
        raise
    stack.callback(kernel32.CloseHandle, handle)
    return handle


def _open_or_create_windows_directory(
    parent_handle,
    name: str,
    stack: ExitStack,
):
    (
        kernel32,
        ntdll,
        info_type,
        unicode_type,
        attributes_type,
        status_type,
    ) = _windows_directory_api()
    name_buffer = ctypes.create_unicode_buffer(name)
    encoded_length = len(name.encode("utf-16-le"))
    object_name = unicode_type(
        encoded_length,
        encoded_length + 2,
        ctypes.cast(name_buffer, ctypes.c_wchar_p),
    )
    attributes = attributes_type(
        ctypes.sizeof(attributes_type),
        parent_handle,
        ctypes.pointer(object_name),
        _WINDOWS_OBJECT_CASE_INSENSITIVE | _WINDOWS_OBJECT_DONT_REPARSE,
        None,
        None,
    )
    status_block = status_type()
    handle = ctypes.c_void_p()
    status = ntdll.NtCreateFile(
        ctypes.byref(handle),
        _WINDOWS_DIRECTORY_ACCESS,
        ctypes.byref(attributes),
        ctypes.byref(status_block),
        None,
        0,
        _WINDOWS_DIRECTORY_SHARE,
        _WINDOWS_FILE_OPEN_IF,
        _WINDOWS_FILE_DIRECTORY_FILE
        | _WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT,
        None,
        0,
    )
    if status < 0:
        error_code = ntdll.RtlNtStatusToDosError(status)
        if error_code in (
            _WINDOWS_ERROR_DIRECTORY,
            _WINDOWS_ERROR_REPARSE_POINT_ENCOUNTERED,
        ):
            raise ProductionCompileError(
                "approved user output directory is not a repository directory"
            ) from ctypes.WinError(error_code, name)
        raise ProductionCompileError(
            "approved user output directory cannot be created: "
            f"{ctypes.FormatError(error_code)}"
        ) from ctypes.WinError(error_code, name)
    opened_handle = handle.value
    try:
        _validate_windows_directory_handle(
            kernel32, info_type, opened_handle
        )
    except BaseException:
        kernel32.CloseHandle(opened_handle)
        raise
    stack.callback(kernel32.CloseHandle, opened_handle)
    return opened_handle


def _prepare_windows_user_directories(
    root: Path,
    parts: Sequence[str],
    stack: ExitStack,
) -> None:
    parent_handle = _pin_windows_directory(root, stack)
    for part in parts:
        parent_handle = _open_or_create_windows_directory(
            parent_handle, part, stack
        )


def _prepare_user_output_directory(
    root: Path,
    source: Path,
    output: Path,
    stack: ExitStack,
) -> Path:
    candidate = output if output.is_absolute() else root / output
    requested = Path(os.path.abspath(candidate))
    try:
        relative = requested.relative_to(root)
    except ValueError as error:
        raise ProductionCompileError(
            f"output must stay inside the repository: {output}"
        ) from error
    if (
        len(relative.parts) < 3
        or relative.parts[0] != "user"
        or relative.parts[1] == "examples"
        or requested.name != f"{source.stem}.o"
    ):
        raise ProductionCompileError(
            "source and output do not form an approved output pair: "
            f"{source.relative_to(root).as_posix()} -> "
            f"{relative.as_posix()}"
        )

    directory_parts = relative.parts[:-1]
    if _HOST_IS_WINDOWS:
        _prepare_windows_user_directories(root, directory_parts, stack)
    else:
        _prepare_posix_user_directories(root, directory_parts, stack)
    return requested


def _validate_output_binding(
    root: Path,
    cohort: str,
    source: Path,
    output: Path,
) -> None:
    source_relative = source.relative_to(root)
    output_relative = output.relative_to(root)
    if cohort == "user":
        valid = (
            len(output_relative.parts) >= 3
            and output_relative.parts[0] == "user"
            and output_relative.parts[1] != "examples"
            and output.name == source.stem + ".o"
        )
    else:
        valid = (
            len(output_relative.parts) >= 3
            and output_relative.parts[:2] == ("kernel", "util")
            and output.name == source.stem + ".o"
        )
    if not valid:
        raise ProductionCompileError(
            f"source and output do not form an approved output pair: "
            f"{source_relative.as_posix()} -> {output_relative.as_posix()}"
        )


def _closure_paths(root: Path, cohort: str, source: Path) -> tuple[Path, ...]:
    relatives: Sequence[str]
    if cohort == "user":
        relatives = USER_INCLUDE_CLOSURE
    else:
        relatives = GENERATED_INCLUDE_CLOSURE
    paths = [source]
    for relative in relatives:
        path = root / relative
        if path.is_symlink() or not path.is_file():
            raise ProductionCompileError(
                f"production include closure is unavailable: {relative}"
            )
        paths.append(path.resolve())
    return tuple(paths)


def _capture(paths: Sequence[Path]) -> dict[Path, bytes]:
    captured = {}
    for path in paths:
        try:
            captured[path] = path.read_bytes()
        except OSError as error:
            raise ProductionCompileError(
                f"cannot read production input {path}: {error}"
            ) from error
    return captured


def _snapshot(paths: Sequence[Path]) -> dict[Path, str]:
    return {
        path: hashlib.sha256(payload).hexdigest()
        for path, payload in _capture(paths).items()
    }


def _write_frozen_closure(
    root: Path,
    frozen_root: Path,
    captured: dict[Path, bytes],
) -> None:
    for source, payload in captured.items():
        relative = source.relative_to(root)
        target = frozen_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)


def compile_production_source(
    root: Path,
    cohort: str,
    source: Path,
    output: Path,
    *,
    manifest: Path | None = None,
    native_compiler: Path | None = None,
    tool_mode: str = "auto",
    executor: ToolRunner | NativeToolExecutor | None = None,
    timeout: int = DEFAULT_TIMEOUT_SECONDS,
) -> None:
    """Compile one approved source and publish its object atomically."""
    root = _root_path(root)
    source, logical_source = _approved_source(root, cohort, source)
    if timeout <= 0:
        raise ProductionCompileError("compiler timeout must be positive")
    if tool_mode not in TOOL_MODES:
        raise ProductionCompileError(
            f"unknown production compiler mode: {tool_mode}"
        )
    if native_compiler is not None and tool_mode == "checked-seed":
        raise ProductionCompileError(
            "a native compiler cannot be used in checked-seed mode"
        )
    if manifest is not None and (
        native_compiler is not None or tool_mode == "native-windows"
    ):
        raise ProductionCompileError(
            "a checked-seed manifest cannot be combined with a native compiler"
        )
    use_native = (
        native_compiler is not None
        or tool_mode == "native-windows"
    )
    if use_native and cohort != "user":
        raise ProductionCompileError(
            "native Windows compilation is limited to the user cohort"
        )

    requested_output = None
    with ExitStack() as output_directory_pins:
        if cohort == "user":
            requested_output = _prepare_user_output_directory(
                root, source, output, output_directory_pins
            )
        output = _output_path(root, output)
        if requested_output is not None and output != requested_output:
            raise ProductionCompileError(
                "approved user output directory changed while preparing output"
            )
    _validate_output_binding(root, cohort, source, output)
    if source == output:
        raise ProductionCompileError("output may not replace its source")

    closure = _closure_paths(root, cohort, source)
    captured = _capture(closure)
    before = {
        path: hashlib.sha256(payload).hexdigest()
        for path, payload in captured.items()
    }
    try:
        with ExitStack() as stack:
            native_snapshot = None
            if use_native:
                try:
                    native_snapshot = capture_native_tool(
                        root, "cupidc", native_compiler
                    )
                    tool_directory = Path(
                        stack.enter_context(
                            tempfile.TemporaryDirectory(
                                prefix="cupidc-production-native-"
                            )
                        )
                    )
                    compiler = native_snapshot.stage(tool_directory)
                    active_executor = (
                        executor
                        if executor is not None
                        else NativeToolExecutor(root)
                    )
                except NativeToolError as error:
                    raise ProductionCompileError(str(error)) from error
            else:
                manifest_path = (
                    manifest.resolve()
                    if manifest is not None
                    else _default_seed_manifest(root)
                )
                seed_directory = Path(
                    stack.enter_context(
                        tempfile.TemporaryDirectory(
                            prefix="cupidc-production-seed-"
                        )
                    )
                )
                try:
                    seed_inputs = freeze_seed_inputs(
                        manifest_path, seed_directory
                    )
                except (BootstrapError, OSError) as error:
                    raise ProductionCompileError(
                        f"checked seed verification failed: {error}"
                    ) from error
                active_executor = executor

            temporary = stack.enter_context(
                tempfile.TemporaryDirectory(
                    prefix=f".{output.name}.cupidc-inputs-",
                    dir=output.parent,
                )
            )
            frozen_root = Path(temporary)
            _write_frozen_closure(root, frozen_root, captured)
            frozen_output = frozen_root / ".output" / output.name
            frozen_output.parent.mkdir()
            logical_temporary = "/.output/" + output.name
            arguments = build_compile_arguments(
                cohort,
                logical_source,
                logical_temporary,
                frozen_root.resolve(),
            )
            try:
                if native_snapshot is not None:
                    result = active_executor.run(
                        compiler, arguments, timeout
                    )
                else:
                    result = run_seed_tool(
                        manifest_path,
                        root,
                        "cupidc",
                        arguments,
                        timeout=timeout,
                        frozen_seed=seed_inputs,
                        runner=active_executor,
                    )
            except BootstrapError as error:
                if isinstance(error.__cause__, subprocess.TimeoutExpired):
                    raise ProductionCompileError(
                        f"CupidC timed out after {timeout} seconds for "
                        f"{logical_source.lstrip('/')}"
                    ) from error
                if isinstance(error.__cause__, OSError):
                    raise ProductionCompileError(
                        f"CupidC could not run for "
                        f"{logical_source.lstrip('/')}: {error.__cause__}"
                    ) from error
                raise ProductionCompileError(str(error)) from error
            except subprocess.TimeoutExpired as error:
                raise ProductionCompileError(
                    f"CupidC timed out after {timeout} seconds for "
                    f"{logical_source.lstrip('/')}"
                ) from error
            except OSError as error:
                raise ProductionCompileError(
                    f"CupidC could not run for "
                    f"{logical_source.lstrip('/')}: {error}"
                ) from error
            if result.returncode != 0:
                details = (result.stderr or result.stdout or "").strip()
                suffix = f": {details}" if details else ""
                raise ProductionCompileError(
                    f"CupidC failed for {logical_source.lstrip('/')} "
                    f"with status {result.returncode}{suffix}"
                )
            if frozen_output.is_symlink() or not frozen_output.is_file():
                raise ProductionCompileError(
                    f"CupidC did not write an object for "
                    f"{logical_source.lstrip('/')}"
                )
            try:
                validate_i386_relocatable(frozen_output)
            except Exception as error:
                raise ProductionCompileError(
                    f"invalid object for "
                    f"{logical_source.lstrip('/')}: {error}"
                ) from error
            if native_snapshot is not None:
                try:
                    native_snapshot.require_unchanged(
                        "compiling with native CupidC"
                    )
                except NativeToolError as error:
                    raise ProductionCompileError(str(error)) from error
            if _snapshot(closure) != before:
                raise ProductionCompileError(
                    f"production inputs changed while compiling "
                    f"{logical_source.lstrip('/')}"
                )
            os.replace(frozen_output, output)
    except OSError as error:
        raise ProductionCompileError(
            f"could not publish production object {output}: {error}"
        ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compile an approved production source with CupidC."
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--cohort",
        required=True,
        choices=tuple(APPROVED_SOURCES),
    )
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
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
        compile_production_source(
            arguments.root,
            arguments.cohort,
            arguments.source,
            arguments.output,
            manifest=arguments.manifest,
            tool_mode=arguments.tool_mode,
            timeout=arguments.timeout,
        )
    except ProductionCompileError as error:
        print(f"CupidC production compile failed: {error}", file=sys.stderr)
        return 1
    print(f"CupidC production object: {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

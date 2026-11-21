## @file
#  @brief Pin executable inputs and output paths on Windows and POSIX.
"""Read code inputs through retained handles and keep their path checks together."""

from __future__ import annotations

import ctypes
import hashlib
import os
import stat
from contextlib import ExitStack, contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import BinaryIO, Iterator


_CODE_POSIX_WALK_SUPPORTED = (
    getattr(os, "O_DIRECTORY", 0) != 0
    and getattr(os, "O_NOFOLLOW", 0) != 0
    and os.open in os.supports_dir_fd
)
_CODE_WINDOWS_SYNCHRONIZE = 0x00100000
_CODE_WINDOWS_FILE_READ_ATTRIBUTES = 0x0080
_CODE_WINDOWS_FILE_TRAVERSE = 0x0020
_CODE_WINDOWS_FILE_ADD_FILE = 0x0002
_CODE_WINDOWS_FILE_DELETE_CHILD = 0x0040
_CODE_WINDOWS_DIRECTORY_ACCESS = (
    _CODE_WINDOWS_SYNCHRONIZE
    | _CODE_WINDOWS_FILE_READ_ATTRIBUTES
    | _CODE_WINDOWS_FILE_TRAVERSE
)
_CODE_WINDOWS_OUTPUT_DIRECTORY_ACCESS = (
    _CODE_WINDOWS_DIRECTORY_ACCESS
    | _CODE_WINDOWS_FILE_ADD_FILE
    | _CODE_WINDOWS_FILE_DELETE_CHILD
)
_CODE_WINDOWS_GENERIC_READ = 0x80000000
_CODE_WINDOWS_FILE_SHARE_READ = 0x0001
_CODE_WINDOWS_FILE_SHARE_WRITE = 0x0002
_CODE_WINDOWS_FILE_SHARE_DELETE = 0x0004
_CODE_WINDOWS_FILE_SHARE = (
    _CODE_WINDOWS_FILE_SHARE_READ | _CODE_WINDOWS_FILE_SHARE_WRITE
)
_CODE_WINDOWS_DELETE = 0x00010000
_CODE_WINDOWS_OPEN_EXISTING = 3
_CODE_WINDOWS_FILE_ATTRIBUTE_DIRECTORY = 0x0010
_CODE_WINDOWS_FILE_ATTRIBUTE_REPARSE_POINT = 0x0400
_CODE_WINDOWS_FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
_CODE_WINDOWS_FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
_CODE_WINDOWS_FILE_DIRECTORY_FILE = 0x00000001
_CODE_WINDOWS_FILE_NON_DIRECTORY_FILE = 0x00000040
_CODE_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
_CODE_WINDOWS_OBJECT_CASE_INSENSITIVE = 0x0040
_CODE_WINDOWS_OBJECT_DONT_REPARSE = 0x1000
_CODE_WINDOWS_FILE_OPEN = 1
_CODE_WINDOWS_FILE_ATTRIBUTE_TAG_INFO_CLASS = 9
_CODE_WINDOWS_FILE_RENAME_INFORMATION_CLASS = 10
_CODE_WINDOWS_ERROR_FILE_NOT_FOUND = 2
_CODE_WINDOWS_ERROR_PATH_NOT_FOUND = 3
_CODE_WINDOWS_ERROR_ACCESS_DENIED = 5
_CODE_WINDOWS_ERROR_DIRECTORY = 267
_CODE_WINDOWS_ERROR_REPARSE_POINT_ENCOUNTERED = 4395


class CodeValidationError(RuntimeError):
    """Executable code could not be validated safely."""

    def __init__(self, message: str, *, tool_stderr: str = "") -> None:
        super().__init__(message)
        self.tool_stderr = tool_stderr


@dataclass(frozen=True)
class _CodeOutput:
    root: Path
    logical: str
    path: Path
    parent_parts: tuple[str, ...]
    parent_identity: tuple[int, int]
    parent_fd: int | None = None
    parent_handle: object | None = None


@dataclass(frozen=True)
class _CodeValidationInput:
    logical: str
    size: int
    sha256: str
    device: int
    inode: int

def _code_logical_path(path: Path, *, subject: str = "code input") -> str:
    windows_path = PureWindowsPath(str(path))
    if (
        path.is_absolute()
        or path.anchor
        or windows_path.is_absolute()
        or windows_path.drive
    ):
        raise CodeValidationError(
            f"{subject} must be relative to the repository root: {path}"
        )
    logical_path = PurePosixPath(path.as_posix())
    if not logical_path.parts or logical_path == PurePosixPath("."):
        raise CodeValidationError(f"{subject} path may not be empty")
    if ".." in logical_path.parts:
        raise CodeValidationError(
            f"{subject} may not contain a parent traversal: {path}"
        )
    if logical_path.parts[0].startswith("-"):
        raise CodeValidationError(
            f"{subject} may not begin with an option marker: {path}"
        )
    return logical_path.as_posix()


def _code_path_is_link_or_junction(
    path: Path,
    logical: str,
    *,
    subject: str = "code input",
) -> bool:
    try:
        if path.is_symlink():
            return True
        is_junction = getattr(os.path, "isjunction", None)
        if is_junction is not None and is_junction(path):
            return True
        try:
            status = path.lstat()
        except FileNotFoundError:
            return False
        reparse_point = getattr(
            stat,
            "FILE_ATTRIBUTE_REPARSE_POINT",
            0x0400,
        )
        return bool(getattr(status, "st_file_attributes", 0) & reparse_point)
    except OSError as error:
        raise CodeValidationError(
            f"{subject} cannot be inspected: {logical}: {error}"
        ) from error


def _code_open_error(
    root: Path,
    inspected: Path,
    logical: str,
    subject: str,
    error: OSError,
) -> CodeValidationError:
    if _code_path_is_link_or_junction(inspected, logical, subject=subject):
        return CodeValidationError(
            f"{subject} may not be a symbolic link or junction: {logical}"
        )
    if isinstance(error, FileNotFoundError):
        return CodeValidationError(f"{subject} does not exist: {logical}")
    try:
        inspected.relative_to(root)
    except ValueError:
        return CodeValidationError(
            f"{subject} resolves outside the repository root: {logical}"
        )
    return CodeValidationError(f"{subject} cannot be opened safely: {logical}: {error}")


@contextmanager
def _open_posix_code_input(
    root: Path,
    logical: str,
    subject: str,
) -> Iterator[BinaryIO]:
    if not _CODE_POSIX_WALK_SUPPORTED:
        raise CodeValidationError("this host cannot safely open code validation inputs")
    parts = PurePosixPath(logical).parts
    directory_flag = getattr(os, "O_DIRECTORY", 0)
    no_follow_flag = getattr(os, "O_NOFOLLOW", 0)
    binary_flag = getattr(os, "O_BINARY", 0)
    with ExitStack() as stack:
        try:
            parent_fd = os.open(
                root,
                os.O_RDONLY | directory_flag | no_follow_flag,
            )
        except OSError as error:
            raise CodeValidationError(
                f"repository root cannot be opened safely: {error}"
            ) from error
        stack.callback(os.close, parent_fd)
        if not stat.S_ISDIR(os.fstat(parent_fd).st_mode):
            raise CodeValidationError("repository root is not a regular directory")

        inspected = root
        for part in parts[:-1]:
            inspected /= part
            try:
                child_fd = os.open(
                    part,
                    os.O_RDONLY | directory_flag | no_follow_flag,
                    dir_fd=parent_fd,
                )
            except OSError as error:
                raise _code_open_error(
                    root, inspected, logical, subject, error
                ) from error
            stack.callback(os.close, child_fd)
            if not stat.S_ISDIR(os.fstat(child_fd).st_mode):
                raise CodeValidationError(
                    f"{subject} has a non-directory parent: {logical}"
                )
            parent_fd = child_fd

        inspected /= parts[-1]
        try:
            descriptor = os.open(
                parts[-1],
                os.O_RDONLY | no_follow_flag | binary_flag,
                dir_fd=parent_fd,
            )
        except OSError as error:
            raise _code_open_error(root, inspected, logical, subject, error) from error
        stack.callback(os.close, descriptor)
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise CodeValidationError(f"{subject} is not a regular file: {logical}")
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            yield stream


def _windows_code_api():
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
    ntdll.NtSetInformationFile.argtypes = (
        wintypes.HANDLE,
        ctypes.POINTER(IoStatusBlock),
        wintypes.LPVOID,
        wintypes.ULONG,
        wintypes.ULONG,
    )
    ntdll.NtSetInformationFile.restype = wintypes.LONG
    return (
        kernel32,
        ntdll,
        FileAttributeTagInfo,
        UnicodeString,
        ObjectAttributes,
        IoStatusBlock,
    )


def _validate_windows_code_handle(
    kernel32,
    info_type,
    handle,
    *,
    directory: bool,
    logical: str,
    subject: str,
) -> None:
    information = info_type()
    if not kernel32.GetFileInformationByHandleEx(
        handle,
        _CODE_WINDOWS_FILE_ATTRIBUTE_TAG_INFO_CLASS,
        ctypes.byref(information),
        ctypes.sizeof(information),
    ):
        error_code = ctypes.get_last_error()
        raise CodeValidationError(
            f"{subject} cannot be inspected: {logical}: "
            f"{ctypes.FormatError(error_code)}"
        ) from ctypes.WinError(error_code, logical)
    if information.attributes & _CODE_WINDOWS_FILE_ATTRIBUTE_REPARSE_POINT:
        raise CodeValidationError(
            f"{subject} may not be a symbolic link or junction: {logical}"
        )
    is_directory = bool(information.attributes & _CODE_WINDOWS_FILE_ATTRIBUTE_DIRECTORY)
    if is_directory != directory:
        if directory:
            message = f"{subject} has a non-directory parent: {logical}"
        else:
            message = f"{subject} is not a regular file: {logical}"
        raise CodeValidationError(message)


def _open_windows_code_root(
    path: Path,
    *,
    logical: str,
    subject: str,
    desired_access: int = _CODE_WINDOWS_DIRECTORY_ACCESS,
):
    kernel32, _, info_type, _, _, _ = _windows_code_api()
    handle = kernel32.CreateFileW(
        str(path),
        desired_access,
        _CODE_WINDOWS_FILE_SHARE,
        None,
        _CODE_WINDOWS_OPEN_EXISTING,
        _CODE_WINDOWS_FILE_FLAG_OPEN_REPARSE_POINT
        | _CODE_WINDOWS_FILE_FLAG_BACKUP_SEMANTICS,
        None,
    )
    invalid_handle = ctypes.c_void_p(-1).value
    if handle == invalid_handle:
        error_code = ctypes.get_last_error()
        raise CodeValidationError(
            f"repository root cannot be opened safely: {ctypes.FormatError(error_code)}"
        ) from ctypes.WinError(error_code, str(path))
    try:
        _validate_windows_code_handle(
            kernel32,
            info_type,
            handle,
            directory=True,
            logical=logical,
            subject=subject,
        )
    except BaseException:
        kernel32.CloseHandle(handle)
        raise
    return kernel32, handle


def _open_windows_code_child(
    parent_handle,
    name: str,
    *,
    directory: bool,
    logical: str,
    subject: str,
    desired_access: int | None = None,
    share_access: int = _CODE_WINDOWS_FILE_SHARE,
):
    (
        kernel32,
        ntdll,
        info_type,
        unicode_type,
        attributes_type,
        status_type,
    ) = _windows_code_api()
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
        _CODE_WINDOWS_OBJECT_CASE_INSENSITIVE | _CODE_WINDOWS_OBJECT_DONT_REPARSE,
        None,
        None,
    )
    status_block = status_type()
    handle = ctypes.c_void_p()
    if desired_access is None:
        desired_access = (
            _CODE_WINDOWS_DIRECTORY_ACCESS
            if directory
            else _CODE_WINDOWS_GENERIC_READ | _CODE_WINDOWS_SYNCHRONIZE
        )
    create_options = _CODE_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT
    create_options |= (
        _CODE_WINDOWS_FILE_DIRECTORY_FILE
        if directory
        else _CODE_WINDOWS_FILE_NON_DIRECTORY_FILE
    )
    status = ntdll.NtCreateFile(
        ctypes.byref(handle),
        desired_access,
        ctypes.byref(attributes),
        ctypes.byref(status_block),
        None,
        0,
        share_access,
        _CODE_WINDOWS_FILE_OPEN,
        create_options,
        None,
        0,
    )
    if status < 0:
        error_code = ntdll.RtlNtStatusToDosError(status)
        opened_directory = False
        if not directory and error_code == _CODE_WINDOWS_ERROR_ACCESS_DENIED:
            try:
                directory_kernel32, directory_handle = _open_windows_code_child(
                    parent_handle,
                    name,
                    directory=True,
                    logical=logical,
                    subject=subject,
                )
            except CodeValidationError:
                pass
            else:
                directory_kernel32.CloseHandle(directory_handle)
                opened_directory = True
        if error_code == _CODE_WINDOWS_ERROR_REPARSE_POINT_ENCOUNTERED:
            message = f"{subject} may not be a symbolic link or junction: {logical}"
        elif opened_directory:
            message = f"{subject} is not a regular file: {logical}"
        elif error_code in (
            _CODE_WINDOWS_ERROR_FILE_NOT_FOUND,
            _CODE_WINDOWS_ERROR_PATH_NOT_FOUND,
        ):
            message = f"{subject} does not exist: {logical}"
        elif error_code == _CODE_WINDOWS_ERROR_DIRECTORY:
            if directory:
                message = f"{subject} has a non-directory parent: {logical}"
            else:
                message = f"{subject} is not a regular file: {logical}"
        else:
            message = (
                f"{subject} cannot be opened safely: {logical}: "
                f"{ctypes.FormatError(error_code)}"
            )
        raise CodeValidationError(message) from ctypes.WinError(error_code, name)
    opened_handle = handle.value
    try:
        _validate_windows_code_handle(
            kernel32,
            info_type,
            opened_handle,
            directory=directory,
            logical=logical,
            subject=subject,
        )
    except BaseException:
        kernel32.CloseHandle(opened_handle)
        raise
    return kernel32, opened_handle


@contextmanager
def _open_windows_code_input(
    root: Path,
    logical: str,
    subject: str,
) -> Iterator[BinaryIO]:
    import msvcrt

    parts = PurePosixPath(logical).parts
    with ExitStack() as stack:
        kernel32, handle = _open_windows_code_root(
            root,
            logical=logical,
            subject=subject,
        )
        stack.callback(kernel32.CloseHandle, handle)
        for part in parts[:-1]:
            kernel32, handle = _open_windows_code_child(
                handle,
                part,
                directory=True,
                logical=logical,
                subject=subject,
            )
            stack.callback(kernel32.CloseHandle, handle)
        kernel32, handle = _open_windows_code_child(
            handle,
            parts[-1],
            directory=False,
            logical=logical,
            subject=subject,
        )
        try:
            descriptor = msvcrt.open_osfhandle(
                handle,
                os.O_RDONLY | getattr(os, "O_BINARY", 0),
            )
        except OSError:
            kernel32.CloseHandle(handle)
            raise
        stack.callback(os.close, descriptor)
        with os.fdopen(descriptor, "rb", closefd=False) as stream:
            yield stream


@contextmanager
def _open_pinned_code_input(
    root: Path,
    logical: str,
    *,
    subject: str = "code input",
) -> Iterator[BinaryIO]:
    opener = _open_windows_code_input if os.name == "nt" else _open_posix_code_input
    with opener(root, logical, subject) as stream:
        yield stream


def _pin_posix_code_directory(
    root: Path,
    parts: tuple[str, ...],
    stack: ExitStack,
    *,
    subject: str,
    logical: str,
) -> int:
    directory_flag = getattr(os, "O_DIRECTORY", 0)
    no_follow_flag = getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(
            root, os.O_RDONLY | directory_flag | no_follow_flag
        )
    except OSError as error:
        raise CodeValidationError(
            f"repository root cannot be opened safely: {error}"
        ) from error
    stack.callback(os.close, descriptor)
    inspected = root
    for part in parts:
        inspected /= part
        try:
            child = os.open(
                part,
                os.O_RDONLY | directory_flag | no_follow_flag,
                dir_fd=descriptor,
            )
        except OSError as error:
            raise _code_open_error(
                root,
                inspected,
                logical,
                subject,
                error,
            ) from error
        stack.callback(os.close, child)
        if not stat.S_ISDIR(os.fstat(child).st_mode):
            raise CodeValidationError(
                f"{subject} has a non-directory parent: {logical}"
            )
        descriptor = child
    return descriptor


def _pin_windows_code_directory(
    root: Path,
    parts: tuple[str, ...],
    stack: ExitStack,
    *,
    subject: str,
    logical: str,
    final_access: int | None = None,
):
    root_access = final_access if not parts and final_access is not None else _CODE_WINDOWS_DIRECTORY_ACCESS
    kernel32, handle = _open_windows_code_root(
        root,
        logical=logical,
        subject=subject,
        desired_access=root_access,
    )
    stack.callback(kernel32.CloseHandle, handle)
    for index, part in enumerate(parts):
        desired_access = (
            final_access
            if final_access is not None and index == len(parts) - 1
            else None
        )
        kernel32, child = _open_windows_code_child(
            handle,
            part,
            directory=True,
            logical=logical,
            subject=subject,
            desired_access=desired_access,
        )
        stack.callback(kernel32.CloseHandle, child)
        handle = child
    return handle


def _windows_code_handle_identity(handle) -> tuple[int, int]:
    from ctypes import wintypes

    class ByHandleFileInformation(ctypes.Structure):
        _fields_ = (
            ("file_attributes", wintypes.DWORD),
            ("creation_time", wintypes.FILETIME),
            ("last_access_time", wintypes.FILETIME),
            ("last_write_time", wintypes.FILETIME),
            ("volume_serial_number", wintypes.DWORD),
            ("file_size_high", wintypes.DWORD),
            ("file_size_low", wintypes.DWORD),
            ("number_of_links", wintypes.DWORD),
            ("file_index_high", wintypes.DWORD),
            ("file_index_low", wintypes.DWORD),
        )

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetFileInformationByHandle.argtypes = (
        wintypes.HANDLE,
        ctypes.POINTER(ByHandleFileInformation),
    )
    kernel32.GetFileInformationByHandle.restype = wintypes.BOOL
    information = ByHandleFileInformation()
    if not kernel32.GetFileInformationByHandle(
        handle, ctypes.byref(information)
    ):
        error_code = ctypes.get_last_error()
        raise CodeValidationError(
            "code directory identity cannot be inspected: "
            f"{ctypes.FormatError(error_code)}"
        ) from ctypes.WinError(error_code)
    file_index = (
        information.file_index_high << 32
    ) | information.file_index_low
    return information.volume_serial_number, file_index


def _pin_code_output(
    root: Path,
    output_argument: Path,
    stack: ExitStack,
) -> _CodeOutput:
    logical = _code_logical_path(output_argument, subject="code output")
    parts = PurePosixPath(logical).parts
    parent_parts = tuple(parts[:-1])
    path = root.joinpath(*parts)
    if os.name == "nt":
        parent_handle = _pin_windows_code_directory(
            root,
            parent_parts,
            stack,
            subject="code output",
            logical=logical,
            final_access=_CODE_WINDOWS_OUTPUT_DIRECTORY_ACCESS,
        )
        identity = _windows_code_handle_identity(parent_handle)
        return _CodeOutput(
            root,
            logical,
            path,
            parent_parts,
            identity,
            parent_handle=parent_handle,
        )
    parent_fd = _pin_posix_code_directory(
        root,
        parent_parts,
        stack,
        subject="code output",
        logical=logical,
    )
    information = os.fstat(parent_fd)
    return _CodeOutput(
        root,
        logical,
        path,
        parent_parts,
        (information.st_dev, information.st_ino),
        parent_fd=parent_fd,
    )


def _require_code_output_parent_unchanged(output: _CodeOutput) -> None:
    with ExitStack() as stack:
        if output.parent_fd is not None:
            current = _pin_posix_code_directory(
                output.root,
                output.parent_parts,
                stack,
                subject="code output",
                logical=output.logical,
            )
            information = os.fstat(current)
            identity = (information.st_dev, information.st_ino)
        elif output.parent_handle is not None:
            current = _pin_windows_code_directory(
                output.root,
                output.parent_parts,
                stack,
                subject="code output",
                logical=output.logical,
                final_access=_CODE_WINDOWS_OUTPUT_DIRECTORY_ACCESS,
            )
            identity = _windows_code_handle_identity(current)
        else:
            raise CodeValidationError("code output parent was not pinned")
    if identity != output.parent_identity:
        raise CodeValidationError("code output parent changed while checked tools ran")


def _code_output_entry(
    output: _CodeOutput,
) -> _CodeValidationInput | None:
    name = PurePosixPath(output.logical).name
    try:
        if output.parent_fd is not None:
            info = os.stat(name, dir_fd=output.parent_fd, follow_symlinks=False)
            if stat.S_ISLNK(info.st_mode):
                raise CodeValidationError(
                    "code output may not be a symbolic link or junction: "
                    f"{output.logical}"
                )
            if not stat.S_ISREG(info.st_mode):
                raise CodeValidationError(
                    f"code output is not a regular file: {output.logical}"
                )
            no_follow_flag = getattr(os, "O_NOFOLLOW", 0)
            descriptor = os.open(
                name,
                os.O_RDONLY
                | no_follow_flag
                | getattr(os, "O_BINARY", 0),
                dir_fd=output.parent_fd,
            )
            with os.fdopen(descriptor, "rb") as stream:
                snapshot = _capture_code_stream(output.logical, stream)
            return snapshot
        if output.parent_handle is not None:
            kernel32, handle = _open_windows_code_child(
                output.parent_handle,
                name,
                directory=False,
                logical=output.logical,
                subject="code output",
            )
            import msvcrt

            try:
                descriptor = msvcrt.open_osfhandle(
                    handle,
                    os.O_RDONLY | getattr(os, "O_BINARY", 0),
                )
            except OSError:
                kernel32.CloseHandle(handle)
                raise
            with os.fdopen(descriptor, "rb") as stream:
                snapshot = _capture_code_stream(output.logical, stream)
            return snapshot
    except FileNotFoundError:
        return None
    except CodeValidationError as error:
        if "does not exist" in str(error):
            return None
        raise
    except OSError as error:
        raise CodeValidationError(
            f"code output cannot be inspected: {output.logical}: {error}"
        ) from error
    raise CodeValidationError("code output parent was not pinned")


def _capture_code_stream(
    logical: str,
    source: BinaryIO,
) -> _CodeValidationInput:
    digest = hashlib.sha256()
    size = 0
    before = os.fstat(source.fileno())
    if not stat.S_ISREG(before.st_mode):
        raise CodeValidationError(
            f"code output is not a regular file: {logical}"
        )
    while True:
        block = source.read(1024 * 1024)
        if not block:
            break
        digest.update(block)
        size += len(block)
    after = os.fstat(source.fileno())
    if (
        before.st_size != after.st_size
        or before.st_mtime_ns != after.st_mtime_ns
        or before.st_size != size
    ):
        raise CodeValidationError(
            f"code output changed while it was being inspected: {logical}"
        )
    return _CodeValidationInput(
        logical,
        size,
        digest.hexdigest(),
        before.st_dev,
        before.st_ino,
    )


def _capture_code_input(
    root: Path,
    logical: str,
    *,
    frozen: Path | None = None,
    subject: str = "code input",
) -> _CodeValidationInput:
    digest = hashlib.sha256()
    size = 0
    destination = None
    try:
        if frozen is not None:
            frozen.parent.mkdir(parents=True, exist_ok=True)
            destination = frozen.open("xb")
        with _open_pinned_code_input(root, logical, subject=subject) as source:
            before = os.fstat(source.fileno())
            if not stat.S_ISREG(before.st_mode):
                raise CodeValidationError(f"{subject} is not a regular file: {logical}")
            while True:
                block = source.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
                size += len(block)
                if destination is not None:
                    destination.write(block)
            after = os.fstat(source.fileno())
        if (
            before.st_size != after.st_size
            or before.st_mtime_ns != after.st_mtime_ns
            or before.st_size != size
        ):
            raise CodeValidationError(
                f"{subject} changed while it was being frozen: {logical}"
            )
        if destination is not None:
            destination.flush()
            os.fsync(destination.fileno())
    except CodeValidationError:
        raise
    except OSError as error:
        raise CodeValidationError(
            f"{subject} cannot be read: {logical}: {error}"
        ) from error
    finally:
        if destination is not None:
            destination.close()
    return _CodeValidationInput(
        logical=logical,
        size=size,
        sha256=digest.hexdigest(),
        device=before.st_dev,
        inode=before.st_ino,
    )


def _rename_windows_code_output(candidate: _CodeOutput, output: _CodeOutput) -> None:
    from ctypes import wintypes

    if candidate.parent_handle is None or output.parent_handle is None:
        raise CodeValidationError("code output parent was not pinned")
    candidate_name = PurePosixPath(candidate.logical).name
    kernel32, handle = _open_windows_code_child(
        candidate.parent_handle,
        candidate_name,
        directory=False,
        logical=candidate.logical,
        subject="checked CupidObj output",
        desired_access=(
            _CODE_WINDOWS_DELETE
            | _CODE_WINDOWS_FILE_READ_ATTRIBUTES
            | _CODE_WINDOWS_SYNCHRONIZE
        ),
        share_access=(
            _CODE_WINDOWS_FILE_SHARE
            | _CODE_WINDOWS_FILE_SHARE_DELETE
        ),
    )
    try:
        output_name = PurePosixPath(output.logical).name

        class FileRenameInfo(ctypes.Structure):
            _fields_ = (
                ("replace_if_exists", wintypes.BOOLEAN),
                ("root_directory", wintypes.HANDLE),
                ("file_name_length", wintypes.DWORD),
                ("file_name", wintypes.WCHAR * (len(output_name) + 1)),
            )

        information = FileRenameInfo()
        information.replace_if_exists = True
        information.root_directory = output.parent_handle
        information.file_name_length = len(output_name.encode("utf-16-le"))
        information.file_name = output_name
        _, ntdll, _, _, _, status_type = _windows_code_api()
        status_block = status_type()
        status = ntdll.NtSetInformationFile(
            handle,
            ctypes.byref(status_block),
            ctypes.byref(information),
            ctypes.sizeof(information),
            _CODE_WINDOWS_FILE_RENAME_INFORMATION_CLASS,
        )
        if status < 0:
            error_code = ntdll.RtlNtStatusToDosError(status)
            raise OSError(error_code, ctypes.FormatError(error_code))
    finally:
        kernel32.CloseHandle(handle)

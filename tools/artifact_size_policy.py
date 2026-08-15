#!/usr/bin/env python3
"""Verify the reviewed sizes of Cupid-owned production artifacts."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import stat
import sys
from contextlib import ExitStack
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


SCHEMA = "cupid.artifact-size-policy.v1"
SEED_SCHEMA = "cupid.bootstrap-seed.v1"
FIXED_ARTIFACT_OWNERS = {
    "boot/boot.bin": "CupidASM",
    "kernel/kernel.bin": "CupidObj",
    "kernel/kernel.elf": "CupidLD",
    "kernel/kernel.elf.pass1": "CupidLD",
}
SEED_ARTIFACT_OWNERS = {
    "cupidasm": "CupidASM",
    "cupidc": "CupidC",
    "cupiddis": "CupidDis",
    "cupidld": "CupidLD",
    "cupidobj": "CupidObj",
}
ARTIFACT_COUNT = len(FIXED_ARTIFACT_OWNERS) + len(SEED_ARTIFACT_OWNERS)
POLICY_KEYS = {"artifacts", "schema"}
ENTRY_KEYS = {"exact_bytes", "path", "producer", "reason"}

_POSIX_PINNING_SUPPORTED = (
    getattr(os, "O_DIRECTORY", 0) != 0
    and getattr(os, "O_NOFOLLOW", 0) != 0
    and os.open in os.supports_dir_fd
)
_WINDOWS_GENERIC_READ = 0x80000000
_WINDOWS_FILE_READ_ATTRIBUTES = 0x0080
_WINDOWS_FILE_TRAVERSE = 0x0020
_WINDOWS_SYNCHRONIZE = 0x00100000
_WINDOWS_DIRECTORY_ACCESS = (
    _WINDOWS_FILE_READ_ATTRIBUTES | _WINDOWS_FILE_TRAVERSE | _WINDOWS_SYNCHRONIZE
)
_WINDOWS_SHARE_READ = 0x0001
_WINDOWS_SHARE_WRITE = 0x0002
_WINDOWS_OPEN_EXISTING = 3
_WINDOWS_FILE_OPEN = 1
_WINDOWS_FILE_ATTRIBUTE_DIRECTORY = 0x0010
_WINDOWS_FILE_ATTRIBUTE_REPARSE_POINT = 0x0400
_WINDOWS_FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
_WINDOWS_FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
_WINDOWS_FILE_DIRECTORY_FILE = 0x00000001
_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
_WINDOWS_FILE_NON_DIRECTORY_FILE = 0x00000040
_WINDOWS_OBJECT_CASE_INSENSITIVE = 0x0040
_WINDOWS_OBJECT_DONT_REPARSE = 0x1000
_WINDOWS_FILE_ATTRIBUTE_TAG_INFO_CLASS = 9


class SizePolicyError(ValueError):
    """A controlled policy or artifact validation failure."""


@dataclass(frozen=True)
class _FileCapture:
    logical: str
    descriptor: int
    status: os.stat_result
    payload: bytes | None


def _plural_bytes(size: int) -> str:
    return "byte" if size == 1 else "bytes"


def _load_json_object(data: bytes, label: str = "policy") -> object:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise SizePolicyError(f"{label} is not valid UTF-8") from error

    def reject_duplicate_keys(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise SizePolicyError(f"{label} object contains duplicate key: {key}")
            value[key] = item
        return value

    try:
        return json.loads(text, object_pairs_hook=reject_duplicate_keys)
    except SizePolicyError:
        raise
    except json.JSONDecodeError as error:
        raise SizePolicyError(
            f"{label} JSON is invalid at line {error.lineno}, column {error.colno}"
        ) from error


def _is_link_or_reparse(info: os.stat_result) -> bool:
    if stat.S_ISLNK(info.st_mode):
        return True
    attributes = getattr(info, "st_file_attributes", 0)
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(attributes & reparse_flag)


def _logical_path(value: object) -> str:
    if not isinstance(value, str) or not value:
        raise SizePolicyError("policy artifact path is invalid")
    logical = PurePosixPath(value)
    if (
        logical.is_absolute()
        or value == "."
        or ".." in logical.parts
        or logical.as_posix() != value
        or "\\" in value
        or "\0" in value
    ):
        raise SizePolicyError(f"policy artifact path is unsafe: {value}")
    return value


def _regular_file_issue(root: Path, logical_path: str) -> str | None:
    current = root
    parts = PurePosixPath(logical_path).parts
    for index, part in enumerate(parts):
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            return f"{logical_path} is missing"
        except OSError as error:
            return f"cannot inspect {logical_path}: {error}"
        if _is_link_or_reparse(info):
            return f"{logical_path} is linked or reparse-backed"
        if index + 1 < len(parts) and not stat.S_ISDIR(info.st_mode):
            return f"{logical_path} has a non-directory parent"
    if not stat.S_ISREG(info.st_mode):
        return f"{logical_path} is not a regular file"
    return None


def _windows_file_api():
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


def _validate_windows_handle(kernel32, info_type, handle, *, directory: bool):
    information = info_type()
    if not kernel32.GetFileInformationByHandleEx(
        handle,
        _WINDOWS_FILE_ATTRIBUTE_TAG_INFO_CLASS,
        ctypes.byref(information),
        ctypes.sizeof(information),
    ):
        error_code = ctypes.get_last_error()
        raise ctypes.WinError(error_code)
    is_directory = bool(information.attributes & _WINDOWS_FILE_ATTRIBUTE_DIRECTORY)
    is_reparse = bool(information.attributes & _WINDOWS_FILE_ATTRIBUTE_REPARSE_POINT)
    if is_reparse or is_directory != directory:
        raise OSError("path is linked, reparse-backed, or has the wrong kind")


def _windows_open_root_handle(path: Path):
    kernel32, _, info_type, _, _, _ = _windows_file_api()
    flags = _WINDOWS_FILE_FLAG_OPEN_REPARSE_POINT
    flags |= _WINDOWS_FILE_FLAG_BACKUP_SEMANTICS
    handle = kernel32.CreateFileW(
        str(path),
        _WINDOWS_DIRECTORY_ACCESS,
        _WINDOWS_SHARE_READ | _WINDOWS_SHARE_WRITE,
        None,
        _WINDOWS_OPEN_EXISTING,
        flags,
        None,
    )
    invalid_handle = ctypes.c_void_p(-1).value
    if handle == invalid_handle:
        error_code = ctypes.get_last_error()
        raise ctypes.WinError(error_code, str(path))
    try:
        _validate_windows_handle(
            kernel32,
            info_type,
            handle,
            directory=True,
        )
    except BaseException:
        kernel32.CloseHandle(handle)
        raise
    return kernel32, handle


def _windows_open_relative_handle(
    parent_handle,
    name: str,
    *,
    directory: bool,
):
    (
        kernel32,
        ntdll,
        info_type,
        unicode_type,
        attributes_type,
        status_type,
    ) = _windows_file_api()
    name_buffer = ctypes.create_unicode_buffer(name)
    encoded_length = len(name.encode("utf-16-le"))
    object_name = unicode_type(
        encoded_length,
        encoded_length + 2,
        ctypes.cast(name_buffer, ctypes.c_wchar_p),
    )
    object_attributes = attributes_type(
        ctypes.sizeof(attributes_type),
        parent_handle,
        ctypes.pointer(object_name),
        _WINDOWS_OBJECT_CASE_INSENSITIVE | _WINDOWS_OBJECT_DONT_REPARSE,
        None,
        None,
    )
    status_block = status_type()
    handle = ctypes.c_void_p()
    access = (
        _WINDOWS_DIRECTORY_ACCESS
        if directory
        else _WINDOWS_GENERIC_READ | _WINDOWS_SYNCHRONIZE
    )
    options = (
        _WINDOWS_FILE_DIRECTORY_FILE if directory else _WINDOWS_FILE_NON_DIRECTORY_FILE
    ) | _WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT
    status = ntdll.NtCreateFile(
        ctypes.byref(handle),
        access,
        ctypes.byref(object_attributes),
        ctypes.byref(status_block),
        None,
        0,
        _WINDOWS_SHARE_READ | _WINDOWS_SHARE_WRITE,
        _WINDOWS_FILE_OPEN,
        options,
        None,
        0,
    )
    if status < 0:
        error_code = ntdll.RtlNtStatusToDosError(status)
        raise ctypes.WinError(error_code, name)
    opened_handle = handle.value
    try:
        _validate_windows_handle(
            kernel32,
            info_type,
            opened_handle,
            directory=directory,
        )
    except BaseException:
        kernel32.CloseHandle(opened_handle)
        raise
    return kernel32, opened_handle


def _stable_file_fields(status: os.stat_result) -> tuple[int, ...]:
    return (
        status.st_dev,
        status.st_ino,
        status.st_nlink,
        status.st_size,
        status.st_mtime_ns,
    )


def _read_descriptor(descriptor: int, expected_size: int) -> bytes:
    os.lseek(descriptor, 0, os.SEEK_SET)
    chunks = []
    remaining = expected_size
    while remaining:
        chunk = os.read(descriptor, min(remaining, 1024 * 1024))
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    extra = os.read(descriptor, 1)
    payload = b"".join(chunks)
    if remaining or extra:
        raise OSError("file size changed while it was read")
    return payload


class _PinnedRepository:
    """Read repository files through a pinned, no-follow directory walk."""

    def __init__(self, root: Path):
        self.root = root
        self._stack = ExitStack()
        self._directories: dict[tuple[str, ...], object] = {}
        self._captures: list[_FileCapture] = []
        if os.name == "nt":
            kernel32, handle = _windows_open_root_handle(root)
            self._stack.callback(kernel32.CloseHandle, handle)
            self._directories[()] = handle
        else:
            if not _POSIX_PINNING_SUPPORTED:
                raise SizePolicyError(
                    "this host cannot safely inspect repository files"
                )
            flags = (
                os.O_RDONLY
                | getattr(os, "O_DIRECTORY", 0)
                | getattr(os, "O_NOFOLLOW", 0)
            )
            descriptor = os.open(root, flags)
            if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
                os.close(descriptor)
                raise SizePolicyError("repository root is not a regular directory")
            self._stack.callback(os.close, descriptor)
            self._directories[()] = descriptor

    def __enter__(self) -> _PinnedRepository:
        return self

    def __exit__(self, *exc_info) -> None:
        self._stack.close()

    def _pin_directory(self, parts: tuple[str, ...]):
        existing = self._directories.get(parts)
        if existing is not None:
            return existing
        parent_parts = parts[:-1]
        parent = self._pin_directory(parent_parts)
        if os.name == "nt":
            kernel32, handle = _windows_open_relative_handle(
                parent,
                parts[-1],
                directory=True,
            )
            self._stack.callback(kernel32.CloseHandle, handle)
            opened = handle
        else:
            flags = (
                os.O_RDONLY
                | getattr(os, "O_DIRECTORY", 0)
                | getattr(os, "O_NOFOLLOW", 0)
            )
            opened = os.open(parts[-1], flags, dir_fd=parent)
            if not stat.S_ISDIR(os.fstat(opened).st_mode):
                os.close(opened)
                raise OSError("path component is not a directory")
            self._stack.callback(os.close, opened)
        self._directories[parts] = opened
        return opened

    def _open_file_from_parent(self, parent, name: str) -> int:
        if os.name == "nt":
            import msvcrt

            kernel32, handle = _windows_open_relative_handle(
                parent,
                name,
                directory=False,
            )
            try:
                descriptor = msvcrt.open_osfhandle(
                    handle,
                    os.O_RDONLY | getattr(os, "O_BINARY", 0),
                )
            except BaseException:
                kernel32.CloseHandle(handle)
                raise
            return descriptor
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
        return os.open(name, flags, dir_fd=parent)

    def _open_file_descriptor(self, parts: tuple[str, ...]) -> int:
        parent = self._pin_directory(parts[:-1])
        return self._open_file_from_parent(parent, parts[-1])

    def _reopen_file_descriptor(self, parts: tuple[str, ...]) -> int:
        parent = self._directories[()]
        with ExitStack() as directories:
            for part in parts[:-1]:
                if os.name == "nt":
                    kernel32, opened = _windows_open_relative_handle(
                        parent,
                        part,
                        directory=True,
                    )
                    directories.callback(kernel32.CloseHandle, opened)
                else:
                    flags = (
                        os.O_RDONLY
                        | getattr(os, "O_DIRECTORY", 0)
                        | getattr(os, "O_NOFOLLOW", 0)
                    )
                    opened = os.open(part, flags, dir_fd=parent)
                    if not stat.S_ISDIR(os.fstat(opened).st_mode):
                        os.close(opened)
                        raise OSError("path component is not a directory")
                    directories.callback(os.close, opened)
                parent = opened
            return self._open_file_from_parent(parent, parts[-1])

    def capture(
        self,
        logical: str,
        *,
        read_payload: bool,
    ) -> tuple[_FileCapture | None, str | None]:
        parts = PurePosixPath(logical).parts
        descriptor = None
        try:
            descriptor = self._open_file_descriptor(parts)
            before = os.fstat(descriptor)
            if not stat.S_ISREG(before.st_mode):
                os.close(descriptor)
                descriptor = None
                return None, f"{logical} is not a regular file"
            payload = None
            if read_payload:
                payload = _read_descriptor(descriptor, before.st_size)
                after = os.fstat(descriptor)
                if _stable_file_fields(before) != _stable_file_fields(after):
                    raise OSError("file changed while it was read")
            capture = _FileCapture(logical, descriptor, before, payload)
            self._stack.callback(os.close, descriptor)
            descriptor = None
            self._captures.append(capture)
            return capture, None
        except OSError as error:
            if descriptor is not None:
                os.close(descriptor)
            issue = _regular_file_issue(self.root, logical)
            if issue is None:
                issue = f"cannot inspect {logical}: {error}"
            return None, issue

    def require_unchanged(self) -> None:
        for capture in self._captures:
            reopened = None
            try:
                current = os.fstat(capture.descriptor)
                reopened = self._reopen_file_descriptor(
                    PurePosixPath(capture.logical).parts
                )
                live = os.fstat(reopened)
            except OSError as error:
                raise SizePolicyError(
                    f"{capture.logical} changed while artifacts were inspected"
                ) from error
            finally:
                if reopened is not None:
                    os.close(reopened)
            expected = _stable_file_fields(capture.status)
            if (
                _stable_file_fields(current) != expected
                or not stat.S_ISREG(live.st_mode)
                or _stable_file_fields(live) != expected
            ):
                raise SizePolicyError(
                    f"{capture.logical} changed while artifacts were inspected"
                )


def _repository_argument(root: Path, argument: Path, label: str) -> str:
    candidate = argument if argument.is_absolute() else root / argument
    absolute = Path(os.path.abspath(candidate))
    try:
        relative = absolute.relative_to(root)
    except ValueError as error:
        raise SizePolicyError(f"{label} path is outside the repository root") from error
    logical = relative.as_posix()
    if not logical or logical == "." or ".." in relative.parts:
        raise SizePolicyError(f"{label} path is outside the repository root")
    return logical


def _required_capture(
    reader: _PinnedRepository,
    logical: str,
    label: str,
) -> bytes:
    capture, issue = reader.capture(logical, read_payload=True)
    if issue is not None or capture is None or capture.payload is None:
        raise SizePolicyError(f"{label} {issue}")
    return capture.payload


def _read_seed_manifest(
    reader: _PinnedRepository,
    logical_manifest: str,
) -> tuple[dict[str, str], dict[str, int]]:
    data = _required_capture(reader, logical_manifest, "seed manifest")
    return _decode_seed_manifest(data, logical_manifest)


def _decode_seed_manifest(
    data: bytes,
    logical_manifest: str,
) -> tuple[dict[str, str], dict[str, int]]:
    decoded = _load_json_object(data, "seed manifest")
    if not isinstance(decoded, dict):
        raise SizePolicyError("seed manifest is not an object")
    if decoded.get("schema") != SEED_SCHEMA:
        raise SizePolicyError("seed manifest schema differs")
    artifacts = decoded.get("artifacts")
    if not isinstance(artifacts, list):
        raise SizePolicyError("seed manifest artifacts are not a list")

    selected: dict[str, tuple[str, int]] = {}
    files = set()
    for index, artifact in enumerate(artifacts):
        if not isinstance(artifact, dict):
            raise SizePolicyError(f"seed manifest artifact {index} is not an object")
        name = artifact.get("name")
        filename = artifact.get("file")
        size = artifact.get("size")
        if not isinstance(name, str) or name not in SEED_ARTIFACT_OWNERS:
            raise SizePolicyError(
                f"seed manifest artifact {index} has an unknown tool name"
            )
        if name in selected:
            raise SizePolicyError(f"seed manifest artifact is duplicated: {name}")
        expected_filename = f"{name}.elf"
        if filename != expected_filename:
            raise SizePolicyError(
                f"seed manifest artifact {name} must use {expected_filename}"
            )
        if filename in files:
            raise SizePolicyError(f"seed manifest file is duplicated: {filename}")
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise SizePolicyError(f"seed manifest artifact {name} has an invalid size")
        files.add(filename)
        selected[name] = (filename, size)

    missing = sorted(set(SEED_ARTIFACT_OWNERS) - set(selected))
    if missing:
        raise SizePolicyError(
            "seed manifest is missing required artifacts: " + ", ".join(missing)
        )
    manifest_parent = PurePosixPath(logical_manifest).parent
    owners = {}
    sizes = {}
    for name, owner in SEED_ARTIFACT_OWNERS.items():
        filename, size = selected[name]
        logical = (manifest_parent / filename).as_posix()
        owners[logical] = owner
        sizes[logical] = size
    return owners, sizes


def _read_policy(
    reader: _PinnedRepository,
    logical_policy: str,
    expected_owners: dict[str, str],
    seed_sizes: dict[str, int],
) -> list[dict[str, object]]:
    data = _required_capture(reader, logical_policy, "policy file")
    return _decode_policy(data, expected_owners, seed_sizes)


def _decode_policy(
    data: bytes,
    expected_owners: dict[str, str],
    seed_sizes: dict[str, int],
) -> list[dict[str, object]]:
    decoded = _load_json_object(data)
    if not isinstance(decoded, dict):
        raise SizePolicyError("policy is not an object")
    if set(decoded) != POLICY_KEYS:
        missing = sorted(POLICY_KEYS - set(decoded))
        unknown = sorted(set(decoded) - POLICY_KEYS)
        if unknown:
            raise SizePolicyError("policy has unknown fields: " + ", ".join(unknown))
        raise SizePolicyError("policy is missing fields: " + ", ".join(missing))
    if decoded["schema"] != SCHEMA:
        raise SizePolicyError("policy schema differs")
    artifacts = decoded["artifacts"]
    if not isinstance(artifacts, list):
        raise SizePolicyError("policy artifacts are not a list")

    paths = []
    entries = []
    for index, entry in enumerate(artifacts):
        if not isinstance(entry, dict):
            raise SizePolicyError(f"policy artifact {index} is not an object")
        if set(entry) != ENTRY_KEYS:
            unknown = sorted(set(entry) - ENTRY_KEYS)
            missing = sorted(ENTRY_KEYS - set(entry))
            if unknown:
                raise SizePolicyError(
                    f"policy artifact {index} has unknown fields: " + ", ".join(unknown)
                )
            raise SizePolicyError(
                f"policy artifact {index} is missing fields: " + ", ".join(missing)
            )
        path = _logical_path(entry["path"])
        if path in paths:
            raise SizePolicyError(f"policy artifact is duplicated: {path}")
        paths.append(path)
        exact_bytes = entry["exact_bytes"]
        if (
            not isinstance(exact_bytes, int)
            or isinstance(exact_bytes, bool)
            or exact_bytes <= 0
        ):
            raise SizePolicyError(f"policy artifact {path} has an invalid exact size")
        producer = entry["producer"]
        if not isinstance(producer, str) or not producer:
            raise SizePolicyError(f"policy artifact {path} has an invalid producer")
        reason = entry["reason"]
        if (
            not isinstance(reason, str)
            or not reason
            or reason.strip() != reason
            or "\n" in reason
            or "\r" in reason
        ):
            raise SizePolicyError(f"policy artifact {path} has an invalid reason")
        entries.append(entry)

    expected_paths = set(expected_owners)
    actual_paths = set(paths)
    unknown_paths = sorted(actual_paths - expected_paths)
    if unknown_paths:
        raise SizePolicyError(
            "policy has unknown artifacts: " + ", ".join(unknown_paths)
        )
    missing_paths = sorted(expected_paths - actual_paths)
    if missing_paths:
        raise SizePolicyError(
            "policy is missing required artifacts: " + ", ".join(missing_paths)
        )
    if paths != sorted(paths):
        raise SizePolicyError("policy artifacts are not in canonical order")
    for entry in entries:
        path = str(entry["path"])
        expected_owner = expected_owners[path]
        if entry["producer"] != expected_owner:
            raise SizePolicyError(
                f"policy artifact {path} must name {expected_owner} as producer"
            )
        if path in seed_sizes and entry["exact_bytes"] != seed_sizes[path]:
            raise SizePolicyError(
                f"policy artifact {path} has exact size "
                f"{entry['exact_bytes']}, but the selected seed manifest "
                f"declares {seed_sizes[path]}"
            )
    return entries


def verify(
    root_argument: Path,
    policy_argument: Path,
    seed_manifest_argument: Path,
) -> None:
    root = Path(os.path.abspath(root_argument))
    try:
        root_info = root.lstat()
    except OSError as error:
        raise SizePolicyError(f"cannot inspect repository root: {error}") from error
    if _is_link_or_reparse(root_info) or not stat.S_ISDIR(root_info.st_mode):
        raise SizePolicyError("repository root is not a regular directory")

    logical_policy = _repository_argument(root, policy_argument, "policy")
    logical_manifest = _repository_argument(
        root,
        seed_manifest_argument,
        "seed manifest",
    )
    try:
        reader_context = _PinnedRepository(root)
    except OSError as error:
        raise SizePolicyError(f"cannot inspect repository root: {error}") from error
    with reader_context as reader:
        seed_owners, seed_sizes = _read_seed_manifest(
            reader,
            logical_manifest,
        )
        expected_owners = dict(FIXED_ARTIFACT_OWNERS)
        expected_owners.update(seed_owners)
        if len(expected_owners) != ARTIFACT_COUNT:
            raise SizePolicyError(
                "selected seed artifacts overlap the fixed output cohort"
            )
        entries = _read_policy(
            reader,
            logical_policy,
            expected_owners,
            seed_sizes,
        )
        failures = []
        for entry in entries:
            path = str(entry["path"])
            capture, issue = reader.capture(path, read_payload=False)
            if issue is not None or capture is None:
                failures.append(issue)
                continue
            size = capture.status.st_size
            expected = int(entry["exact_bytes"])
            if size != expected:
                failures.append(
                    f"{path} has {size} {_plural_bytes(size)}; "
                    f"expected exactly {expected} {_plural_bytes(expected)}"
                )
        if failures:
            raise SizePolicyError("\n- " + "\n- ".join(failures))
        reader.require_unchanged()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify_parser = subparsers.add_parser("verify")
    verify_parser.add_argument("--root", type=Path, required=True)
    verify_parser.add_argument("--policy", type=Path, required=True)
    verify_parser.add_argument("--seed-manifest", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        verify(args.root, args.policy, args.seed_manifest)
    except SizePolicyError as error:
        message = str(error)
        if message.startswith("\n-"):
            sys.stderr.write("artifact size verification failed:" + message + "\n")
        else:
            sys.stderr.write(f"artifact size verification failed: {message}\n")
        return 1
    sys.stdout.write(f"Cupid artifact sizes: ok ({ARTIFACT_COUNT} exact artifacts)\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

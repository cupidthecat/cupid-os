#!/usr/bin/env python3
"""Portable host-side build helpers for CupidOS.

This module replaces shell-only build steps with Python so the same Makefile
can run under Linux shells and native Windows GNU Make.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
from contextlib import ExitStack, contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import BinaryIO, Iterator

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        freeze_seed_inputs,
        run_seed_tool,
        verify_seed_inputs,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        freeze_seed_inputs,
        run_seed_tool,
        verify_seed_inputs,
    )


SECTOR_SIZE = 512
FAT16_TYPES = {0x04, 0x06, 0x0E}
FAT16_EOC = 0xFFFF
FAT16_EOC_MIN = 0xFFF8
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


class KsymsGenerationError(RuntimeError):
    """The kernel symbol source could not be generated safely."""


class EmbedJpegError(RuntimeError):
    """A JPEG asset could not be wrapped safely."""


class EmbedJpegCopyError(EmbedJpegError):
    """A validated JPEG could not be copied to private storage."""


class IsoAuthoringError(RuntimeError):
    """A deterministic ISO fixture could not be authored safely."""


class InstallSourceGenerationError(RuntimeError):
    """An installation table could not be generated safely."""


class DiskImageError(RuntimeError):
    """A persistent disk image could not be published safely."""


@dataclass(frozen=True)
class StageFile:
    source: Path
    dest: str


@dataclass(frozen=True)
class _DiskFileSnapshot:
    requested: Path
    resolved: Path
    size: int
    sha256: str


@dataclass(frozen=True)
class FatLayout:
    partition_sectors: int
    sectors_per_cluster: int
    reserved_sectors: int
    num_fats: int
    root_entries: int
    root_dir_sectors: int
    sectors_per_fat: int
    data_sectors: int
    cluster_count: int


@dataclass(frozen=True)
class _IsoSource:
    name: str
    relative: str
    data: bytes | None
    children: tuple[_IsoSource, ...]

    @property
    def is_directory(self) -> bool:
        return self.data is None


@dataclass(frozen=True)
class _IsoManifest:
    path: Path
    fixture_root: Path
    payload: bytes
    entries: frozenset[str]


@dataclass
class _IsoNode:
    source: _IsoSource
    parent: _IsoNode | None
    identifier: bytes
    children: list[_IsoNode]
    extent: int = 0
    size: int = 0
    directory_number: int = 0

    @property
    def is_directory(self) -> bool:
        return self.source.is_directory


def _ceil_div(a: int, b: int) -> int:
    return (a + b - 1) // b


def _parse_stage(value: str) -> StageFile:
    if ":" not in value:
        raise argparse.ArgumentTypeError("stage entries must be SRC:/guest/path")
    src, dest = value.split(":", 1)
    if not dest.startswith("/"):
        raise argparse.ArgumentTypeError("stage destination must start with /")
    return StageFile(Path(src), dest)


def _choose_layout(partition_sectors: int) -> FatLayout:
    reserved = 1
    num_fats = 2
    root_entries = 512
    root_dir_sectors = _ceil_div(root_entries * 32, SECTOR_SIZE)

    for spc in (1, 2, 4, 8, 16, 32, 64):
        sectors_per_fat = 1
        seen_fat_sizes: set[int] = set()
        while True:
            if sectors_per_fat in seen_fat_sizes:
                break
            seen_fat_sizes.add(sectors_per_fat)
            data_sectors = partition_sectors - reserved - root_dir_sectors - num_fats * sectors_per_fat
            if data_sectors <= 0:
                break
            clusters = data_sectors // spc
            needed_fat = _ceil_div((clusters + 2) * 2, SECTOR_SIZE)
            if needed_fat == sectors_per_fat:
                if 4085 <= clusters < 65525:
                    return FatLayout(
                        partition_sectors=partition_sectors,
                        sectors_per_cluster=spc,
                        reserved_sectors=reserved,
                        num_fats=num_fats,
                        root_entries=root_entries,
                        root_dir_sectors=root_dir_sectors,
                        sectors_per_fat=sectors_per_fat,
                        data_sectors=data_sectors,
                        cluster_count=clusters,
                    )
                break
            sectors_per_fat = needed_fat

    raise ValueError(f"cannot make FAT16 layout for {partition_sectors} sectors")


def _partition_info(image: Path) -> tuple[int, int, int] | None:
    if not image.exists() or image.stat().st_size < SECTOR_SIZE:
        return None
    with image.open("rb") as f:
        mbr = f.read(SECTOR_SIZE)
    if len(mbr) != SECTOR_SIZE or mbr[510:512] != b"\x55\xaa":
        return None
    ptype = mbr[450]
    start = struct.unpack_from("<I", mbr, 454)[0]
    sectors = struct.unpack_from("<I", mbr, 458)[0]
    return ptype, start, sectors


def _valid_existing_image(image: Path, hdd_mb: int, fat_start_lba: int) -> bool:
    expected_size = hdd_mb * 1024 * 1024
    if not image.exists() or image.stat().st_size != expected_size:
        return False
    info = _partition_info(image)
    if not info:
        return False
    ptype, start, sectors = info
    image_sectors = expected_size // SECTOR_SIZE
    partition_sectors = image_sectors - fat_start_lba
    if (
        ptype not in FAT16_TYPES
        or start != fat_start_lba
        or sectors != partition_sectors
    ):
        return False
    with image.open("rb") as f:
        f.seek(fat_start_lba * SECTOR_SIZE)
        bpb = f.read(SECTOR_SIZE)
    if len(bpb) != SECTOR_SIZE or bpb[510:512] != b"\x55\xaa":
        return False
    bytes_per_sector = struct.unpack_from("<H", bpb, 11)[0]
    sectors_per_cluster = bpb[13]
    reserved_sectors = struct.unpack_from("<H", bpb, 14)[0]
    num_fats = bpb[16]
    root_entries = struct.unpack_from("<H", bpb, 17)[0]
    total_sectors = struct.unpack_from("<H", bpb, 19)[0]
    if total_sectors == 0:
        total_sectors = struct.unpack_from("<I", bpb, 32)[0]
    sectors_per_fat = struct.unpack_from("<H", bpb, 22)[0]
    hidden_sectors = struct.unpack_from("<I", bpb, 28)[0]
    if (
        bytes_per_sector != SECTOR_SIZE
        or sectors_per_cluster not in (1, 2, 4, 8, 16, 32, 64)
        or reserved_sectors == 0
        or num_fats not in (1, 2)
        or root_entries == 0
        or total_sectors != partition_sectors
        or sectors_per_fat == 0
        or hidden_sectors != fat_start_lba
    ):
        return False
    root_dir_sectors = _ceil_div(root_entries * 32, SECTOR_SIZE)
    data_start = (
        reserved_sectors + num_fats * sectors_per_fat + root_dir_sectors
    )
    if data_start >= total_sectors:
        return False
    cluster_count = (total_sectors - data_start) // sectors_per_cluster
    fat_entries = sectors_per_fat * SECTOR_SIZE // 2
    return 4085 <= cluster_count < 65525 and cluster_count + 2 <= fat_entries


def _write_mbr(f, bootloader: bytes, fat_start_lba: int, partition_sectors: int) -> None:
    mbr = bytearray(SECTOR_SIZE)
    mbr[: min(446, len(bootloader))] = bootloader[:446]
    off = 446
    mbr[off] = 0x80
    mbr[off + 1 : off + 4] = b"\xfe\xff\xff"
    mbr[off + 4] = 0x06
    mbr[off + 5 : off + 8] = b"\xfe\xff\xff"
    struct.pack_into("<I", mbr, off + 8, fat_start_lba)
    struct.pack_into("<I", mbr, off + 12, partition_sectors)
    mbr[510:512] = b"\x55\xaa"
    f.seek(0)
    f.write(mbr)


def _write_fat16_filesystem(f, fat_start_lba: int, layout: FatLayout) -> None:
    part = fat_start_lba * SECTOR_SIZE
    total16 = layout.partition_sectors if layout.partition_sectors < 65536 else 0
    total32 = 0 if total16 else layout.partition_sectors

    bpb = bytearray(SECTOR_SIZE)
    bpb[0:3] = b"\xeb\x3c\x90"
    bpb[3:11] = b"CUPIDOS "
    struct.pack_into("<H", bpb, 11, SECTOR_SIZE)
    bpb[13] = layout.sectors_per_cluster
    struct.pack_into("<H", bpb, 14, layout.reserved_sectors)
    bpb[16] = layout.num_fats
    struct.pack_into("<H", bpb, 17, layout.root_entries)
    struct.pack_into("<H", bpb, 19, total16)
    bpb[21] = 0xF8
    struct.pack_into("<H", bpb, 22, layout.sectors_per_fat)
    struct.pack_into("<H", bpb, 24, 63)
    struct.pack_into("<H", bpb, 26, 255)
    struct.pack_into("<I", bpb, 28, fat_start_lba)
    struct.pack_into("<I", bpb, 32, total32)
    bpb[36] = 0x80
    bpb[38] = 0x29
    struct.pack_into("<I", bpb, 39, 0xC001D05)
    bpb[43:54] = b"CUPIDOS    "
    bpb[54:62] = b"FAT16   "
    bpb[510:512] = b"\x55\xaa"

    f.seek(part)
    f.write(bpb)

    zero = b"\x00" * SECTOR_SIZE
    for fat_idx in range(layout.num_fats):
        fat_base = part + (layout.reserved_sectors + fat_idx * layout.sectors_per_fat) * SECTOR_SIZE
        f.seek(fat_base)
        first = bytearray(SECTOR_SIZE)
        first[0:4] = b"\xf8\xff\xff\xff"
        f.write(first)
        for _ in range(1, layout.sectors_per_fat):
            f.write(zero)

    root_base = part + (
        layout.reserved_sectors + layout.num_fats * layout.sectors_per_fat
    ) * SECTOR_SIZE
    f.seek(root_base)
    for _ in range(layout.root_dir_sectors):
        f.write(zero)


class Fat16Image:
    def __init__(self, image: Path, fat_start_lba: int):
        self.image = image
        self.fat_start_lba = fat_start_lba
        self.f = image.open("r+b")
        self._read_bpb()

    def close(self) -> None:
        self.f.close()

    def __enter__(self) -> "Fat16Image":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _read_bpb(self) -> None:
        self.part = self.fat_start_lba * SECTOR_SIZE
        self.f.seek(self.part)
        b = self.f.read(SECTOR_SIZE)
        if len(b) != SECTOR_SIZE or b[510:512] != b"\x55\xaa":
            raise ValueError("invalid FAT16 boot sector")
        self.bytes_per_sector = struct.unpack_from("<H", b, 11)[0]
        self.sectors_per_cluster = b[13]
        self.reserved_sectors = struct.unpack_from("<H", b, 14)[0]
        self.num_fats = b[16]
        self.root_entries = struct.unpack_from("<H", b, 17)[0]
        self.total_sectors = struct.unpack_from("<H", b, 19)[0] or struct.unpack_from("<I", b, 32)[0]
        self.sectors_per_fat = struct.unpack_from("<H", b, 22)[0]
        if self.bytes_per_sector != SECTOR_SIZE:
            raise ValueError("only 512-byte FAT16 sectors are supported")

        self.root_dir_sectors = _ceil_div(self.root_entries * 32, SECTOR_SIZE)
        self.fat_start = self.reserved_sectors
        self.root_start = self.reserved_sectors + self.num_fats * self.sectors_per_fat
        self.data_start = self.root_start + self.root_dir_sectors
        self.cluster_count = (
            self.total_sectors - self.data_start
        ) // self.sectors_per_cluster

    def _fat_offset(self, cluster: int, fat_idx: int = 0) -> int:
        return self.part + (
            self.fat_start + fat_idx * self.sectors_per_fat
        ) * SECTOR_SIZE + cluster * 2

    def _read_fat(self, cluster: int) -> int:
        self.f.seek(self._fat_offset(cluster))
        return struct.unpack("<H", self.f.read(2))[0]

    def _write_fat(self, cluster: int, value: int) -> None:
        for fat_idx in range(self.num_fats):
            self.f.seek(self._fat_offset(cluster, fat_idx))
            self.f.write(struct.pack("<H", value))

    def _cluster_offset(self, cluster: int) -> int:
        if cluster < 2:
            raise ValueError("cluster numbers start at 2")
        rel_sector = self.data_start + (cluster - 2) * self.sectors_per_cluster
        return self.part + rel_sector * SECTOR_SIZE

    def _read_dir_entries(self, dir_cluster: int | None) -> tuple[bytearray, int]:
        if dir_cluster is None:
            off = self.part + self.root_start * SECTOR_SIZE
            size = self.root_dir_sectors * SECTOR_SIZE
        else:
            off = self._cluster_offset(dir_cluster)
            size = self.sectors_per_cluster * SECTOR_SIZE
        self.f.seek(off)
        return bytearray(self.f.read(size)), off

    def _write_dir_entries(self, dir_cluster: int | None, data: bytes) -> None:
        if dir_cluster is None:
            off = self.part + self.root_start * SECTOR_SIZE
        else:
            off = self._cluster_offset(dir_cluster)
        self.f.seek(off)
        self.f.write(data)

    def _alloc_cluster(self) -> int:
        for c in range(2, self.cluster_count + 2):
            if self._read_fat(c) == 0:
                self._write_fat(c, FAT16_EOC)
                self.f.seek(self._cluster_offset(c))
                self.f.write(b"\x00" * (self.sectors_per_cluster * SECTOR_SIZE))
                return c
        raise OSError("FAT16 partition is full")

    def _free_chain(self, cluster: int) -> None:
        remaining = self.cluster_count
        while 2 <= cluster < FAT16_EOC_MIN:
            if cluster >= self.cluster_count + 2 or remaining == 0:
                raise ValueError("FAT16 file chain is corrupt")
            nxt = self._read_fat(cluster)
            self._write_fat(cluster, 0)
            cluster = nxt
            remaining -= 1

    @staticmethod
    def _short_name(component: str) -> bytes:
        component = component.replace("\\", "/").split("/")[-1]
        if component in ("", ".", ".."):
            raise ValueError(f"invalid FAT name: {component!r}")
        if "." in component:
            stem, ext = component.rsplit(".", 1)
        else:
            stem, ext = component, ""
        clean_stem = "".join(ch for ch in stem.upper() if ch.isalnum() or ch in "$%'-_@~`!(){}^#&")
        clean_ext = "".join(ch for ch in ext.upper() if ch.isalnum() or ch in "$%'-_@~`!(){}^#&")
        if not clean_stem:
            raise ValueError(f"invalid FAT name: {component!r}")
        if len(clean_stem) > 8:
            clean_stem = clean_stem[:6] + "~1"
        if len(clean_ext) > 3:
            clean_ext = clean_ext[:3]
        return clean_stem[:8].ljust(8).encode("ascii") + clean_ext[:3].ljust(3).encode("ascii")

    @staticmethod
    def _entry_name(entry: bytes) -> bytes:
        return bytes(entry[:11])

    def _find_entry(self, dir_cluster: int | None, name83: bytes) -> tuple[int, bytearray, int] | None:
        data, off = self._read_dir_entries(dir_cluster)
        for idx in range(0, len(data), 32):
            first = data[idx]
            if first == 0x00:
                return None
            if first == 0xE5:
                continue
            if data[idx + 11] == 0x0F:
                continue
            if self._entry_name(data[idx : idx + 32]) == name83:
                return idx, data, off
        return None

    def _find_free_entry(self, dir_cluster: int | None) -> tuple[int, bytearray]:
        data, _ = self._read_dir_entries(dir_cluster)
        for idx in range(0, len(data), 32):
            if data[idx] in (0x00, 0xE5):
                return idx, data
        raise OSError("directory is full")

    def _put_entry(
        self,
        data: bytearray,
        idx: int,
        name83: bytes,
        attr: int,
        first_cluster: int,
        size: int,
    ) -> None:
        entry = bytearray(32)
        entry[0:11] = name83
        entry[11] = attr
        struct.pack_into("<H", entry, 26, first_cluster)
        struct.pack_into("<I", entry, 28, size)
        data[idx : idx + 32] = entry

    def mkdir(self, path: str) -> int:
        parent, name = self._walk_parent(path)
        name83 = self._short_name(name)
        found = self._find_entry(parent, name83)
        if found:
            idx, data, _ = found
            if not (data[idx + 11] & 0x10):
                raise ValueError(f"{path} exists and is not a directory")
            return struct.unpack_from("<H", data, idx + 26)[0]

        cluster = self._alloc_cluster()
        child = bytearray(self.sectors_per_cluster * SECTOR_SIZE)
        self._put_entry(child, 0, b".          ", 0x10, cluster, 0)
        self._put_entry(child, 32, b"..         ", 0x10, parent or 0, 0)
        self._write_dir_entries(cluster, child)

        idx, data = self._find_free_entry(parent)
        self._put_entry(data, idx, name83, 0x10, cluster, 0)
        self._write_dir_entries(parent, data)
        return cluster

    def _walk_parent(self, path: str) -> tuple[int | None, str]:
        parts = [p for p in path.replace("\\", "/").strip("/").split("/") if p]
        if not parts:
            raise ValueError("path must name a file or directory")
        parent: int | None = None
        for part in parts[:-1]:
            name83 = self._short_name(part)
            found = self._find_entry(parent, name83)
            if not found:
                parent = self.mkdir("/".join(parts[: parts.index(part) + 1]))
                continue
            idx, data, _ = found
            if not (data[idx + 11] & 0x10):
                raise ValueError(f"{part} exists and is not a directory")
            parent = struct.unpack_from("<H", data, idx + 26)[0]
        return parent, parts[-1]

    def _ensure_parent_dirs(self, path: str) -> int | None:
        parts = [p for p in path.replace("\\", "/").strip("/").split("/") if p]
        parent: int | None = None
        current: list[str] = []
        for part in parts[:-1]:
            current.append(part)
            name83 = self._short_name(part)
            found = self._find_entry(parent, name83)
            if found:
                idx, data, _ = found
                if not (data[idx + 11] & 0x10):
                    raise ValueError(f"{part} exists and is not a directory")
                parent = struct.unpack_from("<H", data, idx + 26)[0]
            else:
                parent = self.mkdir("/".join(current))
        return parent

    def write_file(self, dest: str, payload: bytes) -> None:
        parent = self._ensure_parent_dirs(dest)
        name = dest.replace("\\", "/").strip("/").split("/")[-1]
        name83 = self._short_name(name)

        found = self._find_entry(parent, name83)
        if found:
            idx, data, _ = found
            old_cluster = struct.unpack_from("<H", data, idx + 26)[0]
            if old_cluster >= 2:
                self._free_chain(old_cluster)
        else:
            idx, data = self._find_free_entry(parent)

        cluster_size = self.sectors_per_cluster * SECTOR_SIZE
        clusters_needed = _ceil_div(len(payload), cluster_size) if payload else 0
        first_cluster = 0
        previous = 0
        for _ in range(clusters_needed):
            cluster = self._alloc_cluster()
            if first_cluster == 0:
                first_cluster = cluster
            if previous:
                self._write_fat(previous, cluster)
            previous = cluster
        if previous:
            self._write_fat(previous, FAT16_EOC)

        cursor = 0
        cluster = first_cluster
        while cluster and cursor < len(payload):
            to_write = payload[cursor : cursor + cluster_size]
            self.f.seek(self._cluster_offset(cluster))
            self.f.write(to_write)
            if len(to_write) < cluster_size:
                self.f.write(b"\x00" * (cluster_size - len(to_write)))
            cursor += len(to_write)
            nxt = self._read_fat(cluster)
            cluster = 0 if nxt >= FAT16_EOC_MIN else nxt

        self._put_entry(data, idx, name83, 0x20, first_cluster, len(payload))
        self._write_dir_entries(parent, data)
        self.f.flush()


def _disk_is_link_or_junction(path: Path, description: str) -> bool:
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
        return bool(
            getattr(status, "st_file_attributes", 0) & reparse_point
        )
    except OSError as error:
        raise DiskImageError(
            f"{description} cannot be inspected: {path}: {error}"
        ) from error


def _disk_absolute(path: Path) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def _resolve_disk_regular(path: Path, description: str) -> Path:
    requested = _disk_absolute(path)
    if _disk_is_link_or_junction(requested, description):
        raise DiskImageError(
            f"{description} may not be a symbolic link or junction: "
            f"{requested}"
        )
    try:
        resolved = requested.resolve(strict=True)
        mode = resolved.lstat().st_mode
    except OSError as error:
        raise DiskImageError(
            f"{description} cannot be resolved: {requested}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise DiskImageError(
            f"{description} is not a regular file: {resolved}"
        )
    return resolved


def _capture_disk_file(
    path: Path,
    description: str,
    *,
    frozen: Path | None = None,
) -> _DiskFileSnapshot:
    requested = _disk_absolute(path)
    resolved = _resolve_disk_regular(requested, description)
    digest = hashlib.sha256()
    size = 0
    destination = None
    try:
        if frozen is not None:
            frozen.parent.mkdir(parents=True, exist_ok=True)
            destination = frozen.open("xb")
        with resolved.open("rb") as source:
            before = os.fstat(source.fileno())
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
            raise DiskImageError(
                f"{description} changed while it was being frozen"
            )
        if destination is not None:
            destination.flush()
            os.fsync(destination.fileno())
    except DiskImageError:
        raise
    except OSError as error:
        raise DiskImageError(
            f"{description} cannot be read: {resolved}: {error}"
        ) from error
    finally:
        if destination is not None:
            destination.close()
    return _DiskFileSnapshot(
        requested=requested,
        resolved=resolved,
        size=size,
        sha256=digest.hexdigest(),
    )


def _require_disk_file_unchanged(
    snapshot: _DiskFileSnapshot,
    description: str,
) -> None:
    current = _capture_disk_file(snapshot.requested, description)
    if (
        current.resolved != snapshot.resolved
        or current.size != snapshot.size
        or current.sha256 != snapshot.sha256
    ):
        raise DiskImageError(
            f"{description} changed while authoring the disk image"
        )


def _disk_output_path(image: Path) -> Path:
    requested = _disk_absolute(image)
    try:
        requested.parent.mkdir(parents=True, exist_ok=True)
        parent = requested.parent.resolve(strict=True)
    except OSError as error:
        raise DiskImageError(
            f"disk image directory cannot be prepared: "
            f"{requested.parent}: {error}"
        ) from error
    output = parent / requested.name
    if _disk_is_link_or_junction(output, "disk image output"):
        raise DiskImageError(
            f"disk image output may not be a symbolic link or junction: "
            f"{output}"
        )
    try:
        mode = output.lstat().st_mode
    except FileNotFoundError:
        return output
    except OSError as error:
        raise DiskImageError(
            f"disk image output cannot be inspected: {output}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise DiskImageError(
            f"disk image output is not a regular file: {output}"
        )
    return output


def _disk_paths_alias(output: Path, input_path: Path) -> bool:
    if output == input_path:
        return True
    if not output.exists() or not input_path.exists():
        return False
    try:
        return os.path.samefile(output, input_path)
    except FileNotFoundError:
        return False
    except OSError as error:
        raise DiskImageError(
            f"disk image path identity cannot be checked: "
            f"{input_path}: {error}"
        ) from error


def _acquire_disk_publication_lock(output: Path) -> tuple[object, str]:
    lock_name = hashlib.sha256(
        str(output).casefold().encode("utf-8")
    ).hexdigest()
    lock_root = Path(tempfile.gettempdir()) / "cupid-hostbuild-locks"
    try:
        lock_root.mkdir(mode=0o700, parents=True, exist_ok=True)
        stream = (lock_root / f"{lock_name}.lock").open("a+b")
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"\0")
            stream.flush()
            os.fsync(stream.fileno())
        stream.seek(0)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            platform = "windows"
        else:
            import fcntl

            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            platform = "posix"
    except (ImportError, OSError) as error:
        try:
            stream.close()
        except (NameError, OSError):
            pass
        raise DiskImageError(
            f"another hostbuild publisher is active for {output}, or its "
            f"publication lock is unavailable: {error}"
        ) from error
    return stream, platform


def _release_disk_publication_lock(lock: tuple[object, str]) -> None:
    stream, platform = lock
    try:
        stream.seek(0)
        if platform == "windows":
            import msvcrt

            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
    except (ImportError, OSError):
        pass
    finally:
        try:
            stream.close()
        except OSError:
            pass


def _disk_template_length(layout: FatLayout, fat_start_lba: int) -> int:
    data_start = (
        layout.reserved_sectors
        + layout.num_fats * layout.sectors_per_fat
        + layout.root_dir_sectors
    )
    return (fat_start_lba + data_start) * SECTOR_SIZE


def _write_pristine_disk_template(
    output: Path,
    bootloader: Path,
    kernel: Path,
    image_sectors: int,
    fat_start_lba: int,
) -> int:
    if fat_start_lba <= 5:
        raise ValueError(
            "FAT partition must start after bootloader and kernel area"
        )
    if fat_start_lba >= image_sectors:
        raise ValueError("FAT partition start is beyond image size")
    boot = bootloader.read_bytes()
    if len(boot) < 5 * SECTOR_SIZE:
        raise ValueError(
            f"{bootloader} is too small; expected at least 5 sectors"
        )
    kernel_size = kernel.stat().st_size
    fat_start_bytes = fat_start_lba * SECTOR_SIZE
    if 5 * SECTOR_SIZE + kernel_size > fat_start_bytes:
        raise ValueError(
            f"{kernel} ({kernel_size} bytes) overlaps FAT partition at "
            f"LBA {fat_start_lba}"
        )
    partition_sectors = image_sectors - fat_start_lba
    layout = _choose_layout(partition_sectors)
    with output.open("xb") as stream:
        _write_mbr(stream, boot, fat_start_lba, partition_sectors)
        stream.seek(SECTOR_SIZE)
        stream.write(boot[SECTOR_SIZE : 5 * SECTOR_SIZE])
        stream.seek(5 * SECTOR_SIZE)
        with kernel.open("rb") as kernel_stream:
            shutil.copyfileobj(kernel_stream, stream)
        _write_fat16_filesystem(stream, fat_start_lba, layout)
        stream.flush()
        os.fsync(stream.fileno())
    return _disk_template_length(layout, fat_start_lba)


def _copy_disk_prefix(source, destination, length: int) -> None:
    remaining = length
    while remaining:
        block = source.read(min(1024 * 1024, remaining))
        if not block:
            raise DiskImageError(
                "checked CupidObj disk template ended before the FAT boundary"
            )
        destination.write(block)
        remaining -= len(block)


def _require_disk_output_unchanged(
    output: Path,
    initial: _DiskFileSnapshot | None,
) -> None:
    if initial is None:
        if output.exists() or _disk_is_link_or_junction(
            output, "disk image output"
        ):
            raise DiskImageError(
                "disk image output appeared while authoring the image"
            )
        return
    current = _capture_disk_file(output, "disk image output")
    if (
        current.size != initial.size
        or current.sha256 != initial.sha256
    ):
        raise DiskImageError(
            "disk image output changed while authoring the image"
        )


def create_or_update_image(
    image: Path,
    bootloader: Path,
    kernel: Path,
    hdd_mb: int,
    fat_start_lba: int,
    stage_files: list[StageFile],
    force_format: bool,
    *,
    seed_manifest: Path,
) -> None:
    try:
        image_bytes = hdd_mb * 1024 * 1024
        if hdd_mb <= 0 or image_bytes % SECTOR_SIZE:
            raise DiskImageError("disk image size must be positive")
        image_sectors = image_bytes // SECTOR_SIZE
        if fat_start_lba <= 5:
            raise DiskImageError(
                "FAT partition must start after bootloader and kernel area"
            )
        if fat_start_lba >= image_sectors:
            raise DiskImageError("FAT partition start is beyond image size")
        layout = _choose_layout(image_sectors - fat_start_lba)
        template_length = _disk_template_length(layout, fat_start_lba)
        fat_start_bytes = fat_start_lba * SECTOR_SIZE

        output = _disk_output_path(image)
        manifest = _resolve_disk_regular(
            seed_manifest, "checked seed manifest"
        )
        try:
            checked_seed = verify_seed_inputs(manifest)
        except BootstrapError as error:
            raise DiskImageError(
                f"checked seed could not be verified: {error}"
            ) from error
        seed_paths = [manifest, *checked_seed.tools.values()]
        seed_snapshots = [
            _capture_disk_file(
                path,
                "checked seed manifest"
                if path == manifest
                else f"checked seed artifact {path.name}",
            )
            for path in seed_paths
        ]

        boot = _resolve_disk_regular(bootloader, "bootloader input")
        kernel_input = _resolve_disk_regular(kernel, "kernel input")
        present_stages: list[tuple[StageFile, Path]] = []
        missing_stages: list[StageFile] = []
        for stage in stage_files:
            requested = _disk_absolute(stage.source)
            if _disk_is_link_or_junction(requested, "stage input"):
                raise DiskImageError(
                    "stage input may not be a symbolic link or junction: "
                    f"{requested}"
                )
            if requested.exists():
                present_stages.append(
                    (
                        stage,
                        _resolve_disk_regular(requested, "stage input"),
                    )
                )
            else:
                missing_stages.append(
                    StageFile(requested, stage.dest)
                )

        aliased_inputs = [
            *seed_paths,
            boot,
            kernel_input,
            *(path for _, path in present_stages),
            *(stage.source for stage in missing_stages),
        ]
        if any(_disk_paths_alias(output, path) for path in aliased_inputs):
            raise DiskImageError("disk image output may not replace an input")

        publication_lock = _acquire_disk_publication_lock(output)
        try:
            with tempfile.TemporaryDirectory(
                prefix=f".{output.name}.image-",
                dir=output.parent,
            ) as temporary:
                private = Path(temporary)
                frozen_boot = private / "input" / "boot.bin"
                frozen_kernel = private / "input" / "kernel.bin"
                boot_snapshot = _capture_disk_file(
                    bootloader,
                    "bootloader input",
                    frozen=frozen_boot,
                )
                kernel_snapshot = _capture_disk_file(
                    kernel,
                    "kernel input",
                    frozen=frozen_kernel,
                )
                if kernel_snapshot.size + 5 * SECTOR_SIZE > fat_start_bytes:
                    raise DiskImageError(
                        f"{kernel} ({kernel_snapshot.size} bytes) overlaps "
                        f"FAT partition at LBA {fat_start_lba}"
                    )

                frozen_stages: list[
                    tuple[StageFile, _DiskFileSnapshot, Path]
                ] = []
                for index, (stage, _) in enumerate(present_stages):
                    frozen_stage = (
                        private / "input" / "stage" / f"{index:04d}.bin"
                    )
                    snapshot = _capture_disk_file(
                        stage.source,
                        "stage input",
                        frozen=frozen_stage,
                    )
                    frozen_stages.append((stage, snapshot, frozen_stage))

                existing_copy = private / "existing.img"
                initial_output: _DiskFileSnapshot | None = None
                if output.exists():
                    try:
                        output_size = output.stat().st_size
                    except OSError as error:
                        raise DiskImageError(
                            f"disk image output cannot be inspected: "
                            f"{output}: {error}"
                        ) from error
                    initial_output = _capture_disk_file(
                        output,
                        "disk image output",
                        frozen=existing_copy
                        if output_size == image_bytes
                        else None,
                    )
                reuse = (
                    not force_format
                    and initial_output is not None
                    and initial_output.size == image_bytes
                    and _valid_existing_image(
                        existing_copy, hdd_mb, fat_start_lba
                    )
                )

                checked_template = private / "checked-template.img"
                arguments = (
                    "disk-template",
                    frozen_boot,
                    "--kernel",
                    frozen_kernel,
                    "--image-sectors",
                    str(image_sectors),
                    "--fat-start-lba",
                    str(fat_start_lba),
                    "-o",
                    checked_template,
                )
                try:
                    generated = run_seed_tool(
                        manifest,
                        private,
                        "cupidobj",
                        arguments,
                        timeout=60,
                    )
                except BootstrapError as error:
                    raise DiskImageError(
                        f"checked CupidObj could not run: {error}"
                    ) from error
                if generated.returncode != 0:
                    details = (
                        generated.stderr or generated.stdout or ""
                    ).strip()
                    suffix = f": {details}" if details else ""
                    raise DiskImageError(
                        "checked CupidObj failed with status "
                        f"{generated.returncode}{suffix}"
                    )
                if _disk_is_link_or_junction(
                    checked_template, "checked CupidObj disk template"
                ):
                    raise DiskImageError(
                        "checked CupidObj disk template may not be a "
                        "symbolic link or junction"
                    )
                checked_snapshot = _capture_disk_file(
                    checked_template,
                    "checked CupidObj disk template",
                )
                if checked_snapshot.size != template_length:
                    raise DiskImageError(
                        "checked CupidObj disk template has the wrong size: "
                        f"expected {template_length}, found "
                        f"{checked_snapshot.size}"
                    )

                oracle = private / "python-template.img"
                oracle_length = _write_pristine_disk_template(
                    oracle,
                    frozen_boot,
                    frozen_kernel,
                    image_sectors,
                    fat_start_lba,
                )
                oracle_snapshot = _capture_disk_file(
                    oracle, "Python disk-template oracle"
                )
                if (
                    oracle_length != template_length
                    or oracle_snapshot.size != checked_snapshot.size
                    or oracle_snapshot.sha256 != checked_snapshot.sha256
                ):
                    raise DiskImageError(
                        "checked CupidObj disk template differs from the "
                        "Python oracle"
                    )

                candidate = private / "output" / "image.img"
                candidate.parent.mkdir()
                if reuse:
                    shutil.copyfile(existing_copy, candidate)
                    with checked_template.open("rb") as source, candidate.open(
                        "r+b"
                    ) as destination:
                        _copy_disk_prefix(
                            source, destination, fat_start_bytes
                        )
                else:
                    shutil.copyfile(checked_template, candidate)
                    with candidate.open("r+b") as destination:
                        destination.truncate(image_bytes)

                if frozen_stages:
                    with Fat16Image(candidate, fat_start_lba) as fat:
                        for stage, _, frozen_stage in frozen_stages:
                            fat.write_file(
                                stage.dest,
                                frozen_stage.read_bytes(),
                            )
                with candidate.open("r+b") as stream:
                    stream.flush()
                    os.fsync(stream.fileno())

                for snapshot in seed_snapshots:
                    description = (
                        "checked seed manifest"
                        if snapshot.resolved == manifest
                        else f"checked seed artifact "
                        f"{snapshot.resolved.name}"
                    )
                    _require_disk_file_unchanged(snapshot, description)
                try:
                    current_seed = verify_seed_inputs(manifest)
                except BootstrapError as error:
                    raise DiskImageError(
                        "checked seed inputs changed while authoring the "
                        f"disk image: {error}"
                    ) from error
                if current_seed.manifest_sha256 != checked_seed.manifest_sha256:
                    raise DiskImageError(
                        "checked seed manifest changed while authoring the "
                        "disk image"
                    )
                _require_disk_file_unchanged(
                    boot_snapshot, "bootloader input"
                )
                _require_disk_file_unchanged(
                    kernel_snapshot, "kernel input"
                )
                for _, snapshot, _ in frozen_stages:
                    _require_disk_file_unchanged(snapshot, "stage input")
                for stage in missing_stages:
                    if stage.source.exists() or _disk_is_link_or_junction(
                        stage.source, "missing stage input"
                    ):
                        raise DiskImageError(
                            "missing stage input appeared while authoring "
                            f"the disk image: {stage.source}"
                        )
                _require_disk_output_unchanged(output, initial_output)
                os.replace(candidate, output)
        except DiskImageError:
            raise
        except OSError as error:
            raise DiskImageError(
                f"disk image could not be published: {output}: {error}"
            ) from error
        finally:
            _release_disk_publication_lock(publication_lock)
        action = "Reused" if reuse else "Created"
        suffix = " (preserving FAT data)" if reuse else (
            f" ({hdd_mb}MB FAT16 at LBA {fat_start_lba})"
        )
        print(f"[hostbuild] {action} {image}{suffix}")
        for stage, _, _ in frozen_stages:
            print(
                f"[hostbuild] Staged {stage.source} -> "
                f"{image}:{stage.dest}"
            )
        for stage in missing_stages:
            print(f"[hostbuild] Skipping missing stage file {stage.source}")
    except DiskImageError:
        raise
    except BootstrapError as error:
        raise DiskImageError(f"checked seed could not be used: {error}") from error
    except (OSError, ValueError) as error:
        raise DiskImageError(f"disk image could not be authored: {error}") from error


def _wad_dest(path: Path, index: int) -> str:
    name = path.name.lower()
    if "freedoom1" in name:
        return "/wads/freedo~1.wad"
    if "freedoom2" in name:
        return "/wads/freedo~2.wad"
    if "doom2" in name:
        return "/wads/doom2.wad"
    if "doom1" in name or name == "doom.wad":
        return "/wads/doom.wad"
    return f"/wads/wad{index}.wad"


def stage_files(image: Path, fat_start_lba: int, stages: list[StageFile]) -> None:
    with Fat16Image(image, fat_start_lba) as fat:
        for stage in stages:
            fat.write_file(stage.dest, stage.source.read_bytes())
            print(f"[hostbuild] Staged {stage.source} -> {image}:{stage.dest}")


def stage_wads(image: Path, fat_start_lba: int, wads: list[Path]) -> None:
    if not wads:
        print("[hostbuild] Skipping WAD staging (no WAD files found)")
        return
    stages = [StageFile(path, _wad_dest(path, i + 1)) for i, path in enumerate(wads)]
    stage_files(image, fat_start_lba, stages)


def build_ksyms_blob(symbols: list[tuple[int, str]]) -> bytes:
    filtered = [(addr, name) for addr, name in symbols if name and not name.startswith(".L")]
    filtered.sort(key=lambda item: item[0])

    seen: set[int] = set()
    unique: list[tuple[int, str]] = []
    for addr, name in filtered:
        if addr in seen:
            continue
        seen.add(addr)
        unique.append((addr, name))

    strtab = bytearray()
    offsets: list[int] = []
    for _, name in unique:
        offsets.append(len(strtab))
        strtab.extend(name.encode("utf-8") + b"\0")

    count = len(unique)
    string_off = 16 + count * 8
    total_size = string_off + len(strtab)
    blob = bytearray(struct.pack("<IIII", 0x4D59534B, count, string_off, total_size))
    for (addr, _), name_off in zip(unique, offsets):
        blob.extend(struct.pack("<II", addr, name_off))
    blob.extend(strtab)
    return bytes(blob)


def _pack_ksyms_words(blob: bytes) -> tuple[int, ...]:
    padding = (-len(blob)) % 4
    padded = blob + (b"\0" * padding)
    return tuple(
        struct.unpack_from("<I", padded, offset)[0]
        for offset in range(0, len(padded), 4)
    )


def _parse_nm_symbols(output: str) -> list[tuple[int, str]]:
    symbols: list[tuple[int, str]] = []
    for line_number, line in enumerate(output.splitlines(), 1):
        parts = line.split()
        if not parts:
            continue
        if len(parts) == 2:
            if parts[0] in {"U", "u", "v", "w"}:
                continue
            raise KsymsGenerationError(
                f"symbol reader omitted an address at line {line_number}"
            )
        if len(parts) != 3 or len(parts[1]) != 1:
            raise KsymsGenerationError(
                f"symbol reader emitted a malformed row at line {line_number}"
            )
        addr_s, typ, name = parts
        try:
            address = int(addr_s, 16)
        except ValueError as error:
            raise KsymsGenerationError(
                f"symbol reader emitted an invalid address at line {line_number}"
            ) from error
        if address < 0 or address > 0xFFFFFFFF:
            raise KsymsGenerationError(
                f"symbol reader address is outside i386 at line {line_number}"
            )
        if typ in {"t", "T", "w", "W"} and not name.startswith(".L"):
            symbols.append((address, name))
    return symbols


def _symbols_from_nm(
    nm: str | tuple[str, ...], elf: Path
) -> list[tuple[int, str]]:
    command = (nm,) if isinstance(nm, str) else nm
    try:
        proc = subprocess.run(
            [*command, "-n", str(elf)],
            check=True,
            text=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as error:
        details = (error.stderr or "").strip()
        suffix = f": {details}" if details else ""
        raise KsymsGenerationError(
            f"symbol reader failed with status {error.returncode}{suffix}"
        ) from error
    except OSError as error:
        raise KsymsGenerationError(
            f"symbol reader could not run: {error}"
        ) from error
    return _parse_nm_symbols(proc.stdout)


def _symbol_text_from_seed(
    manifest: Path,
    working_directory: Path,
    elf: Path,
) -> str:
    try:
        proc = run_seed_tool(
            manifest,
            working_directory,
            "cupiddis",
            ("-n", elf),
            timeout=60,
        )
    except BootstrapError as error:
        raise KsymsGenerationError(
            f"checked CupidDis could not run: {error}"
        ) from error
    if proc.returncode != 0:
        details = (proc.stderr or proc.stdout or "").strip()
        suffix = f": {details}" if details else ""
        raise KsymsGenerationError(
            f"checked CupidDis failed with status "
            f"{proc.returncode}{suffix}"
        )
    return proc.stdout


def _render_ksyms_source(blob: bytes) -> bytes:
    words = _pack_ksyms_words(blob)
    lines = [
        "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n",
        '#include "ksyms.h"\n\n',
        "/* i386 words preserve the blob bytes with fewer initializers. */\n",
        "const unsigned int\n",
        '__attribute__((section(".ksyms"), used, aligned(4)))\n',
        "ksym_blob[] = {\n",
    ]
    for i in range(0, len(words), 8):
        chunk = words[i : i + 8]
        lines.append(
            "  " + " ".join(f"0x{word:08x}u," for word in chunk) + "\n"
        )
    lines.extend(
        (
            "};\n\n",
            f"const unsigned int ksym_blob_size = {len(blob)}u;\n",
        )
    )
    return "".join(lines).encode("ascii")


def _resolve_symbol_reader(nm: str) -> Path:
    candidate = Path(nm)
    if not candidate.exists():
        found = shutil.which(nm)
        if found is None:
            raise KsymsGenerationError(
                f"symbol reader is unavailable: {nm}"
            )
        candidate = Path(found)
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as error:
        raise KsymsGenerationError(
            f"symbol reader cannot be resolved: {nm}"
        ) from error
    if not resolved.is_file():
        raise KsymsGenerationError(
            f"symbol reader is not a file: {resolved}"
        )
    return resolved


def _read_ksyms_input(path: Path, description: str) -> bytes:
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise KsymsGenerationError(
            f"{description} cannot be resolved: {path}"
        ) from error
    if not resolved.is_file():
        raise KsymsGenerationError(
            f"{description} is not a file: {resolved}"
        )
    try:
        return resolved.read_bytes()
    except OSError as error:
        raise KsymsGenerationError(
            f"{description} cannot be read: {resolved}: {error}"
        ) from error


def write_ksyms_source(
    nm: str | None,
    elf: Path,
    out: Path,
    *,
    seed_manifest: Path | None = None,
) -> None:
    if (nm is None) == (seed_manifest is None):
        raise KsymsGenerationError(
            "select exactly one symbol reader"
        )
    reader = _resolve_symbol_reader(nm) if nm is not None else None
    try:
        input_elf = elf.resolve(strict=True)
        output_parent = out.parent.resolve(strict=True)
    except OSError as error:
        raise KsymsGenerationError(
            f"kernel symbol path cannot be resolved: {error}"
        ) from error
    output = output_parent / out.name
    if not input_elf.is_file():
        raise KsymsGenerationError(
            f"pass-one kernel is not a file: {input_elf}"
        )
    if output.is_symlink():
        raise KsymsGenerationError(
            f"generated symbol output may not be a symlink: {output}"
        )
    if output.exists() and not output.is_file():
        raise KsymsGenerationError(
            f"generated symbol output is not a file: {output}"
        )
    if output == input_elf or (reader is not None and output == reader):
        raise KsymsGenerationError(
            "generated symbol output may not replace an input"
        )

    elf_payload = _read_ksyms_input(input_elf, "pass-one kernel")
    reader_payload: bytes | None = None
    reader_mode: int | None = None
    manifest: Path | None = None
    manifest_payload: bytes | None = None
    if reader is not None:
        reader_payload = _read_ksyms_input(reader, "symbol reader")
        try:
            reader_mode = reader.stat().st_mode
        except OSError as error:
            raise KsymsGenerationError(
                f"symbol reader metadata cannot be read: {reader}: {error}"
            ) from error
    else:
        assert seed_manifest is not None
        try:
            manifest = seed_manifest.resolve(strict=True)
        except OSError as error:
            raise KsymsGenerationError(
                f"checked seed manifest cannot be resolved: "
                f"{seed_manifest}: {error}"
            ) from error
        if output == manifest:
            raise KsymsGenerationError(
                "generated symbol output may not replace an input"
            )
        manifest_payload = _read_ksyms_input(
            manifest, "checked seed manifest"
        )

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}.mksyms-",
            dir=output_parent,
        ) as temporary:
            frozen_root = Path(temporary)
            frozen_elf = frozen_root / "input" / "kernel.elf.pass1"
            frozen_output = frozen_root / "output" / output.name
            frozen_elf.parent.mkdir()
            frozen_output.parent.mkdir()
            frozen_elf.write_bytes(elf_payload)

            def require_inputs_unchanged() -> None:
                if (
                    _read_ksyms_input(input_elf, "pass-one kernel")
                    != elf_payload
                    or (
                        reader is not None
                        and _read_ksyms_input(reader, "symbol reader")
                        != reader_payload
                    )
                    or (
                        manifest is not None
                        and _read_ksyms_input(
                            manifest, "checked seed manifest"
                        )
                        != manifest_payload
                    )
                ):
                    raise KsymsGenerationError(
                        "kernel symbol inputs changed while generating source"
                    )

            if reader is not None:
                frozen_reader = (
                    frozen_root / "tool" / f"cupiddis{reader.suffix}"
                )
                frozen_reader.parent.mkdir()
                assert reader_payload is not None
                assert reader_mode is not None
                frozen_reader.write_bytes(reader_payload)
                frozen_reader.chmod(reader_mode)
                symbols = _symbols_from_nm(
                    str(frozen_reader), frozen_elf
                )
                symbol_text = None
            else:
                assert manifest is not None
                symbol_text = _symbol_text_from_seed(
                    manifest, frozen_root, frozen_elf
                )
                symbols = _parse_nm_symbols(symbol_text)
            if not symbols:
                raise KsymsGenerationError(
                    "symbol reader reported no kernel text symbols"
                )
            blob = build_ksyms_blob(symbols)
            expected_source = _render_ksyms_source(blob)
            if manifest is None:
                frozen_output.write_bytes(expected_source)
            else:
                assert symbol_text is not None
                frozen_symbols = frozen_root / "input" / "kernel.symbols"
                frozen_symbols.write_bytes(symbol_text.encode("utf-8"))
                require_inputs_unchanged()
                try:
                    generated = run_seed_tool(
                        manifest,
                        frozen_root,
                        "cupidobj",
                        (
                            "ksyms-source",
                            frozen_symbols,
                            "-o",
                            frozen_output,
                        ),
                        timeout=60,
                    )
                except BootstrapError as error:
                    raise KsymsGenerationError(
                        f"checked CupidObj could not run: {error}"
                    ) from error
                if generated.returncode != 0:
                    details = (
                        generated.stderr or generated.stdout or ""
                    ).strip()
                    suffix = f": {details}" if details else ""
                    raise KsymsGenerationError(
                        "checked CupidObj failed with status "
                        f"{generated.returncode}{suffix}"
                    )
                if frozen_output.is_symlink() or not frozen_output.is_file():
                    raise KsymsGenerationError(
                        "checked CupidObj did not produce a regular source file"
                    )
                generated_source = _read_ksyms_input(
                    frozen_output, "checked CupidObj source"
                )
                if generated_source != expected_source:
                    raise KsymsGenerationError(
                        "checked CupidObj output differs from the Python oracle"
                    )

            require_inputs_unchanged()
            os.replace(frozen_output, output)
    except KsymsGenerationError:
        raise
    except OSError as error:
        raise KsymsGenerationError(
            f"generated symbol source could not be published: {error}"
        ) from error
    print(f"[hostbuild] mksyms: {out} ({len(blob)} bytes)")


def _prepare_baseline_jpeg(src: Path, tmp: Path) -> None:
    try:
        payload = src.read_bytes()
    except OSError as error:
        raise EmbedJpegError(
            f"JPEG input cannot be read: {src}: {error}"
        ) from error
    if not payload.startswith(b"\xff\xd8"):
        raise EmbedJpegError("JPEG input has no SOI marker")

    frame_markers = {
        0xC0,
        0xC1,
        0xC2,
        0xC3,
        0xC5,
        0xC6,
        0xC7,
        0xC9,
        0xCA,
        0xCB,
        0xCD,
        0xCE,
        0xCF,
    }
    offset = 2
    frame_marker: int | None = None
    saw_scan = False
    saw_eoi = False
    while offset < len(payload):
        if payload[offset] != 0xFF:
            raise EmbedJpegError(
                "JPEG marker stream is malformed outside a scan"
            )
        while offset < len(payload) and payload[offset] == 0xFF:
            offset += 1
        if offset >= len(payload):
            break
        marker = payload[offset]
        offset += 1
        if marker == 0x00:
            raise EmbedJpegError(
                "JPEG marker stream contains stuffed data before a scan"
            )
        if marker == 0xD9:
            saw_eoi = True
            if offset != len(payload):
                raise EmbedJpegError(
                    "JPEG input has trailing bytes after the EOI marker"
                )
            break
        if marker in {0x01, 0xD8} or 0xD0 <= marker <= 0xD7:
            if marker != 0x01:
                raise EmbedJpegError(
                    f"unexpected standalone JPEG marker 0x{marker:02x}"
                )
            continue
        if offset + 2 > len(payload):
            raise EmbedJpegError("JPEG marker length is truncated")
        segment_size = int.from_bytes(payload[offset : offset + 2], "big")
        if segment_size < 2 or offset + segment_size > len(payload):
            raise EmbedJpegError("JPEG marker length is invalid")
        if marker in frame_markers:
            if frame_marker is not None:
                raise EmbedJpegError(
                    "JPEG input contains more than one frame header"
                )
            if segment_size < 8:
                raise EmbedJpegError("JPEG frame header is truncated")
            component_count = payload[offset + 7]
            if component_count == 0 or segment_size != 8 + 3 * component_count:
                raise EmbedJpegError(
                    "JPEG frame header has an invalid component table"
                )
            if payload[offset + 2] == 0:
                raise EmbedJpegError(
                    "JPEG frame header has an invalid sample precision"
                )
            if (
                int.from_bytes(payload[offset + 3 : offset + 5], "big") == 0
                or int.from_bytes(payload[offset + 5 : offset + 7], "big")
                == 0
            ):
                raise EmbedJpegError(
                    "JPEG frame header has an invalid image size"
                )
            frame_marker = marker
        if marker == 0xDA:
            if frame_marker is None:
                raise EmbedJpegError(
                    "JPEG scan appears before its frame header"
                )
            if segment_size < 6:
                raise EmbedJpegError("JPEG scan header is truncated")
            scan_components = payload[offset + 2]
            if (
                scan_components == 0
                or segment_size != 6 + 2 * scan_components
            ):
                raise EmbedJpegError(
                    "JPEG scan header has an invalid component table"
                )
            saw_scan = True
            offset += segment_size
            while offset < len(payload):
                if payload[offset] != 0xFF:
                    offset += 1
                    continue
                marker_offset = offset
                while offset < len(payload) and payload[offset] == 0xFF:
                    offset += 1
                if offset >= len(payload):
                    raise EmbedJpegError(
                        "JPEG entropy data ends with a partial marker"
                    )
                scan_marker = payload[offset]
                offset += 1
                if scan_marker == 0x00 or 0xD0 <= scan_marker <= 0xD7:
                    continue
                offset = marker_offset
                break
            continue
        offset += segment_size

    if frame_marker == 0xC2:
        raise EmbedJpegError(
            "unsupported progressive JPEG frame; "
            "check in a baseline SOF0/SOF1 asset"
        )
    if frame_marker not in {0xC0, 0xC1}:
        if frame_marker is None:
            raise EmbedJpegError(
                "JPEG input has no supported SOF0/SOF1 frame"
            )
        raise EmbedJpegError(
            f"unsupported JPEG frame marker 0x{frame_marker:02x}; "
            "check in a baseline SOF0/SOF1 asset"
        )
    if not saw_scan:
        raise EmbedJpegError("JPEG input has no scan")
    if not saw_eoi:
        raise EmbedJpegError("JPEG input has no EOI marker")
    try:
        tmp.write_bytes(payload)
    except OSError as error:
        raise EmbedJpegCopyError(
            f"checked JPEG copy cannot be written: {tmp}: {error}"
        ) from error
    print(f"[hostbuild] JPEG baseline embed {src}")


def _embed_jpeg_with_seed(
    seed_manifest: Path,
    src: Path,
    out: Path,
) -> None:
    try:
        manifest = seed_manifest.resolve(strict=True)
        input_jpeg = src.resolve(strict=True)
        output_parent = out.parent.resolve(strict=True)
    except OSError as error:
        raise EmbedJpegError(
            f"checked JPEG path cannot be resolved: {error}"
        ) from error
    output = output_parent / out.name
    if output.is_symlink():
        raise EmbedJpegError(
            f"embedded JPEG output may not be a symlink: {output}"
        )
    if output.exists() and not output.is_file():
        raise EmbedJpegError(
            f"embedded JPEG output is not a file: {output}"
        )
    if not input_jpeg.is_file():
        raise EmbedJpegError(
            f"JPEG input is not a file: {input_jpeg}"
        )
    if output in {manifest, input_jpeg}:
        raise EmbedJpegError(
            "embedded JPEG output may not replace an input"
        )
    try:
        manifest_payload = manifest.read_bytes()
        jpeg_payload = input_jpeg.read_bytes()
    except OSError as error:
        raise EmbedJpegError(
            f"checked JPEG input cannot be read: {error}"
        ) from error

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}.embed-jpeg-",
            dir=output_parent,
        ) as temporary:
            temporary_root = Path(temporary)
            frozen_jpeg = temporary_root / "input.jpg"
            oracle_jpeg = temporary_root / "asset.python-oracle.jpg"
            wrapped = temporary_root / output.name
            frozen_jpeg.write_bytes(jpeg_payload)
            arguments = [
                "wrap-jpeg",
                str(frozen_jpeg),
                "--identity",
                str(src),
                "-o",
                str(wrapped),
            ]
            try:
                proc = run_seed_tool(
                    manifest,
                    Path.cwd(),
                    "cupidobj",
                    arguments,
                    timeout=60,
                )
            except BootstrapError as error:
                raise EmbedJpegError(
                    f"checked CupidObj could not run: {error}"
                ) from error
            if proc.returncode != 0:
                details = (proc.stderr or proc.stdout or "").strip()
                suffix = f": {details}" if details else ""
                raise EmbedJpegError(
                    f"checked CupidObj failed with status "
                    f"{proc.returncode}{suffix}"
                )
            if wrapped.is_symlink() or not wrapped.is_file():
                raise EmbedJpegError(
                    "checked CupidObj reported success without a regular "
                    "object"
                )
            try:
                _prepare_baseline_jpeg(frozen_jpeg, oracle_jpeg)
            except EmbedJpegCopyError as error:
                raise EmbedJpegError(
                    "Python JPEG oracle could not write its private copy: "
                    f"{error}"
                ) from error
            except EmbedJpegError as error:
                raise EmbedJpegError(
                    "checked CupidObj JPEG acceptance differs from the "
                    f"Python oracle: {error}"
                ) from error
            if oracle_jpeg.read_bytes() != jpeg_payload:
                raise EmbedJpegError(
                    "Python JPEG oracle changed the frozen input bytes"
                )
            if (
                manifest.read_bytes() != manifest_payload
                or input_jpeg.read_bytes() != jpeg_payload
            ):
                raise EmbedJpegError(
                    "checked JPEG inputs changed while wrapping the object"
                )
            os.replace(wrapped, output)
    except EmbedJpegError:
        raise
    except OSError as error:
        raise EmbedJpegError(
            f"embedded JPEG object could not be published: {error}"
        ) from error


def embed_jpeg(
    object_tool: str | None,
    src: Path,
    out: Path,
    *,
    seed_manifest: Path | None = None,
) -> None:
    if (object_tool is None) == (seed_manifest is None):
        raise EmbedJpegError(
            "select exactly one JPEG object writer"
        )
    if seed_manifest is not None:
        _embed_jpeg_with_seed(seed_manifest, src, out)
        return

    assert object_tool is not None
    tmp = Path(str(out) + ".baseline.jpg")
    try:
        _prepare_baseline_jpeg(src, tmp)
        subprocess.run(
            [
                object_tool,
                "wrap",
                str(tmp),
                "--identity",
                str(src),
                "-o",
                str(out),
            ],
            check=True,
        )
    finally:
        if tmp.exists():
            tmp.unlink()


def _name_no_ext(path: str | Path) -> str:
    return Path(path).stem


def _c_symbol_part(name: str) -> str:
    return name.replace("-", "_")


def _validate_install_symbols(
    entries: list[tuple[str, str, str]],
) -> None:
    seen: dict[str, list[tuple[str, str]]] = {}
    for symbol, path, category in entries:
        for earlier_path, earlier_category in seen.get(symbol, []):
            shared_docs_asset = (
                path == earlier_path
                and {category, earlier_category} == {"doc", "home"}
            )
            if not shared_docs_asset:
                raise InstallSourceGenerationError(
                    f"{earlier_path} and {path} map to the same binary symbol "
                    f"{symbol}"
                )
        seen.setdefault(symbol, []).append((path, category))


def gen_bin_programs(out: Path, bins: list[str], headers: list[str], browser: list[str]) -> None:
    bin_names = [_name_no_ext(p) for p in bins]
    hdr_names = [_name_no_ext(p) for p in headers]
    browser_names = [_name_no_ext(p) for p in browser]
    _validate_install_symbols(
        [
            (f"_binary_bin_{name}_cc", path, "bin")
            for name, path in zip(bin_names, bins)
        ]
        + [
            (f"_binary_bin_{name}_h", path, "header")
            for name, path in zip(hdr_names, headers)
        ]
        + [
            (f"_binary_bin_browser_{name}_cc", path, "browser")
            for name, path in zip(browser_names, browser)
        ]
    )
    lines = [
        "/* Auto-generated -- do not edit. */",
        "/* Lists all embedded CupidC programs from bin/ directory */",
        '#include "ramfs.h"',
        '#include "types.h"',
        '#include "../drivers/serial.h"',
    ]
    lines += [f"extern const char _binary_bin_{n}_cc_start[];" for n in bin_names]
    lines += [f"extern const char _binary_bin_{n}_h_start[];" for n in hdr_names]
    lines += [f"extern const char _binary_bin_{n}_cc_end[];" for n in bin_names]
    lines += [f"extern const char _binary_bin_{n}_h_end[];" for n in hdr_names]
    lines += [f"extern const char _binary_bin_browser_{n}_cc_start[];" for n in browser_names]
    lines += [f"extern const char _binary_bin_browser_{n}_cc_end[];" for n in browser_names]
    lines += ["void install_bin_programs(void *fs_private);", "void install_bin_programs(void *fs_private) {"]
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_bin_{n}_cc_end - _binary_bin_{n}_cc_start); ramfs_add_file(fs_private, "bin/{n}.cc", _binary_bin_{n}_cc_start, sz); serial_printf("[kernel] Installed /bin/{n}.cc (%u bytes)\\n", sz); }}'
        for n in bin_names
    ]
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_bin_{n}_h_end - _binary_bin_{n}_h_start); ramfs_add_file(fs_private, "bin/{n}.h", _binary_bin_{n}_h_start, sz); serial_printf("[kernel] Installed /bin/{n}.h (%u bytes)\\n", sz); }}'
        for n in hdr_names
    ]
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_bin_browser_{n}_cc_end - _binary_bin_browser_{n}_cc_start); ramfs_add_file(fs_private, "bin/browser/{n}.cc", _binary_bin_browser_{n}_cc_start, sz); serial_printf("[kernel] Installed /bin/browser/{n}.cc (%u bytes)\\n", sz); }}'
        for n in browser_names
    ]
    lines.append("}")
    out.write_text("\n".join(lines) + "\n", newline="\n")


def gen_docs_programs(out: Path, ctxt: list[str], doc_assets: list[str], home_assets: list[str]) -> None:
    ctxt_names = [_name_no_ext(p) for p in ctxt]
    accepted_doc_assets = [
        path for path in doc_assets if Path(path).suffix.lower() == ".bmp"
    ]
    doc_bmps = [_name_no_ext(path) for path in accepted_doc_assets]
    supported_home_extensions = {"bmp", "png", "jpg", "jpeg"}
    accepted_home_assets = [
        path
        for path in home_assets
        if Path(path).suffix.lower()[1:] in supported_home_extensions
    ]
    home_entries = [
        (_name_no_ext(path), Path(path).suffix.lower()[1:])
        for path in accepted_home_assets
    ]
    _validate_install_symbols(
        [
            (
                f"_binary_cupidos_txt_{_c_symbol_part(name)}_CTXT",
                path,
                "ctxt",
            )
            for name, path in zip(ctxt_names, ctxt)
        ]
        + [
            (f"_binary_{_c_symbol_part(name)}_bmp", path, "doc")
            for name, path in zip(doc_bmps, accepted_doc_assets)
        ]
        + [
            (f"_binary_{_c_symbol_part(name)}_{extension}", path, "home")
            for (name, extension), path in zip(
                home_entries, accepted_home_assets
            )
        ]
    )
    lines = [
        "/* Auto-generated -- do not edit. */",
        "/* Lists all embedded CupidDoc files from cupidos-txt/ directory */",
        '#include "homefs.h"',
        '#include "ramfs.h"',
        '#include "types.h"',
        '#include "vfs.h"',
        '#include "../drivers/serial.h"',
    ]
    lines += [f"extern const char _binary_cupidos_txt_{_c_symbol_part(n)}_CTXT_start[];" for n in ctxt_names]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_bmp_start[];" for n in doc_bmps]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_{ext}_start[];" for n, ext in home_entries]
    lines += [f"extern const char _binary_cupidos_txt_{_c_symbol_part(n)}_CTXT_end[];" for n in ctxt_names]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_bmp_end[];" for n in doc_bmps]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_{ext}_end[];" for n, ext in home_entries]
    lines += [
        "static void install_home_asset(const char *path, const char *data, uint32_t size) {",
        "    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);",
        '    if (fd < 0) { serial_printf("[kernel] Failed to open %s (%d)\\n", path, fd); return; }',
        "    uint32_t off = 0;",
        "    while (off < size) {",
        "        int n = vfs_write(fd, data + off, size - off);",
        "        if (n <= 0) break;",
        "        off += (uint32_t)n;",
        "    }",
        "    vfs_close(fd);",
        '    serial_printf("[kernel] Installed %s (%u bytes)\\n", path, off);',
        "}",
        "void install_docs_programs(void *fs_private);",
        "void install_docs_programs(void *fs_private) {",
    ]
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_cupidos_txt_{_c_symbol_part(n)}_CTXT_end - _binary_cupidos_txt_{_c_symbol_part(n)}_CTXT_start); ramfs_add_file(fs_private, "docs/{n}.ctxt", _binary_cupidos_txt_{_c_symbol_part(n)}_CTXT_start, sz); serial_printf("[kernel] Installed /docs/{n}.ctxt (%u bytes)\\n", sz); }}'
        for n in ctxt_names
    ]
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_{_c_symbol_part(n)}_bmp_end - _binary_{_c_symbol_part(n)}_bmp_start); ramfs_add_file(fs_private, "docs/{n}.bmp", _binary_{_c_symbol_part(n)}_bmp_start, sz); serial_printf("[kernel] Installed /docs/{n}.bmp (%u bytes)\\n", sz); }}'
        for n in doc_bmps
    ]
    lines.append("    homefs_seed_begin();")
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_{_c_symbol_part(n)}_{ext}_end - _binary_{_c_symbol_part(n)}_{ext}_start); install_home_asset("/home/{n}.{ext}", _binary_{_c_symbol_part(n)}_{ext}_start, sz); }}'
        for n, ext in home_entries
    ]
    lines += ["    homefs_seed_end();", "}"]
    out.write_text("\n".join(lines) + "\n", newline="\n")


def gen_demos_programs(out: Path, demos: list[str]) -> None:
    names = [_name_no_ext(p) for p in demos]
    _validate_install_symbols(
        [
            (f"_binary_demos_{name}_asm", path, "demo")
            for name, path in zip(names, demos)
        ]
    )
    lines = [
        "/* Auto-generated -- do not edit. */",
        "/* Lists all embedded CupidASM demos from demos/ directory */",
        '#include "ramfs.h"',
        '#include "types.h"',
        '#include "../drivers/serial.h"',
    ]
    lines += [f"extern const char _binary_demos_{n}_asm_start[];" for n in names]
    lines += [f"extern const char _binary_demos_{n}_asm_end[];" for n in names]
    lines += ["void install_demo_programs(void *fs_private);", "void install_demo_programs(void *fs_private) {"]
    lines += [
        f'    {{ uint32_t sz = (uint32_t)(_binary_demos_{n}_asm_end - _binary_demos_{n}_asm_start); ramfs_add_file(fs_private, "demos/{n}.asm", _binary_demos_{n}_asm_start, sz); serial_printf("[kernel] Installed /demos/{n}.asm (%u bytes)\\n", sz); ramfs_add_file(fs_private, "docs/demos/{n}.asm", _binary_demos_{n}_asm_start, sz); serial_printf("[kernel] Installed /docs/demos/{n}.asm (%u bytes)\\n", sz); }}'
        for n in names
    ]
    lines.append("}")
    out.write_text("\n".join(lines) + "\n", newline="\n")


ISO_BLOCK_SIZE = 2048
ISO_VOLUME_ID = b"CUPID_OS_TEST"
_ISO_RECORDING_DATE = b"\x64\x01\x01\x00\x00\x00\x00"
_ISO_VOLUME_DATE = b"2000010100000000\x00"
_ISO_UNSPECIFIED_DATE = b"0000000000000000\x00"
_ISO_SP = b"SP\x07\x01\xbe\xef\x00"
_ISO_ER_ID = b"RRIP_1991A"
_ISO_ER_DESCRIPTION = (
    b"THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX "
    b"FILE SYSTEM SEMANTICS"
)
_ISO_ER_SOURCE = (
    b"PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE "
    b"PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT "
    b"INFORMATION."
)
_ISO_MAX_DATA_LENGTH = 0xFFFFFFFF
_ISO_MAX_DIRECTORY_DEPTH = 8


def _is_link_or_junction(path: Path) -> bool:
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
        attributes = getattr(status, "st_file_attributes", 0)
        return bool(attributes & reparse_point)
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO path cannot be inspected: {path}: {error}"
        ) from error


def _path_is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def _resolve_iso_paths(fixtures: Path, out: Path) -> tuple[Path, Path]:
    if _is_link_or_junction(fixtures):
        raise IsoAuthoringError(
            f"ISO fixture tree may not be a symbolic link or junction: "
            f"{fixtures}"
        )
    try:
        fixture_root = fixtures.resolve(strict=True)
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture tree cannot be resolved: {fixtures}: {error}"
        ) from error
    try:
        mode = fixture_root.lstat().st_mode
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture tree cannot be inspected: "
            f"{fixture_root}: {error}"
        ) from error
    if not stat.S_ISDIR(mode):
        raise IsoAuthoringError(
            f"ISO fixture tree is not a directory: {fixture_root}"
        )

    unresolved_output = out.resolve(strict=False)
    if _path_is_within(unresolved_output, fixture_root):
        raise IsoAuthoringError(
            "ISO output may not be inside the fixture tree"
        )
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        output_parent = out.parent.resolve(strict=True)
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO output directory cannot be prepared: "
            f"{out.parent}: {error}"
        ) from error
    output = output_parent / out.name
    if _path_is_within(output, fixture_root):
        raise IsoAuthoringError(
            "ISO output may not be inside the fixture tree"
        )
    return fixture_root, output


def _read_iso_output(output: Path) -> bytes | None:
    if _is_link_or_junction(output):
        raise IsoAuthoringError(
            f"ISO output may not be a symbolic link or junction: {output}"
        )
    try:
        mode = output.lstat().st_mode
    except FileNotFoundError:
        return None
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO output cannot be inspected: {output}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"ISO output is not a regular file: {output}"
        )
    try:
        return output.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO output cannot be read: {output}: {error}"
        ) from error


def _validate_iso_component(name: str, relative: str) -> None:
    try:
        encoded = name.encode("ascii")
    except UnicodeEncodeError as error:
        raise IsoAuthoringError(
            f"ISO fixture name must be ASCII: {relative}"
        ) from error
    if not encoded or any(byte < 0x20 or byte == 0x7F for byte in encoded):
        raise IsoAuthoringError(
            f"ISO fixture name contains an unsupported character: "
            f"{relative}"
        )
    if re.fullmatch(r"[A-Za-z0-9._-]+", name) is None:
        raise IsoAuthoringError(
            "ISO fixture names may use only portable filename characters "
            f"(letters, digits, dot, underscore, and dash): {relative}"
        )
    if len(encoded) > 127:
        raise IsoAuthoringError(
            "ISO fixture name exceeds the 127-byte guest directory record "
            f"limit: {relative}"
        )


def _snapshot_iso_entry(
    path: Path,
    relative: str,
    directory_depth: int = 1,
) -> _IsoSource:
    if _is_link_or_junction(path):
        raise IsoAuthoringError(
            f"ISO fixture may not be a symbolic link or junction: "
            f"{relative}"
        )
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture cannot be inspected: {relative}: {error}"
        ) from error

    if stat.S_ISDIR(mode):
        if directory_depth > _ISO_MAX_DIRECTORY_DEPTH:
            raise IsoAuthoringError(
                "ISO fixture directory exceeds the ECMA-119 eight-level "
                f"primary hierarchy limit: {relative}"
            )
        try:
            paths = list(path.iterdir())
        except OSError as error:
            raise IsoAuthoringError(
                f"ISO fixture directory cannot be read: "
                f"{relative or '.'}: {error}"
            ) from error
        names: dict[str, str] = {}
        for child in paths:
            child_relative = (
                f"{relative}/{child.name}" if relative else child.name
            )
            _validate_iso_component(child.name, child_relative)
            folded = child.name.lower()
            if folded in names:
                raise IsoAuthoringError(
                    "ISO fixture directory has a case-insensitive name "
                    f"collision: {names[folded]} and {child_relative}"
                )
            names[folded] = child_relative
        children = tuple(
            _snapshot_iso_entry(
                child,
                f"{relative}/{child.name}" if relative else child.name,
                directory_depth + 1,
            )
            for child in sorted(
                paths,
                key=lambda candidate: (
                    candidate.name.lower(),
                    candidate.name,
                ),
            )
        )
        return _IsoSource(
            name=path.name if relative else "",
            relative=relative,
            data=None,
            children=children,
        )

    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"ISO fixture is not a regular file or directory: {relative}"
        )
    try:
        size = path.stat().st_size
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture size cannot be read: {relative}: {error}"
        ) from error
    if size > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            f"ISO fixture exceeds the 32-bit data-length limit: {relative}"
        )
    try:
        data = path.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture cannot be read: {relative}: {error}"
        ) from error
    if len(data) > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            f"ISO fixture exceeds the 32-bit data-length limit: {relative}"
        )
    return _IsoSource(
        name=path.name,
        relative=relative,
        data=data,
        children=(),
    )


def _snapshot_iso_tree(fixtures: Path) -> _IsoSource:
    return _snapshot_iso_entry(fixtures, "")


def _read_iso_manifest(
    manifest: Path,
    fixtures: Path,
) -> _IsoManifest:
    if _is_link_or_junction(manifest):
        raise IsoAuthoringError(
            "ISO fixture manifest may not be a symbolic link or junction: "
            f"{manifest}"
        )
    try:
        resolved = manifest.resolve(strict=True)
        mode = resolved.lstat().st_mode
        payload = resolved.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture manifest cannot be read: {manifest}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"ISO fixture manifest is not a regular file: {resolved}"
        )
    if _path_is_within(resolved, fixtures):
        raise IsoAuthoringError(
            "ISO fixture manifest must be outside the fixture tree"
        )
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError as error:
        raise IsoAuthoringError(
            "ISO fixture manifest must contain ASCII paths"
        ) from error

    entries: dict[str, str] = {}
    for line_number, entry in enumerate(text.splitlines(), 1):
        if not entry:
            raise IsoAuthoringError(
                "ISO fixture manifest contains a blank entry at line "
                f"{line_number}"
            )
        if entry != entry.strip() or any(
            character.isspace() for character in entry
        ):
            raise IsoAuthoringError(
                "ISO fixture manifest paths may not contain whitespace: "
                f"line {line_number}"
            )
        if "\\" in entry:
            raise IsoAuthoringError(
                "ISO fixture manifest paths must use forward slashes: "
                f"line {line_number}"
            )
        normalized = PurePosixPath(entry)
        if (
            normalized.is_absolute()
            or normalized.as_posix() != entry
            or any(part in {".", ".."} for part in normalized.parts)
        ):
            raise IsoAuthoringError(
                "ISO fixture manifest path is not normalized and relative: "
                f"line {line_number}: {entry}"
            )
        for component in normalized.parts:
            _validate_iso_component(component, entry)
        folded = entry.lower()
        if folded in entries:
            raise IsoAuthoringError(
                "ISO fixture manifest has a duplicate or case-insensitive "
                f"collision: {entries[folded]} and {entry}"
            )
        entries[folded] = entry
    if not entries:
        raise IsoAuthoringError("ISO fixture manifest is empty")
    return _IsoManifest(
        path=resolved,
        fixture_root=fixtures,
        payload=payload,
        entries=frozenset(entries.values()),
    )


def _iso_snapshot_paths(snapshot: _IsoSource) -> frozenset[str]:
    paths: set[str] = set()
    pending = list(snapshot.children)
    while pending:
        source = pending.pop()
        paths.add(source.relative)
        pending.extend(source.children)
    return frozenset(paths)


def _validate_iso_manifest(
    snapshot: _IsoSource,
    manifest: _IsoManifest,
) -> None:
    paths = _iso_snapshot_paths(snapshot)
    undeclared = sorted(paths - manifest.entries)
    if undeclared:
        raise IsoAuthoringError(
            "ISO fixture path is not declared in the ISO fixture manifest: "
            f"{undeclared[0]}"
        )
    missing = sorted(manifest.entries - paths)
    if missing:
        raise IsoAuthoringError(
            "ISO fixture manifest entry does not exist: "
            f"{missing[0]}"
        )


def _verify_iso_manifest(manifest: _IsoManifest) -> None:
    try:
        current = _read_iso_manifest(
            manifest.path,
            manifest.fixture_root,
        )
    except IsoAuthoringError as error:
        raise IsoAuthoringError(
            f"ISO fixture manifest changed while authoring: {error}"
        ) from error
    if current.payload != manifest.payload:
        raise IsoAuthoringError(
            "ISO fixture manifest changed while authoring the image"
        )


def _reject_iso_output_alias(
    fixtures: Path,
    snapshot: _IsoSource,
    output: Path,
) -> None:
    if not output.exists():
        return
    pending = [snapshot]
    while pending:
        source = pending.pop()
        if source.is_directory:
            pending.extend(source.children)
            continue
        fixture = fixtures / Path(source.relative)
        try:
            aliases_fixture = os.path.samefile(output, fixture)
        except OSError as error:
            raise IsoAuthoringError(
                f"ISO path identity cannot be checked: "
                f"{source.relative}: {error}"
            ) from error
        if aliases_fixture:
            raise IsoAuthoringError(
                "ISO output may not be a hard link to a fixture: "
                f"{source.relative}"
            )


def _iso_paths_alias(output: Path, input_path: Path) -> bool:
    if output == input_path:
        return True
    if not output.exists() or not input_path.exists():
        return False
    try:
        return os.path.samefile(output, input_path)
    except FileNotFoundError:
        return False
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO path identity cannot be checked: {input_path}: {error}"
        ) from error


def _resolve_checked_iso_seed(seed_manifest: Path):
    if _is_link_or_junction(seed_manifest):
        raise IsoAuthoringError(
            "checked seed manifest may not be a symbolic link or junction: "
            f"{seed_manifest}"
        )
    try:
        manifest = seed_manifest.resolve(strict=True)
        mode = manifest.lstat().st_mode
    except OSError as error:
        raise IsoAuthoringError(
            f"checked seed manifest cannot be resolved: "
            f"{seed_manifest}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"checked seed manifest is not a regular file: {manifest}"
        )
    try:
        checked_seed = verify_seed_inputs(manifest)
    except BootstrapError as error:
        raise IsoAuthoringError(
            f"checked seed could not be verified: {error}"
        ) from error
    return manifest, checked_seed


def _verify_checked_iso_seed(
    manifest: Path,
    expected_manifest_sha256: str,
) -> None:
    try:
        current_seed = verify_seed_inputs(manifest)
    except BootstrapError as error:
        raise IsoAuthoringError(
            "checked seed inputs changed while authoring the ISO image: "
            f"{error}"
        ) from error
    if current_seed.manifest_sha256 != expected_manifest_sha256:
        raise IsoAuthoringError(
            "checked seed inputs changed while authoring the ISO image: "
            "manifest content differs"
        )


def _acquire_iso_publication_lock(output: Path) -> tuple[object, str]:
    try:
        return _acquire_disk_publication_lock(output)
    except DiskImageError as error:
        raise IsoAuthoringError(str(error)) from error


def _iso_snapshot_inventory(snapshot: _IsoSource) -> list[_IsoSource]:
    inventory: list[_IsoSource] = []
    pending = list(snapshot.children)
    while pending:
        source = pending.pop()
        inventory.append(source)
        pending.extend(source.children)
    return sorted(
        inventory,
        key=lambda source: (
            source.relative.lower(),
            source.relative,
        ),
    )


def _write_private_iso_file(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def _run_checked_iso_author(
    *,
    seed_manifest: Path,
    snapshot: _IsoSource,
    manifest: _IsoManifest,
) -> bytes:
    try:
        with tempfile.TemporaryDirectory(
            prefix=".cupid-iso-author-",
        ) as temporary:
            private = Path(temporary)
            inventory = _iso_snapshot_inventory(snapshot)
            native_files: dict[str, Path] = {}
            file_index = 0
            for source in inventory:
                if source.is_directory:
                    continue
                native = private / "inputs" / f"{file_index:04d}.bin"
                _write_private_iso_file(native, source.data or b"")
                native_files[source.relative] = native
                file_index += 1

            frozen_manifest = private / "fixtures.manifest"
            _write_private_iso_file(frozen_manifest, manifest.payload)
            checked_output = private / "checked.iso"
            arguments: list[str | Path] = [
                "iso-fixture",
                frozen_manifest,
            ]
            for source in inventory:
                arguments.extend(
                    ("--directory", source.relative)
                    if source.is_directory
                    else (
                        "--file",
                        source.relative,
                        native_files[source.relative],
                    )
                )
            arguments.extend(("-o", checked_output))
            try:
                generated = run_seed_tool(
                    seed_manifest,
                    private,
                    "cupidobj",
                    arguments,
                    timeout=60,
                )
            except BootstrapError as error:
                raise IsoAuthoringError(
                    f"checked CupidObj could not run: {error}"
                ) from error
            if generated.returncode != 0:
                details = (
                    generated.stderr or generated.stdout or ""
                ).strip()
                suffix = f": {details}" if details else ""
                raise IsoAuthoringError(
                    "checked CupidObj failed with status "
                    f"{generated.returncode}{suffix}"
                )
            checked_image = _read_iso_output(checked_output)
            if checked_image is None:
                raise IsoAuthoringError(
                    "checked CupidObj reported success without an ISO image"
                )
            return checked_image
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"checked CupidObj ISO workspace failed: {error}"
        ) from error


def _iso_sanitize_identifier(value: str) -> str:
    identifier = "".join(
        character if character in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
        else "_"
        for character in value.upper()
    )
    return identifier or "_"


def _iso_identifier_parts(source: _IsoSource) -> tuple[str, str]:
    name = source.name
    if source.is_directory:
        return _iso_sanitize_identifier(name)[:8], ""
    if "." in name and not name.startswith(".") and not name.endswith("."):
        stem, extension = name.rsplit(".", 1)
    else:
        stem, extension = name, ""
    return (
        _iso_sanitize_identifier(stem)[:8],
        _iso_sanitize_identifier(extension)[:3] if extension else "",
    )


def _allocate_iso_identifier(
    source: _IsoSource,
    used: set[bytes],
) -> bytes:
    stem, extension = _iso_identifier_parts(source)
    sequence = 0
    while True:
        candidate_stem = stem
        if sequence:
            suffix = f"_{sequence}"
            if len(suffix) >= 8:
                raise IsoAuthoringError(
                    "ISO identifier collision space is exhausted in "
                    f"{source.relative}"
                )
            candidate_stem = stem[: 8 - len(suffix)] + suffix
        if source.is_directory:
            text = candidate_stem
        else:
            text = f"{candidate_stem}.{extension};1"
        identifier = text.encode("ascii")
        if identifier not in used:
            used.add(identifier)
            return identifier
        sequence += 1


def _build_iso_layout(
    source: _IsoSource,
    parent: _IsoNode | None = None,
    identifier: bytes = b"\x00",
) -> _IsoNode:
    node = _IsoNode(
        source=source,
        parent=parent,
        identifier=identifier,
        children=[],
    )
    used: set[bytes] = set()
    for child_source in source.children:
        child_identifier = _allocate_iso_identifier(child_source, used)
        node.children.append(
            _build_iso_layout(
                child_source,
                parent=node,
                identifier=child_identifier,
            )
        )
    node.children.sort(key=lambda child: child.identifier)
    return node


def _iso_both_u16(value: int) -> bytes:
    return struct.pack("<H", value) + struct.pack(">H", value)


def _iso_both_u32(value: int) -> bytes:
    return struct.pack("<I", value) + struct.pack(">I", value)


def _iso_er_entry() -> bytes:
    length = (
        8
        + len(_ISO_ER_ID)
        + len(_ISO_ER_DESCRIPTION)
        + len(_ISO_ER_SOURCE)
    )
    return (
        b"ER"
        + bytes(
            (
                length,
                1,
                len(_ISO_ER_ID),
                len(_ISO_ER_DESCRIPTION),
                len(_ISO_ER_SOURCE),
                1,
            )
        )
        + _ISO_ER_ID
        + _ISO_ER_DESCRIPTION
        + _ISO_ER_SOURCE
    )


def _iso_nm_entry(name: str, relative: str) -> bytes:
    encoded = name.encode("ascii")
    length = 5 + len(encoded)
    if length > 255:
        raise IsoAuthoringError(
            f"Rock Ridge name is too long for a SUSP entry: {relative}"
        )
    return b"NM" + bytes((length, 1, 0)) + encoded


def _iso_px_entry(node: _IsoNode) -> bytes:
    if node.is_directory:
        mode = 0o040555
        links = 2 + sum(
            1 for child in node.children if child.is_directory
        )
    else:
        mode = 0o100444
        links = 1
    return (
        b"PX"
        + bytes((36, 1))
        + _iso_both_u32(mode)
        + _iso_both_u32(links)
        + _iso_both_u32(0)
        + _iso_both_u32(0)
    )


def _iso_tf_entry() -> bytes:
    return (
        b"TF"
        + bytes((26, 1, 0x0E))
        + _ISO_RECORDING_DATE
        + _ISO_RECORDING_DATE
        + _ISO_RECORDING_DATE
    )


def _iso_ce_entry(extent: int, length: int) -> bytes:
    return (
        b"CE"
        + bytes((28, 1))
        + _iso_both_u32(extent)
        + _iso_both_u32(0)
        + _iso_both_u32(length)
    )


def _iso_directory_record(
    *,
    extent: int,
    size: int,
    identifier: bytes,
    directory: bool,
    susp: bytes,
    label: str,
) -> bytes:
    if len(identifier) > 255:
        raise IsoAuthoringError(
            f"ISO identifier is too long: {label}"
        )
    padding = 1 if len(identifier) % 2 == 0 else 0
    length = 33 + len(identifier) + padding + len(susp)
    terminal_padding = length % 2
    length += terminal_padding
    if length > 255:
        raise IsoAuthoringError(
            f"Rock Ridge directory record is too long: {label}"
        )
    record = bytearray(length)
    record[0] = length
    record[1] = 0
    record[2:10] = _iso_both_u32(extent)
    record[10:18] = _iso_both_u32(size)
    record[18:25] = _ISO_RECORDING_DATE
    record[25] = 0x02 if directory else 0
    record[26] = 0
    record[27] = 0
    record[28:32] = _iso_both_u16(1)
    record[32] = len(identifier)
    record[33 : 33 + len(identifier)] = identifier
    susp_offset = 33 + len(identifier) + padding
    record[susp_offset : susp_offset + len(susp)] = susp
    return bytes(record)


def _iso_pack_directory(records: list[bytes]) -> bytes:
    payload = bytearray()
    for record in records:
        remaining = ISO_BLOCK_SIZE - (len(payload) % ISO_BLOCK_SIZE)
        if len(record) > remaining:
            payload.extend(b"\x00" * remaining)
        payload.extend(record)
    if len(payload) % ISO_BLOCK_SIZE:
        payload.extend(
            b"\x00" * (ISO_BLOCK_SIZE - len(payload) % ISO_BLOCK_SIZE)
        )
    return bytes(payload)


def _iso_directory_payload(
    node: _IsoNode,
    continuation_extent: int,
    continuation_length: int,
) -> bytes:
    if node.parent is None:
        parent = node
        dot_susp = (
            _ISO_SP
            + _iso_px_entry(node)
            + _iso_tf_entry()
            + _iso_ce_entry(
                continuation_extent,
                continuation_length,
            )
        )
    else:
        parent = node.parent
        dot_susp = _iso_px_entry(node) + _iso_tf_entry()
    records = [
        _iso_directory_record(
            extent=node.extent,
            size=node.size,
            identifier=b"\x00",
            directory=True,
            susp=dot_susp,
            label=f"{node.source.relative or '.'}/.",
        ),
        _iso_directory_record(
            extent=parent.extent,
            size=parent.size,
            identifier=b"\x01",
            directory=True,
            susp=_iso_px_entry(parent) + _iso_tf_entry(),
            label=f"{node.source.relative or '.'}/..",
        ),
    ]
    for child in node.children:
        child_size = (
            child.size
            if child.is_directory
            else len(child.source.data or b"")
        )
        records.append(
            _iso_directory_record(
                extent=child.extent,
                size=child_size,
                identifier=child.identifier,
                directory=child.is_directory,
                susp=(
                    _iso_px_entry(child)
                    + _iso_tf_entry()
                    + _iso_nm_entry(
                        child.source.name,
                        child.source.relative,
                    )
                ),
                label=child.source.relative,
            )
        )
    return _iso_pack_directory(records)


def _iso_directory_order(root: _IsoNode) -> list[_IsoNode]:
    ordered = []
    queue = [root]
    while queue:
        directory = queue.pop(0)
        directory.directory_number = len(ordered) + 1
        ordered.append(directory)
        queue.extend(
            child for child in directory.children if child.is_directory
        )
    if len(ordered) > 0xFFFF:
        raise IsoAuthoringError(
            "ISO fixture tree has more than 65,535 directories"
        )
    return ordered


def _iso_file_order(root: _IsoNode) -> list[_IsoNode]:
    files = []
    queue = [root]
    while queue:
        node = queue.pop()
        if node.is_directory:
            queue.extend(reversed(node.children))
        else:
            files.append(node)
    return sorted(
        files,
        key=lambda node: (
            node.source.relative.lower(),
            node.source.relative,
        ),
    )


def _iso_path_table(
    directories: list[_IsoNode],
    byte_order: str,
) -> bytes:
    prefix = "<" if byte_order == "little" else ">"
    table = bytearray()
    for directory in directories:
        identifier = (
            b"\x00" if directory.parent is None else directory.identifier
        )
        parent_number = (
            1
            if directory.parent is None
            else directory.parent.directory_number
        )
        table.extend((len(identifier), 0))
        table.extend(struct.pack(f"{prefix}I", directory.extent))
        table.extend(struct.pack(f"{prefix}H", parent_number))
        table.extend(identifier)
        if len(identifier) % 2:
            table.append(0)
    return bytes(table)


def _iso_write_identifier(
    descriptor: bytearray,
    start: int,
    length: int,
    value: bytes,
) -> None:
    if len(value) > length:
        raise AssertionError("ISO descriptor identifier is too long")
    descriptor[start : start + length] = value.ljust(length, b" ")


def _iso_primary_volume_descriptor(
    *,
    volume_blocks: int,
    path_table_size: int,
    little_path_extent: int,
    big_path_extent: int,
    root: _IsoNode,
) -> bytes:
    descriptor = bytearray(ISO_BLOCK_SIZE)
    descriptor[:7] = b"\x01CD001\x01"
    _iso_write_identifier(descriptor, 8, 32, b"CUPID OS")
    _iso_write_identifier(descriptor, 40, 32, ISO_VOLUME_ID)
    descriptor[80:88] = _iso_both_u32(volume_blocks)
    descriptor[120:124] = _iso_both_u16(1)
    descriptor[124:128] = _iso_both_u16(1)
    descriptor[128:132] = _iso_both_u16(ISO_BLOCK_SIZE)
    descriptor[132:140] = _iso_both_u32(path_table_size)
    descriptor[140:144] = struct.pack("<I", little_path_extent)
    descriptor[144:148] = b"\x00" * 4
    descriptor[148:152] = struct.pack(">I", big_path_extent)
    descriptor[152:156] = b"\x00" * 4
    root_record = _iso_directory_record(
        extent=root.extent,
        size=root.size,
        identifier=b"\x00",
        directory=True,
        susp=b"",
        label="primary volume descriptor root",
    )
    descriptor[156 : 156 + len(root_record)] = root_record
    _iso_write_identifier(descriptor, 190, 128, b"CUPID_OS_TEST_FIXTURE")
    _iso_write_identifier(descriptor, 318, 128, b"CUPID OS")
    _iso_write_identifier(
        descriptor,
        446,
        128,
        b"CUPID OS REPOSITORY HOSTBUILD",
    )
    _iso_write_identifier(
        descriptor,
        574,
        128,
        b"CUPID OS DETERMINISTIC ISO9660 AUTHOR",
    )
    _iso_write_identifier(descriptor, 702, 37, b"")
    _iso_write_identifier(descriptor, 739, 37, b"")
    _iso_write_identifier(descriptor, 776, 37, b"")
    descriptor[813:830] = _ISO_VOLUME_DATE
    descriptor[830:847] = _ISO_VOLUME_DATE
    descriptor[847:864] = _ISO_UNSPECIFIED_DATE
    descriptor[864:881] = _ISO_VOLUME_DATE
    descriptor[881] = 1
    return bytes(descriptor)


def _render_iso_image(snapshot: _IsoSource) -> bytes:
    root = _build_iso_layout(snapshot)
    directories = _iso_directory_order(root)
    files = _iso_file_order(root)
    continuation = _iso_er_entry()

    for directory in directories:
        directory.size = len(
            _iso_directory_payload(
                directory,
                continuation_extent=0,
                continuation_length=len(continuation),
            )
        )

    empty_little_path = _iso_path_table(directories, "little")
    path_table_size = len(empty_little_path)
    if path_table_size > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            "ISO path table exceeds the 32-bit size limit"
        )
    path_table_blocks = _ceil_div(path_table_size, ISO_BLOCK_SIZE)
    little_path_extent = 18
    big_path_extent = little_path_extent + path_table_blocks
    next_extent = big_path_extent + path_table_blocks

    for directory in directories:
        directory.extent = next_extent
        next_extent += _ceil_div(directory.size, ISO_BLOCK_SIZE)
    continuation_extent = next_extent
    next_extent += _ceil_div(
        len(continuation),
        ISO_BLOCK_SIZE,
    )
    for file_node in files:
        data = file_node.source.data or b""
        if data:
            file_node.extent = next_extent
            next_extent += _ceil_div(len(data), ISO_BLOCK_SIZE)
        else:
            file_node.extent = 0
    if next_extent > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            "ISO volume exceeds the 32-bit block-count limit"
        )

    little_path = _iso_path_table(directories, "little")
    big_path = _iso_path_table(directories, "big")
    if len(little_path) != path_table_size or len(big_path) != path_table_size:
        raise AssertionError("ISO path-table layout changed after placement")

    image = bytearray(next_extent * ISO_BLOCK_SIZE)
    descriptor = _iso_primary_volume_descriptor(
        volume_blocks=next_extent,
        path_table_size=path_table_size,
        little_path_extent=little_path_extent,
        big_path_extent=big_path_extent,
        root=root,
    )
    image[
        16 * ISO_BLOCK_SIZE : 17 * ISO_BLOCK_SIZE
    ] = descriptor
    terminator = bytearray(ISO_BLOCK_SIZE)
    terminator[:7] = b"\xffCD001\x01"
    image[
        17 * ISO_BLOCK_SIZE : 18 * ISO_BLOCK_SIZE
    ] = terminator
    little_start = little_path_extent * ISO_BLOCK_SIZE
    image[little_start : little_start + len(little_path)] = little_path
    big_start = big_path_extent * ISO_BLOCK_SIZE
    image[big_start : big_start + len(big_path)] = big_path
    continuation_start = continuation_extent * ISO_BLOCK_SIZE
    image[
        continuation_start : continuation_start + len(continuation)
    ] = continuation

    for directory in directories:
        payload = _iso_directory_payload(
            directory,
            continuation_extent=continuation_extent,
            continuation_length=len(continuation),
        )
        if len(payload) != directory.size:
            raise AssertionError(
                "ISO directory layout changed after placement"
            )
        start = directory.extent * ISO_BLOCK_SIZE
        image[start : start + len(payload)] = payload
    for file_node in files:
        data = file_node.source.data or b""
        if not data:
            continue
        start = file_node.extent * ISO_BLOCK_SIZE
        image[start : start + len(data)] = data
    return bytes(image)


def _verify_iso_snapshot(
    fixtures: Path,
    snapshot: _IsoSource,
) -> None:
    try:
        current = _snapshot_iso_tree(fixtures)
    except IsoAuthoringError as error:
        raise IsoAuthoringError(
            f"ISO fixture tree changed while authoring: {error}"
        ) from error
    if current != snapshot:
        raise IsoAuthoringError(
            "ISO fixture tree changed while authoring the image"
        )


def _publish_iso_image(
    *,
    fixtures: Path,
    snapshot: _IsoSource,
    output: Path,
    initial_output: bytes | None,
    image: bytes,
    manifest: _IsoManifest | None,
    seed_manifest: Path | None = None,
    seed_manifest_sha256: str | None = None,
) -> bool:
    if (seed_manifest is None) != (seed_manifest_sha256 is None):
        raise AssertionError("checked ISO seed evidence is incomplete")
    if initial_output == image:
        _verify_iso_snapshot(fixtures, snapshot)
        if manifest is not None:
            _verify_iso_manifest(manifest)
        if seed_manifest is not None and seed_manifest_sha256 is not None:
            _verify_checked_iso_seed(
                seed_manifest,
                seed_manifest_sha256,
            )
        if _read_iso_output(output) != initial_output:
            raise IsoAuthoringError(
                "ISO output changed while authoring the image"
            )
        return False

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}.iso-",
            dir=output.parent,
        ) as temporary:
            candidate = Path(temporary) / output.name
            with candidate.open("wb") as stream:
                stream.write(image)
                stream.flush()
                os.fsync(stream.fileno())
            _verify_iso_snapshot(fixtures, snapshot)
            if manifest is not None:
                _verify_iso_manifest(manifest)
            if seed_manifest is not None and seed_manifest_sha256 is not None:
                _verify_checked_iso_seed(
                    seed_manifest,
                    seed_manifest_sha256,
                )
            if _read_iso_output(output) != initial_output:
                raise IsoAuthoringError(
                    "ISO output changed while authoring the image"
                )
            os.replace(candidate, output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO image could not be published: {output}: {error}"
        ) from error
    return True


def _gen_big_with_seed(
    seed_manifest: Path,
    source: Path,
    output: Path,
    initial_output: bytes | None,
    expected: bytes,
) -> bool:
    if _is_link_or_junction(source):
        raise IsoAuthoringError(
            f"generated ISO fixture source may not be a symbolic link or "
            f"junction: {source}"
        )
    try:
        manifest = seed_manifest.resolve(strict=True)
        assembly = source.resolve(strict=True)
    except OSError as error:
        raise IsoAuthoringError(
            f"checked big-fixture input cannot be resolved: {error}"
        ) from error
    if not manifest.is_file():
        raise IsoAuthoringError(
            f"checked seed manifest is not a regular file: {manifest}"
        )
    if not assembly.is_file():
        raise IsoAuthoringError(
            f"generated ISO fixture source is not a regular file: {assembly}"
        )
    if output in {manifest, assembly}:
        raise IsoAuthoringError(
            "generated ISO fixture output may not replace an input"
        )
    try:
        manifest_payload = manifest.read_bytes()
        source_payload = assembly.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"checked big-fixture input cannot be read: {error}"
        ) from error

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}.gen-big-",
            dir=output.parent,
        ) as temporary:
            temporary_root = Path(temporary)
            frozen_source = temporary_root / "input" / assembly.name
            candidate = temporary_root / "output" / output.name
            frozen_source.parent.mkdir()
            candidate.parent.mkdir()
            frozen_source.write_bytes(source_payload)
            arguments = [
                "-f",
                "bin",
                str(frozen_source),
                "-o",
                str(candidate),
            ]
            try:
                proc = run_seed_tool(
                    manifest,
                    Path.cwd(),
                    "cupidasm",
                    arguments,
                    timeout=60,
                )
            except BootstrapError as error:
                raise IsoAuthoringError(
                    f"checked CupidASM could not run: {error}"
                ) from error
            if proc.returncode != 0:
                details = (proc.stderr or proc.stdout or "").strip()
                suffix = f": {details}" if details else ""
                raise IsoAuthoringError(
                    f"checked CupidASM failed with status "
                    f"{proc.returncode}{suffix}"
                )
            if _is_link_or_junction(candidate):
                raise IsoAuthoringError(
                    "checked CupidASM output may not be a symbolic link or "
                    "junction"
                )
            try:
                candidate_mode = candidate.lstat().st_mode
            except FileNotFoundError as error:
                raise IsoAuthoringError(
                    "checked CupidASM reported success without big.bin"
                ) from error
            if not stat.S_ISREG(candidate_mode):
                raise IsoAuthoringError(
                    "checked CupidASM big.bin output is not a regular file"
                )
            candidate_payload = candidate.read_bytes()
            if candidate_payload != expected:
                raise IsoAuthoringError(
                    "checked CupidASM big.bin differs from the 4096-byte "
                    "fixture pattern"
                )
            if (
                manifest.read_bytes() != manifest_payload
                or assembly.read_bytes() != source_payload
            ):
                raise IsoAuthoringError(
                    "checked big-fixture inputs changed while CupidASM ran"
                )
            if _read_iso_output(output) != initial_output:
                raise IsoAuthoringError(
                    "generated ISO fixture changed while writing big.bin"
                )
            if initial_output == candidate_payload:
                return False
            os.replace(candidate, output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"generated ISO fixture cannot be published: {output}: {error}"
        ) from error
    return True


def gen_big(
    out: Path,
    *,
    seed_manifest: Path | None = None,
    source: Path | None = None,
) -> None:
    if (seed_manifest is None) != (source is None):
        raise IsoAuthoringError(
            "select both the checked seed manifest and CupidASM source"
        )
    payload = bytes(i & 0xFF for i in range(4096))
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        output = out.parent.resolve(strict=True) / out.name
        initial_output = _read_iso_output(output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"generated ISO fixture cannot be prepared: {out}: {error}"
        ) from error
    if seed_manifest is not None and source is not None:
        changed = _gen_big_with_seed(
            seed_manifest,
            source,
            output,
            initial_output,
            payload,
        )
        action = "Generated" if changed else "Reused"
        print(f"[hostbuild] {action} {out} (4096 bytes)")
        return
    if initial_output == payload:
        print(f"[hostbuild] Reused {out} (4096 bytes)")
        return

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{out.name}.gen-big-",
            dir=output.parent,
        ) as temporary:
            candidate = Path(temporary) / output.name
            with candidate.open("wb") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            if _read_iso_output(output) != initial_output:
                raise IsoAuthoringError(
                    "generated ISO fixture changed while writing big.bin"
                )
            os.replace(candidate, output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"generated ISO fixture cannot be published: {out}: {error}"
        ) from error
    print(f"[hostbuild] Generated {out} (4096 bytes)")


def build_iso(
    fixtures: Path,
    out: Path,
    manifest: Path | None = None,
    *,
    seed_manifest: Path | None = None,
) -> None:
    checked_seed = None
    checked_seed_manifest = None
    if seed_manifest is not None:
        checked_seed_manifest, checked_seed = _resolve_checked_iso_seed(
            seed_manifest
        )
        try:
            requested_output = out.resolve(strict=False)
        except OSError as error:
            raise IsoAuthoringError(
                f"ISO output cannot be resolved: {out}: {error}"
            ) from error
        seed_paths = [
            checked_seed_manifest,
            *checked_seed.tools.values(),
        ]
        if any(
            _iso_paths_alias(requested_output, path)
            for path in seed_paths
        ):
            raise IsoAuthoringError(
                "ISO output may not replace a checked seed input"
            )
        if _path_is_within(
            requested_output,
            checked_seed_manifest.parent,
        ):
            raise IsoAuthoringError(
                "ISO output may not be inside the checked seed directory"
            )
    fixture_root, output = _resolve_iso_paths(fixtures, out)
    try:
        snapshot = _snapshot_iso_tree(fixture_root)
        checked_manifest = (
            _read_iso_manifest(manifest, fixture_root)
            if manifest is not None
            else None
        )
        if checked_manifest is not None:
            _validate_iso_manifest(snapshot, checked_manifest)
            if output.exists() and os.path.samefile(
                checked_manifest.path,
                output,
            ):
                raise IsoAuthoringError(
                    "ISO output may not alias the fixture manifest"
                )
        if checked_seed_manifest is not None:
            if checked_manifest is None:
                raise IsoAuthoringError(
                    "checked CupidObj ISO authoring requires a fixture "
                    "manifest"
                )
            seed_paths = [
                checked_seed_manifest,
                *checked_seed.tools.values(),
            ]
            if any(
                _iso_paths_alias(output, path) for path in seed_paths
            ):
                raise IsoAuthoringError(
                    "ISO output may not replace a checked seed input"
                )
        _reject_iso_output_alias(fixture_root, snapshot, output)
        publication_lock = _acquire_iso_publication_lock(output)
        try:
            initial_output = _read_iso_output(output)
            if checked_seed_manifest is not None:
                checked_image = _run_checked_iso_author(
                    seed_manifest=checked_seed_manifest,
                    snapshot=snapshot,
                    manifest=checked_manifest,
                )
                oracle_image = _render_iso_image(snapshot)
                if checked_image != oracle_image:
                    raise IsoAuthoringError(
                        "checked CupidObj ISO fixture differs from the "
                        "Python oracle"
                    )
                image = checked_image
            else:
                image = _render_iso_image(snapshot)
            changed = _publish_iso_image(
                fixtures=fixture_root,
                snapshot=snapshot,
                output=output,
                initial_output=initial_output,
                image=image,
                manifest=checked_manifest,
                seed_manifest=checked_seed_manifest,
                seed_manifest_sha256=(
                    checked_seed.manifest_sha256
                    if checked_seed is not None
                    else None
                ),
            )
        finally:
            _release_disk_publication_lock(publication_lock)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture image could not be authored: {error}"
        ) from error
    action = "Built" if changed else "Reused"
    print(
        f"[hostbuild] {action} {output} "
        f"({len(image)} bytes, deterministic ISO9660/Rock Ridge)"
    )


def create_usb_image(out: Path, size_mb: int = 32, partition_lba: int = 2048) -> None:
    sectors = size_mb * 1024 * 1024 // SECTOR_SIZE
    if partition_lba >= sectors:
        raise ValueError("USB partition start is beyond image size")
    layout = _choose_layout(sectors - partition_lba)
    with out.open("wb") as f:
        f.truncate(sectors * SECTOR_SIZE)
        mbr = bytearray(SECTOR_SIZE)
        off = 446
        mbr[off] = 0x80
        mbr[off + 1 : off + 4] = b"\x00\x01\x00"
        mbr[off + 4] = 0x06
        mbr[off + 5 : off + 8] = b"\xfe\xff\xff"
        struct.pack_into("<I", mbr, off + 8, partition_lba)
        struct.pack_into("<I", mbr, off + 12, sectors - partition_lba)
        mbr[510:512] = b"\x55\xaa"
        f.seek(0)
        f.write(mbr)
        _write_fat16_filesystem(f, partition_lba, layout)
    print(f"[hostbuild] Built {out} ({size_mb}MB FAT16 USB image)")


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


def _code_inputs_from_manifest(path: Path) -> list[str]:
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise CodeValidationError(
            f"frozen code input manifest cannot be read: {error}"
        ) from error
    if not payload:
        raise CodeValidationError("code input manifest may not be empty")
    if not payload.endswith(b"\n"):
        raise CodeValidationError("code input manifest must end with a newline")
    if b"\r" in payload:
        raise CodeValidationError("code input manifest must use LF newlines")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CodeValidationError(
            "code input manifest must contain valid UTF-8"
        ) from error

    logical_paths: list[str] = []
    seen: set[str] = set()
    for line_number, line in enumerate(text[:-1].split("\n"), start=1):
        if not line:
            raise CodeValidationError(
                f"code input manifest line {line_number} is blank"
            )
        if line.startswith("#"):
            raise CodeValidationError(
                f"code input manifest line {line_number} may not be a comment"
            )
        if "\\" in line:
            raise CodeValidationError(
                f"code input manifest line {line_number} must use forward slashes"
            )
        if any(character.isspace() for character in line):
            raise CodeValidationError(
                f"code input manifest line {line_number} may not contain whitespace"
            )
        has_control = any(
            ord(character) < 32 or ord(character) == 127 for character in line
        )
        if has_control:
            raise CodeValidationError(
                f"code input manifest line {line_number} may not contain "
                "control characters"
            )
        logical = _code_logical_path(Path(line))
        if logical != line:
            raise CodeValidationError(
                f"code input manifest line {line_number} is not a "
                "canonical repository path"
            )
        key = logical.casefold()
        if key in seen:
            raise CodeValidationError(f"code input is listed more than once: {logical}")
        seen.add(key)
        logical_paths.append(logical)
    return logical_paths


def _seed_manifest_logical_path(root: Path, manifest: Path) -> str:
    if not manifest.is_absolute() and not PureWindowsPath(str(manifest)).is_absolute():
        return _code_logical_path(manifest, subject="checked seed manifest")
    absolute = Path(os.path.abspath(manifest))
    try:
        relative = absolute.relative_to(root)
    except ValueError as error:
        raise CodeValidationError(
            "checked seed manifest must be inside the repository root"
        ) from error
    return _code_logical_path(relative, subject="checked seed manifest")


def _seed_artifact_logical_paths(manifest: Path, manifest_logical: str) -> list[str]:
    try:
        payload = json.loads(manifest.read_text(encoding="utf-8"))
        artifacts = payload["artifacts"]
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise CodeValidationError(
            f"checked seed manifest cannot be read: {error}"
        ) from error
    if not isinstance(artifacts, list):
        raise CodeValidationError("checked seed manifest artifacts must be a list")
    parent = PurePosixPath(manifest_logical).parent
    logical_paths: list[str] = []
    seen: set[str] = set()
    for index, artifact in enumerate(artifacts):
        if not isinstance(artifact, dict) or not isinstance(artifact.get("file"), str):
            raise CodeValidationError(
                f"checked seed manifest artifact {index} has no file name"
            )
        file_name = artifact["file"]
        if PurePosixPath(file_name).name != file_name or "\\" in file_name:
            raise CodeValidationError(
                f"checked seed manifest artifact {index} has an unsafe file name"
            )
        logical = _code_logical_path(
            Path((parent / file_name).as_posix()),
            subject="checked seed artifact",
        )
        key = logical.casefold() if os.name == "nt" else logical
        if key in seen:
            raise CodeValidationError(
                f"checked seed artifact is listed more than once: {file_name}"
            )
        seen.add(key)
        logical_paths.append(logical)
    return logical_paths


def _freeze_code_seed_inputs(
    root: Path,
    seed_manifest: Path,
    private_root: Path,
) -> tuple[object, Path, list[_CodeValidationInput]]:
    manifest_logical = _seed_manifest_logical_path(root, seed_manifest)
    live_manifest = root.joinpath(*PurePosixPath(manifest_logical).parts)
    seed_source = private_root / ".checked-seed-source"
    frozen_manifest = seed_source.joinpath(*PurePosixPath(manifest_logical).parts)
    snapshots = [
        _capture_code_input(
            root,
            manifest_logical,
            frozen=frozen_manifest,
            subject="checked seed manifest",
        )
    ]
    for logical in _seed_artifact_logical_paths(frozen_manifest, manifest_logical):
        snapshots.append(
            _capture_code_input(
                root,
                logical,
                frozen=seed_source.joinpath(*PurePosixPath(logical).parts),
                subject="checked seed artifact",
            )
        )
    try:
        frozen_seed = freeze_seed_inputs(
            frozen_manifest,
            private_root / ".checked-seed",
        )
    except (BootstrapError, OSError) as error:
        raise CodeValidationError(f"checked seed could not be frozen: {error}") from error
    return frozen_seed, live_manifest, snapshots


def _require_code_output_not_an_input(
    output: _CodeOutput,
    output_snapshot: _CodeValidationInput | None,
    inputs: list[_CodeValidationInput],
) -> None:
    output_key = output.logical.casefold() if os.name == "nt" else output.logical
    for input_snapshot in inputs:
        input_key = (
            input_snapshot.logical.casefold()
            if os.name == "nt"
            else input_snapshot.logical
        )
        if output_key == input_key:
            raise CodeValidationError("code output may not replace an input")
        if output_snapshot is not None and (
            output_snapshot.device,
            output_snapshot.inode,
        ) == (input_snapshot.device, input_snapshot.inode):
            raise CodeValidationError("code output may not replace an input")


def _require_code_inputs_unchanged(
    root: Path,
    manifest_snapshot: _CodeValidationInput | None,
    snapshots: list[_CodeValidationInput],
    *,
    activity: str,
    tool_stderr: str,
) -> None:
    if manifest_snapshot is not None:
        try:
            current_manifest = _capture_code_input(
                root,
                manifest_snapshot.logical,
                subject="code input manifest",
            )
        except CodeValidationError as error:
            raise CodeValidationError(
                f"code input manifest changed while {activity} ran: "
                f"{manifest_snapshot.logical}",
                tool_stderr=tool_stderr,
            ) from error
        if (
            current_manifest.size != manifest_snapshot.size
            or current_manifest.sha256 != manifest_snapshot.sha256
        ):
            raise CodeValidationError(
                f"code input manifest changed while {activity} ran: "
                f"{manifest_snapshot.logical}",
                tool_stderr=tool_stderr,
            )
    for snapshot in snapshots:
        try:
            current = _capture_code_input(root, snapshot.logical)
        except CodeValidationError as error:
            raise CodeValidationError(
                f"code input changed while {activity} ran: {snapshot.logical}",
                tool_stderr=tool_stderr,
            ) from error
        if current.size != snapshot.size or current.sha256 != snapshot.sha256:
            raise CodeValidationError(
                f"code input changed while {activity} ran: {snapshot.logical}",
                tool_stderr=tool_stderr,
            )


def _require_code_seed_inputs_unchanged(
    root: Path,
    snapshots: list[_CodeValidationInput],
    *,
    tool_stderr: str,
) -> None:
    for index, snapshot in enumerate(snapshots):
        subject = "checked seed manifest" if index == 0 else "checked seed artifact"
        try:
            current = _capture_code_input(root, snapshot.logical, subject=subject)
        except CodeValidationError as error:
            raise CodeValidationError(
                f"checked seed inputs changed while checked tools ran: "
                f"{snapshot.logical}",
                tool_stderr=tool_stderr,
            ) from error
        if current.size != snapshot.size or current.sha256 != snapshot.sha256:
            raise CodeValidationError(
                f"checked seed inputs changed while checked tools ran: "
                f"{snapshot.logical}",
                tool_stderr=tool_stderr,
            )


def _require_code_output_unchanged(
    output: _CodeOutput,
    initial: _CodeValidationInput | None,
) -> None:
    current = _code_output_entry(output)
    if current is None:
        if initial is not None:
            raise CodeValidationError("code output changed while checked tools ran")
        return
    if initial is None:
        raise CodeValidationError("code output appeared while checked tools ran")
    if current.size != initial.size or current.sha256 != initial.sha256:
        raise CodeValidationError("code output changed while checked tools ran")


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


def _publish_code_output(candidate: _CodeOutput, output: _CodeOutput) -> None:
    candidate_name = PurePosixPath(candidate.logical).name
    output_name = PurePosixPath(output.logical).name
    if candidate.parent_fd is not None and output.parent_fd is not None:
        os.replace(
            candidate_name,
            output_name,
            src_dir_fd=candidate.parent_fd,
            dst_dir_fd=output.parent_fd,
        )
        return
    if candidate.parent_handle is not None and output.parent_handle is not None:
        _rename_windows_code_output(candidate, output)
        return
    raise CodeValidationError("code output parent was not pinned")


@dataclass(frozen=True)
class _CheckedAssemblyTransaction:
    _repository_root: Path
    _private_root: Path
    _stack: ExitStack
    _source_snapshot: _CodeValidationInput
    _frozen_seed: object
    _live_seed_manifest: Path
    _seed_snapshots: list[_CodeValidationInput]
    _pinned_output: _CodeOutput
    _initial_output: _CodeValidationInput | None
    candidate_logical: str
    _candidate_output: _CodeOutput

    def pin_private_output(self, logical: str) -> _CodeOutput:
        return _pin_code_output(self._private_root, Path(logical), self._stack)

    def run_tool(
        self,
        tool_name: str,
        arguments: tuple[str, ...],
    ) -> subprocess.CompletedProcess[str]:
        return run_seed_tool(
            self._live_seed_manifest,
            self._private_root,
            tool_name,
            arguments,
            timeout=60,
            frozen_seed=self._frozen_seed,
        )

    def candidate_snapshot(self) -> _CodeValidationInput | None:
        return _code_output_entry(self._candidate_output)

    def candidate_bytes(
        self,
        expected: _CodeValidationInput,
        *,
        activity: str,
        tool_stderr: str,
    ) -> bytes:
        try:
            payload = self._candidate_output.path.read_bytes()
        except OSError as error:
            raise CodeValidationError(
                "checked CupidASM output cannot be read: "
                f"{self._candidate_output.logical}: {error}",
                tool_stderr=tool_stderr,
            ) from error
        if (
            len(payload) != expected.size
            or hashlib.sha256(payload).hexdigest() != expected.sha256
        ):
            raise CodeValidationError(
                f"checked CupidASM output changed while {activity} ran",
                tool_stderr=tool_stderr,
            )
        return payload

    def require_inputs_unchanged(
        self,
        *,
        activity: str,
        tool_stderr: str,
    ) -> None:
        _require_code_inputs_unchanged(
            self._repository_root,
            None,
            [self._source_snapshot],
            activity=activity,
            tool_stderr=tool_stderr,
        )
        _require_code_seed_inputs_unchanged(
            self._repository_root,
            self._seed_snapshots,
            tool_stderr=tool_stderr,
        )

    def require_private_output_unchanged(
        self,
        output: _CodeOutput,
        expected: _CodeValidationInput,
        *,
        description: str,
        activity: str = "CupidDis",
        tool_stderr: str,
    ) -> None:
        current = _code_output_entry(output)
        if (
            current is None
            or current.size != expected.size
            or current.sha256 != expected.sha256
        ):
            raise CodeValidationError(
                f"{description} changed while {activity} ran",
                tool_stderr=tool_stderr,
            )

    def require_candidate_unchanged(
        self,
        expected: _CodeValidationInput,
        *,
        activity: str = "CupidDis",
        tool_stderr: str,
    ) -> None:
        self.require_private_output_unchanged(
            self._candidate_output,
            expected,
            description="checked CupidASM output",
            activity=activity,
            tool_stderr=tool_stderr,
        )

    def require_publication_boundary_unchanged(self, tool_stderr: str) -> None:
        try:
            _require_code_output_parent_unchanged(self._pinned_output)
            _require_code_output_unchanged(
                self._pinned_output,
                self._initial_output,
            )
        except CodeValidationError as error:
            raise CodeValidationError(
                str(error),
                tool_stderr=tool_stderr,
            ) from error

    def publish(self) -> None:
        _publish_code_output(self._candidate_output, self._pinned_output)


@contextmanager
def _checked_assembly_transaction(
    seed_manifest: Path,
    repository_root: Path,
    source_logical: str,
    output: Path,
    *,
    source_subject: str,
    temporary_prefix: str,
) -> Iterator[_CheckedAssemblyTransaction]:
    with ExitStack() as stack:
        pinned_output = _pin_code_output(repository_root, output, stack)
        try:
            publication_lock = _acquire_disk_publication_lock(
                pinned_output.path
            )
        except DiskImageError as error:
            raise CodeValidationError(str(error)) from error
        stack.callback(_release_disk_publication_lock, publication_lock)
        temporary = stack.enter_context(
            tempfile.TemporaryDirectory(
                prefix=temporary_prefix,
                dir=repository_root,
            )
        )
        private_root = Path(temporary)
        source_snapshot = _capture_code_input(
            repository_root,
            source_logical,
            frozen=private_root.joinpath(*PurePosixPath(source_logical).parts),
            subject=source_subject,
        )
        initial_output = _code_output_entry(pinned_output)
        _require_code_output_not_an_input(
            pinned_output,
            initial_output,
            [source_snapshot],
        )
        frozen_seed, live_seed_manifest, seed_snapshots = (
            _freeze_code_seed_inputs(
                repository_root,
                seed_manifest,
                private_root,
            )
        )
        _require_code_output_not_an_input(
            pinned_output,
            initial_output,
            seed_snapshots,
        )
        candidate_logical = (
            ".cupid-output/" + PurePosixPath(pinned_output.logical).name
        )
        candidate_path = private_root.joinpath(
            *PurePosixPath(candidate_logical).parts
        )
        candidate_path.parent.mkdir()
        candidate_output = _pin_code_output(
            private_root,
            Path(candidate_logical),
            stack,
        )
        yield _CheckedAssemblyTransaction(
            _repository_root=repository_root,
            _private_root=private_root,
            _stack=stack,
            _source_snapshot=source_snapshot,
            _frozen_seed=frozen_seed,
            _live_seed_manifest=live_seed_manifest,
            _seed_snapshots=seed_snapshots,
            _pinned_output=pinned_output,
            _initial_output=initial_output,
            candidate_logical=candidate_logical,
            _candidate_output=candidate_output,
        )


def validate_code(
    seed_manifest: Path,
    root: Path,
    inputs: list[Path] | None = None,
    *,
    input_manifest: Path | None = None,
    output: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        repository_root = root.resolve(strict=True)
    except OSError as error:
        raise CodeValidationError(
            f"repository root cannot be resolved: {root}: {error}"
        ) from error
    if not repository_root.is_dir():
        raise CodeValidationError(
            f"repository root is not a directory: {repository_root}"
        )
    seed_manifest_logical = _seed_manifest_logical_path(
        repository_root,
        seed_manifest,
    )
    live_seed_manifest = repository_root.joinpath(
        *PurePosixPath(seed_manifest_logical).parts
    )
    selected_inputs = list(inputs or [])
    if input_manifest is None and not selected_inputs:
        raise CodeValidationError("select code inputs or one code input manifest")
    if input_manifest is not None and selected_inputs:
        raise CodeValidationError(
            "code inputs and --input-manifest may not be combined"
        )

    try:
        with ExitStack() as stack:
            pinned_output = None
            temporary_parent = None
            if output is not None:
                pinned_output = _pin_code_output(repository_root, output, stack)
                try:
                    publication_lock = _acquire_disk_publication_lock(
                        pinned_output.path
                    )
                except DiskImageError as error:
                    raise CodeValidationError(str(error)) from error
                stack.callback(_release_disk_publication_lock, publication_lock)
                temporary_parent = pinned_output.path.parent
            temporary = stack.enter_context(
                tempfile.TemporaryDirectory(
                    prefix=".cupid-code-validation-",
                    dir=temporary_parent,
                )
            )
            private_root = Path(temporary)
            manifest_snapshot = None
            if input_manifest is None:
                logical_paths = []
                seen: set[str] = set()
                for path in selected_inputs:
                    logical = _code_logical_path(path)
                    key = logical.casefold() if os.name == "nt" else logical
                    if key in seen:
                        raise CodeValidationError(
                            f"code input is listed more than once: {logical}"
                        )
                    seen.add(key)
                    logical_paths.append(logical)
            else:
                manifest_logical = _code_logical_path(
                    input_manifest, subject="code input manifest"
                )
                frozen_manifest = private_root.joinpath(
                    *PurePosixPath(manifest_logical).parts
                )
                manifest_snapshot = _capture_code_input(
                    repository_root,
                    manifest_logical,
                    frozen=frozen_manifest,
                    subject="code input manifest",
                )
                logical_paths = _code_inputs_from_manifest(frozen_manifest)
            snapshots = [
                _capture_code_input(
                    repository_root,
                    logical,
                    frozen=private_root.joinpath(*PurePosixPath(logical).parts),
                )
                for logical in logical_paths
            ]
            initial_output = None
            frozen_seed = None
            seed_snapshots: list[_CodeValidationInput] = []
            candidate_output = None
            if pinned_output is not None:
                linked_kernel_inputs = (
                    "kernel/kernel.elf.pass1",
                    "kernel/kernel.elf",
                )
                missing_linked_inputs = [
                    logical
                    for logical in linked_kernel_inputs
                    if logical not in logical_paths
                ]
                if missing_linked_inputs:
                    raise CodeValidationError(
                        "code publication requires the linked kernel inputs "
                        "in the validated input cohort: "
                        + ", ".join(missing_linked_inputs)
                    )
                initial_output = _code_output_entry(pinned_output)
                protected_inputs = list(snapshots)
                if manifest_snapshot is not None:
                    protected_inputs.append(manifest_snapshot)
                _require_code_output_not_an_input(
                    pinned_output,
                    initial_output,
                    protected_inputs,
                )
                (
                    frozen_seed,
                    live_seed_manifest,
                    seed_snapshots,
                ) = _freeze_code_seed_inputs(
                    repository_root,
                    seed_manifest,
                    private_root,
                )
                _require_code_output_not_an_input(
                    pinned_output,
                    initial_output,
                    seed_snapshots,
                )
                candidate_logical = (
                    ".cupid-output/"
                    + PurePosixPath(pinned_output.logical).name
                )
                candidate_path = private_root.joinpath(
                    *PurePosixPath(candidate_logical).parts
                )
                candidate_path.parent.mkdir()
                candidate_output = _pin_code_output(
                    private_root,
                    Path(candidate_logical),
                    stack,
                )
            try:
                result = run_seed_tool(
                    live_seed_manifest,
                    private_root,
                    "cupiddis",
                    ("--require-known", *logical_paths),
                    timeout=300,
                    **({"frozen_seed": frozen_seed} if frozen_seed is not None else {}),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidDis could not run: {error}"
                ) from error

            tool_stderr = result.stderr or ""
            if result.stdout or result.returncode != 0 or output is None:
                _require_code_inputs_unchanged(
                    repository_root,
                    manifest_snapshot,
                    snapshots,
                    activity="CupidDis",
                    tool_stderr=tool_stderr,
                )
                if result.stdout:
                    raise CodeValidationError(
                        "checked CupidDis wrote unexpected standard output",
                        tool_stderr=tool_stderr,
                    )
                return result

            assert candidate_output is not None
            assert frozen_seed is not None
            try:
                linked_validation = run_seed_tool(
                    live_seed_manifest,
                    private_root,
                    "cupiddis",
                    (
                        "--require-known",
                        "--require-local-targets",
                        "--require-code-anchors",
                        "kernel/kernel.elf.pass1",
                        "kernel/kernel.elf",
                    ),
                    timeout=600,
                    frozen_seed=frozen_seed,
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    "checked CupidDis linked-code validation could not run: "
                    f"{error}",
                    tool_stderr=tool_stderr,
                ) from error
            linked_stderr = tool_stderr + (linked_validation.stderr or "")
            _require_code_inputs_unchanged(
                repository_root,
                manifest_snapshot,
                snapshots,
                activity="CupidDis linked-code validation",
                tool_stderr=linked_stderr,
            )
            _require_code_seed_inputs_unchanged(
                repository_root,
                seed_snapshots,
                tool_stderr=linked_stderr,
            )
            if linked_validation.stdout:
                raise CodeValidationError(
                    "checked CupidDis linked-code validation wrote unexpected "
                    "standard output",
                    tool_stderr=linked_stderr,
                )
            if linked_validation.returncode != 0:
                return subprocess.CompletedProcess(
                    linked_validation.args,
                    linked_validation.returncode,
                    "",
                    linked_stderr,
                )
            try:
                flattened = run_seed_tool(
                    live_seed_manifest,
                    private_root,
                    "cupidobj",
                    (
                        "flat",
                        "kernel/kernel.elf",
                        "-o",
                        candidate_output.logical,
                    ),
                    timeout=300,
                    frozen_seed=frozen_seed,
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidObj could not run: {error}",
                    tool_stderr=linked_stderr,
                ) from error
            combined_stderr = linked_stderr + (flattened.stderr or "")
            _require_code_inputs_unchanged(
                repository_root,
                manifest_snapshot,
                snapshots,
                activity="checked tools",
                tool_stderr=combined_stderr,
            )
            _require_code_seed_inputs_unchanged(
                repository_root,
                seed_snapshots,
                tool_stderr=combined_stderr,
            )
            if flattened.stdout:
                raise CodeValidationError(
                    "checked CupidObj wrote unexpected standard output",
                    tool_stderr=combined_stderr,
                )
            if flattened.returncode != 0:
                return subprocess.CompletedProcess(
                    flattened.args,
                    flattened.returncode,
                    "",
                    combined_stderr,
                )
            candidate_snapshot = _code_output_entry(candidate_output)
            if candidate_snapshot is None:
                raise CodeValidationError(
                    "checked CupidObj output does not exist: "
                    f"{candidate_output.logical}",
                    tool_stderr=combined_stderr,
                )
            assert pinned_output is not None
            try:
                _require_code_output_parent_unchanged(pinned_output)
                _require_code_output_unchanged(pinned_output, initial_output)
            except CodeValidationError as error:
                raise CodeValidationError(
                    str(error), tool_stderr=combined_stderr
                ) from error
            try:
                _publish_code_output(candidate_output, pinned_output)
            except OSError as error:
                raise CodeValidationError(
                    f"validated kernel could not be published: {error}",
                    tool_stderr=combined_stderr,
                ) from error
            return subprocess.CompletedProcess(
                flattened.args,
                0,
                "",
                combined_stderr,
            )
    except CodeValidationError:
        raise
    except OSError as error:
        raise CodeValidationError(
            f"private code snapshot could not be created: {error}"
        ) from error


def assemble_cupidasm_object(
    seed_manifest: Path,
    root: Path,
    source: Path,
    output: Path,
) -> subprocess.CompletedProcess[str]:
    try:
        try:
            from tools.cupidc_kernel_compile import (
                KernelCompileError,
                validate_i386_relocatable_bytes,
            )
        except ModuleNotFoundError:
            from cupidc_kernel_compile import (
                KernelCompileError,
                validate_i386_relocatable_bytes,
            )
    except (ImportError, SyntaxError) as error:
        raise CodeValidationError(
            f"CupidASM object validator could not be loaded: {error}"
        ) from error

    try:
        repository_root = root.resolve(strict=True)
    except OSError as error:
        raise CodeValidationError(
            f"repository root cannot be resolved: {root}: {error}"
        ) from error
    if not repository_root.is_dir():
        raise CodeValidationError(
            f"repository root is not a directory: {repository_root}"
        )
    source_logical = _code_logical_path(
        source,
        subject="CupidASM object source",
    )

    try:
        with _checked_assembly_transaction(
            seed_manifest,
            repository_root,
            source_logical,
            output,
            source_subject="CupidASM object source",
            temporary_prefix=".cupidasm-object-",
        ) as transaction:
            candidate_logical = transaction.candidate_logical
            try:
                assembled = transaction.run_tool(
                    "cupidasm",
                    (
                        "-f",
                        "elf32",
                        "-o",
                        candidate_logical,
                        source_logical,
                    ),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidASM could not run: {error}"
                ) from error
            tool_stderr = assembled.stderr or ""
            transaction.require_inputs_unchanged(
                activity="CupidASM",
                tool_stderr=tool_stderr,
            )
            if assembled.returncode != 0:
                return subprocess.CompletedProcess(
                    assembled.args,
                    assembled.returncode,
                    "",
                    tool_stderr,
                )
            if assembled.stdout:
                raise CodeValidationError(
                    "checked CupidASM wrote unexpected standard output",
                    tool_stderr=tool_stderr,
                )
            if tool_stderr:
                raise CodeValidationError(
                    "checked CupidASM wrote unexpected standard error",
                    tool_stderr=tool_stderr,
                )
            candidate_snapshot = transaction.candidate_snapshot()
            if candidate_snapshot is None:
                raise CodeValidationError(
                    "checked CupidASM output does not exist: "
                    f"{candidate_logical}",
                    tool_stderr=tool_stderr,
                )

            candidate_bytes = transaction.candidate_bytes(
                candidate_snapshot,
                activity="structural validation",
                tool_stderr=tool_stderr,
            )
            try:
                validate_i386_relocatable_bytes(
                    candidate_bytes,
                    require_executable=True,
                )
            except KernelCompileError as error:
                raise CodeValidationError(
                    "checked CupidASM relocatable object validation failed: "
                    f"{error}",
                    tool_stderr=tool_stderr,
                ) from error
            transaction.require_candidate_unchanged(
                candidate_snapshot,
                activity="structural validation",
                tool_stderr=tool_stderr,
            )

            try:
                disassembled = transaction.run_tool(
                    "cupiddis",
                    (
                        "--require-known",
                        "--require-local-targets",
                        candidate_logical,
                    ),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidDis could not run: {error}",
                    tool_stderr=tool_stderr,
                ) from error
            combined_stderr = tool_stderr + (disassembled.stderr or "")
            transaction.require_inputs_unchanged(
                activity="checked tools",
                tool_stderr=combined_stderr,
            )
            transaction.require_candidate_unchanged(
                candidate_snapshot,
                tool_stderr=combined_stderr,
            )
            transaction.require_publication_boundary_unchanged(combined_stderr)
            if disassembled.returncode != 0:
                return subprocess.CompletedProcess(
                    disassembled.args,
                    disassembled.returncode,
                    "",
                    combined_stderr,
                )
            if disassembled.stdout:
                raise CodeValidationError(
                    "checked CupidDis wrote unexpected standard output",
                    tool_stderr=combined_stderr,
                )
            if disassembled.stderr:
                raise CodeValidationError(
                    "checked CupidDis wrote unexpected standard error",
                    tool_stderr=combined_stderr,
                )
            try:
                transaction.publish()
            except OSError as error:
                raise CodeValidationError(
                    "validated CupidASM object could not be published: "
                    f"{error}",
                    tool_stderr=combined_stderr,
                ) from error
            return subprocess.CompletedProcess(
                disassembled.args,
                0,
                "",
                combined_stderr,
            )
    except CodeValidationError:
        raise
    except OSError as error:
        raise CodeValidationError(
            f"private CupidASM object snapshot could not be created: {error}"
        ) from error


_SMP_TRAMPOLINE_RAW_MAP = (
    b"cupid.raw-map.v1\n"
    b"size 4096\n"
    b"base 0x00008000\n"
    b"range 0x00000000 code16\n"
    b"range 0x0000001f data\n"
    b"range 0x00000210 code32\n"
    b"range 0x00000254 data\n"
)


def assemble_smp_trampoline(
    seed_manifest: Path,
    root: Path,
    source: Path,
    output: Path,
) -> subprocess.CompletedProcess[str]:
    try:
        repository_root = root.resolve(strict=True)
    except OSError as error:
        raise CodeValidationError(
            f"repository root cannot be resolved: {root}: {error}"
        ) from error
    if not repository_root.is_dir():
        raise CodeValidationError(
            f"repository root is not a directory: {repository_root}"
        )
    source_logical = _code_logical_path(
        source,
        subject="SMP trampoline source",
    )

    try:
        with _checked_assembly_transaction(
            seed_manifest,
            repository_root,
            source_logical,
            output,
            source_subject="SMP trampoline source",
            temporary_prefix=".smp-trampoline-",
        ) as transaction:
            candidate_logical = transaction.candidate_logical
            map_logical = candidate_logical + ".cupidmap"
            map_output = transaction.pin_private_output(map_logical)

            try:
                assembled = transaction.run_tool(
                    "cupidasm",
                    (
                        "-f",
                        "bin",
                        "--map",
                        map_logical,
                        "-o",
                        candidate_logical,
                        source_logical,
                    ),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidASM could not run: {error}"
                ) from error
            tool_stderr = assembled.stderr or ""
            transaction.require_inputs_unchanged(
                activity="CupidASM",
                tool_stderr=tool_stderr,
            )
            if assembled.returncode != 0:
                return subprocess.CompletedProcess(
                    assembled.args,
                    assembled.returncode,
                    "",
                    tool_stderr,
                )
            if assembled.stdout:
                raise CodeValidationError(
                    "checked CupidASM wrote unexpected standard output",
                    tool_stderr=tool_stderr,
                )
            if tool_stderr:
                raise CodeValidationError(
                    "checked CupidASM wrote unexpected standard error",
                    tool_stderr=tool_stderr,
                )
            candidate_snapshot = transaction.candidate_snapshot()
            if candidate_snapshot is None:
                raise CodeValidationError(
                    "checked CupidASM output does not exist: "
                    f"{candidate_logical}",
                    tool_stderr=tool_stderr,
                )
            if candidate_snapshot.size != 4096:
                raise CodeValidationError(
                    "checked CupidASM trampoline output must be exactly "
                    f"4096 bytes, got {candidate_snapshot.size}",
                    tool_stderr=tool_stderr,
                )
            map_snapshot = _code_output_entry(map_output)
            if map_snapshot is None:
                raise CodeValidationError(
                    "checked CupidASM SMP trampoline range map does not exist: "
                    f"{map_output.logical}",
                    tool_stderr=tool_stderr,
                )
            if map_snapshot.size == 0:
                raise CodeValidationError(
                    "checked CupidASM SMP trampoline range map may not be empty",
                    tool_stderr=tool_stderr,
                )
            try:
                map_payload = map_output.path.read_bytes()
            except OSError as error:
                raise CodeValidationError(
                    "checked CupidASM SMP trampoline range map cannot be read: "
                    f"{map_output.logical}: {error}",
                    tool_stderr=tool_stderr,
                ) from error
            if (
                len(map_payload) != map_snapshot.size
                or hashlib.sha256(map_payload).hexdigest()
                != map_snapshot.sha256
            ):
                raise CodeValidationError(
                    "checked CupidASM SMP trampoline range map changed while "
                    "layout policy validation ran",
                    tool_stderr=tool_stderr,
                )
            if map_payload != _SMP_TRAMPOLINE_RAW_MAP:
                raise CodeValidationError(
                    "checked CupidASM SMP trampoline range map does not match "
                    "the required layout policy",
                    tool_stderr=tool_stderr,
                )

            try:
                disassembled = transaction.run_tool(
                    "cupiddis",
                    (
                        "--raw",
                        "--range-map",
                        map_logical,
                        "--require-known",
                        "--require-local-targets",
                        candidate_logical,
                    ),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidDis could not run: {error}",
                    tool_stderr=tool_stderr,
                ) from error
            combined_stderr = tool_stderr + (disassembled.stderr or "")
            transaction.require_inputs_unchanged(
                activity="checked tools",
                tool_stderr=combined_stderr,
            )
            transaction.require_candidate_unchanged(
                candidate_snapshot,
                tool_stderr=combined_stderr,
            )
            transaction.require_private_output_unchanged(
                map_output,
                map_snapshot,
                description="checked CupidASM SMP trampoline range map",
                tool_stderr=combined_stderr,
            )
            transaction.require_publication_boundary_unchanged(combined_stderr)
            if disassembled.returncode != 0:
                return subprocess.CompletedProcess(
                    disassembled.args,
                    disassembled.returncode,
                    "",
                    combined_stderr,
                )
            if disassembled.stdout:
                raise CodeValidationError(
                    "checked CupidDis wrote unexpected standard output",
                    tool_stderr=combined_stderr,
                )
            if disassembled.stderr:
                raise CodeValidationError(
                    "checked CupidDis wrote unexpected standard error",
                    tool_stderr=combined_stderr,
                )
            try:
                transaction.publish()
            except OSError as error:
                raise CodeValidationError(
                    f"validated SMP trampoline could not be published: {error}",
                    tool_stderr=combined_stderr,
                ) from error
            return subprocess.CompletedProcess(
                disassembled.args,
                0,
                "",
                combined_stderr,
            )
    except CodeValidationError:
        raise
    except OSError as error:
        raise CodeValidationError(
            f"private SMP trampoline snapshot could not be created: {error}"
        ) from error


def assemble_bootloader(
    seed_manifest: Path,
    root: Path,
    source: Path,
    output: Path,
) -> subprocess.CompletedProcess[str]:
    try:
        repository_root = root.resolve(strict=True)
    except OSError as error:
        raise CodeValidationError(
            f"repository root cannot be resolved: {root}: {error}"
        ) from error
    if not repository_root.is_dir():
        raise CodeValidationError(
            f"repository root is not a directory: {repository_root}"
        )
    source_logical = _code_logical_path(
        source,
        subject="bootloader source",
    )

    try:
        with _checked_assembly_transaction(
            seed_manifest,
            repository_root,
            source_logical,
            output,
            source_subject="bootloader source",
            temporary_prefix=".bootloader-",
        ) as transaction:
            candidate_logical = transaction.candidate_logical
            map_logical = candidate_logical + ".cupidmap"
            map_output = transaction.pin_private_output(map_logical)

            try:
                assembled = transaction.run_tool(
                    "cupidasm",
                    (
                        "-f",
                        "bin",
                        "--map",
                        map_logical,
                        "-o",
                        candidate_logical,
                        source_logical,
                    ),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidASM could not run: {error}"
                ) from error
            tool_stderr = assembled.stderr or ""
            transaction.require_inputs_unchanged(
                activity="CupidASM",
                tool_stderr=tool_stderr,
            )
            if assembled.returncode != 0:
                return subprocess.CompletedProcess(
                    assembled.args,
                    assembled.returncode,
                    "",
                    tool_stderr,
                )
            if assembled.stdout:
                raise CodeValidationError(
                    "checked CupidASM wrote unexpected standard output",
                    tool_stderr=tool_stderr,
                )
            if tool_stderr:
                raise CodeValidationError(
                    "checked CupidASM wrote unexpected standard error",
                    tool_stderr=tool_stderr,
                )
            candidate_snapshot = transaction.candidate_snapshot()
            if candidate_snapshot is None:
                raise CodeValidationError(
                    "checked CupidASM output does not exist: "
                    f"{candidate_logical}",
                    tool_stderr=tool_stderr,
                )
            if candidate_snapshot.size != 2560:
                raise CodeValidationError(
                    "checked CupidASM bootloader output must be exactly "
                    f"2560 bytes, got {candidate_snapshot.size}",
                    tool_stderr=tool_stderr,
                )
            map_snapshot = _code_output_entry(map_output)
            if map_snapshot is None:
                raise CodeValidationError(
                    "checked CupidASM range map does not exist: "
                    f"{map_output.logical}",
                    tool_stderr=tool_stderr,
                )
            if map_snapshot.size == 0:
                raise CodeValidationError(
                    "checked CupidASM range map may not be empty",
                    tool_stderr=tool_stderr,
                )

            try:
                disassembled = transaction.run_tool(
                    "cupiddis",
                    (
                        "--require-known",
                        "--require-local-targets",
                        "--raw",
                        "--range-map",
                        map_logical,
                        candidate_logical,
                    ),
                )
            except BootstrapError as error:
                raise CodeValidationError(
                    f"checked CupidDis could not run: {error}",
                    tool_stderr=tool_stderr,
                ) from error
            combined_stderr = tool_stderr + (disassembled.stderr or "")
            transaction.require_inputs_unchanged(
                activity="checked tools",
                tool_stderr=combined_stderr,
            )
            transaction.require_candidate_unchanged(
                candidate_snapshot,
                tool_stderr=combined_stderr,
            )
            transaction.require_private_output_unchanged(
                map_output,
                map_snapshot,
                description="checked CupidASM range map",
                tool_stderr=combined_stderr,
            )
            transaction.require_publication_boundary_unchanged(combined_stderr)
            if disassembled.returncode != 0:
                return subprocess.CompletedProcess(
                    disassembled.args,
                    disassembled.returncode,
                    "",
                    combined_stderr,
                )
            if disassembled.stdout:
                raise CodeValidationError(
                    "checked CupidDis wrote unexpected standard output",
                    tool_stderr=combined_stderr,
                )
            if disassembled.stderr:
                raise CodeValidationError(
                    "checked CupidDis wrote unexpected standard error",
                    tool_stderr=combined_stderr,
                )
            try:
                transaction.publish()
            except OSError as error:
                raise CodeValidationError(
                    f"validated bootloader could not be published: {error}",
                    tool_stderr=combined_stderr,
                ) from error
            return subprocess.CompletedProcess(
                disassembled.args,
                0,
                "",
                combined_stderr,
            )
    except CodeValidationError:
        raise
    except OSError as error:
        raise CodeValidationError(
            f"private bootloader snapshot could not be created: {error}"
        ) from error


def clean_paths(patterns: list[str]) -> None:
    for pattern in patterns:
        for path in Path(".").glob(pattern):
            if path.is_dir():
                shutil.rmtree(path)
            else:
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("image")
    p.add_argument("--seed-manifest", type=Path, required=True)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--bootloader", type=Path, required=True)
    p.add_argument("--kernel", type=Path, required=True)
    p.add_argument("--hdd-mb", type=int, required=True)
    p.add_argument("--fat-start-lba", type=int, required=True)
    p.add_argument("--stage", action="append", type=_parse_stage, default=[])
    p.add_argument("--force-format", action="store_true")
    p.add_argument("--wads", nargs="*", type=Path, default=[])

    p = sub.add_parser("stage")
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--fat-start-lba", type=int, required=True)
    p.add_argument("stage", nargs="+", type=_parse_stage)

    p = sub.add_parser("stage-wads")
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--fat-start-lba", type=int, required=True)
    p.add_argument("wads", nargs="*", type=Path)

    p = sub.add_parser("mksyms")
    readers = p.add_mutually_exclusive_group()
    readers.add_argument("--nm")
    readers.add_argument("--seed-manifest", type=Path)
    p.add_argument("elf", type=Path)
    p.add_argument("out", type=Path)

    p = sub.add_parser("embed-jpeg")
    writers = p.add_mutually_exclusive_group()
    writers.add_argument("--object-tool")
    writers.add_argument("--seed-manifest", type=Path)
    p.add_argument("src", type=Path)
    p.add_argument("out", type=Path)

    p = sub.add_parser("gen-bin-programs")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--bin", nargs="*", default=[])
    p.add_argument("--headers", nargs="*", default=[])
    p.add_argument("--browser", nargs="*", default=[])

    p = sub.add_parser("gen-docs-programs")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--ctxt", nargs="*", default=[])
    p.add_argument("--doc-assets", nargs="*", default=[])
    p.add_argument("--home-assets", nargs="*", default=[])

    p = sub.add_parser("gen-demos-programs")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--demos", nargs="*", default=[])

    p = sub.add_parser("gen-big")
    p.add_argument("--seed-manifest", type=Path)
    p.add_argument("--source", type=Path)
    p.add_argument("out", type=Path)

    p = sub.add_parser("build-iso")
    p.add_argument("--seed-manifest", type=Path, required=True)
    p.add_argument("--fixtures", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)

    p = sub.add_parser("usb-image")
    p.add_argument("out", type=Path)
    p.add_argument("--size-mb", type=int, default=32)
    p.add_argument("--partition-lba", type=int, default=2048)

    p = sub.add_parser("validate-code")
    p.add_argument("--seed-manifest", type=Path, required=True)
    p.add_argument("--root", type=Path, default=Path("."))
    p.add_argument("--input-manifest", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("inputs", nargs="*", type=Path)

    p = sub.add_parser("assemble-smp-trampoline")
    p.add_argument("--seed-manifest", type=Path, required=True)
    p.add_argument("--root", type=Path, default=Path("."))
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)

    p = sub.add_parser("assemble-cupidasm-object")
    p.add_argument("--seed-manifest", type=Path, required=True)
    p.add_argument("--root", type=Path, default=Path("."))
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)

    p = sub.add_parser("assemble-bootloader")
    p.add_argument("--seed-manifest", type=Path, required=True)
    p.add_argument("--root", type=Path, default=Path("."))
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)

    p = sub.add_parser("clean")
    p.add_argument("patterns", nargs="+")

    args = ap.parse_args(argv)
    if args.cmd == "image":
        stages = list(args.stage)
        stages += [StageFile(path, _wad_dest(path, i + 1)) for i, path in enumerate(args.wads or [])]
        try:
            create_or_update_image(
                args.image,
                args.bootloader,
                args.kernel,
                args.hdd_mb,
                args.fat_start_lba,
                stages,
                args.force_format,
                seed_manifest=args.seed_manifest,
            )
        except DiskImageError as error:
            print(f"[hostbuild] image failed: {error}", file=sys.stderr)
            return 1
    elif args.cmd == "stage":
        stage_files(args.image, args.fat_start_lba, args.stage)
    elif args.cmd == "stage-wads":
        stage_wads(args.image, args.fat_start_lba, args.wads)
    elif args.cmd == "mksyms":
        try:
            nm = args.nm
            if nm is None and args.seed_manifest is None:
                nm = "nm"
            write_ksyms_source(
                nm,
                args.elf,
                args.out,
                seed_manifest=args.seed_manifest,
            )
        except KsymsGenerationError as error:
            print(f"[hostbuild] mksyms failed: {error}", file=sys.stderr)
            return 1
    elif args.cmd == "embed-jpeg":
        object_tool = args.object_tool
        if object_tool is None and args.seed_manifest is None:
            object_tool = "cupidobj"
        try:
            embed_jpeg(
                object_tool,
                args.src,
                args.out,
                seed_manifest=args.seed_manifest,
            )
        except EmbedJpegError as error:
            print(
                f"[hostbuild] embed-jpeg failed: {error}",
                file=sys.stderr,
            )
            return 1
    elif args.cmd == "gen-bin-programs":
        try:
            gen_bin_programs(args.out, args.bin, args.headers, args.browser)
        except InstallSourceGenerationError as error:
            print(f"[hostbuild] install source failed: {error}", file=sys.stderr)
            return 1
    elif args.cmd == "gen-docs-programs":
        try:
            gen_docs_programs(
                args.out, args.ctxt, args.doc_assets, args.home_assets
            )
        except InstallSourceGenerationError as error:
            print(f"[hostbuild] install source failed: {error}", file=sys.stderr)
            return 1
    elif args.cmd == "gen-demos-programs":
        try:
            gen_demos_programs(args.out, args.demos)
        except InstallSourceGenerationError as error:
            print(f"[hostbuild] install source failed: {error}", file=sys.stderr)
            return 1
    elif args.cmd == "gen-big":
        try:
            gen_big(
                args.out,
                seed_manifest=args.seed_manifest,
                source=args.source,
            )
        except IsoAuthoringError as error:
            print(
                f"[hostbuild] gen-big failed: {error}",
                file=sys.stderr,
            )
            return 1
    elif args.cmd == "build-iso":
        try:
            build_iso(
                args.fixtures,
                args.out,
                args.manifest,
                seed_manifest=args.seed_manifest,
            )
        except IsoAuthoringError as error:
            print(
                f"[hostbuild] build-iso failed: {error}",
                file=sys.stderr,
            )
            return 1
    elif args.cmd == "usb-image":
        create_usb_image(args.out, args.size_mb, args.partition_lba)
    elif args.cmd == "validate-code":
        try:
            result = validate_code(
                args.seed_manifest,
                args.root,
                args.inputs,
                input_manifest=args.input_manifest,
                output=args.output,
            )
        except CodeValidationError as error:
            if error.tool_stderr:
                sys.stderr.write(error.tool_stderr)
            print(
                f"[hostbuild] validate-code failed: {error}",
                file=sys.stderr,
            )
            return 1
        if result.stderr:
            sys.stderr.write(result.stderr)
        return result.returncode
    elif args.cmd == "assemble-smp-trampoline":
        try:
            result = assemble_smp_trampoline(
                args.seed_manifest,
                args.root,
                args.source,
                args.output,
            )
        except CodeValidationError as error:
            if error.tool_stderr:
                sys.stderr.write(error.tool_stderr)
            print(
                f"[hostbuild] assemble-smp-trampoline failed: {error}",
                file=sys.stderr,
            )
            return 1
        if result.stderr:
            sys.stderr.write(result.stderr)
        return result.returncode
    elif args.cmd == "assemble-cupidasm-object":
        try:
            result = assemble_cupidasm_object(
                args.seed_manifest,
                args.root,
                args.source,
                args.output,
            )
        except CodeValidationError as error:
            if error.tool_stderr:
                sys.stderr.write(error.tool_stderr)
            print(
                f"[hostbuild] assemble-cupidasm-object failed: {error}",
                file=sys.stderr,
            )
            return 1
        if result.stderr:
            sys.stderr.write(result.stderr)
        return result.returncode
    elif args.cmd == "assemble-bootloader":
        try:
            result = assemble_bootloader(
                args.seed_manifest,
                args.root,
                args.source,
                args.output,
            )
        except CodeValidationError as error:
            if error.tool_stderr:
                sys.stderr.write(error.tool_stderr)
            print(
                f"[hostbuild] assemble-bootloader failed: {error}",
                file=sys.stderr,
            )
            return 1
        if result.stderr:
            sys.stderr.write(result.stderr)
        return result.returncode
    elif args.cmd == "clean":
        clean_paths(args.patterns)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

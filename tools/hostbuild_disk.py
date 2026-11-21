## @file
#  @brief Publish retained disk images and stage files on FAT16.
"""Compose Cupid OS disk images and retain their persistent FAT partition."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import stat
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path

if __package__:
    from .bootstrap_toolchain import BootstrapError, run_seed_tool, verify_seed_inputs
else:
    from bootstrap_toolchain import BootstrapError, run_seed_tool, verify_seed_inputs


SECTOR_SIZE = 512
FAT16_TYPES = {0x04, 0x06, 0x0E}
FAT16_EOC = 0xFFFF
FAT16_EOC_MIN = 0xFFF8


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


def _ceil_div(a: int, b: int) -> int:
    return (a + b - 1) // b


def _parse_stage(value: str) -> StageFile:
    separator = value.find(":")
    if separator == 1 and value[0] in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ":
        # A Windows drive colon belongs to SRC when another separator follows.
        drive_separator = value.find(":", 2)
        if drive_separator != -1:
            separator = drive_separator
    if separator == -1:
        raise argparse.ArgumentTypeError("stage entries must be SRC:/guest/path")
    src, dest = value[:separator], value[separator + 1:]
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

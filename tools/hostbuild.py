#!/usr/bin/env python3
"""Portable host-side build helpers for CupidOS.

This module replaces shell-only build steps with Python so the same Makefile
can run under Linux shells and native Windows GNU Make.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


SECTOR_SIZE = 512
FAT16_TYPES = {0x04, 0x06, 0x0E}
FAT16_EOC = 0xFFFF
FAT16_EOC_MIN = 0xFFF8


@dataclass(frozen=True)
class StageFile:
    source: Path
    dest: str


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


def _path_for_symbol(path: str | Path) -> str:
    return "".join(ch if ch.isalnum() else "_" for ch in str(path).replace("\\", "/"))


def _objcopy_binary(objcopy: str, src: Path, out: Path) -> None:
    subprocess.run(
        [objcopy, "-I", "binary", "-O", "elf32-i386", "-B", "i386", str(src), str(out)],
        check=True,
    )


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
        while True:
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
    if ptype not in FAT16_TYPES or start != fat_start_lba or sectors == 0:
        return False
    with image.open("rb") as f:
        f.seek(fat_start_lba * SECTOR_SIZE)
        bpb = f.read(SECTOR_SIZE)
    return (
        len(bpb) == SECTOR_SIZE
        and bpb[510:512] == b"\x55\xaa"
        and struct.unpack_from("<H", bpb, 11)[0] == SECTOR_SIZE
        and bpb[16] in (1, 2)
        and struct.unpack_from("<H", bpb, 22)[0] > 0
    )


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
        while 2 <= cluster < FAT16_EOC_MIN:
            nxt = self._read_fat(cluster)
            self._write_fat(cluster, 0)
            cluster = nxt

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


def create_or_update_image(
    image: Path,
    bootloader: Path,
    kernel: Path,
    hdd_mb: int,
    fat_start_lba: int,
    stage_files: list[StageFile],
    force_format: bool,
) -> None:
    image_sectors = hdd_mb * 1024 * 1024 // SECTOR_SIZE
    if fat_start_lba <= 5:
        raise ValueError("FAT partition must start after bootloader and kernel area")
    if fat_start_lba >= image_sectors:
        raise ValueError("FAT partition start is beyond image size")

    boot = bootloader.read_bytes()
    if len(boot) < 5 * SECTOR_SIZE:
        raise ValueError(f"{bootloader} is too small; expected at least 5 sectors")
    kernel_size = kernel.stat().st_size
    kernel_end = 5 * SECTOR_SIZE + kernel_size
    fat_start_bytes = fat_start_lba * SECTOR_SIZE
    if kernel_end > fat_start_bytes:
        raise ValueError(
            f"{kernel} ({kernel_size} bytes) overlaps FAT partition at LBA {fat_start_lba}"
        )

    partition_sectors = image_sectors - fat_start_lba
    layout = _choose_layout(partition_sectors)
    recreate = force_format or not _valid_existing_image(image, hdd_mb, fat_start_lba)

    if recreate:
        if image.exists():
            image.unlink()
        with image.open("wb") as f:
            f.truncate(image_sectors * SECTOR_SIZE)
            _write_mbr(f, boot, fat_start_lba, partition_sectors)
            _write_fat16_filesystem(f, fat_start_lba, layout)
        print(f"[hostbuild] Created {image} ({hdd_mb}MB FAT16 at LBA {fat_start_lba})")
    else:
        print(f"[hostbuild] Reusing existing image {image} (preserving FAT data)")

    with image.open("r+b") as f:
        _write_mbr(f, boot, fat_start_lba, partition_sectors)
        f.seek(SECTOR_SIZE)
        f.write(boot[SECTOR_SIZE : 5 * SECTOR_SIZE])
        f.seek(5 * SECTOR_SIZE)
        f.write(b"\x00" * (fat_start_bytes - 5 * SECTOR_SIZE))
        f.seek(5 * SECTOR_SIZE)
        with kernel.open("rb") as kf:
            shutil.copyfileobj(kf, f)

    if stage_files:
        with Fat16Image(image, fat_start_lba) as fat:
            for sf in stage_files:
                if sf.source.exists():
                    fat.write_file(sf.dest, sf.source.read_bytes())
                    print(f"[hostbuild] Staged {sf.source} -> {image}:{sf.dest}")
                else:
                    print(f"[hostbuild] Skipping missing stage file {sf.source}")


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


def _symbols_from_nm(nm: str, elf: Path) -> list[tuple[int, str]]:
    proc = subprocess.run([nm, "-n", str(elf)], check=True, text=True, capture_output=True)
    symbols: list[tuple[int, str]] = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        addr_s, typ, name = parts[0], parts[1], parts[2]
        if typ not in {"t", "T", "w", "W"} or name.startswith(".L"):
            continue
        try:
            symbols.append((int(addr_s, 16), name))
        except ValueError:
            continue
    return symbols


def write_ksyms_source(nm: str, elf: Path, out: Path) -> None:
    blob = build_ksyms_blob(_symbols_from_nm(nm, elf))
    with out.open("w", newline="\n") as f:
        f.write("/* Auto-generated by tools/hostbuild.py -- do not edit. */\n")
        f.write('#include "ksyms.h"\n\n')
        f.write("const unsigned char\n")
        f.write('__attribute__((section(".ksyms"), used, aligned(4)))\n')
        f.write("ksym_blob[] = {\n")
        for i in range(0, len(blob), 16):
            chunk = blob[i : i + 16]
            f.write("  " + " ".join(f"0x{b:02x}," for b in chunk) + "\n")
        f.write("};\n\n")
        f.write("const unsigned int ksym_blob_size = sizeof(ksym_blob);\n")
    print(f"[hostbuild] mksyms: {out} ({len(blob)} bytes)")


def embed_jpeg(objcopy: str, src: Path, out: Path) -> None:
    tmp = Path(str(out) + ".baseline.jpg")
    converted = False
    try:
        jpegtran = shutil.which("jpegtran")
        if jpegtran:
            result = subprocess.run(
                [jpegtran, "-copy", "none", "-optimize", "-outfile", str(tmp), str(src)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            converted = result.returncode == 0 and tmp.exists()
        if not converted and shutil.which("djpeg") and shutil.which("cjpeg"):
            with tmp.open("wb") as f:
                p1 = subprocess.Popen(
                    [shutil.which("djpeg") or "djpeg", "-rgb", str(src)],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                )
                p2 = subprocess.run(
                    [shutil.which("cjpeg") or "cjpeg", "-quality", "90", "-baseline", "-optimize"],
                    stdin=p1.stdout,
                    stdout=f,
                    stderr=subprocess.DEVNULL,
                )
                if p1.stdout:
                    p1.stdout.close()
                converted = p1.wait() == 0 and p2.returncode == 0
        if not converted and shutil.which("ffmpeg"):
            result = subprocess.run(
                [
                    shutil.which("ffmpeg") or "ffmpeg",
                    "-y",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-i",
                    str(src),
                    "-frames:v",
                    "1",
                    "-q:v",
                    "2",
                    str(tmp),
                ]
            )
            converted = result.returncode == 0 and tmp.exists()
        if not converted:
            shutil.copyfile(src, tmp)
            print(f"[hostbuild] JPEG raw embed {src}")
        else:
            print(f"[hostbuild] JPEG baseline embed {src}")

        _objcopy_binary(objcopy, tmp, out)
        old = _path_for_symbol(tmp)
        new = _path_for_symbol(src)
        subprocess.run(
            [
                objcopy,
                f"--redefine-sym=_binary_{old}_start=_binary_{new}_start",
                f"--redefine-sym=_binary_{old}_end=_binary_{new}_end",
                f"--redefine-sym=_binary_{old}_size=_binary_{new}_size",
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


def gen_bin_programs(out: Path, bins: list[str], headers: list[str], browser: list[str]) -> None:
    bin_names = [_name_no_ext(p) for p in bins]
    hdr_names = [_name_no_ext(p) for p in headers]
    browser_names = [_name_no_ext(p) for p in browser]
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
    doc_bmps = [_name_no_ext(p) for p in doc_assets if Path(p).suffix.lower() == ".bmp"]
    home_bmps = [_name_no_ext(p) for p in home_assets if Path(p).suffix.lower() == ".bmp"]
    home_pngs = [_name_no_ext(p) for p in home_assets if Path(p).suffix.lower() == ".png"]
    home_jpgs = [_name_no_ext(p) for p in home_assets if Path(p).suffix.lower() == ".jpg"]
    home_jpegs = [_name_no_ext(p) for p in home_assets if Path(p).suffix.lower() == ".jpeg"]
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
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_bmp_start[];" for n in home_bmps]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_png_start[];" for n in home_pngs]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_jpg_start[];" for n in home_jpgs]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_jpeg_start[];" for n in home_jpegs]
    lines += [f"extern const char _binary_cupidos_txt_{_c_symbol_part(n)}_CTXT_end[];" for n in ctxt_names]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_bmp_end[];" for n in doc_bmps]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_bmp_end[];" for n in home_bmps]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_png_end[];" for n in home_pngs]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_jpg_end[];" for n in home_jpgs]
    lines += [f"extern const char _binary_{_c_symbol_part(n)}_jpeg_end[];" for n in home_jpegs]
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
    for names, ext in ((home_bmps, "bmp"), (home_pngs, "png"), (home_jpgs, "jpg"), (home_jpegs, "jpeg")):
        lines += [
            f'    {{ uint32_t sz = (uint32_t)(_binary_{_c_symbol_part(n)}_{ext}_end - _binary_{_c_symbol_part(n)}_{ext}_start); install_home_asset("/home/{n}.{ext}", _binary_{_c_symbol_part(n)}_{ext}_start, sz); }}'
            for n in names
        ]
    lines += ["    homefs_seed_end();", "}"]
    out.write_text("\n".join(lines) + "\n", newline="\n")


def gen_demos_programs(out: Path, demos: list[str]) -> None:
    names = [_name_no_ext(p) for p in demos]
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


def gen_big(out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(i & 0xFF for i in range(4096)))
    print(f"[hostbuild] Generated {out} (4096 bytes)")


def build_iso(fixtures: Path, out: Path) -> None:
    gen_big(fixtures / "big.bin")
    tool = shutil.which("mkisofs") or shutil.which("genisoimage") or shutil.which("xorrisofs")
    if not tool:
        raise SystemExit("ERROR: need mkisofs, genisoimage, or xorrisofs to build test ISO")
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([tool, "-R", "-quiet", "-o", str(out), str(fixtures)], check=True)


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
    p.add_argument("--nm", default="nm")
    p.add_argument("elf", type=Path)
    p.add_argument("out", type=Path)

    p = sub.add_parser("embed-jpeg")
    p.add_argument("--objcopy", default="objcopy")
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
    p.add_argument("out", type=Path)

    p = sub.add_parser("build-iso")
    p.add_argument("--fixtures", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)

    p = sub.add_parser("usb-image")
    p.add_argument("out", type=Path)
    p.add_argument("--size-mb", type=int, default=32)
    p.add_argument("--partition-lba", type=int, default=2048)

    p = sub.add_parser("clean")
    p.add_argument("patterns", nargs="+")

    args = ap.parse_args(argv)
    if args.cmd == "image":
        stages = list(args.stage)
        stages += [StageFile(path, _wad_dest(path, i + 1)) for i, path in enumerate(args.wads or [])]
        create_or_update_image(
            args.image,
            args.bootloader,
            args.kernel,
            args.hdd_mb,
            args.fat_start_lba,
            stages,
            args.force_format,
        )
    elif args.cmd == "stage":
        stage_files(args.image, args.fat_start_lba, args.stage)
    elif args.cmd == "stage-wads":
        stage_wads(args.image, args.fat_start_lba, args.wads)
    elif args.cmd == "mksyms":
        write_ksyms_source(args.nm, args.elf, args.out)
    elif args.cmd == "embed-jpeg":
        embed_jpeg(args.objcopy, args.src, args.out)
    elif args.cmd == "gen-bin-programs":
        gen_bin_programs(args.out, args.bin, args.headers, args.browser)
    elif args.cmd == "gen-docs-programs":
        gen_docs_programs(args.out, args.ctxt, args.doc_assets, args.home_assets)
    elif args.cmd == "gen-demos-programs":
        gen_demos_programs(args.out, args.demos)
    elif args.cmd == "gen-big":
        gen_big(args.out)
    elif args.cmd == "build-iso":
        build_iso(args.fixtures, args.out)
    elif args.cmd == "usb-image":
        create_usb_image(args.out, args.size_mb, args.partition_lba)
    elif args.cmd == "clean":
        clean_paths(args.patterns)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

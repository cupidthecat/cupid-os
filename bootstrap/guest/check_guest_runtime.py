#!/usr/bin/env python3
"""Rebuild CupidC objects inside Cupid OS and compare persisted bytes."""

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.hostbuild import Fat16Image

BUILD = ROOT / "build" / "guest-engine"
FAT_START_LBA = 20480
SEED_MANIFEST = "bootstrap/seeds/i386-linux/manifest.json"
HOMEFS_MAGIC = 0x31465348
COMPILER_REAPED = r".*?released compiler image lease"


def run(*arguments: str) -> None:
    subprocess.run([sys.executable, *arguments], cwd=ROOT, check=True)


def read_homefs(image: Path) -> bytes:
    with Fat16Image(image, FAT_START_LBA) as disk:
        entry = disk._find_entry(None, b"HOMEFS  SYS")
        if entry is None:
            raise ValueError("missing HOMEFS.SYS")
        offset, directory, _ = entry
        cluster = struct.unpack_from("<H", directory, offset + 26)[0]
        remaining = struct.unpack_from("<I", directory, offset + 28)[0]
        locations = set()
        payload = bytearray()
        while remaining:
            if cluster in locations or not 2 <= cluster <= disk.cluster_count + 1:
                raise ValueError("invalid HOMEFS.SYS cluster chain")
            locations.add(cluster)
            count = min(remaining, disk.sectors_per_cluster * disk.bytes_per_sector)
            disk.f.seek(disk._cluster_offset(cluster))
            block = disk.f.read(count)
            if len(block) != count:
                raise ValueError("truncated HOMEFS.SYS cluster")
            payload.extend(block)
            remaining -= count
            cluster = disk._read_fat(cluster)
        return bytes(payload)


def compare_objects(payload: bytes, include_frontend: bool) -> None:
    magic, version, count = struct.unpack_from("<III", payload)
    if magic != HOMEFS_MAGIC or version != 1:
        raise ValueError("invalid HOMEFS.SYS header")

    offset = 12
    paths = []
    references = {"ctool.o": BUILD / "ctool.o"}
    if include_frontend:
        references["frontend.o"] = BUILD / "cupidc_frontend.o"
    for index in range(count):
        parent, _, size, name_size = struct.unpack_from("<IIII", payload, offset)
        offset += 16
        end_name = offset + name_size
        end_file = end_name + size
        if end_file > len(payload):
            raise ValueError("truncated HOMEFS.SYS entry")
        name = payload[offset:end_name].decode("utf-8")
        if index == 0 and parent == 0xFFFFFFFF:
            path = ""
        elif parent < index:
            path = f"{paths[parent]}/{name}" if paths[parent] else name
        else:
            raise ValueError("invalid HOMEFS.SYS parent")
        paths.append(path)

        if path in references:
            data = payload[end_name:end_file]
            if data != references.pop(path).read_bytes():
                raise ValueError(f"guest /home/{path} differs from the checked object")
            digest = hashlib.sha256(data).hexdigest()
            print(f"/home/{path} matches all {len(data)} bytes ({digest})")
        offset = end_file
    if references:
        raise ValueError(f"missing guest objects: {', '.join(references)}")


def check(scratch: Path, qemu: str, timeout: float, include_frontend: bool) -> None:
    disk = scratch / "cupidos.img"
    job = scratch / "cupidc.job"
    job.write_bytes(b"/toolchain/ctool.cc\n/home/ctool.o\n")
    rejected = scratch / "bad.job"
    rejected.write_bytes(b"/toolchain/missing.cc\n/home/ctool.o\n")
    stages = [f"{BUILD}/engsrc.iso:/engsrc.iso", f"{BUILD}/cupidc-guest.elf:/cupcc",
              f"{rejected}:/bad.job"]
    if include_frontend:
        frontend = scratch / "front.job"
        frontend.write_bytes(b"/toolchain/cupidc_frontend.cc\n/home/frontend.o\n")
        stages.append(f"{frontend}:/front.job")

    run("tools/hostbuild.py", "image", "--seed-manifest", SEED_MANIFEST,
        "--image", str(disk), "--bootloader", "boot/boot.bin",
        "--kernel", "kernel/kernel.bin", "--hdd-mb", "200",
        "--fat-start-lba", str(FAT_START_LBA), "--stage", f"{job}:/cupidc.job")
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--image", str(disk),
             "--smp", "4", "--nic", "e1000", "--require-command-completion",
             "--timeout", str(timeout))
    log_name = 'frontend' if include_frontend else 'ctool'
    completed = r"\[terminal\] command complete"
    # First boot seeds /home from the staged request. Stage the large ISO and
    # executable after that import so they remain on FAT16.
    run(*smoke, "--log", str(BUILD / f"cupidc-{log_name}-seed-home.log"), "--command", "ls",
        "--success-pattern", completed)
    run("tools/hostbuild.py", "stage", "--image", str(disk), "--fat-start-lba",
        str(FAT_START_LBA), *stages)
    commands = [
        ("mount /disk/engsrc.iso /src", r"VFS: mounted .* at .*/src"),
        ("exec /disk/cupcc", r"\[cupidc-guest\] wrote /home/ctool[.]o" + COMPILER_REAPED),
        ("cp /disk/bad.job /home/cupidc.job", completed),
        ("exec /disk/cupcc", r"\[cupidc-guest\] compilation failed: " + COMPILER_REAPED),
    ]
    if include_frontend:
        commands.extend((
            ("cp /disk/front.job /home/cupidc.job", completed),
            ("exec /disk/cupcc", r"\[cupidc-guest\] wrote /home/frontend[.]o" + COMPILER_REAPED),
        ))
    arguments = []
    for command, pattern in commands[:-1]:
        arguments.extend(("--setup-command", command, "--setup-success-pattern", pattern))
    command, pattern = commands[-1]
    run(*smoke, "--log", str(BUILD / f"cupidc-{log_name}-runtime.log"), *arguments,
        "--command", command, "--success-pattern", pattern)
    compare_objects(read_homefs(disk), include_frontend)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--frontend", action="store_true", help="also rebuild the full frontend")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupidc-guest-") as temporary:
        check(Path(temporary), args.qemu, args.timeout, args.frontend)


if __name__ == "__main__":
    main()

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
COMPILE_JOB = b"/toolchain/ctool.cc\n/home/ctool.o\n"
REJECTED_JOB = b"/toolchain/missing.cc\n/home/ctool.o\n"
FRONTEND_JOB = b"/toolchain/cupidc_frontend.cc\n/home/frontend.o\n"


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


def write_frontend_homefs(path: Path) -> None:
    files = ((b"cupidc.job", COMPILE_JOB), (b"bad.job", REJECTED_JOB),
             (b"front.job", FRONTEND_JOB), (b"src.iso", (BUILD / "engsrc.iso").read_bytes()))
    with path.open("wb") as output:
        output.write(struct.pack("<III", HOMEFS_MAGIC, 1, len(files) + 1))
        output.write(struct.pack("<IIII", 0xffffffff, 1, 0, 0))
        for name, contents in files:
            output.write(struct.pack("<IIII", 0, 0, len(contents), len(name)))
            output.write(name)
            output.write(contents)


def create_disk(disk: Path, *staged: str) -> None:
    stage_args = [argument for item in staged for argument in ("--stage", item)]
    run("tools/hostbuild.py", "image", "--seed-manifest", SEED_MANIFEST,
        "--image", str(disk), "--bootloader", "boot/boot.bin",
        "--kernel", "kernel/kernel.bin", "--hdd-mb", "200",
        "--fat-start-lba", str(FAT_START_LBA), *stage_args)


def check(scratch: Path, qemu: str, timeout: float, include_frontend: bool) -> None:
    disk = scratch / "cupidos.img"
    worker = f"{BUILD}/cupidc-guest.elf:/cupcc"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--image", str(disk),
             "--smp", "1" if include_frontend else "4", "--nic", "e1000",
             "--require-command-completion",
             "--timeout", str(timeout))
    log_name = "frontend" if include_frontend else "ctool"
    completed = r"\[terminal\] command complete"

    if include_frontend:
        homefs = scratch / "homefs.sys"
        write_frontend_homefs(homefs)
        create_disk(disk, f"{homefs}:/homefs.sys", worker)
        source_iso, requests = "/home/src.iso", "/home"
    else:
        job = scratch / "cupidc.job"
        job.write_bytes(COMPILE_JOB)
        rejected = scratch / "bad.job"
        rejected.write_bytes(REJECTED_JOB)
        # Let the normal first-boot import place the request in /home.
        create_disk(disk, f"{job}:/cupidc.job")
        run(*smoke, "--log", str(BUILD / "cupidc-ctool-seed-home.log"), "--command", "ls",
            "--success-pattern", completed)
        run("tools/hostbuild.py", "stage", "--image", str(disk), "--fat-start-lba",
            str(FAT_START_LBA), f"{BUILD}/engsrc.iso:/engsrc.iso", worker, f"{rejected}:/bad.job")
        source_iso, requests = "/disk/engsrc.iso", "/disk"

    commands = [
        (f"mount {source_iso} /src", r"VFS: mounted 'iso9660' at '/src'"),
        ("exec /disk/cupcc", r"\[cupidc-guest\] wrote /home/ctool[.]o" + COMPILER_REAPED),
        (f"cp {requests}/bad.job /home/cupidc.job", completed),
        ("exec /disk/cupcc", r"\[cupidc-guest\] compilation failed: not_found" + COMPILER_REAPED),
    ]
    if include_frontend:
        commands.extend((
            ("cp /home/front.job /home/cupidc.job", completed),
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
    parser.add_argument("--timeout", type=float, help="QEMU phase deadline in seconds")
    parser.add_argument("--frontend", action="store_true", help="also rebuild the full frontend")
    args = parser.parse_args()
    timeout = args.timeout if args.timeout is not None else (3600 if args.frontend else 1800)
    with tempfile.TemporaryDirectory(prefix="cupidc-guest-") as temporary:
        check(Path(temporary), args.qemu, timeout, args.frontend)


if __name__ == "__main__":
    main()

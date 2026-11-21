#!/usr/bin/env python3
## @file
#  @brief Run the guest compiler and compare objects saved on its disk.
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
BUILD_FAILURE_PATTERN = r"\[cupidbuild-guest\] (?:stopped at |invalid request file|could not allocate)"
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


def compare_homefs_files(payload: bytes, references: dict[str, Path]) -> dict[str, bytes]:
    magic, version, count = struct.unpack_from("<III", payload)
    if magic != HOMEFS_MAGIC or version != 1:
        raise ValueError("invalid HOMEFS.SYS header")

    offset = 12
    paths = []
    matched = {}
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
            if path in matched or data != references[path].read_bytes():
                raise ValueError(f"guest /home/{path} differs from the checked file")
            digest = hashlib.sha256(data).hexdigest()
            print(f"/home/{path} matches all {len(data)} bytes ({digest})")
            matched[path] = data
        offset = end_file
    if missing := references.keys() - matched.keys():
        raise ValueError(f"missing guest files: {', '.join(sorted(missing))}")
    return matched


def compare_object_bundle(payload: bytes, scratch: Path, reference: Path,
                          members: dict[str, Path]) -> dict[str, Path]:
    matched = compare_homefs_files(payload, {reference.name: reference})
    iso = scratch / reference.name
    iso.write_bytes(matched[reference.name])
    objects = scratch / "objects"
    subprocess.run(["xorriso", "-osirrox", "on", "-indev", iso, "-extract", "/", objects],
                   check=True, capture_output=True, timeout=60)
    if {path.name for path in objects.iterdir()} != set(members):
        raise ValueError("guest ISO has a different object inventory")
    paths = {}
    for name, expected in members.items():
        destination = objects / name
        if destination.read_bytes() != expected.read_bytes():
            raise ValueError(f"guest ISO contains different bytes for {expected}")
        paths[name] = destination
    return paths


def write_homefs(path: Path, files: tuple[tuple[bytes, bytes], ...]) -> None:
    entries = [(0xffffffff, 1, b"", b"")]
    directories = {(): 0}
    seen = set()
    for name, contents in files:
        parts = tuple(name.split(b"/"))
        if parts in seen or parts in directories or any(part in (b"", b".", b"..") for part in parts):
            raise ValueError(f"invalid HomeFS fixture path {name!r}")
        parent = 0
        for count in range(1, len(parts)):
            directory = parts[:count]
            if directory in seen:
                raise ValueError(f"HomeFS fixture parent is a file for {name!r}")
            if directory not in directories:
                directories[directory] = len(entries)
                entries.append((parent, 1, directory[-1], b""))
            parent = directories[directory]
        entries.append((parent, 0, parts[-1], contents))
        seen.add(parts)

    with path.open("wb") as output:
        output.write(struct.pack("<III", HOMEFS_MAGIC, 1, len(entries)))
        for parent, kind, name, contents in entries:
            output.write(struct.pack("<IIII", parent, kind, len(contents), len(name)))
            output.write(name)
            output.write(contents)


def build_plan_manifest(steps: tuple[tuple[str, bytes, bytes], ...]) -> bytes:
    return b"".join(operation.encode("ascii") + b"\n/home/" + name + b"\n"
                    for operation, name, _ in steps)


def create_disk(disk: Path, *staged: str, production_root: Path = ROOT,
                kernel_image: Path | None = None) -> None:
    stage_args = [argument for item in staged for argument in ("--stage", item)]
    if kernel_image is None:
        kernel_image = production_root / "kernel/kernel.bin"
    run("tools/hostbuild.py", "image", "--seed-manifest", SEED_MANIFEST,
        "--image", str(disk), "--bootloader", str(production_root / "boot/boot.bin"),
        "--kernel", str(kernel_image), "--hdd-mb", "200",
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
        write_homefs(homefs, ((b"cupidc.job", COMPILE_JOB), (b"bad.job", REJECTED_JOB),
                             (b"front.job", FRONTEND_JOB),
                             (b"src.iso", (BUILD / "engsrc.iso").read_bytes())))
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
    references = {"ctool.o": BUILD / "ctool.o"}
    if include_frontend:
        references["frontend.o"] = BUILD / "cupidc_frontend.o"
    compare_homefs_files(read_homefs(disk), references)


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

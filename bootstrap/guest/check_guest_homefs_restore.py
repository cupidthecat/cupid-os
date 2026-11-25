#!/usr/bin/env python3
"""Restore a large HomeFS container and reject a bad record or short read."""

import argparse
from pathlib import Path
import struct
import tempfile

from check_guest_runtime import (
    BUILD, FAT_START_LBA, HOMEFS_MAGIC, compare_homefs_files, create_disk, read_homefs, run, write_homefs,
)
from tools.hostbuild_disk import FAT16_EOC, Fat16Image


def check_rejected(image: Path, log: Path, smoke: tuple[str, ...]) -> None:
    run(*smoke, "--image", str(image), "--log", str(log), "--verify-smp-runtime",
        "--command", "ls", "--success-pattern", r"\[cupidc\] JIT execution complete")
    boot_log = log.read_text(encoding="utf-8", errors="replace")
    if ("[homefs] refusing invalid container (-5)" not in boot_log or
            "VFS: fallback mounted fat16 on /home" not in boot_log):
        raise ValueError("the invalid fixture did not trigger HomeFS fallback")


def check_import(scratch: Path, smoke: tuple[str, ...]) -> None:
    root_file = scratch / "root.txt"
    leaf = scratch / "leaf.txt"
    root_file.write_bytes(b"root fixture")
    leaf.write_bytes(b"nested fixture")
    image = scratch / "import.img"
    create_disk(image, f"{root_file}:/root.txt", f"{leaf}:/folder/leaf.txt")
    run(*smoke, "--image", str(image), "--log", str(BUILD / "homefs-import-runtime.log"), "--verify-smp-runtime",
        "--command", "ls", "--success-pattern", r"\[cupidc\] JIT execution complete")
    compare_homefs_files(read_homefs(image), {"root.txt": root_file, "folder/leaf.txt": leaf})


def check_sparse_length(scratch: Path, smoke: tuple[str, ...]) -> None:
    invalid = scratch / "sparse-invalid.sys"
    invalid.write_bytes(b"".join((
        struct.pack("<III", HOMEFS_MAGIC, 2, 2),
        struct.pack("<IIIII", 0xFFFFFFFF, 1, 0, 0, 0),
        struct.pack("<IIIII", 0, 0, 2, 4, 3),
        b"fileABC",
    )))
    image = scratch / "sparse-invalid.img"
    create_disk(image, f"{invalid}:/homefs.sys")
    check_rejected(image, BUILD / "homefs-restore-sparse-invalid.log", smoke)
    if read_homefs(image) != invalid.read_bytes():
        raise ValueError("the rejected sparse HomeFS container changed")


def check(scratch: Path, qemu: str, timeout: float) -> None:
    # The 256 MiB kernel heap cannot hold two copies of this file.
    payload_size = 132 * 1024 * 1024
    source = f"""void main() {{
    int fd = vfs_open("/home/project/artifact.img", 0);
    if (fd < 0)
        return;
    char tail[5];
    int offset = vfs_seek(fd, -4, 2);
    int count = vfs_read(fd, tail, 4);
    vfs_close(fd);
    tail[4] = 0;
    if (offset == {payload_size - 4} && count == 4)
        serial_printf("[homefs-restore] artifact tail: %s\\n", tail);
}}
""".encode("ascii")
    valid = scratch / "valid.sys"
    write_homefs(valid, (
        (b"bin/empty", b""),
        (b"project/artifact.img", bytes(payload_size - 4) + b"TAIL"),
        (b"project/probe.cc", source),
    ))
    image = scratch / "valid.img"
    create_disk(image, f"{valid}:/homefs.sys")
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--key-pause", "0.35", "--timeout", str(timeout),
             "--require-command-completion")
    log = BUILD / "homefs-restore-large.log"
    run(*smoke, "--image", str(image), "--log", str(log), "--verify-smp-runtime",
        "--command", "exec /home/project/probe.cc",
        "--success-pattern", r"\[homefs-restore\] artifact tail: TAIL")
    if "VFS: mounted homefs on /home" not in log.read_text(encoding="utf-8", errors="replace"):
        raise ValueError("the large fixture did not mount as HomeFS")

    invalid = scratch / "invalid.sys"
    write_homefs(invalid, ((b"project/first", b"before"), (b"project/last", b"after")))
    with invalid.open("r+b") as stream:
        stream.seek(-(16 + len(b"last") + len(b"after")), 2)
        stream.write(struct.pack("<I", 0xFFFFFFFE))
    image = scratch / "invalid.img"
    create_disk(image, f"{invalid}:/homefs.sys")
    check_rejected(image, BUILD / "homefs-restore-invalid.log", smoke)
    if read_homefs(image) != invalid.read_bytes():
        raise ValueError("the rejected HomeFS container changed")

    short = scratch / "short.sys"
    write_homefs(short, ((b"payload", bytes(65536)),))
    image = scratch / "short.img"
    create_disk(image, f"{short}:/homefs.sys")
    with Fat16Image(image, FAT_START_LBA) as disk:
        offset, directory, _ = disk._find_entry(None, b"HOMEFS  SYS")
        entry = directory[offset:offset + 32]
        cluster = struct.unpack_from("<H", entry, 26)[0]
        disk._write_fat(cluster, FAT16_EOC)
    check_rejected(image, BUILD / "homefs-restore-short.log", smoke)
    with Fat16Image(image, FAT_START_LBA) as disk:
        offset, directory, _ = disk._find_entry(None, b"HOMEFS  SYS")
        if directory[offset:offset + 32] != entry or disk._read_fat(cluster) != FAT16_EOC:
            raise ValueError("the short HomeFS container was replaced")
    check_import(scratch, smoke)
    check_sparse_length(scratch, smoke)
    print("HomeFS restored the large file and preserved the rejected containers.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=900)
    args = parser.parse_args()
    BUILD.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cupid-homefs-restore-") as temporary:
        check(Path(temporary), args.qemu, args.timeout)

#!/usr/bin/env python3
## @file
#  @brief Build a disk prefix in Cupid OS, compare its bytes, and boot it.
"""Build a FAT16 disk prefix in Cupid OS and boot the resulting image."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from check_guest_runtime import (
    BUILD,
    COMPILER_REAPED,
    FAT_START_LBA,
    ROOT,
    SEED_MANIFEST,
    compare_homefs_files,
    create_disk,
    read_homefs,
    run,
    write_homefs,
)


def check(scratch: Path, root: Path, boot: Path, kernel: Path, qemu: str, timeout: float) -> None:
    reference = scratch / "host-prefix.img"
    subprocess.run([
        ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf", "run", "--root", ROOT,
        "--seed-manifest", ROOT / SEED_MANIFEST,
        "--seed-release", ROOT / "bootstrap/seeds/release.json",
        "--tool", "cupidobj", "--", "disk-template", boot, "--kernel", kernel,
        "--image-sectors", "409600", "--fat-start-lba", str(FAT_START_LBA), "-o", reference,
    ], check=True, timeout=180)
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, (
        (b"cupiddisk.job", b"/disk/boot.bin\n/disk/kern.bin\n/home/disk-prefix.img\n"),
    ))
    image = scratch / "input.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{boot}:/boot.bin", f"{kernel}:/kern.bin",
                f"{BUILD / 'cupiddisk-guest.elf'}:/cupdisk", production_root=root)
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
             "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout),
             "--require-command-completion")
    run(*smoke, "--image", str(image), "--log", str(BUILD / "disk-template-runtime.log"),
        "--command", "exec /disk/cupdisk",
        "--success-pattern", r"\[cupiddisk-guest\] wrote /home/disk-prefix[.]img" + COMPILER_REAPED)

    matched = compare_homefs_files(read_homefs(image), {"disk-prefix.img": reference})
    media = scratch / "guest.img"
    with media.open("xb") as disk:
        disk.write(matched["disk-prefix.img"])
        disk.truncate(200 * 1024 * 1024)
    run(*smoke, "--image", str(media), "--log", str(BUILD / "disk-template-boot.log"),
        "--command", "ls", "--success-pattern", r"\[terminal\] command complete")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--boot", type=Path, help="bootloader bytes for the guest disk job")
    parser.add_argument("--kernel", type=Path, help="raw kernel bytes for the guest disk job")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    root = args.production_root.resolve()
    boot = args.boot.resolve() if args.boot else root / "boot/boot.bin"
    kernel = args.kernel.resolve() if args.kernel else root / "kernel/kernel.bin"
    with tempfile.TemporaryDirectory(prefix="cupid-disk-template-") as temporary:
        check(Path(temporary), root, boot, kernel, args.qemu, args.timeout)


if __name__ == "__main__":
    main()

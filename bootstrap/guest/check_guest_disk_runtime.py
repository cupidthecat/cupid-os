#!/usr/bin/env python3
## @file
#  @brief Verify guest disk output and boot the resulting image.
"""Build boot media in Cupid OS and boot the saved result."""

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


def write_reference_disk(reference: Path, boot: Path, kernel: Path, image_mib: int | None) -> None:
    subprocess.run([
        ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf", "run", "--root", ROOT,
        "--seed-manifest", ROOT / SEED_MANIFEST,
        "--seed-release", ROOT / "bootstrap/seeds/release.json",
        "--tool", "cupidobj", "--", "disk-template", boot, "--kernel", kernel,
        "--image-sectors", str((image_mib or 200) * 2048),
        "--fat-start-lba", str(FAT_START_LBA), "-o", reference,
    ], check=True, timeout=180)
    if image_mib is not None:
        with reference.open("r+b") as disk:
            disk.truncate(image_mib * 1024 * 1024)


def check(scratch: Path, root: Path, boot: Path, kernel: Path, qemu: str, timeout: float,
          *, image_mib: int | None = None) -> None:
    reference = scratch / "checked.img"
    write_reference_disk(reference, boot, kernel, image_mib)
    product = "cupidos.img" if image_mib is not None else "disk-prefix.img"
    option = f"image {image_mib}\n" if image_mib is not None else ""
    request = f"/disk/boot.bin\n/disk/kern.bin\n/home/{product}\n{option}".encode("ascii")
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupiddisk.job", request),))
    image = scratch / "input.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{boot}:/boot.bin", f"{kernel}:/kern.bin",
                f"{BUILD / 'cupiddisk-guest.elf'}:/cupdisk", production_root=root)
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
             "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout),
             "--require-command-completion")
    run(*smoke, "--image", str(image), "--log", str(BUILD / "disk-template-runtime.log"),
        "--command", "exec /disk/cupdisk",
        "--success-pattern", rf"\[cupiddisk-guest\] wrote /home/{product[:-4]}[.]img" + COMPILER_REAPED)

    matched = compare_homefs_files(read_homefs(image), {product: reference})
    boot_disk(scratch / "guest.img", matched[product], smoke, BUILD / "disk-template-boot.log",
              complete=image_mib is not None)


def boot_disk(media: Path, contents: bytes, smoke: tuple[str, ...], log: Path, *, complete: bool) -> None:
    with media.open("xb") as disk:
        disk.write(contents)
        if not complete:
            disk.truncate(200 * 1024 * 1024)
    run(*smoke, "--image", str(media), "--log", str(log),
        "--verify-smp-runtime", "--command", "ls",
        "--success-pattern", r"\[cupidc\] JIT compile: /bin/ls[.]cc.*?\[cupidc\] JIT execution complete")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--boot", type=Path, help="bootloader bytes for the guest disk job")
    parser.add_argument("--kernel", type=Path, help="raw kernel bytes for the guest disk job")
    parser.add_argument("--image-mib", type=int, help="publish a complete disk of this size")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    root = args.production_root.resolve()
    boot = args.boot.resolve() if args.boot else root / "boot/boot.bin"
    kernel = args.kernel.resolve() if args.kernel else root / "kernel/kernel.bin"
    with tempfile.TemporaryDirectory(prefix="cupid-disk-template-") as temporary:
        check(Path(temporary), root, boot, kernel, args.qemu, args.timeout, image_mib=args.image_mib)


if __name__ == "__main__":
    main()

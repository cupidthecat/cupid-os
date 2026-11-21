#!/usr/bin/env python3
## @file
#  @brief Execute the full kernel and disk plan in one guest command.
"""Assemble, link and publish the checked kernel and disk prefix in Cupid OS."""

import argparse
from pathlib import Path
import tempfile

from check_guest_disk_runtime import boot_prefix, write_reference_prefix
from check_guest_runtime import (
    BUILD,
    COMPILER_REAPED,
    ROOT,
    compare_homefs_files,
    create_disk,
    read_homefs,
    run,
    write_homefs,
)


def link_request(output: str, objects: list[str]) -> bytes:
    lines = ("/home/" + output, "script", "/disk/link.ld", *objects)
    return ("\n".join(lines) + "\n").encode("ascii")


def run_kernel_plan(scratch: Path, root: Path, objects: list[Path], qemu: str, timeout: float) -> None:
    aliases = [f"/obj{index // 64}/i{index:03}.o" for index in range(len(objects))]
    link_inputs = ["/disk" + path for path in aliases]
    steps = (
        ("raw", b"boot.job", b"/disk/boot.asm\n/home/boot.bin\nbootloader\n"),
        ("link", b"pass1.job", link_request("pass1.elf", link_inputs)),
        ("symbols", b"symbols.job", b"/home/pass1.elf\n/home/ksyms_data.cc\n"),
        ("compile", b"compile.job", b"/home/ksyms_data.cc\n/home/ksyms_data.o\n"),
        ("link", b"final.job", link_request("kernel.elf", [*link_inputs, "/home/ksyms_data.o"])),
        ("object", b"flatten.job", b"/home/kernel.elf\n/home/kernel.bin\n"),
        ("disk", b"disk.job", b"/home/boot.bin\n/home/kernel.bin\n/home/disk-prefix.img\n"),
    )
    plan = b"".join(operation.encode("ascii") + b"\n/home/" + name + b"\n"
                    for operation, name, _ in steps)
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidbuild.job", plan),
                         (b"ksyms.h", (root / "kernel/cpu/ksyms.h").read_bytes()),
                         (b"types.h", (root / "kernel/core/types.h").read_bytes()),
                         *((name, contents) for _, name, contents in steps)))
    image = scratch / "build.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{root / 'link.ld'}:/link.ld",
                f"{root / 'boot/boot.asm'}:/boot.asm", f"{BUILD / 'cupidbuild-guest.elf'}:/b",
                *(f"{source}:{alias}" for source, alias in zip(objects, aliases)),
                production_root=root)
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.75",
             "--require-command-completion")
    run(*smoke, "--image", str(image), "--log", str(BUILD / "kernel-plan-runtime.log"),
        "--command", "exec /disk/b",
        "--success-pattern", r"\[cupidbuild-guest\] build complete" + COMPILER_REAPED)

    expected_prefix = scratch / "expected-prefix.img"
    write_reference_prefix(expected_prefix, root / "boot/boot.bin", root / "kernel/kernel.bin")
    matched = compare_homefs_files(read_homefs(image), {
        "boot.bin": root / "boot/boot.bin",
        "pass1.elf": root / "kernel/kernel.elf.pass1",
        "ksyms_data.cc": root / "kernel/cpu/ksyms_data.cc",
        "ksyms_data.o": root / "kernel/cpu/ksyms_data.o",
        "kernel.elf": root / "kernel/kernel.elf",
        "kernel.bin": root / "kernel/kernel.bin",
        "disk-prefix.img": expected_prefix,
    })
    boot_prefix(scratch / "sealed.img", matched["disk-prefix.img"], smoke,
                BUILD / "kernel-plan-boot.log")


if __name__ == "__main__":
    from check_guest_kernel_link import production_objects

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    root = args.production_root.resolve()
    inputs = [root / name for name in production_objects(root)[:-1]]
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-plan-") as temporary:
        run_kernel_plan(Path(temporary), root, inputs, args.qemu, args.timeout)

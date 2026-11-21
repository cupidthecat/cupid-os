#!/usr/bin/env python3
## @file
#  @brief Rebuild and boot a kernel with the guest toolchain workers.
"""Run both production kernel links and ELF flattening inside Cupid OS."""

import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

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


def production_objects(root: Path) -> list[str]:
    recipe = subprocess.run(
        ["make", "--no-print-directory", "-n", "-W", "link.ld", "kernel/kernel.elf"],
        cwd=root, check=True, capture_output=True, text=True, timeout=180,
    )
    outputs = {}
    for line in recipe.stdout.splitlines():
        if "--tool cupidld -- -m elf_i386 -T link.ld -o " not in line:
            continue
        command = shlex.split(line)
        output_index = command.index("-o") + 1
        output = command[output_index]
        if output in outputs:
            raise ValueError(f"repeated production kernel link for {output}")
        outputs[output] = command[output_index + 1:]

    pass_one = outputs.get("kernel/kernel.elf.pass1")
    final = outputs.get("kernel/kernel.elf")
    if not pass_one or final != [*pass_one, "kernel/cpu/ksyms_data.o"]:
        raise ValueError("production link recipes differ from the two-pass kernel build")
    for path in final:
        if not path.endswith(".o") or not (root / path).resolve().is_relative_to(root):
            raise ValueError(f"unexpected production object path {path}")
    return final


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    objects = production_objects(root)[:-1]
    # Keep each FAT staging directory within a single directory cluster.
    inputs = [f"/obj{index // 64}/i{index:03}.o" for index in range(len(objects))]
    stage_objects = [f"{root / path}:{alias}" for path, alias in zip(objects, inputs)]
    disk_objects = [f"/disk{alias}" for alias in inputs]

    def link_request(destination: str, cohort: list[str]) -> bytes:
        paths = (destination, "script", "/disk/link.ld", *cohort)
        return ("\n".join(paths) + "\n").encode("ascii")

    homefs = scratch / "homefs.sys"
    write_homefs(homefs, (
        (b"cupidld.job", link_request("/home/pass1.elf", disk_objects)),
        (b"cupidksyms.job", b"/home/pass1.elf\n/home/ksyms_data.cc\n"),
        (b"cupidc.job", b"/home/ksyms_data.cc\n/home/ksyms_data.o\n"),
        (b"ksyms.h", (root / "kernel/cpu/ksyms.h").read_bytes()),
        (b"types.h", (root / "kernel/core/types.h").read_bytes()),
        (b"final.job", link_request("/home/kernel.elf", [*disk_objects, "/home/ksyms_data.o"])),
        (b"cupidobj.job", b"/home/kernel.elf\n/home/kernel.bin\n"),
    ))
    image = scratch / "link.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{root / 'link.ld'}:/link.ld",
                f"{BUILD / 'cupidld-guest.elf'}:/cupld",
                f"{BUILD / 'cupidobj-guest.elf'}:/cupobj",
                f"{BUILD / 'cupidksyms-guest.elf'}:/cupks",
                f"{BUILD / 'cupidc-guest.elf'}:/cupcc",
                *stage_objects, production_root=root)
    completed = r"\[terminal\] command complete"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
             "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout),
             "--require-command-completion")
    run(*smoke, "--image", str(image), "--log", str(BUILD / "kernel-build-runtime.log"),
        "--setup-command", "exec /disk/cupld",
        "--setup-success-pattern", r"\[cupidld-guest\] wrote /home/pass1[.]elf" + COMPILER_REAPED,
        "--setup-command", "exec /disk/cupks",
        "--setup-success-pattern", r"\[cupidksyms-guest\] wrote /home/ksyms_data[.]cc" + COMPILER_REAPED,
        "--setup-command", "exec /disk/cupcc",
        "--setup-success-pattern", r"\[cupidc-guest\] wrote /home/ksyms_data[.]o" + COMPILER_REAPED,
        "--setup-command", "cp /home/final.job /home/cupidld.job",
        "--setup-success-pattern", completed,
        "--setup-command", "exec /disk/cupld",
        "--setup-success-pattern", r"\[cupidld-guest\] wrote /home/kernel[.]elf" + COMPILER_REAPED,
        "--command", "exec /disk/cupobj",
        "--success-pattern", r"\[cupidobj-guest\] wrote /home/kernel[.]bin" + COMPILER_REAPED)

    matched = compare_homefs_files(read_homefs(image), {
        "pass1.elf": root / "kernel/kernel.elf.pass1",
        "ksyms_data.cc": root / "kernel/cpu/ksyms_data.cc",
        "ksyms_data.o": root / "kernel/cpu/ksyms_data.o",
        "kernel.elf": root / "kernel/kernel.elf",
        "kernel.bin": root / "kernel/kernel.bin",
    })
    kernel = scratch / "kernel.bin"
    kernel.write_bytes(matched["kernel.bin"])
    boot_image = scratch / "relinked.img"
    create_disk(boot_image, production_root=root, kernel_image=kernel)
    run(*smoke, "--image", str(boot_image), "--log", str(BUILD / "kernel-build-boot.log"),
        "--command", "ls", "--success-pattern", completed)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT,
                        help="completed checked OS build with both kernel ELF files")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-link-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.qemu, args.timeout)


if __name__ == "__main__":
    main()

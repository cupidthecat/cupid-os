#!/usr/bin/env python3
## @file
#  @brief Compare raw bootloader and AP trampoline bytes assembled in Cupid OS.
"""Assemble the bootloader and AP trampoline inside Cupid OS."""

import argparse
from pathlib import Path
import tempfile

from check_guest_raw_assembly import shift_smp_entry
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


RAW_PRODUCTS = {
    "bootloader": ("boot/boot.asm", "boot/boot.bin"),
    "smp-trampoline": ("kernel/smp/smp_trampoline.S", "kernel/smp_trampoline.bin"),
}
TRAMPOLINE_OBJECT = "kernel/smp/smp_trampoline.o"
RAW_READY = r"\[cupidraw-guest\] wrote /home/raw[.]bin" + COMPILER_REAPED


def assemble_production_raw(scratch: Path, root: Path, mode: str, qemu: str,
                             timeout: float) -> dict[str, bytes]:
    scratch.mkdir()
    homefs = scratch / "homefs.sys"
    worker = f"{BUILD / 'cupidraw-guest.elf'}:/r"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
             "--require-command-completion")
    source, output = RAW_PRODUCTS[mode]
    request = f"/disk/in.asm\n/home/raw.bin\n{mode}\n".encode("ascii")
    jobs = [(b"cupidraw.job", request)]
    stage = [worker, f"{root / source}:/in.asm"]
    expectations = {"raw.bin": output}
    commands = ("--command", "exec /disk/r", "--success-pattern", RAW_READY)
    if mode == "smp-trampoline":
        wrap = b"/home/raw.bin\n/home/smp.o\nwrap-readonly\nsmp_trampoline\n"
        jobs.append((b"cupidobj.job", wrap))
        stage.append(f"{BUILD / 'cupidobj-guest.elf'}:/o")
        expectations["smp.o"] = TRAMPOLINE_OBJECT
        commands = ("--setup-command", "exec /disk/r", "--setup-success-pattern", RAW_READY,
                    "--command", "exec /disk/o", "--success-pattern",
                    r"\[cupidobj-guest\] wrote /home/smp[.]o" + COMPILER_REAPED)
    write_homefs(homefs, tuple(jobs))
    disk = scratch / "raw.img"
    create_disk(disk, f"{homefs}:/homefs.sys", *stage, production_root=root)
    run(*smoke, "--image", str(disk), "--log", str(BUILD / f"kernel-raw-{mode}.log"), *commands)
    matched = compare_homefs_files(read_homefs(disk), {name: root / path for name, path in expectations.items()})
    disk.unlink()
    return {path: matched[name] for name, path in expectations.items()}


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    for mode in RAW_PRODUCTS:
        assemble_production_raw(scratch / mode, root, mode, qemu, timeout)

    smp_source, smp_image = RAW_PRODUCTS["smp-trampoline"]
    changed = scratch / "shifted.asm"
    changed.write_bytes(shift_smp_entry((root / smp_source).read_bytes()))
    request = b"/disk/in.asm\n/home/raw.bin\nsmp-trampoline\n"
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidraw.job", request), (b"raw.bin", (root / smp_image).read_bytes())))
    disk = scratch / "rejected.img"
    worker = f"{BUILD / 'cupidraw-guest.elf'}:/r"
    create_disk(disk, f"{homefs}:/homefs.sys", f"{changed}:/in.asm", worker, production_root=root)
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
        "--require-command-completion", "--image", str(disk),
        "--log", str(BUILD / "kernel-raw-rejected.log"),
        "--command", "exec /disk/r", "--success-pattern",
        r"\[cupidraw-guest\] assembly failed: input" + COMPILER_REAPED)
    compare_homefs_files(read_homefs(disk), {"raw.bin": root / smp_image})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-raw-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.qemu, args.timeout)


if __name__ == "__main__":
    main()

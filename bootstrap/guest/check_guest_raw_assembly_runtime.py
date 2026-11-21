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


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    homefs = scratch / "homefs.sys"
    worker = f"{BUILD / 'cupidraw-guest.elf'}:/r"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
             "--require-command-completion")
    sources = {
        "bootloader": ("boot/boot.asm", "boot/boot.bin"),
        "smp-trampoline": ("kernel/smp/smp_trampoline.S", "kernel/smp_trampoline.bin"),
    }
    raw_ready = r"\[cupidraw-guest\] wrote /home/raw[.]bin" + COMPILER_REAPED
    for mode, (source, output) in sources.items():
        request = f"/disk/in.asm\n/home/raw.bin\n{mode}\n".encode("ascii")
        jobs = [(b"cupidraw.job", request)]
        stage = [worker, f"{root / source}:/in.asm"]
        expectations = {"raw.bin": root / output}
        commands = ("--command", "exec /disk/r", "--success-pattern", raw_ready)
        if mode == "smp-trampoline":
            wrap = b"/home/raw.bin\n/home/smp.o\nwrap-readonly\nsmp_trampoline\n"
            jobs.append((b"cupidobj.job", wrap))
            stage.append(f"{BUILD / 'cupidobj-guest.elf'}:/o")
            expectations["smp.o"] = root / "kernel/smp/smp_trampoline.o"
            commands = ("--setup-command", "exec /disk/r", "--setup-success-pattern", raw_ready,
                        "--command", "exec /disk/o", "--success-pattern",
                        r"\[cupidobj-guest\] wrote /home/smp[.]o" + COMPILER_REAPED)
        write_homefs(homefs, tuple(jobs))
        disk = scratch / f"{mode}.img"
        create_disk(disk, f"{homefs}:/homefs.sys", *stage, production_root=root)
        run(*smoke, "--image", str(disk), "--log", str(BUILD / f"kernel-raw-{mode}.log"), *commands)
        compare_homefs_files(read_homefs(disk), expectations)
        disk.unlink()

    smp_source, smp_image = sources["smp-trampoline"]
    changed = scratch / "shifted.asm"
    changed.write_bytes(shift_smp_entry((root / smp_source).read_bytes()))
    request = b"/disk/in.asm\n/home/raw.bin\nsmp-trampoline\n"
    write_homefs(homefs, ((b"cupidraw.job", request), (b"raw.bin", (root / smp_image).read_bytes())))
    disk = scratch / "rejected.img"
    create_disk(disk, f"{homefs}:/homefs.sys", f"{changed}:/in.asm", worker, production_root=root)
    run(*smoke, "--image", str(disk), "--log", str(BUILD / "kernel-raw-rejected.log"),
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

#!/usr/bin/env python3
## @file
#  @brief Rebuild and inspect the production ELF32 assembly objects in Cupid OS.
"""Assemble the production ISR and context switch in Cupid OS."""

import argparse
from pathlib import Path
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


ASSEMBLY_SOURCES = {
    "isr": "kernel/cpu/isr.asm",
    "switch": "kernel/core/context_switch.asm",
}


def assemble_production_objects(scratch: Path, root: Path, qemu: str,
                                timeout: float) -> dict[str, Path]:
    scratch.mkdir()
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, (
        (b"cupidasm.job", b"/disk/isr.asm\n/home/isr.o\n"),
        (b"switch.job", b"/disk/switch.asm\n/home/switch.o\n"),
        (b"bad.job", b"/disk/bad.asm\n/home/isr.o\n"),
    ))
    invalid = scratch / "bad.asm"
    invalid.write_bytes(b"[BITS 32]\nsection .text\ndb 0x0f\n")
    disk = scratch / "assembly.img"
    create_disk(disk, f"{homefs}:/homefs.sys", f"{invalid}:/bad.asm",
                f"{BUILD / 'cupidasm-guest.elf'}:/a",
                *(f"{root / source}:/{name}.asm" for name, source in ASSEMBLY_SOURCES.items()),
                production_root=root)
    completed = r"\[terminal\] command complete"
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
        "--require-command-completion", "--image", str(disk),
        "--log", str(BUILD / "kernel-assembly-runtime.log"),
        "--setup-command", "exec /disk/a",
        "--setup-success-pattern", r"\[cupidasm-guest\] wrote /home/isr[.]o" + COMPILER_REAPED,
        "--setup-command", "cp /home/switch.job /home/cupidasm.job",
        "--setup-success-pattern", completed,
        "--setup-command", "exec /disk/a",
        "--setup-success-pattern", r"\[cupidasm-guest\] wrote /home/switch[.]o" + COMPILER_REAPED,
        "--setup-command", "cp /home/bad.job /home/cupidasm.job",
        "--setup-success-pattern", completed,
        "--command", "exec /disk/a",
        "--success-pattern", r"\[cupidasm-guest\] object code check failed.*?"
        r"\[cupidasm-guest\] assembly failed: input" + COMPILER_REAPED)

    references = {f"{name}.o": (root / source).with_suffix(".o")
                  for name, source in ASSEMBLY_SOURCES.items()}
    matched = compare_homefs_files(read_homefs(disk), references)
    disk.unlink()
    objects = {}
    for name, reference in references.items():
        output = scratch / name
        output.write_bytes(matched[name])
        objects[reference.relative_to(root).as_posix()] = output
    return objects


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-assembly-") as temporary:
        assemble_production_objects(Path(temporary) / "assemble", args.production_root.resolve(),
                                    args.qemu, args.timeout)


if __name__ == "__main__":
    main()

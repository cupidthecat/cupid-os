#!/usr/bin/env python3
## @file
#  @brief Boot a published disk and check its staged ISO, programs, and Doom.
"""Check an existing release disk on private copies."""

import argparse
from pathlib import Path
import shutil
import tempfile

from check_guest_runtime import BUILD, ROOT, compare_homefs_files, read_homefs, run


LS = r"\[cupidc\] JIT compile: /bin/ls[.]cc.*?\[cupidc\] JIT execution complete"
CAT = r"\[cupidc\] JIT compile: /bin/cat[.]cc.*?\[cupidc\] JIT execution complete"
ISO_OUTPUT = "release-iso.txt"


def check_programs(image: Path, smoke: tuple[str, ...], logs: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="cupid-release-programs-") as directory:
        scratch = Path(directory)
        copy = scratch / "programs.img"
        shutil.copyfile(image, copy)
        run(*smoke, "--image", str(copy), "--log", str(logs / "release-programs.log"),
            "--setup-command", "ls", "--setup-success-pattern", LS,
            "--setup-command", "mount /disk/hello.iso /iso",
            "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/iso'",
            "--setup-command", f"cp /iso/readme.txt /home/{ISO_OUTPUT}",
            "--setup-success-pattern", r"\[terminal\] command complete",
            "--command", "cat /iso/readme.txt", "--success-pattern", CAT)
        expected = scratch / ISO_OUTPUT
        expected.write_bytes((ROOT / "test_iso/fixtures/readme.txt").read_bytes())
        compare_homefs_files(read_homefs(copy), {ISO_OUTPUT: expected})


def check_doom(image: Path, smoke: tuple[str, ...], logs: Path) -> None:
    run(*smoke, "--image", str(image), "--private-image", "--log", str(logs / "release-doom.log"),
        "--setup-command", "ls", "--setup-success-pattern", LS,
        "--command", "doom -iwad /disk/wads/freedo~1.wad -timedemo demo1",
        "--success-pattern", r"timed [0-9]+ gametics in [0-9]+ realtics.*?\[doom\] returned to shell")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="published disk with the checked ISO and Freedoom")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--logs", type=Path, default=BUILD)
    parser.add_argument("--timeout", type=float, default=3600)
    parser.add_argument("--programs-only", action="store_true", help="check only ls and the ISO file readback")
    args = parser.parse_args()

    image = args.image.resolve(strict=True)
    logs = args.logs.resolve()
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", args.qemu, "--cpu", "max", "--smp", "4",
             "--nic", "e1000", "--timeout", str(args.timeout), "--setup-timeout", "180",
             "--key-pause", "0.75", "--require-command-completion", "--verify-smp-runtime")
    check_programs(image, smoke, logs)
    if not args.programs_only:
        check_doom(image, smoke, logs)


if __name__ == "__main__":
    main()

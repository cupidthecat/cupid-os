#!/usr/bin/env python3
## @file
#  @brief Wrap production kernel assets in Cupid OS and compare saved objects.
"""Rebuild an embedded shell command and bitmap inside Cupid OS."""

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


ASSET_SOURCES = {
    "bin/ls.o": "bin/ls.cc",
    "image.bmp.o": "image.bmp",
}


def wrap_production_assets(scratch: Path, root: Path, qemu: str,
                           timeout: float) -> dict[str, Path]:
    scratch.mkdir()
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, (
        (b"cupidobj.job", b"/disk/text.src\n/home/text.o\nwrap-text\n/bin/ls.cc\n"),
        (b"logo.job", b"/disk/logo.bmp\n/home/logo.o\nwrap\n/image.bmp\n"),
        (b"bad.job", b"/disk/text.src\n/home/text.o\nwrap-text\n/../bin/ls.cc\n"),
    ))
    image = scratch / "assets.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{root / ASSET_SOURCES['bin/ls.o']}:/text.src",
                f"{root / ASSET_SOURCES['image.bmp.o']}:/logo.bmp",
                f"{BUILD / 'cupidobj-guest.elf'}:/o", production_root=root)
    completed = r"\[terminal\] command complete"
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
        "--require-command-completion", "--image", str(image),
        "--log", str(BUILD / "kernel-assets-runtime.log"),
        "--setup-command", "exec /disk/o",
        "--setup-success-pattern", r"\[cupidobj-guest\] wrote /home/text[.]o" + COMPILER_REAPED,
        "--setup-command", "cp /home/logo.job /home/cupidobj.job",
        "--setup-success-pattern", completed,
        "--setup-command", "exec /disk/o",
        "--setup-success-pattern", r"\[cupidobj-guest\] wrote /home/logo[.]o" + COMPILER_REAPED,
        "--setup-command", "cp /home/bad.job /home/cupidobj.job",
        "--setup-success-pattern", completed,
        "--command", "exec /disk/o",
        "--success-pattern", r"\[cupidobj-guest\] invalid request file" + COMPILER_REAPED)

    references = {"text.o": "bin/ls.o", "logo.o": "image.bmp.o"}
    matched = compare_homefs_files(read_homefs(image),
                                   {name: root / path for name, path in references.items()})
    image.unlink()
    objects = {}
    for name, path in references.items():
        output = scratch / name
        output.write_bytes(matched[name])
        objects[path] = output
    return objects


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-assets-") as temporary:
        wrap_production_assets(Path(temporary) / "wrap", args.production_root.resolve(),
                               args.qemu, args.timeout)


if __name__ == "__main__":
    main()

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
    "bin/ls.o": ("bin/ls.cc", "wrap-text"),
    "image.bmp.o": ("image.bmp", "wrap"),
}


def wrap_production_assets(scratch: Path, root: Path, qemu: str,
                           timeout: float) -> dict[str, Path]:
    scratch.mkdir()
    homefs = scratch / "homefs.sys"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
             "--require-command-completion", "--command", "exec /disk/o")
    worker = f"{BUILD / 'cupidobj-guest.elf'}:/o"
    objects = {}
    for path, (source, mode) in ASSET_SOURCES.items():
        request = f"/disk/in\n/home/asset.o\n{mode}\n/{source}\n".encode("ascii")
        write_homefs(homefs, ((b"cupidobj.job", request),))
        image = scratch / "asset.img"
        create_disk(image, f"{homefs}:/homefs.sys", f"{root / source}:/in", worker,
                    production_root=root)
        log = BUILD / f"kernel-asset-{Path(source).name}.log"
        run(*smoke, "--image", str(image), "--log", str(log),
            "--success-pattern", r"\[cupidobj-guest\] wrote /home/asset[.]o" + COMPILER_REAPED)
        matched = compare_homefs_files(read_homefs(image), {"asset.o": root / path})
        image.unlink()
        output = scratch / Path(path).name
        output.write_bytes(matched["asset.o"])
        objects[path] = output

    target = "bin/ls.o"
    rejected = b"/disk/in\n/home/asset.o\nwrap-text\n/../bin/ls.cc\n"
    write_homefs(homefs, ((b"cupidobj.job", rejected), (b"asset.o", objects[target].read_bytes())))
    image = scratch / "rejected.img"
    create_disk(image, f"{homefs}:/homefs.sys", worker, production_root=root)
    run(*smoke, "--image", str(image), "--log", str(BUILD / "kernel-asset-rejected.log"),
        "--success-pattern", r"\[cupidobj-guest\] invalid request file" + COMPILER_REAPED)
    compare_homefs_files(read_homefs(image), {"asset.o": root / target})
    image.unlink()
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

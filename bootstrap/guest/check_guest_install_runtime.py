#!/usr/bin/env python3
## @file
#  @brief Generate and compile the production kernel installation sources in Cupid OS.
"""Rebuild the kernel's bin, docs and demos installation objects in Cupid OS."""

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from check_guest_runtime import (
    BUILD,
    COMPILER_REAPED,
    ROOT,
    SEED_MANIFEST,
    compare_homefs_files,
    create_disk,
    read_homefs,
    run,
    write_homefs,
)


INSTALL_SOURCES = tuple(f"kernel/util/{kind}_programs_gen.cc" for kind in ("bin", "docs", "demos"))
INSTALL_HEADERS = (
    "drivers/serial.h", "kernel/core/types.h", "kernel/fs/homefs.h",
    "kernel/fs/ramfs.h", "kernel/fs/vfs.h",
)


def installation_jobs(root: Path) -> dict[str, bytes]:
    source_seed = str(Path(SEED_MANIFEST).with_name("cupidobj.elf"))
    recipe = subprocess.run(["make", "--no-print-directory", "-n", "-W", source_seed,
                             *INSTALL_SOURCES], cwd=root, check=True, capture_output=True,
                            text=True, timeout=180)
    jobs = {}
    for line in recipe.stdout.splitlines():
        _, marker, arguments = line.partition("--tool cupidobj -- install-source ")
        if not marker:
            continue
        kind, *inputs, option, output = shlex.split(arguments)
        if option != "-o" or output not in INSTALL_SOURCES or output in jobs:
            raise ValueError(f"unexpected installation source recipe for {output}")
        paths = (argument if argument.startswith("--") else f"/{argument}" for argument in inputs)
        jobs[output] = ("\n".join((f"/home/{output}", kind, *paths)) + "\n").encode("ascii")
    if set(jobs) != set(INSTALL_SOURCES):
        raise ValueError("production recipes do not generate all three installation sources")
    return jobs


def rebuild_installation_objects(scratch: Path, root: Path, qemu: str,
                                 timeout: float) -> dict[str, Path]:
    scratch.mkdir()
    jobs = installation_jobs(root)
    headers = tuple((path.encode("ascii"), (root / path).read_bytes()) for path in INSTALL_HEADERS)
    homefs = scratch / "homefs.sys"
    installer = f"{BUILD / 'cupidinstall-guest.elf'}:/i"
    compiler = f"{BUILD / 'cupidc-guest.elf'}:/c"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
             "--require-command-completion")
    objects = {}
    for source in INSTALL_SOURCES:
        obj = str(Path(source).with_suffix(".o"))
        compile_job = (f"/home/{source}\n/home/{obj}\ninclude /home/kernel\n"
                       "include /home/kernel/core\ninclude /home/kernel/fs\n").encode("ascii")
        write_homefs(homefs, (*headers, (source.encode("ascii"), b"preceding source\n"),
                              (b"cupidinstall.job", jobs[source]), (b"cupidc.job", compile_job)))
        disk = scratch / "install.img"
        create_disk(disk, f"{homefs}:/homefs.sys", installer, compiler, production_root=root)
        log = BUILD / f"kernel-install-{Path(source).stem}.log"
        run(*smoke, "--image", str(disk), "--log", str(log),
            "--setup-command", "exec /disk/i",
            "--setup-success-pattern", rf"\[cupidinstall-guest\] wrote /home/{re.escape(source)}"
            + COMPILER_REAPED,
            "--command", "exec /disk/c",
            "--success-pattern", rf"\[cupidc-guest\] wrote /home/{re.escape(obj)}" + COMPILER_REAPED)
        matched = compare_homefs_files(read_homefs(disk), {source: root / source, obj: root / obj})
        disk.unlink()
        destination = scratch / Path(obj).name
        destination.write_bytes(matched[obj])
        objects[obj] = destination

    demos = INSTALL_SOURCES[-1]
    rejected = f"/home/{demos}\ndemos\n--demos\n/demos/hello.cc\n".encode("ascii")
    write_homefs(homefs, ((demos.encode("ascii"), matched[demos]), (b"cupidinstall.job", rejected)))
    disk = scratch / "rejected.img"
    create_disk(disk, f"{homefs}:/homefs.sys", installer, production_root=root)
    run(*smoke, "--image", str(disk), "--log", str(BUILD / "kernel-install-rejected.log"),
        "--command", "exec /disk/i",
        "--success-pattern", r"\[cupidinstall-guest\] generation failed: input" + COMPILER_REAPED)
    compare_homefs_files(read_homefs(disk), {demos: root / demos})
    disk.unlink()
    return objects


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=900)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-install-") as temporary:
        rebuild_installation_objects(Path(temporary) / "install", args.production_root.resolve(),
                                     args.qemu, args.timeout)


if __name__ == "__main__":
    main()

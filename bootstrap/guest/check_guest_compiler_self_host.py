#!/usr/bin/env python3
## @file
#  @brief Rebuild the complete current compiler inside Cupid OS and run it.
"""Compile all worker objects inside Cupid OS, link them, and run the new compiler."""

import argparse
from pathlib import Path
import tempfile

from check_guest_asset_bundle import expected_bundle
from check_guest_runtime import (
    BUILD,
    COMPILER_REAPED,
    ROOT,
    compare_homefs_files,
    compare_object_bundle,
    create_disk,
    read_homefs,
    run,
    write_homefs,
)


COMPILER_SOURCES = (
    *(f"toolchain/{name}.cc" for name in (
        "ctool", "elf32", "x86", "cupidc_pp", "cupidc_type", "cupidc_frontend",
        "cupidc_ir", "cupidc_emit",
    )),
    *(f"bootstrap/guest/{name}.cc" for name in (
        "cupidc_engine", "guest_job", "cupidc_guest", "cupidc_bundle",
    )),
    "toolchain/cupidobj.cc",
)


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    aliases = {f"i{index:02}.o": source for index, source in enumerate(COMPILER_SOURCES)}
    compiler = BUILD / "cupidc-guest.elf"
    source_iso = f"{BUILD / 'engsrc.iso'}:/s.iso"
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.75",
             "--require-command-completion", "--setup-command", "mount /disk/s.iso /src",
             "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/src'")
    rebuilt = rebuild_compiler(scratch, root, smoke, aliases, source_iso, compiler)
    confirm_compiler(scratch, root, smoke, source_iso, rebuilt)


def rebuild_compiler(scratch: Path, root: Path, smoke: tuple[str, ...], aliases: dict[str, str],
                     source_iso: str, compiler: Path) -> bytes:
    members = {name: Path(source).with_suffix(".o").name for name, source in aliases.items()}
    reference = expected_bundle(BUILD, scratch / "reference", members, "c.iso")
    source_list = "".join(f"/{source}\n/{name}\n" for name, source in aliases.items())
    link = "\n".join(("/home/new.elf", "0x01e00000", "cupidc_guest_run",
                      *(f"/o/{name}" for name in members))) + "\n"
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidc.job", b"/home/sources.list\n/home/c.iso\nbundle\n"),
                          (b"sources.list", source_list.encode("ascii")),
                          (b"cupidld.job", link.encode("ascii"))))
    image = scratch / "build.img"
    create_disk(image, f"{homefs}:/homefs.sys", source_iso, f"{compiler}:/c",
                f"{BUILD / 'cupidld-guest.elf'}:/l", production_root=root)
    run(*smoke, "--image", str(image), "--log", str(BUILD / "compiler-self-host.log"),
        "--setup-command", "exec /disk/c",
        "--setup-success-pattern", r"\[cupidc-guest\] wrote /home/c[.]iso" + COMPILER_REAPED,
        "--setup-command", "mount /home/c.iso /o",
        "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/o'",
        "--command", "exec /disk/l",
        "--success-pattern", r"\[cupidld-guest\] wrote /home/new[.]elf" + COMPILER_REAPED)

    payload = read_homefs(image)
    compare_object_bundle(payload, scratch, reference, {name: BUILD / obj for name, obj in members.items()})
    matched = compare_homefs_files(payload, {"new.elf": compiler})
    print(f"All {len(members)} compiler objects matched.")
    image.unlink()
    return matched["new.elf"]


def confirm_compiler(scratch: Path, root: Path, smoke: tuple[str, ...],
                     source_iso: str, contents: bytes) -> None:
    rebuilt = scratch / "rebuilt.elf"
    rebuilt.write_bytes(contents)
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidc.job", b"/toolchain/ctool.cc\n/home/check.o\n"),))
    disk = scratch / "run.img"
    create_disk(disk, f"{homefs}:/homefs.sys", source_iso, f"{rebuilt}:/c", production_root=root)
    run(*smoke, "--image", str(disk), "--log", str(BUILD / "compiler-self-host-run.log"),
        "--command", "exec /disk/c",
        "--success-pattern", r"\[cupidc-guest\] wrote /home/check[.]o" + COMPILER_REAPED)
    compare_homefs_files(read_homefs(disk), {"check.o": BUILD / "ctool.o"})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=3600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-compiler-self-host-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.qemu, args.timeout)


if __name__ == "__main__":
    main()

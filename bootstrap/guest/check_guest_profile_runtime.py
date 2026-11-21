#!/usr/bin/env python3
## @file
#  @brief Compare production objects built by the guest and checked host compilers.
"""Compile production kernel and Doom source profiles inside Cupid OS."""

import argparse
from pathlib import Path
import re
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
from source_iso import publish_source_iso
from tools.cupidc_kernel_compile import (
    COMPILER_PROFILE_ARGUMENTS,
    COMPILER_PROFILE_SOURCES,
    _kernel_input_paths,
)


DEFAULT_SOURCES = (
    "kernel/usb/ehci.cc",
    "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/src/sha1.cc",
    "kernel/doom/src/d_main.cc",
)
SOURCE_PROFILES = {
    source: profile
    for profile, sources in COMPILER_PROFILE_SOURCES.items()
    for source in sources
}


def compile_job(source: str, profile: str, output: str) -> bytes:
    lines = ["/" + source, f"/home/{output}"]
    arguments = iter(COMPILER_PROFILE_ARGUMENTS[profile])
    options = {"-D": "define", "-I": "include", "--include-angle": "angle", "-include": "force"}
    for flag in arguments:
        if flag in ("--gnu", "--freestanding"):
            continue
        if flag == "--doom-compat":
            lines.append("doom")
        else:
            lines.append(f"{options[flag]} {next(arguments)}")
    return ("\n".join(lines) + "\n").encode("ascii")


def publish_profile_iso(iso: Path, root: Path, sources: list[str], profile: str) -> None:
    files = {
        path.relative_to(root)
        for source in sources
        for path in _kernel_input_paths(root, source, profile)
    }
    dirs = {parent for path in files for parent in path.parents if parent != Path(".")}
    members = sorted(dirs, key=lambda path: (len(path.parts), path.as_posix())) + sorted(files)
    result = publish_source_iso(
        ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf", root, BUILD / "cupidc-engine.elf",
        iso, tuple(path.as_posix() for path in members),
    )
    if result:
        raise RuntimeError(f"{profile} ISO publication failed with status {result}")


def check(scratch: Path, root: Path, sources: list[str], qemu: str, timeout: float) -> None:
    # Keep the source trees within the checked ISO publisher's entry limit.
    groups: dict[str, list[str]] = {}
    for source in sources:
        groups.setdefault(SOURCE_PROFILES[source], []).append(source)
    images = {}
    for profile, members in groups.items():
        iso = scratch / f"{profile}.iso"
        publish_profile_iso(iso, root, members, profile)
        images[profile] = iso

    for index, source in enumerate(sources):
        profile = SOURCE_PROFILES[source]
        reference = root / Path(source).with_suffix(".o")
        output = reference.name
        homefs = scratch / "homefs.sys"
        write_homefs(homefs, ((b"cupidc.job", compile_job(source, profile, output)),))
        disk = scratch / f"profile-{index}.img"
        create_disk(disk, f"{homefs}:/homefs.sys", f"{images[profile]}:/profile.iso",
                    f"{BUILD / 'cupidc-guest.elf'}:/cupcc", production_root=root)
        pattern = r"\[cupidc-guest\] wrote /home/" + re.escape(output) + COMPILER_REAPED
        log = BUILD / f"production-profile-{source.replace('/', '-')}.log"
        run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
            "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout),
            "--image", str(disk), "--log", str(log), "--require-command-completion",
            "--setup-command", "mount /disk/profile.iso /src",
            "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/src'",
            "--command", "exec /disk/cupcc", "--success-pattern", pattern)
        compare_homefs_files(read_homefs(disk), {output: reference})
        disk.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--source", action="append", metavar="PATH",
                        help="replay a repository-relative production source (repeatable)")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    sources = args.source if args.source is not None else list(DEFAULT_SOURCES)
    if unknown := set(sources) - SOURCE_PROFILES.keys():
        parser.error(f"no checked production profile for {', '.join(sorted(unknown))}")
    if len(set(sources)) != len(sources):
        parser.error("production sources must be distinct")
    with tempfile.TemporaryDirectory(prefix="cupid-profile-") as temporary:
        check(Path(temporary), args.production_root.resolve(), sources, args.qemu, args.timeout)


if __name__ == "__main__":
    main()

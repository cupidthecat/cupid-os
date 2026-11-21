#!/usr/bin/env python3
## @file
#  @brief Compare production objects built by the guest and checked host compilers.
"""Compile production kernel and Doom source profiles inside Cupid OS."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from check_guest_asset_bundle import expected_bundle
from check_guest_runtime import (
    BUILD,
    COMPILER_REAPED,
    ROOT,
    compare_object_bundle,
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
from tools.gui_terminal_smoke import PANIC_RE


DEFAULT_SOURCES = (
    "kernel/usb/ehci.cc",
    "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/src/sha1.cc",
    "kernel/doom/src/d_main.cc",
)
MAX_BATCH_SOURCES = 8
SOURCE_PROFILES = {
    source: profile
    for profile, sources in COMPILER_PROFILE_SOURCES.items()
    for source in sources
}


def profile_options(profile: str) -> list[str]:
    lines = []
    arguments = iter(COMPILER_PROFILE_ARGUMENTS[profile])
    options = {"-D": "define", "-I": "include", "--include-angle": "angle", "-include": "force"}
    for flag in arguments:
        if flag in ("--gnu", "--freestanding"):
            continue
        if flag == "--doom-compat":
            lines.append("doom")
        else:
            lines.append(f"{options[flag]} {next(arguments)}")
    return lines


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


def compile_profile_batch(scratch: Path, root: Path, profile: str, sources: list[str],
                          qemu: str, timeout: float) -> dict[str, Path]:
    iso = scratch / "profile.iso"
    publish_profile_iso(iso, root, sources, profile)
    members = {f"obj{index}.o": Path(source).with_suffix(".o").as_posix()
               for index, source in enumerate(sources)}
    reference = expected_bundle(root, scratch / "reference", members, "objects.iso")
    sources_list = [line for source, output in zip(sources, members) for line in (f"/{source}", f"/{output}")]
    request = ["/home/sources.list", "/home/objects.iso", "bundle", *profile_options(profile)]
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidc.job", ("\n".join(request) + "\n").encode("ascii")),
                          (b"sources.list", ("\n".join(sources_list) + "\n").encode("ascii"))))
    disk = scratch / "profile.img"
    create_disk(disk, f"{homefs}:/homefs.sys", f"{iso}:/p.iso",
                f"{BUILD / 'cupidc-guest.elf'}:/c", production_root=root)
    log = BUILD / f"production-profile-{sources[0].replace('/', '-')}.log"
    mounted = "VFS: mounted 'iso9660' at '/src'"
    command = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
               "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.75",
               "--setup-timeout", "75", "--image", str(disk), "--log", str(log),
               "--require-command-completion", "--setup-command", "mount /disk/p.iso /src",
               "--setup-success-pattern", mounted, "--command", "exec /disk/c",
               "--success-pattern", r"\[cupidc-guest\] wrote /home/objects[.]iso" + COMPILER_REAPED)
    try:
        run(*command)
    except subprocess.CalledProcessError:
        transcript = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
        if (mounted in transcript or "[terminal] command complete" not in transcript or
                PANIC_RE.search(transcript)):
            raise
        print("Retrying the guest command after the source mount was missed.")
        run(*command)

    matched = compare_object_bundle(read_homefs(disk), scratch, reference,
                                    {name: root / obj for name, obj in members.items()})
    objects = {}
    for name, logical_object in members.items():
        destination = scratch.parent / "objects" / logical_object
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(matched[name].read_bytes())
        objects[logical_object] = destination
    print(f"All {len(objects)} guest objects matched profile {profile}.")
    return objects


def compile_production_objects(
    scratch: Path, root: Path, sources: list[str], qemu: str, timeout: float,
    capture_directory: Path | None = None,
) -> dict[str, Path]:
    if not sources:
        return {}
    scratch.mkdir()

    groups: dict[str, list[str]] = {}
    for source in sources:
        groups.setdefault(SOURCE_PROFILES[source], []).append(source)
    objects = {}
    for profile, members in groups.items():
        # Bound ISO membership and the HomeFS output accumulated in each boot.
        for start in range(0, len(members), MAX_BATCH_SOURCES):
            batch = members[start:start + MAX_BATCH_SOURCES]
            with tempfile.TemporaryDirectory(dir=scratch, prefix=f"{profile}-") as temporary:
                compiled = compile_profile_batch(Path(temporary), root, profile, batch, qemu, timeout)
                objects.update(compiled)
                if capture_directory is not None:
                    for logical, product in compiled.items():
                        destination = capture_directory / logical
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        with tempfile.TemporaryDirectory(dir=destination.parent) as private:
                            candidate = Path(private) / destination.name
                            shutil.copyfile(product, candidate)
                            destination.hardlink_to(candidate)
    return objects


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    cohort = parser.add_mutually_exclusive_group()
    cohort.add_argument("--source", action="append", metavar="PATH",
                        help="replay a repository-relative production source (repeatable)")
    cohort.add_argument("--all", action="store_true", help="replay all production profiles")
    parser.add_argument("--capture-objects", type=Path,
                        help="save checked guest objects in a new directory as each group finishes")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    sources = list(SOURCE_PROFILES) if args.all else (args.source or list(DEFAULT_SOURCES))
    if unknown := set(sources) - SOURCE_PROFILES.keys():
        parser.error(f"no checked production profile for {', '.join(sorted(unknown))}")
    if len(set(sources)) != len(sources):
        parser.error("production sources must be distinct")
    if args.capture_objects is not None:
        try:
            args.capture_objects.mkdir(parents=True)
        except OSError as error:
            parser.error(f"cannot create capture directory: {error}")
    with tempfile.TemporaryDirectory(prefix="cupid-profile-") as temporary:
        compile_production_objects(Path(temporary) / "compile", args.production_root.resolve(),
                                   sources, args.qemu, args.timeout, args.capture_objects)


if __name__ == "__main__":
    main()

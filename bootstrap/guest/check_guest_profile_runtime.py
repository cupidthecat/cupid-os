#!/usr/bin/env python3
## @file
#  @brief Compare production objects built by the guest and checked host compilers.
"""Compile production kernel and Doom source profiles inside Cupid OS."""

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
MAX_BATCH_SOURCES = 8
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


def compile_profile_batch(scratch: Path, root: Path, profile: str, sources: list[str],
                          qemu: str, timeout: float) -> dict[str, Path]:
    iso = scratch / "profile.iso"
    publish_profile_iso(iso, root, sources, profile)
    jobs = []
    references = {}
    commands = [("mount /disk/profile.iso /src", r"VFS: mounted 'iso9660' at '/src'")]
    for index, source in enumerate(sources):
        output = f"obj{index}.o"
        job = f"cc{index}.job" if index else "cupidc.job"
        references[output] = root / Path(source).with_suffix(".o")
        jobs.append((job.encode("ascii"), compile_job(source, profile, output)))
        if index:
            commands.append((f"cp /home/{job} /home/cupidc.job", r"\[terminal\] command complete"))
        commands.append(("exec /disk/cupcc", rf"\[cupidc-guest\] wrote /home/obj{index}[.]o" + COMPILER_REAPED))

    homefs = scratch / "homefs.sys"
    write_homefs(homefs, tuple(jobs))
    disk = scratch / "profile.img"
    create_disk(disk, f"{homefs}:/homefs.sys", f"{iso}:/profile.iso",
                f"{BUILD / 'cupidc-guest.elf'}:/cupcc", production_root=root)
    arguments = []
    for command, pattern in commands[:-1]:
        arguments.extend(("--setup-command", command, "--setup-success-pattern", pattern))
    command, pattern = commands[-1]
    log = BUILD / f"production-profile-{sources[0].replace('/', '-')}.log"
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
        "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout),
        "--image", str(disk), "--log", str(log), "--require-command-completion",
        *arguments, "--command", command, "--success-pattern", pattern)

    matched = compare_homefs_files(read_homefs(disk), references)
    objects = {}
    for output, reference in references.items():
        logical_object = reference.relative_to(root)
        destination = scratch.parent / "objects" / logical_object
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(matched[output])
        objects[logical_object.as_posix()] = destination
    return objects


def compile_production_objects(
    scratch: Path, root: Path, sources: list[str], qemu: str, timeout: float,
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
                objects.update(compile_profile_batch(Path(temporary), root, profile, batch, qemu, timeout))
    return objects


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
        compile_production_objects(Path(temporary) / "compile", args.production_root.resolve(),
                                   sources, args.qemu, args.timeout)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
## @file
#  @brief Rebuild and boot the production kernel from source in one guest build.
"""Stage the production sources, run a guest build plan, and boot its disk."""

import argparse
from collections.abc import Callable, Collection, Iterable
from itertools import groupby, islice
from pathlib import Path
import re
import tempfile

from check_guest_assembly_runtime import ASSEMBLY_SOURCES
from check_guest_asset_bundle import production_assets
from check_guest_install_runtime import INSTALL_HEADERS, installation_jobs
from check_guest_kernel_link import production_objects
from check_guest_kernel_plan import execute_kernel_plan
from check_guest_profile_runtime import SOURCE_PROFILES, profile_options
from check_guest_raw_assembly_runtime import RAW_PRODUCTS, TRAMPOLINE_OBJECT
from check_guest_runtime import BUILD, ROOT, build_plan_manifest, create_disk
from source_iso import publish_source_iso
from tools.cupidc_kernel_compile import _kernel_input_paths


DOOM = Path("kernel/doom")
SOURCE_ISOS = (("s.iso", "/src"), ("d.iso", "/src/kernel/doom"))
PLAN_ISO = ("p.iso", "/plan")
REQUEST_DIRECTORY = Path("jobs")
ASSET_DIRECTORY = "assets"
ASSET_ROOT = f"{PLAN_ISO[1]}/{ASSET_DIRECTORY}"
MEDIA_BUILD = "cupidbuild-media.elf"
OBJECT_LIST_LIMIT = 256
INSTALL_INCLUDE_OPTIONS = ("include /src/kernel", "include /src/kernel/core", "include /src/kernel/fs")
KERNEL_BUILD_FILES = frozenset(map(Path, (
    "boot/boot.asm", "link.ld", "kernel/cpu/ksyms.h", "kernel/core/types.h",
)))


def object_name(source: str) -> str:
    return Path(source).with_suffix(".o").as_posix()


def job_contents(*lines: str) -> bytes:
    return ("\n".join(lines) + "\n").encode("ascii")


def iso_members(files: Collection[Path]) -> tuple[str, ...]:
    directories = {parent for path in files for parent in path.parents if parent != Path(".")}
    members = sorted(directories, key=lambda path: (len(path.parts), path.as_posix())) + sorted(files)
    return tuple(path.as_posix() for path in members)


def media_plan_files(steps: tuple[tuple[str, bytes, bytes], ...]) -> dict[Path, bytes]:
    for _, name, _ in steps:
        if not re.fullmatch(rb"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise ValueError(f"invalid media job name {name!r}")
    jobs = tuple((operation, f"{index // 64:02}/".encode("ascii") + name, contents)
                 for index, (operation, name, contents) in enumerate(steps))
    plan = build_plan_manifest(jobs, request_root=b"/plan/jobs/")
    files = {REQUEST_DIRECTORY / name.decode("ascii"): contents for _, name, contents in jobs}
    if len(files) != len(jobs):
        raise ValueError("the guest plan repeats a job path")
    files[Path("cupidbuild.job")] = plan
    return files


def installation_request(source: str, contents: bytes) -> tuple[str, bytes]:
    destination = Path(source).name
    first_line, _, arguments = contents.partition(b"\n")
    if first_line != f"/home/{source}".encode("ascii") or not arguments:
        raise ValueError(f"invalid installation job for {source}")
    return destination, f"/home/{destination}\n".encode("ascii") + arguments


def publish_plan_iso(publisher: Path, root: Path, output: Path,
                     steps: tuple[tuple[str, bytes, bytes], ...],
                     asset_input: tuple[Path, tuple[str, ...]] | None = None) -> int:
    plan_files = media_plan_files(steps)
    asset_paths = {}
    if asset_input:
        asset_root, members = asset_input
        asset_paths[ASSET_DIRECTORY] = asset_root
        asset_paths.update((f"{ASSET_DIRECTORY}/{member}", asset_root / member) for member in members)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix="plan-inputs-") as temporary:
        staging = Path(temporary)
        for relative, contents in plan_files.items():
            destination = staging / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(contents)
        return publish_source_iso(publisher, root, BUILD / "cupidc-engine.elf", output,
                                  (*iso_members(plan_files.keys()), *asset_paths),
                                  source_directory=staging, source_overrides=asset_paths)


def source_plan(root: Path, temporary_objects: bool):
    objects = production_objects(root)[:-1]
    if len(set(objects)) != len(objects):
        raise ValueError("the kernel link repeats a pass-one object")
    output_root = "/tmp" if temporary_objects else "/home"
    aliases = {name: f"{output_root}/i{index:03}.o" for index, name in enumerate(objects)}
    steps = []
    files = set(KERNEL_BUILD_FILES) | set(map(Path, INSTALL_HEADERS))
    assets = set()
    references = {}
    built = set()

    def add(operation: str, name: str, contents: bytes) -> None:
        steps.append((operation, name.encode("ascii"), contents))

    def add_lists(operation: str, prefix: str, entries: Iterable[tuple[str, str, str]],
                  header: Callable[[str], tuple[str, ...]]) -> None:
        index = 0
        for key, group in groupby(entries, key=lambda entry: entry[0]):
            options = header(key)
            while batch := tuple(islice(group, OBJECT_LIST_LIMIT)):
                paths = tuple(path for _, source, obj in batch for path in (source, obj))
                add(operation, f"{prefix}{index}.job", job_contents(*options, *paths))
                index += 1

    def output(name: str) -> str:
        if name in built or name not in aliases:
            raise ValueError(f"unexpected source-plan object {name}")
        built.add(name)
        alias = aliases[name]
        if not temporary_objects:
            references[alias.removeprefix("/home/")] = root / name
        return alias

    for index, source in enumerate(ASSEMBLY_SOURCES.values()):
        files.add(Path(source))
        add("assemble", f"a{index}.job",
            job_contents(f"/src/{source}", output(object_name(source))))

    trampoline, raw = RAW_PRODUCTS["smp-trampoline"]
    files.add(Path(trampoline))
    add("raw", "smp.job", job_contents(f"/src/{trampoline}", "/home/smp.bin", "smp-trampoline"))
    add("object", "wrap-smp.job",
        job_contents("/home/smp.bin", output(TRAMPOLINE_OBJECT), "wrap-readonly", "smp_trampoline"))
    references["smp.bin"] = root / raw

    for index, (source, contents) in enumerate(installation_jobs(root).items()):
        destination, job = installation_request(source, contents)
        add("install", f"i{index}.job", job)
        add("compile", f"install{index}.job", job_contents(
            f"/home/{destination}", output(object_name(source)), *INSTALL_INCLUDE_OPTIONS,
        ))
        references[destination] = root / source

    asset_objects = []
    for obj, (source, mode) in production_assets(root).items():
        assets.add(Path(source))
        asset_objects.append((mode, f"/{source}", output(obj)))
    add_lists("asset-list", "asset", asset_objects, lambda mode: (mode, ASSET_ROOT))

    compiled_objects = []
    for source, profile in SOURCE_PROFILES.items():
        obj = object_name(source)
        if obj not in aliases:
            continue
        files.update(path.relative_to(root) for path in _kernel_input_paths(root, source, profile))
        compiled_objects.append((profile, f"/{source}", output(obj)))
    add_lists("compile-list", "source", compiled_objects,
              lambda profile: (*profile_options(profile), "sources"))

    if missing := aliases.keys() - built:
        raise ValueError(f"no guest source job for {', '.join(sorted(missing))}")
    steps.append(("kernel", b"kernel.job", job_contents(*aliases.values())))
    if len(steps) > 512:
        raise ValueError(f"{len(steps)} steps exceed the guest plan limit")
    doom = {path for path in files if path.is_relative_to(DOOM)}
    inputs = ((root, iso_members((files - doom) | {DOOM})),
              (root / DOOM, iso_members({path.relative_to(DOOM) for path in doom})),
              (root, iso_members(assets)))
    print(f"Staging {len(files | assets)} source files for {len(steps)} guest steps "
          f"and {len(built)} pass-one objects.")
    return tuple(steps), inputs, references


def replay(scratch: Path, root: Path, qemu: str, timeout: float,
           steps: tuple[tuple[str, bytes, bytes], ...],
           sources: tuple[tuple[str, str], ...],
           inputs: tuple[tuple[Path, tuple[str, ...]], ...],
           references: dict[str, Path], staged: tuple[str, ...] = (), *,
           asset_input: tuple[Path, tuple[str, ...]] | None = None) -> None:
    publisher = ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf"
    isos = []
    for (name, _), (source, members) in zip(sources, inputs, strict=True):
        iso = scratch / name
        if publish_source_iso(publisher, root, BUILD / "cupidc-engine.elf", iso, members,
                              source_directory=source):
            raise ValueError(f"could not publish {name}")
        isos.append(f"{iso}:/{name}")
    plan = scratch / PLAN_ISO[0]
    if publish_plan_iso(publisher, root, plan, steps, asset_input):
        raise ValueError(f"could not publish {plan.name}")
    isos.append(f"{plan}:/{plan.name}")
    image = scratch / "build.img"
    create_disk(image, *isos, f"{BUILD / MEDIA_BUILD}:/b", *staged, production_root=root)
    execute_kernel_plan(scratch, root, image, qemu, timeout, references, (*sources, PLAN_ISO))


def check(scratch: Path, root: Path, qemu: str, timeout: float, temporary_objects: bool) -> None:
    steps, inputs, references = source_plan(root, temporary_objects)
    sources, doom, assets = inputs
    replay(scratch, root, qemu, timeout, steps, SOURCE_ISOS, (sources, doom), references,
           asset_input=assets)


def check_kernel_job(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    objects = production_objects(root)[:-1]
    missing = [source for source in objects if not (root / source).is_file()]
    if missing:
        raise ValueError(f"checked pass-one objects are missing: {', '.join(missing)}")
    aliases = tuple(f"/obj{index // 64}/i{index:03}.o" for index in range(len(objects)))
    staged = tuple(f"{root / source}:{alias}" for source, alias in zip(objects, aliases, strict=True))
    steps = (("kernel", b"kernel.job", job_contents(*(f"/disk{alias}" for alias in aliases))),)
    print(f"Staging {len(objects)} checked pass-one objects for the guest kernel job.")
    replay(scratch, root, qemu, timeout, steps, SOURCE_ISOS[:1],
           ((root, iso_members(KERNEL_BUILD_FILES)),), {}, staged)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--temporary-objects", action="store_true",
                      help="keep intermediate link objects in RAMFS")
    mode.add_argument("--link-only", action="store_true",
                      help="stage checked pass-one objects and run the guest kernel job")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=14400)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-source-plan-") as temporary:
        root = args.production_root.resolve()
        if args.link_only:
            check_kernel_job(Path(temporary), root, args.qemu, args.timeout)
        else:
            check(Path(temporary), root, args.qemu, args.timeout, args.temporary_objects)

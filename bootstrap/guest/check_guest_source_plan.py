#!/usr/bin/env python3
## @file
#  @brief Rebuild and boot the production kernel from source in one guest build.
"""Stage the production sources, run a guest build plan, and boot its disk."""

import argparse
from pathlib import Path
import tempfile

from check_guest_assembly_runtime import ASSEMBLY_SOURCES
from check_guest_asset_bundle import production_assets
from check_guest_install_runtime import INSTALL_HEADERS, installation_jobs
from check_guest_kernel_link import production_objects
from check_guest_kernel_plan import execute_kernel_plan, kernel_steps, write_kernel_plan
from check_guest_profile_runtime import SOURCE_PROFILES, profile_options
from check_guest_raw_assembly_runtime import RAW_PRODUCTS, TRAMPOLINE_OBJECT
from check_guest_runtime import BUILD, ROOT, create_disk
from source_iso import publish_source_iso
from tools.cupidc_kernel_compile import _kernel_input_paths


DOOM = Path("kernel/doom")
SOURCE_ISOS = (("s.iso", "/src"), ("d.iso", "/src/kernel/doom"), ("a.iso", "/assets"))


def object_name(source: str) -> str:
    return Path(source).with_suffix(".o").as_posix()


def job_contents(*lines: str) -> bytes:
    return ("\n".join(lines) + "\n").encode("ascii")


def iso_members(files: set[Path]) -> tuple[str, ...]:
    directories = {parent for path in files for parent in path.parents if parent != Path(".")}
    members = sorted(directories, key=lambda path: (len(path.parts), path.as_posix())) + sorted(files)
    return tuple(path.as_posix() for path in members)


def source_plan(root: Path, temporary_objects: bool):
    objects = production_objects(root)[:-1]
    if len(set(objects)) != len(objects):
        raise ValueError("the kernel link repeats a pass-one object")
    output_root = "/tmp" if temporary_objects else "/home"
    aliases = {name: f"{output_root}/i{index:03}.o" for index, name in enumerate(objects)}
    steps = []
    files = set()
    assets = set()
    references = {}
    built = set()

    def add(operation: str, name: str, contents: bytes) -> None:
        steps.append((operation, name.encode("ascii"), contents))

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
        add("install", f"kernel/util/i{index}.job", contents)
        add("compile", f"install{index}.job", job_contents(
            f"/home/{source}", output(object_name(source)), "include /home/kernel",
            "include /home/kernel/core", "include /home/kernel/fs",
        ))
        references[source] = root / source

    for index, (obj, (source, mode)) in enumerate(production_assets(root).items()):
        assets.add(Path(source))
        add("object", f"asset{index}.job", job_contents(f"/assets/{source}", output(obj), mode, f"/{source}"))

    for index, (source, profile) in enumerate(SOURCE_PROFILES.items()):
        obj = object_name(source)
        if obj not in aliases:
            continue
        files.update(path.relative_to(root) for path in _kernel_input_paths(root, source, profile))
        add("compile", f"source{index}.job",
            job_contents(f"/{source}", output(obj), *profile_options(profile)))

    if missing := aliases.keys() - built:
        raise ValueError(f"no guest source job for {', '.join(sorted(missing))}")
    steps.extend(kernel_steps(list(aliases.values())))
    if len(steps) > 512:
        raise ValueError(f"{len(steps)} steps exceed the guest plan limit")
    doom = {path for path in files if path.is_relative_to(DOOM)}
    inputs = ((root, iso_members((files - doom) | {DOOM})),
              (root / DOOM, iso_members({path.relative_to(DOOM) for path in doom})),
              (root, iso_members(assets)))
    print(f"Staging {len(files | assets)} source files for {len(steps)} guest steps "
          f"and {len(built)} pass-one objects.")
    return tuple(steps), inputs, references


def check(scratch: Path, root: Path, qemu: str, timeout: float, temporary_objects: bool) -> None:
    steps, inputs, references = source_plan(root, temporary_objects)
    publisher = ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf"
    isos = []
    for (name, _), (source, members) in zip(SOURCE_ISOS, inputs):
        iso = scratch / name
        if publish_source_iso(publisher, root, BUILD / "cupidc-engine.elf", iso, members,
                              source_directory=source):
            raise ValueError(f"could not publish {name}")
        isos.append(f"{iso}:/{name}")
    headers = tuple((path.encode("ascii"), (root / path).read_bytes()) for path in INSTALL_HEADERS)
    homefs = scratch / "homefs.sys"
    write_kernel_plan(homefs, root, steps, headers)
    image = scratch / "build.img"
    create_disk(image, f"{homefs}:/homefs.sys", *isos,
                f"{root / 'link.ld'}:/link.ld", f"{root / 'boot/boot.asm'}:/boot.asm",
                f"{BUILD / 'cupidbuild-guest.elf'}:/b", production_root=root)
    execute_kernel_plan(scratch, root, image, qemu, timeout, references, SOURCE_ISOS)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--temporary-objects", action="store_true",
                        help="keep intermediate link objects in RAMFS")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=14400)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-source-plan-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.qemu, args.timeout,
              args.temporary_objects)

#!/usr/bin/env python3
## @file
#  @brief Rebuild and boot the production kernel from source in one guest build.
"""Stage the production sources, run a guest build plan, and boot its disk."""

import argparse
from collections.abc import Callable, Collection, Iterable
from itertools import groupby, islice
from pathlib import Path
import re
import subprocess
import tempfile

from check_guest_assembly_runtime import ASSEMBLY_SOURCES
from check_guest_asset_bundle import production_assets
from check_guest_install_runtime import INSTALL_HEADERS, installation_jobs
from check_guest_kernel_link import production_objects
from check_guest_kernel_plan import execute_kernel_plan
from check_guest_profile_runtime import SOURCE_PROFILES, profile_options
from check_guest_raw_assembly_runtime import RAW_PRODUCTS, TRAMPOLINE_OBJECT
from check_guest_runtime import BUILD, ROOT, SEED_MANIFEST, build_plan_manifest, create_disk, write_homefs
from source_iso import publish_source_iso
from tools.cupidc_kernel_compile import _kernel_input_paths
from tools.hostbuild_disk import StageFile


DOOM = Path("kernel/doom")
SOURCE_ISOS = (("s.iso", "/src"), ("d.iso", "/src/kernel/doom"))
PLAN_ISO = ("p.iso", "/plan")
REQUEST_DIRECTORY = Path("jobs")
ASSET_DIRECTORY = "assets"
ASSET_ROOT = f"{PLAN_ISO[1]}/{ASSET_DIRECTORY}"
MEDIA_BUILD = "cupidbuild-media.elf"
INSTALLED_ROOT = "/home/project"
INSTALLED_BUILD_NAME = "build"
INSTALLED_BUILD = f"{INSTALLED_ROOT}/{INSTALLED_BUILD_NAME}"
PROJECT_LAYOUT = Path("bootstrap/guest/kernel.layout")
BUILD_STEP_LIMIT = 512
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


def media_plan_files(steps: tuple[tuple[str, bytes, bytes], ...], *,
                     request_root: bytes = b"/plan/jobs/") -> dict[Path, bytes]:
    for _, name, _ in steps:
        if not re.fullmatch(rb"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise ValueError(f"invalid media job name {name!r}")
    jobs = tuple((operation, f"{index // 64:02}/".encode("ascii") + name, contents)
                 for index, (operation, name, contents) in enumerate(steps))
    plan = build_plan_manifest(jobs, request_root=request_root)
    files = {REQUEST_DIRECTORY / name.decode("ascii"): contents for _, name, contents in jobs}
    if len(files) != len(jobs):
        raise ValueError("the guest plan repeats a job path")
    files[Path("cupidbuild.job")] = plan
    return files


def installation_request(source: str, contents: bytes, directory: str = "/home") -> tuple[str, bytes]:
    destination = Path(source).name
    first_line, _, arguments = contents.partition(b"\n")
    if first_line != f"/home/{source}".encode("ascii") or not arguments:
        raise ValueError(f"invalid installation job for {source}")
    return destination, f"{directory}/{destination}\n".encode("ascii") + arguments


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


def image_options(image_mib: int | None, stage_assets: bool) -> tuple[str, ...]:
    options = (f"image {image_mib}",) if image_mib is not None else ()
    if stage_assets:
        options += ("stage disk-assets",)
    return options


def source_plan(root: Path, temporary_objects: bool, *, installed_tree: bool = False,
                image_mib: int | None = None, stage_assets: bool = False):
    objects = production_objects(root)[:-1]
    if len(set(objects)) != len(objects):
        raise ValueError("the kernel link repeats a pass-one object")
    product_dir = INSTALLED_BUILD if installed_tree else "/home"
    output_root = "/tmp" if temporary_objects else product_dir
    aliases = {name: f"{output_root}/i{index:03}.o" for index, name in enumerate(objects)}
    steps = []
    files = set(KERNEL_BUILD_FILES) | set(map(Path, INSTALL_HEADERS))
    assets = set()
    references = {}
    built = set()
    source_root = INSTALLED_ROOT if installed_tree else "/src"
    source_option = ("root .",) if installed_tree else ()
    output_option = (f"output {INSTALLED_BUILD_NAME}",) if installed_tree else ()
    disk_option = image_options(image_mib, stage_assets)

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
            job_contents(f"{source_root}/{source}", output(object_name(source))))

    trampoline, raw = RAW_PRODUCTS["smp-trampoline"]
    files.add(Path(trampoline))
    smp_path = f"{product_dir}/smp.bin"
    add("raw", "smp.job", job_contents(f"{source_root}/{trampoline}", smp_path, "smp-trampoline"))
    add("object", "wrap-smp.job",
        job_contents(smp_path, output(TRAMPOLINE_OBJECT), "wrap-readonly", "smp_trampoline"))
    references[smp_path.removeprefix("/home/")] = root / raw

    for index, (source, contents) in enumerate(installation_jobs(root).items()):
        destination, job = installation_request(source, contents, product_dir)
        generated_path = f"{product_dir}/{destination}"
        add("install", f"i{index}.job", job)
        add("compile", f"install{index}.job", job_contents(
            generated_path, output(object_name(source)),
            *(option.replace("/src", source_root, 1) for option in INSTALL_INCLUDE_OPTIONS),
        ))
        references[generated_path.removeprefix("/home/")] = root / source

    asset_objects = []
    for obj, (source, mode) in production_assets(root).items():
        assets.add(Path(source))
        asset_objects.append((mode, f"/{source}", output(obj)))
    add_lists("asset-list", "asset", asset_objects,
              lambda mode: (mode, source_root if installed_tree else ASSET_ROOT))

    compiled_objects = []
    for source, profile in SOURCE_PROFILES.items():
        obj = object_name(source)
        if obj not in aliases:
            continue
        files.update(path.relative_to(root) for path in _kernel_input_paths(root, source, profile))
        compiled_objects.append((profile, f"/{source}", output(obj)))
    add_lists("compile-list", "source", compiled_objects,
              lambda profile: (*source_option, *profile_options(profile), "sources"))

    if missing := aliases.keys() - built:
        raise ValueError(f"no guest source job for {', '.join(sorted(missing))}")
    steps.append(("kernel", b"kernel.job", job_contents(*source_option, *output_option, *disk_option, *aliases.values())))
    step_count = len(steps) + int(installed_tree)
    if step_count > BUILD_STEP_LIMIT:
        raise ValueError(f"{step_count} steps exceed the guest plan limit")
    doom = {path for path in files if path.is_relative_to(DOOM)}
    inputs = ((root, iso_members((files - doom) | {DOOM})),
              (root / DOOM, iso_members({path.relative_to(DOOM) for path in doom})),
              (root, iso_members(assets)))
    print(f"Staging {len(files | assets)} source files for {step_count} guest steps "
          f"and {len(built)} pass-one objects.")
    return tuple(steps), inputs, references


def project_layout(root: Path) -> bytes:
    """Check the native layout against the current production recipes."""
    objects = production_objects(root)[:-1]
    roles = {}
    lines = ["cupid-kernel 1"]

    for source, request in installation_jobs(root).items():
        _, kind, *options = request.decode("ascii").splitlines()
        roles[object_name(source)] = (kind, source)
        lines.extend((kind, *options, "end"))
    for profile in dict.fromkeys(SOURCE_PROFILES.values()):
        lines.extend((profile, *profile_options(profile), "end"))
    for source, profile in SOURCE_PROFILES.items():
        if object_name(source) in objects:
            roles[object_name(source)] = (profile, source)
    for source in ASSEMBLY_SOURCES.values():
        roles[object_name(source)] = ("assemble", source)
    roles[TRAMPOLINE_OBJECT] = ("trampoline", RAW_PRODUCTS["smp-trampoline"][0])
    roles.update((obj, (mode, source)) for obj, (source, mode) in production_assets(root).items())

    if set(roles) != set(objects) or len(set(objects)) != len(objects):
        raise ValueError("the layout and production pass-one objects differ")
    lines.append("objects")
    for obj in objects:
        role, source = roles[obj]
        lines.append(f"{role} /{source}")
    return job_contents(*lines)


def native_plan_files(root: Path, *, temporary_objects: bool, image_mib: int | None,
                      stage_assets: bool) -> dict[Path, bytes]:
    layout = (root / PROJECT_LAYOUT).read_bytes()
    if layout != project_layout(root):
        raise ValueError("kernel layout differs from production. Run --write-project-layout and review the changes.")
    options = ("objects tmp",) if temporary_objects else ()
    return {
        PROJECT_LAYOUT: layout,
        Path("cupidbuild.job"): job_contents("project-kernel", "kernel-project.job"),
        Path("kernel-project.job"): job_contents(PROJECT_LAYOUT.as_posix(), *options,
                                                *image_options(image_mib, stage_assets)),
    }


def stage_installed_tree(scratch: Path, root: Path, image: Path,
                         steps: tuple[tuple[str, bytes, bytes], ...],
                         sources: Collection[Path], *staged: str, image_assets: tuple[StageFile, ...] = (),
                         native_plan: dict[Path, bytes] | None = None) -> None:
    if native_plan is None:
        if len(steps) >= BUILD_STEP_LIMIT:
            raise ValueError(f"{len(steps) + 1} steps exceed the guest plan limit")
        plan = media_plan_files(steps, request_root=b"jobs/")
        manifest = Path("cupidbuild.job")
        plan[manifest] = job_contents("mkdir", INSTALLED_BUILD_NAME) + plan[manifest]
    else:
        plan = native_plan
    project = INSTALLED_ROOT.removeprefix("/home/")
    files = [(f"{project}/{path.as_posix()}".encode("ascii"), contents)
             for path, contents in plan.items()]
    files.extend((f"{project}/{source.as_posix()}".encode("ascii"), (root / source).read_bytes())
                 for source in sorted(sources))
    if image_assets:
        pairs = []
        for index, asset in enumerate(image_assets):
            name = f"disk-inputs/{index}"
            files.append((f"{project}/{name}".encode("ascii"), asset.source.read_bytes()))
            pairs.extend((name, asset.dest))
        files.append((f"{project}/disk-assets".encode("ascii"), job_contents(*pairs)))
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, tuple(files))
    create_disk(image, f"{homefs}:/homefs.sys", f"{BUILD / 'cupidbuild-guest.elf'}:/b",
                *staged, production_root=root)


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


def check_installed(scratch: Path, root: Path, qemu: str, timeout: float, temporary_objects: bool,
                    *, image_mib: int | None = None, image_assets: tuple[StageFile, ...] = (),
                    native_project: bool = False, release_output: Path | None = None) -> None:
    steps, inputs, references = source_plan(root, temporary_objects, installed_tree=True, image_mib=image_mib,
                                            stage_assets=bool(image_assets))
    sources = {(base / member).relative_to(root) for base, members in inputs for member in members
               if not (base / member).is_dir()}
    image = scratch / "build.img"
    native_plan = native_plan_files(root, temporary_objects=temporary_objects, image_mib=image_mib,
                                    stage_assets=bool(image_assets)) if native_project else None
    if native_plan is not None:
        print(f"Staging the checked kernel layout from {PROJECT_LAYOUT}; CupidBuild will prepare the requests.")
    stage_installed_tree(scratch, root, image, steps, sources, image_assets=image_assets,
                         native_plan=native_plan)
    execute_kernel_plan(scratch, root, image, qemu, timeout, references,
                        project_directory=INSTALLED_ROOT, output_directory=INSTALLED_BUILD,
                        image_mib=image_mib, image_assets=image_assets, release_output=release_output)


def verify_pass_one(scratch: Path, root: Path, objects: list[str]) -> None:
    checked = root / "kernel/kernel.elf.pass1"
    replayed = scratch / "checked-pass-one.elf"
    subprocess.run([
        ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf", "run", "--root", ROOT,
        "--seed-manifest", ROOT / SEED_MANIFEST,
        "--seed-release", ROOT / "bootstrap/seeds/release.json",
        "--tool", "cupidld", "--", "-m", "elf_i386", "-T", root / "link.ld",
        "-o", replayed, *(root / path for path in objects),
    ], check=True, timeout=180)
    if replayed.read_bytes() != checked.read_bytes():
        raise ValueError("staged objects differ from the checked pass-one link. Rebuild the reference kernel.")


def check_kernel_job(scratch: Path, root: Path, qemu: str, timeout: float, *,
                     installed_tree: bool = False, image_mib: int | None = None,
                     image_assets: tuple[StageFile, ...] = (), release_output: Path | None = None) -> None:
    objects = production_objects(root)[:-1]
    missing = [source for source in objects if not (root / source).is_file()]
    if missing:
        raise ValueError(f"checked pass-one objects are missing: {', '.join(missing)}")
    verify_pass_one(scratch, root, objects)
    aliases = tuple(f"/obj{index // 64}/i{index:03}.o" for index in range(len(objects)))
    staged = tuple(f"{root / source}:{alias}" for source, alias in zip(objects, aliases, strict=True))
    options = ("root .", f"output {INSTALLED_BUILD_NAME}") if installed_tree else ()
    options += image_options(image_mib, bool(image_assets))
    steps = (("kernel", b"kernel.job",
              job_contents(*options, *(f"/disk{alias}" for alias in aliases))),)
    print(f"Staging {len(objects)} checked pass-one objects for the guest kernel job.")
    if installed_tree:
        image = scratch / "build.img"
        stage_installed_tree(scratch, root, image, steps, KERNEL_BUILD_FILES, *staged,
                             image_assets=image_assets)
        execute_kernel_plan(scratch, root, image, qemu, timeout, {},
                            project_directory=INSTALLED_ROOT, output_directory=INSTALLED_BUILD,
                            image_mib=image_mib, image_assets=image_assets, release_output=release_output)
        return
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
    mode.add_argument("--check-project-layout", action="store_true",
                      help="check the installed kernel layout against the production recipes")
    mode.add_argument("--write-project-layout", action="store_true",
                      help="refresh the checked kernel layout from the production recipes")
    parser.add_argument("--native-project", action="store_true",
                        help="prepare kernel requests in CupidBuild from the checked layout")
    parser.add_argument("--installed-tree", action="store_true",
                        help="put the project and build plan in HomeFS")
    parser.add_argument("--image-mib", type=int, help="build a complete disk in the installed project")
    parser.add_argument("--stage-iso", action="store_true", help="stage the checked hello.iso")
    parser.add_argument("--stage-wad", type=Path, help="stage a Freedoom WAD at /wads/freedoom1.wad")
    parser.add_argument("--release-output", type=Path, help="save the complete guest image after comparison and boot")
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=14400)
    args = parser.parse_args()
    if args.native_project and (not args.installed_tree or args.link_only):
        parser.error("--native-project requires an installed source rebuild")
    if args.image_mib is not None and not args.installed_tree:
        parser.error("--image-mib requires --installed-tree")
    if (args.stage_iso or args.stage_wad or args.release_output) and args.image_mib is None:
        parser.error("--stage-iso, --stage-wad, and --release-output require --image-mib")
    root = args.production_root.resolve()
    if args.write_project_layout or args.check_project_layout:
        expected = project_layout(root)
        layout = root / PROJECT_LAYOUT
        if args.write_project_layout:
            layout.write_bytes(expected)
        elif layout.read_bytes() != expected:
            parser.error(f"{PROJECT_LAYOUT} differs from production. Run --write-project-layout and review the changes.")
        print(f"{PROJECT_LAYOUT} matches the production recipes.")
        raise SystemExit(0)
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-source-plan-") as temporary:
        image_assets = (StageFile(root / "test_iso/hello.iso", "/hello.iso"),) if args.stage_iso else ()
        if args.stage_wad:
            image_assets += (StageFile(args.stage_wad.resolve(strict=True), "/wads/freedoom1.wad"),)
        if args.link_only:
            check_kernel_job(Path(temporary), root, args.qemu, args.timeout,
                             installed_tree=args.installed_tree, image_mib=args.image_mib, image_assets=image_assets,
                             release_output=args.release_output)
        elif args.installed_tree:
            check_installed(Path(temporary), root, args.qemu, args.timeout, args.temporary_objects,
                            image_mib=args.image_mib, image_assets=image_assets, native_project=args.native_project,
                            release_output=args.release_output)
        else:
            check(Path(temporary), root, args.qemu, args.timeout, args.temporary_objects)

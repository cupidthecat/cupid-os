#!/usr/bin/env python3
## @file
#  @brief Rebuild the ordinary production asset objects in a single guest job.
"""Rebuild every ordinary kernel asset object and read it back from an ISO."""

import argparse
from pathlib import Path
import shlex
import shutil
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
from source_iso import publish_command, stage_seed_pair, write_manifest


def production_assets(root: Path) -> dict[str, tuple[str, str]]:
    seed = str(Path(SEED_MANIFEST).with_name("cupidobj.elf"))
    recipe = subprocess.run(["make", "--no-print-directory", "-n", "-W", seed, "kernel/kernel.elf"],
                            cwd=root, check=True, capture_output=True, text=True, timeout=180)
    assets = {}
    for line in recipe.stdout.splitlines():
        _, marker, arguments = line.partition("--tool cupidobj -- ")
        if not marker:
            continue
        command = shlex.split(arguments)
        if not command or command[0] not in ("wrap", "wrap-text"):
            continue
        if command == ["wrap", "kernel/smp_trampoline.bin", "--stem", "smp_trampoline",
                       "--section", ".rodata", "--readonly", "-o", "kernel/smp/smp_trampoline.o"]:
            continue
        if len(command) != 4 or command[2] != "-o":
            raise ValueError(f"unexpected asset recipe {arguments}")
        mode, source, _, obj = command
        for path in (source, obj):
            if Path(path).is_absolute() or not (root / path).resolve().is_relative_to(root):
                raise ValueError(f"asset recipe path leaves the build root: {path}")
        if obj in assets or not obj.endswith(".o"):
            raise ValueError(f"unexpected asset output {obj}")
        assets[obj] = (source, mode)
    if not assets:
        raise ValueError("no ordinary kernel asset recipes were found")
    return assets


def expected_bundle(root: Path, scratch: Path, members: dict[str, str]) -> Path:
    fixtures = scratch / "fixtures"
    fixtures.mkdir(parents=True)
    for name, obj in members.items():
        shutil.copyfile(root / obj, fixtures / name)
    write_manifest(scratch / "manifest", members)
    stage_seed_pair(ROOT, scratch)
    publisher = ROOT / Path(SEED_MANIFEST).with_name("cupidbuild.elf")
    subprocess.run(publish_command(publisher, scratch, "assets.iso"), check=True, timeout=180)
    return scratch / "assets.iso"


def wrap_production_bundle(scratch: Path, root: Path, qemu: str,
                           timeout: float) -> dict[str, Path]:
    scratch.mkdir()
    assets = production_assets(root)
    members = {}
    manifest = []
    staged = []
    for index, (obj, (source, mode)) in enumerate(assets.items()):
        name = f"i{index:03}.o"
        input_alias = f"/in{index // 64}/i{index:03}"
        members[name] = obj
        manifest.extend((f"/disk{input_alias}", f"/{name}", mode, f"/{source}"))
        staged.append(f"{root / source}:{input_alias}")
    reference = expected_bundle(root, scratch / "reference", members)
    request = b"/home/assets.list\n/home/assets.iso\nwrap-bundle\n"
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidobj.job", request),
                          (b"assets.list", ("\n".join(manifest) + "\n").encode("ascii"))))
    disk = scratch / "bundle.img"
    worker = f"{BUILD / 'cupidobj-guest.elf'}:/o"
    create_disk(disk, f"{homefs}:/homefs.sys", worker, *staged, production_root=root)
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.5",
        "--require-command-completion", "--image", str(disk),
        "--log", str(BUILD / "kernel-asset-bundle.log"), "--command", "exec /disk/o",
        "--success-pattern", r"\[cupidobj-guest\] wrote /home/assets[.]iso" + COMPILER_REAPED)

    matched = compare_homefs_files(read_homefs(disk), {"assets.iso": reference})
    disk.unlink()
    iso = scratch / "guest.iso"
    iso.write_bytes(matched["assets.iso"])
    objects = scratch / "objects"
    subprocess.run(["xorriso", "-osirrox", "on", "-indev", iso, "-extract", "/", objects],
                   check=True, capture_output=True, timeout=60)
    if {path.name for path in objects.iterdir()} != set(members):
        raise ValueError("guest asset ISO has a different object inventory")
    for name, obj in members.items():
        if (objects / name).read_bytes() != (root / obj).read_bytes():
            raise ValueError(f"guest asset ISO contains different bytes for {obj}")
    print(f"All {len(members)} guest asset objects matched the checked kernel inputs.")
    return {obj: objects / name for name, obj in members.items()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-asset-bundle-") as temporary:
        wrap_production_bundle(Path(temporary) / "wrap", args.production_root.resolve(),
                               args.qemu, args.timeout)


if __name__ == "__main__":
    main()

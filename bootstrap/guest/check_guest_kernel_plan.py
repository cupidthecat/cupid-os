#!/usr/bin/env python3
## @file
#  @brief Execute the full kernel and disk plan in one guest command.
"""Assemble, link and publish the checked kernel and disk prefix in Cupid OS."""

import argparse
from pathlib import Path
import subprocess
import tempfile

from check_guest_disk_runtime import boot_disk, write_reference_disk
from check_guest_runtime import (
    BUILD,
    BUILD_FAILURE_PATTERN,
    COMPILER_REAPED,
    FAT_START_LBA,
    ROOT,
    build_plan_manifest,
    compare_homefs_files,
    create_disk,
    read_homefs,
    run,
    write_homefs,
)
from tools.gui_terminal_smoke import PANIC_RE
from tools.hostbuild_disk import StageFile, stage_files


def link_request(output: str, objects: list[str], script: str) -> bytes:
    lines = ("/home/" + output, "script", script, *objects)
    return ("\n".join(lines) + "\n").encode("ascii")


def kernel_steps(link_inputs: list[str]) -> tuple[tuple[str, bytes, bytes], ...]:
    script = "/disk/link.ld"
    return (
        ("raw", b"boot.job", b"/disk/boot.asm\n/home/boot.bin\nbootloader\n"),
        ("link", b"pass1.job", link_request("pass1.elf", link_inputs, script)),
        ("symbols", b"symbols.job", b"/home/pass1.elf\n/home/ksyms_data.cc\n"),
        ("compile", b"compile.job", b"/home/ksyms_data.cc\n/home/ksyms_data.o\n"),
        ("link", b"final.job", link_request("kernel.elf", [*link_inputs, "/home/ksyms_data.o"], script)),
        ("object", b"flatten.job", b"/home/kernel.elf\n/home/kernel.bin\n"),
        ("disk", b"disk.job", b"/home/boot.bin\n/home/kernel.bin\n/home/disk-prefix.img\n"),
    )


def write_kernel_plan(homefs: Path, root: Path, steps: tuple[tuple[str, bytes, bytes], ...],
                      inputs: tuple[tuple[bytes, bytes], ...] = ()) -> None:
    write_homefs(homefs, ((b"cupidbuild.job", build_plan_manifest(steps)),
                         (b"ksyms.h", (root / "kernel/cpu/ksyms.h").read_bytes()),
                         (b"types.h", (root / "kernel/core/types.h").read_bytes()),
                         *((name, contents) for _, name, contents in steps), *inputs))


def execute_kernel_plan(scratch: Path, root: Path, image: Path, qemu: str, timeout: float,
                        extra_references: dict[str, Path],
                        mounts: tuple[tuple[str, str], ...] = (), *,
                        project_directory: str | None = None, output_directory: str = "/home",
                        image_mib: int | None = None, image_assets: tuple[StageFile, ...] = ()) -> None:
    smoke = ("tools/gui_terminal_smoke.py", "--qemu", qemu, "--cpu", "max",
             "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.75",
             "--require-command-completion")
    setup = ["--setup-timeout", "75"]
    markers = []
    for iso, mount in mounts:
        marker = f"VFS: mounted 'iso9660' at '{mount}'"
        markers.append(marker)
        setup.extend(("--setup-command", f"mount /disk/{iso} {mount}",
                      "--setup-success-pattern", marker))
    if project_directory:
        setup.extend(("--setup-command", f"cd {project_directory}",
                      "--setup-success-pattern", r"\[terminal\] command complete"))
    log = "kernel-installed-tree" if project_directory else "kernel-source-plan" if mounts else "kernel-plan"
    runtime_log = BUILD / f"{log}-runtime.log"
    command = (*smoke, "--smp", "1" if mounts or project_directory else "4", *setup,
               "--image", str(image), "--log", str(runtime_log),
               "--command", "exec /disk/b", "--failure-pattern", BUILD_FAILURE_PATTERN,
               "--success-pattern", r"\[cupidbuild-guest\] build complete" + COMPILER_REAPED)
    try:
        run(*command)
    except subprocess.CalledProcessError:
        transcript = runtime_log.read_text(encoding="utf-8", errors="replace") if runtime_log.is_file() else ""
        if (all(marker in transcript for marker in markers) or
                "[terminal] command complete" not in transcript or PANIC_RE.search(transcript)):
            raise
        print("Retrying the guest build after a source mount was missed.")
        run(*command)

    disk_name = "cupidos.img" if image_mib is not None else "disk-prefix.img"
    expected_disk = scratch / "expected.img"
    write_reference_disk(expected_disk, root / "boot/boot.bin", root / "kernel/kernel.bin", image_mib)
    if image_assets:
        stage_files(expected_disk, FAT_START_LBA, list(image_assets))
    products = {
        "boot.bin": root / "boot/boot.bin",
        "pass1.elf": root / "kernel/kernel.elf.pass1",
        "ksyms_data.cc": root / "kernel/cpu/ksyms_data.cc",
        "ksyms_data.o": root / "kernel/cpu/ksyms_data.o",
        "kernel.elf": root / "kernel/kernel.elf",
        "kernel.bin": root / "kernel/kernel.bin",
        disk_name: expected_disk,
    }
    prefix = output_directory.removeprefix("/home").lstrip("/")
    prefix = f"{prefix}/" if prefix else ""
    matched = compare_homefs_files(read_homefs(image), {
        **{prefix + name: path for name, path in products.items()}, **extra_references,
    })
    boot_disk(scratch / "sealed.img", matched[prefix + disk_name], (*smoke, "--smp", "4"),
              BUILD / f"{log}-boot.log", complete=image_mib is not None)


def run_kernel_plan(scratch: Path, root: Path, objects: list[Path], qemu: str, timeout: float) -> None:
    aliases = [f"/obj{index // 64}/i{index:03}.o" for index in range(len(objects))]
    steps = kernel_steps(["/disk" + path for path in aliases])
    homefs = scratch / "homefs.sys"
    write_kernel_plan(homefs, root, steps)
    image = scratch / "build.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{root / 'link.ld'}:/link.ld",
                f"{root / 'boot/boot.asm'}:/boot.asm", f"{BUILD / 'cupidbuild-guest.elf'}:/b",
                *(f"{source}:{alias}" for source, alias in zip(objects, aliases)),
                production_root=root)
    execute_kernel_plan(scratch, root, image, qemu, timeout, {})


if __name__ == "__main__":
    from check_guest_kernel_link import production_objects

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=1800)
    args = parser.parse_args()
    root = args.production_root.resolve()
    inputs = [root / name for name in production_objects(root)[:-1]]
    with tempfile.TemporaryDirectory(prefix="cupid-kernel-plan-") as temporary:
        run_kernel_plan(Path(temporary), root, inputs, args.qemu, args.timeout)

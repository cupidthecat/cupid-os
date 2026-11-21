#!/usr/bin/env python3
## @file
#  @brief Rebuild and run a complete peer tool inside Cupid OS.
"""Rebuild a complete guest tool, then run it on a checked input."""

import argparse
import re
from pathlib import Path
import tempfile

from check_guest_compiler_self_host import COMPILER_SOURCES
from check_guest_runtime import (
    BUILD,
    BUILD_FAILURE_PATTERN,
    COMPILER_REAPED,
    ROOT,
    compare_homefs_files,
    create_disk,
    read_homefs,
    run,
    write_homefs,
)


PEERS = {
    "cupidasm": (
        "toolchain/ctool.cc", "toolchain/elf32.cc", "toolchain/x86.cc",
        "toolchain/cupidasm.cc", "toolchain/cupiddis.cc", "bootstrap/guest/guest_job.cc",
        "bootstrap/guest/cupidasm_guest.cc",
    ),
    "cupidld": (
        "toolchain/ctool.cc", "toolchain/elf32.cc", "toolchain/cupidld.cc",
        "bootstrap/guest/guest_job.cc", "bootstrap/guest/cupidld_guest.cc",
    ),
    "cupidobj": (
        "toolchain/ctool.cc", "toolchain/elf32.cc", "toolchain/cupidobj.cc",
        "bootstrap/guest/guest_job.cc", "bootstrap/guest/cupidobj_guest.cc",
    ),
    "cupidbuild": (
        *COMPILER_SOURCES,
        "toolchain/cupidld.cc", "bootstrap/guest/cupidld_guest.cc", "bootstrap/guest/cupidbuild_guest.cc",
        "bootstrap/guest/cupidbuild_kernel.cc",
        "toolchain/cupidasm.cc", "toolchain/cupiddis.cc", "bootstrap/guest/cupidasm_guest.cc",
        "bootstrap/guest/guest_raw_assembly.cc", "bootstrap/guest/cupidinstall_guest.cc",
        "bootstrap/guest/cupidksyms_guest.cc", "bootstrap/guest/cupidobj_guest.cc",
        "bootstrap/guest/cupiddisk_guest.cc",
    ),
}


def fixture(root: Path, peer: str) -> tuple[tuple[bytes, bytes], str, tuple[str, Path]]:
    if peer in ("cupidasm", "cupidbuild"):
        source = root / "kernel/cpu/isr.asm"
        name = b"asm.job" if peer == "cupidbuild" else b"cupidasm.job"
        return (name, b"/disk/in.asm\n/home/check.o\n"), f"{source}:/in.asm", (
            "check.o", source.with_suffix(".o"),
        )
    if peer == "cupidld":
        request = b"/home/hello.elf\n0x01c00000\n_start\n/disk/in.o\n"
        return (b"cupidld.job", request), f"{root / 'user/build/hello.o'}:/in.o", (
            "hello.elf", root / "user/build/hello",
        )
    source = "file_example_JPG_1MB.jpg"
    request = f"/disk/in.jpg\n/home/check.o\nwrap-jpeg\n/{source}\n".encode("ascii")
    return (b"cupidobj.job", request), f"{root / source}:/in.jpg", ("check.o", root / (source + ".o"))


def check(scratch: Path, root: Path, peer: str, qemu: str, timeout: float) -> None:
    jobs = []
    plan = []
    inputs = []
    references = {"worker.elf": BUILD / f"{peer}-guest.elf"}
    for index, source in enumerate(PEERS[peer]):
        alias = f"i{index}.o"
        job = f"c{index}.job"
        jobs.append((job.encode("ascii"), f"/{source}\n/home/{alias}\n".encode("ascii")))
        plan.extend(("compile", f"/home/{job}"))
        inputs.append(f"/home/{alias}")
        references[alias] = BUILD / Path(source).with_suffix(".o").name
    link = ("\n".join(("/home/worker.elf", "0x01e00000", f"{peer}_guest_run", *inputs)) + "\n").encode("ascii")
    plan.extend(("link", "/home/link.job"))
    request, source_alias, (output, expected) = fixture(root, peer)
    references[output] = expected
    success = rf"\[{peer}-guest\] wrote /home/{re.escape(output)}" + COMPILER_REAPED
    setup = ()
    if peer == "cupidbuild":
        jobs.append((b"r", b"assemble\n/home/asm.job\n"))
        setup = ("--setup-command", "cp /home/r /home/cupidbuild.job",
                 "--setup-success-pattern", r"\[terminal\] command complete")
        success = r"\[cupidasm-guest\] wrote /home/check[.]o.*?\[cupidbuild-guest\] build complete" + COMPILER_REAPED
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidbuild.job", ("\n".join(plan) + "\n").encode("ascii")),
                         (b"link.job", link), request, *jobs))

    image = scratch / "peer.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{BUILD / 'engsrc.iso'}:/s.iso",
                f"{BUILD / 'cupidbuild-guest.elf'}:/b", source_alias, production_root=root)
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "1", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.75",
        "--image", str(image), "--log", str(BUILD / f"self-host-{peer}.log"),
        "--require-command-completion", "--failure-pattern", BUILD_FAILURE_PATTERN,
        "--setup-command", "mount /disk/s.iso /src",
        "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/src'",
        "--setup-command", "exec /disk/b",
        "--setup-success-pattern", r"\[cupidbuild-guest\] build complete" + COMPILER_REAPED, *setup,
        "--command", "exec /home/worker.elf",
        "--success-pattern", success)
    compare_homefs_files(read_homefs(image), references)
    print(f"All {len(PEERS[peer])} {peer} worker objects and the runtime output matched.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", choices=PEERS)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=3600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix=f"cupid-{args.tool}-self-host-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.tool, args.qemu, args.timeout)

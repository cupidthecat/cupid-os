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
)
from check_guest_source_plan import MEDIA_BUILD, PLAN_ISO, job_contents, publish_plan_iso


PEERS = {
    "cupidc": COMPILER_SOURCES,
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


def fixture(root: Path, peer: str) -> tuple[tuple[tuple[str, bytes], ...], tuple[str, ...], tuple[str, Path]]:
    if peer == "cupidc":
        request = ("cupidc.job", job_contents("/toolchain/ctool.cc", "/home/check.o"))
        return (request,), (), ("check.o", BUILD / "ctool.o")
    if peer in ("cupidasm", "cupidbuild"):
        source = root / "kernel/cpu/isr.asm"
        name = "asm.job" if peer == "cupidbuild" else "cupidasm.job"
        jobs = ((name, job_contents("/disk/in.asm", "/home/check.o")),)
        if peer == "cupidbuild":
            jobs += (("cupidbuild.job", job_contents("assemble", "/home/asm.job")),)
        return jobs, (f"{source}:/in.asm",), ("check.o", source.with_suffix(".o"))
    if peer == "cupidld":
        request = job_contents("/home/hello.elf", "0x01c00000", "_start", "/disk/in.o")
        return (("cupidld.job", request),), (f"{root / 'user/build/hello.o'}:/in.o",), (
            "hello.elf", root / "user/build/hello",
        )
    source = "file_example_JPG_1MB.jpg"
    request = job_contents("/disk/in.jpg", "/home/check.o", "wrap-jpeg", f"/{source}")
    return (("cupidobj.job", request),), (f"{root / source}:/in.jpg",), ("check.o", root / (source + ".o"))


def check(scratch: Path, root: Path, peer: str, qemu: str, timeout: float) -> None:
    sources = PEERS[peer]
    inputs = [f"/home/i{index}.o" for index in range(len(sources))]
    references = {"worker.elf": BUILD / f"{peer}-guest.elf"}
    pairs = tuple(path for source, obj in zip(sources, inputs, strict=True) for path in (f"/{source}", obj))
    references.update((Path(obj).name, BUILD / Path(source).with_suffix(".o").name)
                      for source, obj in zip(sources, inputs, strict=True))
    steps = (
        ("compile-list", b"sources.job", job_contents("sources", *pairs)),
        ("link", b"link.job", job_contents("/home/worker.elf", "0x01e00000", f"{peer}_guest_run", *inputs)),
    )
    plan = scratch / PLAN_ISO[0]
    publisher = ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf"
    if publish_plan_iso(publisher, root, plan, steps):
        raise ValueError("could not publish the peer build plan ISO")

    jobs, asset_inputs, (output, expected) = fixture(root, peer)
    references[output] = expected
    success = rf"\[{peer}-guest\] wrote /home/{re.escape(output)}" + COMPILER_REAPED
    if peer == "cupidbuild":
        success = r"\[cupidasm-guest\] wrote /home/check[.]o.*?\[cupidbuild-guest\] build complete" + COMPILER_REAPED
    staged = []
    setup = []
    for index, (name, contents) in enumerate(jobs):
        alias = f"r{index}"
        request = scratch / alias
        request.write_bytes(contents)
        staged.append(f"{request}:/{alias}")
        setup.extend(("--setup-command", f"cp /disk/{alias} /home/{name}",
                      "--setup-success-pattern", r"\[terminal\] command complete"))

    image = scratch / "peer.img"
    create_disk(image, f"{BUILD / 'engsrc.iso'}:/s.iso", f"{plan}:/{plan.name}",
                f"{BUILD / MEDIA_BUILD}:/b", *asset_inputs, *staged, production_root=root)
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "1", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "0.75",
        "--image", str(image), "--log", str(BUILD / f"self-host-{peer}.log"),
        "--require-command-completion", "--failure-pattern", BUILD_FAILURE_PATTERN,
        "--setup-command", "mount /disk/s.iso /src",
        "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/src'",
        "--setup-command", f"mount /disk/{plan.name} {PLAN_ISO[1]}",
        "--setup-success-pattern", f"VFS: mounted 'iso9660' at '{PLAN_ISO[1]}'",
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

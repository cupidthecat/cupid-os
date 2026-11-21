#!/usr/bin/env python3
## @file
#  @brief Build and run a user program through the in-OS build driver.
"""Run a compile-and-link plan in one guest command and compare both products."""

import argparse
from pathlib import Path
import tempfile

from check_guest_build import COMPILE, LINK
from check_guest_install_runtime import INSTALL_HEADERS, installation_jobs
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
from check_guest_source_plan import (
    INSTALL_INCLUDE_OPTIONS,
    MEDIA_BUILD,
    PLAN_ISO,
    installation_request,
    iso_members,
    job_contents,
    publish_plan_iso,
)
from check_guest_user_runtime import USER_OUTPUT
from source_iso import publish_source_iso


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    publisher = ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf"
    iso = scratch / "user.iso"
    demos = "kernel/util/demos_programs_gen.cc"
    installed_source, request = installation_request(demos, installation_jobs(root)[demos])
    members = iso_members({Path(path) for path in (*INSTALL_HEADERS, "kernel/cpu/isr.asm",
                                                   "user/cupid.h", "user/examples/hello.cc")})
    steps = (
        ("assemble", b"isr.job", job_contents("/src/kernel/cpu/isr.asm", "/home/isr.o")),
        ("install", b"install.job", request),
        ("compile", b"compile-install.job", job_contents(
            f"/home/{installed_source}", "/home/demos.o", *INSTALL_INCLUDE_OPTIONS)),
        ("compile", b"hello-temp.cj", COMPILE.replace(b"/home/hello.o", b"/tmp/hello.o")),
        ("compile", b"hello.cj", COMPILE),
        ("link", b"hello.lj", LINK.replace(b"/home/hello.o", b"/tmp/hello.o")),
    )
    if publish_source_iso(publisher, root, BUILD / "cupidc-engine.elf", iso, members):
        raise ValueError("could not publish the user source ISO")
    plan = scratch / PLAN_ISO[0]
    if publish_plan_iso(publisher, root, plan, steps):
        raise ValueError("could not publish the user plan ISO")

    disk = scratch / "build.img"
    create_disk(disk, f"{iso}:/s.iso", f"{plan}:/{plan.name}", f"{BUILD / MEDIA_BUILD}:/b",
                production_root=root)
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "1", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "1",
        "--image", str(disk), "--log", str(BUILD / "guest-build-runtime.log"),
        "--require-command-completion", "--failure-pattern", BUILD_FAILURE_PATTERN,
        "--setup-command", "mount /disk/s.iso /src",
        "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/src'",
        "--setup-command", f"mount /disk/{plan.name} {PLAN_ISO[1]}",
        "--setup-success-pattern", f"VFS: mounted 'iso9660' at '{PLAN_ISO[1]}'",
        "--setup-command", "exec /disk/b",
        "--setup-success-pattern", r"\[cupidbuild-guest\] build complete" + COMPILER_REAPED,
        "--command", "exec /home/hello.elf",
        "--success-pattern", rf"\[elf\] Loaded /home/hello\.elf as PID (?P<pid>[1-9][0-9]*).*?"
                             rf"\[elf-syscall\] pid=(?P=pid) op={USER_OUTPUT['hello']}[\r\n]+.*?"
                             r"\[PROCESS\] PID (?P=pid) .* exiting")
    compare_homefs_files(read_homefs(disk), {
        "isr.o": root / "kernel/cpu/isr.o",
        installed_source: root / demos,
        "demos.o": root / Path(demos).with_suffix(".o"),
        "hello.o": root / "user/build/hello.o",
        "hello.elf": root / "user/build/hello",
    })


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-guest-build-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.qemu, args.timeout)

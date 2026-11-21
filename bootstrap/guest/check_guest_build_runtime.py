#!/usr/bin/env python3
## @file
#  @brief Build and run a user program through the in-OS build driver.
"""Run a compile-and-link plan in one guest command and compare both products."""

import argparse
from pathlib import Path
import tempfile

from check_guest_build import COMPILE, LINK, PLAN
from check_guest_install_runtime import installation_jobs
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
from check_guest_user_runtime import USER_OUTPUT
from source_iso import publish_source_iso


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    publisher = ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf"
    iso = scratch / "user.iso"
    members = ("user", "user/examples", "user/cupid.h", "user/examples/hello.cc")
    if publish_source_iso(publisher, root, BUILD / "cupidc-engine.elf", iso, members):
        raise ValueError("could not publish the user source ISO")

    demos = "kernel/util/demos_programs_gen.cc"
    install = b"kernel/util/install.job"
    plan = (b"assemble\n/home/isr.job\ninstall\n/home/" + install +
            b"\ncompile\n/home/hello-temp.cj\n" + PLAN)
    temporary = COMPILE.replace(b"/home/hello.o", b"/tmp/hello.o")
    link = LINK.replace(b"/home/hello.o", b"/tmp/hello.o")
    homefs = scratch / "homefs.sys"
    write_homefs(homefs, ((b"cupidbuild.job", plan), (b"hello.cj", COMPILE), (b"hello.lj", link),
                         (b"hello-temp.cj", temporary),
                         (b"isr.job", b"/disk/isr.asm\n/home/isr.o\n"),
                         (install, installation_jobs(root)[demos])))
    disk = scratch / "build.img"
    create_disk(disk, f"{homefs}:/homefs.sys", f"{iso}:/s.iso",
                f"{BUILD / 'cupidbuild-guest.elf'}:/b", f"{root / 'kernel/cpu/isr.asm'}:/isr.asm",
                production_root=root)
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "1", "--cpu", "max",
        "--nic", "e1000", "--timeout", str(timeout), "--key-pause", "1",
        "--image", str(disk), "--log", str(BUILD / "guest-build-runtime.log"),
        "--require-command-completion", "--failure-pattern", BUILD_FAILURE_PATTERN,
        "--setup-command", "mount /disk/s.iso /src",
        "--setup-success-pattern", r"VFS: mounted 'iso9660' at '/src'",
        "--setup-command", "exec /disk/b",
        "--setup-success-pattern", r"\[cupidbuild-guest\] build complete" + COMPILER_REAPED,
        "--command", "exec /home/hello.elf",
        "--success-pattern", rf"\[elf\] Loaded /home/hello\.elf as PID (?P<pid>[1-9][0-9]*).*?"
                             rf"\[elf-syscall\] pid=(?P=pid) op={USER_OUTPUT['hello']}[\r\n]+.*?"
                             r"\[PROCESS\] PID (?P=pid) .* exiting")
    compare_homefs_files(read_homefs(disk), {
        "isr.o": root / "kernel/cpu/isr.o",
        demos: root / demos,
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

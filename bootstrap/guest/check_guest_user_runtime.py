#!/usr/bin/env python3
## @file
#  @brief Build and execute the checked user programs inside Cupid OS.
"""Compare guest-built user objects and executables with the checked host build."""

import argparse
from pathlib import Path
import re
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
from tools.cupidc_production_compile import USER_SOURCES


READ_ME = b"Cupid external runtime fixture.\n[elf-syscall] pid=999 op=exit\n"
USER_OUTPUT = {
    "cat": r"print bytes=62 fnv1a=0xc12ed628",
    "hello": r"print bytes=27 fnv1a=0x6d2edfa6",
    "ls": r"print bytes=3 fnv1a=0x5acad8be",
}


def check(scratch: Path, root: Path, qemu: str, timeout: float) -> None:
    iso = scratch / "user.iso"
    members = ("user", "user/examples", "user/cupid.h", *USER_SOURCES)
    result = publish_source_iso(
        ROOT / "bootstrap/seeds/i386-linux/cupidbuild.elf", root, BUILD / "cupidc-engine.elf",
        iso, members,
    )
    if result:
        raise RuntimeError(f"user source ISO publication failed with status {result}")

    jobs = [(b"readme.txt", READ_ME)]
    references = {}
    commands = [("mount /disk/user.iso /src", r"VFS: mounted 'iso9660' at '/src'")]
    completed = r"\[terminal\] command complete"
    for index, source in enumerate(USER_SOURCES):
        name = Path(source).stem
        obj = f"{name}.o"
        elf = f"{name}.elf"
        compile_job = "cupidc.job" if index == 0 else f"cc{index}.job"
        link_job = "cupidld.job" if index == 0 else f"ld{index}.job"
        jobs.extend((
            (compile_job.encode("ascii"), f"/{source}\n/home/{obj}\ninclude /user\n".encode("ascii")),
            (link_job.encode("ascii"), f"/home/{elf}\n0x01c00000\n_start\n/home/{obj}\n".encode("ascii")),
        ))
        references[obj] = root / "user/build" / obj
        references[elf] = root / "user/build" / name
        if index:
            commands.append((f"cp /home/{compile_job} /home/cupidc.job", completed))
        commands.append((
            "exec /disk/cupcc",
            r"\[cupidc-guest\] wrote /home/" + re.escape(obj) + COMPILER_REAPED,
        ))
        if index:
            commands.append((f"cp /home/{link_job} /home/cupidld.job", completed))
        commands.append((
            "exec /disk/cupld",
            r"\[cupidld-guest\] wrote /home/" + re.escape(elf) + COMPILER_REAPED,
        ))

    for source in USER_SOURCES:
        name = Path(source).stem
        path = f"/home/{name}.elf"
        commands.append((
            f"exec {path}",
            rf"\[elf\] Loaded {re.escape(path)} as PID (?P<pid>[1-9][0-9]*).*?"
            rf"\[elf-syscall\] pid=(?P=pid) op={USER_OUTPUT[name]}[\r\n]+.*?"
            rf"\[elf-syscall\] pid=(?P=pid) op=exit[\r\n]+.*?"
            rf"\[PROCESS\] PID (?P=pid) .*{re.escape(path)}.* exiting",
        ))

    homefs = scratch / "homefs.sys"
    write_homefs(homefs, tuple(jobs))
    image = scratch / "user.img"
    create_disk(image, f"{homefs}:/homefs.sys", f"{iso}:/user.iso",
                f"{BUILD / 'cupidc-guest.elf'}:/cupcc",
                f"{BUILD / 'cupidld-guest.elf'}:/cupld", production_root=root)
    arguments = []
    for command, pattern in commands[:-1]:
        arguments.extend(("--setup-command", command, "--setup-success-pattern", pattern))
    command, pattern = commands[-1]
    run("tools/gui_terminal_smoke.py", "--qemu", qemu, "--smp", "4",
        "--cpu", "max", "--nic", "e1000", "--timeout", str(timeout), "--image", str(image),
        "--log", str(BUILD / "user-build-runtime.log"), "--require-command-completion",
        *arguments, "--command", command, "--success-pattern", pattern)
    compare_homefs_files(read_homefs(image), references)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--production-root", type=Path, default=ROOT)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cupid-guest-user-") as temporary:
        check(Path(temporary), args.production_root.resolve(), args.qemu, args.timeout)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
## @file
#  @brief Check guest build plans, checked outputs and failure boundaries.
"""Run the guest build driver with the Linux syscall fixture."""

from pathlib import Path
import shutil
import subprocess
import tempfile

from check_guest_runtime import BUILD, ROOT, SEED_MANIFEST

PLAN = b"compile\n/home/hello.cj\nlink\n/home/hello.lj\n"
COMPILE = b"/user/examples/hello.cc\n/home/hello.o\ninclude /user\n"
LINK = b"/home/hello.elf\n0x01c00000\n_start\n/home/hello.o\n"


def check(scratch: Path) -> None:
    sources = scratch / "src"
    (sources / "user/examples").mkdir(parents=True)
    for path in ("user/cupid.h", "user/examples/hello.cc"):
        shutil.copyfile(ROOT / path, sources / path)
    reference_object = sources / "check.o"
    reference_executable = sources / "check.elf"
    manifest = ROOT / SEED_MANIFEST
    command = [manifest.with_name("cupidbuild.elf"), "run", "--root", ROOT,
               "--seed-manifest", manifest, "--seed-release", ROOT / "bootstrap/seeds/release.json",
               "--tool"]
    subprocess.run([*command, "cupidc", "--", "--root", sources, "--freestanding",
                    "--gnu", "-I", "/user", "-c", "/user/examples/hello.cc", "-o", "/check.o"],
                   check=True, timeout=120)
    subprocess.run([*command, "cupidld", "--", "-m", "elf_i386", "--text-address",
                    "0x01c00000", "--entry", "_start", "-o", reference_executable, reference_object],
                   check=True, timeout=120)

    home = scratch / "home"
    home.mkdir()
    (home / "hello.cj").write_bytes(COMPILE)
    (home / "hello.lj").write_bytes(LINK)
    plan = home / "cupidbuild.job"
    object_path = home / "hello.o"
    executable = home / "hello.elf"

    def invoke(marker: bytes) -> bytes:
        result = subprocess.run([BUILD / "cupidbuild-guest-linux-contract.elf", scratch],
                                check=True, capture_output=True, timeout=120)
        assert marker in result.stdout, result.stdout
        return result.stdout

    for invalid in (PLAN.replace(b"link\n", b"unknown\n"),
                    PLAN.replace(b"/home/hello.lj\n", b"/home/../hello.lj\n"),
                    PLAN + b"link\n", PLAN + PLAN * 32):
        plan.write_bytes(invalid)
        invoke(b"[cupidbuild-guest] invalid request")
        assert not object_path.exists() and not executable.exists()

    plan.write_bytes(PLAN)
    log = invoke(b"[cupidbuild-guest] build complete")
    assert log.index(b"wrote /home/hello.o") < log.index(b"wrote /home/hello.elf")
    assert object_path.read_bytes() == reference_object.read_bytes()
    assert executable.read_bytes() == reference_executable.read_bytes()

    sentinel = b"preceding executable"
    executable.write_bytes(sentinel)
    (home / "hello.cj").write_bytes(COMPILE.replace(b"/hello.cc", b"/missing.cc"))
    log = invoke(b"[cupidbuild-guest] stopped at /home/hello.cj")
    assert b"[cupidld-guest]" not in log
    assert object_path.read_bytes() == reference_object.read_bytes()
    assert executable.read_bytes() == sentinel

    (home / "hello.cj").write_bytes(COMPILE)
    (home / "hello.lj").write_bytes(LINK.replace(b"/hello.o", b"/missing.o"))
    log = invoke(b"[cupidbuild-guest] stopped at /home/hello.lj")
    assert b"wrote /home/hello.o" in log and b"build complete" not in log
    assert executable.read_bytes() == sentinel
    raw = home / "kernel.bin"
    raw.write_bytes(b"preceding raw kernel")
    (home / "flatten.job").write_bytes(b"/home/hello.elf\n/home/kernel.bin\n")
    (home / "disk.job").write_bytes(b"/home/boot.bin\n/home/kernel.bin\n/home/disk.img\n")
    plan.write_bytes(b"object\n/home/flatten.job\ndisk\n/home/disk.job\n")
    log = invoke(b"[cupidbuild-guest] stopped at /home/flatten.job")
    assert b"[cupiddisk-guest]" not in log and raw.read_bytes() == b"preceding raw kernel"

    boot = home / "boot.bin"
    boot.write_bytes(b"preceding bootloader")
    (home / "boot.job").write_bytes(b"/missing.asm\n/home/boot.bin\nbootloader\n")
    plan.write_bytes(b"raw\n/home/boot.job\n" + PLAN)
    log = invoke(b"[cupidbuild-guest] stopped at /home/boot.job")
    assert b"[cupidc-guest]" not in log and boot.read_bytes() == b"preceding bootloader"
    print("Guest build matched the checked object and executable and stopped on eight rejected plans or steps.")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(dir=BUILD, prefix="guest-build-") as temporary:
        check(Path(temporary))

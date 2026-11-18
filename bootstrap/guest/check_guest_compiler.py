"""Exercise the compiler executable through its Cupid OS syscall adapter."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
build = root / "build/guest-engine"
runner = build / "cupidc-guest-linux-contract.elf"

with tempfile.TemporaryDirectory(dir=build, prefix="guest-driver-") as temporary:
    jail = Path(temporary)
    sources = jail / "src/toolchain"
    sources.mkdir(parents=True)
    for source in ("ctool.cc", "ctool.h"):
        shutil.copyfile(root / "toolchain" / source, sources / source)
    (jail / "src/bad.cc").write_bytes(b"int invalid = ;\n")
    home = jail / "home"
    home.mkdir()
    object_file = home / "ctool.o"
    pending = home / ".cupidc-pending.o"
    job = home / "cupidc.job"
    sentinel = b"preceding artifact"

    def check(request, marker):
        job.write_bytes(request)
        result = subprocess.run([runner, jail], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout

    success = b"/toolchain/ctool.cc\n/home/ctool.o\n"
    object_file.write_bytes(sentinel)
    check(success, b"[cupidc-guest] wrote /home/ctool.o")
    assert object_file.read_bytes() == (build / "ctool.o").read_bytes()
    assert not pending.exists()

    object_file.write_bytes(sentinel)
    check(b"/bad.cc\n/home/ctool.o\n", b"[cupidc-guest] compilation failed")
    assert object_file.read_bytes() == sentinel and not pending.exists()
    check(b"/missing.cc\n/home/ctool.o\n", b"[cupidc-guest] compilation failed")
    assert object_file.read_bytes() == sentinel and not pending.exists()
    check(b"/toolchain/ctool.cc\n/home/../ctool.o\n", b"[cupidc-guest] invalid request file")
    assert not (jail / "ctool.o").exists()
    check(b"/toolchain/ctool.cc\n/home/ctool.o\nextra\n", b"[cupidc-guest] invalid request file")
    assert object_file.read_bytes() == sentinel
    check(b"/toolchain/ctool.cc\n/home/.cupidc-pending.o\n", b"[cupidc-guest] invalid request file")
    assert not pending.exists()

    pending.write_bytes(sentinel)
    check(success, b"[cupidc-guest] compilation failed")
    assert object_file.read_bytes() == sentinel and pending.read_bytes() == sentinel
    pending.unlink()
    blocked = home / "blocked.o"
    blocked.mkdir()
    check(b"/toolchain/ctool.cc\n/home/blocked.o\n", b"[cupidc-guest] compilation failed")
    assert blocked.is_dir() and not pending.exists()

    check(success, b"[cupidc-guest] wrote /home/ctool.o")
    assert object_file.read_bytes() == (build / "ctool.o").read_bytes()
    assert not pending.exists()
    print("Guest compiler adapter reproduced ctool.o; seven rejected requests preserved output.")

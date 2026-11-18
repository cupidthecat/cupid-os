"""Compare the guest CupidObj worker with the checked hosted flattener."""

from pathlib import Path
import subprocess
import sys
import tempfile


def check(root: Path, scratch: Path) -> None:
    build = root / "build/guest-engine"
    seed = root / "bootstrap/seeds/i386-linux"
    runner = build / "cupidobj-guest-linux-contract.elf"
    command = [seed / "cupidbuild.elf", "run", "--root", root,
               "--seed-manifest", seed / "manifest.json",
               "--seed-release", root / "bootstrap/seeds/release.json", "--tool"]

    source = scratch / "start.cc"
    obj = scratch / "start.o"
    executable = scratch / "start.elf"
    expected = scratch / "expected.bin"
    source.write_text("int _start(void) { return 42; }\n", encoding="ascii")
    subprocess.run([*command, "cupidc", "--", "--root", root, "--freestanding",
                    "-c", "/" + source.relative_to(root).as_posix(),
                    "-o", "/" + obj.relative_to(root).as_posix()], check=True, timeout=120)
    subprocess.run([*command, "cupidld", "--", "-m", "elf_i386", "--text-address",
                    "0x01c00000", "--entry", "_start", "-o", executable, obj],
                   check=True, timeout=120)
    subprocess.run([*command, "cupidobj", "--", "flat", executable, "-o", expected],
                   check=True, timeout=120)

    home = scratch / "home"
    home.mkdir()
    job = home / "cupidobj.job"
    output = home / "flat.bin"
    pending = home / ".cupidobj-pending.bin"
    sentinel = b"preceding raw image"
    request = b"/start.elf\n/home/flat.bin\n"
    invalid = b"[cupidobj-guest] invalid request"
    failed = b"[cupidobj-guest] flatten failed: "
    (scratch / "bad.elf").write_bytes(b"invalid ELF")
    with (scratch / "big.elf").open("wb") as oversized:
        oversized.truncate(16 * 1024 * 1024 + 1)

    def invoke(task: bytes, marker: bytes) -> None:
        job.write_bytes(task)
        result = subprocess.run([runner, scratch], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout

    for task, marker in (
        (b"/start.elf\n/flat.bin\n", invalid),
        (request + b"extra\n", invalid),
        (request.replace(b"start.elf", b"missing.elf"), failed + b"not_found"),
        (request.replace(b"start.elf", b"bad.elf"), failed),
        (request.replace(b"start.elf", b"big.elf"), failed + b"limit"),
    ):
        output.write_bytes(sentinel)
        invoke(task, marker)
        assert output.read_bytes() == sentinel and not pending.exists()

    pending.write_bytes(sentinel)
    invoke(request, failed)
    assert output.read_bytes() == sentinel and pending.read_bytes() == sentinel
    pending.unlink()
    blocked = home / "blocked.bin"
    blocked.mkdir()
    invoke(request.replace(b"flat.bin", b"blocked.bin"), failed)
    assert blocked.is_dir() and not pending.exists()

    invoke(request, b"[cupidobj-guest] wrote /home/flat.bin")
    assert output.read_bytes() == expected.read_bytes() and not pending.exists()
    print("Guest CupidObj matched the checked raw image and preserved output across seven rejections.")


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=root / "build/guest-engine", prefix="guest-object-") as temp:
        check(root, Path(temp))

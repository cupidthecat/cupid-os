## @file
#  @brief Check disk-template output and failed requests through the Linux fixture.
"""Compare a guest disk template with the checked hosted CupidObj output."""

from pathlib import Path
import subprocess
import sys
import tempfile


def check(root: Path, scratch: Path) -> None:
    seed = root / "bootstrap/seeds/i386-linux"
    runner = root / "build/guest-engine/cupiddisk-guest-linux-contract.elf"
    home = scratch / "home"
    inputs = scratch / "inputs"
    home.mkdir()
    inputs.mkdir()
    boot = inputs / "boot.bin"
    boot_image = bytearray(5 * 512)
    boot_image[510:512] = b"\x55\xaa"
    boot_image[-8:] = b"stage002"
    boot.write_bytes(boot_image)
    kernel = inputs / "kernel.bin"
    kernel.write_bytes(bytes(range(256)) * 8)
    reference = scratch / "checked.img"
    subprocess.run([seed / "cupidbuild.elf", "run", "--root", root,
                    "--seed-manifest", seed / "manifest.json",
                    "--seed-release", root / "bootstrap/seeds/release.json",
                    "--tool", "cupidobj", "--", "disk-template", boot,
                    "--kernel", kernel, "--image-sectors", "409600",
                    "--fat-start-lba", "20480", "-o", reference], check=True, timeout=120)

    output = home / "template.img"
    job = home / "cupiddisk.job"
    pending = home / ".cupiddisk-pending.img"

    def invoke(request: bytes, marker: bytes) -> None:
        job.write_bytes(request)
        result = subprocess.run([runner, scratch], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout

    build = b"/inputs/boot.bin\n/inputs/kernel.bin\n/home/template.img\n"
    invoke(build, b"[cupiddisk-guest] wrote /home/template.img")
    expected = reference.read_bytes()
    assert len(expected) == 10_697_216
    assert output.read_bytes() == expected and not pending.exists()

    (inputs / "short.bin").write_bytes(boot_image[:-1])
    with (inputs / "large.bin").open("wb") as large:
        large.truncate((20480 - 5) * 512 + 1)
    invalid = b"[cupiddisk-guest] invalid request"
    failed = b"[cupiddisk-guest] template failed"
    for request, marker in (
        (build.replace(b"/inputs/kernel.bin", b"/inputs/missing.bin"), failed + b": not_found"),
        (build.replace(b"/inputs/kernel.bin", b"/inputs/large.bin"), failed),
        (build.replace(b"/inputs/boot.bin", b"/inputs/short.bin"), failed),
        (build.replace(b"/inputs/kernel.bin\n", b""), invalid),
        (build.replace(b"/home/template.img", b"/home/../template.img"), invalid),
        (build + b"/inputs/extra\n", invalid),
    ):
        invoke(request, marker)
        assert output.read_bytes() == expected and not pending.exists()
    pending.write_bytes(b"preceding pending image")
    invoke(build, failed)
    assert output.read_bytes() == expected and pending.read_bytes() == b"preceding pending image"
    print("Guest disk template matched the checked bytes and preserved output on seven rejections.")


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=root / "build/guest-engine", prefix="guest-disk-") as temp:
        check(root, Path(temp))

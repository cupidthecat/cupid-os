## @file
#  @brief Check disk-template output and failed requests through the Linux fixture.
"""Compare a guest disk template with the checked hosted CupidObj output."""

from pathlib import Path
import subprocess
import sys
import tempfile

from check_guest_disk_runtime import FAT_START_LBA, write_reference_disk
from tools.hostbuild_disk import StageFile, stage_files


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

    def invoke(request: bytes, marker: bytes, *options: str) -> None:
        job.write_bytes(request)
        result = subprocess.run([runner, scratch, *options], check=True, capture_output=True, timeout=90)
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
    pending.unlink()

    full = build + b"image 32\n"
    image_reference = scratch / "full.img"
    write_reference_disk(image_reference, boot, kernel, 32)
    invoke(full, b"[cupiddisk-guest] wrote /home/template.img")
    complete = image_reference.read_bytes()
    assert len(complete) == 32 * 1024 * 1024
    assert output.read_bytes() == complete and not pending.exists()

    for mode in (b"image", b"image -1", b"image 10", b"image 2048", b"image 32 extra",
                 b"image 32\nimage 32"):
        invoke(build + mode + b"\n", invalid)
        assert output.read_bytes() == complete and not pending.exists()
    invoke(full.replace(b"/home/template.img", b"/tmp/template.img"), invalid)
    invoke(full, failed + b": limit", "--fail-seek")
    assert output.read_bytes() == complete and not pending.exists()
    iso = inputs / "fixture.iso"
    wad = inputs / "sample.wad"
    iso.write_bytes(bytes(range(256)) * 35)
    wad.write_bytes(b"wad fixture" * 120)
    stages = home / "assets"
    entries = b"/inputs/fixture.iso\n/hello.iso\n/inputs/sample.wad\n/wads/freedo~1.wad\n"
    stages.write_bytes(entries)
    stage_files(image_reference, FAT_START_LBA, [StageFile(iso, "/hello.iso"), StageFile(wad, "/wads/freedo~1.wad")])
    expected_staged = image_reference.read_bytes()
    staged = full + b"stage /home/assets\n"
    invoke(staged, b"[cupiddisk-guest] wrote /home/template.img")
    assert output.read_bytes() == expected_staged and not pending.exists()

    # Fail after the first asset has already changed the private candidate.
    for invalid_list in (entries + b"/inputs/missing\n/last.bin\n",
                         entries + b"/inputs/fixture.iso\n",
                         entries + b"/inputs/fixture.iso\n/../last.bin\n"):
        stages.write_bytes(invalid_list)
        invoke(staged, failed)
        assert output.read_bytes() == expected_staged and not pending.exists()
    invoke(staged.replace(b"/home/assets", b"/home/.cupiddisk-pending.img"), invalid)
    invoke(build + b"stage /home/assets\n", invalid)
    assert output.read_bytes() == expected_staged and not pending.exists()
    print("Guest prefix, full image and staged assets matched checked output. Rejections preserved the image.")


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=root / "build/guest-engine", prefix="guest-disk-") as temp:
        check(root, Path(temp))

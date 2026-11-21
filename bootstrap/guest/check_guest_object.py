## @file
#  @brief Verify guest ELF flattening, asset wrapping and output preservation.
"""Compare the guest CupidObj worker with the checked hosted tool."""

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

    asset = scratch / "asset"
    wrapped = home / "asset.o"
    request = b"/asset\n/home/asset.o\n"
    identity = "bin/test-clip.cc"
    for mode, contents in (("wrap", b"\x00Cupid\r\n\xff"),
                           ("wrap-text", b"Cupid\r\nLine\rEnd\n")):
        asset.write_bytes(contents)
        subprocess.run([*command, "cupidobj", "--", mode, asset,
                        "--identity", identity, "-o", expected], check=True, timeout=120)
        invoke(request + f"{mode}\n/{identity}\n".encode("ascii"),
               b"[cupidobj-guest] wrote /home/asset.o")
        assert wrapped.read_bytes() == expected.read_bytes() and not pending.exists()

    for fields in (b"wrap\n", b"wrap\n/\n", b"wrap\n/../escape\n", b"wrap-text\nasset\n",
                   b"unknown\n/asset\n", b"wrap\n/asset\nextra\n"):
        invoke(request + fields, invalid)
        assert wrapped.read_bytes() == expected.read_bytes() and not pending.exists()

    invoke(b"/missing\n/home/asset.o\nwrap\n/asset\n",
           b"[cupidobj-guest] wrap failed: not_found")
    assert wrapped.read_bytes() == expected.read_bytes() and not pending.exists()
    print("Guest CupidObj matched checked text and binary objects and preserved them on rejection.")

    subprocess.run([*command, "cupidobj", "--", "wrap", asset, "--stem", "smp_trampoline",
                    "--section", ".rodata", "--readonly", "-o", expected], check=True, timeout=120)
    invoke(request + b"wrap-readonly\nsmp_trampoline\n", b"[cupidobj-guest] wrote /home/asset.o")
    assert wrapped.read_bytes() == expected.read_bytes() and not pending.exists()
    for stem in (b"/smp/trampoline", b"32_start", b"smp-trampoline"):
        invoke(request + b"wrap-readonly\n" + stem + b"\n", invalid)
        assert wrapped.read_bytes() == expected.read_bytes() and not pending.exists()
    print("Guest CupidObj matched the checked read-only object and rejected invalid symbol stems.")

    text_object = scratch / "text.o"
    subprocess.run([*command, "cupidobj", "--", "wrap-text", asset,
                    "--identity", identity, "-o", text_object], check=True, timeout=120)
    iso_paths = scratch / "iso.paths"
    iso_paths.write_bytes(b"raw.o\ntext.o\n")
    expected_iso = scratch / "expected.iso"
    subprocess.run([*command, "cupidobj", "--", "iso-fixture", iso_paths,
                    "--file", "raw.o", expected, "--file", "text.o", text_object,
                    "-o", expected_iso], check=True, timeout=120)
    members = (b"/asset\n/raw.o\nwrap-readonly\nsmp_trampoline\n"
               b"/asset\n/text.o\nwrap-text\n/bin/test-clip.cc\n")
    manifest = scratch / "assets.list"
    manifest.write_bytes(members)
    bundle = home / "assets.iso"
    request = b"/assets.list\n/home/assets.iso\nwrap-bundle\n"
    invoke(request, b"[cupidobj-guest] wrote /home/assets.iso")
    assert bundle.read_bytes() == expected_iso.read_bytes() and not pending.exists()

    for bad, result in ((members.replace(b"text.o", b"RAW.o"), b"input"),
                        (members.rsplit(b"/bin/", 1)[0], b"input"),
                        (members.replace(b"/asset\n/text", b"/missing\n/text"), b"not_found"),
                        (b"/asset\n/raw.o\nwrap\n/asset\n" * 257, b"limit")):
        manifest.write_bytes(bad)
        invoke(request, b"[cupidobj-guest] bundle failed: " + result)
        assert bundle.read_bytes() == expected_iso.read_bytes() and not pending.exists()
    print("Guest CupidObj matched the checked asset ISO and preserved it after four rejections.")


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=root / "build/guest-engine", prefix="guest-object-") as temp:
        check(root, Path(temp))

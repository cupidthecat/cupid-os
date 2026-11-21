## @file
#  @brief Check the guest linker's ELF output and publication failures.
"""Check the Cupid OS linker adapter against the checked hosted linker."""

from pathlib import Path
import subprocess
import sys
import tempfile


def check(root: Path, scratch: Path) -> None:
    build = root / "build/guest-engine"
    runner = build / "cupidld-guest-linux-contract.elf"
    seed = root / "bootstrap/seeds/i386-linux"
    command = [seed / "cupidbuild.elf", "run", "--root", root,
               "--seed-manifest", seed / "manifest.json",
               "--seed-release", root / "bootstrap/seeds/release.json", "--tool"]

    inputs = scratch / "inputs"
    home = scratch / "home"
    inputs.mkdir()
    home.mkdir()
    for name, content in (
        ("left", "int left(void) { return 7; }\n"),
        ("entry", "int left(void); int _start(void) { return left(); }\n"),
    ):
        source = inputs / f"{name}.cc"
        output = inputs / f"{name}.o"
        source.write_text(content, encoding="ascii")
        subprocess.run([*command, "cupidc", "--", "--root", root, "--freestanding",
                        "-c", "/" + source.relative_to(root).as_posix(),
                        "-o", "/" + output.relative_to(root).as_posix()],
                       check=True, timeout=120)
    elf = home / "test.elf"
    task = home / "cupidld.job"
    pending = home / ".cupidld-pending.elf"
    objects = b"/inputs/entry.o\n/inputs/left.o\n"
    sentinel = b"previous executable"

    def invoke(request, marker):
        task.write_bytes(request)
        outcome = subprocess.run([runner, scratch], check=True, capture_output=True, timeout=90)
        assert marker in outcome.stdout, outcome.stdout

    invalid = b"[cupidld-guest] invalid request"
    failed = b"[cupidld-guest] link failed"
    start = b"/home/test.elf\n0x01c00000\n_start\n"
    for request, marker in (
        (b"/home/../test.elf\n0x01c00000\n_start\n" + objects, invalid),
        (start.replace(b"0x01c00000", b"0x00f00000") + objects, invalid),
        (start, invalid),
        (start + b"/inputs/entry.o\n" * 513, invalid),
        (start + b"/inputs/missing.o\n", failed),
        (start + b"/inputs/entry.o\n", failed),
        (start + objects + b"/inputs/left.o\n", failed),
    ):
        elf.write_bytes(sentinel)
        invoke(request, marker)
        assert elf.read_bytes() == sentinel and not pending.exists()

    pending.write_bytes(sentinel)
    invoke(start + objects, failed)
    assert elf.read_bytes() == sentinel and pending.read_bytes() == sentinel
    pending.unlink()
    blocked = home / "blocked.elf"
    blocked.mkdir()
    invoke(start.replace(b"/home/test.elf", b"/home/blocked.elf") + objects, failed)
    assert blocked.is_dir() and not pending.exists()

    for base in ("0x01c00000", "0x01e00000"):
        reference = scratch / "checked.elf"
        subprocess.run([*command, "cupidld", "--", "-m", "elf_i386",
                        "--text-address", base, "--entry", "_start",
                        "-o", reference, inputs / "entry.o", inputs / "left.o"],
                       check=True, timeout=120)
        invoke(start.replace(b"0x01c00000", base.encode("ascii")) + objects,
               b"[cupidld-guest] wrote /home/test.elf")
        assert elf.read_bytes() == reference.read_bytes() and not pending.exists()

    script = inputs / "link.ld"
    script.write_text(
        "ENTRY(_start)\nSECTIONS { . = 0x100000; .text : { *(.text) } }\n",
        encoding="ascii",
    )
    script_job = b"/home/test.elf\nscript\n/inputs/link.ld\n" + objects
    subprocess.run([*command, "cupidld", "--", "-m", "elf_i386", "-T", script,
                    "-o", reference, inputs / "entry.o", inputs / "left.o"],
                   check=True, timeout=120)
    invoke(script_job, b"[cupidld-guest] wrote /home/test.elf")
    assert elf.read_bytes() == reference.read_bytes() and not pending.exists()

    script.write_text(
        "ENTRY(_start)\nSECTIONS { . = 0x100000; .text : { *(.text) } "
        'ASSERT(1 <= 0, "guest script guard") }\n',
        encoding="ascii",
    )
    for request, marker in (
        (script_job.replace(b"/inputs/link.ld", b"/inputs/../link.ld"), invalid),
        (script_job.replace(b"/inputs/link.ld", b"/inputs/missing.ld"), failed + b": not_found"),
        (script_job, b"guest script guard"),
    ):
        invoke(request, marker)
        assert elf.read_bytes() == reference.read_bytes() and not pending.exists()

    print("Guest linker matched three checked ELF images and preserved output across twelve rejections.")


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=root / "build/guest-engine", prefix="guest-linker-") as temp:
        check(root, Path(temp))

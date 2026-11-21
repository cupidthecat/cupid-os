## @file
#  @brief Compare guest raw assembly against hosted production checks.
"""Check guest bootloader and AP trampoline assembly and preservation on failure."""

from pathlib import Path
import subprocess
import sys
import tempfile


def shift_smp_entry(trampoline: bytes) -> bytes:
    shifted = trampoline.replace(b"    cld", b"    nop\n    cld", 1)
    assert shifted != trampoline
    return shifted


def check(root: Path, runner: Path, scratch: Path) -> None:
    seed = root / "bootstrap/seeds/i386-linux"
    authority = ["--seed-manifest", seed / "manifest.json",
                 "--seed-release", root / "bootstrap/seeds/release.json", "--root", root]
    sources = {
        "bootloader": root / "boot/boot.asm",
        "smp-trampoline": root / "kernel/smp/smp_trampoline.S",
    }
    home = scratch / "home"
    home.mkdir()
    request = home / "cupidraw.job"
    output = home / "raw.bin"
    pending = home / ".cupidraw-pending.bin"
    source = scratch / "input.asm"
    expected = scratch / "expected.bin"

    def invoke(mode: str, marker: bytes) -> None:
        request.write_bytes(f"/input.asm\n/home/raw.bin\n{mode}\n".encode("ascii"))
        result = subprocess.run([runner, scratch], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout
        assert not pending.exists()

    for mode, original in sources.items():
        source.write_bytes(original.read_bytes())
        subprocess.run([seed / "cupidbuild.elf", f"assemble-{mode}", *authority,
                        "--source", original.relative_to(root), "--output", expected.relative_to(root)],
                       cwd=root, check=True, timeout=120)
        invoke(mode, b"[cupidraw-guest] wrote /home/raw.bin")
        assert output.read_bytes() == expected.read_bytes()

    previous = output.read_bytes()
    shifted = shift_smp_entry(sources["smp-trampoline"].read_bytes())
    bad_jump = b"BITS 16\nORG 0\njmp 0x4000\ntimes 2560 - ($ - $$) db 0\n"
    for mode, invalid, size in (("smp-trampoline", shifted, 4096), ("bootloader", bad_jump, 2560)):
        source.write_bytes(invalid)
        subprocess.run([sys.executable, root / "tools/bootstrap_toolchain.py", "run", "--manifest",
                        seed / "manifest.json", "--root", root, "--tool", "cupidasm", "--",
                        "-f", "bin", "-o", expected, source], cwd=root, check=True, timeout=120)
        assert expected.stat().st_size == size
        invoke(mode, b"[cupidraw-guest] assembly failed: input")
        assert output.read_bytes() == previous

    invoke("unrecognized", b"[cupidraw-guest] invalid request")
    assert output.read_bytes() == previous
    print("Guest raw assembly matched the checked bootloader and trampoline and preserved prior output.")


if __name__ == "__main__":
    root, runner = (Path(part).resolve() for part in sys.argv[1:])
    with tempfile.TemporaryDirectory(dir=runner.parent, prefix="guest-raw-") as temporary:
        check(root, runner, Path(temporary))

"""Compare the guest kernel symbol worker with the checked hosted generator."""

from pathlib import Path
import subprocess
import sys
import tempfile


def check(root: Path, scratch: Path) -> None:
    seed = root / "bootstrap/seeds/i386-linux"
    builder = seed / "cupidbuild.elf"
    release = root / "bootstrap/seeds/release.json"
    common = ["--root", root, "--seed-manifest", seed / "manifest.json",
              "--seed-release", release]
    runner = root / "build/guest-engine/cupidksyms-guest-linux-contract.elf"
    home = scratch / "home"
    inputs = scratch / "inputs"
    home.mkdir()
    inputs.mkdir()
    source = inputs / "entry.cc"
    obj = inputs / "entry.o"
    elf = inputs / "pass1.elf"
    source.write_text("int _start(void) { return 7; }\n", encoding="ascii")
    subprocess.run([builder, "run", *common, "--tool", "cupidc", "--", "--root", root,
                    "--freestanding", "-c", "/" + source.relative_to(root).as_posix(),
                    "-o", "/" + obj.relative_to(root).as_posix()], check=True, timeout=180)
    subprocess.run([builder, "run", *common, "--tool", "cupidld", "--", "-m", "elf_i386",
                    "--text-address", "0x00100000", "--entry", "_start", "-o", elf, obj],
                   check=True, timeout=180)

    reference = scratch / "checked.cc"
    subprocess.run([builder, "generate-ksyms", *common,
                    "--source", elf.relative_to(root), "--output", reference.relative_to(root)],
                   check=True, timeout=180)
    output = home / "ksyms_data.cc"
    job = home / "cupidksyms.job"
    pending = home / ".cupidksyms-pending.cc"

    def invoke(request: bytes, marker: bytes) -> None:
        job.write_bytes(request)
        result = subprocess.run([runner, scratch], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout

    first_job = b"/inputs/pass1.elf\n/home/ksyms_data.cc\n"
    invoke(first_job, b"[cupidksyms-guest] wrote /home/ksyms_data.cc")
    expected = reference.read_bytes()
    assert output.read_bytes() == expected and not pending.exists()

    (inputs / "invalid.elf").write_bytes(b"not an ELF image")
    for request, marker in (
        (first_job.replace(b"/inputs/pass1.elf", b"/inputs/missing.elf"),
         b"generation failed: not_found"),
        (first_job.replace(b"/inputs/pass1.elf", b"/inputs/invalid.elf"),
         b"generation failed"),
        (first_job.replace(b"/home/ksyms_data.cc", b"/home/../ksyms_data.cc"),
         b"invalid request"),
    ):
        invoke(request, marker)
        assert output.read_bytes() == expected and not pending.exists()
    pending.write_bytes(b"earlier pending file")
    invoke(first_job, b"generation failed")
    assert output.read_bytes() == expected and pending.read_bytes() == b"earlier pending file"
    print("Guest symbol generation matched the checked source and preserved output on rejection.")


if __name__ == "__main__":
    root = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(dir=root / "build/guest-engine", prefix="guest-ksyms-") as temp:
        check(root, Path(temp))

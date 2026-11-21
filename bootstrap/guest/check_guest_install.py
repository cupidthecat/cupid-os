## @file
#  @brief Compare guest installation sources and check rejected jobs.
"""Check all three guest installation generators against hosted CupidObj."""

from pathlib import Path
import subprocess
import sys
import tempfile


def check(root: Path, runner: Path, scratch: Path) -> None:
    seed = root / "bootstrap/seeds/i386-linux"
    command = [seed / "cupidbuild.elf", "run", "--root", root,
               "--seed-manifest", seed / "manifest.json",
               "--seed-release", root / "bootstrap/seeds/release.json",
               "--tool", "cupidobj", "--", "install-source"]
    home = scratch / "home"
    home.mkdir()
    request = home / "cupidinstall.job"
    output = home / "install.cc"
    pending = home / ".cupidinstall-pending.cc"

    def invoke(contents: bytes, marker: bytes) -> None:
        request.write_bytes(contents)
        result = subprocess.run([runner, scratch], check=True, capture_output=True, timeout=90)
        assert marker in result.stdout, result.stdout
        assert not pending.exists()

    expected = scratch / "expected.cc"
    cases = {
        "bin": ("--bin", "bin/ls.cc", "--headers", "bin/shell.h",
                "--browser", "bin/browser/css.cc"),
        "docs": ("--ctxt", "cupidos-txt/00INDEX.CTXT", "--doc-assets", "image.bmp",
                 "--home-assets", "snail.bmp", "test.png"),
        "demos": ("--demos", "demos/hello.asm", "demos/loop.asm"),
    }
    for kind, arguments in cases.items():
        subprocess.run([*command, kind, *arguments, "-o", expected],
                       cwd=root, check=True, timeout=120)
        lines = ["/home/install.cc", kind]
        lines.extend(argument if argument.startswith("--") else f"/{argument}"
                     for argument in arguments)
        invoke(("\n".join(lines) + "\n").encode("ascii"),
               b"[cupidinstall-guest] wrote /home/install.cc")
        assert output.read_bytes() == expected.read_bytes()

    previous = output.read_bytes()
    header = b"/home/install.cc\n"
    for body in (
        b"missing\n--demos\n/demos/hello.asm\n",
        b"bin\n--headers\n/bin/shell.h\n--bin\n--browser\n",
        b"bin\n--bin\n--headers\n--browser\n",
        b"demos\n--demos\n/../demos/hello.asm\n",
        b"demos\n--demos\n/demos/hello.asm\n--demos\n",
        b"demos\n--demos\n" + b"/demos/hello.asm\n" * 513,
    ):
        invoke(header + body, b"[cupidinstall-guest] invalid request")
        assert output.read_bytes() == previous
    invoke(header + b"demos\n--demos\n/demos/hello.cc\n",
           b"[cupidinstall-guest] generation failed: input")
    assert output.read_bytes() == previous
    print("Guest installation sources matched checked CupidObj and survived seven rejections.")


if __name__ == "__main__":
    root, runner = (Path(path).resolve() for path in sys.argv[1:])
    with tempfile.TemporaryDirectory(dir=runner.parent, prefix="guest-install-") as temporary:
        check(root, runner, Path(temporary))

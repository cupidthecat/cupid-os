"""Stage the engine build inputs for checked CupidBuild ISO publication."""

import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


TOOLS = ("cupidasm", "cupidbuild", "cupidc", "cupiddis", "cupidld", "cupidobj")


def publish_command(publisher, scratch, output):
    return [
        publisher, "publish-iso-fixture", "--root", scratch,
        "--manifest", "manifest", "--fixtures", "fixtures", "--output", output,
        "--linux-manifest", "bootstrap/seeds/i386-linux/manifest.json",
        "--windows-manifest", "bootstrap/seeds/i386-windows/manifest.json",
        "--seed-release", "bootstrap/seeds/release.json",
    ]


def write_manifest(path, members):
    path.write_bytes(("\n".join(members) + "\n").encode("ascii"))


def stage_inputs(root, engine, members, fixtures):
    fixtures.mkdir()
    for logical in members:
        relative = Path(logical)
        if relative.is_absolute() or relative.anchor or ".." in relative.parts:
            raise ValueError(f"invalid engine ISO path: {logical}")
        source = engine if logical == engine.name else root / relative
        target = fixtures / relative
        information = source.lstat()
        if stat.S_ISDIR(information.st_mode):
            target.mkdir()
        elif stat.S_ISREG(information.st_mode):
            shutil.copyfile(source, target)
        else:
            raise ValueError(f"engine ISO input is not a regular file or directory: {source}")


def stage_seed_pair(root, scratch):
    source = root / "bootstrap/seeds"
    destination = scratch / "bootstrap/seeds"
    destination.mkdir(parents=True)
    shutil.copyfile(source / "release.json", destination / "release.json")
    for target, extension in (("i386-linux", "elf"), ("i386-windows", "exe")):
        target_dir = destination / target
        target_dir.mkdir()
        for name in ("manifest.json", *(f"{tool}.{extension}" for tool in TOOLS)):
            shutil.copy2(source / target / name, target_dir / name)


def publish_source_iso(publisher, root, engine, output, members):
    with tempfile.TemporaryDirectory(dir=output.parent, prefix="iso-inputs-") as temporary:
        scratch = Path(temporary)
        write_manifest(scratch / "manifest", members)
        stage_inputs(root, engine, members, scratch / "fixtures")
        stage_seed_pair(root, scratch)
        result = subprocess.run(publish_command(publisher, scratch, "candidate.iso"), timeout=180)
        if result.returncode:
            return result.returncode

        candidate = scratch / "candidate.iso"
        try:
            prior = output.read_bytes()
        except FileNotFoundError:
            prior = None
        if candidate.read_bytes() != prior:
            candidate.replace(output)
    return 0


if __name__ == "__main__":
    publisher, root, engine, output = (Path(arg).absolute() for arg in sys.argv[1:5])
    sys.exit(publish_source_iso(publisher, root, engine, output, sys.argv[5:]))

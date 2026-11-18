"""Read back every source ISO file and check failed publication."""

import subprocess
import sys
import tempfile
from pathlib import Path

from source_iso import (
    publish_command, publish_source_iso, stage_inputs, stage_seed_pair, write_manifest,
)


def verify_contents(root, engine, members, image, extracted):
    expected = set(members)
    subprocess.run(
        ["xorriso", "-osirrox", "on", "-indev", image, "-extract", "/", extracted],
        check=True, capture_output=True, timeout=60,
    )
    imported = {path.relative_to(extracted).as_posix(): path for path in extracted.rglob("*")}
    if set(imported) != expected:
        raise RuntimeError("ISO paths differ from the manifest")

    files = 0
    for logical, path in imported.items():
        original = engine if logical == engine.name else root / logical
        if original.is_dir() and path.is_dir():
            continue
        if not original.is_file() or not path.is_file() or path.read_bytes() != original.read_bytes():
            raise RuntimeError(f"ISO member differs: {logical}")
        files += 1
    print(f"ISO readback matched {files} files and {len(expected) - files} directories", flush=True)


def check_incomplete_inventory(publisher, root, engine, members, scratch):
    write_manifest(scratch / "manifest", members)
    stage_inputs(root, engine, [member for member in members if member != engine.name],
                 scratch / "fixtures")
    stage_seed_pair(root, scratch)
    output = scratch / "protected.iso"
    sentinel = b"keep the preceding ISO"
    output.write_bytes(sentinel)
    result = subprocess.run(
        publish_command(publisher, scratch, output.name),
        capture_output=True, text=True, timeout=60,
    )
    if result.returncode == 0 or output.read_bytes() != sentinel:
        raise RuntimeError(f"incomplete inventory did not preserve the output: {result}")
    print("Incomplete ISO inventory rejected; preceding output preserved", flush=True)


def check_unchanged_publication(publisher, root, engine, image, members):
    timestamp = image.stat().st_mtime_ns
    contents = image.read_bytes()
    if publish_source_iso(publisher, root, engine, image, members) != 0:
        raise RuntimeError("rebuilding the same ISO failed")
    if image.stat().st_mtime_ns != timestamp or image.read_bytes() != contents:
        raise RuntimeError("rebuilding the same ISO changed its bytes or timestamp")
    print("Unchanged ISO kept its bytes and timestamp", flush=True)


if __name__ == "__main__":
    publisher, root, engine, image = (Path(arg).absolute() for arg in sys.argv[1:5])
    members = sys.argv[5:]
    with tempfile.TemporaryDirectory(dir=image.parent, prefix="iso-check-") as temporary:
        scratch = Path(temporary)
        verify_contents(root, engine, members, image, scratch / "tree")
        check_incomplete_inventory(publisher, root, engine, members, scratch)
    check_unchanged_publication(publisher, root, engine, image, members)

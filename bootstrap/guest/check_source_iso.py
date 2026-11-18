"""Read back every source ISO file and check failed publication."""

import subprocess
import sys
import tempfile
from pathlib import Path


def verify_contents(root, engine, manifest, image, extracted):
    expected = set(manifest.read_text(encoding="ascii").splitlines())
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


def check_incomplete_inventory(runner, engine, manifest, output):
    sentinel = b"keep the preceding ISO"
    output.write_bytes(sentinel)
    result = subprocess.run(
        [*runner, "--tool", "cupidobj", "--", "iso-fixture", manifest,
         "--file", engine.name, engine, "-o", output],
        capture_output=True, text=True, timeout=60,
    )
    if (result.returncode == 0 or "manifest entry has no typed input" not in result.stderr or
        output.read_bytes() != sentinel):
        raise RuntimeError(f"incomplete inventory did not preserve the output: {result}")
    print("Incomplete ISO inventory rejected; preceding output preserved", flush=True)


if __name__ == "__main__":
    root, engine, manifest, image = (Path(argument).resolve() for argument in sys.argv[1:5])
    if sys.argv[5] != "--":
        raise SystemExit("expected '--' before the checked runner")
    with tempfile.TemporaryDirectory(dir=image.parent, prefix="iso-check-") as temporary:
        scratch = Path(temporary)
        verify_contents(root, engine, manifest, image, scratch / "tree")
        check_incomplete_inventory(sys.argv[6:], engine, manifest, scratch / "protected.iso")

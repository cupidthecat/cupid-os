"""Copy tracked wiki files into a separate GitHub wiki checkout."""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile


def _git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def tracked_changes(root, before=""):
    current = _git(root, "ls-files", "-z", "--", "wiki/")
    deleted = b""
    if before and set(before) != {"0"}:
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", before):
            raise ValueError("before must be a complete Git commit ID")
        _git(root, "cat-file", "-e", before + "^{commit}")
        deleted = _git(root, "diff", "--no-renames", "--diff-filter=D", "--name-only",
                       "-z", before, "HEAD", "--", "wiki/")

    def paths(payload):
        result = []
        for item in payload.split(b"\0"):
            if item:
                name = os.fsdecode(item)
                if not name.startswith("wiki/"):
                    raise ValueError("Git returned a path outside wiki/")
                result.append(name[len("wiki/"):])
        return result

    return paths(current), paths(deleted)


def _relative(name):
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or path.as_posix() != name or "\\" in name
            or any(part in (".", "..") or part.lower() == ".git" or ":" in part
                   for part in path.parts)):
        raise ValueError(f"invalid wiki path: {name!r}")
    return Path(*path.parts)


def _check_path(root, relative, *, required=False):
    path = root
    for part in (None, *relative.parts):
        if part is not None:
            path = path / part
        try:
            info = path.lstat()
        except FileNotFoundError:
            if required:
                raise
            continue
        if path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"wiki path must not follow a link: {path}")
        expected = stat.S_ISREG if path == root / relative else stat.S_ISDIR
        if not expected(info.st_mode):
            raise ValueError(f"unexpected wiki path type: {path}")
    return root / relative


def sync(source, destination, current, deleted):
    """Validate the complete plan before changing any published file."""
    source, destination = Path(source).absolute(), Path(destination).absolute()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("source and destination wiki directories must be separate")
    current = [_relative(name) for name in current]
    deleted = [_relative(name) for name in deleted]
    names = [path.as_posix().casefold() for path in current + deleted]
    if len(names) != len(set(names)):
        raise ValueError("duplicate or conflicting wiki paths")
    writes, removals = [], []
    for relative in current:
        contents = _check_path(source, relative, required=True).read_bytes()
        target = _check_path(destination, relative)
        if not target.exists() or target.read_bytes() != contents:
            writes.append((target, contents))
    for relative in deleted:
        target = _check_path(destination, relative)
        if target.exists():
            removals.append(target)
    for target, contents in writes:
        target.parent.mkdir(parents=True, exist_ok=True)
        descriptor, name = tempfile.mkstemp(prefix=".wiki-sync-", dir=target.parent)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(contents)
            os.replace(name, target)
        finally:
            if os.path.exists(name):
                os.unlink(name)
    for target in removals:
        target.unlink()
    return len(writes), len(removals)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Cupid OS repository")
    parser.add_argument("--destination", type=Path, required=True, help="wiki Git checkout")
    parser.add_argument("--before", default="", help="push event's previous commit ID")
    args = parser.parse_args(argv)
    try:
        current, deleted = tracked_changes(args.source, args.before)
        written, removed = sync(args.source / "wiki", args.destination, current, deleted)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"wiki sync failed: {error}\n")
    print(f"Wiki files updated: {written}; removed: {removed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

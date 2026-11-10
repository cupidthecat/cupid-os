"""Copy tracked wiki files into a separate GitHub wiki checkout."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import stat
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit


REQUIRED_PAGES = frozenset(("Home.md", "_Sidebar.md", "Getting-Started.md"))
STATE_FILE = ".cupid-wiki-sync.json"


def _git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def tracked_changes(root, before=""):
    current = _git(root, "ls-files", "-z", "--", "wiki/")
    deleted = b""
    if before and not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", before):
        raise ValueError("before must be a complete Git commit ID")
    if before and set(before) != {"0"}:
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
    name.encode("utf-8")
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or path.as_posix() != name or "\\" in name
            or name == STATE_FILE or any(ord(char) < 32 or ord(char) == 127 for char in name)
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


def pending_deletions(destination, current, deleted):
    path = _check_path(destination, Path(STATE_FILE))
    if not path.exists():
        return deleted
    state = json.loads(path.read_text(encoding="utf-8"))
    if (not isinstance(state, dict) or set(state) != {"schema", "source_revision", "files"}
            or type(state["schema"]) is not int or state["schema"] != 1
            or type(state["files"]) is not list
            or type(state["source_revision"]) is not str
            or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", state["source_revision"])
            or any(type(name) is not str for name in state["files"])):
        raise ValueError("invalid published wiki synchronization state")
    managed = [_relative(name).as_posix() for name in state["files"]]
    if len(managed) != len(set(managed)):
        raise ValueError("duplicate published wiki paths")
    return sorted(set(deleted) | (set(managed) - set(current)))


def _replace_bytes(target, contents):
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=".wiki-sync-", dir=target.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(contents)
        os.replace(name, target)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def _link_targets(text):
    lines = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line)
        if fence:
            if (marker and marker[1][0] == fence[0] and len(marker[1]) >= fence[1]
                    and not marker[2].strip()):
                fence = None
        elif marker:
            fence = (marker[1][0], len(marker[1]))
        elif not line.startswith(("    ", "\t")):
            lines.append(line)
    text = re.sub(r"(`+).*?\1", "", "\n".join(lines))
    # Inline links/images and reference definitions share the same destination rules.
    for match in re.finditer(r'\]\(\s*(?:<([^>\n]+)>|([^\s)]+))', text):
        yield match[1] or match[2]
    references = {}
    for match in re.finditer(r'^ {0,3}\[([^\]\n]+)\]:\s*(?:<([^>\n]+)>|(\S+))', text, re.M):
        references[' '.join(match[1].split()).casefold()] = match[2] or match[3]
        yield match[2] or match[3]
    for match in re.finditer(r'\[([^\]\n]+)\]\[([^\]\n]*)\]', text):
        label = ' '.join((match[2] or match[1]).split()).casefold()
        if label not in references:
            raise ValueError(f"undefined wiki link reference: {label}")
    for match in re.finditer(r'\[\[([^\]\n]+)\]\]', text):
        yield match[1].split("|")[-1]


def validate(source, destination, current, deleted):
    """Check required pages and local link targets before publication."""
    current = [_relative(name).as_posix() for name in current]
    deleted = [_relative(name).as_posix() for name in deleted]
    folded = [name.casefold() for name in current + deleted]
    if len(folded) != len(set(folded)):
        raise ValueError("duplicate or conflicting wiki paths")
    deleted = set(deleted)
    missing = REQUIRED_PAGES - set(current)
    if missing:
        raise ValueError("required wiki pages missing: " + ", ".join(sorted(missing)))
    info = destination.lstat()
    if (not stat.S_ISDIR(info.st_mode) or destination.is_symlink()
            or getattr(info, "st_file_attributes", 0) & 0x400):
        raise ValueError("published wiki must be an unlinked directory")
    available = set(current)
    for parent, directories, files in os.walk(destination, followlinks=False):
        directories[:] = [name for name in directories if name.lower() != ".git"
                          and not (Path(parent) / name).is_symlink()]
        for name in files:
            path = Path(parent) / name
            if not path.is_symlink():
                available.add(path.relative_to(destination).as_posix())
    available -= deleted
    pages = {name[:-3] for name in available if name.endswith(".md")}
    for name in current:
        path = _check_path(source, Path(name), required=True)
        if not name.endswith(".md"):
            continue
        text = path.read_text(encoding="utf-8")
        for target in _link_targets(text):
            url = urlsplit(target)
            if url.scheme in ("http", "https", "mailto") or target.startswith("//"):
                continue
            if url.scheme:
                raise ValueError(f"unsupported wiki link in {name}: {target}")
            if not url.path:
                continue
            relative = posixpath.normpath(posixpath.join(posixpath.dirname(name),
                                                       unquote(url.path)))
            _relative(relative)
            if relative not in available and relative not in pages:
                raise ValueError(f"missing wiki link target in {name}: {target}")


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
        _replace_bytes(target, contents)
    for target in removals:
        target.unlink()
    return len(writes), len(removals)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Cupid OS repository")
    parser.add_argument("--destination", type=Path, required=True, help="wiki Git checkout")
    parser.add_argument("--before", default="", help="push event's previous commit ID")
    parser.add_argument("--check", action="store_true", help="validate without copying files")
    args = parser.parse_args(argv)
    try:
        current, deleted = tracked_changes(args.source, args.before)
        deleted = pending_deletions(args.destination, current, deleted)
        validate(args.source / "wiki", args.destination, current, deleted)
        if args.check:
            print(f"Wiki validation passed: {len(current)} tracked files")
            return 0
        revision = _git(args.source, "rev-parse", "HEAD").decode("ascii").strip()
        if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", revision):
            raise ValueError("invalid source commit ID")
        written, removed = sync(args.source / "wiki", args.destination, current, deleted)
        state = {"schema": 1, "source_revision": revision, "files": sorted(current)}
        contents = (json.dumps(state, indent=2, sort_keys=True) + "\n").encode("utf-8")
        target = _check_path(args.destination, Path(STATE_FILE))
        if not target.exists() or target.read_bytes() != contents:
            _replace_bytes(target, contents)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"wiki sync failed: {error}\n")
    print(f"Wiki files updated: {written}; removed: {removed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

## @file
#  @brief Author deterministic ISO9660 fixtures and verify checked images.
"""Author the fixture ISO and keep its input and output checks together."""

from __future__ import annotations

import os
import re
import stat
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

if __package__:
    from .bootstrap_toolchain import BootstrapError, run_seed_tool, verify_seed_inputs
    from .hostbuild_disk import (
        DiskImageError, _acquire_disk_publication_lock, _ceil_div,
        _release_disk_publication_lock,
    )
else:
    from bootstrap_toolchain import BootstrapError, run_seed_tool, verify_seed_inputs
    from hostbuild_disk import (
        DiskImageError, _acquire_disk_publication_lock, _ceil_div,
        _release_disk_publication_lock,
    )


class IsoAuthoringError(RuntimeError):
    """A deterministic ISO fixture could not be authored safely."""


@dataclass(frozen=True)
class _IsoSource:
    name: str
    relative: str
    data: bytes | None
    children: tuple[_IsoSource, ...]

    @property
    def is_directory(self) -> bool:
        return self.data is None


@dataclass(frozen=True)
class _IsoManifest:
    path: Path
    fixture_root: Path
    payload: bytes
    entries: frozenset[str]


@dataclass
class _IsoNode:
    source: _IsoSource
    parent: _IsoNode | None
    identifier: bytes
    children: list[_IsoNode]
    extent: int = 0
    size: int = 0
    directory_number: int = 0

    @property
    def is_directory(self) -> bool:
        return self.source.is_directory


ISO_BLOCK_SIZE = 2048
ISO_VOLUME_ID = b"CUPID_OS_TEST"
_ISO_RECORDING_DATE = b"\x64\x01\x01\x00\x00\x00\x00"
_ISO_VOLUME_DATE = b"2000010100000000\x00"
_ISO_UNSPECIFIED_DATE = b"0000000000000000\x00"
_ISO_SP = b"SP\x07\x01\xbe\xef\x00"
_ISO_ER_ID = b"RRIP_1991A"
_ISO_ER_DESCRIPTION = (
    b"THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX "
    b"FILE SYSTEM SEMANTICS"
)
_ISO_ER_SOURCE = (
    b"PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE "
    b"PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT "
    b"INFORMATION."
)
_ISO_MAX_DATA_LENGTH = 0xFFFFFFFF
_ISO_MAX_DIRECTORY_DEPTH = 8


def _is_link_or_junction(path: Path) -> bool:
    try:
        if path.is_symlink():
            return True
        is_junction = getattr(os.path, "isjunction", None)
        if is_junction is not None and is_junction(path):
            return True
        try:
            status = path.lstat()
        except FileNotFoundError:
            return False
        reparse_point = getattr(
            stat,
            "FILE_ATTRIBUTE_REPARSE_POINT",
            0x0400,
        )
        attributes = getattr(status, "st_file_attributes", 0)
        return bool(attributes & reparse_point)
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO path cannot be inspected: {path}: {error}"
        ) from error


def _path_is_within(path: Path, directory: Path) -> bool:
    try:
        path.relative_to(directory)
    except ValueError:
        return False
    return True


def _resolve_iso_paths(fixtures: Path, out: Path) -> tuple[Path, Path]:
    if _is_link_or_junction(fixtures):
        raise IsoAuthoringError(
            f"ISO fixture tree may not be a symbolic link or junction: "
            f"{fixtures}"
        )
    try:
        fixture_root = fixtures.resolve(strict=True)
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture tree cannot be resolved: {fixtures}: {error}"
        ) from error
    try:
        mode = fixture_root.lstat().st_mode
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture tree cannot be inspected: "
            f"{fixture_root}: {error}"
        ) from error
    if not stat.S_ISDIR(mode):
        raise IsoAuthoringError(
            f"ISO fixture tree is not a directory: {fixture_root}"
        )

    unresolved_output = out.resolve(strict=False)
    if _path_is_within(unresolved_output, fixture_root):
        raise IsoAuthoringError(
            "ISO output may not be inside the fixture tree"
        )
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        output_parent = out.parent.resolve(strict=True)
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO output directory cannot be prepared: "
            f"{out.parent}: {error}"
        ) from error
    output = output_parent / out.name
    if _path_is_within(output, fixture_root):
        raise IsoAuthoringError(
            "ISO output may not be inside the fixture tree"
        )
    return fixture_root, output


def _read_iso_output(output: Path) -> bytes | None:
    if _is_link_or_junction(output):
        raise IsoAuthoringError(
            f"ISO output may not be a symbolic link or junction: {output}"
        )
    try:
        mode = output.lstat().st_mode
    except FileNotFoundError:
        return None
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO output cannot be inspected: {output}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"ISO output is not a regular file: {output}"
        )
    try:
        return output.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO output cannot be read: {output}: {error}"
        ) from error


def _validate_iso_component(name: str, relative: str) -> None:
    try:
        encoded = name.encode("ascii")
    except UnicodeEncodeError as error:
        raise IsoAuthoringError(
            f"ISO fixture name must be ASCII: {relative}"
        ) from error
    if not encoded or any(byte < 0x20 or byte == 0x7F for byte in encoded):
        raise IsoAuthoringError(
            f"ISO fixture name contains an unsupported character: "
            f"{relative}"
        )
    if re.fullmatch(r"[A-Za-z0-9._-]+", name) is None:
        raise IsoAuthoringError(
            "ISO fixture names may use only portable filename characters "
            f"(letters, digits, dot, underscore, and dash): {relative}"
        )
    if len(encoded) > 127:
        raise IsoAuthoringError(
            "ISO fixture name exceeds the 127-byte guest directory record "
            f"limit: {relative}"
        )


def _snapshot_iso_entry(
    path: Path,
    relative: str,
    directory_depth: int = 1,
) -> _IsoSource:
    if _is_link_or_junction(path):
        raise IsoAuthoringError(
            f"ISO fixture may not be a symbolic link or junction: "
            f"{relative}"
        )
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture cannot be inspected: {relative}: {error}"
        ) from error

    if stat.S_ISDIR(mode):
        if directory_depth > _ISO_MAX_DIRECTORY_DEPTH:
            raise IsoAuthoringError(
                "ISO fixture directory exceeds the ECMA-119 eight-level "
                f"primary hierarchy limit: {relative}"
            )
        try:
            paths = list(path.iterdir())
        except OSError as error:
            raise IsoAuthoringError(
                f"ISO fixture directory cannot be read: "
                f"{relative or '.'}: {error}"
            ) from error
        names: dict[str, str] = {}
        for child in paths:
            child_relative = (
                f"{relative}/{child.name}" if relative else child.name
            )
            _validate_iso_component(child.name, child_relative)
            folded = child.name.lower()
            if folded in names:
                raise IsoAuthoringError(
                    "ISO fixture directory has a case-insensitive name "
                    f"collision: {names[folded]} and {child_relative}"
                )
            names[folded] = child_relative
        children = tuple(
            _snapshot_iso_entry(
                child,
                f"{relative}/{child.name}" if relative else child.name,
                directory_depth + 1,
            )
            for child in sorted(
                paths,
                key=lambda candidate: (
                    candidate.name.lower(),
                    candidate.name,
                ),
            )
        )
        return _IsoSource(
            name=path.name if relative else "",
            relative=relative,
            data=None,
            children=children,
        )

    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"ISO fixture is not a regular file or directory: {relative}"
        )
    try:
        size = path.stat().st_size
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture size cannot be read: {relative}: {error}"
        ) from error
    if size > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            f"ISO fixture exceeds the 32-bit data-length limit: {relative}"
        )
    try:
        data = path.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture cannot be read: {relative}: {error}"
        ) from error
    if len(data) > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            f"ISO fixture exceeds the 32-bit data-length limit: {relative}"
        )
    return _IsoSource(
        name=path.name,
        relative=relative,
        data=data,
        children=(),
    )


def _snapshot_iso_tree(fixtures: Path) -> _IsoSource:
    return _snapshot_iso_entry(fixtures, "")


def _read_iso_manifest(
    manifest: Path,
    fixtures: Path,
) -> _IsoManifest:
    if _is_link_or_junction(manifest):
        raise IsoAuthoringError(
            "ISO fixture manifest may not be a symbolic link or junction: "
            f"{manifest}"
        )
    try:
        resolved = manifest.resolve(strict=True)
        mode = resolved.lstat().st_mode
        payload = resolved.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture manifest cannot be read: {manifest}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"ISO fixture manifest is not a regular file: {resolved}"
        )
    if _path_is_within(resolved, fixtures):
        raise IsoAuthoringError(
            "ISO fixture manifest must be outside the fixture tree"
        )
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError as error:
        raise IsoAuthoringError(
            "ISO fixture manifest must contain ASCII paths"
        ) from error

    entries: dict[str, str] = {}
    for line_number, entry in enumerate(text.splitlines(), 1):
        if not entry:
            raise IsoAuthoringError(
                "ISO fixture manifest contains a blank entry at line "
                f"{line_number}"
            )
        if entry != entry.strip() or any(
            character.isspace() for character in entry
        ):
            raise IsoAuthoringError(
                "ISO fixture manifest paths may not contain whitespace: "
                f"line {line_number}"
            )
        if "\\" in entry:
            raise IsoAuthoringError(
                "ISO fixture manifest paths must use forward slashes: "
                f"line {line_number}"
            )
        normalized = PurePosixPath(entry)
        if (
            normalized.is_absolute()
            or normalized.as_posix() != entry
            or any(part in {".", ".."} for part in normalized.parts)
        ):
            raise IsoAuthoringError(
                "ISO fixture manifest path is not normalized and relative: "
                f"line {line_number}: {entry}"
            )
        for component in normalized.parts:
            _validate_iso_component(component, entry)
        folded = entry.lower()
        if folded in entries:
            raise IsoAuthoringError(
                "ISO fixture manifest has a duplicate or case-insensitive "
                f"collision: {entries[folded]} and {entry}"
            )
        entries[folded] = entry
    if not entries:
        raise IsoAuthoringError("ISO fixture manifest is empty")
    return _IsoManifest(
        path=resolved,
        fixture_root=fixtures,
        payload=payload,
        entries=frozenset(entries.values()),
    )


def _iso_snapshot_paths(snapshot: _IsoSource) -> frozenset[str]:
    paths: set[str] = set()
    pending = list(snapshot.children)
    while pending:
        source = pending.pop()
        paths.add(source.relative)
        pending.extend(source.children)
    return frozenset(paths)


def _validate_iso_manifest(
    snapshot: _IsoSource,
    manifest: _IsoManifest,
) -> None:
    paths = _iso_snapshot_paths(snapshot)
    undeclared = sorted(paths - manifest.entries)
    if undeclared:
        raise IsoAuthoringError(
            "ISO fixture path is not declared in the ISO fixture manifest: "
            f"{undeclared[0]}"
        )
    missing = sorted(manifest.entries - paths)
    if missing:
        raise IsoAuthoringError(
            "ISO fixture manifest entry does not exist: "
            f"{missing[0]}"
        )


def _verify_iso_manifest(manifest: _IsoManifest) -> None:
    try:
        current = _read_iso_manifest(
            manifest.path,
            manifest.fixture_root,
        )
    except IsoAuthoringError as error:
        raise IsoAuthoringError(
            f"ISO fixture manifest changed while authoring: {error}"
        ) from error
    if current.payload != manifest.payload:
        raise IsoAuthoringError(
            "ISO fixture manifest changed while authoring the image"
        )


def _reject_iso_output_alias(
    fixtures: Path,
    snapshot: _IsoSource,
    output: Path,
) -> None:
    if not output.exists():
        return
    pending = [snapshot]
    while pending:
        source = pending.pop()
        if source.is_directory:
            pending.extend(source.children)
            continue
        fixture = fixtures / Path(source.relative)
        try:
            aliases_fixture = os.path.samefile(output, fixture)
        except OSError as error:
            raise IsoAuthoringError(
                f"ISO path identity cannot be checked: "
                f"{source.relative}: {error}"
            ) from error
        if aliases_fixture:
            raise IsoAuthoringError(
                "ISO output may not be a hard link to a fixture: "
                f"{source.relative}"
            )


def _iso_paths_alias(output: Path, input_path: Path) -> bool:
    if output == input_path:
        return True
    if not output.exists() or not input_path.exists():
        return False
    try:
        return os.path.samefile(output, input_path)
    except FileNotFoundError:
        return False
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO path identity cannot be checked: {input_path}: {error}"
        ) from error


def _resolve_checked_iso_seed(seed_manifest: Path):
    if _is_link_or_junction(seed_manifest):
        raise IsoAuthoringError(
            "checked seed manifest may not be a symbolic link or junction: "
            f"{seed_manifest}"
        )
    try:
        manifest = seed_manifest.resolve(strict=True)
        mode = manifest.lstat().st_mode
    except OSError as error:
        raise IsoAuthoringError(
            f"checked seed manifest cannot be resolved: "
            f"{seed_manifest}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise IsoAuthoringError(
            f"checked seed manifest is not a regular file: {manifest}"
        )
    try:
        checked_seed = verify_seed_inputs(manifest)
    except BootstrapError as error:
        raise IsoAuthoringError(
            f"checked seed could not be verified: {error}"
        ) from error
    return manifest, checked_seed


def _verify_checked_iso_seed(
    manifest: Path,
    expected_manifest_sha256: str,
) -> None:
    try:
        current_seed = verify_seed_inputs(manifest)
    except BootstrapError as error:
        raise IsoAuthoringError(
            "checked seed inputs changed while authoring the ISO image: "
            f"{error}"
        ) from error
    if current_seed.manifest_sha256 != expected_manifest_sha256:
        raise IsoAuthoringError(
            "checked seed inputs changed while authoring the ISO image: "
            "manifest content differs"
        )


def _acquire_iso_publication_lock(output: Path) -> tuple[object, str]:
    try:
        return _acquire_disk_publication_lock(output)
    except DiskImageError as error:
        raise IsoAuthoringError(str(error)) from error


def _iso_snapshot_inventory(snapshot: _IsoSource) -> list[_IsoSource]:
    inventory: list[_IsoSource] = []
    pending = list(snapshot.children)
    while pending:
        source = pending.pop()
        inventory.append(source)
        pending.extend(source.children)
    return sorted(
        inventory,
        key=lambda source: (
            source.relative.lower(),
            source.relative,
        ),
    )


def _write_private_iso_file(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def _run_checked_iso_author(
    *,
    seed_manifest: Path,
    snapshot: _IsoSource,
    manifest: _IsoManifest,
) -> bytes:
    try:
        with tempfile.TemporaryDirectory(
            prefix=".cupid-iso-author-",
        ) as temporary:
            private = Path(temporary)
            inventory = _iso_snapshot_inventory(snapshot)
            native_files: dict[str, Path] = {}
            file_index = 0
            for source in inventory:
                if source.is_directory:
                    continue
                native = private / "inputs" / f"{file_index:04d}.bin"
                _write_private_iso_file(native, source.data or b"")
                native_files[source.relative] = native
                file_index += 1

            frozen_manifest = private / "fixtures.manifest"
            _write_private_iso_file(frozen_manifest, manifest.payload)
            checked_output = private / "checked.iso"
            arguments: list[str | Path] = [
                "iso-fixture",
                frozen_manifest,
            ]
            for source in inventory:
                arguments.extend(
                    ("--directory", source.relative)
                    if source.is_directory
                    else (
                        "--file",
                        source.relative,
                        native_files[source.relative],
                    )
                )
            arguments.extend(("-o", checked_output))
            try:
                generated = run_seed_tool(
                    seed_manifest,
                    private,
                    "cupidobj",
                    arguments,
                    timeout=60,
                )
            except BootstrapError as error:
                raise IsoAuthoringError(
                    f"checked CupidObj could not run: {error}"
                ) from error
            if generated.returncode != 0:
                details = (
                    generated.stderr or generated.stdout or ""
                ).strip()
                suffix = f": {details}" if details else ""
                raise IsoAuthoringError(
                    "checked CupidObj failed with status "
                    f"{generated.returncode}{suffix}"
                )
            checked_image = _read_iso_output(checked_output)
            if checked_image is None:
                raise IsoAuthoringError(
                    "checked CupidObj reported success without an ISO image"
                )
            return checked_image
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"checked CupidObj ISO workspace failed: {error}"
        ) from error


def _iso_sanitize_identifier(value: str) -> str:
    identifier = "".join(
        character if character in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
        else "_"
        for character in value.upper()
    )
    return identifier or "_"


def _iso_identifier_parts(source: _IsoSource) -> tuple[str, str]:
    name = source.name
    if source.is_directory:
        return _iso_sanitize_identifier(name)[:8], ""
    if "." in name and not name.startswith(".") and not name.endswith("."):
        stem, extension = name.rsplit(".", 1)
    else:
        stem, extension = name, ""
    return (
        _iso_sanitize_identifier(stem)[:8],
        _iso_sanitize_identifier(extension)[:3] if extension else "",
    )


def _allocate_iso_identifier(
    source: _IsoSource,
    used: set[bytes],
) -> bytes:
    stem, extension = _iso_identifier_parts(source)
    sequence = 0
    while True:
        candidate_stem = stem
        if sequence:
            suffix = f"_{sequence}"
            if len(suffix) >= 8:
                raise IsoAuthoringError(
                    "ISO identifier collision space is exhausted in "
                    f"{source.relative}"
                )
            candidate_stem = stem[: 8 - len(suffix)] + suffix
        if source.is_directory:
            text = candidate_stem
        else:
            text = f"{candidate_stem}.{extension};1"
        identifier = text.encode("ascii")
        if identifier not in used:
            used.add(identifier)
            return identifier
        sequence += 1


def _build_iso_layout(
    source: _IsoSource,
    parent: _IsoNode | None = None,
    identifier: bytes = b"\x00",
) -> _IsoNode:
    node = _IsoNode(
        source=source,
        parent=parent,
        identifier=identifier,
        children=[],
    )
    used: set[bytes] = set()
    for child_source in source.children:
        child_identifier = _allocate_iso_identifier(child_source, used)
        node.children.append(
            _build_iso_layout(
                child_source,
                parent=node,
                identifier=child_identifier,
            )
        )
    node.children.sort(key=lambda child: child.identifier)
    return node


def _iso_both_u16(value: int) -> bytes:
    return struct.pack("<H", value) + struct.pack(">H", value)


def _iso_both_u32(value: int) -> bytes:
    return struct.pack("<I", value) + struct.pack(">I", value)


def _iso_er_entry() -> bytes:
    length = (
        8
        + len(_ISO_ER_ID)
        + len(_ISO_ER_DESCRIPTION)
        + len(_ISO_ER_SOURCE)
    )
    return (
        b"ER"
        + bytes(
            (
                length,
                1,
                len(_ISO_ER_ID),
                len(_ISO_ER_DESCRIPTION),
                len(_ISO_ER_SOURCE),
                1,
            )
        )
        + _ISO_ER_ID
        + _ISO_ER_DESCRIPTION
        + _ISO_ER_SOURCE
    )


def _iso_nm_entry(name: str, relative: str) -> bytes:
    encoded = name.encode("ascii")
    length = 5 + len(encoded)
    if length > 255:
        raise IsoAuthoringError(
            f"Rock Ridge name is too long for a SUSP entry: {relative}"
        )
    return b"NM" + bytes((length, 1, 0)) + encoded


def _iso_px_entry(node: _IsoNode) -> bytes:
    if node.is_directory:
        mode = 0o040555
        links = 2 + sum(
            1 for child in node.children if child.is_directory
        )
    else:
        mode = 0o100444
        links = 1
    return (
        b"PX"
        + bytes((36, 1))
        + _iso_both_u32(mode)
        + _iso_both_u32(links)
        + _iso_both_u32(0)
        + _iso_both_u32(0)
    )


def _iso_tf_entry() -> bytes:
    return (
        b"TF"
        + bytes((26, 1, 0x0E))
        + _ISO_RECORDING_DATE
        + _ISO_RECORDING_DATE
        + _ISO_RECORDING_DATE
    )


def _iso_ce_entry(extent: int, length: int) -> bytes:
    return (
        b"CE"
        + bytes((28, 1))
        + _iso_both_u32(extent)
        + _iso_both_u32(0)
        + _iso_both_u32(length)
    )


def _iso_directory_record(
    *,
    extent: int,
    size: int,
    identifier: bytes,
    directory: bool,
    susp: bytes,
    label: str,
) -> bytes:
    if len(identifier) > 255:
        raise IsoAuthoringError(
            f"ISO identifier is too long: {label}"
        )
    padding = 1 if len(identifier) % 2 == 0 else 0
    length = 33 + len(identifier) + padding + len(susp)
    terminal_padding = length % 2
    length += terminal_padding
    if length > 255:
        raise IsoAuthoringError(
            f"Rock Ridge directory record is too long: {label}"
        )
    record = bytearray(length)
    record[0] = length
    record[1] = 0
    record[2:10] = _iso_both_u32(extent)
    record[10:18] = _iso_both_u32(size)
    record[18:25] = _ISO_RECORDING_DATE
    record[25] = 0x02 if directory else 0
    record[26] = 0
    record[27] = 0
    record[28:32] = _iso_both_u16(1)
    record[32] = len(identifier)
    record[33 : 33 + len(identifier)] = identifier
    susp_offset = 33 + len(identifier) + padding
    record[susp_offset : susp_offset + len(susp)] = susp
    return bytes(record)


def _iso_pack_directory(records: list[bytes]) -> bytes:
    payload = bytearray()
    for record in records:
        remaining = ISO_BLOCK_SIZE - (len(payload) % ISO_BLOCK_SIZE)
        if len(record) > remaining:
            payload.extend(b"\x00" * remaining)
        payload.extend(record)
    if len(payload) % ISO_BLOCK_SIZE:
        payload.extend(
            b"\x00" * (ISO_BLOCK_SIZE - len(payload) % ISO_BLOCK_SIZE)
        )
    return bytes(payload)


def _iso_directory_payload(
    node: _IsoNode,
    continuation_extent: int,
    continuation_length: int,
) -> bytes:
    if node.parent is None:
        parent = node
        dot_susp = (
            _ISO_SP
            + _iso_px_entry(node)
            + _iso_tf_entry()
            + _iso_ce_entry(
                continuation_extent,
                continuation_length,
            )
        )
    else:
        parent = node.parent
        dot_susp = _iso_px_entry(node) + _iso_tf_entry()
    records = [
        _iso_directory_record(
            extent=node.extent,
            size=node.size,
            identifier=b"\x00",
            directory=True,
            susp=dot_susp,
            label=f"{node.source.relative or '.'}/.",
        ),
        _iso_directory_record(
            extent=parent.extent,
            size=parent.size,
            identifier=b"\x01",
            directory=True,
            susp=_iso_px_entry(parent) + _iso_tf_entry(),
            label=f"{node.source.relative or '.'}/..",
        ),
    ]
    for child in node.children:
        child_size = (
            child.size
            if child.is_directory
            else len(child.source.data or b"")
        )
        records.append(
            _iso_directory_record(
                extent=child.extent,
                size=child_size,
                identifier=child.identifier,
                directory=child.is_directory,
                susp=(
                    _iso_px_entry(child)
                    + _iso_tf_entry()
                    + _iso_nm_entry(
                        child.source.name,
                        child.source.relative,
                    )
                ),
                label=child.source.relative,
            )
        )
    return _iso_pack_directory(records)


def _iso_directory_order(root: _IsoNode) -> list[_IsoNode]:
    ordered = []
    queue = [root]
    while queue:
        directory = queue.pop(0)
        directory.directory_number = len(ordered) + 1
        ordered.append(directory)
        queue.extend(
            child for child in directory.children if child.is_directory
        )
    if len(ordered) > 0xFFFF:
        raise IsoAuthoringError(
            "ISO fixture tree has more than 65,535 directories"
        )
    return ordered


def _iso_file_order(root: _IsoNode) -> list[_IsoNode]:
    files = []
    queue = [root]
    while queue:
        node = queue.pop()
        if node.is_directory:
            queue.extend(reversed(node.children))
        else:
            files.append(node)
    return sorted(
        files,
        key=lambda node: (
            node.source.relative.lower(),
            node.source.relative,
        ),
    )


def _iso_path_table(
    directories: list[_IsoNode],
    byte_order: str,
) -> bytes:
    prefix = "<" if byte_order == "little" else ">"
    table = bytearray()
    for directory in directories:
        identifier = (
            b"\x00" if directory.parent is None else directory.identifier
        )
        parent_number = (
            1
            if directory.parent is None
            else directory.parent.directory_number
        )
        table.extend((len(identifier), 0))
        table.extend(struct.pack(f"{prefix}I", directory.extent))
        table.extend(struct.pack(f"{prefix}H", parent_number))
        table.extend(identifier)
        if len(identifier) % 2:
            table.append(0)
    return bytes(table)


def _iso_write_identifier(
    descriptor: bytearray,
    start: int,
    length: int,
    value: bytes,
) -> None:
    if len(value) > length:
        raise AssertionError("ISO descriptor identifier is too long")
    descriptor[start : start + length] = value.ljust(length, b" ")


def _iso_primary_volume_descriptor(
    *,
    volume_blocks: int,
    path_table_size: int,
    little_path_extent: int,
    big_path_extent: int,
    root: _IsoNode,
) -> bytes:
    descriptor = bytearray(ISO_BLOCK_SIZE)
    descriptor[:7] = b"\x01CD001\x01"
    _iso_write_identifier(descriptor, 8, 32, b"CUPID OS")
    _iso_write_identifier(descriptor, 40, 32, ISO_VOLUME_ID)
    descriptor[80:88] = _iso_both_u32(volume_blocks)
    descriptor[120:124] = _iso_both_u16(1)
    descriptor[124:128] = _iso_both_u16(1)
    descriptor[128:132] = _iso_both_u16(ISO_BLOCK_SIZE)
    descriptor[132:140] = _iso_both_u32(path_table_size)
    descriptor[140:144] = struct.pack("<I", little_path_extent)
    descriptor[144:148] = b"\x00" * 4
    descriptor[148:152] = struct.pack(">I", big_path_extent)
    descriptor[152:156] = b"\x00" * 4
    root_record = _iso_directory_record(
        extent=root.extent,
        size=root.size,
        identifier=b"\x00",
        directory=True,
        susp=b"",
        label="primary volume descriptor root",
    )
    descriptor[156 : 156 + len(root_record)] = root_record
    _iso_write_identifier(descriptor, 190, 128, b"CUPID_OS_TEST_FIXTURE")
    _iso_write_identifier(descriptor, 318, 128, b"CUPID OS")
    _iso_write_identifier(
        descriptor,
        446,
        128,
        b"CUPID OS REPOSITORY HOSTBUILD",
    )
    _iso_write_identifier(
        descriptor,
        574,
        128,
        b"CUPID OS DETERMINISTIC ISO9660 AUTHOR",
    )
    _iso_write_identifier(descriptor, 702, 37, b"")
    _iso_write_identifier(descriptor, 739, 37, b"")
    _iso_write_identifier(descriptor, 776, 37, b"")
    descriptor[813:830] = _ISO_VOLUME_DATE
    descriptor[830:847] = _ISO_VOLUME_DATE
    descriptor[847:864] = _ISO_UNSPECIFIED_DATE
    descriptor[864:881] = _ISO_VOLUME_DATE
    descriptor[881] = 1
    return bytes(descriptor)


def _render_iso_image(snapshot: _IsoSource) -> bytes:
    root = _build_iso_layout(snapshot)
    directories = _iso_directory_order(root)
    files = _iso_file_order(root)
    continuation = _iso_er_entry()

    for directory in directories:
        directory.size = len(
            _iso_directory_payload(
                directory,
                continuation_extent=0,
                continuation_length=len(continuation),
            )
        )

    empty_little_path = _iso_path_table(directories, "little")
    path_table_size = len(empty_little_path)
    if path_table_size > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            "ISO path table exceeds the 32-bit size limit"
        )
    path_table_blocks = _ceil_div(path_table_size, ISO_BLOCK_SIZE)
    little_path_extent = 18
    big_path_extent = little_path_extent + path_table_blocks
    next_extent = big_path_extent + path_table_blocks

    for directory in directories:
        directory.extent = next_extent
        next_extent += _ceil_div(directory.size, ISO_BLOCK_SIZE)
    continuation_extent = next_extent
    next_extent += _ceil_div(
        len(continuation),
        ISO_BLOCK_SIZE,
    )
    for file_node in files:
        data = file_node.source.data or b""
        if data:
            file_node.extent = next_extent
            next_extent += _ceil_div(len(data), ISO_BLOCK_SIZE)
        else:
            file_node.extent = 0
    if next_extent > _ISO_MAX_DATA_LENGTH:
        raise IsoAuthoringError(
            "ISO volume exceeds the 32-bit block-count limit"
        )

    little_path = _iso_path_table(directories, "little")
    big_path = _iso_path_table(directories, "big")
    if len(little_path) != path_table_size or len(big_path) != path_table_size:
        raise AssertionError("ISO path-table layout changed after placement")

    image = bytearray(next_extent * ISO_BLOCK_SIZE)
    descriptor = _iso_primary_volume_descriptor(
        volume_blocks=next_extent,
        path_table_size=path_table_size,
        little_path_extent=little_path_extent,
        big_path_extent=big_path_extent,
        root=root,
    )
    image[
        16 * ISO_BLOCK_SIZE : 17 * ISO_BLOCK_SIZE
    ] = descriptor
    terminator = bytearray(ISO_BLOCK_SIZE)
    terminator[:7] = b"\xffCD001\x01"
    image[
        17 * ISO_BLOCK_SIZE : 18 * ISO_BLOCK_SIZE
    ] = terminator
    little_start = little_path_extent * ISO_BLOCK_SIZE
    image[little_start : little_start + len(little_path)] = little_path
    big_start = big_path_extent * ISO_BLOCK_SIZE
    image[big_start : big_start + len(big_path)] = big_path
    continuation_start = continuation_extent * ISO_BLOCK_SIZE
    image[
        continuation_start : continuation_start + len(continuation)
    ] = continuation

    for directory in directories:
        payload = _iso_directory_payload(
            directory,
            continuation_extent=continuation_extent,
            continuation_length=len(continuation),
        )
        if len(payload) != directory.size:
            raise AssertionError(
                "ISO directory layout changed after placement"
            )
        start = directory.extent * ISO_BLOCK_SIZE
        image[start : start + len(payload)] = payload
    for file_node in files:
        data = file_node.source.data or b""
        if not data:
            continue
        start = file_node.extent * ISO_BLOCK_SIZE
        image[start : start + len(data)] = data
    return bytes(image)


def _verify_iso_snapshot(
    fixtures: Path,
    snapshot: _IsoSource,
) -> None:
    try:
        current = _snapshot_iso_tree(fixtures)
    except IsoAuthoringError as error:
        raise IsoAuthoringError(
            f"ISO fixture tree changed while authoring: {error}"
        ) from error
    if current != snapshot:
        raise IsoAuthoringError(
            "ISO fixture tree changed while authoring the image"
        )


def _publish_iso_image(
    *,
    fixtures: Path,
    snapshot: _IsoSource,
    output: Path,
    initial_output: bytes | None,
    image: bytes,
    manifest: _IsoManifest | None,
    seed_manifest: Path | None = None,
    seed_manifest_sha256: str | None = None,
) -> bool:
    if (seed_manifest is None) != (seed_manifest_sha256 is None):
        raise AssertionError("checked ISO seed evidence is incomplete")
    if initial_output == image:
        _verify_iso_snapshot(fixtures, snapshot)
        if manifest is not None:
            _verify_iso_manifest(manifest)
        if seed_manifest is not None and seed_manifest_sha256 is not None:
            _verify_checked_iso_seed(
                seed_manifest,
                seed_manifest_sha256,
            )
        if _read_iso_output(output) != initial_output:
            raise IsoAuthoringError(
                "ISO output changed while authoring the image"
            )
        return False

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}.iso-",
            dir=output.parent,
        ) as temporary:
            candidate = Path(temporary) / output.name
            with candidate.open("wb") as stream:
                stream.write(image)
                stream.flush()
                os.fsync(stream.fileno())
            _verify_iso_snapshot(fixtures, snapshot)
            if manifest is not None:
                _verify_iso_manifest(manifest)
            if seed_manifest is not None and seed_manifest_sha256 is not None:
                _verify_checked_iso_seed(
                    seed_manifest,
                    seed_manifest_sha256,
                )
            if _read_iso_output(output) != initial_output:
                raise IsoAuthoringError(
                    "ISO output changed while authoring the image"
                )
            os.replace(candidate, output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO image could not be published: {output}: {error}"
        ) from error
    return True


def _gen_big_with_seed(
    seed_manifest: Path,
    source: Path,
    output: Path,
    initial_output: bytes | None,
    expected: bytes,
) -> bool:
    if _is_link_or_junction(source):
        raise IsoAuthoringError(
            f"generated ISO fixture source may not be a symbolic link or "
            f"junction: {source}"
        )
    try:
        manifest = seed_manifest.resolve(strict=True)
        assembly = source.resolve(strict=True)
    except OSError as error:
        raise IsoAuthoringError(
            f"checked big-fixture input cannot be resolved: {error}"
        ) from error
    if not manifest.is_file():
        raise IsoAuthoringError(
            f"checked seed manifest is not a regular file: {manifest}"
        )
    if not assembly.is_file():
        raise IsoAuthoringError(
            f"generated ISO fixture source is not a regular file: {assembly}"
        )
    if output in {manifest, assembly}:
        raise IsoAuthoringError(
            "generated ISO fixture output may not replace an input"
        )
    try:
        manifest_payload = manifest.read_bytes()
        source_payload = assembly.read_bytes()
    except OSError as error:
        raise IsoAuthoringError(
            f"checked big-fixture input cannot be read: {error}"
        ) from error

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{output.name}.gen-big-",
            dir=output.parent,
        ) as temporary:
            temporary_root = Path(temporary)
            frozen_source = temporary_root / "input" / assembly.name
            candidate = temporary_root / "output" / output.name
            frozen_source.parent.mkdir()
            candidate.parent.mkdir()
            frozen_source.write_bytes(source_payload)
            arguments = [
                "-f",
                "bin",
                str(frozen_source),
                "-o",
                str(candidate),
            ]
            try:
                proc = run_seed_tool(
                    manifest,
                    Path.cwd(),
                    "cupidasm",
                    arguments,
                    timeout=60,
                )
            except BootstrapError as error:
                raise IsoAuthoringError(
                    f"checked CupidASM could not run: {error}"
                ) from error
            if proc.returncode != 0:
                details = (proc.stderr or proc.stdout or "").strip()
                suffix = f": {details}" if details else ""
                raise IsoAuthoringError(
                    f"checked CupidASM failed with status "
                    f"{proc.returncode}{suffix}"
                )
            if _is_link_or_junction(candidate):
                raise IsoAuthoringError(
                    "checked CupidASM output may not be a symbolic link or "
                    "junction"
                )
            try:
                candidate_mode = candidate.lstat().st_mode
            except FileNotFoundError as error:
                raise IsoAuthoringError(
                    "checked CupidASM reported success without big.bin"
                ) from error
            if not stat.S_ISREG(candidate_mode):
                raise IsoAuthoringError(
                    "checked CupidASM big.bin output is not a regular file"
                )
            candidate_payload = candidate.read_bytes()
            if candidate_payload != expected:
                raise IsoAuthoringError(
                    "checked CupidASM big.bin differs from the 4096-byte "
                    "fixture pattern"
                )
            if (
                manifest.read_bytes() != manifest_payload
                or assembly.read_bytes() != source_payload
            ):
                raise IsoAuthoringError(
                    "checked big-fixture inputs changed while CupidASM ran"
                )
            if _read_iso_output(output) != initial_output:
                raise IsoAuthoringError(
                    "generated ISO fixture changed while writing big.bin"
                )
            if initial_output == candidate_payload:
                return False
            os.replace(candidate, output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"generated ISO fixture cannot be published: {output}: {error}"
        ) from error
    return True


def gen_big(
    out: Path,
    *,
    seed_manifest: Path | None = None,
    source: Path | None = None,
) -> None:
    if (seed_manifest is None) != (source is None):
        raise IsoAuthoringError(
            "select both the checked seed manifest and CupidASM source"
        )
    payload = bytes(i & 0xFF for i in range(4096))
    try:
        out.parent.mkdir(parents=True, exist_ok=True)
        output = out.parent.resolve(strict=True) / out.name
        initial_output = _read_iso_output(output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"generated ISO fixture cannot be prepared: {out}: {error}"
        ) from error
    if seed_manifest is not None and source is not None:
        changed = _gen_big_with_seed(
            seed_manifest,
            source,
            output,
            initial_output,
            payload,
        )
        action = "Generated" if changed else "Reused"
        print(f"[hostbuild] {action} {out} (4096 bytes)")
        return
    if initial_output == payload:
        print(f"[hostbuild] Reused {out} (4096 bytes)")
        return

    try:
        with tempfile.TemporaryDirectory(
            prefix=f".{out.name}.gen-big-",
            dir=output.parent,
        ) as temporary:
            candidate = Path(temporary) / output.name
            with candidate.open("wb") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            if _read_iso_output(output) != initial_output:
                raise IsoAuthoringError(
                    "generated ISO fixture changed while writing big.bin"
                )
            os.replace(candidate, output)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"generated ISO fixture cannot be published: {out}: {error}"
        ) from error
    print(f"[hostbuild] Generated {out} (4096 bytes)")


def build_iso(
    fixtures: Path,
    out: Path,
    manifest: Path | None = None,
    *,
    seed_manifest: Path | None = None,
) -> None:
    checked_seed = None
    checked_seed_manifest = None
    if seed_manifest is not None:
        checked_seed_manifest, checked_seed = _resolve_checked_iso_seed(
            seed_manifest
        )
        try:
            requested_output = out.resolve(strict=False)
        except OSError as error:
            raise IsoAuthoringError(
                f"ISO output cannot be resolved: {out}: {error}"
            ) from error
        seed_paths = [
            checked_seed_manifest,
            *checked_seed.tools.values(),
        ]
        if any(
            _iso_paths_alias(requested_output, path)
            for path in seed_paths
        ):
            raise IsoAuthoringError(
                "ISO output may not replace a checked seed input"
            )
        if _path_is_within(
            requested_output,
            checked_seed_manifest.parent,
        ):
            raise IsoAuthoringError(
                "ISO output may not be inside the checked seed directory"
            )
    fixture_root, output = _resolve_iso_paths(fixtures, out)
    try:
        snapshot = _snapshot_iso_tree(fixture_root)
        checked_manifest = (
            _read_iso_manifest(manifest, fixture_root)
            if manifest is not None
            else None
        )
        if checked_manifest is not None:
            _validate_iso_manifest(snapshot, checked_manifest)
            if output.exists() and os.path.samefile(
                checked_manifest.path,
                output,
            ):
                raise IsoAuthoringError(
                    "ISO output may not alias the fixture manifest"
                )
        if checked_seed_manifest is not None:
            if checked_manifest is None:
                raise IsoAuthoringError(
                    "checked CupidObj ISO authoring requires a fixture "
                    "manifest"
                )
            seed_paths = [
                checked_seed_manifest,
                *checked_seed.tools.values(),
            ]
            if any(
                _iso_paths_alias(output, path) for path in seed_paths
            ):
                raise IsoAuthoringError(
                    "ISO output may not replace a checked seed input"
                )
        _reject_iso_output_alias(fixture_root, snapshot, output)
        publication_lock = _acquire_iso_publication_lock(output)
        try:
            initial_output = _read_iso_output(output)
            if checked_seed_manifest is not None:
                checked_image = _run_checked_iso_author(
                    seed_manifest=checked_seed_manifest,
                    snapshot=snapshot,
                    manifest=checked_manifest,
                )
                oracle_image = _render_iso_image(snapshot)
                if checked_image != oracle_image:
                    raise IsoAuthoringError(
                        "checked CupidObj ISO fixture differs from the "
                        "Python oracle"
                    )
                image = checked_image
            else:
                image = _render_iso_image(snapshot)
            changed = _publish_iso_image(
                fixtures=fixture_root,
                snapshot=snapshot,
                output=output,
                initial_output=initial_output,
                image=image,
                manifest=checked_manifest,
                seed_manifest=checked_seed_manifest,
                seed_manifest_sha256=(
                    checked_seed.manifest_sha256
                    if checked_seed is not None
                    else None
                ),
            )
        finally:
            _release_disk_publication_lock(publication_lock)
    except IsoAuthoringError:
        raise
    except OSError as error:
        raise IsoAuthoringError(
            f"ISO fixture image could not be authored: {error}"
        ) from error
    action = "Built" if changed else "Reused"
    print(
        f"[hostbuild] {action} {output} "
        f"({len(image)} bytes, deterministic ISO9660/Rock Ridge)"
    )

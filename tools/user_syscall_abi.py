#!/usr/bin/env python3
"""Check the shared i386 syscall ABI used by Cupid OS user programs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


SCHEMA = "cupid.user-syscall-abi.v1"
EXPECTED_VERSION = 5
EXPECTED_FIELD_COUNT = 103
EXPECTED_TABLE_SIZE = 412
EXPECTED_DIRENT_SIZE = 136
EXPECTED_STAT_SIZE = 8
EXPECTED_ABI_SHA256 = (
    "3e4d31320b2f56d19d37796ef679d1abbb228de9f36c9520d2dd5ec430c3c0bc"
)
EXPECTED_PROVIDER_SHA256 = (
    "0a51ba85c93b0249215b05e54867fabe0e7206d7e58a7695911a6ecb060916f4"
)

ABI_INPUTS = (
    "kernel/core/types.h",
    "kernel/core/syscall.h",
    "kernel/core/syscall.cc",
    "kernel/fs/vfs.h",
    "kernel/network/socket.h",
    "user/cupid.h",
)

VFS_CONSTANTS = {
    "O_RDONLY": 0x0000,
    "O_WRONLY": 0x0001,
    "O_RDWR": 0x0002,
    "O_CREAT": 0x0100,
    "O_TRUNC": 0x0200,
    "O_APPEND": 0x0400,
    "SEEK_SET": 0,
    "SEEK_CUR": 1,
    "SEEK_END": 2,
    "VFS_TYPE_FILE": 0,
    "VFS_TYPE_DIR": 1,
    "VFS_TYPE_DEV": 2,
    "VFS_MAX_NAME": 128,
    "VFS_MAX_PATH": 512,
}

NETWORK_CONSTANTS = {
    "SOCK_UDP": ("SOCK_TYPE_UDP", 1),
    "SOCK_TCP": ("SOCK_TYPE_TCP", 2),
    "SOL_TLS": ("SOL_TLS", 1),
    "TLS_ENABLE": ("TLS_ENABLE", 1),
}

TCP_STATES = {
    "TCPS_CLOSED": 0,
    "TCPS_LISTEN": 1,
    "TCPS_SYN_SENT": 2,
    "TCPS_SYN_RCVD": 3,
    "TCPS_ESTABLISHED": 4,
    "TCPS_FIN_WAIT_1": 5,
    "TCPS_FIN_WAIT_2": 6,
    "TCPS_TIME_WAIT": 7,
    "TCPS_CLOSE_WAIT": 8,
    "TCPS_LAST_ACK": 9,
}

REVIEWED_PROVIDER_ALIASES = {
    "ntohs": "htons",
    "ntohl": "htonl",
}

TYPE_ALIASES = {
    "vfs_dirent_t": "cupid_dirent_t",
    "vfs_stat_t": "cupid_stat_t",
}

SCALAR_TYPES = {
    "uint8_t": {"bytes": 1, "signed": False},
    "uint16_t": {"bytes": 2, "signed": False},
    "uint32_t": {"bytes": 4, "signed": False},
    "int32_t": {"bytes": 4, "signed": True},
    "size_t": {"bytes": 4, "signed": False},
}

I386_INTEGER_LAYOUTS = {
    "signed char": {"bytes": 1, "signed": True},
    "unsigned char": {"bytes": 1, "signed": False},
    "signed short": {"bytes": 2, "signed": True},
    "unsigned short": {"bytes": 2, "signed": False},
    "signed int": {"bytes": 4, "signed": True},
    "unsigned int": {"bytes": 4, "signed": False},
    "signed long": {"bytes": 4, "signed": True},
    "unsigned long": {"bytes": 4, "signed": False},
    "signed long long": {"bytes": 8, "signed": True},
    "unsigned long long": {"bytes": 8, "signed": False},
}

IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
TOKEN = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*|"
    r"0[xX][0-9A-Fa-f]+|[0-9]+|"
    r"\.\.\.|<<=|>>=|==|!=|<=|>=|->|&&|\|\||"
    r"[^\s]"
)


class UserSyscallAbiError(RuntimeError):
    """The kernel and user headers do not describe one compatible ABI."""


@dataclass(frozen=True)
class Field:
    name: str
    declaration: str


@dataclass(frozen=True)
class AbiInputSnapshot:
    payload: bytes
    text: str


def _root_path(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise UserSyscallAbiError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise UserSyscallAbiError(
            f"repository root is not a directory: {resolved}"
        )
    return resolved


def _read_input(root: Path, relative: str) -> AbiInputSnapshot:
    path = root / relative
    if path.is_symlink() or not path.is_file():
        raise UserSyscallAbiError(f"ABI input is unavailable: {relative}")
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise UserSyscallAbiError(
            f"cannot read ABI input {relative}: {error}"
        ) from error
    try:
        text = payload.decode("utf-8")
    except UnicodeError as error:
        raise UserSyscallAbiError(
            f"cannot read ABI input {relative}: {error}"
        ) from error
    return AbiInputSnapshot(payload=payload, text=text)


def _require_inputs_unchanged(
    root: Path,
    snapshots: dict[str, AbiInputSnapshot],
) -> None:
    for relative, snapshot in snapshots.items():
        try:
            current = _read_input(root, relative)
        except UserSyscallAbiError as error:
            raise UserSyscallAbiError(
                f"ABI input changed while checking: {relative}: {error}"
            ) from error
        if current.payload != snapshot.payload:
            raise UserSyscallAbiError(
                f"ABI input changed while checking: {relative}"
            )


def _without_comments(source: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\r\n]*", " ", source, flags=re.S)


def _declarations(body: str) -> tuple[str, ...]:
    declarations = []
    start = 0
    parentheses = 0
    brackets = 0
    for index, character in enumerate(body):
        if character == "(":
            parentheses += 1
        elif character == ")":
            parentheses -= 1
        elif character == "[":
            brackets += 1
        elif character == "]":
            brackets -= 1
        elif character == ";" and parentheses == 0 and brackets == 0:
            declaration = body[start:index].strip()
            if declaration:
                declarations.append(declaration)
            start = index + 1
        if parentheses < 0 or brackets < 0:
            raise UserSyscallAbiError(
                "syscall table declaration has unbalanced delimiters"
            )
    if parentheses != 0 or brackets != 0 or body[start:].strip():
        raise UserSyscallAbiError(
            "syscall table declaration is incomplete"
        )
    return tuple(declarations)


def _canonical_declaration(declaration: str) -> str:
    for source, destination in TYPE_ALIASES.items():
        declaration = re.sub(
            rf"\b{re.escape(source)}\b",
            destination,
            declaration,
        )
    return " ".join(TOKEN.findall(declaration))


def _canonical_integer_type(declaration: str, label: str) -> str:
    tokens = declaration.split()
    sign_tokens = [token for token in tokens if token in {"signed", "unsigned"}]
    if len(sign_tokens) > 1:
        raise UserSyscallAbiError(
            f"{label} has an invalid integer type: {declaration}"
        )
    sign = sign_tokens[0] if sign_tokens else "signed"
    body = [token for token in tokens if token not in {"signed", "unsigned"}]
    if body == ["char"]:
        kind = "char"
    elif body in (["short"], ["short", "int"]):
        kind = "short"
    elif body in ([], ["int"]):
        kind = "int"
    elif body in (["long"], ["long", "int"]):
        kind = "long"
    elif body in (
        ["long", "long"],
        ["long", "long", "int"],
    ):
        kind = "long long"
    else:
        raise UserSyscallAbiError(
            f"{label} has an unsupported integer type: {declaration}"
        )
    return f"{sign} {kind}"


def _scalar_typedefs(source: str, label: str) -> dict[str, str]:
    source = _without_comments(source)
    typedefs = {}
    for name in SCALAR_TYPES:
        matches = re.findall(
            rf"\btypedef\s+([^;{{}}]+?)\s+{re.escape(name)}\s*;",
            source,
        )
        if len(matches) != 1:
            raise UserSyscallAbiError(
                f"{label} must define {name} exactly once"
            )
        typedefs[name] = _canonical_integer_type(
            " ".join(matches[0].split()),
            f"{label} {name}",
        )
    return typedefs


def _compare_scalar_typedefs(
    kernel_types: str,
    user_header: str,
) -> dict[str, dict[str, object]]:
    kernel = _scalar_typedefs(kernel_types, "kernel types header")
    user = _scalar_typedefs(user_header, "user API header")
    report = {}
    for name, expected in SCALAR_TYPES.items():
        if kernel[name] != user[name]:
            raise UserSyscallAbiError(
                f"{name} differs: kernel {kernel[name]}, user {user[name]}"
            )
        layout = I386_INTEGER_LAYOUTS[kernel[name]]
        if layout != expected:
            raise UserSyscallAbiError(
                f"{name} has i386 layout {layout}, expected {expected}"
            )
        report[name] = dict(layout)
    return report


def _field(declaration: str) -> Field:
    function_pointer = re.search(
        r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
        declaration,
    )
    if function_pointer is not None:
        name = function_pointer.group(1)
    else:
        ordinary = re.search(
            r"([A-Za-z_][A-Za-z0-9_]*)\s*"
            r"(?:\[[^\]]*\]\s*)?$",
            declaration,
        )
        if ordinary is None:
            raise UserSyscallAbiError(
                f"cannot name syscall table field: {declaration}"
            )
        name = ordinary.group(1)
    return Field(name, _canonical_declaration(declaration))


def _table_fields(source: str, label: str) -> tuple[Field, ...]:
    match = re.search(
        r"typedef\s+struct\s+cupid_syscall_table\s*\{"
        r"(?P<body>.*?)"
        r"\}\s*cupid_syscall_table_t\s*;",
        _without_comments(source),
        re.S,
    )
    if match is None:
        raise UserSyscallAbiError(
            f"{label} does not define cupid_syscall_table_t"
        )
    fields = tuple(
        _field(declaration)
        for declaration in _declarations(match.group("body"))
    )
    counts = Counter(field.name for field in fields)
    duplicates = sorted(name for name, count in counts.items() if count != 1)
    if duplicates:
        raise UserSyscallAbiError(
            f"{label} repeats syscall fields: {', '.join(duplicates)}"
        )
    return fields


def _compare_fields(
    kernel_fields: tuple[Field, ...],
    user_fields: tuple[Field, ...],
) -> None:
    common = min(len(kernel_fields), len(user_fields))
    for index in range(common):
        kernel = kernel_fields[index]
        user = user_fields[index]
        if kernel != user:
            raise UserSyscallAbiError(
                f"syscall field {index} differs: kernel {kernel.name} "
                f"({kernel.declaration}), user {user.name} "
                f"({user.declaration})"
            )
    if len(kernel_fields) != len(user_fields):
        raise UserSyscallAbiError(
            f"syscall field count differs: kernel {len(kernel_fields)}, "
            f"user {len(user_fields)}"
        )


def _integer_macro(source: str, name: str, label: str) -> int:
    values = re.findall(
        rf"(?m)^\s*#\s*define\s+{re.escape(name)}\s+"
        r"([+-]?(?:0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*)\s*(?:$|/)",
        source,
    )
    if len(values) != 1:
        raise UserSyscallAbiError(
            f"{label} must define {name} exactly once"
        )
    literal = re.sub(r"[uUlL]+$", "", values[0])
    try:
        return int(literal, 0)
    except ValueError as error:
        raise UserSyscallAbiError(
            f"{label} has a noninteger {name}"
        ) from error


def _compare_vfs_constants(kernel_vfs: str, user_header: str) -> None:
    for name, expected in VFS_CONSTANTS.items():
        kernel = _integer_macro(kernel_vfs, name, "kernel VFS header")
        user = _integer_macro(user_header, name, "user API header")
        if kernel != user:
            raise UserSyscallAbiError(
                f"{name} differs: kernel {kernel}, user {user}"
            )
        if kernel != expected:
            raise UserSyscallAbiError(
                f"{name} changed from the reviewed ABI value "
                f"{expected} to {kernel}"
            )


def _enum_values(source: str, name: str, label: str) -> dict[str, int]:
    match = re.search(
        r"typedef\s+enum(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\{"
        r"(?P<body>[^{}]*)"
        rf"\}}\s*{re.escape(name)}\s*;",
        _without_comments(source),
        re.S,
    )
    if match is None:
        raise UserSyscallAbiError(f"{label} does not define {name}")
    values = {}
    next_value = 0
    for clause in match.group("body").split(","):
        clause = clause.strip()
        if not clause:
            continue
        parts = clause.split("=", 1)
        enumerator = parts[0].strip()
        if not IDENTIFIER.fullmatch(enumerator):
            raise UserSyscallAbiError(
                f"{label} has an invalid {name} enumerator"
            )
        if len(parts) == 2:
            literal = re.sub(r"[uUlL]+$", "", parts[1].strip())
            try:
                next_value = int(literal, 0)
            except ValueError as error:
                raise UserSyscallAbiError(
                    f"{label} has a noninteger {enumerator}"
                ) from error
        values[enumerator] = next_value
        next_value += 1
    return values


def _compare_network_constants(
    kernel_socket: str,
    user_header: str,
) -> None:
    for user_name, (kernel_name, expected) in NETWORK_CONSTANTS.items():
        kernel = _integer_macro(
            kernel_socket, kernel_name, "kernel socket header"
        )
        user = _integer_macro(user_header, user_name, "user API header")
        if kernel != user:
            raise UserSyscallAbiError(
                f"{user_name} differs: kernel {kernel}, user {user}"
            )
        if kernel != expected:
            raise UserSyscallAbiError(
                f"{kernel_name} changed from the reviewed ABI value "
                f"{expected} to {kernel}"
            )

    kernel_states = _enum_values(
        kernel_socket, "tcp_state_t", "kernel socket header"
    )
    for name, expected in TCP_STATES.items():
        kernel = kernel_states.get(name)
        if kernel is None:
            raise UserSyscallAbiError(
                f"kernel socket header does not define {name}"
            )
        user = _integer_macro(user_header, name, "user API header")
        if kernel != user:
            raise UserSyscallAbiError(
                f"{name} differs: kernel {kernel}, user {user}"
            )
        if kernel != expected:
            raise UserSyscallAbiError(
                f"{name} changed from the reviewed ABI value "
                f"{expected} to {kernel}"
            )


def _record_fields(source: str, name: str, label: str) -> tuple[Field, ...]:
    records = {
        match.group("name"): match.group("body")
        for match in re.finditer(
            r"typedef\s+struct(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*\{"
            r"(?P<body>[^{}]*)"
            r"\}\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;",
            _without_comments(source),
            re.S,
        )
    }
    body = records.get(name)
    if body is None:
        raise UserSyscallAbiError(f"{label} does not define {name}")
    return tuple(_field(declaration) for declaration in _declarations(body))


def _align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _i386_record_layout(
    fields: tuple[Field, ...],
    constants: dict[str, int],
) -> tuple[int, dict[str, int]]:
    scalar_layouts = {
        "char": (1, 1),
        "uint8_t": (1, 1),
        "uint16_t": (2, 2),
        "uint32_t": (4, 4),
        "int32_t": (4, 4),
    }
    offset = 0
    record_alignment = 1
    offsets = {}
    for field in fields:
        tokens = field.declaration.split()
        if not tokens or tokens[0] not in scalar_layouts:
            raise UserSyscallAbiError(
                f"unsupported VFS record field: {field.declaration}"
            )
        size, alignment = scalar_layouts[tokens[0]]
        if "[" in tokens:
            bracket = tokens.index("[")
            if bracket + 2 >= len(tokens) or tokens[bracket + 2] != "]":
                raise UserSyscallAbiError(
                    f"unsupported VFS array field: {field.declaration}"
                )
            length_token = tokens[bracket + 1]
            if length_token in constants:
                length = constants[length_token]
            else:
                try:
                    length = int(length_token, 0)
                except ValueError as error:
                    raise UserSyscallAbiError(
                        f"unknown VFS array bound: {length_token}"
                    ) from error
            size *= length
        offset = _align_up(offset, alignment)
        offsets[field.name] = offset
        offset += size
        record_alignment = max(record_alignment, alignment)
    return _align_up(offset, record_alignment), offsets


def _compare_vfs_records(
    kernel_vfs: str,
    user_header: str,
) -> tuple[int, dict[str, int], int, dict[str, int]]:
    pairs = (
        ("vfs_dirent_t", "cupid_dirent_t"),
        ("vfs_stat_t", "cupid_stat_t"),
    )
    layouts = []
    for kernel_name, user_name in pairs:
        kernel_fields = _record_fields(
            kernel_vfs, kernel_name, "kernel VFS header"
        )
        user_fields = _record_fields(
            user_header, user_name, "user API header"
        )
        if kernel_fields != user_fields:
            raise UserSyscallAbiError(
                f"{user_name} does not match {kernel_name}"
            )
        layouts.append(_i386_record_layout(kernel_fields, VFS_CONSTANTS))
    return layouts[0][0], layouts[0][1], layouts[1][0], layouts[1][1]


def _function_body(source: str, name: str) -> str:
    source = _without_comments(source)
    match = re.search(
        rf"\bvoid\s+{re.escape(name)}\s*\(\s*void\s*\)\s*\{{",
        source,
    )
    if match is None:
        raise UserSyscallAbiError(f"kernel does not define {name}")
    start = match.end()
    depth = 1
    quote = ""
    escaped = False
    for index in range(start, len(source)):
        character = source[index]
        if quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = ""
            continue
        if character in ('"', "'"):
            quote = character
        elif character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    raise UserSyscallAbiError(f"kernel function {name} is incomplete")


def _check_initializer(
    implementation: str,
    fields: tuple[Field, ...],
) -> tuple[int, str]:
    body = _function_body(implementation, "syscall_init")
    assignments = re.findall(
        r"\bsyscall_table\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\s*="
        r"\s*([^;]+?)\s*;",
        _without_comments(body),
    )
    counts = Counter(name for name, _value in assignments)
    expected = {field.name for field in fields}
    missing = sorted(name for name in expected if counts[name] == 0)
    duplicates = sorted(name for name in expected if counts[name] > 1)
    unknown = sorted(name for name in counts if name not in expected)
    if missing:
        raise UserSyscallAbiError(
            f"missing initializer assignments: {', '.join(missing)}"
        )
    if duplicates:
        raise UserSyscallAbiError(
            f"duplicate initializer assignments: {', '.join(duplicates)}"
        )
    if unknown:
        raise UserSyscallAbiError(
            f"unknown initializer assignments: {', '.join(unknown)}"
        )
    if not re.search(
        r"\bsyscall_table\s*\.\s*version\s*=\s*"
        r"CUPID_SYSCALL_VERSION\s*;",
        body,
    ):
        raise UserSyscallAbiError(
            "kernel syscall version is not initialized from "
            "CUPID_SYSCALL_VERSION"
        )
    if not re.search(
        r"\bsyscall_table\s*\.\s*table_size\s*=\s*"
        r"\(\s*uint32_t\s*\)\s*sizeof\s*\(\s*"
        r"cupid_syscall_table_t\s*\)\s*;",
        body,
    ):
        raise UserSyscallAbiError(
            "kernel syscall table size is not initialized from its type"
        )

    values = {name: value.strip() for name, value in assignments}
    providers = []
    for field in fields[2:]:
        provider = values[field.name]
        if IDENTIFIER.fullmatch(provider) is None:
            raise UserSyscallAbiError(
                f"syscall provider for {field.name} is not one identifier: "
                f"{provider}"
            )
        providers.append((field.name, provider))
    for field, reviewed_provider in REVIEWED_PROVIDER_ALIASES.items():
        provider = values[field]
        if provider != reviewed_provider:
            raise UserSyscallAbiError(
                f"syscall provider contract changed: {field} uses "
                f"{provider}, expected {reviewed_provider}"
            )

    digest = hashlib.sha256()
    for field, provider in providers:
        digest.update(f"{field}={provider}\n".encode("utf-8"))
    provider_sha256 = digest.hexdigest()
    if (
        EXPECTED_PROVIDER_SHA256
        and provider_sha256 != EXPECTED_PROVIDER_SHA256
    ):
        raise UserSyscallAbiError(
            "syscall provider contract changed without updating the "
            "reviewed ABI contract"
        )
    return len(providers), provider_sha256


def _abi_digest(fields: tuple[Field, ...]) -> str:
    digest = hashlib.sha256()
    for index, field in enumerate(fields):
        digest.update(
            f"{index}:{field.name}:{field.declaration}\n".encode("utf-8")
        )
    return digest.hexdigest()


def check_syscall_abi(root: Path) -> dict[str, object]:
    """Return the reviewed ABI report or fail with a precise mismatch."""
    root = _root_path(root)
    snapshots = {
        relative: _read_input(root, relative) for relative in ABI_INPUTS
    }
    sources = {
        relative: snapshot.text
        for relative, snapshot in snapshots.items()
    }
    kernel_types = sources["kernel/core/types.h"]
    kernel_header = sources["kernel/core/syscall.h"]
    implementation = sources["kernel/core/syscall.cc"]
    kernel_vfs = sources["kernel/fs/vfs.h"]
    kernel_socket = sources["kernel/network/socket.h"]
    user_header = sources["user/cupid.h"]

    kernel_fields = _table_fields(kernel_header, "kernel syscall header")
    user_fields = _table_fields(user_header, "user API header")
    _compare_fields(kernel_fields, user_fields)
    scalar_types = _compare_scalar_typedefs(kernel_types, user_header)

    kernel_version = _integer_macro(
        kernel_header,
        "CUPID_SYSCALL_VERSION",
        "kernel syscall header",
    )
    user_version = _integer_macro(
        user_header,
        "CUPID_SYSCALL_VERSION",
        "user API header",
    )
    if kernel_version != user_version:
        raise UserSyscallAbiError(
            f"syscall version differs: kernel {kernel_version}, "
            f"user {user_version}"
        )
    if kernel_version != EXPECTED_VERSION:
        raise UserSyscallAbiError(
            f"syscall version {kernel_version} is not the reviewed "
            f"version {EXPECTED_VERSION}"
        )

    _compare_vfs_constants(kernel_vfs, user_header)
    _compare_network_constants(kernel_socket, user_header)
    (
        dirent_size,
        dirent_offsets,
        stat_size,
        stat_offsets,
    ) = _compare_vfs_records(kernel_vfs, user_header)
    if dirent_size != EXPECTED_DIRENT_SIZE:
        raise UserSyscallAbiError(
            f"directory entry size {dirent_size} is not the reviewed "
            f"size {EXPECTED_DIRENT_SIZE}"
        )
    if stat_size != EXPECTED_STAT_SIZE:
        raise UserSyscallAbiError(
            f"file status size {stat_size} is not the reviewed "
            f"size {EXPECTED_STAT_SIZE}"
        )
    if dirent_offsets != {"name": 0, "size": 128, "type": 132}:
        raise UserSyscallAbiError(
            f"directory entry offsets changed: {dirent_offsets}"
        )
    if stat_offsets != {"size": 0, "type": 4}:
        raise UserSyscallAbiError(
            f"file status offsets changed: {stat_offsets}"
        )
    provider_count, provider_sha256 = _check_initializer(
        implementation,
        kernel_fields,
    )

    field_count = len(kernel_fields)
    table_size = field_count * 4
    if field_count != EXPECTED_FIELD_COUNT:
        raise UserSyscallAbiError(
            f"syscall field count {field_count} is not the reviewed "
            f"count {EXPECTED_FIELD_COUNT}"
        )
    if table_size != EXPECTED_TABLE_SIZE:
        raise UserSyscallAbiError(
            f"syscall table size {table_size} is not the reviewed "
            f"size {EXPECTED_TABLE_SIZE}"
        )

    abi_sha256 = _abi_digest(kernel_fields)
    if EXPECTED_ABI_SHA256 and abi_sha256 != EXPECTED_ABI_SHA256:
        raise UserSyscallAbiError(
            "syscall field signatures changed without updating the "
            "reviewed ABI contract"
        )

    report = {
        "schema": SCHEMA,
        "version": kernel_version,
        "field_count": field_count,
        "table_size": table_size,
        "dirent_size": dirent_size,
        "dirent_offsets": dirent_offsets,
        "stat_size": stat_size,
        "stat_offsets": stat_offsets,
        "scalar_types": scalar_types,
        "provider_count": provider_count,
        "provider_sha256": provider_sha256,
        "first_function": kernel_fields[2].name,
        "last_function": kernel_fields[-1].name,
        "abi_sha256": abi_sha256,
    }
    _require_inputs_unchanged(root, snapshots)
    return report


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Check the kernel and user i386 syscall ABI."
    )
    parser.add_argument("--root", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        report = check_syscall_abi(arguments.root)
    except UserSyscallAbiError as error:
        print(f"Cupid user ABI check failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())

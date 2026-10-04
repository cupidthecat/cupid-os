"""Qualify the staged native ABI command against retained OS declarations."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import stat

from tools import bootstrap_toolchain as seed
from tools import bootstrap_stage_release as release
from tools import user_syscall_abi as oracle


MAX_INPUT_BYTES = 1024 * 1024


def _capture(root: Path, names: tuple[str, ...]) -> dict:
    result = {}
    for name in names:
        path = root / name
        for parent in (root, *path.parents):
            if parent == root.parent:
                break
            if parent.is_symlink():
                raise seed.BootstrapError(f"ABI source ancestor is linked: {parent}")
        payload = release._regular_bytes(path, MAX_INPUT_BYTES)
        observed = path.lstat()
        result[name] = (payload, (observed.st_dev, observed.st_ino, observed.st_mode,
                                  observed.st_size, observed.st_mtime_ns, observed.st_ctime_ns))
    return result


def _tree(root: Path) -> dict:
    result = {}
    for path in (root, *sorted(root.rglob("*"))):
        observed = path.lstat()
        if not (stat.S_ISREG(observed.st_mode) or stat.S_ISDIR(observed.st_mode)):
            raise seed.BootstrapError(f"ABI fixture contains a linked or special entry: {path}")
        payload = release._regular_bytes(path, MAX_INPUT_BYTES) if path.is_file() else None
        result[path.relative_to(root).as_posix()] = (
            payload, observed.st_dev, observed.st_ino, observed.st_mode,
            observed.st_size, observed.st_mtime_ns, observed.st_ctime_ns)
    return result


def _canonical(value) -> str:
    # Canonical JSON distinguishes integers, floats and booleans, unlike dict equality.
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)


def check_behavior(runner, source_root: Path, behavior_root: Path,
                   stage_two, stage_three, label_prefix: str) -> None:
    """Run eleven positive and eleven negative pairs; retain evidence on success."""
    label = label_prefix + "CupidBuild ABI behavior"
    try:
        original = _capture(source_root, oracle.ABI_INPUTS)
        tools = tuple(stage.tools["cupidbuild"] for stage in (stage_two, stage_three))
        tool_bytes = tuple(release._regular_bytes(path) for path in tools)
        gate = behavior_root / "cupidbuild-user-abi"
        gate.mkdir()
        root = gate / "caf\u00e9-\u65e5\u672c\u8a9e-\U0001f600"
        root.mkdir()
        for name, (payload, _observed) in original.items():
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
        expected = oracle.check_syscall_abi(root)
        records = []

        def unchanged():
            if _capture(source_root, oracle.ABI_INPUTS) != original:
                raise seed.BootstrapError(f"{label}: original ABI inputs changed")
            if tuple(release._regular_bytes(path) for path in tools) != tool_bytes:
                raise seed.BootstrapError(f"{label}: staged CupidBuild changed")

        def command(name, status=0, diagnostic="", arguments=None):
            before = _tree(root)
            unchanged()
            result = seed._run_stage_pair(
                runner, stage_two, stage_three, "cupidbuild",
                arguments if arguments is not None else ["verify-user-abi", "--root", root],
                timeout=60)
            seed._expect_status(result, status, f"{label}: {name}")
            if _tree(root) != before:
                raise seed.BootstrapError(f"{label}: command changed its input tree")
            unchanged()
            if status == 0:
                actual = release._json(result.stdout.encode("utf-8"))
                if result.stderr or _canonical(actual) != _canonical(expected):
                    raise seed.BootstrapError(f"{label}: complete oracle report differs")
            elif result.stdout or not result.stderr or diagnostic not in result.stderr:
                raise seed.BootstrapError(f"{label}: failure output differs")
            records.append({"case": name, "status": status,
                            "stdout_sha256": hashlib.sha256(result.stdout.encode("utf-8")).hexdigest(),
                            "stderr_sha256": hashlib.sha256(result.stderr.encode("utf-8")).hexdigest()})

        command("canonical")
        types = root / "kernel/core/types.h"
        types_before = types.read_bytes()
        unicode_literals = (
            '\n/* caf\u00e9 \u65e5\u672c\u8a9e \U0001f600 */\n'
            'static const char *abi_literal_probe = "quote: \\" slash: \\\\";\n'
            "static const char abi_character_probe = '\\'';\n"
        ).encode("utf-8")
        types.write_bytes(types_before + unicode_literals)
        if _canonical(oracle.check_syscall_abi(root)) != _canonical(expected):
            raise seed.BootstrapError(f"{label}: positive fixture changed the ABI")
        command("unicode-and-escaped-literals")
        types.write_bytes(types_before)

        failures = (
            ("version", "user/cupid.h", b"#define CUPID_SYSCALL_VERSION 5",
             b"#define CUPID_SYSCALL_VERSION 4", "syscall version differs"),
            ("provider", "kernel/core/syscall.cc", b"syscall_table.print = syscall_print;",
             b"syscall_table.print = syscall_printf;", "syscall provider contract changed"),
            ("string-open", "kernel/core/types.h", None, b' "', "incomplete string or character literal"),
            ("character-open", "kernel/core/types.h", None, b" '", "incomplete string or character literal"),
            ("string-escaped-close", "kernel/core/types.h", None,
             b' "' + bytes((92, 34)), "incomplete string or character literal"),
            ("character-escaped-close", "kernel/core/types.h", None,
             b" '" + bytes((92, 39)), "incomplete string or character literal"),
            ("invalid-utf8", "kernel/core/types.h", None, b"\xff", "NUL-free UTF-8"),
            ("missing-input", "kernel/core/syscall.cc", None, None, "ABI verification failed"),
        )
        for name, relative, needle, replacement, diagnostic in failures:
            path = root / relative
            before = original[relative][0]
            if replacement is None:
                path.unlink()
            elif needle is None:
                path.write_bytes(before + replacement)
            else:
                if before.count(needle) != 1:
                    raise seed.BootstrapError(f"{label}: negative fixture anchor differs: {name}")
                path.write_bytes(before.replace(needle, replacement))
            command(name, 1, diagnostic)
            path.write_bytes(before)
            command(name + "-repair")

        usage_cases = (
            ("missing-root", ["verify-user-abi"]),
            ("duplicate-root", ["verify-user-abi", "--root", root, "--root", root]),
            ("unexpected-seed", ["verify-user-abi", "--root", root,
                                  "--seed-manifest", "absent-manifest.json"]),
        )
        for name, arguments in usage_cases:
            command(name, 2, "verify-user-abi --root ROOT", arguments)
        command("final-recovery")
        unchanged()
        if [row["status"] for row in records].count(0) != 11 or len(records) != 22:
            raise seed.BootstrapError(f"{label}: command matrix count differs")
        if {name: payload for name, (payload, _observed) in _capture(root, oracle.ABI_INPUTS).items()} != {
                name: payload for name, (payload, _observed) in original.items()}:
            raise seed.BootstrapError(f"{label}: repaired fixture differs")
        evidence = {"schema": "cupid.bootstrap-user-abi-behavior.v1",
                    "source_inputs": {name: release._identity(row[0]) for name, row in original.items()},
                    "tool_sha256": [hashlib.sha256(payload).hexdigest() for payload in tool_bytes],
                    "oracle_report": expected, "cases": records}
        (gate / "behavior.json").write_bytes(release._encode(evidence))
    except (OSError, oracle.UserSyscallAbiError) as error:
        raise seed.BootstrapError(f"{label}: {error}") from error

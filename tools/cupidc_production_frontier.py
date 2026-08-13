#!/usr/bin/env python3
"""Check deterministic CupidC ownership for small production cohorts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence

try:
    from tools.cupidc_production_compile import (
        GENERATED_INCLUDE_CLOSURE,
        GENERATED_INSTALL_SOURCES,
        USER_INCLUDE_CLOSURE,
        USER_SOURCES,
        ProductionCompileError,
        compile_production_source,
    )
    from tools.cupidld_user_link import (
        UserLinkError,
        link_user_program,
        validate_user_executable,
    )
    from tools.native_user_toolchain import NATIVE_USER_TOOL_SOURCES
    from tools.user_syscall_abi import ABI_INPUTS
except ModuleNotFoundError:
    from cupidc_production_compile import (
        GENERATED_INCLUDE_CLOSURE,
        GENERATED_INSTALL_SOURCES,
        USER_INCLUDE_CLOSURE,
        USER_SOURCES,
        ProductionCompileError,
        compile_production_source,
    )
    from cupidld_user_link import (
        UserLinkError,
        link_user_program,
        validate_user_executable,
    )
    from native_user_toolchain import NATIVE_USER_TOOL_SOURCES
    from user_syscall_abi import ABI_INPUTS


SCHEMA = "cupid.production-frontier.v1"
LINUX_PRODUCTION_SEED_FILES = (
    "bootstrap/seeds/i386-linux/manifest.json",
    "bootstrap/seeds/i386-linux/cupidasm.elf",
    "bootstrap/seeds/i386-linux/cupidc.elf",
    "bootstrap/seeds/i386-linux/cupiddis.elf",
    "bootstrap/seeds/i386-linux/cupidld.elf",
    "bootstrap/seeds/i386-linux/cupidobj.elf",
)
WINDOWS_PRODUCTION_SEED_FILES = (
    "bootstrap/seeds/i386-windows/manifest.json",
    "bootstrap/seeds/i386-windows/cupidasm.exe",
    "bootstrap/seeds/i386-windows/cupidc.exe",
    "bootstrap/seeds/i386-windows/cupiddis.exe",
    "bootstrap/seeds/i386-windows/cupidld.exe",
    "bootstrap/seeds/i386-windows/cupidobj.exe",
)
CONTROL_FILES = (
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_kernel_compile.py",
    "tools/cupidc_production_compile.py",
    "tools/cupidc_production_frontier.py",
    "tools/native_user_toolchain.py",
)
USER_CONTROL_FILES = (
    "user/Makefile",
    "tools/cupidld_user_link.py",
    "tools/user_syscall_abi.py",
)
GENERATED_CONTROL_FILES = (
    "Makefile",
    "tools/hostbuild.py",
)


class FrontierError(RuntimeError):
    """A production frontier did not reproduce its declared outputs."""


def _native_windows_host() -> bool:
    return os.name == "nt"


def _production_seed_files() -> tuple[str, ...]:
    return (
        WINDOWS_PRODUCTION_SEED_FILES
        if _native_windows_host()
        else LINUX_PRODUCTION_SEED_FILES
    )


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _read_file(root: Path, relative: str) -> bytes:
    path = root / relative
    if path.is_symlink() or not path.is_file():
        raise FrontierError(f"frontier input is unavailable: {relative}")
    try:
        return path.read_bytes()
    except OSError as error:
        raise FrontierError(
            f"cannot read frontier input {relative}: {error}"
        ) from error


def _snapshot(root: Path, relatives: Sequence[str]) -> dict[str, str]:
    if len(relatives) != len(set(relatives)):
        raise FrontierError("frontier input closure contains duplicate paths")
    return {
        relative: _sha256(_read_file(root, relative))
        for relative in sorted(relatives)
    }


def _aggregate(snapshot: dict[str, str]) -> str:
    digest = hashlib.sha256()
    for path, file_digest in sorted(snapshot.items()):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(file_digest.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _relative_files(root: Path, pattern: str) -> tuple[str, ...]:
    return tuple(
        sorted(
            path.relative_to(root).as_posix()
            for path in root.glob(pattern)
            if path.is_file() and not path.is_symlink()
        )
    )


def _user_inputs(*, include_native_tools: bool = False) -> tuple[str, ...]:
    inputs = {
        *USER_SOURCES,
        *USER_INCLUDE_CLOSURE,
        *ABI_INPUTS,
        *CONTROL_FILES,
        *USER_CONTROL_FILES,
        *_production_seed_files(),
    }
    if include_native_tools:
        inputs.update(NATIVE_USER_TOOL_SOURCES)
    return tuple(sorted(inputs))


def _generator_inputs(root: Path) -> dict[str, tuple[str, ...]]:
    bin_sources = tuple(
        path
        for path in _relative_files(root, "bin/*.cc")
        if path not in {"bin/old_cc2.cc", "bin/old_cc2_single.cc"}
    )
    bin_headers = _relative_files(root, "bin/*.h")
    browser_sources = _relative_files(root, "bin/browser/*.cc")
    docs = _relative_files(root, "cupidos-txt/*.CTXT")
    top_level_assets = tuple(
        path
        for pattern in ("*.bmp", "*.png", "*.jpg", "*.jpeg")
        for path in _relative_files(root, pattern)
    )
    demos = _relative_files(root, "demos/*.asm")
    return {
        "bin": bin_sources,
        "headers": bin_headers,
        "browser": browser_sources,
        "ctxt": docs,
        "doc-assets": ("image.bmp",),
        "home-assets": top_level_assets,
        "demos": demos,
    }


def _generated_inputs(
    root: Path, generator_inputs: dict[str, tuple[str, ...]]
) -> tuple[str, ...]:
    generator_paths = {
        path for paths in generator_inputs.values() for path in paths
    }
    return tuple(
        sorted(
            {
                *GENERATED_INSTALL_SOURCES,
                *GENERATED_INCLUDE_CLOSURE,
                *CONTROL_FILES,
                *GENERATED_CONTROL_FILES,
                *_production_seed_files(),
                *generator_paths,
            }
        )
    )


def _run_generator(
    root: Path,
    command: Sequence[str],
    output: Path,
) -> bytes:
    full_command = [
        sys.executable,
        str(root / "tools" / "hostbuild.py"),
        *command,
        "--out",
        str(output),
    ]
    try:
        result = subprocess.run(
            full_command,
            cwd=root,
            text=True,
            capture_output=True,
            timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise FrontierError(f"generated-source replay could not run: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise FrontierError(
            f"generated-source replay failed with status "
            f"{result.returncode}: {detail}"
        )
    if output.is_symlink() or not output.is_file():
        raise FrontierError("generated-source replay did not write its output")
    try:
        return output.read_bytes()
    except OSError as error:
        raise FrontierError(
            f"cannot read generated-source replay {output}: {error}"
        ) from error


def _generator_commands(
    inputs: dict[str, tuple[str, ...]]
) -> dict[str, tuple[str, ...]]:
    return {
        "kernel/util/bin_programs_gen.cc": (
            "gen-bin-programs",
            "--bin",
            *inputs["bin"],
            "--headers",
            *inputs["headers"],
            "--browser",
            *inputs["browser"],
        ),
        "kernel/util/docs_programs_gen.cc": (
            "gen-docs-programs",
            "--ctxt",
            *inputs["ctxt"],
            "--doc-assets",
            *inputs["doc-assets"],
            "--home-assets",
            *inputs["home-assets"],
        ),
        "kernel/util/demos_programs_gen.cc": (
            "gen-demos-programs",
            "--demos",
            *inputs["demos"],
        ),
    }


def _object_record(payload: bytes) -> dict[str, object]:
    return {"bytes": len(payload), "sha256": _sha256(payload)}


def run_user_frontier(
    root: Path,
    *,
    compare_checked_seed: bool = False,
) -> dict[str, object]:
    root = root.resolve(strict=True)
    if compare_checked_seed and not _native_windows_host():
        raise FrontierError(
            "native Windows checked-seed comparison requires Windows"
        )
    closure = _user_inputs(include_native_tools=compare_checked_seed)
    before = _snapshot(root, closure)
    native_options = (
        {"tool_mode": "native-windows"}
        if compare_checked_seed
        else {}
    )
    records = {}
    with tempfile.TemporaryDirectory(
        prefix=".user-cupidc-frontier-",
        dir=root / "user",
    ) as temporary:
        temporary_root = Path(temporary)
        run_directories = {
            label: temporary_root / label for label in ("run-a", "run-b")
        }
        for directory in run_directories.values():
            directory.mkdir()

        for source_relative in USER_SOURCES:
            name = Path(source_relative).stem
            object_payloads = []
            executable_payloads = []
            for directory in run_directories.values():
                object_path = directory / f"{name}.o"
                executable_path = directory / name
                try:
                    compile_production_source(
                        root,
                        "user",
                        root / source_relative,
                        object_path,
                        **native_options,
                    )
                    link_user_program(
                        root,
                        object_path,
                        executable_path,
                        **native_options,
                    )
                except (ProductionCompileError, UserLinkError) as error:
                    raise FrontierError(
                        f"user frontier failed for {source_relative}: {error}"
                    ) from error
                object_payloads.append(object_path.read_bytes())
                executable_payloads.append(executable_path.read_bytes())
            if object_payloads[0] != object_payloads[1]:
                raise FrontierError(
                    f"user object is nondeterministic: {source_relative}"
                )
            if executable_payloads[0] != executable_payloads[1]:
                raise FrontierError(
                    f"user executable is nondeterministic: {source_relative}"
                )

            object_relative = f"user/build/{name}.o"
            installed_object = _read_file(root, object_relative)
            if any(
                installed_object != replayed
                for replayed in object_payloads
            ):
                raise FrontierError(
                    f"production user object differs from the frontier: "
                    f"{object_relative}"
                )
            production = root / "user" / "build" / name
            if production.is_symlink() or not production.is_file():
                raise FrontierError(
                    f"production user executable is unavailable: "
                    f"user/build/{name}"
                )
            validate_user_executable(production)
            production_payload = production.read_bytes()
            if production_payload != executable_payloads[0]:
                raise FrontierError(
                    f"production user executable differs from the frontier: "
                    f"user/build/{name}"
                )
            record = {
                "object": _object_record(installed_object),
                "executable": _object_record(executable_payloads[0]),
            }
            if compare_checked_seed:
                checked_directory = temporary_root / "checked-seed"
                checked_directory.mkdir(exist_ok=True)
                checked_object = checked_directory / f"{name}.o"
                checked_executable = checked_directory / name
                try:
                    compile_production_source(
                        root,
                        "user",
                        root / source_relative,
                        checked_object,
                        tool_mode="checked-seed",
                    )
                    link_user_program(
                        root,
                        checked_object,
                        checked_executable,
                        tool_mode="checked-seed",
                    )
                except (ProductionCompileError, UserLinkError) as error:
                    raise FrontierError(
                        f"checked-seed comparison failed for "
                        f"{source_relative}: {error}"
                    ) from error
                if checked_object.read_bytes() != object_payloads[0]:
                    raise FrontierError(
                        "native Windows object differs from the checked seed: "
                        f"{source_relative}"
                    )
                if checked_executable.read_bytes() != executable_payloads[0]:
                    raise FrontierError(
                        "native Windows executable differs from the checked "
                        f"seed: {source_relative}"
                    )
                record["checked_seed_match"] = True
            records[source_relative] = record

    after = _snapshot(root, closure)
    if (
        after != before
        or _user_inputs(include_native_tools=compare_checked_seed) != closure
    ):
        raise FrontierError("user frontier inputs changed during the check")
    return {
        "schema": SCHEMA,
        "cohort": "user",
        "input_count": len(closure),
        "input_sha256": _aggregate(before),
        "checked_seed_comparison": compare_checked_seed,
        "sources": records,
    }


def run_generated_frontier(root: Path) -> dict[str, object]:
    root = root.resolve(strict=True)
    generator_inputs = _generator_inputs(root)
    closure = _generated_inputs(root, generator_inputs)
    before = _snapshot(root, closure)
    records = {}
    commands = _generator_commands(generator_inputs)

    with tempfile.TemporaryDirectory(
        prefix=".generated-cupidc-frontier-",
        dir=root / "kernel" / "util",
    ) as temporary:
        temporary_root = Path(temporary)
        run_directories = {
            label: temporary_root / label for label in ("run-a", "run-b")
        }
        for directory in run_directories.values():
            directory.mkdir()
        for source_relative in GENERATED_INSTALL_SOURCES:
            source = root / source_relative
            production_payload = source.read_bytes()
            replay_payloads = []
            for label in ("a", "b"):
                replay = temporary_root / f"{Path(source_relative).stem}.{label}.cc"
                replay_payloads.append(
                    _run_generator(
                        root,
                        commands[source_relative],
                        replay,
                    )
                )
            if replay_payloads[0] != replay_payloads[1]:
                raise FrontierError(
                    f"generated source is nondeterministic: {source_relative}"
                )
            if production_payload != replay_payloads[0]:
                raise FrontierError(
                    f"generated source is stale: {source_relative}"
                )

            object_payloads = []
            for directory in run_directories.values():
                object_path = directory / f"{Path(source_relative).stem}.o"
                try:
                    compile_production_source(
                        root,
                        "generated-install",
                        source,
                        object_path,
                    )
                except ProductionCompileError as error:
                    raise FrontierError(
                        f"generated frontier failed for "
                        f"{source_relative}: {error}"
                    ) from error
                object_payloads.append(object_path.read_bytes())
            if object_payloads[0] != object_payloads[1]:
                raise FrontierError(
                    f"generated object is nondeterministic: {source_relative}"
                )
            object_relative = Path(source_relative).with_suffix(".o").as_posix()
            installed_object = _read_file(root, object_relative)
            if any(
                installed_object != replayed
                for replayed in object_payloads
            ):
                raise FrontierError(
                    f"production generated object differs from the frontier: "
                    f"{object_relative}"
                )
            records[source_relative] = {
                "source": _object_record(production_payload),
                "object": _object_record(installed_object),
            }

    after_inputs = _generator_inputs(root)
    after = _snapshot(root, closure)
    if (
        after != before
        or after_inputs != generator_inputs
        or _generated_inputs(root, after_inputs) != closure
    ):
        raise FrontierError(
            "generated install-table inputs changed during the check"
        )
    return {
        "schema": SCHEMA,
        "cohort": "generated-install",
        "input_count": len(closure),
        "input_sha256": _aggregate(before),
        "sources": records,
    }


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Check deterministic CupidC production ownership."
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--cohort",
        required=True,
        choices=("user", "generated-install"),
    )
    parser.add_argument(
        "--compare-checked-seed",
        action="store_true",
        help=(
            "compare the native Windows user artifacts with the checked "
            "production seed"
        ),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        report = (
            run_user_frontier(
                arguments.root,
                compare_checked_seed=arguments.compare_checked_seed,
            )
            if arguments.cohort == "user"
            else run_generated_frontier(arguments.root)
        )
    except (FrontierError, OSError) as error:
        print(f"CupidC production frontier failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())

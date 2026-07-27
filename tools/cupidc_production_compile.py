#!/usr/bin/env python3
"""Compile approved Cupid OS production sources with a frozen CupidC tool."""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from contextlib import ExitStack
from pathlib import Path
from typing import Sequence

try:
    from tools.bootstrap_toolchain import BootstrapError, freeze_seed_inputs
    from tools.cupidc_kernel_compile import (
        KERNEL_I386_ARGUMENTS,
        KernelCompileError,
        SeedExecutor,
        validate_i386_relocatable,
    )
    from tools.native_user_toolchain import (
        NativeToolError,
        NativeToolExecutor,
        capture_native_tool,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import BootstrapError, freeze_seed_inputs
    from cupidc_kernel_compile import (
        KERNEL_I386_ARGUMENTS,
        KernelCompileError,
        SeedExecutor,
        validate_i386_relocatable,
    )
    from native_user_toolchain import (
        NativeToolError,
        NativeToolExecutor,
        capture_native_tool,
    )


USER_SOURCES = (
    "user/examples/cat.cc",
    "user/examples/hello.cc",
    "user/examples/ls.cc",
)
GENERATED_INSTALL_SOURCES = (
    "kernel/util/bin_programs_gen.cc",
    "kernel/util/demos_programs_gen.cc",
    "kernel/util/docs_programs_gen.cc",
)
APPROVED_SOURCES = {
    "user": USER_SOURCES,
    "generated-install": GENERATED_INSTALL_SOURCES,
}

USER_I386_ARGUMENTS = (
    "--freestanding",
    "-I",
    "/user",
)

USER_INCLUDE_CLOSURE = (
    "user/cupid.h",
)
GENERATED_INCLUDE_CLOSURE = (
    "drivers/serial.h",
    "kernel/core/types.h",
    "kernel/fs/homefs.h",
    "kernel/fs/ramfs.h",
    "kernel/fs/vfs.h",
)

DEFAULT_TIMEOUT_SECONDS = 180
TOOL_MODES = ("auto", "checked-seed", "native-windows")


class ProductionCompileError(RuntimeError):
    """A production compilation could not publish an object."""


def profile_arguments(cohort: str) -> tuple[str, ...]:
    if cohort == "user":
        return USER_I386_ARGUMENTS
    if cohort == "generated-install":
        return KERNEL_I386_ARGUMENTS
    raise ProductionCompileError(f"unknown production cohort: {cohort}")


def build_compile_arguments(
    cohort: str,
    logical_source: str,
    logical_output: str,
    compiler_root: str,
) -> tuple[str, ...]:
    return (
        "-c",
        logical_source,
        "-o",
        logical_output,
        *profile_arguments(cohort),
        "--root",
        compiler_root,
    )


def _root_path(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise ProductionCompileError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise ProductionCompileError(
            f"repository root is not a directory: {resolved}"
        )
    return resolved


def _approved_source(
    root: Path, cohort: str, source: Path
) -> tuple[Path, str]:
    if cohort not in APPROVED_SOURCES:
        raise ProductionCompileError(f"unknown production cohort: {cohort}")
    candidate = source if source.is_absolute() else root / source
    if candidate.is_symlink():
        raise ProductionCompileError("production source may not be a symlink")
    try:
        resolved = candidate.resolve(strict=True)
        relative = resolved.relative_to(root).as_posix()
    except (OSError, ValueError) as error:
        raise ProductionCompileError(
            f"source must resolve inside the repository: {source}"
        ) from error
    if relative not in APPROVED_SOURCES[cohort]:
        raise ProductionCompileError(
            f"source is outside the approved {cohort} cohort: {relative}"
        )
    if not resolved.is_file():
        raise ProductionCompileError(
            f"approved source is not a file: {relative}"
        )
    return resolved, "/" + relative


def _output_path(root: Path, output: Path) -> Path:
    candidate = output if output.is_absolute() else root / output
    if candidate.is_symlink():
        raise ProductionCompileError("output may not be a symlink")
    try:
        parent = candidate.parent.resolve(strict=True)
        resolved = (parent / candidate.name).resolve(strict=False)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise ProductionCompileError(
            f"output must stay inside the repository: {output}"
        ) from error
    if not parent.is_dir() or not relative.parts:
        raise ProductionCompileError(
            f"output parent is not a repository directory: {parent}"
        )
    if resolved.exists() and not resolved.is_file():
        raise ProductionCompileError(
            f"output is not a regular file: {resolved}"
        )
    if resolved.suffix != ".o":
        raise ProductionCompileError("compiler output must use the .o suffix")
    return resolved


def _validate_output_binding(
    root: Path,
    cohort: str,
    source: Path,
    output: Path,
) -> None:
    source_relative = source.relative_to(root)
    output_relative = output.relative_to(root)
    if cohort == "user":
        valid = (
            len(output_relative.parts) >= 3
            and output_relative.parts[0] == "user"
            and output_relative.parts[1] != "examples"
            and output.name == source.stem + ".o"
        )
    else:
        valid = (
            len(output_relative.parts) >= 3
            and output_relative.parts[:2] == ("kernel", "util")
            and output.name == source.stem + ".o"
        )
    if not valid:
        raise ProductionCompileError(
            f"source and output do not form an approved output pair: "
            f"{source_relative.as_posix()} -> {output_relative.as_posix()}"
        )


def _closure_paths(root: Path, cohort: str, source: Path) -> tuple[Path, ...]:
    relatives: Sequence[str]
    if cohort == "user":
        relatives = USER_INCLUDE_CLOSURE
    else:
        relatives = GENERATED_INCLUDE_CLOSURE
    paths = [source]
    for relative in relatives:
        path = root / relative
        if path.is_symlink() or not path.is_file():
            raise ProductionCompileError(
                f"production include closure is unavailable: {relative}"
            )
        paths.append(path.resolve())
    return tuple(paths)


def _capture(paths: Sequence[Path]) -> dict[Path, bytes]:
    captured = {}
    for path in paths:
        try:
            captured[path] = path.read_bytes()
        except OSError as error:
            raise ProductionCompileError(
                f"cannot read production input {path}: {error}"
            ) from error
    return captured


def _snapshot(paths: Sequence[Path]) -> dict[Path, str]:
    return {
        path: hashlib.sha256(payload).hexdigest()
        for path, payload in _capture(paths).items()
    }


def _write_frozen_closure(
    root: Path,
    frozen_root: Path,
    captured: dict[Path, bytes],
) -> None:
    for source, payload in captured.items():
        relative = source.relative_to(root)
        target = frozen_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)


def _compiler_root_for(
    executor: SeedExecutor | NativeToolExecutor,
    path: Path,
) -> str:
    mapper = getattr(executor, "compiler_root_for", None)
    if callable(mapper):
        return str(mapper(path))
    return str(path.resolve())


def compile_production_source(
    root: Path,
    cohort: str,
    source: Path,
    output: Path,
    *,
    manifest: Path | None = None,
    native_compiler: Path | None = None,
    tool_mode: str = "auto",
    executor: SeedExecutor | NativeToolExecutor | None = None,
    timeout: int = DEFAULT_TIMEOUT_SECONDS,
) -> None:
    """Compile one approved source and publish its object atomically."""
    root = _root_path(root)
    source, logical_source = _approved_source(root, cohort, source)
    output = _output_path(root, output)
    _validate_output_binding(root, cohort, source, output)
    if source == output:
        raise ProductionCompileError("output may not replace its source")
    if timeout <= 0:
        raise ProductionCompileError("compiler timeout must be positive")
    if tool_mode not in TOOL_MODES:
        raise ProductionCompileError(
            f"unknown production compiler mode: {tool_mode}"
        )
    if native_compiler is not None and tool_mode == "checked-seed":
        raise ProductionCompileError(
            "a native compiler cannot be used in checked-seed mode"
        )
    if manifest is not None and (
        native_compiler is not None or tool_mode == "native-windows"
    ):
        raise ProductionCompileError(
            "a checked-seed manifest cannot be combined with a native compiler"
        )
    use_native = (
        native_compiler is not None
        or tool_mode == "native-windows"
        or (
            tool_mode == "auto"
            and cohort == "user"
            and executor is None
            and manifest is None
            and os.name == "nt"
        )
    )
    if use_native and cohort != "user":
        raise ProductionCompileError(
            "native Windows compilation is limited to the user cohort"
        )

    closure = _closure_paths(root, cohort, source)
    captured = _capture(closure)
    before = {
        path: hashlib.sha256(payload).hexdigest()
        for path, payload in captured.items()
    }
    try:
        with ExitStack() as stack:
            native_snapshot = None
            if use_native:
                try:
                    native_snapshot = capture_native_tool(
                        root, "cupidc", native_compiler
                    )
                    tool_directory = Path(
                        stack.enter_context(
                            tempfile.TemporaryDirectory(
                                prefix="cupidc-production-native-"
                            )
                        )
                    )
                    compiler = native_snapshot.stage(tool_directory)
                    active_executor = (
                        executor
                        if executor is not None
                        else NativeToolExecutor(root)
                    )
                except NativeToolError as error:
                    raise ProductionCompileError(str(error)) from error
            else:
                manifest_path = (
                    manifest.resolve()
                    if manifest is not None
                    else root
                    / "bootstrap"
                    / "seeds"
                    / "i386-linux"
                    / "manifest.json"
                )
                try:
                    active_executor = (
                        executor
                        if executor is not None
                        else SeedExecutor(root)
                    )
                except KernelCompileError as error:
                    raise ProductionCompileError(
                        f"checked seed executor is unavailable: {error}"
                    ) from error
                seed_directory = Path(
                    stack.enter_context(
                        tempfile.TemporaryDirectory(
                            prefix="cupidc-production-seed-"
                        )
                    )
                )
                try:
                    seed_inputs = freeze_seed_inputs(
                        manifest_path, seed_directory
                    )
                except (BootstrapError, OSError) as error:
                    raise ProductionCompileError(
                        f"checked seed verification failed: {error}"
                    ) from error
                compiler = seed_inputs.tools.get("cupidc")
                if compiler is None:
                    raise ProductionCompileError(
                        "checked seed verification did not return CupidC"
                    )

            temporary = stack.enter_context(
                tempfile.TemporaryDirectory(
                    prefix=f".{output.name}.cupidc-inputs-",
                    dir=output.parent,
                )
            )
            frozen_root = Path(temporary)
            _write_frozen_closure(root, frozen_root, captured)
            frozen_output = frozen_root / ".output" / output.name
            frozen_output.parent.mkdir()
            logical_temporary = "/.output/" + output.name
            arguments = build_compile_arguments(
                cohort,
                logical_source,
                logical_temporary,
                _compiler_root_for(active_executor, frozen_root),
            )
            try:
                result = active_executor.run(
                    compiler, arguments, timeout
                )
            except subprocess.TimeoutExpired as error:
                raise ProductionCompileError(
                    f"CupidC timed out after {timeout} seconds for "
                    f"{logical_source.lstrip('/')}"
                ) from error
            except OSError as error:
                raise ProductionCompileError(
                    f"CupidC could not run for "
                    f"{logical_source.lstrip('/')}: {error}"
                ) from error
            if result.returncode != 0:
                details = (result.stderr or result.stdout or "").strip()
                suffix = f": {details}" if details else ""
                raise ProductionCompileError(
                    f"CupidC failed for {logical_source.lstrip('/')} "
                    f"with status {result.returncode}{suffix}"
                )
            if frozen_output.is_symlink() or not frozen_output.is_file():
                raise ProductionCompileError(
                    f"CupidC did not write an object for "
                    f"{logical_source.lstrip('/')}"
                )
            try:
                validate_i386_relocatable(frozen_output)
            except Exception as error:
                raise ProductionCompileError(
                    f"invalid object for "
                    f"{logical_source.lstrip('/')}: {error}"
                ) from error
            if native_snapshot is not None:
                try:
                    native_snapshot.require_unchanged(
                        "compiling with native CupidC"
                    )
                except NativeToolError as error:
                    raise ProductionCompileError(str(error)) from error
            if _snapshot(closure) != before:
                raise ProductionCompileError(
                    f"production inputs changed while compiling "
                    f"{logical_source.lstrip('/')}"
                )
            os.replace(frozen_output, output)
    except OSError as error:
        raise ProductionCompileError(
            f"could not publish production object {output}: {error}"
        ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compile an approved production source with CupidC."
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--cohort",
        required=True,
        choices=tuple(APPROVED_SOURCES),
    )
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument(
        "--tool-mode",
        choices=TOOL_MODES,
        default="auto",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        compile_production_source(
            arguments.root,
            arguments.cohort,
            arguments.source,
            arguments.output,
            manifest=arguments.manifest,
            tool_mode=arguments.tool_mode,
            timeout=arguments.timeout,
        )
    except ProductionCompileError as error:
        print(f"CupidC production compile failed: {error}", file=sys.stderr)
        return 1
    print(f"CupidC production object: {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

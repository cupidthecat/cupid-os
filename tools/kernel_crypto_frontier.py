#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path

try:
    from tools.cupidc_kernel_compile import (
        APPROVED_CRYPTO_SOURCES,
        BLOCKED_CRYPTO_SOURCES,
        KernelCompileError,
        KERNEL_CRYPTO_C_SOURCES,
        KERNEL_I386_ARGUMENTS,
        validate_i386_relocatable_bytes,
    )
except ModuleNotFoundError:
    from cupidc_kernel_compile import (
        APPROVED_CRYPTO_SOURCES,
        BLOCKED_CRYPTO_SOURCES,
        KernelCompileError,
        KERNEL_CRYPTO_C_SOURCES,
        KERNEL_I386_ARGUMENTS,
        validate_i386_relocatable_bytes,
    )

try:
    from tools.bootstrap_toolchain import BootstrapError, freeze_seed_inputs
except ModuleNotFoundError:
    from bootstrap_toolchain import BootstrapError, freeze_seed_inputs

CRYPTO_SOURCES = APPROVED_CRYPTO_SOURCES

BOUNDARIES = (
    (
        "kernel/crypto/csprng.c",
        29,
        "CTB00000F",
        "GNU inline assembly is outside this function-body slice",
    ),
    (
        "kernel/crypto/asn1.c",
        102,
        "CTD000006",
        "CupidC IR lowering does not yet support this conversion",
    ),
    (
        "kernel/crypto/x509.c",
        120,
        "CTD000006",
        "CupidC IR lowering does not yet support this conversion",
    ),
    (
        "kernel/crypto/x509_chain.c",
        96,
        "CTD000006",
        "CupidC IR lowering does not yet support this conversion",
    ),
)

class FrontierError(Exception):
    pass


def _validated_wsl_private_directory(stdout):
    lines = stdout.splitlines()
    if (
        len(lines) != 1
        or re.fullmatch(
            r"/tmp/cupid-kernel-frontier\.[A-Za-z0-9]{6}",
            lines[0],
        )
        is None
    ):
        raise FrontierError(
            "WSL returned an invalid private seed directory"
        )
    return lines[0]


def _run_control_command(command, operation):
    try:
        result = subprocess.run(
            command,
            text=True,
            capture_output=True,
        )
    except OSError as error:
        raise FrontierError(f"{operation}: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr + result.stdout).strip()
        raise FrontierError(f"{operation}: {detail}")
    return result


def _wsl_path(wsl, path):
    result = _run_control_command(
        [wsl, "-e", "wslpath", "-a", str(path.resolve())],
        f"WSL could not translate {path}",
    )
    translated = result.stdout.strip()
    if not translated:
        raise FrontierError(f"WSL returned no path for {path}")
    return translated


@contextmanager
def _default_seed_execution(root):
    seed_directory = root / "bootstrap" / "seeds" / "i386-linux"
    manifest_path = seed_directory / "manifest.json"
    with tempfile.TemporaryDirectory(
        prefix="cupid-kernel-crypto-seed-"
    ) as temporary:
        try:
            seed_inputs = freeze_seed_inputs(
                manifest_path, type(root)(temporary)
            )
        except (BootstrapError, OSError) as error:
            raise FrontierError(
                f"checked seed verification failed: {error}"
            ) from error
        seed = seed_inputs.tools["cupidc"]
        provenance = _compiler_provenance(
            seed,
            "checked-seed",
            seed_manifest_sha256=seed_inputs.manifest_sha256,
        )
        if os.name != "nt":
            yield [str(seed)], str(root), provenance
            return

        wsl = shutil.which("wsl")
        if wsl is None:
            raise FrontierError(
                "WSL is required to run the checked i386 Linux seed on Windows"
            )
        compiler_root = _wsl_path(wsl, root)
        linux_seed = _wsl_path(wsl, seed)
        stage_script = (
            "umask 077; "
            'private="$(mktemp -d '
            '"/tmp/cupid-kernel-frontier.XXXXXX")" '
            "|| exit 125; "
            'chmod 700 "$private" || exit 125; '
            'cp "$1" "$private/tool" || exit 125; '
            'chmod 700 "$private/tool" || exit 125; '
            'printf "%s\\n" "$private"'
        )
        stage_result = _run_control_command(
            [
                wsl,
                "-e",
                "sh",
                "-c",
                stage_script,
                "sh",
                linux_seed,
            ],
            "could not stage the checked CupidC seed in WSL",
        )
        private_directory = _validated_wsl_private_directory(
            stage_result.stdout
        )
        staged_seed = private_directory + "/tool"
        body_failed = True
        try:
            yield [wsl, "-e", staged_seed], compiler_root, provenance
            body_failed = False
        finally:
            try:
                _run_control_command(
                    [
                        wsl,
                        "-e",
                        "rm",
                        "-rf",
                        "--",
                        private_directory,
                    ],
                    "could not remove the staged CupidC seed from WSL",
                )
            except FrontierError:
                if not body_failed:
                    raise


@contextmanager
def _explicit_compiler_execution(arguments, root):
    compiler_host_path = arguments.compiler_host_path
    if compiler_host_path is None:
        if arguments.compiler is None:
            compiler_host_path = (
                root
                / "bootstrap"
                / "seeds"
                / "i386-linux"
                / "cupidc.elf"
            )
        else:
            compiler_host_path = Path(arguments.compiler)
    if not compiler_host_path.is_absolute():
        compiler_host_path = root / compiler_host_path
    compiler_host_path = compiler_host_path.resolve()
    if not compiler_host_path.is_file():
        raise FrontierError(
            f"compiler provenance file does not exist: {compiler_host_path}"
        )

    with tempfile.TemporaryDirectory(
        prefix="cupid-kernel-crypto-compiler-"
    ) as temporary:
        frozen_compiler = Path(temporary) / compiler_host_path.name
        try:
            shutil.copyfile(compiler_host_path, frozen_compiler)
            frozen_compiler.chmod(0o700)
        except OSError as error:
            raise FrontierError(
                f"could not freeze explicit compiler: {error}"
            ) from error
        if arguments.runner is None:
            prefix = [str(frozen_compiler)]
        else:
            prefix = [
                arguments.runner,
                *arguments.runner_arg,
                str(frozen_compiler),
            ]
        provenance = _compiler_provenance(
            frozen_compiler,
            "explicit",
        )
        yield (
            prefix,
            arguments.compiler_root or str(root),
            provenance,
        )


def _parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Check the strict CupidC kernel crypto source frontier."
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="host path to the Cupid OS repository",
    )
    parser.add_argument(
        "--compiler",
        help="compiler path as seen by the runner",
    )
    parser.add_argument(
        "--compiler-host-path",
        type=Path,
        help="host path used to identify an explicit compiler",
    )
    parser.add_argument(
        "--runner",
        help="optional executable that launches the compiler",
    )
    parser.add_argument(
        "--runner-arg",
        action="append",
        default=[],
        help="argument placed between the runner and compiler",
    )
    parser.add_argument(
        "--compiler-root",
        help="repository root as seen by the compiler",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="empty host directory inside the repository for audit artifacts",
    )
    return parser.parse_args(argv)


def _inside_root(root, path):
    try:
        return path.relative_to(root)
    except ValueError as error:
        raise FrontierError(
            f"output directory must be inside repository root: {path}"
        ) from error


def _logical_path(root, path):
    relative = _inside_root(root, path)
    return "/" + relative.as_posix()


def _profile_arguments():
    return list(KERNEL_I386_ARGUMENTS)


def _compiler_arguments(compiler_root):
    values = _profile_arguments()
    values.extend(("--root", compiler_root))
    return values


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _input_paths(root):
    paths = {root / source for source in KERNEL_CRYPTO_C_SOURCES}
    arguments = KERNEL_I386_ARGUMENTS
    for index, argument in enumerate(arguments):
        if argument != "-I":
            continue
        include_root = root / arguments[index + 1].lstrip("/")
        if not include_root.is_dir():
            continue
        for suffix in ("*.h", "*.inc"):
            paths.update(include_root.rglob(suffix))
    return sorted(paths)


def _capture_input_snapshot(root):
    snapshot = {}
    for path in _input_paths(root):
        try:
            relative = path.resolve().relative_to(root)
        except ValueError as error:
            raise FrontierError(
                f"frontier input is outside repository root: {path}"
            ) from error
        if not path.is_file():
            raise FrontierError(f"frontier input is not a file: {path}")
        snapshot[relative.as_posix()] = _sha256(path)
    return snapshot


def _snapshot_sha256(snapshot):
    encoded = json.dumps(
        snapshot,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def _require_input_snapshot(root, expected):
    actual = _capture_input_snapshot(root)
    if actual == expected:
        return
    changed = sorted(
        path
        for path in set(expected) | set(actual)
        if expected.get(path) != actual.get(path)
    )
    rendered = ", ".join(changed[:5])
    if len(changed) > 5:
        rendered += f", and {len(changed) - 5} more"
    raise FrontierError(
        f"kernel crypto inputs changed during frontier run: {rendered}"
    )


def _profile_provenance():
    arguments = _profile_arguments()
    encoded = json.dumps(
        arguments,
        ensure_ascii=True,
        separators=(",", ":"),
    ).encode("ascii")
    return {
        "name": "KERNEL_I386",
        "arguments": arguments,
        "sha256": hashlib.sha256(encoded).hexdigest(),
    }


def _compiler_provenance(
    compiler,
    mode,
    seed_manifest_sha256=None,
):
    if not compiler.is_file():
        raise FrontierError(f"compiler provenance file does not exist: {compiler}")
    record = {
        "mode": mode,
        "size": compiler.stat().st_size,
        "sha256": _sha256(compiler),
    }
    if seed_manifest_sha256 is not None:
        record["seed_manifest_sha256"] = seed_manifest_sha256
    return {
        "compiler": record,
        "profile": _profile_provenance(),
    }


def _invoke(command):
    try:
        return subprocess.run(
            command,
            text=True,
            capture_output=True,
        )
    except OSError as error:
        raise FrontierError(f"could not run compiler: {error}") from error


def _compile(
    command_prefix,
    compiler_arguments,
    source,
    logical_output,
    host_output,
):
    result = _invoke(
        [
            *command_prefix,
            "-c",
            "/" + source,
            "-o",
            logical_output,
            *compiler_arguments,
        ]
    )
    if result.returncode != 0:
        detail = (result.stderr + result.stdout).strip()
        raise FrontierError(f"{source} did not compile: {detail}")
    if not host_output.is_file():
        raise FrontierError(f"{source} did not publish an object")


def _check_boundary(
    command_prefix,
    compiler_arguments,
    source,
    logical_output,
    host_output,
    line,
    code,
    message,
):
    result = _invoke(
        [
            *command_prefix,
            "-c",
            "/" + source,
            "-o",
            logical_output,
            *compiler_arguments,
        ]
    )
    detail = result.stderr + result.stdout
    location = f"/{source}:{line}:"
    if result.returncode == 0:
        raise FrontierError(f"{source} unexpectedly compiled")
    if host_output.exists():
        raise FrontierError(f"{source} published an object after failure")
    if location not in detail or code not in detail or message not in detail:
        raise FrontierError(
            f"{source} returned an unexpected diagnostic: {detail.strip()}"
        )


def _prepare_output(root, requested):
    output = requested
    if not output.is_absolute():
        output = root / output
    output = output.resolve()
    _inside_root(root, output)
    if output.exists():
        if not output.is_dir():
            raise FrontierError(f"output path is not a directory: {output}")
        raise FrontierError(f"output directory already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    return output


@contextmanager
def _staged_output(output):
    with tempfile.TemporaryDirectory(
        prefix=f".{output.name}.",
        dir=output.parent,
    ) as temporary:
        staging = Path(temporary) / "result"
        staging.mkdir()
        for name in ("first", "second", "negative"):
            (staging / name).mkdir()
        yield staging
        try:
            os.replace(staging, output)
        except OSError as error:
            raise FrontierError(
                f"could not publish frontier directory {output}: {error}"
            ) from error


def _validate_elf32_header(image):
    try:
        validate_i386_relocatable_bytes(image)
    except KernelCompileError as error:
        raise FrontierError(str(error)) from error


def _execute_frontier(
    root,
    output,
    prefix,
    compiler_root,
    provenance,
    input_snapshot,
):
    common = _compiler_arguments(compiler_root)
    source_records = []

    for source in CRYPTO_SOURCES:
        name = Path(source).stem + ".o"
        first_path = output / "first" / name
        second_path = output / "second" / name
        _compile(
            prefix,
            common,
            source,
            _logical_path(root, first_path),
            first_path,
        )
        _require_input_snapshot(root, input_snapshot)
        _compile(
            prefix,
            common,
            source,
            _logical_path(root, second_path),
            second_path,
        )
        _require_input_snapshot(root, input_snapshot)
        first = first_path.read_bytes()
        second = second_path.read_bytes()
        if not first:
            raise FrontierError(f"{source} produced an empty object")
        if first != second:
            raise FrontierError(f"{source} object output is not deterministic")
        try:
            _validate_elf32_header(first)
        except FrontierError as error:
            raise FrontierError(f"{source} produced invalid ELF32: {error}") from error
        source_records.append(
            {
                "source": source,
                "size": len(first),
                "source_sha256": input_snapshot[source],
                "object_sha256": hashlib.sha256(first).hexdigest(),
            }
        )

    boundary_records = []
    for source, line, code, message in BOUNDARIES:
        negative_path = output / "negative" / (Path(source).stem + ".o")
        _check_boundary(
            prefix,
            common,
            source,
            _logical_path(root, negative_path),
            negative_path,
            line,
            code,
            message,
        )
        _require_input_snapshot(root, input_snapshot)
        boundary_records.append(
            {
                "source": source,
                "line": line,
                "code": code,
                "message": message,
                "source_sha256": input_snapshot[source],
            }
        )

    manifest = {
        "schema": "cupid.kernel-crypto-frontier.v1",
        "target": {
            "architecture": "i386",
            "byte_order": "little",
            "elf_class": 32,
            "object_type": "ET_REL",
        },
        "provenance": provenance,
        "input_snapshot": {
            "count": len(input_snapshot),
            "files": input_snapshot,
            "sha256": _snapshot_sha256(input_snapshot),
        },
        "sources": source_records,
        "boundaries": boundary_records,
    }
    temporary_manifest = output / "manifest.json.tmp"
    temporary_manifest.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    temporary_manifest.replace(output / "manifest.json")


def _run_frontier(arguments):
    root = arguments.root.resolve()
    if not root.is_dir():
        raise FrontierError(f"repository root does not exist: {root}")
    boundary_sources = tuple(sorted(item[0] for item in BOUNDARIES))
    if boundary_sources != BLOCKED_CRYPTO_SOURCES:
        raise FrontierError("declared crypto boundary cohort differs")
    actual_sources = tuple(
        sorted(
            path.relative_to(root).as_posix()
            for path in (root / "kernel" / "crypto").glob("*.c")
        )
    )
    if actual_sources != KERNEL_CRYPTO_C_SOURCES:
        missing = sorted(set(KERNEL_CRYPTO_C_SOURCES) - set(actual_sources))
        unexpected = sorted(set(actual_sources) - set(KERNEL_CRYPTO_C_SOURCES))
        raise FrontierError(
            "kernel crypto source inventory differs: "
            f"missing={missing!r}, unexpected={unexpected!r}"
        )

    output = _prepare_output(root, arguments.output_dir)
    input_snapshot = _capture_input_snapshot(root)
    execution_context = (
        _default_seed_execution(root)
        if arguments.compiler is None and arguments.runner is None
        else _explicit_compiler_execution(arguments, root)
    )
    with _staged_output(output) as staging:
        with execution_context as execution:
            prefix, compiler_root, provenance = execution
            _execute_frontier(
                root,
                staging,
                prefix,
                arguments.compiler_root or compiler_root,
                provenance,
                input_snapshot,
            )
            _require_input_snapshot(root, input_snapshot)


def main(argv=None):
    arguments = _parse_arguments(argv)
    try:
        _run_frontier(arguments)
    except FrontierError as error:
        print(f"kernel crypto frontier: error: {error}", file=sys.stderr)
        return 1
    print(
        "kernel crypto frontier: "
        f"ok ({len(CRYPTO_SOURCES)} sources, {len(BOUNDARIES)} boundaries)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

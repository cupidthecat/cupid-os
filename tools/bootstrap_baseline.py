"""Capture reproducible host-toolchain evidence for Cupid OS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


class BaselineError(RuntimeError):
    """A baseline could not be captured or verified."""


@dataclass(frozen=True)
class CheckSpec:
    """One named command in the baseline workflow."""

    name: str
    command: tuple[str, ...]


@dataclass(frozen=True)
class CommandResult:
    """Observable result returned by a command adapter."""

    returncode: int
    elapsed_seconds: float
    output: str


@dataclass(frozen=True)
class ToolSpec:
    """How one required host tool is selected and queried."""

    default: str
    environment: str
    version_args: tuple[str, ...]


SCHEMA = "cupid.bootstrap-baseline.v1"
BUILD_ENVIRONMENT = ("CC_TARGET", "EXTRA_CFLAGS")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hash_artifacts(root: Path, paths: Iterable[str]) -> dict[str, object]:
    """Return a stable, content-addressed manifest for relative artifact paths."""
    root = root.resolve()
    files: list[dict[str, object]] = []
    for relative in sorted(set(paths)):
        path = (root / relative).resolve()
        try:
            normalized = path.relative_to(root).as_posix()
        except ValueError as exc:
            raise BaselineError(f"artifact escapes baseline root: {relative}") from exc
        if not path.is_file():
            raise BaselineError(f"missing baseline artifact: {normalized}")
        files.append(
            {
                "path": normalized,
                "size": path.stat().st_size,
                "sha256": _sha256_file(path),
            }
        )

    aggregate = hashlib.sha256()
    for entry in files:
        aggregate.update(
            f"{entry['path']}\0{entry['size']}\0{entry['sha256']}\n".encode("utf-8")
        )
    return {"digest": aggregate.hexdigest(), "files": files}


def compare_artifact_manifests(
    first: dict[str, object], second: dict[str, object]
) -> dict[str, object]:
    """Compare two build manifests and localize every differing artifact."""
    first_files = {
        str(entry["path"]): {
            "size": entry["size"],
            "sha256": entry["sha256"],
        }
        for entry in first["files"]
    }
    second_files = {
        str(entry["path"]): {
            "size": entry["size"],
            "sha256": entry["sha256"],
        }
        for entry in second["files"]
    }
    mismatches = []
    for path in sorted(set(first_files) | set(second_files)):
        first_entry = first_files.get(path)
        second_entry = second_files.get(path)
        if first_entry != second_entry:
            mismatches.append(
                {"path": path, "first": first_entry, "second": second_entry}
            )
    return {"matched": not mismatches, "mismatches": mismatches}


def elf32_section_size(path: Path, section_name: str) -> int:
    """Read one section size directly from a little-endian ELF32 file."""
    image = path.read_bytes()
    if len(image) < 52 or image[:7] != b"\x7fELF\x01\x01\x01":
        raise BaselineError(f"not a little-endian ELF32 file: {path}")

    section_offset = struct.unpack_from("<I", image, 32)[0]
    section_entry_size = struct.unpack_from("<H", image, 46)[0]
    section_count = struct.unpack_from("<H", image, 48)[0]
    string_index = struct.unpack_from("<H", image, 50)[0]
    table_end = section_offset + section_entry_size * section_count
    if (
        section_entry_size < 40
        or string_index >= section_count
        or table_end > len(image)
    ):
        raise BaselineError(f"invalid ELF32 section table: {path}")

    string_header = section_offset + string_index * section_entry_size
    strings_offset, strings_size = struct.unpack_from("<II", image, string_header + 16)
    strings_end = strings_offset + strings_size
    if strings_end > len(image):
        raise BaselineError(f"invalid ELF32 section-name table: {path}")
    strings = image[strings_offset:strings_end]

    for index in range(section_count):
        header = section_offset + index * section_entry_size
        name_offset = struct.unpack_from("<I", image, header)[0]
        if name_offset >= len(strings):
            continue
        name_end = strings.find(b"\0", name_offset)
        if name_end < 0:
            continue
        name = strings[name_offset:name_end].decode("ascii", errors="replace")
        if name == section_name:
            return struct.unpack_from("<I", image, header + 20)[0]
    raise BaselineError(f"ELF32 section not found: {section_name}")


def run_check_sequence(
    checks: Iterable[CheckSpec], invoke: Callable[[CheckSpec], CommandResult]
) -> list[dict[str, object]]:
    """Run ordered checks, preserving the first failure and every skipped check."""
    results: list[dict[str, object]] = []
    failed_name: str | None = None
    for check in checks:
        if failed_name is not None:
            results.append(
                {
                    "name": check.name,
                    "command": list(check.command),
                    "status": "skipped",
                    "reason": f"blocked by failed check: {failed_name}",
                }
            )
            continue

        command_result = invoke(check)
        status = "pass" if command_result.returncode == 0 else "fail"
        output_lines = command_result.output.rstrip().splitlines()
        results.append(
            {
                "name": check.name,
                "command": list(check.command),
                "status": status,
                "returncode": command_result.returncode,
                "elapsed_seconds": round(command_result.elapsed_seconds, 3),
                "output_sha256": hashlib.sha256(
                    command_result.output.encode("utf-8", errors="replace")
                ).hexdigest(),
                "output_tail": output_lines[-20:],
            }
        )
        if status == "fail":
            failed_name = check.name
    return results


def _split_command(value: str) -> tuple[str, ...]:
    parts = shlex.split(value, posix=os.name != "nt")
    if os.name == "nt":
        parts = [
            part[1:-1]
            if len(part) >= 2 and part[0] == part[-1] and part[0] in {'"', "'"}
            else part
            for part in parts
        ]
    if not parts:
        raise BaselineError("empty tool command")
    return tuple(parts)


def _tool_specs() -> dict[str, ToolSpec]:
    windows = os.name == "nt"
    return {
        "make": ToolSpec("make", "MAKE", ("--version",)),
        "python": ToolSpec("python" if windows else "python3", "PYTHON", ("--version",)),
        "c_compiler": ToolSpec("clang" if windows else "gcc", "CC", ("--version",)),
        "symbol_reader": ToolSpec(
            "llvm-nm" if windows else "nm", "NM", ("--version",)
        ),
        "qemu": ToolSpec("qemu-system-i386", "QEMU", ("--version",)),
    }


def _optional_oracle_tool_specs() -> dict[str, ToolSpec]:
    return {
        "nasm": ToolSpec("nasm", "NASM", ("-v",)),
    }


def optional_oracle_commands() -> dict[str, tuple[str, ...]]:
    """Return the exact configured commands used by optional oracle tests."""
    return {
        name: _split_command(os.environ.get(spec.environment, spec.default))
        for name, spec in _optional_oracle_tool_specs().items()
    }


def _tool_commands() -> dict[str, tuple[tuple[str, ...], tuple[str, ...]]]:
    specs = _tool_specs()
    commands = {
        name: _split_command(os.environ.get(spec.environment, spec.default))
        for name, spec in specs.items()
    }
    return {
        name: (command, specs[name].version_args)
        for name, command in commands.items()
    }


def resolve_tool_command(command: tuple[str, ...]) -> tuple[str, ...] | None:
    """Resolve one configured executable while preserving its arguments."""
    executable = shutil.which(command[0])
    if executable is None and Path(command[0]).is_file():
        executable = str(Path(command[0]).resolve())
    if executable is None:
        return None
    return (str(Path(executable).resolve()), *command[1:])


def _probe_tool(
    command: tuple[str, ...], version_args: tuple[str, ...]
) -> dict[str, object]:
    resolved = resolve_tool_command(command)
    if resolved is None:
        return {
            "status": "missing",
            "command": list(command),
            "error": f"executable not found: {command[0]}",
        }
    try:
        completed = subprocess.run(
            [*resolved, *version_args],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as exc:
        return {
            "status": "error",
            "command": list(command),
            "executable": resolved[0],
            "error": str(exc),
        }
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    result: dict[str, object] = {
        "status": "pass" if completed.returncode == 0 else "error",
        "command": list(command),
        "executable": resolved[0],
        "executable_sha256": _sha256_file(Path(resolved[0])),
        "version": lines[0] if lines else "",
        "version_returncode": completed.returncode,
    }
    return result


def probe_tools() -> tuple[dict[str, dict[str, object]], dict[str, tuple[str, ...]]]:
    """Resolve and fingerprint every external tool used by the oracle baseline."""
    definitions = _tool_commands()
    evidence = {
        name: _probe_tool(command, version_args)
        for name, (command, version_args) in definitions.items()
    }
    commands = {
        name: (str(evidence[name]["executable"]), *command[1:])
        for name, (command, _) in definitions.items()
        if evidence[name]["status"] == "pass"
    }
    return evidence, commands


def probe_optional_oracle_tools() -> dict[str, dict[str, object]]:
    """Record comparison tools without making them normal-build requirements."""
    commands = optional_oracle_commands()
    return {
        name: _probe_tool(commands[name], spec.version_args)
        for name, spec in _optional_oracle_tool_specs().items()
    }


def probe_optional_jpeg_tools() -> dict[str, dict[str, object]]:
    """Record optional JPEG tools that can influence embedded object bytes."""
    definitions = {
        "jpegtran": (("jpegtran",), ("-version",)),
        "djpeg": (("djpeg",), ("-version",)),
        "cjpeg": (("cjpeg",), ("-version",)),
        "ffmpeg": (("ffmpeg",), ("-version",)),
    }
    return {
        name: _probe_tool(command, version_args)
        for name, (command, version_args) in definitions.items()
    }


def _run_command(check: CheckSpec, cwd: Path) -> CommandResult:
    print(f"[baseline] {check.name}: {shlex.join(check.command)}", flush=True)
    started = time.perf_counter()
    completed = subprocess.run(
        list(check.command),
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    elapsed = time.perf_counter() - started
    output = completed.stdout
    root_text = str(cwd)
    output = output.replace(root_text, "<WORKTREE>")
    output = output.replace(root_text.replace("\\", "/"), "<WORKTREE>")
    temp_text = re.escape(str(Path(tempfile.gettempdir()).resolve()))
    output = re.sub(
        temp_text + r"[\\/](?:tmp|cupid-baseline-)[^\\/\s]+",
        "<TEMP>",
        output,
        flags=re.IGNORECASE,
    )
    status = "pass" if completed.returncode == 0 else "FAIL"
    print(f"[baseline] {check.name}: {status} ({elapsed:.3f}s)", flush=True)
    if completed.returncode != 0:
        for line in output.rstrip().splitlines()[-40:]:
            print(line, flush=True)
    return CommandResult(completed.returncode, elapsed, output)


def _git_output(repo_root: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise BaselineError(completed.stdout.strip() or f"git {' '.join(args)} failed")
    return completed.stdout.strip()


@contextmanager
def isolated_worktree(repo_root: Path, revision: str, run_index: int):
    """Check out one committed revision in a verified disposable directory."""
    temp_root = Path(tempfile.gettempdir()).resolve()
    base = Path(tempfile.mkdtemp(prefix=f"cupid-baseline-{run_index}-")).resolve()
    if temp_root != base and temp_root not in base.parents:
        raise BaselineError(f"unsafe baseline temporary directory: {base}")
    checkout = base / "checkout"
    added = False
    try:
        print(f"[baseline] creating isolated build {run_index}", flush=True)
        completed = subprocess.run(
            [
                "git",
                "-C",
                str(repo_root),
                "worktree",
                "add",
                "--detach",
                str(checkout),
                revision,
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if completed.returncode != 0:
            raise BaselineError(completed.stdout.strip())
        added = True
        yield checkout
    finally:
        if added:
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(repo_root),
                    "worktree",
                    "remove",
                    "--force",
                    str(checkout),
                ],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            subprocess.run(
                ["git", "-C", str(repo_root), "worktree", "prune"],
                check=False,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        if base.exists():
            shutil.rmtree(base)


def _last_json_array(lines: Iterable[str]) -> list[str]:
    for line in reversed(list(lines)):
        candidate = line.strip()
        if candidate.startswith("["):
            try:
                value = json.loads(candidate)
            except json.JSONDecodeError:
                continue
            if isinstance(value, list) and all(isinstance(item, str) for item in value):
                return value
    raise BaselineError("Make did not emit the bootstrap artifact list")


def capture_build(
    checkout: Path,
    run_index: int,
    jobs: int,
    tool_commands: dict[str, tuple[str, ...]],
    invoke: Callable[[CheckSpec], CommandResult] | None = None,
) -> dict[str, object]:
    """Build and hash one worktree before optional runtime verification."""
    make = tool_commands["make"]
    python = tool_commands["python"]
    qemu = tool_commands["qemu"]
    make_config = (
        "WAD_SRCS=",
        "HDD_MB=200",
        "FAT_START_LBA=16384",
    )
    build_checks = [
        CheckSpec("clean", (*make, *make_config, "distclean")),
        CheckSpec("build", (*make, f"-j{jobs}", *make_config, "all")),
        CheckSpec(
            "artifact-list",
            (*make, "-s", *make_config, "print-bootstrap-artifacts"),
        ),
    ]
    if invoke is None:
        invoke = lambda check: _run_command(check, checkout)
    results = run_check_sequence(build_checks, invoke)
    build: dict[str, object] = {
        "run": run_index,
        "status": "pass" if all(item["status"] == "pass" for item in results) else "fail",
        "checks": results,
    }
    if build["status"] != "pass":
        return build

    artifact_check = next(item for item in results if item["name"] == "artifact-list")
    artifacts = _last_json_array(artifact_check["output_tail"])
    artifact_check["output_tail"] = [f"<artifact list: {len(artifacts)} paths>"]
    build["artifact_order"] = list(dict.fromkeys(artifacts))
    build["artifacts"] = hash_artifacts(checkout, build["artifact_order"])
    build["quality"] = {
        "kernel_text_bytes": elf32_section_size(checkout / "kernel/kernel.elf", ".text"),
        "kernel_elf_bytes": (checkout / "kernel/kernel.elf").stat().st_size,
        "kernel_binary_bytes": (checkout / "kernel/kernel.bin").stat().st_size,
        "disk_image_bytes": (checkout / "cupidos.img").stat().st_size,
    }

    if run_index == 1:
        log_dir = checkout / "build" / "bootstrap" / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        validation_checks = [
            CheckSpec(
                "host-unit-tests",
                (
                    *python,
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "tests",
                    "-p",
                    "test_*.py",
                ),
            ),
            CheckSpec(
                "cupidc-gui-smoke",
                (
                    *python,
                    "tools/gui_terminal_smoke.py",
                    "--qemu",
                    qemu[0],
                    "--command",
                    "/bin/ls.cc",
                    "--key-pause",
                    "0.60",
                    "--log",
                    "build/bootstrap/logs/cupidc.log",
                    "--timeout",
                    "60",
                ),
            ),
            CheckSpec(
                "cupidasm-gui-smoke",
                (
                    *python,
                    "tools/gui_terminal_smoke.py",
                    "--qemu",
                    qemu[0],
                    "--command",
                    "as /demos/hello.asm",
                    "--key-pause",
                    "0.60",
                    "--log",
                    "build/bootstrap/logs/cupidasm.log",
                    "--timeout",
                    "60",
                ),
            ),
        ]
        validation_results = run_check_sequence(validation_checks, invoke)
        results.extend(validation_results)
        if any(item["status"] != "pass" for item in validation_results):
            build["status"] = "fail"
    return build


def _write_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    temporary.replace(path)


def capture_baseline(
    repo_root: Path,
    revision: str,
    output: Path,
    jobs: int,
    runs: int,
) -> tuple[dict[str, object], int]:
    """Capture build, runtime, quality, and two-run reproducibility evidence."""
    if jobs < 1:
        raise BaselineError("--jobs must be at least 1")
    if runs < 2:
        raise BaselineError("--runs must be at least 2")
    repo_root = repo_root.resolve()
    source_revision = _git_output(repo_root, "rev-parse", "--verify", f"{revision}^{{commit}}")
    tools, tool_commands = probe_tools()
    oracle_tools = probe_optional_oracle_tools()
    jpeg_tools = probe_optional_jpeg_tools()
    missing_tools = sorted(name for name, item in tools.items() if item["status"] != "pass")
    manifest: dict[str, object] = {
        "schema": SCHEMA,
        "source": {
            "revision": source_revision,
            "isolated_worktree": True,
        },
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python_version": platform.python_version(),
        },
        "configuration": {
            "runs": runs,
            "jobs": jobs,
            "hdd_mb": 200,
            "fat_start_lba": 16384,
            "optional_wads": False,
            "environment_overrides": {
                name: os.environ[name]
                for name in (
                    *(spec.environment for spec in _tool_specs().values()),
                    *(
                        spec.environment
                        for spec in _optional_oracle_tool_specs().values()
                    ),
                    *BUILD_ENVIRONMENT,
                )
                if name in os.environ
            },
        },
        "tools": tools,
        "optional_oracle_tools": oracle_tools,
        "optional_jpeg_tools": jpeg_tools,
        "builds": [],
    }
    if missing_tools:
        manifest["status"] = "fail"
        manifest["error"] = f"tool preflight failed: {', '.join(missing_tools)}"
        _write_json(output, manifest)
        return manifest, 1

    builds = []
    try:
        for run_index in range(1, runs + 1):
            with isolated_worktree(repo_root, source_revision, run_index) as checkout:
                build = capture_build(
                    checkout,
                    run_index,
                    jobs,
                    tool_commands,
                )
                builds.append(build)
                if build["status"] != "pass":
                    break
    except (BaselineError, OSError, subprocess.SubprocessError) as exc:
        manifest["builds"] = builds
        manifest["status"] = "fail"
        manifest["error"] = str(exc)
        _write_json(output, manifest)
        return manifest, 1

    manifest["builds"] = builds
    if len(builds) != runs or any(build["status"] != "pass" for build in builds):
        manifest["status"] = "fail"
        manifest["error"] = "baseline check failed before reproducibility comparison"
        _write_json(output, manifest)
        return manifest, 1

    comparisons = [
        compare_artifact_manifests(builds[0]["artifacts"], build["artifacts"])
        for build in builds[1:]
    ]
    mismatches = [
        {"run": index + 2, "files": comparison["mismatches"]}
        for index, comparison in enumerate(comparisons)
        if not comparison["matched"]
    ]
    manifest["reproducibility"] = {
        "matched": not mismatches,
        "reference_run": 1,
        "compared_runs": list(range(2, runs + 1)),
        "mismatches": mismatches,
    }
    build_seconds = []
    for build in builds:
        build_check = next(item for item in build["checks"] if item["name"] == "build")
        build_seconds.append(build_check["elapsed_seconds"])
    manifest["performance"] = {
        "build_seconds": build_seconds,
        "runtime_and_test_seconds": {
            item["name"]: item["elapsed_seconds"]
            for item in builds[0]["checks"]
            if item["name"] in {
                "host-unit-tests",
                "cupidc-gui-smoke",
                "cupidasm-gui-smoke",
            }
        },
        "gating": False,
        "note": "Host wall-clock observations are frozen evidence, not the future 20% guest-performance gate.",
    }
    manifest["quality"] = builds[0]["quality"]
    manifest["status"] = "pass" if not mismatches else "fail"
    if mismatches:
        manifest["error"] = "isolated builds produced different artifacts"
    _write_json(output, manifest)
    return manifest, 0 if manifest["status"] == "pass" else 1


def _default_output(repo_root: Path) -> Path:
    host = f"{platform.system()}-{platform.machine()}".lower().replace(" ", "-")
    return repo_root / "build" / "bootstrap" / f"{host}.json"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build a committed Cupid OS revision twice and record baseline evidence."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--revision", default="HEAD")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--runs", type=int, default=2)
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    output = args.output or _default_output(repo_root)
    if not output.is_absolute():
        output = repo_root / output
    try:
        manifest, returncode = capture_baseline(
            repo_root,
            args.revision,
            output.resolve(),
            args.jobs,
            args.runs,
        )
    except BaselineError as exc:
        print(f"baseline: {exc}", file=sys.stderr)
        return 2
    print(f"[baseline] evidence: {output.resolve()}", flush=True)
    print(f"[baseline] status: {manifest['status']}", flush=True)
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())

"""Produce a deterministic inventory of Cupid OS build and language inputs."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import posixpath
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


SCHEMA = "cupid.build-graph-audit.v1"
SOURCE_SUFFIXES = {
    ".c": "c",
    ".cc": "cupid_c",
    ".cup": "cupid_script",
    ".h": "c_header",
    ".asm": "assembly",
    ".s": "assembly",
}
TOOL_MARKERS = (
    ("$(CUPIDASM)", "cupid_assembler"),
    ("$(CUPIDLD)", "cupid_linker"),
    ("$(CUPIDOBJ)", "cupid_object"),
    ("$(CC)", "host_c_compiler"),
    ("$(ASM)", "nasm"),
    ("$(LD)", "host_linker"),
    ("$(OBJCOPY)", "host_object_copy"),
    ("$(NM)", "host_symbol_reader"),
    ("$(PYTHON)", "host_python"),
    ("$(MAKE)", "make"),
)
EXCLUDED_SOURCE_TREES = {".agents", ".git", "__pycache__", "build", "templeos"}

# These relations cannot be inferred from byte identity: the older sources have
# diverged from the active implementation or changed language/path. Keep the
# project-specific audit decisions explicit and validate the target against the
# active graph before reporting one.
KNOWN_SOURCE_RELATIONS = {
    "bin/cupidc.c": ("historical_copy_of", "kernel/lang/cupidc.c"),
    "bin/cupidc_lex.c": ("historical_copy_of", "kernel/lang/cupidc_lex.c"),
    "bin/cupidc_parse.c": ("historical_copy_of", "kernel/lang/cupidc_parse.c"),
    "bin/fat16.c": ("historical_copy_of", "kernel/fs/fat16.c"),
    "bin/fat16_vfs.c": ("historical_copy_of", "kernel/fs/fat16_vfs.c"),
    "bin/kernel.c": ("historical_copy_of", "kernel/core/kernel.c"),
    "bin/terminal_app.c": ("historical_copy_of", "kernel/gui/terminal_app.c"),
    "demos/paint.cc": ("superseded_by", "bin/paint.cc"),
    "kernel/core/scheduler.c": ("superseded_by", "kernel/core/process.c"),
    "kernel/core/scheduler.h": ("superseded_by", "kernel/core/process.h"),
    "kernel/gui/notepad.c": ("superseded_by", "bin/notepad.cc"),
    "kernel/gui/terminal_ansi.c": ("superseded_by", "kernel/gui/ansi.c"),
}


class AuditError(RuntimeError):
    """The supported build graph could not be inventoried."""


@dataclass
class MakeRule:
    """One expanded target rule from GNU Make's database."""

    prerequisites: list[str] = field(default_factory=list)
    recipe: list[str] = field(default_factory=list)


@dataclass
class BuildModel:
    """One supported Make root normalized to repository-relative paths."""

    directory: str
    root_target: str
    rules: dict[str, MakeRule]
    reachable: set[str]
    direct_sources: set[str]
    generated_sources: set[str]
    forced_sources: set[str]
    includes_by_source: dict[str, list[str]]
    include_search_paths: list[str]
    transforms: list[dict[str, object]]


@dataclass
class FeatureEvidence:
    """Aggregated, source-located evidence for one language requirement."""

    occurrences: int = 0
    files: set[str] = field(default_factory=set)
    examples: list[dict[str, object]] = field(default_factory=list)


class FeatureCollector:
    """Collect stable feature identifiers through one small interface."""

    def __init__(self) -> None:
        self._features: dict[str, FeatureEvidence] = {}
        self._by_source: dict[str, set[str]] = {}

    def add(
        self,
        feature_id: str,
        path: str,
        line: int,
        text: str,
        occurrences: int = 1,
    ) -> None:
        if occurrences <= 0:
            return
        evidence = self._features.setdefault(feature_id, FeatureEvidence())
        evidence.occurrences += occurrences
        evidence.files.add(path)
        self._by_source.setdefault(path, set()).add(feature_id)
        example = {"path": path, "line": line, "text": text.strip()[:160]}
        if example not in evidence.examples and len(evidence.examples) < 3:
            evidence.examples.append(example)

    def for_source(self, path: str) -> list[str]:
        return sorted(self._by_source.get(path, set()))

    def inventory(self) -> list[dict[str, object]]:
        return [
            {
                "id": feature_id,
                "category": feature_id.split(".", 2)[1],
                "occurrences": evidence.occurrences,
                "files": sorted(evidence.files),
                "examples": evidence.examples,
            }
            for feature_id, evidence in sorted(self._features.items())
        ]


C_KEYWORD_FEATURES = {
    "_Alignas": "c.type.alignment_specifier",
    "_Alignof": "c.expression.alignof",
    "_Atomic": "c.type.atomic",
    "_Bool": "c.type.bool",
    "_Complex": "c.type.complex",
    "_Generic": "c.expression.generic_selection",
    "_Imaginary": "c.type.imaginary",
    "_Noreturn": "c.function.noreturn",
    "_Static_assert": "c.declaration.static_assert",
    "_Thread_local": "c.storage.thread_local",
    "auto": "c.storage.auto",
    "break": "c.control.break",
    "case": "c.control.case",
    "char": "c.type.char",
    "const": "c.qualifier.const",
    "continue": "c.control.continue",
    "default": "c.control.default",
    "do": "c.control.do",
    "double": "c.type.double",
    "else": "c.control.else",
    "enum": "c.type.enum",
    "extern": "c.storage.extern",
    "float": "c.type.float",
    "for": "c.control.for",
    "goto": "c.control.goto",
    "if": "c.control.if",
    "inline": "c.storage.inline",
    "int": "c.type.int",
    "long": "c.type.long",
    "register": "c.storage.register",
    "restrict": "c.qualifier.restrict",
    "return": "c.control.return",
    "short": "c.type.short",
    "signed": "c.type.signed",
    "sizeof": "c.expression.sizeof",
    "static": "c.storage.static",
    "struct": "c.type.struct",
    "switch": "c.control.switch",
    "typedef": "c.type.typedef",
    "union": "c.type.union",
    "unsigned": "c.type.unsigned",
    "void": "c.type.void",
    "volatile": "c.qualifier.volatile",
    "while": "c.control.while",
}

CUPID_TYPE_TOKENS = {
    "Bool": "bool",
    "F64": "f64",
    "I8": "i8",
    "I16": "i16",
    "I32": "i32",
    "I64": "i64",
    "U0": "u0",
    "U8": "u8",
    "U16": "u16",
    "U32": "u32",
    "U64": "u64",
    "double2": "double2",
    "float4": "float4",
}

CUPID_KEYWORD_FEATURES = {
    "class": "cupid_c.declaration.class",
    "del": "cupid_c.expression.del",
    "new": "cupid_c.expression.new",
    "noreg": "cupid_c.storage.noreg",
    "reg": "cupid_c.storage.reg",
}

C_PREPROCESSOR_DIRECTIVES = {
    "define",
    "elif",
    "else",
    "endif",
    "error",
    "if",
    "ifdef",
    "ifndef",
    "include",
    "line",
    "pragma",
    "undef",
}

ASM_DIRECTIVES = {
    "align",
    "bits",
    "common",
    "db",
    "dd",
    "dq",
    "dt",
    "dw",
    "endstruc",
    "equ",
    "extern",
    "global",
    "incbin",
    "istruc",
    "org",
    "rb",
    "rd",
    "reserve",
    "resb",
    "resd",
    "resq",
    "rest",
    "resw",
    "rw",
    "section",
    "segment",
    "struc",
    "times",
}

ASM_PREFIXES = {"a16", "a32", "lock", "o16", "o32", "rep", "repe", "repne"}
ASM_REGISTERS = {
    "al", "ah", "ax", "eax", "bl", "bh", "bx", "ebx",
    "cl", "ch", "cx", "ecx", "dl", "dh", "dx", "edx",
    "si", "esi", "di", "edi", "sp", "esp", "bp", "ebp",
    "cs", "ds", "es", "fs", "gs", "ss", "cr0", "cr2", "cr3", "cr4",
    "dr0", "dr1", "dr2", "dr3", "dr6", "dr7",
    *(f"mm{index}" for index in range(8)),
    *(f"xmm{index}" for index in range(8)),
    *(f"st{index}" for index in range(8)),
}


def _normalized_relative(root: Path, path: Path) -> str | None:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return None


def _language(path: str) -> str | None:
    return SOURCE_SUFFIXES.get(Path(path).suffix.lower())


def _run_make_database(root: Path, make: str, target: str) -> str:
    # GNU Make executes recipes containing $(MAKE) even under -n.  Replace the
    # recursive command while printing the database so a missing hosted Cupid
    # tool cannot append a nested Makefile database and overwrite this root's
    # `all` rule during parsing.  Recipes remain unexpanded in `-p` output.
    result = subprocess.run(
        [make, "MAKE=:", "--no-print-directory", "-prRn", target],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(
            f"GNU Make could not expand target {target!r}: {detail}"
        )
    return result.stdout


def _read_make_json_list(root: Path, make: str, target: str) -> list[str]:
    result = subprocess.run(
        [make, "--no-print-directory", "-s", target],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(f"GNU Make target {target!r} failed: {detail}")
    for line in reversed(result.stdout.splitlines()):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, list) and all(isinstance(item, str) for item in value):
            return value
    raise AuditError(f"GNU Make target {target!r} did not emit a JSON string list")


def _parse_make_rules(database: str) -> dict[str, MakeRule]:
    marker = "# Files"
    if marker not in database:
        raise AuditError("GNU Make database did not contain a Files section")

    rules: dict[str, MakeRule] = {}
    current_targets: list[str] = []
    in_files = False
    for raw_line in database.splitlines():
        line = raw_line.rstrip()
        if line == marker:
            in_files = True
            continue
        if not in_files:
            continue
        if line.startswith("# files hash-table stats:"):
            break
        if line.startswith("\t"):
            command = line.lstrip()
            for target in current_targets:
                rules[target].recipe.append(command)
            continue
        if not line or line[0].isspace() or line.startswith("#"):
            continue
        if ":" not in line:
            continue

        target_text, prerequisite_text = line.split(":", 1)
        if "=" in target_text:
            continue
        current_targets = target_text.split()
        prerequisites = [
            item
            for item in prerequisite_text.split()
            if item not in {"|", "FORCE"}
        ]
        for target in current_targets:
            rules[target] = MakeRule(prerequisites=list(prerequisites))

    return rules


def _reachable_rules(rules: dict[str, MakeRule], target: str) -> set[str]:
    if target not in rules:
        raise AuditError(f"GNU Make database has no target named {target!r}")
    reachable: set[str] = set()
    pending = [target]
    while pending:
        current = pending.pop()
        if current in reachable:
            continue
        reachable.add(current)
        rule = rules.get(current)
        if rule is None:
            continue
        pending.extend(
            prerequisite
            for prerequisite in rule.prerequisites
            if prerequisite in rules and prerequisite not in reachable
        )
    return reachable


def _mask_c_comments(text: str) -> str:
    """Replace C comments with whitespace while preserving literals and lines."""
    output: list[str] = []
    index = 0
    state = "code"
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            output.append(char)
            index += 1
            if char == '"':
                state = "string"
            elif char == "'":
                state = "character"
            continue
        if state == "line_comment":
            output.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
            continue
        if state == "block_comment":
            if char == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
                continue
            output.append("\n" if char == "\n" else " ")
            index += 1
            continue
        output.append(char)
        index += 1
        if char == "\\" and index < len(text):
            output.append(text[index])
            index += 1
        elif (state == "string" and char == '"') or (
            state == "character" and char == "'"
        ):
            state = "code"
    return "".join(output)


def _declared_includes(path: Path, language: str) -> list[tuple[str, str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise AuditError(f"could not read source input {path}: {exc}") from exc
    if language == "assembly":
        return [
            (match.group(1), "assembly")
            for match in re.finditer(
                r'^\s*%include\s+["\']([^"\']+)["\']',
                text,
                flags=re.MULTILINE | re.IGNORECASE,
            )
        ]
    text = _mask_c_comments(text)
    return [
        (match.group(2), "quoted" if match.group(1) == '"' else "angle")
        for match in re.finditer(
            r'^\s*#\s*include\s*(["<])([^">]+)[">]',
            text,
            flags=re.MULTILINE,
        )
    ]


def _make_include_configuration(root: Path) -> tuple[list[str], list[str]]:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return [], []
    text = makefile.read_text(encoding="utf-8", errors="replace")
    logical_text = re.sub(r"\\\r?\n", " ", text)
    include_paths: list[str] = []
    forced_includes: list[str] = []
    for match in re.finditer(r"(?:^|\s)-I\s*([^\s]+)", logical_text):
        value = match.group(1).strip('"\'').replace("\\", "/")
        value = re.sub(r"^\./", "", value)
        if "$" not in value and value not in include_paths:
            include_paths.append(value)
    for match in re.finditer(r"(?:^|\s)-include\s+([^\s]+)", logical_text):
        value = match.group(1).strip('"\'').replace("\\", "/")
        value = re.sub(r"^\./", "", value)
        if "$" not in value and value not in forced_includes:
            forced_includes.append(value)
    return include_paths, forced_includes


def _resolve_include(
    root: Path,
    source_path: Path,
    include: str,
    kind: str,
    include_paths: list[str],
) -> str | None:
    search_roots: list[Path] = []
    if kind in {"quoted", "assembly"}:
        search_roots.append(source_path.parent)
    search_roots.extend(root / path for path in include_paths)
    search_roots.append(root)
    for search_root in search_roots:
        candidate = search_root / include
        if not candidate.is_file():
            continue
        normalized = _normalized_relative(root, candidate)
        if normalized is not None and _language(normalized) is not None:
            return normalized
    return None


def _include_closure(
    root: Path,
    direct_sources: set[str],
    include_paths: list[str],
    opaque_sources: set[str] | None = None,
) -> dict[str, list[str]]:
    includes_by_source: dict[str, list[str]] = {}
    opaque_sources = opaque_sources or set()
    pending = sorted(direct_sources)
    seen: set[str] = set()
    while pending:
        relative = pending.pop()
        if relative in seen:
            continue
        seen.add(relative)
        language = _language(relative)
        if language not in {"c", "cupid_c", "c_header", "assembly"}:
            continue
        if relative in opaque_sources:
            includes_by_source[relative] = []
            continue
        source_path = root / relative
        if not source_path.is_file():
            continue

        resolved: list[str] = []
        for include, kind in _declared_includes(source_path, language):
            included_relative = _resolve_include(
                root,
                source_path,
                include,
                kind,
                include_paths,
            )
            if included_relative is None or _language(included_relative) is None:
                continue
            resolved.append(included_relative)
            if included_relative not in seen:
                pending.append(included_relative)
        includes_by_source[relative] = sorted(set(resolved))
    return includes_by_source


def _tools_for_recipe(recipe: list[str]) -> list[str]:
    joined = "\n".join(recipe)
    tools = [tool for marker, tool in TOOL_MARKERS if marker in joined]
    if recipe and not tools:
        return ["host_shell"]
    return tools


def _artifact_coverage_contract(
    root: Path,
    make: str,
    rules: dict[str, MakeRule],
    transforms: list[dict[str, object]],
) -> dict[str, object] | None:
    target = "print-bootstrap-artifacts"
    if target not in rules:
        return None
    declared = set(_read_make_json_list(root, make, target))
    linked_objects = {
        str(source)
        for transform in transforms
        if {"host_linker", "cupid_linker"}.intersection(transform["tools"])
        for source in transform["inputs"]
        if str(source).endswith(".o")
    }
    missing = sorted(linked_objects - declared)
    return {
        "status": "pass" if not missing else "fail",
        "declared_artifacts": len(declared),
        "linked_objects": len(linked_objects),
        "missing_link_inputs": missing,
    }


def _prefix_repo_path(directory: str, path: str) -> str:
    normalized = path.replace("\\", "/")
    if directory in {"", "."} or normalized.startswith("/"):
        return posixpath.normpath(normalized)
    if re.match(r"^[A-Za-z]:/", normalized):
        return normalized
    if normalized.startswith("./"):
        normalized = normalized[2:]
    return posixpath.normpath(f"{directory.rstrip('/')}/{normalized}")


def _operation_for_recipe(
    recipe: list[str],
    tools: list[str],
    output: str,
    c_object_operation: str,
) -> str:
    joined = " ".join(recipe).lower()
    if "host_c_compiler" in tools:
        if output.lower().endswith((".o", ".obj")) or re.search(
            r"(?:^|\s)-c(?:\s|$)", joined
        ):
            return c_object_operation
        return "compile_and_link_host_executable"
    if "nasm" in tools or "cupid_assembler" in tools:
        if re.search(r"(?:^|\s)-f\s+bin(?:\s|$)", joined):
            return "assemble_flat_binary"
        if re.search(r"(?:^|\s)-f\s+elf32(?:\s|$)", joined):
            return "assemble_elf32_relocatable"
        return "assemble"
    if "host_linker" in tools or "cupid_linker" in tools:
        return "link_elf32_executable"
    if "host_object_copy" in tools or "cupid_object" in tools:
        if (
            ("-i binary" in joined and "-o elf32-i386" in joined)
            or re.search(r"(?:^|\s)wrap(?:\s|$)", joined)
        ):
            return "wrap_binary_as_elf32_relocatable"
        if re.search(r"(?:^|\s)-o\s+binary(?:\s|$)", joined) or re.search(
            r"(?:^|\s)flat(?:\s|$)", joined
        ):
            return "extract_raw_binary"
        return "transform_object"
    if "host_python" in tools:
        if " hostbuild.py image " in joined:
            return "package_disk_image"
        if " gen-" in joined or " mksyms " in joined:
            return "generate_c_source"
        return "host_orchestration"
    if "make" in tools:
        return "recursive_make"
    return "host_command"


def _build_transforms(
    directory: str,
    reachable: set[str],
    rules: dict[str, MakeRule],
) -> list[dict[str, object]]:
    transforms = []
    host_object_outputs = {
        prerequisite
        for local_output in reachable
        if "host_c_compiler" in _tools_for_recipe(rules[local_output].recipe)
        and not local_output.lower().endswith((".o", ".obj"))
        and not re.search(
            r"(?:^|\s)-c(?:\s|$)",
            " ".join(rules[local_output].recipe).lower(),
        )
        for prerequisite in rules[local_output].prerequisites
        if prerequisite.lower().endswith((".o", ".obj"))
    }
    for local_output in sorted(reachable):
        rule = rules[local_output]
        if not rule.recipe:
            continue
        tools = _tools_for_recipe(rule.recipe)
        transforms.append(
            {
                "output": _prefix_repo_path(directory, local_output),
                "inputs": [
                    _prefix_repo_path(directory, item)
                    for item in dict.fromkeys(rule.prerequisites)
                ],
                "tools": tools,
                "operation": _operation_for_recipe(
                    rule.recipe,
                    tools,
                    local_output,
                    (
                        "compile_c_to_host_object"
                        if local_output in host_object_outputs
                        else "compile_c_to_elf32_object"
                    ),
                ),
                "recipe": rule.recipe,
            }
        )
    return transforms


def _collect_build_model(
    root: Path,
    make: str,
    target: str,
    directory: str,
) -> BuildModel:
    normalized_directory = directory.replace("\\", "/").strip("/") or "."
    build_root = root if normalized_directory == "." else root / normalized_directory
    if not (build_root / "Makefile").is_file():
        raise AuditError(
            f"supplemental build directory has no Makefile: {normalized_directory}"
        )

    database = _run_make_database(build_root, make, target)
    rules = _parse_make_rules(database)
    reachable = _reachable_rules(rules, target)
    graph_sources = {
        item
        for rule_target in reachable
        for item in [rule_target, *rules[rule_target].prerequisites]
        if _language(item) is not None
    }
    generated_local = {
        item
        for item in graph_sources
        if item in rules and bool(rules[item].recipe)
    }
    direct_local = {
        item
        for item in graph_sources
        if (build_root / item).is_file() or item in generated_local
    }
    include_paths, forced_include_names = _make_include_configuration(build_root)
    forced_local: set[str] = set()
    for include in forced_include_names:
        resolved = _resolve_include(
            build_root,
            build_root / "Makefile",
            include,
            "angle",
            include_paths,
        )
        if resolved is not None:
            forced_local.add(resolved)
    includes_local = _include_closure(
        build_root,
        direct_local | forced_local,
        include_paths,
        generated_local,
    )

    return BuildModel(
        directory=normalized_directory,
        root_target=target,
        rules=rules,
        reachable=reachable,
        direct_sources={
            _prefix_repo_path(normalized_directory, path) for path in direct_local
        },
        generated_sources={
            _prefix_repo_path(normalized_directory, path) for path in generated_local
        },
        forced_sources={
            _prefix_repo_path(normalized_directory, path) for path in forced_local
        },
        includes_by_source={
            _prefix_repo_path(normalized_directory, source): [
                _prefix_repo_path(normalized_directory, included)
                for included in includes
            ]
            for source, includes in includes_local.items()
        },
        include_search_paths=[
            _prefix_repo_path(normalized_directory, path) for path in include_paths
        ],
        transforms=_build_transforms(normalized_directory, reachable, rules),
    )


def _source_digest(path: Path) -> str:
    """Hash source/control text under the repository's canonical LF policy."""
    content = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return hashlib.sha256(content).hexdigest()


def _source_cohort(path: str, language: str | None, generated: bool) -> str:
    if generated:
        if path == "kernel/cpu/ksyms_data.c":
            return "generated_symbol_table"
        return "generated_install_table"
    if path.startswith("user/examples/"):
        return "user_program"
    if path.startswith("user/"):
        return "user_runtime_interface"
    if path == "boot/boot.asm":
        return "boot_assembly"
    if path in {
        "kernel/cpu/isr.asm",
        "kernel/core/context_switch.asm",
        "kernel/smp/smp_trampoline.S",
    }:
        return "kernel_assembly"
    if path.startswith("demos/") and language == "assembly":
        return "cupid_asm_demo"
    if path.startswith("bin/browser/") and language == "cupid_c":
        return "cupid_c_browser_fragment"
    if path.startswith("bin/") and language == "cupid_c":
        return "cupid_c_program"
    if path.startswith("bin/") and language == "c_header":
        return "cupid_c_runtime_header"
    if path.startswith("kernel/doom/src/"):
        return "vendored_doom"
    if path.startswith("kernel/doom/"):
        return "doom_port"
    basename = Path(path).name
    if path.startswith("toolchain/tests/"):
        return "toolchain_contract"
    if path.startswith("toolchain/") and basename.startswith("ctool_host"):
        return "toolchain_host_adapter"
    if path.startswith("toolchain/") and basename in {
        "cupiddis.c",
        "cupiddis.h",
        "cupiddis_main.c",
    }:
        return "cupiddis"
    if path.startswith("toolchain/") and basename in {
        "cupidasm.c",
        "cupidasm.h",
        "cupidasm_main.c",
    }:
        return "cupidasm"
    if path.startswith("toolchain/"):
        return "toolchain_core"
    if path.startswith("kernel/lang/") and basename.startswith("ctool_kernel"):
        return "toolchain_kernel_adapter"
    if path.startswith("kernel/lang/") and basename.startswith("cupidc"):
        return "cupidc"
    if path.startswith("kernel/lang/") and (
        basename == "as.c" or basename == "as.h" or basename.startswith("as_")
    ):
        return "cupidasm"
    if path.startswith("kernel/lang/") and basename in {"dis.c", "dis.h"}:
        return "cupiddis"
    if path.startswith("drivers/"):
        return "driver"
    if path.startswith("kernel/"):
        parts = path.split("/")
        return f"kernel_{parts[1]}" if len(parts) > 2 else "kernel"
    return "project_source"


def _roadmap(
    sources: list[dict[str, object]],
    features: list[dict[str, object]],
) -> dict[str, object]:
    feature_by_id = {str(feature["id"]): feature for feature in features}
    sources_by_cohort: dict[str, list[str]] = collections.defaultdict(list)
    for source in sources:
        sources_by_cohort[str(source["cohort"])].append(str(source["path"]))

    definitions = [
        (
            "host_runnable_toolchain_core",
            "Establish a host-runnable shared Cupid Toolchain core",
            (
                "toolchain_core",
                "toolchain_host_adapter",
                "toolchain_kernel_adapter",
                "toolchain_contract",
                "cupidc",
                "cupidasm",
                "cupiddis",
            ),
            (),
            "The shared foundations cross hosted and kernel adapters; CupidDis and hosted CupidASM consume them, while CupidC and the CupidASM kernel adapter remain.",
        ),
        (
            "elf32_relocatable_interchange",
            "Emit and consume deterministic ELF32 relocatable objects",
            ("generated_install_table", "generated_symbol_table"),
            ("c.output.elf32_relocatable", "asm.output.elf32_relocatable"),
            "Every compiled C unit and two kernel assembly units cross the ELF32 ET_REL seam.",
        ),
        (
            "shared_i386_abi_and_instruction_model",
            "Share one i386 ABI and instruction model",
            ("kernel_assembly",),
            ("c.extension.inline_assembly", "asm.instruction.", "asm.register."),
            "C code generation, assembly encoding, and disassembly exercise the same 16/32-bit machine domain.",
        ),
        (
            "cupiddis_object_inspection",
            "Make CupidDis inspect raw and ELF32 relocatable output",
            ("cupiddis",),
            ("asm.output.", "asm.relocation."),
            "Assembler migration needs independent sections, symbols, relocation, and instruction evidence.",
        ),
        (
            "cupidasm_source_controls_and_expressions",
            "Implement the active Cupid ASM directives and expression language",
            ("boot_assembly", "kernel_assembly", "cupid_asm_demo"),
            ("asm.directive.", "asm.expression.", "asm.preprocessor."),
            "BITS, ORG, data/reserve forms, times, includes, %define, and label arithmetic gate real sources.",
        ),
        (
            "cupidasm_encoding_and_raw_parity",
            "Reach byte parity for boot and trampoline binaries",
            ("boot_assembly", "kernel_assembly"),
            ("asm.addressing.", "asm.prefix.", "asm.output.flat_binary"),
            "Fixed boot offsets require complete 16/32-bit encoding and ModRM/SIB/address-size behavior.",
        ),
        (
            "cupidasm_symbols_and_relocations",
            "Emit ELF32 sections, symbols, and i386 relocations",
            ("kernel_assembly",),
            ("asm.directive.global", "asm.directive.extern", "asm.relocation."),
            "ISR and context-switch objects must interoperate with host- and CupidC-produced objects.",
        ),
        (
            "cupidc_preprocessor",
            "Implement the active C and Cupid C preprocessing contract",
            (),
            ("c.preprocessor.", "cupid_c.directive."),
            "Includes, forced headers, conditionals, macro rescanning, paste/stringify, and packing affect every C cohort.",
        ),
        (
            "cupidc_c11_types_initializers_and_abi",
            "Implement freestanding C11 type, initializer, and cdecl semantics",
            (),
            ("c.type.", "c.declarator.", "c.initializer.", "c.qualifier."),
            "Kernel and user sources require ILP32 layout, 64-bit arithmetic, callbacks, aggregates, and volatile semantics.",
        ),
        (
            "cupidc_platform_extensions",
            "Implement required GNU attributes and extended inline assembly",
            (),
            ("c.extension.attribute.", "c.extension.inline_assembly"),
            "Core platform and tool sources directly depend on attributes, constraints, clobbers, and privileged instructions.",
        ),
        (
            "cupidc_doom_compatibility",
            "Compile the complete Doom and compatibility cohort",
            ("doom_port", "vendored_doom"),
            (),
            "Vendored Doom adds relaxed diagnostics and legacy declaration/callback compatibility without weakening strict C mode.",
        ),
        (
            "cupid_mode_production_and_extensions",
            "Scale Cupid mode across embedded programs and browser fragments",
            ("cupid_c_program", "cupid_c_browser_fragment"),
            ("cupid_c.",),
            "Production-sized globals/includes come before demo-only class, allocation, register, and SIMD extensions.",
        ),
    ]

    capability_priorities = []
    for identifier, title, cohorts, feature_prefixes, rationale in definitions:
        cohort_paths = {
            path for cohort in cohorts for path in sources_by_cohort.get(cohort, [])
        }
        matched_features = sorted(
            feature_id
            for feature_id in feature_by_id
            if any(
                feature_id == prefix or feature_id.startswith(prefix)
                for prefix in feature_prefixes
            )
        )
        feature_paths = {
            str(path)
            for feature_id in matched_features
            for path in feature_by_id[feature_id]["files"]
        }
        evidence_paths = sorted(cohort_paths | feature_paths)
        if not evidence_paths and not matched_features:
            continue
        capability_priorities.append(
            {
                "rank": len(capability_priorities) + 1,
                "id": identifier,
                "title": title,
                "rationale": rationale,
                "cohorts": list(cohorts),
                "feature_ids": matched_features,
                "source_count": len(evidence_paths),
                "sample_sources": evidence_paths[:12],
            }
        )

    cohort_definitions = [
        (
            "toolchain_sources",
            (
                "toolchain_core",
                "toolchain_host_adapter",
                "toolchain_kernel_adapter",
                "toolchain_contract",
                "cupidc",
                "cupidasm",
                "cupiddis",
            ),
            "Bootstrap the tools that transfer ownership to every later cohort.",
        ),
        (
            "boot_and_kernel_assembly",
            ("boot_assembly", "kernel_assembly"),
            "Keep the four production transforms CupidASM-owned while retaining NASM only as an optional parity oracle.",
        ),
        (
            "kernel_and_drivers",
            tuple(
                sorted(
                    cohort
                    for cohort in sources_by_cohort
                    if cohort == "driver"
                    or (cohort.startswith("kernel_") and cohort not in {"kernel_assembly"})
                )
            ),
            "Move foundational strict C before vendored compatibility cohorts.",
        ),
        (
            "doom_and_vendored_c",
            ("doom_port", "vendored_doom"),
            "Preserve upstream behavior under a deliberate compatibility mode.",
        ),
        (
            "user_programs",
            ("user_program", "user_runtime_interface"),
            "Migrate the remaining separate host-C compilation path to CupidC and stage its CupidLD outputs deliberately.",
        ),
        (
            "embedded_cupid_sources",
            (
                "cupid_c_runtime_header",
                "cupid_c_program",
                "cupid_c_browser_fragment",
                "cupid_asm_demo",
            ),
            "Keep runtime CupidC/CupidASM regression corpora active through the host migration.",
        ),
    ]
    source_cohort_order = []
    for identifier, cohorts, rationale in cohort_definitions:
        paths = sorted(
            {path for cohort in cohorts for path in sources_by_cohort.get(cohort, [])}
        )
        if not paths:
            continue
        source_cohort_order.append(
            {
                "rank": len(source_cohort_order) + 1,
                "id": identifier,
                "cohorts": list(cohorts),
                "source_count": len(paths),
                "sample_sources": paths[:12],
                "rationale": rationale,
            }
        )
    return {
        "capability_priorities": capability_priorities,
        "source_cohort_order": source_cohort_order,
    }


def _abi_inventory(root: Path, transforms: list[dict[str, object]]) -> dict[str, object] | None:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return None
    make_text = makefile.read_text(encoding="utf-8", errors="replace")
    if "-m32" not in make_text or (
        "elf_i386" not in make_text and "elf32-i386" not in make_text
    ):
        return None

    linker_script = root / "link.ld"
    linker_record = None
    if linker_script.is_file():
        text = linker_script.read_text(encoding="utf-8", errors="replace")
        features = []
        for token in ("ALIGN", "ASSERT", "COMMON", "ENTRY", "SECTIONS"):
            if re.search(rf"\b{token}\b", text):
                features.append(token)
        if re.search(r"\*\s*\(", text):
            features.append("input_section_wildcards")
        if re.search(r"(?m)^\s*\.\s*=", text):
            features.append("location_counter")
        if re.search(r"(?m)^\s*[A-Za-z_]\w*\s*=", text):
            features.append("symbol_definitions")
        output_sections = sorted(
            set(
                re.findall(
                    r"(?m)^\s*(\.[A-Za-z_][\w.]*)\s*(?:ALIGN\([^)]*\)\s*)?:",
                    text,
                )
            )
        )
        linker_inputs = {
            str(source)
            for transform in transforms
            if transform["operation"] == "link_elf32_executable"
            for source in transform["inputs"]
        }
        linker_record = {
            "path": "link.ld",
            "sha256": _source_digest(linker_script),
            "features": sorted(features),
            "output_sections": output_sections,
            "declared_make_prerequisite": "link.ld" in linker_inputs,
        }

    return {
        "architecture": "i386",
        "endianness": "little",
        "data_model": "ILP32",
        "plain_char": "signed",
        "calling_convention": "cdecl",
        "stack_alignment_bytes": 16 if "-mstackrealign" in make_text else 4,
        "frame_pointer_preserved": "-fno-omit-frame-pointer" in make_text,
        "object_interchange": "ELF32 ET_REL",
        "final_kernel_container": "ELF32 ET_EXEC",
        "required_relocations": ["R_386_32", "R_386_PC32"],
        "linker_script": linker_record,
        "referenced_by_link_flags": "link.ld" in make_text,
    }


def _provenance(
    root: Path,
    models: list[BuildModel],
    sources: list[dict[str, object]],
) -> dict[str, object]:
    generator = Path(__file__).resolve()
    control_files = []
    for model in models:
        relative = (
            "Makefile"
            if model.directory == "."
            else f"{model.directory}/Makefile"
        )
        path = root / relative
        control_files.append(
            {
                "path": relative,
                "sha256": _source_digest(path),
            }
        )
    aggregate = hashlib.sha256()
    for source in sources:
        aggregate.update(
            (
                f"{source['path']}\0{source['origin']}\0"
                f"{source['sha256'] or 'generated'}\n"
            ).encode("utf-8")
        )
    return {
        "generator": {
            "path": "tools/build_graph_audit.py",
            "sha256": _source_digest(generator),
        },
        "control_files": control_files,
        "active_source_digest": aggregate.hexdigest(),
        "text_hash_policy": "canonical_lf",
    }


def _is_excluded_source_path(path: str) -> bool:
    return any(part.lower() in EXCLUDED_SOURCE_TREES for part in Path(path).parts)


def _tracked_paths(root: Path) -> list[str] | None:
    probe = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--show-toplevel"],
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if probe.returncode != 0:
        return None
    try:
        git_root = Path(probe.stdout.strip()).resolve()
    except OSError:
        return None
    if git_root != root.resolve():
        return None
    listing = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        capture_output=True,
    )
    if listing.returncode != 0:
        detail = listing.stderr.decode("utf-8", errors="replace").strip()
        raise AuditError(f"git could not enumerate tracked sources: {detail}")
    return [
        item.decode("utf-8", errors="surrogateescape").replace("\\", "/")
        for item in listing.stdout.split(b"\0")
        if item
    ]


def _source_universe(root: Path) -> list[str]:
    candidates = _tracked_paths(root)
    if candidates is None:
        candidates = [
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file()
        ]
    return sorted(
        path
        for path in candidates
        if not _is_excluded_source_path(path)
        and _language(path) is not None
        and (root / path).is_file()
    )


def _explicitly_excluded_sources(root: Path) -> set[str]:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return set()
    text = makefile.read_text(encoding="utf-8", errors="replace")
    excluded: set[str] = set()
    for match in re.finditer(r"\$\(\s*filter-out\s+([^,\n]+),", text):
        for token in match.group(1).replace("\\", " ").split():
            normalized = token.replace("\\", "/")
            if "$" not in normalized and "%" not in normalized:
                if _language(normalized) is not None:
                    excluded.add(normalized)
    return excluded


def _unreachable_inventory(
    root: Path,
    active_sources: set[str],
) -> list[dict[str, object]]:
    universe = _source_universe(root)
    digests = {path: _source_digest(root / path) for path in universe}
    by_digest: dict[str, list[str]] = collections.defaultdict(list)
    for path, digest in digests.items():
        by_digest[digest].append(path)
    explicitly_excluded = _explicitly_excluded_sources(root)

    inventory = []
    for path in sorted(set(universe) - active_sources):
        duplicate_paths = sorted(
            candidate
            for candidate in by_digest[digests[path]]
            if candidate != path
        )
        relations = []
        known_relation = KNOWN_SOURCE_RELATIONS.get(path)
        if known_relation is not None and known_relation[1] in active_sources:
            relations.append(
                {
                    "kind": known_relation[0],
                    "path": known_relation[1],
                    "evidence": "audited project source relationship",
                }
            )
        relations.extend(
            {
                "kind": "exact_content_match",
                "path": candidate,
                "evidence": "canonical source SHA-256 equality",
            }
            for candidate in duplicate_paths
        )
        if path in explicitly_excluded:
            classification = "explicitly_excluded"
            reason = "listed in a Make filter-out expression"
        elif any(
            relation["kind"] == "historical_copy_of" for relation in relations
        ):
            classification = "historical_copy"
            reason = "diverged historical copy of an active implementation"
        elif any(relation["kind"] == "superseded_by" for relation in relations):
            classification = "superseded"
            reason = "replaced by the recorded active implementation"
        elif duplicate_paths:
            classification = "exact_duplicate"
            reason = "content SHA-256 matches another source-like file"
        else:
            classification = "not_reached"
            reason = "not reachable from the supported Make target or include closure"
        inventory.append(
            {
                "path": path,
                "language": _language(path),
                "classification": classification,
                "reason": reason,
                "duplicate_of": duplicate_paths,
                "relations": relations,
                "lines": len(
                    (root / path).read_text(
                        encoding="utf-8", errors="replace"
                    ).splitlines()
                ),
                "sha256": digests[path],
            }
        )
    return inventory


def _mask_c_noncode(text: str) -> str:
    """Mask comments and literals while retaining code positions and line numbers."""
    output: list[str] = []
    index = 0
    state = "code"
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            if char in {'"', "'"}:
                output.append(" ")
                index += 1
                state = "string" if char == '"' else "character"
                continue
            output.append(char)
            index += 1
            continue
        if state == "line_comment":
            output.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
            continue
        if state == "block_comment":
            if char == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
                continue
            output.append("\n" if char == "\n" else " ")
            index += 1
            continue
        if char == "\\" and following:
            output.append(" ")
            output.append("\n" if following == "\n" else " ")
            index += 2
            continue
        delimiter = '"' if state == "string" else "'"
        output.append("\n" if char == "\n" else " ")
        index += 1
        if char == delimiter:
            state = "code"
    return "".join(output)


def _add_regex_feature(
    collector: FeatureCollector,
    feature_id: str,
    path: str,
    line_number: int,
    original_line: str,
    code_line: str,
    pattern: str,
    flags: int = 0,
) -> None:
    occurrences = len(re.findall(pattern, code_line, flags=flags))
    collector.add(feature_id, path, line_number, original_line, occurrences)


def _scan_c_features(
    path: str,
    text: str,
    language: str,
    collector: FeatureCollector,
) -> None:
    masked = _mask_c_noncode(text)
    original_lines = text.splitlines()
    code_lines = masked.splitlines()
    for line_number, code_line in enumerate(code_lines, start=1):
        original_line = original_lines[line_number - 1]
        tokens = re.findall(r"\b[A-Za-z_]\w*\b", code_line)
        for token in sorted(set(tokens)):
            feature_id = C_KEYWORD_FEATURES.get(token)
            if feature_id is not None:
                collector.add(
                    feature_id,
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )
            if language == "cupid_c" and token in CUPID_TYPE_TOKENS:
                collector.add(
                    f"cupid_c.type.{CUPID_TYPE_TOKENS[token]}",
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )
            if language == "cupid_c" and token in CUPID_KEYWORD_FEATURES:
                collector.add(
                    CUPID_KEYWORD_FEATURES[token],
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )

        directive_match = re.match(r"\s*#\s*([A-Za-z_]\w*)", code_line)
        if directive_match:
            directive = directive_match.group(1).lower()
            feature_id = (
                f"c.preprocessor.{directive}"
                if directive in C_PREPROCESSOR_DIRECTIVES or language != "cupid_c"
                else f"cupid_c.directive.{directive}"
            )
            collector.add(
                feature_id, path, line_number, original_line
            )
        macro_match = re.match(
            r"\s*#\s*define\s+[A-Za-z_]\w*\(([^)]*)\)", code_line
        )
        if macro_match:
            collector.add(
                "c.preprocessor.function_macro", path, line_number, original_line
            )
            if "..." in macro_match.group(1):
                collector.add(
                    "c.preprocessor.variadic_macro",
                    path,
                    line_number,
                    original_line,
                )
        define_match = re.match(
            r"\s*#\s*define\s+[A-Za-z_]\w*(?:\([^)]*\))?\s*(.*)$",
            code_line,
        )
        if define_match:
            replacement = define_match.group(1)
            collector.add(
                "c.preprocessor.token_paste",
                path,
                line_number,
                original_line,
                replacement.count("##"),
            )
            stringify_count = len(
                re.findall(r"(?<!#)#(?!#)\s*[A-Za-z_]\w*", replacement)
            )
            collector.add(
                "c.preprocessor.stringify",
                path,
                line_number,
                original_line,
                stringify_count,
            )
            collector.add(
                "c.preprocessor.gnu_variadic_comma_elision",
                path,
                line_number,
                original_line,
                len(re.findall(r",\s*##\s*__VA_ARGS__\b", replacement)),
            )
        if re.match(r"\s*#\s*pragma\s+pack\b", code_line):
            collector.add(
                "c.preprocessor.pragma.pack", path, line_number, original_line
            )

        _add_regex_feature(
            collector,
            "c.declarator.function_pointer",
            path,
            line_number,
            original_line,
            code_line,
            r"\(\s*\*\s*[A-Za-z_]\w*\s*\)\s*\(",
        )
        _add_regex_feature(
            collector,
            "c.declarator.unsized_array",
            path,
            line_number,
            original_line,
            code_line,
            r"\b[A-Za-z_]\w*\s*\[\s*\]\s*(?:[;,=])",
        )
        _add_regex_feature(
            collector,
            "c.declarator.variadic",
            path,
            line_number,
            original_line,
            code_line,
            r"\.\.\.",
        )
        _add_regex_feature(
            collector,
            "c.type.long_long",
            path,
            line_number,
            original_line,
            code_line,
            r"\blong\s+long\b",
        )
        bit_field_count = int(
            re.fullmatch(
                r"\s*(?!(?:case|default)\b)"
                r"(?:(?:const|volatile|signed|unsigned|short|long|_Atomic)\s+)*"
                r"(?:(?:struct|union|enum)\s+[A-Za-z_]\w*|[A-Za-z_]\w*)"
                r"(?:\s+|\s*\*+\s*)(?:[A-Za-z_]\w*)?\s*"
                r":\s*(?:\d+|[A-Za-z_]\w*)\s*;\s*",
                code_line,
            )
            is not None
        )
        collector.add(
            "c.declarator.bit_field",
            path,
            line_number,
            original_line,
            bit_field_count,
        )
        designated_count = len(
            re.findall(
                r"(?:^|[{,])\s*(?:\.[A-Za-z_]\w*|\[[^\]]+\])\s*=",
                code_line,
            )
        )
        collector.add(
            "c.initializer.designated",
            path,
            line_number,
            original_line,
            designated_count,
        )
        compound_count = 0
        for match in re.finditer(
            r"\(\s*(?:(?:struct|union)\s+)?[A-Za-z_]\w*(?:\s*\*)?\s*\)\s*\{",
            code_line,
        ):
            prefix = code_line[:match.start()].rstrip()
            if prefix.endswith((")", "]")):
                continue
            if prefix and (prefix[-1].isalnum() or prefix[-1] == "_"):
                preceding = re.search(r"([A-Za-z_]\w*)$", prefix)
                if preceding is None or preceding.group(1) != "return":
                    continue
            compound_count += 1
        collector.add(
            "c.expression.compound_literal",
            path,
            line_number,
            original_line,
            compound_count,
        )
        _add_regex_feature(
            collector,
            "c.extension.inline_assembly",
            path,
            line_number,
            original_line,
            code_line,
            r"\b(?:asm|__asm|__asm__)\b",
        )
        if language == "cupid_c":
            _add_regex_feature(
                collector,
                "cupid_c.extension.asm_block",
                path,
                line_number,
                original_line,
                code_line,
                r"\basm\s*\{",
            )
        _add_regex_feature(
            collector,
            "c.extension.statement_expression",
            path,
            line_number,
            original_line,
            code_line,
            r"\(\s*\{",
        )
        _add_regex_feature(
            collector,
            "c.extension.typeof",
            path,
            line_number,
            original_line,
            code_line,
            r"\b(?:typeof|__typeof|__typeof__)\b",
        )
        for builtin in sorted(set(re.findall(r"\b(__builtin_[A-Za-z_]\w*)\b", code_line))):
            collector.add(
                f"c.extension.builtin.{builtin.removeprefix('__builtin_').lower()}",
                path,
                line_number,
                original_line,
                code_line.count(builtin),
            )
        for attribute_match in re.finditer(
            r"\b__attribute__\s*\(\((.*?)\)\)", code_line
        ):
            names = re.findall(r"\b([A-Za-z_]\w*)\b", attribute_match.group(1))
            for name in sorted(set(names)):
                collector.add(
                    f"c.extension.attribute.{name.strip('_').lower()}",
                    path,
                    line_number,
                    original_line,
                    names.count(name),
                )


def _mask_asm_strings(line: str) -> str:
    """Replace quoted ASM data with spaces while preserving source positions."""
    output = list(line)
    quote: str | None = None
    escaped = False
    for index, char in enumerate(line):
        if quote is None:
            if char in {'"', "'"}:
                quote = char
                output[index] = " "
            continue

        output[index] = " "
        if escaped:
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == quote:
            quote = None
    return "".join(output)


def _strip_asm_comment(line: str) -> str:
    comment_index = _mask_asm_strings(line).find(";")
    return line if comment_index < 0 else line[:comment_index]


def _asm_bracketed_directive(line: str) -> str | None:
    match = re.fullmatch(
        r"\[\s*(bits|org)\b[^\]]*\]\s*", line, flags=re.IGNORECASE
    )
    return match.group(1).lower() if match is not None else None


def _scan_asm_features(path: str, text: str, collector: FeatureCollector) -> None:
    extern_symbols: set[str] = set()
    for raw_line in text.splitlines():
        declaration = re.match(
            r"^\s*extern\s+(.+)$",
            _strip_asm_comment(raw_line),
            flags=re.IGNORECASE,
        )
        if declaration:
            extern_symbols.update(
                re.findall(r"[A-Za-z_.$?][\w.$@?]*", declaration.group(1))
            )

    for line_number, original_line in enumerate(text.splitlines(), start=1):
        code_line = _strip_asm_comment(original_line).strip()
        if not code_line:
            continue

        label_match = re.match(r"^([.$?A-Za-z_][\w.$@?]*):", code_line)
        if label_match:
            label = label_match.group(1)
            collector.add(
                "asm.label.local" if label.startswith(".") else "asm.label.global",
                path,
                line_number,
                original_line,
            )
            code_line = code_line[label_match.end():].lstrip()
            if not code_line:
                continue

        bracketed_directive = _asm_bracketed_directive(code_line)
        scan_line = (
            bracketed_directive if bracketed_directive is not None else code_line
        )
        first_match = re.match(r"([^\s,]+)", scan_line)
        if first_match is None:
            continue
        first = first_match.group(1).lower()
        mnemonic: str | None = None
        if first.startswith("%"):
            collector.add(
                f"asm.preprocessor.{first[1:]}", path, line_number, original_line
            )
        else:
            tokens = re.findall(r"[A-Za-z_][\w.$@?]*", scan_line.lower())
            directive = None
            if first in ASM_DIRECTIVES:
                directive = first
            elif len(tokens) >= 2 and tokens[1] in ASM_DIRECTIVES:
                directive = tokens[1]
            if directive is not None:
                collector.add(
                    f"asm.directive.{directive}", path, line_number, original_line
                )
            if directive is None:
                if first in ASM_PREFIXES:
                    collector.add(
                        f"asm.prefix.{first}", path, line_number, original_line
                    )
                instruction_index = 1 if first in ASM_PREFIXES else 0
                if instruction_index < len(tokens):
                    mnemonic = tokens[instruction_index]
                    if mnemonic not in ASM_DIRECTIVES:
                        collector.add(
                            f"asm.instruction.{mnemonic}",
                            path,
                            line_number,
                            original_line,
                        )
            if first == "times":
                for data_directive in ("db", "dw", "dd", "dq", "dt"):
                    if re.search(rf"\b{data_directive}\b", code_line, re.IGNORECASE):
                        collector.add(
                            f"asm.directive.{data_directive}",
                            path,
                            line_number,
                            original_line,
                        )

        words = re.findall(r"\b[A-Za-z_][A-Za-z_0-9]*\b", code_line.lower())
        for register in sorted(set(words) & ASM_REGISTERS):
            collector.add(
                f"asm.register.{register}",
                path,
                line_number,
                original_line,
                words.count(register),
            )
        memory_line = _mask_asm_strings(code_line)
        memory_operands = re.findall(r"\[[^\[\]]*\]", memory_line)
        if bracketed_directive is None and memory_operands:
            collector.add("asm.addressing.memory", path, line_number, original_line)
            if any(
                re.search(r"\*\s*(?:2|4|8)\b", operand)
                for operand in memory_operands
            ):
                collector.add(
                    "asm.addressing.base_index_scale",
                    path,
                    line_number,
                    original_line,
                )
        if re.search(r"\b(?:cs|ds|es|fs|gs|ss)\s*:", code_line, re.IGNORECASE):
            collector.add(
                "asm.addressing.segment_override", path, line_number, original_line
            )
        for size in ("byte", "word", "dword", "qword", "tword"):
            if re.search(rf"\b{size}\s*(?:ptr\s*)?\[", code_line, re.IGNORECASE):
                collector.add(
                    f"asm.addressing.size.{size}", path, line_number, original_line
                )
        if "$$" in code_line:
            collector.add(
                "asm.expression.section_origin", path, line_number, original_line
            )
        elif "$" in code_line:
            collector.add(
                "asm.expression.current_offset", path, line_number, original_line
            )
        if first != "extern":
            referenced_externals = [
                symbol
                for symbol in extern_symbols
                if re.search(rf"(?<![\w.$@?]){re.escape(symbol)}(?![\w.$@?])", code_line)
            ]
            if referenced_externals:
                relocation = (
                    "pc_relative_external"
                    if mnemonic is not None
                    and (mnemonic == "call" or mnemonic == "jmp" or mnemonic.startswith("j"))
                    else "absolute_external"
                )
                collector.add(
                    f"asm.relocation.{relocation}",
                    path,
                    line_number,
                    original_line,
                    len(referenced_externals),
                )


def _scan_source_features(
    path: str,
    source_path: Path,
    language: str,
    collector: FeatureCollector,
) -> None:
    text = source_path.read_text(encoding="utf-8", errors="replace")
    if language in {"c", "c_header", "cupid_c"}:
        _scan_c_features(path, text, language, collector)
    elif language == "assembly":
        _scan_asm_features(path, text, collector)


def _scan_build_features(
    transforms: list[dict[str, object]],
    collector: FeatureCollector,
) -> None:
    for transform in transforms:
        operation = str(transform["operation"])
        for source in transform["inputs"]:
            path = str(source)
            language = _language(path)
            feature_id = None
            if language == "c" and operation == "compile_c_to_elf32_object":
                feature_id = "c.output.elf32_relocatable"
            elif language == "assembly" and operation == "assemble_flat_binary":
                feature_id = "asm.output.flat_binary"
            elif language == "assembly" and operation == "assemble_elf32_relocatable":
                feature_id = "asm.output.elf32_relocatable"
            elif language == "assembly" and operation == "wrap_binary_as_elf32_relocatable":
                feature_id = "asm.delivery.embedded_source"
            elif language == "cupid_c" and operation == "wrap_binary_as_elf32_relocatable":
                feature_id = "cupid_c.delivery.embedded_source"
            elif language == "c_header" and operation == "wrap_binary_as_elf32_relocatable":
                feature_id = "cupid_c.delivery.embedded_header"
            if feature_id is not None:
                collector.add(
                    feature_id,
                    path,
                    1,
                    f"{operation} -> {transform['output']}",
                )


def build_audit(
    root: Path,
    make: str,
    target: str,
    supplemental_builds: list[tuple[str, str]] | None = None,
) -> dict[str, object]:
    root = root.resolve()
    root_model = _collect_build_model(root, make, target, ".")
    supplemental_models = [
        _collect_build_model(root, make, supplemental_target, directory)
        for directory, supplemental_target in (supplemental_builds or [])
    ]
    models = [root_model, *supplemental_models]

    direct_sources = set().union(*(model.direct_sources for model in models))
    generated_sources = set().union(*(model.generated_sources for model in models))
    forced_sources = set().union(*(model.forced_sources for model in models))
    includes_by_source: dict[str, list[str]] = {}
    build_memberships: dict[str, set[str]] = collections.defaultdict(set)
    for model in models:
        model_sources = set(model.direct_sources | model.forced_sources)
        model_sources.update(model.includes_by_source)
        for included in model.includes_by_source.values():
            model_sources.update(included)
        for source, includes in model.includes_by_source.items():
            includes_by_source[source] = sorted(
                set(includes_by_source.get(source, [])) | set(includes)
            )
        for source in model_sources:
            build_memberships[source].add(model.directory)

    all_sources = set(direct_sources | forced_sources)
    all_sources.update(includes_by_source)
    for included in includes_by_source.values():
        all_sources.update(included)

    all_transforms = [
        transform for model in models for transform in model.transforms
    ]
    source_build_owners: dict[str, set[str]] = collections.defaultdict(set)
    for transform in all_transforms:
        for source in transform["inputs"]:
            if source in all_sources:
                source_build_owners[source].update(transform["tools"])

    feature_collector = FeatureCollector()
    for relative in sorted(all_sources):
        language = _language(relative)
        if language is not None and relative not in generated_sources:
            _scan_source_features(
                relative,
                root / relative,
                language,
                feature_collector,
            )
    _scan_build_features(all_transforms, feature_collector)

    sources = []
    for relative in sorted(all_sources):
        path = root / relative
        generated = relative in generated_sources
        language = _language(relative)
        owners = sorted(source_build_owners.get(relative, set()))
        runtime_owner = None
        if language == "cupid_c":
            runtime_owner = "CupidC"
        elif (
            language == "assembly"
            and (
                "cupid_assembler" in owners
                or (
                    {"host_object_copy", "cupid_object"}.intersection(owners)
                    and "nasm" not in owners
                )
            )
        ):
            runtime_owner = "CupidASM"
        elif language == "c_header" and {
            "host_object_copy",
            "cupid_object",
        }.intersection(owners):
            runtime_owner = "CupidC"
        sources.append(
            {
                "path": relative,
                "language": language,
                "origin": "generated" if generated else "tracked",
                "cohort": _source_cohort(relative, language, generated),
                "reachability": (
                    "direct_build_input"
                    if relative in direct_sources
                    else "forced_include"
                    if relative in forced_sources
                    else "transitive_include"
                ),
                "builds": sorted(build_memberships.get(relative, set())),
                "build_owners": owners,
                "runtime_owner": runtime_owner,
                "includes": includes_by_source.get(relative, []),
                "features": feature_collector.for_source(relative),
                "lines": (
                    None
                    if generated
                    else len(path.read_text(encoding="utf-8", errors="replace").splitlines())
                ),
                "sha256": None if generated else _source_digest(path),
            }
        )

    unreachable_sources = _unreachable_inventory(root, all_sources)
    contracts: dict[str, object] = {}
    artifact_contract = _artifact_coverage_contract(
        root,
        make,
        root_model.rules,
        root_model.transforms,
    )
    if artifact_contract is not None:
        contracts["bootstrap_artifact_coverage"] = artifact_contract
    feature_inventory = feature_collector.inventory()
    roadmap = _roadmap(sources, feature_inventory)
    abi = _abi_inventory(root, all_transforms)
    provenance = _provenance(root, models, sources)

    return {
        "schema": SCHEMA,
        "abi": abi,
        "provenance": provenance,
        "build": {
            "directory": root_model.directory,
            "root_target": target,
            "include_search_paths": root_model.include_search_paths,
            "forced_includes": sorted(root_model.forced_sources),
            "transforms": root_model.transforms,
        },
        "supplemental_builds": [
            {
                "directory": model.directory,
                "root_target": model.root_target,
                "include_search_paths": model.include_search_paths,
                "forced_includes": sorted(model.forced_sources),
                "transforms": model.transforms,
            }
            for model in supplemental_models
        ],
        "contracts": contracts,
        "features": feature_inventory,
        "roadmap": roadmap,
        "sources": sources,
        "unreachable_sources": unreachable_sources,
        "summary": {
            "active_sources": len(sources),
            "unreachable_sources": len(unreachable_sources),
            "features": len(feature_inventory),
            "transforms": len(all_transforms),
        },
    }


def _json_payload(audit: dict[str, object]) -> str:
    return json.dumps(audit, indent=2, sort_keys=True) + "\n"


def _markdown_cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def _render_markdown(audit: dict[str, object]) -> str:
    source_counts = collections.Counter(
        str(source["language"]) for source in audit["sources"]
    )
    cohort_counts = collections.Counter(
        str(source["cohort"]) for source in audit["sources"]
    )
    cohort_lines: collections.Counter[str] = collections.Counter()
    for source in audit["sources"]:
        if source["lines"] is not None:
            cohort_lines[str(source["cohort"])] += int(source["lines"])
    unreachable_counts = collections.Counter(
        str(source["classification"])
        for source in audit["unreachable_sources"]
    )
    tool_counts: collections.Counter[str] = collections.Counter()
    all_builds = [audit["build"], *audit["supplemental_builds"]]
    all_transforms = [
        transform for build in all_builds for transform in build["transforms"]
    ]
    for transform in all_transforms:
        tool_counts.update(str(tool) for tool in transform["tools"])
    feature_counts: collections.Counter[str] = collections.Counter()
    feature_occurrences: collections.Counter[str] = collections.Counter()
    for feature in audit["features"]:
        parts = str(feature["id"]).split(".")
        group = ".".join(parts[:2]) if len(parts) > 1 else parts[0]
        feature_counts[group] += 1
        feature_occurrences[group] += int(feature["occurrences"])

    lines = [
        "# Active build and source audit",
        "",
        "This file is generated deterministically by "
        "`tools/build_graph_audit.py` from the supported Make graph and source tree.",
        "",
        "## Scope",
        "",
        f"- Root Make target: `{audit['build']['root_target']}`",
        "- Supplemental builds: "
        + (
            ", ".join(
                f"`{build['directory']}:{build['root_target']}`"
                for build in audit["supplemental_builds"]
            )
            or "none"
        ),
        f"- Active source inputs: {audit['summary']['active_sources']}",
        f"- Unreachable source-like files: {audit['summary']['unreachable_sources']}",
        f"- Reachable output transforms: {audit['summary']['transforms']}",
        f"- Distinct feature requirements: {audit['summary']['features']}",
        "- The `TempleOS/` reference tree is excluded.",
        "- Source and control-file SHA-256 values use canonical LF text bytes.",
        "",
        "Generated C translation units are recorded as reachable build inputs but have "
        "no source hash or lexical features; their content is owned by the recorded "
        "generator transform.",
        "",
        "## Active language inputs",
        "",
        "| Language | Files |",
        "| --- | ---: |",
    ]
    lines.extend(
        f"| `{_markdown_cell(language)}` | {count} |"
        for language, count in sorted(source_counts.items())
    )
    lines.extend(
        [
            "",
            "## Source cohorts",
            "",
            "| Cohort | Files | Checked-source lines |",
            "| --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(cohort)}` | {count} | {cohort_lines[cohort]} |"
        for cohort, count in sorted(cohort_counts.items())
    )
    lines.extend(
        [
            "",
            "## Supported build roots",
            "",
            "| Directory | Root target | Transforms | Include paths |",
            "| --- | --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{build['directory']}` | `{build['root_target']}` | "
        f"{len(build['transforms'])} | {len(build['include_search_paths'])} |"
        for build in all_builds
    )
    lines.extend(
        [
            "",
            "## Current output ownership",
            "",
            "| Tool interface | Reachable transforms |",
            "| --- | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(tool)}` | {count} |"
        for tool, count in sorted(tool_counts.items())
    )
    lines.extend(
        [
            "",
            "## Feature inventory",
            "",
            "| Feature family | Distinct requirements | Lexical/build occurrences |",
            "| --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(group)}` | {count} | {feature_occurrences[group]} |"
        for group, count in sorted(feature_counts.items())
    )
    lines.extend(
        [
            "",
            "The JSON companion records stable feature IDs, occurrence counts, files, "
            "and representative source locations.",
            "",
        ]
    )
    if audit["abi"] is not None:
        abi = audit["abi"]
        linker = abi["linker_script"]
        lines.extend(
            [
                "## ABI and object contract",
                "",
                "| Property | Required value |",
                "| --- | --- |",
                f"| Architecture | `{abi['architecture']}` |",
                f"| Data model | `{abi['data_model']}` |",
                f"| Endianness | `{abi['endianness']}` |",
                f"| Calling convention | `{abi['calling_convention']}` |",
                f"| Object interchange | `{abi['object_interchange']}` |",
                f"| Required relocations | `{', '.join(abi['required_relocations'])}` |",
                f"| Stack alignment | {abi['stack_alignment_bytes']} bytes |",
                "",
            ]
        )
        if linker is not None:
            lines.extend(
                [
                    f"`{linker['path']}` has SHA-256 `{linker['sha256']}` and uses "
                    f"{', '.join(f'`{feature}`' for feature in linker['features'])}.",
                    "It is referenced by linker flags but is not a declared Make "
                    "prerequisite." if not linker["declared_make_prerequisite"] else
                    "It is also a declared Make prerequisite.",
                    "",
                ]
            )
    lines.extend(
        [
            "## Source-driven capability priority",
            "",
            "| Rank | Capability | Source evidence |",
            "| ---: | --- | ---: |",
        ]
    )
    lines.extend(
        f"| {item['rank']} | `{item['id']}` - {_markdown_cell(item['title'])} | "
        f"{item['source_count']} |"
        for item in audit["roadmap"]["capability_priorities"]
    )
    lines.extend(
        [
            "",
            "## Source-cohort migration order",
            "",
            "| Rank | Cohort step | Files | Rationale |",
            "| ---: | --- | ---: | --- |",
        ]
    )
    lines.extend(
        f"| {item['rank']} | `{item['id']}` | {item['source_count']} | "
        f"{_markdown_cell(item['rationale'])} |"
        for item in audit["roadmap"]["source_cohort_order"]
    )
    lines.extend(
        [
            "",
            "## Unreachable source classification",
            "",
            "| Classification | Files |",
            "| --- | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(classification)}` | {count} |"
        for classification, count in sorted(unreachable_counts.items())
    )
    lines.extend(
        [
            "",
            "An exact content match does not by itself prove semantic duplication; "
            "path-sensitive compatibility headers remain removal-blocked.",
            "",
            "| Path | Language | Classification | Lines | Evidence |",
            "| --- | --- | --- | ---: | --- |",
        ]
    )
    for source in audit["unreachable_sources"]:
        duplicates = source.get("duplicate_of", [])
        semantic_relations = [
            relation
            for relation in source.get("relations", [])
            if relation["kind"] != "exact_content_match"
        ]
        if semantic_relations:
            evidence = ", ".join(
                f"{relation['kind']}: `{relation['path']}`"
                for relation in semantic_relations
            )
        elif duplicates:
            shown = ", ".join(f"`{path}`" for path in duplicates[:3])
            if len(duplicates) > 3:
                shown += f" (+{len(duplicates) - 3} more)"
            evidence = f"content match: {shown}"
        else:
            evidence = str(source["reason"])
        lines.append(
            f"| `{source['path']}` | `{source['language']}` | "
            f"`{source['classification']}` | {source['lines']} | "
            f"{_markdown_cell(evidence)} |"
        )
    lines.extend(
        [
            "",
            "## Audit contracts",
            "",
            "| Contract | Status | Detail |",
            "| --- | --- | --- |",
        ]
    )
    if audit["contracts"]:
        for name, contract in sorted(audit["contracts"].items()):
            missing = contract.get("missing_link_inputs", [])
            detail = (
                f"{contract.get('linked_objects', 0)} linked objects; "
                f"{contract.get('declared_artifacts', 0)} declared artifacts; "
                f"{len(missing)} missing"
            )
            lines.append(
                f"| `{_markdown_cell(name)}` | `{contract['status']}` | "
                f"{_markdown_cell(detail)} |"
            )
    else:
        lines.append("| _none declared_ | `not_applicable` | n/a |")
    lines.extend(
        [
            "",
            "## Interpretation limits",
            "",
            "- Feature occurrences are comment/string-masked lexical evidence, not a "
            "substitute for a compiler AST or executed semantic tests.",
            "- Include reachability follows checked Make include paths, forced includes, "
            "quoted/angle C includes, and `%include`; condition evaluation remains a "
            "future compiler-owned manifest capability.",
            "- Relocation kinds and ABI values are required interchange contracts; "
            "per-object relocation counts are recorded in the chronological bootstrap log.",
            "- `not_reached` means absent from the supported roots recorded above, not "
            "automatically safe to delete.",
        ]
    )
    return "\n".join(lines) + "\n"


def _write_text_atomic(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="\n",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as stream:
        temporary = Path(stream.name)
        stream.write(payload)
    temporary.replace(path)


def _check_text(path: Path, expected: str) -> bool:
    try:
        actual = path.read_text(encoding="utf-8")
    except OSError:
        return False
    return actual == expected


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--make", default="make", help="GNU Make executable")
    parser.add_argument("--target", default="all", help="supported build root")
    parser.add_argument(
        "--supplemental-build",
        action="append",
        default=[],
        metavar="DIRECTORY:TARGET",
        help="additional supported Make root (repeatable)",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, help="generated Markdown summary")
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify checked outputs and passing contracts without writing",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    try:
        supplemental_builds = []
        for specification in args.supplemental_build:
            if ":" not in specification:
                raise AuditError(
                    "supplemental build must use DIRECTORY:TARGET syntax: "
                    f"{specification!r}"
                )
            directory, supplemental_target = specification.rsplit(":", 1)
            if not directory or not supplemental_target:
                raise AuditError(
                    "supplemental build must use DIRECTORY:TARGET syntax: "
                    f"{specification!r}"
                )
            supplemental_builds.append((directory, supplemental_target))
        audit = build_audit(
            args.root,
            args.make,
            args.target,
            supplemental_builds,
        )
        json_payload = _json_payload(audit)
        markdown_payload = _render_markdown(audit) if args.summary else None
    except AuditError as exc:
        print(f"build graph audit failed: {exc}", file=sys.stderr)
        return 2

    if args.check:
        stale = []
        if not _check_text(args.output, json_payload):
            stale.append(args.output)
        if args.summary and not _check_text(args.summary, markdown_payload or ""):
            stale.append(args.summary)
        for path in stale:
            print(f"build graph audit out of date: {path.name}", file=sys.stderr)
        failed_contracts = [
            name
            for name, contract in audit["contracts"].items()
            if contract.get("status") != "pass"
        ]
        for name in failed_contracts:
            print(f"build graph audit contract failed: {name}", file=sys.stderr)
        return 1 if stale or failed_contracts else 0

    _write_text_atomic(args.output, json_payload)
    if args.summary and markdown_payload is not None:
        _write_text_atomic(args.summary, markdown_payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

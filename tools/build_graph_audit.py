"""Produce a deterministic inventory of Cupid OS build and language inputs."""

from __future__ import annotations

import argparse
import bisect
import collections
import hashlib
import json
import posixpath
import re
import shlex
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
    ("$(CUPIDDIS)", "cupid_disassembler"),
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


@dataclass(frozen=True)
class CIncludeDirective:
    """One source-written C include operand after phases two and three."""

    line: int
    marker: str
    raw: str
    normalized: str
    kind: str
    spelling: str | None
    conditional_stack: tuple[str, ...]


@dataclass(frozen=True)
class CPreprocessorProfile:
    """Named policy for one exact active preprocessing cohort."""

    name: str
    mode: str
    gnu_extensions: str
    hosted_environment: str


@dataclass(frozen=True)
class CPreprocessorActiveCasesManifest:
    """Checked X-macro inputs for active CupidC preprocessing jobs."""

    profiles: tuple[CPreprocessorProfile, ...]
    include_roots: tuple[tuple[str, str, str], ...]
    macros: tuple[tuple[str, str, str], ...]
    forced_includes: tuple[tuple[str, str], ...]
    active_cases: tuple[tuple[str, str], ...]
    generated_cases: tuple[tuple[str, str], ...]
    include_only: tuple[tuple[str, str], ...]
    non_roots: tuple[tuple[str, str], ...]
    deferred_hosted: tuple[tuple[str, str], ...]


_C_PP_INCLUDE_BOTH = (
    "(CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE)"
)
_C_PP_PROFILE_ROWS = (
    CPreprocessorProfile(
        name="KERNEL_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="DOOM_COMPAT_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="DOOM_TREE_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="USER_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="CUPID_RUNTIME",
        mode="CTOOL_C_PP_MODE_CUPID",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_TOOLCHAIN_64",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="HOSTED_KERNEL_BRIDGE_64",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_LINUX",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_LINUX_GNU",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_TRUE",
    ),
)
_C_PP_HOSTED_PROFILES = frozenset(
    profile.name
    for profile in _C_PP_PROFILE_ROWS
    if profile.hosted_environment == "CTOOL_TRUE"
)
_C_PP_KERNEL_INCLUDE_ROOTS = (
    "/kernel",
    "/kernel/audio",
    "/kernel/core",
    "/kernel/cpu",
    "/kernel/crypto",
    "/kernel/doom",
    "/kernel/fs",
    "/kernel/gfx",
    "/kernel/gui",
    "/kernel/lang",
    "/kernel/mm",
    "/kernel/network",
    "/kernel/smp",
    "/kernel/tls",
    "/kernel/usb",
    "/kernel/util",
    "/drivers",
    "/toolchain",
)
_C_PP_DOOM_EXTRA_INCLUDE_ROOTS = (
    "/kernel/doom/src",
    "/kernel/doom/src/include_stubs",
)
_C_PP_COMMON_I386_MACROS = (
    ("__GNUC__", "1"),
    ("__SIZEOF_POINTER__", "4"),
    ("__ORDER_LITTLE_ENDIAN__", "1234"),
    ("__ORDER_BIG_ENDIAN__", "4321"),
    ("__ORDER_PDP_ENDIAN__", "3412"),
    ("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__"),
)
_C_PP_ACTIVE_COUNTS = {
    "KERNEL_I386": 152,
    "DOOM_COMPAT_I386": 6,
    "DOOM_TREE_I386": 80,
    "USER_I386": 3,
    "CUPID_RUNTIME": 105,
    "HOSTED_TOOLCHAIN_64": 12,
    "HOSTED_KERNEL_BRIDGE_64": 1,
    "HOSTED_I386_LINUX": 19,
    "HOSTED_I386_LINUX_GNU": 1,
}
_C_PP_HOSTED_I386_STRICT_CASES = (
    "/toolchain/ctool.c",
    "/toolchain/ctool_host.c",
    "/toolchain/cupidasm.c",
    "/toolchain/cupidasm_main.c",
    "/toolchain/cupidc_emit.c",
    "/toolchain/cupidc_frontend.c",
    "/toolchain/cupidc_ir.c",
    "/toolchain/cupidc_main.c",
    "/toolchain/cupidc_pp.c",
    "/toolchain/cupidc_type.c",
    "/toolchain/cupiddis.c",
    "/toolchain/cupiddis_main.c",
    "/toolchain/cupidld.c",
    "/toolchain/cupidld_main.c",
    "/toolchain/cupidobj.c",
    "/toolchain/cupidobj_main.c",
    "/toolchain/elf32.c",
    "/toolchain/tests/hosted_i386_runtime_contract.c",
    "/toolchain/x86.c",
)
_C_PP_HOSTED_I386_GNU_CASES = (
    "/toolchain/hosted/i386-linux/runtime.c",
)
_C_PP_GENERATED_KERNEL_CASES = (
    "/kernel/cpu/ksyms_data.c",
    "/kernel/util/bin_programs_gen.c",
    "/kernel/util/demos_programs_gen.c",
    "/kernel/util/docs_programs_gen.c",
)
_C_PP_NON_ROOT_HEADERS = (
    "/bin/fat16.h",
    "/bin/shell.h",
)
_C_PP_DEFERRED_HOSTED_CASES = (
    "/toolchain/ctool_host.c",
    "/toolchain/cupidasm_main.c",
    "/toolchain/cupiddis_main.c",
    "/toolchain/cupidc_main.c",
    "/toolchain/cupidld_main.c",
    "/toolchain/cupidobj_main.c",
    "/toolchain/tests/core_contract.c",
    "/toolchain/tests/cupidasm_contract.c",
    "/toolchain/tests/cupidasm_demos_contract.c",
    "/toolchain/tests/cupidasm_kernel_elf_contract.c",
    "/toolchain/tests/cupidc_frontend_contract.c",
    "/toolchain/tests/cupidc_ir_contract.c",
    "/toolchain/tests/cupidc_object_contract.c",
    "/toolchain/tests/cupidc_pp_contract.c",
    "/toolchain/tests/cupidc_type_contract.c",
    "/toolchain/tests/cupiddis_contract.c",
    "/toolchain/tests/cupidld_contract.c",
    "/toolchain/tests/cupidobj_contract.c",
    "/toolchain/tests/elf32_contract.c",
    "/toolchain/tests/x86_contract.c",
)
_C_PP_HOSTED_BRIDGE_CASES = frozenset(
    {
        "/kernel/lang/as_elf.c",
        "/toolchain/tests/cupidasm_kernel_elf_contract.c",
    }
)


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

GNU_C_OPERATOR_FEATURES = {
    "__alignof": "c.extension.gnu_alignof",
    "__alignof__": "c.extension.gnu_alignof",
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


def _c_include_directives(
    text: str, display_path: str
) -> list[CIncludeDirective]:
    raw_logical_lines = _c_raw_logical_lines(text)
    if not raw_logical_lines:
        return []
    logical_text = "\n".join(line for _, _, line in raw_logical_lines)
    source_lines = _mask_c_comments_preserve_literals(logical_text).split("\n")
    if len(source_lines) != len(raw_logical_lines):
        raise AuditError("C masking changed the logical line count")

    directives: list[CIncludeDirective] = []
    conditional_stack: list[str] = []
    conditional_pattern = re.compile(
        r"^\s*(#|%:)\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$"
    )
    include_pattern = re.compile(r"^\s*(#|%:)\s*include\b(.*)$")
    for (line_number, _original_line, raw_line), source_line in zip(
        raw_logical_lines, source_lines, strict=True
    ):
        conditional_match = conditional_pattern.match(source_line)
        if conditional_match is not None:
            marker, directive, remainder = conditional_match.groups()
            operand = remainder.strip()
            if directive in {"if", "ifdef", "ifndef", "elif"} and operand:
                normalized = " ".join(
                    _normalize_c_preprocessing_tokens(
                        operand, display_path, line_number
                    )
                )
                evidence = f"{marker}{directive} {normalized} at line {line_number}"
            else:
                evidence = f"{marker}{directive} at line {line_number}"
            if directive in {"if", "ifdef", "ifndef"}:
                conditional_stack.append(evidence)
            elif directive in {"elif", "else"} and conditional_stack:
                conditional_stack[-1] = evidence
            elif directive == "endif" and conditional_stack:
                conditional_stack.pop()
            continue

        include_match = include_pattern.match(source_line)
        if include_match is None:
            continue
        marker, remainder = include_match.groups()
        operand = remainder.strip()
        quoted_match = re.fullmatch(r'"([^"\r\n]+)"\s*', operand)
        angle_match = re.fullmatch(r"<([^>\r\n]+)>\s*", operand)
        if quoted_match is not None:
            kind = "quoted"
            spelling: str | None = quoted_match.group(1)
            normalized_operand = f'"{spelling}"'
        elif angle_match is not None:
            kind = "angle"
            spelling = angle_match.group(1)
            normalized_operand = f"<{spelling}>"
        else:
            kind = "pp_tokens"
            spelling = None
            tokens = (
                _normalize_c_preprocessing_tokens(
                    operand, display_path, line_number
                )
                if operand
                else ()
            )
            normalized_operand = " ".join(tokens) if tokens else "<empty>"
        directives.append(
            CIncludeDirective(
                line=line_number,
                marker=marker,
                raw=raw_line.strip()[:160],
                normalized=normalized_operand,
                kind=kind,
                spelling=spelling,
                conditional_stack=tuple(conditional_stack),
            )
        )
    return directives


def _reject_pp_token_include(
    display_path: str, directive: CIncludeDirective
) -> None:
    conditional = (
        " > ".join(directive.conditional_stack)
        if directive.conditional_stack
        else "<unconditional>"
    )
    raise AuditError(
        f"{display_path}:{directive.line}: macro-expanded #include operand "
        "cannot be represented by the deterministic include closure; "
        f"marker={directive.marker!r}; raw={directive.raw!r}; "
        f"normalized={directive.normalized!r}; conditional={conditional!r}"
    )


def _declared_includes(
    path: Path, language: str, display_path: str | None = None
) -> list[tuple[str, str]]:
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
    source_name = display_path or path.as_posix()
    includes: list[tuple[str, str]] = []
    for directive in _c_include_directives(text, source_name):
        if directive.kind == "pp_tokens":
            _reject_pp_token_include(source_name, directive)
        if directive.spelling is None:
            raise AuditError(
                f"{source_name}:{directive.line}: include spelling is absent"
            )
        includes.append((directive.spelling, directive.kind))
    return includes


def _make_include_configuration(root: Path) -> tuple[list[str], list[str]]:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return [], []
    text = makefile.read_text(encoding="utf-8", errors="replace")
    logical_text = re.sub(r"\\\r?\n", " ", text)
    include_paths: list[str] = []
    forced_includes: list[str] = []
    for match in re.finditer(r"(?:^|[\s=])-I\s*([^\s]+)", logical_text):
        value = match.group(1).strip('"\'').replace("\\", "/")
        value = re.sub(r"^\./", "", value)
        if "$" not in value and value not in include_paths:
            include_paths.append(value)
    for match in re.finditer(
        r"(?:^|[\s=])-include\s+([^\s]+)", logical_text
    ):
        value = match.group(1).strip('"\'').replace("\\", "/")
        value = re.sub(r"^\./", "", value)
        if "$" not in value and value not in forced_includes:
            forced_includes.append(value)
    return include_paths, forced_includes


def _read_evaluated_make_variables(
    root: Path, make: str, variables: tuple[str, ...]
) -> dict[str, str]:
    target = "__cupid_audit_profile_values__"
    value_names = [f"__CUPID_AUDIT_VALUE_{index}" for index in range(len(variables))]
    origin_names = [
        f"__CUPID_AUDIT_ORIGIN_{index}" for index in range(len(variables))
    ]
    overlay_lines = []
    for index, variable in enumerate(variables):
        overlay_lines.append(f"{value_names[index]} := $({variable})")
        overlay_lines.append(f"{origin_names[index]} := $(origin {variable})")
    overlay_lines.extend((f".PHONY: {target}", f"{target}:", ""))
    result = subprocess.run(
        [
            make,
            "MAKE=:",
            "--no-print-directory",
            "-prRn",
            "-f",
            "Makefile",
            "-f",
            "-",
            target,
        ],
        cwd=root,
        input="\n".join(overlay_lines),
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(
            f"GNU Make could not evaluate CupidC profile variables in "
            f"{root}: {detail}"
        )
    wanted = set(value_names) | set(origin_names)
    evaluated: dict[str, str] = {}
    assignment_pattern = re.compile(
        r"^(__CUPID_AUDIT_(?:VALUE|ORIGIN)_[0-9]+)\s*:=\s?(.*)$"
    )
    for line in result.stdout.splitlines():
        match = assignment_pattern.match(line)
        if match is not None and match.group(1) in wanted:
            evaluated[match.group(1)] = match.group(2)
    missing = sorted(wanted - set(evaluated))
    if missing:
        raise AuditError(
            f"GNU Make omitted CupidC profile sentinel(s): {missing!r}"
        )
    values: dict[str, str] = {}
    for index, variable in enumerate(variables):
        if evaluated[origin_names[index]] == "undefined":
            raise AuditError(
                f"missing Make variable in CupidC preprocessing profile: "
                f"{variable}"
            )
        values[variable] = evaluated[value_names[index]]
    return values


def _make_preprocessor_flags(
    expanded: str, variable: str
) -> tuple[list[str], list[str], dict[str, str], set[str]]:
    if "$(" in expanded or "${" in expanded:
        raise AuditError(
            f"unmodeled Make reference/function remains in CupidC profile "
            f"{variable}: {expanded!r}"
        )
    try:
        tokens = shlex.split(expanded, posix=True)
    except ValueError as exc:
        raise AuditError(
            f"could not tokenize Make variable {variable}: {exc}"
        ) from exc
    include_paths: list[str] = []
    forced_includes: list[str] = []
    defines: dict[str, str] = {}
    flags: set[str] = set()
    index = 0
    while index < len(tokens):
        token = tokens[index]
        flags.add(token)
        if token == "-I":
            index += 1
            if index >= len(tokens):
                raise AuditError(f"missing -I operand in Make variable {variable}")
            include_paths.append(tokens[index])
        elif token.startswith("-I") and len(token) > 2:
            include_paths.append(token[2:])
        elif token == "-include":
            index += 1
            if index >= len(tokens):
                raise AuditError(
                    f"missing -include operand in Make variable {variable}"
                )
            forced_includes.append(tokens[index])
        elif token == "-D":
            index += 1
            if index >= len(tokens):
                raise AuditError(f"missing -D operand in Make variable {variable}")
            definition = tokens[index]
            macro_name, separator, replacement = definition.partition("=")
            defines[macro_name] = replacement if separator else "1"
        elif token.startswith("-D") and len(token) > 2:
            macro_name, separator, replacement = token[2:].partition("=")
            defines[macro_name] = replacement if separator else "1"
        index += 1
    return include_paths, forced_includes, defines, flags


def _make_flag_logical_path(directory: str, path: str) -> str:
    normalized = path.replace("\\", "/")
    if "$" in normalized or re.match(r"^[A-Za-z]:/", normalized):
        raise AuditError(
            f"non-logical Make path in CupidC preprocessing profile: {path!r}"
        )
    if normalized.startswith("/"):
        relative = normalized[1:]
    else:
        relative = posixpath.normpath(
            normalized
            if directory == "."
            else posixpath.join(directory, normalized)
        )
    if relative in {"", ".", ".."} or relative.startswith("../"):
        raise AuditError(
            f"Make path escapes CupidC preprocessing profile: {path!r}"
        )
    if relative.startswith("./"):
        relative = relative[2:]
    return f"/{relative}"


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
        for include, kind in _declared_includes(
            source_path, language, relative
        ):
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
        if re.search(r"(?:^|\s)wrap-text(?:\s|$)", joined):
            return "wrap_text_as_elf32_relocatable"
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


def _mask_c_comments_preserve_literals(text: str) -> str:
    """Mask comments while retaining literal preprocessing-token spelling."""
    output: list[str] = []
    index = 0
    state = "code"
    delimiter = ""
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
            if char in {'"', "'"}:
                delimiter = char
                state = "literal"
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
            escaped = text[index]
            output.append(escaped)
            index += 1
        elif char == delimiter:
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


def _c_physical_lines(text: str) -> list[tuple[str, bool]]:
    """Split only the CR/LF sequences that phase two treats as newlines."""
    lines: list[tuple[str, bool]] = []
    start = 0
    index = 0
    while index < len(text):
        if text[index] not in {"\r", "\n"}:
            index += 1
            continue
        lines.append((text[start:index], True))
        if (
            text[index] == "\r"
            and index + 1 < len(text)
            and text[index + 1] == "\n"
        ):
            index += 2
        else:
            index += 1
        start = index
    if start < len(text):
        lines.append((text[start:], False))
    return lines


def _c_raw_logical_lines(text: str) -> list[tuple[int, str, str]]:
    """Return phase-two logical lines before comments and literals are masked."""
    raw_lines = _c_physical_lines(text)
    raw_logical_lines: list[tuple[int, str, str]] = []
    chunks: list[str] = []
    start_index = 0
    for index, (raw_body, terminated) in enumerate(raw_lines):
        if not chunks:
            start_index = index
        continued = terminated and raw_body.endswith("\\")
        chunks.append(raw_body[:-1] if continued else raw_body)
        if continued:
            continue
        raw_logical_lines.append(
            (
                start_index + 1,
                raw_lines[start_index][0],
                "".join(chunks),
            )
        )
        chunks = []

    if chunks:
        raw_logical_lines.append(
            (
                start_index + 1,
                raw_lines[start_index][0],
                "".join(chunks),
            )
        )
    return raw_logical_lines


def _c_logical_lines(text: str) -> list[tuple[int, str, str]]:
    """Return phase-two logical lines with their first physical location."""
    raw_logical_lines = _c_raw_logical_lines(text)
    if not raw_logical_lines:
        return []

    masked_text = _mask_c_noncode(
        "\n".join(code_line for _, _, code_line in raw_logical_lines)
    )
    masked_lines = masked_text.split("\n")
    if len(masked_lines) != len(raw_logical_lines):
        raise AuditError("C masking changed the logical line count")
    return [
        (line_number, original_line, masked_line)
        for (line_number, original_line, _), masked_line in zip(
            raw_logical_lines, masked_lines, strict=True
        )
    ]


_C_PP_PUNCTUATORS = tuple(
    sorted(
        {
            "%:%:",
            ">>=",
            "<<=",
            "...",
            "##",
            "->",
            "++",
            "--",
            "<<",
            ">>",
            "<=",
            ">=",
            "==",
            "!=",
            "&&",
            "||",
            "*=",
            "/=",
            "%=",
            "+=",
            "-=",
            "&=",
            "^=",
            "|=",
            "<:",
            ":>",
            "<%",
            "%>",
            "%:",
            "[",
            "]",
            "(",
            ")",
            "{",
            "}",
            ".",
            "&",
            "*",
            "+",
            "-",
            "~",
            "!",
            "/",
            "%",
            "<",
            ">",
            "^",
            "|",
            "?",
            ":",
            ";",
            "=",
            ",",
            "#",
        },
        key=lambda spelling: (-len(spelling), spelling),
    )
)


def _c_ucn_width(text: str, index: int) -> int:
    if index + 2 > len(text) or text[index] != "\\":
        return 0
    marker = text[index + 1]
    digits = 4 if marker == "u" else 8 if marker == "U" else 0
    if digits == 0 or index + 2 + digits > len(text):
        return 0
    spelling = text[index + 2 : index + 2 + digits]
    valid = all(char in "0123456789abcdefABCDEF" for char in spelling)
    return 2 + digits if valid else 0


def _c_identifier_unit_width(text: str, index: int, initial: bool) -> int:
    char = text[index]
    if char == "_" or char.isalpha() or (not initial and char.isdigit()):
        return 1
    return _c_ucn_width(text, index)


def _c_literal_end(text: str, index: int) -> int:
    delimiter_index = index
    if text.startswith("u8", index) and index + 2 < len(text):
        delimiter_index = index + 2
    elif text[index] in {"L", "u", "U"} and index + 1 < len(text):
        delimiter_index = index + 1
    if text[delimiter_index] not in {'"', "'"}:
        return index
    delimiter = text[delimiter_index]
    cursor = delimiter_index + 1
    while cursor < len(text):
        if text[cursor] == "\\":
            if cursor + 1 >= len(text) or text[cursor + 1] in {"\r", "\n"}:
                return index
            cursor += 2
            continue
        if text[cursor] == delimiter:
            return cursor + 1
        if text[cursor] in {"\r", "\n"}:
            return index
        cursor += 1
    return index


def _c_pp_number_end(text: str, index: int) -> int:
    cursor = index + 1
    while cursor < len(text):
        char = text[cursor]
        if char == "." or char == "_" or char.isalnum():
            cursor += 1
            continue
        ucn_width = _c_ucn_width(text, cursor)
        if ucn_width != 0:
            cursor += ucn_width
            continue
        if char in {"+", "-"} and text[cursor - 1] in {"e", "E", "p", "P"}:
            cursor += 1
            continue
        break
    return cursor


def _normalize_c_preprocessing_tokens(
    expression: str, path: str, line: int
) -> tuple[str, ...]:
    tokens: list[str] = []
    index = 0
    while index < len(expression):
        if expression[index].isspace():
            index += 1
            continue
        literal_end = _c_literal_end(expression, index)
        if literal_end != index:
            tokens.append(expression[index:literal_end])
            index = literal_end
            continue
        identifier_width = _c_identifier_unit_width(
            expression, index, initial=True
        )
        if identifier_width != 0:
            end = index + identifier_width
            while end < len(expression):
                width = _c_identifier_unit_width(expression, end, initial=False)
                if width == 0:
                    break
                end += width
            tokens.append(expression[index:end])
            index = end
            continue
        if expression[index].isdigit() or (
            expression[index] == "."
            and index + 1 < len(expression)
            and expression[index + 1].isdigit()
        ):
            end = _c_pp_number_end(expression, index)
            tokens.append(expression[index:end])
            index = end
            continue
        punctuator = next(
            (
                spelling
                for spelling in _C_PP_PUNCTUATORS
                if expression.startswith(spelling, index)
            ),
            None,
        )
        if punctuator is not None:
            tokens.append(punctuator)
            index += len(punctuator)
            continue
        excerpt = expression[index : index + 12]
        raise AuditError(
            f"{path}:{line}: unrecognized preprocessing token at {excerpt!r}"
        )
    if not tokens:
        raise AuditError(f"{path}:{line}: conditional expression is empty")
    return tuple(tokens)


def _scan_c_macro_features(
    path: str,
    logical_lines: list[tuple[int, str, str]],
    collector: FeatureCollector,
) -> None:
    for line_number, original_line, code_line in logical_lines:
        macro_match = re.match(
            r"\s*(?:#|%:)\s*define\s+[A-Za-z_]\w*\(([^)]*)\)", code_line
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
            r"\s*(?:#|%:)\s*define\s+[A-Za-z_]\w*(?:\([^)]*\))?\s*(.*)$",
            code_line,
        )
        if define_match:
            replacement = define_match.group(1)
            paste_count, stringify_count = _c_macro_operator_counts(replacement)
            collector.add(
                "c.preprocessor.token_paste",
                path,
                line_number,
                original_line,
                paste_count,
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
                len(
                    re.findall(
                        r",\s*(?:##|%:%:)\s*__VA_ARGS__\b", replacement
                    )
                ),
            )


def _c_macro_operator_counts(replacement: str) -> tuple[int, int]:
    """Count paste and parameter-stringify tokens with C longest matching."""
    paste_count = 0
    stringify_count = 0
    index = 0
    while index < len(replacement):
        if replacement.startswith("%:%:", index):
            paste_count += 1
            index += 4
            continue
        if replacement.startswith("##", index):
            paste_count += 1
            index += 2
            continue
        width = 0
        if replacement.startswith("%:", index):
            width = 2
        elif replacement[index] == "#":
            width = 1
        if width != 0:
            operand = index + width
            while operand < len(replacement) and replacement[operand].isspace():
                operand += 1
            if operand < len(replacement) and (
                replacement[operand] == "_"
                or "A" <= replacement[operand] <= "Z"
                or "a" <= replacement[operand] <= "z"
            ):
                stringify_count += 1
            index += width
            continue
        index += 1
    return paste_count, stringify_count


def _c_attribute_names(contents: str) -> list[str]:
    items: list[str] = []
    start = 0
    depth = 0
    for index, char in enumerate(contents):
        if char == "(":
            depth += 1
        elif char == ")" and depth != 0:
            depth -= 1
        elif char == "," and depth == 0:
            items.append(contents[start:index])
            start = index + 1
    items.append(contents[start:])
    names: list[str] = []
    for item in items:
        match = re.match(r"\s*([A-Za-z_]\w*)\b", item)
        if match is not None:
            names.append(match.group(1).strip("_").lower())
    return names


def _scan_c_attributes(
    path: str,
    logical_lines: list[tuple[int, str, str]],
    collector: FeatureCollector,
) -> None:
    code = "\n".join(code_line for _, _, code_line in logical_lines)
    line_starts: list[int] = []
    offset = 0
    for _, _, code_line in logical_lines:
        line_starts.append(offset)
        offset += len(code_line) + 1
    cursor = 0
    introducer = re.compile(r"\b__attribute(?:__)?\b")
    while True:
        match = introducer.search(code, cursor)
        if match is None:
            return
        position = match.end()
        while position < len(code) and code[position].isspace():
            position += 1
        if position >= len(code) or code[position] != "(":
            cursor = match.end()
            continue
        position += 1
        while position < len(code) and code[position].isspace():
            position += 1
        if position >= len(code) or code[position] != "(":
            cursor = match.end()
            continue
        contents_start = position + 1
        position = contents_start
        depth = 0
        contents_end: int | None = None
        group_end: int | None = None
        while position < len(code):
            char = code[position]
            if char == "(":
                depth += 1
            elif char == ")":
                if depth != 0:
                    depth -= 1
                else:
                    close = position + 1
                    while close < len(code) and code[close].isspace():
                        close += 1
                    if close < len(code) and code[close] == ")":
                        contents_end = position
                        group_end = close + 1
                        break
            position += 1
        if contents_end is None or group_end is None:
            cursor = match.end()
            continue
        line_index = bisect.bisect_right(line_starts, match.start()) - 1
        line_number, original_line, _ = logical_lines[line_index]
        counts = collections.Counter(
            _c_attribute_names(code[contents_start:contents_end])
        )
        for name in sorted(counts):
            collector.add(
                f"c.extension.attribute.{name}",
                path,
                line_number,
                original_line,
                counts[name],
            )
        cursor = group_end


def _scan_c_features(
    path: str,
    text: str,
    language: str,
    collector: FeatureCollector,
) -> None:
    logical_lines = _c_logical_lines(text)
    _scan_c_macro_features(path, logical_lines, collector)
    _scan_c_attributes(path, logical_lines, collector)
    for line_number, original_line, code_line in logical_lines:
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
            feature_id = GNU_C_OPERATOR_FEATURES.get(token)
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

        directive_match = re.match(
            r"\s*(?:#|%:)\s*([A-Za-z_]\w*)", code_line
        )
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
        if re.match(r"\s*(?:#|%:)\s*pragma\s+pack\b", code_line):
            collector.add(
                "c.preprocessor.pragma.pack", path, line_number, original_line
            )
        if re.match(r"\s*(?:#|%:)\s*pragma\s+once\b", code_line):
            collector.add(
                "c.preprocessor.pragma.once", path, line_number, original_line
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


def _c_preprocessor_include_operands_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    source_files = 0
    include_occurrences = 0
    direct_quoted_occurrences = 0
    direct_angle_occurrences = 0
    pp_token_operand_occurrences = 0
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    max_conditional_depth = 0
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        source_files += 1
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        for directive in _c_include_directives(text, path):
            include_occurrences += 1
            if directive.kind == "quoted":
                direct_quoted_occurrences += 1
            elif directive.kind == "angle":
                direct_angle_occurrences += 1
            else:
                pp_token_operand_occurrences += 1
                _reject_pp_token_include(path, directive)
            if directive.marker == "#":
                ordinary_marker_occurrences += 1
            else:
                digraph_marker_occurrences += 1
            max_conditional_depth = max(
                max_conditional_depth, len(directive.conditional_stack)
            )
    return {
        "status": "pass",
        "source_files": source_files,
        "include_occurrences": include_occurrences,
        "direct_quoted_occurrences": direct_quoted_occurrences,
        "direct_angle_occurrences": direct_angle_occurrences,
        "pp_token_operand_occurrences": pp_token_operand_occurrences,
        "ordinary_marker_occurrences": ordinary_marker_occurrences,
        "digraph_marker_occurrences": digraph_marker_occurrences,
        "max_conditional_depth": max_conditional_depth,
    }


def _c_preprocessor_line_directives_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[
        tuple[str, str, bool | None, int], list[dict[str, object]]
    ] = collections.defaultdict(list)
    source_files = 0
    named_line_occurrences = 0
    direct_line_occurrences = 0
    pp_token_line_occurrences = 0
    filename_occurrences = 0
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    numeric_marker_occurrences = 0
    max_conditional_depth = 0
    temple_sources = sorted(
        path
        for path in active_sources
        if _language(path) in {"c", "c_header", "cupid_c"}
        and path.replace("\\", "/").split("/", 1)[0].casefold()
        == "templeos"
    )
    if temple_sources:
        raise AuditError(
            f"{temple_sources[0]}: TempleOS reference tree cannot be an "
            "active C preprocessing input"
        )
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        source_files += 1
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        payload_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(payload_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        conditional_depth = 0
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            payload_line,
        ) in zip(
            logical_lines,
            directive_lines,
            payload_lines,
            strict=True,
        ):
            conditional_match = re.match(
                r"\s*(?:#|%:)\s*(if|ifdef|ifndef|endif)\b",
                directive_line,
            )
            if conditional_match is not None:
                if conditional_match.group(1) == "endif":
                    conditional_depth = max(0, conditional_depth - 1)
                else:
                    conditional_depth += 1
                continue

            match = re.match(r"\s*(#|%:)\s*line\b", directive_line)
            if match is None:
                numeric_match = re.match(
                    r"\s*(#|%:)\s*([0-9]+)(?=\s|$)", directive_line
                )
                if numeric_match is None:
                    continue
                marker, line_number_token = numeric_match.groups()
                payload = (
                    line_number_token + payload_line[numeric_match.end() :]
                )
                tokens = _normalize_c_preprocessing_tokens(
                    payload, path, line_number
                )
                has_filename = (
                    len(tokens) >= 2
                    and tokens[1].startswith('"')
                    and tokens[1].endswith('"')
                )
                numeric_marker_occurrences += 1
                max_conditional_depth = max(
                    max_conditional_depth, conditional_depth
                )
                by_form[
                    (
                        "numeric_marker",
                        marker,
                        has_filename,
                        conditional_depth,
                    )
                ].append(
                    {
                        "path": path,
                        "line": line_number,
                        "text": raw_line.strip()[:160],
                        "operand": " ".join(tokens),
                    }
                )
                continue
            marker = match.group(1)
            payload = payload_line[match.end() :]
            if not payload.strip():
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #line form: "
                    "empty operand"
                )
            tokens = _normalize_c_preprocessing_tokens(
                payload, path, line_number
            )
            direct_decimal = re.fullmatch(r"[0-9]+", tokens[0]) is not None
            has_filename = (
                len(tokens) == 2
                and tokens[1].startswith('"')
                and tokens[1].endswith('"')
            )
            if direct_decimal and len(tokens) == 1:
                form = "direct_decimal"
                direct_line_occurrences += 1
            elif direct_decimal and has_filename:
                form = "direct_decimal_filename"
                direct_line_occurrences += 1
                filename_occurrences += 1
            else:
                form = "pp_tokens"
                pp_token_line_occurrences += 1
                # Expansion decides whether the final standard form contains
                # a filename; the source audit deliberately does not evaluate
                # macros independently from the CupidC corpus harness.
                has_filename = None
            named_line_occurrences += 1
            if marker == "#":
                ordinary_marker_occurrences += 1
            else:
                digraph_marker_occurrences += 1
            max_conditional_depth = max(
                max_conditional_depth, conditional_depth
            )
            by_form[(form, marker, has_filename, conditional_depth)].append(
                {
                    "path": path,
                    "line": line_number,
                    "text": raw_line.strip()[:160],
                    "operand": " ".join(tokens),
                }
            )

    forms = []
    for (form, marker, has_filename, conditional_depth), evidence in sorted(
        by_form.items(), key=lambda item: item[0]
    ):
        forms.append(
            {
                "form": form,
                "marker": marker,
                "has_filename": has_filename,
                "conditional_depth": conditional_depth,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    return {
        "status": "pass",
        "source_files": source_files,
        "named_line_occurrences": named_line_occurrences,
        "direct_line_occurrences": direct_line_occurrences,
        "pp_token_line_occurrences": pp_token_line_occurrences,
        "filename_occurrences": filename_occurrences,
        "ordinary_marker_occurrences": ordinary_marker_occurrences,
        "digraph_marker_occurrences": digraph_marker_occurrences,
        "numeric_marker_occurrences": numeric_marker_occurrences,
        "max_conditional_depth": max_conditional_depth,
        "forms": forms,
    }


def _c_preprocessor_conditionals_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_expression: dict[tuple[str, ...], list[dict[str, object]]] = (
        collections.defaultdict(list)
    )
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        expression_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(expression_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            expression_line,
        ) in zip(
            logical_lines,
            directive_lines,
            expression_lines,
            strict=True,
        ):
            match = re.match(
                r"\s*(?:#|%:)\s*(if|elif)\b", directive_line
            )
            if match is None:
                continue
            directive = match.group(1)
            tokens = _normalize_c_preprocessing_tokens(
                expression_line[match.end() :], path, line_number
            )
            by_expression[tokens].append(
                {
                    "path": path,
                    "line": line_number,
                    "directive": directive,
                    "text": raw_line.strip()[:160],
                }
            )

    expressions: list[dict[str, object]] = []
    if_occurrences = 0
    elif_occurrences = 0
    directive_expression_pairs = 0
    for tokens, evidence in sorted(
        by_expression.items(), key=lambda item: item[0]
    ):
        if_count = sum(item["directive"] == "if" for item in evidence)
        elif_count = sum(item["directive"] == "elif" for item in evidence)
        if_occurrences += if_count
        elif_occurrences += elif_count
        directive_expression_pairs += int(if_count != 0) + int(elif_count != 0)
        expressions.append(
            {
                "expression": " ".join(tokens),
                "if_occurrences": if_count,
                "elif_occurrences": elif_count,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    return {
        "status": "pass",
        "if_occurrences": if_occurrences,
        "elif_occurrences": elif_occurrences,
        "expression_occurrences": if_occurrences + elif_occurrences,
        "unique_expressions": len(expressions),
        "directive_expression_pairs": directive_expression_pairs,
        "expressions": expressions,
    }


def _c_preprocessor_pragmas_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[
        tuple[str, str, int | None], list[dict[str, object]]
    ] = collections.defaultdict(list)
    once_occurrences = 0
    pack_push_occurrences = 0
    pack_pop_occurrences = 0
    pack_underflow_occurrences = 0
    unmatched_pack_pushes = 0
    max_pack_depth = 0
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        payload_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(payload_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        pack_depth = 0
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            payload_line,
        ) in zip(
            logical_lines,
            directive_lines,
            payload_lines,
            strict=True,
        ):
            if re.search(r"\b_Pragma\b", directive_line):
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #pragma form: "
                    "_Pragma operator"
                )
            match = re.match(r"\s*(?:#|%:)\s*pragma\b", directive_line)
            if match is None:
                continue
            payload = payload_line[match.end() :]
            if not payload.strip():
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #pragma form: "
                    "<empty>"
                )
            tokens = _normalize_c_preprocessing_tokens(
                payload, path, line_number
            )
            form: str
            action: str
            alignment: int | None
            if tokens == ("once",):
                form = "once"
                action = "once"
                alignment = None
                once_occurrences += 1
            elif tokens == ("pack", "(", "pop", ")"):
                form = "pack(pop)"
                action = "pack_pop"
                alignment = None
                pack_pop_occurrences += 1
                if pack_depth == 0:
                    pack_underflow_occurrences += 1
                else:
                    pack_depth -= 1
            elif (
                len(tokens) == 6
                and tokens[:4] == ("pack", "(", "push", ",")
                and tokens[5] == ")"
                and tokens[4] in {"1", "2", "4", "8", "16"}
            ):
                alignment = int(tokens[4])
                form = f"pack(push, {alignment})"
                action = "pack_push"
                pack_push_occurrences += 1
                pack_depth += 1
                max_pack_depth = max(max_pack_depth, pack_depth)
            else:
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #pragma form: "
                    f"{' '.join(tokens)}"
                )
            by_form[(form, action, alignment)].append(
                {
                    "path": path,
                    "line": line_number,
                    "text": raw_line.strip()[:160],
                }
            )
        unmatched_pack_pushes += pack_depth

    forms: list[dict[str, object]] = []
    for (form, action, alignment), evidence in sorted(by_form.items()):
        forms.append(
            {
                "form": form,
                "action": action,
                "alignment": alignment,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    pack_occurrences = pack_push_occurrences + pack_pop_occurrences
    return {
        "status": "pass",
        "pragma_occurrences": once_occurrences + pack_occurrences,
        "once_occurrences": once_occurrences,
        "pack_occurrences": pack_occurrences,
        "pack_push_occurrences": pack_push_occurrences,
        "pack_pop_occurrences": pack_pop_occurrences,
        "pack_balanced": (
            pack_underflow_occurrences == 0 and unmatched_pack_pushes == 0
        ),
        "max_pack_depth": max_pack_depth,
        "pack_underflow_occurrences": pack_underflow_occurrences,
        "unmatched_pack_pushes": unmatched_pack_pushes,
        "forms": forms,
    }


def _c_preprocessor_cupid_exe_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[
        tuple[str, str, int], list[dict[str, object]]
    ] = collections.defaultdict(list)
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    max_conditional_depth = 0
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"cupid_c", "c_header"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        payload_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(payload_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        conditional_depth = 0
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            payload_line,
        ) in zip(
            logical_lines,
            directive_lines,
            payload_lines,
            strict=True,
        ):
            match = re.match(
                r"\s*(#|%:)\s*([A-Za-z_]\w*)\b", directive_line
            )
            if match is None:
                continue
            marker, directive = match.groups()
            if directive in {"if", "ifdef", "ifndef"}:
                conditional_depth += 1
                continue
            if directive == "endif":
                conditional_depth = max(0, conditional_depth - 1)
                continue
            if directive.casefold() != "exe":
                continue
            payload = payload_line[match.end() :]
            try:
                tokens = (
                    _normalize_c_preprocessing_tokens(
                        payload, path, line_number
                    )
                    if payload.strip()
                    else ()
                )
            except AuditError:
                raise AuditError(
                    f"{path}:{line_number}: unclassified active Cupid #exe "
                    f"form: {payload.strip()[:80]}"
                ) from None
            if directive != "exe" or not tokens or tokens[0] != "{":
                rendered = " ".join(tokens) if tokens else "<empty>"
                raise AuditError(
                    f"{path}:{line_number}: unclassified active Cupid #exe "
                    f"form: {directive} {rendered}"
                )
            if conditional_depth != 0:
                raise AuditError(
                    f"{path}:{line_number}: unclassified active Cupid #exe "
                    f"form: conditional depth {conditional_depth}"
                )
            max_conditional_depth = max(
                max_conditional_depth, conditional_depth
            )
            by_form[("block", marker, conditional_depth)].append(
                {
                    "path": path,
                    "line": line_number,
                    "text": raw_line.strip()[:160],
                }
            )
            if marker == "#":
                ordinary_marker_occurrences += 1
            else:
                digraph_marker_occurrences += 1

    forms: list[dict[str, object]] = []
    for (form, marker, conditional_depth), evidence in sorted(by_form.items()):
        forms.append(
            {
                "form": form,
                "marker": marker,
                "conditional_depth": conditional_depth,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    exe_occurrences = ordinary_marker_occurrences + digraph_marker_occurrences
    return {
        "status": "pass",
        "exe_occurrences": exe_occurrences,
        "block_occurrences": exe_occurrences,
        "ordinary_marker_occurrences": ordinary_marker_occurrences,
        "digraph_marker_occurrences": digraph_marker_occurrences,
        "max_conditional_depth": max_conditional_depth,
        "forms": forms,
    }


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
            elif language == "assembly" and operation in {
                "wrap_binary_as_elf32_relocatable",
                "wrap_text_as_elf32_relocatable",
            }:
                feature_id = "asm.delivery.embedded_source"
            elif language == "cupid_c" and operation in {
                "wrap_binary_as_elf32_relocatable",
                "wrap_text_as_elf32_relocatable",
            }:
                feature_id = "cupid_c.delivery.embedded_source"
            elif language == "c_header" and operation in {
                "wrap_binary_as_elf32_relocatable",
                "wrap_text_as_elf32_relocatable",
            }:
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
    contracts["c_preprocessor_include_operands"] = (
        _c_preprocessor_include_operands_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    contracts["c_preprocessor_line_directives"] = (
        _c_preprocessor_line_directives_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    contracts["c_preprocessor_conditionals"] = (
        _c_preprocessor_conditionals_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    contracts["c_preprocessor_pragmas"] = _c_preprocessor_pragmas_contract(
        root,
        all_sources,
        generated_sources,
    )
    contracts["c_preprocessor_cupid_exe"] = (
        _c_preprocessor_cupid_exe_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    feature_inventory = feature_collector.inventory()
    roadmap = _roadmap(sources, feature_inventory)
    abi = _abi_inventory(root, all_transforms)
    provenance = _provenance(root, models, sources)

    audit = {
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
    if {model.directory for model in models} == {".", "user", "toolchain"}:
        _validate_c_preprocessor_make_profiles(root, make)
        _validate_hosted_i386_contract_profiles(root)
        active_manifest = _c_preprocessor_active_cases_manifest(audit)
        contracts["c_preprocessor_translation_units"] = (
            _c_preprocessor_translation_unit_contract(active_manifest)
        )
    return audit


def _c_preprocessor_logical_path(path: str) -> str:
    normalized = path.replace("\\", "/")
    if (
        not normalized
        or normalized.startswith("/")
        or re.match(r"^[A-Za-z]:/", normalized)
        or posixpath.normpath(normalized) != normalized
        or normalized == ".."
        or normalized.startswith("../")
    ):
        raise AuditError(
            f"CupidC active preprocessing case is not repository-relative: {path!r}"
        )
    return f"/{normalized}"


def _c_preprocessor_compile_recipe_tokens(
    transform: dict[str, object],
) -> list[str]:
    output = str(transform.get("output", "<unknown>"))
    recipe = transform.get("recipe")
    if (
        not isinstance(recipe, list)
        or len(recipe) != 1
        or not isinstance(recipe[0], str)
        or "\n" in recipe[0]
        or "\r" in recipe[0]
    ):
        raise AuditError(
            f"CupidC active preprocessing compile recipe is not exactly one "
            f"command for {output}"
        )
    try:
        tokens = shlex.split(recipe[0], comments=False, posix=True)
        uncommented_tokens = shlex.split(
            recipe[0], comments=True, posix=True
        )
    except ValueError as exc:
        raise AuditError(
            f"could not tokenize CupidC compile recipe for {output}: {exc}"
        ) from exc
    if tokens != uncommented_tokens:
        raise AuditError(
            f"CupidC active preprocessing compile recipe contains a shell "
            f"comment for {output}"
        )
    if not tokens:
        raise AuditError(
            f"CupidC active preprocessing compile recipe is empty for {output}"
        )
    return tokens


def _c_preprocessor_require_compiler_invocation(
    transform: dict[str, object],
    tokens: list[str],
    expected_argument_profile: list[str],
    subject: str,
) -> None:
    output = str(transform.get("output", "<unknown>"))
    if tokens[0] not in {"$(CC)", "${CC}"}:
        raise AuditError(
            f"CupidC active preprocessing compile recipe does not invoke "
            f"$(CC) directly for {output}: {tokens[0]!r}"
        )
    recipe = transform.get("recipe")
    if not isinstance(recipe, list) or not isinstance(recipe[0], str):
        raise AuditError(
            f"CupidC active preprocessing compile recipe is absent for {output}"
        )
    argument_profile: list[str] = []
    for token in tokens[1:]:
        if token == "$<":
            continue
        if "`" in token:
            raise AuditError(
                f"CupidC active preprocessing compile recipe has unmodeled "
                f"shell substitution for {output}: {token!r}"
            )
        if any(character in token for character in ";&|<>"):
            raise AuditError(
                f"CupidC active preprocessing compile recipe has unmodeled "
                f"shell control for {output}: {token!r}"
            )
    for token in tokens:
        marker_match = re.fullmatch(
            r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)|"
            r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}",
            token,
        )
        if marker_match is not None:
            marker = marker_match.group(1) or marker_match.group(2)
            if re.search(
                rf"(?<!\S){re.escape(token)}(?!\S)", recipe[0]
            ) is None:
                raise AuditError(
                    f"CupidC active preprocessing compiler argument profile "
                    f"differs for {subject}: Make marker {marker!r} is not an "
                    f"unquoted argument"
                )
            argument_profile.append(marker)
        elif token == "-I../kernel/lang":
            argument_profile.append(token)
    if argument_profile != expected_argument_profile:
        raise AuditError(
            f"CupidC active preprocessing compiler argument profile differs "
            f"for {subject}: expected={expected_argument_profile!r}, "
            f"actual={argument_profile!r}"
        )


def _c_preprocessor_recipe_markers(
    transform: dict[str, object], allowed: set[str]
) -> collections.Counter[str]:
    output = str(transform.get("output", "<unknown>"))
    recipe = transform.get("recipe")
    if not isinstance(recipe, list) or not all(
        isinstance(line, str) for line in recipe
    ):
        raise AuditError(
            f"CupidC active preprocessing recipe is absent for {output}"
        )
    recipe_text = "\n".join(recipe)
    markers: collections.Counter[str] = collections.Counter()
    automatic_variables = frozenset("@%<?^+|*")
    index = 0
    while index < len(recipe_text):
        if recipe_text[index] != "$":
            index += 1
            continue
        if index + 1 >= len(recipe_text):
            raise AuditError(
                f"CupidC active preprocessing found an unmodeled recipe "
                f"dollar reference for {output}: trailing '$'"
            )
        opener = recipe_text[index + 1]
        if opener in "({":
            closer = ")" if opener == "(" else "}"
            end = recipe_text.find(closer, index + 2)
            if end < 0:
                raise AuditError(
                    f"CupidC active preprocessing found an unmodeled recipe "
                    f"Make reference/function for {output}: "
                    f"{recipe_text[index:]!r}"
                )
            reference = recipe_text[index : end + 1]
            name = recipe_text[index + 2 : end]
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
                raise AuditError(
                    f"CupidC active preprocessing found an unmodeled recipe "
                    f"Make reference/function for {output}: {reference!r}"
                )
            markers[name] += 1
            index = end + 1
            continue
        if opener in automatic_variables:
            index += 2
            continue
        raise AuditError(
            f"CupidC active preprocessing found an unmodeled recipe dollar "
            f"reference for {output}: {recipe_text[index:index + 2]!r}"
        )
    unknown = sorted(set(markers) - allowed)
    if unknown:
        raise AuditError(
            f"CupidC active preprocessing found unknown recipe marker(s) for "
            f"{output}: {', '.join(unknown)}"
        )
    return markers


def _c_preprocessor_literal_recipe_flags(
    transform: dict[str, object], tokens: list[str] | None = None
) -> list[str]:
    if tokens is None:
        recipe = transform.get("recipe")
        if not isinstance(recipe, list):
            return []
        tokens = []
        for line in recipe:
            if not isinstance(line, str):
                continue
            try:
                tokens.extend(shlex.split(line, posix=True))
            except ValueError as exc:
                raise AuditError(
                    f"could not tokenize CupidC compile recipe for "
                    f"{transform.get('output')}: {exc}"
                ) from exc
    safe_recipe_flags = {"-c", "-o", "-Os"}
    return sorted(
        token
        for token in tokens
        if (token.startswith("-") and token not in safe_recipe_flags)
        or token.startswith("@")
        or re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", token) is not None
    )


def _c_preprocessor_one_c_root(transform: dict[str, object]) -> str:
    output = str(transform.get("output", "<unknown>"))
    inputs = transform.get("inputs")
    if not isinstance(inputs, list) or not all(
        isinstance(path, str) for path in inputs
    ):
        raise AuditError(
            f"CupidC active preprocessing inputs are absent for {output}"
        )
    roots = [path for path in inputs if _language(path) == "c"]
    if len(roots) != 1:
        rendered = ", ".join(roots) if roots else "<none>"
        raise AuditError(
            f"CupidC active preprocessing expected exactly one C "
            f"translation-unit root for {output}; found {len(roots)}: {rendered}"
        )
    return roots[0]


def _c_preprocessor_profile_for_c_transform(
    directory: str, transform: dict[str, object]
) -> str:
    output = str(transform.get("output", "<unknown>"))
    recipe_tokens = _c_preprocessor_compile_recipe_tokens(transform)
    if directory == ".":
        markers = _c_preprocessor_recipe_markers(
            transform,
            {
                "CC",
                "CFLAGS",
                "CFLAGS_DOOM",
                "CFLAGS_DOOM_TREE",
                "SIMD_CFLAGS",
                "OPT",
            },
        )
        profiles = {
            "CFLAGS": "KERNEL_I386",
            "SIMD_CFLAGS": "KERNEL_I386",
            "CFLAGS_DOOM": "DOOM_COMPAT_I386",
            "CFLAGS_DOOM_TREE": "DOOM_TREE_I386",
        }
    elif directory == "user":
        markers = _c_preprocessor_recipe_markers(
            transform, {"CC", "CFLAGS"}
        )
        profiles = {"CFLAGS": "USER_I386"}
    elif directory == "toolchain":
        markers = _c_preprocessor_recipe_markers(
            transform, {"CC", "CPPFLAGS", "CFLAGS"}
        )
        expected_markers = collections.Counter(
            {"CC": 1, "CPPFLAGS": 1, "CFLAGS": 1}
        )
        if markers != expected_markers:
            raise AuditError(
                f"CupidC active preprocessing hosted recipe markers differ "
                f"for {output}: "
                f"expected={dict(sorted(expected_markers.items()))!r}, "
                f"actual={dict(sorted(markers.items()))!r}"
            )
        profiles = {"CFLAGS": "HOSTED_TOOLCHAIN_64"}
    else:
        raise AuditError(
            f"CupidC active preprocessing has no profile for supported build "
            f"directory {directory!r} ({output})"
        )
    selected_markers = [marker for marker in profiles if marker in markers]
    selected = [profiles[marker] for marker in selected_markers]
    if len(selected) != 1:
        rendered = ", ".join(sorted(markers)) if markers else "<none>"
        raise AuditError(
            f"CupidC active preprocessing expected exactly one profile recipe "
            f"marker for {output}; found {len(selected)} in: {rendered}"
        )
    selected_marker = selected_markers[0]
    if directory != "toolchain":
        expected_markers = collections.Counter(
            {"CC": 1, selected_marker: 1}
        )
        if directory == "." and "OPT" in markers:
            expected_markers["OPT"] = 1
        if markers != expected_markers:
            raise AuditError(
                f"CupidC active preprocessing recipe markers differ for "
                f"{output}: "
                f"expected={dict(sorted(expected_markers.items()))!r}, "
                f"actual={dict(sorted(markers.items()))!r}"
            )
    literal_flags = _c_preprocessor_literal_recipe_flags(
        transform, recipe_tokens
    )
    if directory == "toolchain":
        if literal_flags not in ([], ["-I../kernel/lang"]):
            raise AuditError(
                f"CupidC active preprocessing found literal preprocessor "
                f"flag(s) outside the selected profile for {output}: "
                f"{literal_flags!r}"
            )
        logical = _c_preprocessor_logical_path(
            _c_preprocessor_one_c_root(transform)
        )
        bridge_recipe = literal_flags == ["-I../kernel/lang"]
        bridge_source = logical in _C_PP_HOSTED_BRIDGE_CASES
        if bridge_recipe != bridge_source:
            raise AuditError(
                f"CupidC active preprocessing hosted bridge recipe differs "
                f"for {logical}: expected_bridge={bridge_source!r}, "
                f"actual_flags={literal_flags!r}"
            )
        expected_argument_profile = ["CC", "CPPFLAGS"]
        if bridge_source:
            expected_argument_profile.append("-I../kernel/lang")
        expected_argument_profile.append("CFLAGS")
        _c_preprocessor_require_compiler_invocation(
            transform, recipe_tokens, expected_argument_profile, logical
        )
        return (
            "HOSTED_KERNEL_BRIDGE_64"
            if bridge_source
            else selected[0]
        )
    if literal_flags:
        raise AuditError(
            f"CupidC active preprocessing found literal preprocessor "
            f"flag(s) outside the selected profile for {output}: "
            f"{literal_flags!r}"
        )
    expected_argument_profile = ["CC", selected_marker]
    if directory == "." and "OPT" in markers:
        expected_argument_profile.append("OPT")
    _c_preprocessor_require_compiler_invocation(
        transform, recipe_tokens, expected_argument_profile, output
    )
    return selected[0]


def _c_preprocessor_profile_configuration() -> tuple[
    tuple[tuple[str, str, str], ...],
    tuple[tuple[str, str, str], ...],
    tuple[tuple[str, str], ...],
]:
    include_roots: list[tuple[str, str, str]] = []
    for profile in ("KERNEL_I386", "DOOM_COMPAT_I386", "DOOM_TREE_I386"):
        include_roots.extend(
            (profile, root, _C_PP_INCLUDE_BOTH)
            for root in _C_PP_KERNEL_INCLUDE_ROOTS
        )
        if profile != "KERNEL_I386":
            include_roots.extend(
                (profile, root, _C_PP_INCLUDE_BOTH)
                for root in _C_PP_DOOM_EXTRA_INCLUDE_ROOTS
            )
    include_roots.append(
        ("USER_I386", "/user", _C_PP_INCLUDE_BOTH)
    )
    include_roots.extend(
        (
            ("HOSTED_TOOLCHAIN_64", "/toolchain", _C_PP_INCLUDE_BOTH),
            (
                "HOSTED_KERNEL_BRIDGE_64",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_KERNEL_BRIDGE_64",
                "/kernel/lang",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/hosted/i386-linux/include",
                "CTOOL_C_PP_INCLUDE_ANGLE",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/hosted/i386-linux/include",
                "CTOOL_C_PP_INCLUDE_ANGLE",
            ),
        )
    )

    macros: list[tuple[str, str, str]] = []
    for profile in (
        "KERNEL_I386",
        "DOOM_COMPAT_I386",
        "DOOM_TREE_I386",
        "USER_I386",
    ):
        macros.extend(
            (profile, name, replacement)
            for name, replacement in _C_PP_COMMON_I386_MACROS
        )
        if profile in {
            "KERNEL_I386",
            "DOOM_COMPAT_I386",
            "DOOM_TREE_I386",
        }:
            macros.append((profile, "__SSE2__", "1"))
        if profile == "KERNEL_I386":
            macros.append((profile, "DEBUG", "1"))
        elif profile == "DOOM_TREE_I386":
            macros.extend(
                (
                    (profile, "DEFAULT_SAVEGAMEDIR", '"/home/doom/"'),
                    (profile, "DOOM_PORT_CUPIDOS", "1"),
                )
            )
    macros.extend(
        (
            ("HOSTED_TOOLCHAIN_64", "__SIZEOF_POINTER__", "8"),
            ("HOSTED_KERNEL_BRIDGE_64", "__SIZEOF_POINTER__", "8"),
            ("HOSTED_I386_LINUX", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_I386_LINUX_GNU", "__SIZEOF_POINTER__", "4"),
        )
    )
    forced_includes = (
        ("DOOM_TREE_I386", "/kernel/doom/dglibc_compat.h"),
    )
    return tuple(include_roots), tuple(macros), forced_includes


_C_PP_I386_MODELED_FLAGS = frozenset(
    {
        "--target=i386-unknown-elf",
        "-m32",
        "-mfpmath=sse",
        "-msse",
        "-msse2",
        "-mstackrealign",
        "-fno-pie",
        "-fno-stack-protector",
        "-nostdlib",
        "-nostdinc",
        "-ffreestanding",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-c",
        "-fno-omit-frame-pointer",
        "-static",
    }
)
_C_PP_COMMON_MODELED_FLAGS = frozenset(
    {
        "-O2",
        "-pedantic",
        "-Werror",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wpointer-arith",
        "-Wcast-qual",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wwrite-strings",
        "-Wno-gnu-zero-variadic-macro-arguments",
        "-Wno-strict-prototypes",
        "-Wno-implicit-int-conversion",
        "-Wno-sign-conversion",
        "-Wno-unused",
        "-Wno-unused-result",
        "-Wno-implicit-function-declaration",
        "-Wno-sign-compare",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-type-limits",
        "-Wno-missing-field-initializers",
        "-I",
        "-D",
        "-include",
    }
)


def _c_preprocessor_unmodeled_flags(
    flags: set[str], profile_flags: frozenset[str] | set[str] = frozenset()
) -> list[str]:
    modeled_flags = set(_C_PP_COMMON_MODELED_FLAGS)
    modeled_flags.update(profile_flags)
    return sorted(
        flag
        for flag in flags
        if flag not in modeled_flags
        and not (flag.startswith("-I") and len(flag) > 2)
        and not (flag.startswith("-D") and len(flag) > 2)
    )


def _validate_hosted_i386_contract_profiles(root: Path) -> None:
    contract_path = root / "toolchain" / "tests" / "cupidc_object_contract.c"
    try:
        source = contract_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise AuditError(
            "CupidC hosted i386 source-profile contract is unavailable: "
            f"{contract_path}"
        ) from exc

    array_start = (
        "static const host_tool_source_case_t source_cases[] = {"
    )
    array_end = "  static const ctool_u32 cupidasm_objects[] = {"
    start = source.find(array_start)
    end = source.find(array_end, start + len(array_start))
    if start < 0 or end < 0:
        raise AuditError(
            "CupidC hosted i386 source-profile table shape changed"
        )
    table = source[start:end]
    row_pattern = re.compile(
        r'\{\s*"(?P<source>/[^"]+)"\s*,\s*"(?P<object>/[^"]+)"\s*,\s*'
        r"(?P<kind>HOST_TOOL_SOURCE_C|HOST_TOOL_SOURCE_ASSEMBLY)\s*,\s*"
        r"(?P<gnu>CTOOL_TRUE|CTOOL_FALSE)\s*\}",
        re.DOTALL,
    )
    rows = [
        (
            match.group("source"),
            match.group("kind"),
            match.group("gnu"),
        )
        for match in row_pattern.finditer(table)
    ]
    actual: dict[str, tuple[str, str]] = {}
    for path, kind, gnu_extensions in rows:
        if path in actual:
            raise AuditError(
                "CupidC hosted i386 source-profile table duplicates "
                f"{path}"
            )
        actual[path] = (kind, gnu_extensions)
    expected = {
        path: ("HOST_TOOL_SOURCE_C", "CTOOL_FALSE")
        for path in _C_PP_HOSTED_I386_STRICT_CASES
    }
    expected.update(
        {
            path: ("HOST_TOOL_SOURCE_C", "CTOOL_TRUE")
            for path in _C_PP_HOSTED_I386_GNU_CASES
        }
    )
    expected["/toolchain/hosted/i386-linux/start.asm"] = (
        "HOST_TOOL_SOURCE_ASSEMBLY",
        "CTOOL_FALSE",
    )
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        changed = sorted(
            path
            for path in set(actual) & set(expected)
            if actual[path] != expected[path]
        )
        raise AuditError(
            "CupidC hosted i386 source-profile rows differ from the checked "
            f"contract: missing={missing!r}, unexpected={unexpected!r}, "
            f"changed={changed!r}"
        )

    emitter_start = source.find(
        "static ctool_status_t emit_hosted_i386_source_with_extensions("
    )
    emitter_end = source.find(
        "\nstatic ctool_status_t emit_hosted_i386_source(",
        emitter_start + 1,
    )
    if emitter_start < 0 or emitter_end < 0:
        raise AuditError(
            "CupidC hosted i386 profile emitter shape changed"
        )
    emitter = source[emitter_start:emitter_end]
    required_emitter_fragments = (
        "pp_request = profile->request;",
        "pp_request.gnu_extensions = gnu_extensions;",
        "ctool_c_preprocess(job, source, &pp_request, &tape)",
        "parse_request.gnu_extensions = gnu_extensions;",
    )
    missing_fragments = [
        fragment
        for fragment in required_emitter_fragments
        if emitter.count(fragment) != 1
    ]
    if missing_fragments:
        raise AuditError(
            "CupidC hosted i386 profile emitter does not forward the checked "
            f"GNU mode: {missing_fragments!r}"
        )
    compile_loop = source[end:]
    if compile_loop.count("source_cases[index].gnu_extensions") != 2:
        raise AuditError(
            "CupidC hosted i386 compile loop does not consume each checked "
            "source profile for both emissions"
        )


def _validate_c_preprocessor_make_profiles(root: Path, make: str) -> None:
    include_rows, macro_rows, forced_rows = (
        _c_preprocessor_profile_configuration()
    )
    manifest_roots: dict[str, list[str]] = collections.defaultdict(list)
    for profile, path, _ in include_rows:
        manifest_roots[profile].append(path)
    manifest_forced: dict[str, list[str]] = collections.defaultdict(list)
    for profile, path in forced_rows:
        manifest_forced[profile].append(path)

    root_values = _read_evaluated_make_variables(
        root,
        make,
        ("CFLAGS", "SIMD_CFLAGS", "CFLAGS_DOOM", "CFLAGS_DOOM_TREE", "OPT"),
    )
    user_values = _read_evaluated_make_variables(
        root / "user", make, ("CFLAGS",)
    )
    toolchain_values = _read_evaluated_make_variables(
        root / "toolchain", make, ("CPPFLAGS", "CFLAGS")
    )
    hosted_flags = (
        f"{toolchain_values['CPPFLAGS']} {toolchain_values['CFLAGS']}"
    )
    specifications = (
        ("KERNEL_I386", ".", "CFLAGS", root_values["CFLAGS"]),
        ("KERNEL_I386", ".", "SIMD_CFLAGS", root_values["SIMD_CFLAGS"]),
        (
            "DOOM_COMPAT_I386",
            ".",
            "CFLAGS_DOOM",
            root_values["CFLAGS_DOOM"],
        ),
        (
            "DOOM_TREE_I386",
            ".",
            "CFLAGS_DOOM_TREE",
            root_values["CFLAGS_DOOM_TREE"],
        ),
        ("USER_I386", "user", "CFLAGS", user_values["CFLAGS"]),
        (
            "HOSTED_TOOLCHAIN_64",
            "toolchain",
            "CPPFLAGS+CFLAGS",
            hosted_flags,
        ),
        (
            "HOSTED_KERNEL_BRIDGE_64",
            "toolchain",
            "CPPFLAGS+-I../kernel/lang+CFLAGS",
            f"{toolchain_values['CPPFLAGS']} -I../kernel/lang "
            f"{toolchain_values['CFLAGS']}",
        ),
    )
    make_owned_macro_names = {
        "KERNEL_I386": {"DEBUG"},
        "DOOM_COMPAT_I386": set(),
        "DOOM_TREE_I386": {
            "DEFAULT_SAVEGAMEDIR",
            "DOOM_PORT_CUPIDOS",
        },
        "USER_I386": set(),
        "HOSTED_TOOLCHAIN_64": set(),
        "HOSTED_KERNEL_BRIDGE_64": set(),
    }
    manifest_macros: dict[str, dict[str, str]] = collections.defaultdict(dict)
    for profile, name, replacement in macro_rows:
        manifest_macros[profile][name] = replacement
    for profile, directory, variable, expanded in specifications:
        includes, forced, defines, flags = _make_preprocessor_flags(
            expanded, variable
        )
        logical_includes = [
            _make_flag_logical_path(directory, path) for path in includes
        ]
        logical_forced = [
            _make_flag_logical_path(directory, path) for path in forced
        ]
        if logical_includes != manifest_roots[profile]:
            raise AuditError(
                f"CupidC profile {profile} include-root order differs from "
                f"Make {variable}: expected={manifest_roots[profile]!r}, "
                f"actual={logical_includes!r}"
            )
        if logical_forced != manifest_forced[profile]:
            raise AuditError(
                f"CupidC profile {profile} forced includes differ from Make "
                f"{variable}: expected={manifest_forced[profile]!r}, "
                f"actual={logical_forced!r}"
            )
        missing_manifest_macros = sorted(
            make_owned_macro_names[profile] - set(manifest_macros[profile])
        )
        if missing_manifest_macros:
            raise AuditError(
                f"CupidC profile {profile} omits Make-owned macro action(s): "
                f"{missing_manifest_macros!r}"
            )
        expected_defines = {
            name: manifest_macros[profile][name]
            for name in make_owned_macro_names[profile]
        }
        if defines != expected_defines:
            raise AuditError(
                f"CupidC profile {profile} configured macros differ from Make "
                f"{variable}: expected={expected_defines!r}, "
                f"actual={defines!r}"
            )
        hosted_profile = profile in _C_PP_HOSTED_PROFILES
        if hosted_profile:
            required_flags = {"-std=c11"}
        else:
            required_flags = {"-m32", "-ffreestanding"}
            if profile != "USER_I386":
                required_flags.update(("-msse2", "-nostdinc"))
        missing_flags = sorted(required_flags - flags)
        if missing_flags:
            raise AuditError(
                f"CupidC profile {profile} lost target flag(s) in Make "
                f"{variable}: {missing_flags!r}"
            )
        profile_flags = (
            {"-std=c11"}
            if hosted_profile
            else _C_PP_I386_MODELED_FLAGS
        )
        unsupported = _c_preprocessor_unmodeled_flags(
            flags, profile_flags
        )
        if unsupported:
            raise AuditError(
                f"CupidC profile {profile} has unmodeled preprocessor flag(s) "
                f"in Make {variable}: {unsupported!r}"
            )

    opt_includes, opt_forced, opt_defines, opt_flags = (
        _make_preprocessor_flags(root_values["OPT"], "OPT")
    )
    opt_unmodeled = _c_preprocessor_unmodeled_flags(opt_flags)
    if opt_includes or opt_forced or opt_defines or opt_unmodeled:
        raise AuditError(
            "CupidC KERNEL_I386 OPT has preprocessor effects: "
            f"includes={opt_includes!r}, forced={opt_forced!r}, "
            f"defines={opt_defines!r}, unsupported={opt_unmodeled!r}"
        )


def _c_preprocessor_deferred_reason(path: str) -> str:
    external_header_units = {
        "/toolchain/ctool_host.c",
        "/toolchain/cupidasm_main.c",
        "/toolchain/cupiddis_main.c",
        "/toolchain/cupidc_main.c",
        "/toolchain/cupidld_main.c",
        "/toolchain/cupidobj_main.c",
    }
    if path not in external_header_units and not path.startswith(
        "/toolchain/tests/"
    ):
        raise AuditError(
            f"CupidC hosted deferral is not an external runtime unit: {path}"
        )
    return "external system headers/runtime block hosted CupidC preprocessing"


def _c_preprocessor_require_exact_paths(
    label: str, actual: list[str], expected: tuple[str, ...]
) -> None:
    if tuple(sorted(actual)) != tuple(sorted(expected)):
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        raise AuditError(
            f"CupidC active preprocessing {label} changed; "
            f"missing={missing!r}, unexpected={unexpected!r}"
        )


def _c_preprocessor_active_cases_manifest(
    audit: dict[str, object],
) -> CPreprocessorActiveCasesManifest:
    sources = audit.get("sources")
    if not isinstance(sources, list):
        raise AuditError("CupidC active preprocessing source inventory is absent")
    source_entries: dict[str, dict[str, object]] = {}
    for entry in sources:
        if not isinstance(entry, dict) or "path" not in entry:
            raise AuditError(
                "CupidC active preprocessing source inventory is malformed"
            )
        path = str(entry["path"])
        if path in source_entries:
            raise AuditError(
                f"CupidC active preprocessing source is duplicated: {path}"
            )
        source_entries[path] = entry

    root_build = audit.get("build")
    supplemental = audit.get("supplemental_builds")
    if not isinstance(root_build, dict) or not isinstance(supplemental, list):
        raise AuditError("CupidC active preprocessing build inventory is absent")
    builds: list[dict[str, object]] = [root_build]
    for build in supplemental:
        if not isinstance(build, dict):
            raise AuditError(
                "CupidC active preprocessing supplemental build is malformed"
            )
        builds.append(build)

    active_by_profile: dict[str, list[str]] = {
        profile.name: [] for profile in _C_PP_PROFILE_ROWS
    }
    generated: list[str] = []
    include_only: list[str] = []
    non_roots: list[str] = []
    deferred_hosted: list[str] = []
    seen_directories: set[str] = set()

    for build in builds:
        directory = str(build.get("directory", ""))
        if directory in seen_directories:
            raise AuditError(
                f"CupidC active preprocessing build is duplicated: {directory!r}"
            )
        seen_directories.add(directory)
        transforms = build.get("transforms")
        if not isinstance(transforms, list):
            raise AuditError(
                f"CupidC active preprocessing transforms are absent for {directory!r}"
            )
        for transform_value in sorted(
            transforms,
            key=lambda item: str(item.get("output", ""))
            if isinstance(item, dict)
            else "",
        ):
            if not isinstance(transform_value, dict):
                raise AuditError(
                    f"CupidC active preprocessing transform is malformed in "
                    f"{directory!r}"
                )
            transform = transform_value
            operation = str(transform.get("operation", ""))
            tools = transform.get("tools")
            output = str(transform.get("output", ""))
            if (
                directory == "toolchain"
                and output
                == "toolchain/build/cupidc-hosted-i386-tools.json"
            ):
                inputs = transform.get("inputs")
                if (
                    operation != "host_orchestration"
                    or tools != ["host_python"]
                    or not isinstance(inputs, list)
                    or not all(isinstance(path, str) for path in inputs)
                ):
                    raise AuditError(
                        "CupidC hosted i386 closure transform differs from "
                        "the checked orchestration contract"
                    )
                closure_roots = [
                    _c_preprocessor_logical_path(path)
                    for path in inputs
                    if _language(path) == "c"
                ]
                expected_closure = (
                    _C_PP_HOSTED_I386_STRICT_CASES
                    + _C_PP_HOSTED_I386_GNU_CASES
                )
                _c_preprocessor_require_exact_paths(
                    "hosted i386 closure", closure_roots, expected_closure
                )
                object_contract_inputs = [
                    path
                    for path in inputs
                    if re.fullmatch(
                        r"toolchain/build/cupidc-object-contract(?:\.exe)?",
                        path,
                    )
                    is not None
                ]
                if len(object_contract_inputs) != 1:
                    raise AuditError(
                        "CupidC hosted i386 closure must depend on exactly one "
                        "native object contract"
                    )
                recipe = transform.get("recipe")
                if (
                    not isinstance(recipe, list)
                    or not all(isinstance(line, str) for line in recipe)
                    or len(recipe) < 2
                    or recipe[0].strip()
                    != (
                        "$(CUPIDC_OBJECT_CONTRACT) self-host-link-tools .. "
                        "\\"
                    )
                    or recipe[1].strip()
                    != "$(CUPIDC_HOSTED_I386_ARTIFACTS)"
                ):
                    raise AuditError(
                        "CupidC hosted i386 closure recipe no longer invokes "
                        "the checked self-host link operation"
                    )
                for logical in expected_closure:
                    entry = source_entries.get(logical[1:])
                    if entry is None or entry.get("origin") != "tracked":
                        raise AuditError(
                            "CupidC hosted i386 closure source is not tracked: "
                            f"{logical}"
                        )
                active_by_profile["HOSTED_I386_LINUX"].extend(
                    _C_PP_HOSTED_I386_STRICT_CASES
                )
                active_by_profile["HOSTED_I386_LINUX_GNU"].extend(
                    _C_PP_HOSTED_I386_GNU_CASES
                )
                continue
            if operation in {
                "compile_c_to_elf32_object",
                "compile_c_to_host_object",
            }:
                if tools != ["host_c_compiler"]:
                    raise AuditError(
                        f"CupidC active preprocessing compile transform has "
                        f"unexpected tools for {transform.get('output')}: {tools!r}"
                    )
                profile = _c_preprocessor_profile_for_c_transform(
                    directory, transform
                )
                root = _c_preprocessor_one_c_root(transform)
                logical = _c_preprocessor_logical_path(root)
                entry = source_entries.get(root)
                if entry is None:
                    raise AuditError(
                        f"CupidC active preprocessing root is absent from source "
                        f"inventory: {root}"
                    )
                origin = str(entry.get("origin", ""))
                if logical in _C_PP_DEFERRED_HOSTED_CASES:
                    if (
                        directory != "toolchain"
                        or operation != "compile_c_to_host_object"
                        or profile not in _C_PP_HOSTED_PROFILES
                    ):
                        raise AuditError(
                            f"CupidC active preprocessing hosted deferral "
                            f"transform differs for {logical}: "
                            f"directory={directory!r}, operation={operation!r}, "
                            f"profile={profile!r}"
                        )
                    if origin != "tracked":
                        raise AuditError(
                            f"CupidC active preprocessing hosted deferral is "
                            f"not a tracked source ({origin!r}): {logical}"
                        )
                    deferred_hosted.append(logical)
                    continue
                if origin == "generated":
                    if profile != "KERNEL_I386":
                        raise AuditError(
                            f"CupidC generated root has non-kernel profile: {root}"
                        )
                    generated.append(logical)
                elif origin == "tracked":
                    active_by_profile[profile].append(logical)
                else:
                    raise AuditError(
                        f"CupidC active preprocessing root has unknown origin "
                        f"{origin!r}: {root}"
                    )
                continue

            inputs = transform.get("inputs")
            if not isinstance(inputs, list):
                if operation in {
                    "wrap_binary_as_elf32_relocatable",
                    "wrap_text_as_elf32_relocatable",
                }:
                    raise AuditError(
                        f"CupidC delivery transform inputs are absent for "
                        f"{transform.get('output')}"
                    )
                continue
            delivered_inputs = [
                path
                for path in inputs
                if isinstance(path, str)
                and (
                    _language(path) == "cupid_c"
                    or (
                        _language(path) == "c_header"
                        and path.startswith("bin/")
                    )
                )
            ]
            if (
                delivered_inputs
                and directory == "."
                and operation == "generate_c_source"
                and transform.get("output") == "kernel/util/bin_programs_gen.c"
                and tools == ["host_python"]
            ):
                # This generator embeds the delivered Cupid sources; their
                # preprocessing ownership is established by the separate
                # CupidObj wrap transform for each source below.
                continue
            if delivered_inputs and operation != "wrap_text_as_elf32_relocatable":
                raise AuditError(
                    f"CupidC active preprocessing found an unclassified Cupid "
                    f"delivery transform: {transform.get('output')} ({operation})"
                )
            if operation != "wrap_text_as_elf32_relocatable":
                continue
            cupid_inputs = [
                path
                for path in inputs
                if isinstance(path, str)
                and _language(path) in {"cupid_c", "c_header"}
            ]
            if not cupid_inputs:
                continue
            if directory != ".":
                raise AuditError(
                    f"CupidC active preprocessing found an unclassified Cupid "
                    f"delivery transform: {transform.get('output')} ({operation})"
                )
            _c_preprocessor_recipe_markers(transform, {"CUPIDOBJ"})
            if tools != ["cupid_object"]:
                raise AuditError(
                    f"CupidC delivery transform has unexpected tools for "
                    f"{transform.get('output')}: {tools!r}"
                )
            if len(cupid_inputs) != 1:
                raise AuditError(
                    f"CupidC delivery transform expected exactly one source for "
                    f"{transform.get('output')}; found {len(cupid_inputs)}"
                )
            root = cupid_inputs[0]
            logical = _c_preprocessor_logical_path(root)
            if _language(root) == "c_header":
                non_roots.append(logical)
            elif root.startswith("bin/browser/"):
                include_only.append(logical)
            elif root.startswith("bin/") and root.count("/") == 1:
                active_by_profile["CUPID_RUNTIME"].append(logical)
            else:
                raise AuditError(
                    f"CupidC delivery source has no active-case classification: {root}"
                )

    for profile, expected_count in _C_PP_ACTIVE_COUNTS.items():
        cases = active_by_profile[profile]
        if len(cases) != len(set(cases)):
            raise AuditError(
                f"CupidC active preprocessing profile has duplicate roots: {profile}"
            )
        if len(cases) != expected_count:
            raise AuditError(
                f"CupidC active preprocessing profile {profile} expected "
                f"{expected_count} tracked roots; found {len(cases)}"
            )
    _c_preprocessor_require_exact_paths(
        "generated kernel roots", generated, _C_PP_GENERATED_KERNEL_CASES
    )
    _c_preprocessor_require_exact_paths(
        "delivered non-root headers", non_roots, _C_PP_NON_ROOT_HEADERS
    )
    _c_preprocessor_require_exact_paths(
        "deferred hosted roots", deferred_hosted, _C_PP_DEFERRED_HOSTED_CASES
    )
    if len(include_only) != 22 or len(include_only) != len(set(include_only)):
        raise AuditError(
            f"CupidC active preprocessing expected 22 distinct browser "
            f"include-only fragments; found {len(include_only)}"
        )
    browser = source_entries.get("bin/browser.cc")
    browser_includes = set(browser.get("includes", [])) if browser else set()
    unresolved_fragments = sorted(
        path[1:] for path in include_only if path[1:] not in browser_includes
    )
    if unresolved_fragments:
        raise AuditError(
            f"CupidC browser fragments lost /bin/browser.cc ownership: "
            f"{unresolved_fragments!r}"
        )

    include_roots, macros, forced_includes = (
        _c_preprocessor_profile_configuration()
    )
    active_rows = tuple(
        (profile.name, path)
        for profile in _C_PP_PROFILE_ROWS
        for path in sorted(active_by_profile[profile.name])
    )
    generated_rows = tuple(
        ("KERNEL_I386", path) for path in sorted(generated)
    )
    return CPreprocessorActiveCasesManifest(
        profiles=_C_PP_PROFILE_ROWS,
        include_roots=include_roots,
        macros=macros,
        forced_includes=forced_includes,
        active_cases=active_rows,
        generated_cases=generated_rows,
        include_only=tuple(
            (path, "/bin/browser.cc") for path in sorted(include_only)
        ),
        non_roots=tuple(
            (
                path,
                "delivered header requires a translation-unit owner context",
            )
            for path in sorted(non_roots)
        ),
        deferred_hosted=tuple(
            (path, _c_preprocessor_deferred_reason(path))
            for path in sorted(deferred_hosted)
        ),
    )


def _c_preprocessor_translation_unit_contract(
    manifest: CPreprocessorActiveCasesManifest,
) -> dict[str, object]:
    profiles = []
    for profile_policy in manifest.profiles:
        name = profile_policy.name
        roots = [
            {"path": path, "forms": forms}
            for profile, path, forms in manifest.include_roots
            if profile == name
        ]
        macros = [
            {"name": macro_name, "replacement": replacement}
            for profile, macro_name, replacement in manifest.macros
            if profile == name
        ]
        forced = [
            path
            for profile, path in manifest.forced_includes
            if profile == name
        ]
        profiles.append(
            {
                "name": name,
                "mode": profile_policy.mode,
                "gnu_extensions": (
                    profile_policy.gnu_extensions == "CTOOL_TRUE"
                ),
                "hosted_environment": (
                    profile_policy.hosted_environment == "CTOOL_TRUE"
                ),
                "tracked_translation_units": sum(
                    profile == name
                    for profile, _ in manifest.active_cases
                ),
                "generated_translation_units": sum(
                    profile == name
                    for profile, _ in manifest.generated_cases
                ),
                "include_roots": roots,
                "macro_actions": macros,
                "forced_includes": forced,
            }
        )
    external_deferred = sum(
        reason.startswith("external system headers/runtime")
        for _, reason in manifest.deferred_hosted
    )
    hermetic_deferred = sum(
        reason.startswith("hermetic unit")
        for _, reason in manifest.deferred_hosted
    )
    return {
        "status": "pass",
        "tracked_translation_units": len(manifest.active_cases),
        "generated_translation_units": len(manifest.generated_cases),
        "total_translation_units": (
            len(manifest.active_cases) + len(manifest.generated_cases)
        ),
        "include_only_fragments": len(manifest.include_only),
        "delivered_non_root_headers": len(manifest.non_roots),
        "deferred_hosted_translation_units": len(manifest.deferred_hosted),
        "deferred_external_header_units": external_deferred,
        "deferred_hermetic_units": hermetic_deferred,
        "profiles": profiles,
    }


def _c_string_literal(value: str) -> str:
    pieces = ['"']
    for byte in value.encode("utf-8"):
        if byte == 0x22:
            pieces.append('\\"')
        elif byte == 0x5C:
            pieces.append("\\\\")
        elif byte == 0x3F:
            pieces.append("\\?")
        elif 0x20 <= byte <= 0x7E:
            pieces.append(chr(byte))
        else:
            pieces.append(f"\\{byte:03o}")
    pieces.append('"')
    return "".join(pieces)


def _render_c_preprocessor_active_cases(
    manifest: CPreprocessorActiveCasesManifest,
) -> str:
    groups = (
        [
            f"CUPIDC_PP_PROFILE({profile.name}, {profile.mode}, "
            f"{profile.gnu_extensions}, {profile.hosted_environment})"
            for profile in manifest.profiles
        ],
        [
            f"CUPIDC_PP_INCLUDE_ROOT({name}, {_c_string_literal(path)}, {forms})"
            for name, path, forms in manifest.include_roots
        ],
        [
            f"CUPIDC_PP_MACRO({profile}, {_c_string_literal(name)}, "
            f"{_c_string_literal(replacement)})"
            for profile, name, replacement in manifest.macros
        ],
        [
            f"CUPIDC_PP_FORCED_INCLUDE({profile}, {_c_string_literal(path)})"
            for profile, path in manifest.forced_includes
        ],
        [
            f"CUPIDC_PP_ACTIVE_CASE({profile}, {_c_string_literal(path)})"
            for profile, path in manifest.active_cases
        ],
        [
            f"CUPIDC_PP_GENERATED_CASE({profile}, {_c_string_literal(path)})"
            for profile, path in manifest.generated_cases
        ],
        [
            f"CUPIDC_PP_INCLUDE_ONLY({_c_string_literal(path)}, "
            f"{_c_string_literal(owner)})"
            for path, owner in manifest.include_only
        ],
        [
            f"CUPIDC_PP_NON_ROOT({_c_string_literal(path)}, "
            f"{_c_string_literal(reason)})"
            for path, reason in manifest.non_roots
        ],
        [
            f"CUPIDC_PP_DEFERRED_HOSTED({_c_string_literal(path)}, "
            f"{_c_string_literal(reason)})"
            for path, reason in manifest.deferred_hosted
        ],
    )
    lines = [
        "/* Checked active CupidC preprocessing cases.",
        " * Generated by tools/build_graph_audit.py; do not edit.",
        " * __GNUC__=1 is an active-source definedness compatibility marker,",
        " * not the version of the compiler hosting the bootstrap.",
        " */",
        "",
    ]
    for group in groups:
        lines.extend(group)
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


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
            if "tracked_translation_units" in contract:
                profile_counts = ", ".join(
                    f"{profile['name']}={profile['tracked_translation_units']}"
                    for profile in contract["profiles"]
                )
                detail = (
                    f"{contract['tracked_translation_units']} tracked + "
                    f"{contract['generated_translation_units']} generated "
                    f"translation units ({profile_counts}); "
                    f"{contract['include_only_fragments']} include-only, "
                    f"{contract['delivered_non_root_headers']} non-root headers; "
                    f"{contract['deferred_hosted_translation_units']} hosted "
                    f"deferred ({contract['deferred_external_header_units']} "
                    "external, "
                    f"{contract['deferred_hermetic_units']} hermetic)"
                )
            elif "expression_occurrences" in contract:
                detail = (
                    f"{contract['expression_occurrences']} conditional expressions "
                    f"({contract['if_occurrences']} #if, "
                    f"{contract['elif_occurrences']} #elif); "
                    f"{contract['unique_expressions']} normalized expressions; "
                    f"{contract['directive_expression_pairs']} "
                    "directive/expression pairs"
                )
            elif "named_line_occurrences" in contract:
                directive_word = (
                    "directive"
                    if contract["named_line_occurrences"] == 1
                    else "directives"
                )
                source_word = (
                    "file" if contract["source_files"] == 1 else "files"
                )
                detail = (
                    f"{contract['named_line_occurrences']} named #line "
                    f"{directive_word} ({contract['direct_line_occurrences']} "
                    f"direct, {contract['pp_token_line_occurrences']} pp-token; "
                    f"{contract['filename_occurrences']} filename); "
                    f"{contract['numeric_marker_occurrences']} numeric markers; "
                    f"{contract['source_files']} source {source_word}; "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "pp_token_operand_occurrences" in contract:
                detail = (
                    f"{contract['include_occurrences']} C include operands "
                    f"({contract['direct_quoted_occurrences']} quoted, "
                    f"{contract['direct_angle_occurrences']} angle, "
                    f"{contract['pp_token_operand_occurrences']} pp-token); "
                    f"{contract['source_files']} source files; "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "exe_occurrences" in contract:
                detail = (
                    f"{contract['block_occurrences']} Cupid #exe blocks "
                    f"({contract['ordinary_marker_occurrences']} #, "
                    f"{contract['digraph_marker_occurrences']} %:); "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "pragma_occurrences" in contract:
                detail = (
                    f"{contract['pragma_occurrences']} pragmas "
                    f"({contract['once_occurrences']} once, "
                    f"{contract['pack_push_occurrences']} pack pushes, "
                    f"{contract['pack_pop_occurrences']} pack pops); "
                    "pack balanced: "
                    f"{'yes' if contract['pack_balanced'] else 'no'}; "
                    f"max pack depth {contract['max_pack_depth']}"
                )
            else:
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
            "- Include reachability follows checked Make include paths, forced "
            "includes, "
            "quoted/angle C includes, and `%include`; the conditional contract records "
            "normalized source expressions while evaluation remains a "
            "compiler-contract responsibility.",
            "- Named `#line` pp-token operands are classified before macro expansion; "
            "the CupidC corpus harness owns expansion and semantic validation.",
            "- Relocation kinds and ABI values are required interchange contracts; "
            "per-object relocation counts are recorded in the chronological bootstrap log.",
            "- `not_reached` means absent from the supported roots recorded above, not "
            "automatically safe to delete.",
        ]
    )
    return "\n".join(lines) + "\n\n"


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
        "--c-preprocessor-active-cases",
        type=Path,
        help="generated checked X-macro manifest for active CupidC jobs",
    )
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
        active_cases_payload = (
            _render_c_preprocessor_active_cases(
                _c_preprocessor_active_cases_manifest(audit)
            )
            if args.c_preprocessor_active_cases
            else None
        )
    except AuditError as exc:
        print(f"build graph audit failed: {exc}", file=sys.stderr)
        return 2

    if args.check:
        stale = []
        if not _check_text(args.output, json_payload):
            stale.append(args.output)
        if args.summary and not _check_text(args.summary, markdown_payload or ""):
            stale.append(args.summary)
        if args.c_preprocessor_active_cases and not _check_text(
            args.c_preprocessor_active_cases, active_cases_payload or ""
        ):
            stale.append(args.c_preprocessor_active_cases)
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
    if args.c_preprocessor_active_cases and active_cases_payload is not None:
        _write_text_atomic(
            args.c_preprocessor_active_cases, active_cases_payload
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

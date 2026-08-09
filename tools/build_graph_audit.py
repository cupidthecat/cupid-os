"""Produce a deterministic inventory of Cupid OS build and language inputs."""

from __future__ import annotations

import argparse
import ast
import bisect
import collections
import hashlib
import json
import os
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

# Keep the checked graph independent of the host running the audit. The
# Windows branch stays canonical so platform-specific declarations remain
# visible, while the C locale keeps Make wildcard ordering stable. Direct
# Linux builds cover the Linux execution branch.
CANONICAL_MAKE_VARIABLES = ("OS=Windows_NT",)
TOOL_MARKERS = (
    ("build-iso --seed-manifest", "cupid_object"),
    ("image --seed-manifest", "cupid_object"),
    ("gen-big --seed-manifest", "cupid_assembler"),
    ("mksyms --seed-manifest", "cupid_disassembler"),
    ("mksyms --seed-manifest", "cupid_object"),
    ("embed-jpeg --seed-manifest", "cupid_object"),
    ("$(CUPIDDIS)", "cupid_disassembler"),
    ("$(CUPIDDIS)", "host_python"),
    ("$(CUPIDASM)", "cupid_assembler"),
    ("$(CUPIDASM)", "host_python"),
    ("$(CUPIDLD_USER_LINK)", "cupid_linker"),
    ("$(CUPIDLD_USER_LINK)", "host_python"),
    ("$(CUPIDLD)", "cupid_linker"),
    ("$(CUPIDLD)", "host_python"),
    ("$(CUPIDOBJ)", "cupid_object"),
    ("$(CUPIDOBJ)", "host_python"),
    ("$(CUPIDC_PRODUCTION_COMPILE)", "cupid_c_compiler"),
    ("$(CUPIDC_PRODUCTION_COMPILE)", "host_python"),
    ("$(CUPIDC_KERNEL_COMPILE)", "cupid_c_compiler"),
    ("$(CUPIDC_KERNEL_COMPILE)", "host_python"),
    ("$(CC)", "host_c_compiler"),
    ("$(ASM)", "nasm"),
    ("$(LD)", "host_linker"),
    ("$(OBJCOPY)", "host_object_copy"),
    ("$(NM)", "host_symbol_reader"),
    ("$(USER_SYSCALL_ABI)", "host_python"),
    ("$(PYTHON)", "host_python"),
    ("$(MAKE)", "make"),
)
USER_SYSCALL_ABI_AUDIT_INPUTS = (
    "tools/user_syscall_abi.py",
    "kernel/core/types.h",
    "kernel/core/syscall.h",
    "kernel/core/syscall.cc",
    "kernel/fs/vfs.h",
    "kernel/network/socket.h",
    "user/cupid.h",
)
CUPIDC_KERNEL_CONTROL_FILES = (
    "tools/cupidc_kernel_compile.py",
    "tools/kernel_cupidc_frontier.py",
    "tools/bootstrap_toolchain.py",
    "bootstrap/seeds/i386-linux/manifest.json",
)
_CUPIDOBJ_PROFILE_MANIFEST_OUTPUT = (
    "build/bootstrap/doom-cupidc-inputs.json"
)
_CUPIDOBJ_PROFILE_MANIFEST_RECIPE = [
    "$(PYTHON) tools/cupidc_kernel_compile.py --root . \\",
    "--manifest $(BOOTSTRAP_SEED_MANIFEST) \\",
    "--write-profile-input-manifest $@",
]
_CUPIDOBJ_PROFILE_MANIFEST_CONTROL_INPUTS = (
    "Makefile",
    "tools/bootstrap_toolchain.py",
    "bootstrap/seeds/i386-linux/manifest.json",
    "bootstrap/seeds/i386-linux/cupidasm.elf",
    "bootstrap/seeds/i386-linux/cupidc.elf",
    "bootstrap/seeds/i386-linux/cupiddis.elf",
    "bootstrap/seeds/i386-linux/cupidld.elf",
    "bootstrap/seeds/i386-linux/cupidobj.elf",
    "tools/cupidc_kernel_compile.py",
)
_CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_FILES = (
    "tools/cupidc_kernel_compile.py",
    "toolchain/cupidobj.cc",
    "CONTEXT.md",
    "docs/bootstrap/README.md",
)
_CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_DIRECTORIES = (
    "kernel/doom/src",
)
CUPIDC_PRODUCTION_CONTROL_FILES = (
    "tools/cupidc_production_compile.py",
    "tools/cupidc_production_frontier.py",
    "tools/cupidld_user_link.py",
    "tools/native_user_toolchain.py",
)
EXCLUDED_SOURCE_TREES = {".agents", ".git", "__pycache__", "build", "templeos"}

# These relations cannot be inferred from byte identity: the older sources have
# diverged from the active implementation or changed language/path. Keep the
# project-specific audit decisions explicit and validate the target against the
# active graph before reporting one.
KNOWN_SOURCE_RELATIONS = {
    "bin/cupidc.c": ("historical_copy_of", "kernel/lang/cupidc.cc"),
    "bin/cupidc_lex.c": ("historical_copy_of", "kernel/lang/cupidc_lex.cc"),
    "bin/cupidc_parse.c": ("historical_copy_of", "kernel/lang/cupidc_parse.cc"),
    "bin/fat16.c": ("historical_copy_of", "kernel/fs/fat16.cc"),
    "bin/fat16_vfs.c": ("historical_copy_of", "kernel/fs/fat16_vfs.cc"),
    "bin/kernel.c": ("historical_copy_of", "kernel/core/kernel.cc"),
    "bin/terminal_app.c": ("historical_copy_of", "kernel/gui/terminal_app.cc"),
    "demos/paint.cc": ("superseded_by", "bin/paint.cc"),
    "kernel/core/scheduler.c": ("superseded_by", "kernel/core/process.cc"),
    "kernel/core/scheduler.h": ("superseded_by", "kernel/core/process.h"),
    "kernel/gui/notepad.c": ("superseded_by", "bin/notepad.cc"),
    "kernel/gui/terminal_ansi.c": ("superseded_by", "kernel/gui/ansi.cc"),
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
    implicit_function_declarations: str
    compatibility_pointer_conversions: str


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
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="DOOM_COMPAT_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_TRUE",
        compatibility_pointer_conversions="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="DOOM_TREE_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_TRUE",
        compatibility_pointer_conversions="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="USER_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="FREESTANDING_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="CUPID_RUNTIME",
        mode="CTOOL_C_PP_MODE_CUPID",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_TOOLCHAIN_64",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_KERNEL_BRIDGE_64",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_LINUX",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_KERNEL_BRIDGE",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_LINUX_GNU",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
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
    "KERNEL_I386": 155,
    "DOOM_COMPAT_I386": 3,
    "DOOM_TREE_I386": 80,
    "USER_I386": 3,
    "FREESTANDING_I386": 1,
    "CUPID_RUNTIME": 105,
    "HOSTED_TOOLCHAIN_64": 0,
    "HOSTED_KERNEL_BRIDGE_64": 0,
    "HOSTED_I386_LINUX": 31,
    "HOSTED_I386_KERNEL_BRIDGE": 2,
    "HOSTED_I386_LINUX_GNU": 2,
}
_C_PP_HOSTED_I386_STRICT_CASES = (
    "/toolchain/ctool.cc",
    "/toolchain/ctool_host.cc",
    "/toolchain/cupidasm.cc",
    "/toolchain/cupidasm_main.cc",
    "/toolchain/cupidc_emit.cc",
    "/toolchain/cupidc_frontend.cc",
    "/toolchain/cupidc_ir.cc",
    "/toolchain/cupidc_main.cc",
    "/toolchain/cupidc_pp.cc",
    "/toolchain/cupidc_type.cc",
    "/toolchain/cupiddis.cc",
    "/toolchain/cupiddis_main.cc",
    "/toolchain/cupidld.cc",
    "/toolchain/cupidld_main.cc",
    "/toolchain/cupidobj.cc",
    "/toolchain/cupidobj_main.cc",
    "/toolchain/elf32.cc",
    "/toolchain/x86.cc",
)
_C_PP_HOSTED_I386_GNU_CASES = (
    "/toolchain/hosted/i386-linux/runtime.cc",
    "/toolchain/tests/hosted_i386_runtime_contract.cc",
)
_C_PP_TOOLCHAIN_CONTRACT_CASES = (
    "/kernel/lang/as_elf.cc",
    "/toolchain/tests/core_contract.cc",
    "/toolchain/tests/cupidasm_contract.cc",
    "/toolchain/tests/cupidasm_demos_contract.cc",
    "/toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "/toolchain/tests/cupidc_frontend_contract.cc",
    "/toolchain/tests/cupidc_ir_contract.cc",
    "/toolchain/tests/cupidc_object_contract.cc",
    "/toolchain/tests/cupidc_pp_contract.cc",
    "/toolchain/tests/cupidc_type_contract.cc",
    "/toolchain/tests/cupiddis_contract.cc",
    "/toolchain/tests/cupidld_contract.cc",
    "/toolchain/tests/cupidobj_contract.cc",
    "/toolchain/tests/elf32_contract.cc",
    "/toolchain/tests/hosted_i386_windows_contract.cc",
    "/toolchain/tests/x86_contract.cc",
)
_C_PP_GENERATED_KERNEL_CASES = (
    "/kernel/cpu/ksyms_data.cc",
    "/kernel/util/bin_programs_gen.cc",
    "/kernel/util/demos_programs_gen.cc",
    "/kernel/util/docs_programs_gen.cc",
)
_C_PP_NON_ROOT_HEADERS = (
    "/bin/fat16.h",
    "/bin/shell.h",
)
_C_PP_DEFERRED_HOSTED_CASES: tuple[str, ...] = ()
_C_PP_HOSTED_BRIDGE_CASES = frozenset(
    {
        "/kernel/lang/as_elf.cc",
        "/toolchain/tests/cupidasm_kernel_elf_contract.cc",
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


def _canonical_make_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    return environment


def _run_make_database(root: Path, make: str, target: str) -> str:
    # GNU Make executes recipes containing $(MAKE) even under -n.  Replace the
    # recursive command while printing the database so a missing hosted Cupid
    # tool cannot append a nested Makefile database and overwrite this root's
    # `all` rule during parsing.  Recipes remain unexpanded in `-p` output.
    result = subprocess.run(
        [
            make,
            *CANONICAL_MAKE_VARIABLES,
            "MAKE=:",
            "--no-print-directory",
            "-prRn",
            target,
        ],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=_canonical_make_environment(),
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(
            f"GNU Make could not expand target {target!r}: {detail}"
        )
    return result.stdout


def _audit_python_make_variable() -> str:
    executable = str(Path(sys.executable).resolve())
    shell_word = (
        subprocess.list2cmdline([executable])
        if sys.platform == "win32"
        else shlex.quote(executable)
    )
    return f"PYTHON={shell_word}"


def _read_make_json_list(root: Path, make: str, target: str) -> list[str]:
    result = subprocess.run(
        [
            make,
            *CANONICAL_MAKE_VARIABLES,
            _audit_python_make_variable(),
            "--no-print-directory",
            "-s",
            target,
        ],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=_canonical_make_environment(),
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
            *CANONICAL_MAKE_VARIABLES,
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
        env=_canonical_make_environment(),
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


def _validate_cupidc_kernel_compile_make_binding(
    root: Path,
    make: str,
    transforms: list[dict[str, object]],
) -> None:
    if not any(
        "cupid_c_compiler" in transform.get("tools", [])
        for transform in transforms
    ):
        return

    values = _read_evaluated_make_variables(
        root,
        make,
        ("PYTHON", "CUPIDC_KERNEL_COMPILE"),
    )
    try:
        python_tokens = shlex.split(values["PYTHON"])
        wrapper_tokens = shlex.split(values["CUPIDC_KERNEL_COMPILE"])
    except ValueError as error:
        raise AuditError(
            f"CupidC kernel wrapper Make binding cannot be tokenized: {error}"
        ) from error
    if not python_tokens:
        raise AuditError("CupidC kernel wrapper has an empty PYTHON binding")

    executable = Path(
        python_tokens[0].replace("\\", "/")
    ).name.lower()
    if executable.endswith(".exe"):
        executable = executable[:-4]
    if re.fullmatch(r"(?:py|python(?:[0-9]+(?:\.[0-9]+)*)?)", executable) is None:
        raise AuditError(
            "CupidC kernel wrapper PYTHON binding is not a Python launcher: "
            f"{python_tokens[0]!r}"
        )
    if len(python_tokens) != 1:
        raise AuditError(
            "CupidC kernel wrapper PYTHON binding must contain only the "
            f"Python launcher: found {python_tokens!r}"
        )

    expected = [
        python_tokens[0],
        "tools/cupidc_kernel_compile.py",
        "--root",
        ".",
    ]
    if wrapper_tokens != expected:
        raise AuditError(
            "CupidC kernel wrapper Make binding differs from the checked "
            f"command: expected {expected!r}, found {wrapper_tokens!r}"
        )


def _python_make_tokens(value: str, label: str) -> list[str]:
    try:
        tokens = shlex.split(value)
    except ValueError as error:
        raise AuditError(f"{label} cannot be tokenized: {error}") from error
    if len(tokens) != 1:
        raise AuditError(
            f"{label} must contain only a Python launcher: {tokens!r}"
        )
    executable = Path(tokens[0].replace("\\", "/")).name.lower()
    if executable.endswith(".exe"):
        executable = executable[:-4]
    if re.fullmatch(
        r"(?:py|python(?:[0-9]+(?:\.[0-9]+)*)?)", executable
    ) is None:
        raise AuditError(f"{label} is not a Python launcher: {tokens[0]!r}")
    return tokens


def _validate_cupidc_production_make_bindings(
    root: Path,
    make: str,
    models: list[BuildModel],
) -> None:
    for model in models:
        recipe_text = "\n".join(
            line
            for transform in model.transforms
            for line in transform.get("recipe", [])
            if isinstance(line, str)
        )
        uses_compile = "$(CUPIDC_PRODUCTION_COMPILE)" in recipe_text
        uses_link = "$(CUPIDLD_USER_LINK)" in recipe_text
        if not uses_compile and not uses_link:
            continue
        make_root = root if model.directory == "." else root / model.directory
        variables = ["PYTHON"]
        if uses_compile:
            variables.append("CUPIDC_PRODUCTION_COMPILE")
        if uses_link:
            variables.append("CUPIDLD_USER_LINK")
        values = _read_evaluated_make_variables(
            make_root, make, tuple(variables)
        )
        python_tokens = _python_make_tokens(
            values["PYTHON"],
            f"CupidC production PYTHON binding in {model.directory}",
        )
        if model.directory == ".":
            expected_compile = [
                python_tokens[0],
                "tools/cupidc_production_compile.py",
                "--root",
                ".",
                "--cohort",
                "generated-install",
            ]
            if uses_link:
                raise AuditError(
                    "checked user linker may not run from the root build"
                )
        elif model.directory == "user":
            expected_compile = [
                python_tokens[0],
                "../tools/cupidc_production_compile.py",
                "--root",
                "..",
                "--cohort",
                "user",
            ]
        else:
            raise AuditError(
                "CupidC production wrapper is bound in an unsupported "
                f"Make root: {model.directory}"
            )
        if uses_compile:
            try:
                actual_compile = shlex.split(
                    values["CUPIDC_PRODUCTION_COMPILE"]
                )
            except ValueError as error:
                raise AuditError(
                    "CupidC production wrapper binding cannot be tokenized: "
                    f"{error}"
                ) from error
            if actual_compile != expected_compile:
                raise AuditError(
                    "CupidC production wrapper binding differs from the "
                    f"checked command in {model.directory}: "
                    f"expected={expected_compile!r}, "
                    f"actual={actual_compile!r}"
                )
        if uses_link:
            if model.directory != "user":
                raise AuditError(
                    "checked user linker is outside the user build"
                )
            expected_link = [
                python_tokens[0],
                "../tools/cupidld_user_link.py",
                "--root",
                "..",
            ]
            try:
                actual_link = shlex.split(values["CUPIDLD_USER_LINK"])
            except ValueError as error:
                raise AuditError(
                    "CupidLD user wrapper binding cannot be tokenized: "
                    f"{error}"
                ) from error
            if actual_link != expected_link:
                raise AuditError(
                    "CupidLD user wrapper binding differs from the checked "
                    f"command: expected={expected_link!r}, "
                    f"actual={actual_link!r}"
                )


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


def _recipe_tokens(recipe: list[str]) -> list[str]:
    return [
        token
        for token in "\n".join(recipe).split()
        if token != "\\"
    ]


def _tools_for_recipe(recipe: list[str]) -> list[str]:
    tokens = _recipe_tokens(recipe)
    joined = " ".join(tokens)
    tools = []
    if "--write-profile-input-manifest" in tokens:
        tools.append("cupid_object")
    for marker, tool in TOOL_MARKERS:
        if marker in joined and tool not in tools:
            tools.append(tool)
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
    inputs: list[str],
) -> str:
    joined = " ".join(recipe).lower()
    if (
        "cupid_object" in tools
        and "--write-profile-input-manifest"
        in (token.lower() for token in _recipe_tokens(recipe))
    ):
        return "generate_profile_manifest"
    if "hostbuild.py build-iso " in joined:
        return "package_iso9660_image"
    if "hostbuild.py image " in joined:
        return "package_disk_image"
    if (
        "gen-big" in joined
        and "--seed-manifest" in joined
        and "cupid_assembler" in tools
    ):
        return "assemble_flat_binary"
    if " mksyms " in f" {joined} " and "cupid_object" in tools:
        return "generate_ksyms_source"
    if any(
        posixpath.normpath(path.replace("\\", "/")).endswith(
            "tools/user_syscall_abi.py"
        )
        for path in inputs
    ):
        return "verify_user_syscall_abi"
    if "host_c_compiler" in tools or "cupid_c_compiler" in tools:
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
        install_source_command = re.compile(
            r"^[@+-]*\$\(cupidobj\)\s+install-source(?:\s|$)"
        )
        if any(
            install_source_command.search(command.strip().lower())
            for command in recipe
        ):
            return "generate_install_source"
        if re.search(r"(?:^|\s)wrap-text(?:\s|$)", joined):
            return "wrap_text_as_elf32_relocatable"
        if (
            ("-i binary" in joined and "-o elf32-i386" in joined)
            or re.search(r"(?:^|\s)wrap(?:\s|$)", joined)
            or re.search(r"(?:^|\s)embed-jpeg(?:\s|$)", joined)
        ):
            return "wrap_binary_as_elf32_relocatable"
        if re.search(r"(?:^|\s)-o\s+binary(?:\s|$)", joined) or re.search(
            r"(?:^|\s)flat(?:\s|$)", joined
        ):
            return "extract_raw_binary"
        return "transform_object"
    if "host_python" in tools:
        if " mksyms " in joined or (
            " gen-" in joined
            and output.lower().endswith((".c", ".cc"))
        ):
            return "generate_c_source"
        if " gen-" in joined:
            return "generate_binary_fixture"
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
                    rule.prerequisites,
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
        if path == "kernel/cpu/ksyms_data.cc":
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
        "cupiddis.cc",
        "cupiddis.h",
        "cupiddis_main.cc",
    }:
        return "cupiddis"
    if path.startswith("toolchain/") and basename in {
        "cupidasm.cc",
        "cupidasm.h",
        "cupidasm_main.cc",
    }:
        return "cupidasm"
    if path.startswith("toolchain/"):
        return "toolchain_core"
    if path.startswith("kernel/lang/") and basename.startswith("ctool_kernel"):
        return "toolchain_kernel_adapter"
    if path.startswith("kernel/lang/") and basename.startswith("cupidc"):
        return "cupidc"
    if path.startswith("kernel/lang/") and (
        basename in {"as.c", "as.cc", "as.h"} or basename.startswith("as_")
    ):
        return "cupidasm"
    if path.startswith("kernel/lang/") and basename in {
        "dis.c",
        "dis.cc",
        "dis.h",
    }:
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
            (
                "c.output.elf32_relocatable",
                "cupid_c.output.elf32_relocatable",
                "asm.output.elf32_relocatable",
            ),
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
            "Keep the four boot and kernel transforms plus the ISO lane fixture CupidASM-owned while retaining NASM only as an optional parity oracle.",
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
            "Keep the checked-seed CupidC and CupidLD user build reproducible on Linux and Windows, keep the native Windows oracle explicit, then stage its validated executables deliberately.",
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
    if any(
        "cupid_c_compiler" in transform.get("tools", [])
        for model in models
        for transform in model.transforms
    ):
        for relative in CUPIDC_KERNEL_CONTROL_FILES:
            path = root / relative
            if not path.is_file():
                raise AuditError(
                    "CupidC ownership control file is missing: "
                    f"{relative}"
                )
            control_files.append(
                {
                    "path": relative,
                    "sha256": _source_digest(path),
                }
            )
    if any(
        any(
            marker in "\n".join(transform.get("recipe", []))
            for marker in (
                "$(CUPIDC_PRODUCTION_COMPILE)",
                "$(CUPIDLD_USER_LINK)",
            )
        )
        for model in models
        for transform in model.transforms
    ):
        for relative in CUPIDC_PRODUCTION_CONTROL_FILES:
            path = root / relative
            if not path.is_file():
                raise AuditError(
                    "CupidC production ownership control file is missing: "
                    f"{relative}"
                )
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
            elif (
                language == "cupid_c"
                and operation == "compile_c_to_elf32_object"
            ):
                feature_id = "cupid_c.output.elf32_relocatable"
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


_CHECKED_SEED_RUNNER_FILES = (
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_kernel_compile.py",
    "tools/cupidc_production_compile.py",
    "tools/cupidld_user_link.py",
)


def _is_checked_seed_runner_production_root(root: Path) -> bool:
    return all(
        (root / relative).is_file()
        for relative in (
            "Makefile",
            "toolchain/cupidobj.cc",
            "bootstrap/seeds/i386-linux/manifest.json",
        )
    ) and all(
        (root / relative).is_dir()
        for relative in (
            "kernel/doom/src",
            "user/examples",
        )
    )


def _read_checked_seed_runner_module(
    root: Path,
    relative: str,
) -> ast.Module:
    path = root / relative
    try:
        return ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    except (OSError, SyntaxError) as error:
        raise AuditError(
            f"checked-seed runner contract file is unavailable: {relative}: "
            f"{error}"
        ) from error


def _checked_seed_function(
    tree: ast.Module,
    name: str,
    relative: str,
) -> ast.FunctionDef:
    functions = [
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == name
    ]
    if len(functions) != 1:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"expected one {name} function"
        )
    function = functions[0]
    if function.decorator_list or any(
        isinstance(node, (ast.Yield, ast.YieldFrom))
        for node in ast.walk(function)
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"{name} is decorated or yields instead of running directly"
        )
    return function


def _validate_shared_seed_runner(
    tree: ast.Module,
    relative: str,
) -> None:
    function = _checked_seed_function(tree, "run_seed_tool", relative)
    provider_rebindings = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Name)
        and isinstance(node.ctx, (ast.Store, ast.Del))
        and node.id == "run_seed_tool"
    ]
    provider_namespace_mutations = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and (
            (
                isinstance(node.func, ast.Name)
                and node.func.id
                in {
                    "globals",
                    "locals",
                    "exec",
                    "eval",
                    "setattr",
                    "delattr",
                    "__import__",
                }
            )
            or (
                isinstance(node.func, ast.Attribute)
                and node.func.attr
                in {"__setitem__", "__setattr__", "__delitem__"}
            )
        )
    ]
    if provider_rebindings or provider_namespace_mutations:
        raise AuditError(
            "checked-seed runner contract changed: exported runner authority "
            "is rebound dynamically"
        )
    keyword_defaults = {
        argument.arg: default
        for argument, default in zip(
            function.args.kwonlyargs,
            function.args.kw_defaults,
            strict=True,
        )
    }
    runner_default = keyword_defaults.get("runner")
    if not (
        isinstance(runner_default, ast.Constant)
        and runner_default.value is None
    ):
        raise AuditError(
            "checked-seed runner contract changed: runner is not one "
            "optional keyword-only injection"
        )

    nested = [
        statement
        for statement in function.body
        if isinstance(statement, ast.FunctionDef)
        and statement.name == "run_frozen"
    ]
    if len(nested) != 1:
        raise AuditError(
            "checked-seed runner contract changed: frozen execution helper "
            "is not unique"
        )
    run_frozen = nested[0]
    if run_frozen.decorator_list or any(
        isinstance(node, (ast.Yield, ast.YieldFrom))
        for node in ast.walk(run_frozen)
    ):
        raise AuditError(
            "checked-seed runner contract changed: frozen execution helper "
            "is decorated or yields"
        )
    body = run_frozen.body
    returns = [
        node for node in ast.walk(run_frozen) if isinstance(node, ast.Return)
    ]
    if len(returns) != 1 or returns[0] not in body:
        raise AuditError(
            "checked-seed runner contract changed: frozen execution has an "
            "alternate return path"
        )
    run_parents = {
        child: parent
        for parent in ast.walk(run_frozen)
        for child in ast.iter_child_nodes(parent)
    }
    seed_input_names = [
        node
        for node in ast.walk(run_frozen)
        if isinstance(node, ast.Name) and node.id == "seed_inputs"
    ]
    seed_input_load_parents = sorted(
        ast.unparse(run_parents[node])
        for node in seed_input_names
        if isinstance(node.ctx, ast.Load)
    )
    if seed_input_load_parents != [
        "seed_inputs.manifest_sha256",
        "seed_inputs.tools",
    ] or any(
        isinstance(node.ctx, (ast.Store, ast.Del)) for node in seed_input_names
    ):
        raise AuditError(
            "checked-seed runner contract changed: frozen seed capture is "
            "read or mutated outside its executable and manifest checks"
        )

    tool_runs = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "run"
    ]
    if (
        len(tool_runs) != 1
        or ast.unparse(tool_runs[0].func) != "active_runner.run"
    ):
        raise AuditError(
            "checked-seed runner contract changed: frozen dispatch has an "
            "unchecked tool execution path"
        )

    frozen_branches = [
        statement
        for statement in function.body
        if isinstance(statement, ast.If)
        and ast.unparse(statement.test) == "frozen_seed is not None"
    ]
    if not (
        len(frozen_branches) == 1
        and not frozen_branches[0].orelse
        and len(frozen_branches[0].body) == 1
        and isinstance(frozen_branches[0].body[0], ast.Return)
        and ast.unparse(frozen_branches[0].body[0].value)
        == "run_frozen(frozen_seed)"
    ):
        raise AuditError(
            "checked-seed runner contract changed: supplied frozen capture "
            "does not enter the checked helper directly"
        )
    frozen_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and ast.unparse(node.func) == "run_frozen"
    ]
    if sorted(ast.unparse(call) for call in frozen_calls) != [
        "run_frozen(freeze_seed_inputs(manifest_path, Path(temporary)))",
        "run_frozen(frozen_seed)",
    ]:
        raise AuditError(
            "checked-seed runner contract changed: frozen capture dispatch "
            "has another route"
        )
    fallback_paths = [
        statement
        for statement in function.body
        if isinstance(statement, ast.With)
        and len(statement.items) == 1
        and ast.unparse(statement.items[0].context_expr).startswith(
            "tempfile.TemporaryDirectory("
        )
    ]
    if not (
        len(fallback_paths) == 1
        and len(fallback_paths[0].body) == 1
        and isinstance(fallback_paths[0].body[0], ast.Return)
        and ast.unparse(fallback_paths[0].body[0].value)
        == "run_frozen(freeze_seed_inputs(manifest_path, Path(temporary)))"
    ):
        raise AuditError(
            "checked-seed runner contract changed: new seed capture does not "
            "enter the checked helper directly"
        )

    def store_count(name: str) -> int:
        return sum(
            isinstance(node, ast.Name)
            and isinstance(node.ctx, ast.Store)
            and node.id == name
            for node in ast.walk(run_frozen)
        )

    for name in ("executable", "active_runner", "result", "live_seed"):
        if store_count(name) != 1:
            raise AuditError(
                "checked-seed runner contract changed: "
                f"{name} is not one immutable execution binding"
            )

    def direct_index(predicate) -> int:
        matches = [
            index
            for index, statement in enumerate(body)
            if predicate(statement)
        ]
        if len(matches) != 1:
            raise AuditError(
                "checked-seed runner contract changed: execution and live "
                "cohort checks are not one ordered path"
            )
        return matches[0]

    executable_index = direct_index(
        lambda statement: isinstance(statement, ast.Try)
        and len(statement.body) == 1
        and ast.unparse(statement.body[0])
        == "executable = seed_inputs.tools[tool_name]"
    )
    runner_index = direct_index(
        lambda statement: isinstance(statement, ast.Assign)
        and ast.unparse(statement)
        == "active_runner = runner if runner is not None else ToolRunner(root)"
    )
    execution_index = direct_index(
        lambda statement: isinstance(statement, ast.Try)
        and len(statement.body) == 1
        and ast.unparse(statement.body[0])
        == "result = active_runner.run(executable, arguments, timeout)"
    )
    live_index = direct_index(
        lambda statement: isinstance(statement, ast.Try)
        and len(statement.body) == 1
        and ast.unparse(statement.body[0])
        == "live_seed = _load_seed_inputs(manifest_path, None)"
    )
    comparison_index = direct_index(
        lambda statement: isinstance(statement, ast.If)
        and ast.unparse(statement.test)
        == "live_seed.manifest_sha256 != seed_inputs.manifest_sha256"
        and not statement.orelse
        and len(statement.body) == 1
        and isinstance(statement.body[0], ast.Raise)
        and isinstance(statement.body[0].exc, ast.Call)
        and ast.unparse(statement.body[0].exc.func) == "BootstrapError"
    )
    return_index = direct_index(
        lambda statement: isinstance(statement, ast.Return)
        and ast.unparse(statement.value) == "result"
    )
    for checked_try in (body[execution_index], body[live_index]):
        if any(
            not handler.body or not isinstance(handler.body[-1], ast.Raise)
            for handler in checked_try.handlers
        ):
            raise AuditError(
                "checked-seed runner contract changed: execution or live "
                "cohort failure does not raise"
            )
    if not (
        executable_index
        < runner_index
        < execution_index
        < live_index
        < comparison_index
        < return_index
    ):
        raise AuditError(
            "checked-seed runner contract changed: live cohort validation "
            "does not precede success"
        )


def _validate_checked_seed_wrapper(
    tree: ast.Module,
    relative: str,
    function_name: str,
    tool_name: str,
    runner_name: str,
    publication_call: str,
    native_split: bool,
) -> None:
    dynamic_namespace_calls = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and (
            (
                isinstance(node.func, ast.Name)
                and node.func.id
                in {
                    "globals",
                    "locals",
                    "exec",
                    "eval",
                    "setattr",
                    "delattr",
                    "__import__",
                }
            )
            or (
                isinstance(node.func, ast.Attribute)
                and node.func.attr
                in {"__setitem__", "__setattr__", "__delitem__"}
            )
        )
    ]
    expected_imports = ["bootstrap_toolchain", "tools.bootstrap_toolchain"]

    def imported_modules(name: str) -> list[str | None]:
        return [
            node.module
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
            for imported in node.names
            if imported.name == name and imported.asname is None
        ]

    protected_names = {"run_seed_tool", "freeze_seed_inputs"}
    protected_rebindings = [
        node
        for node in ast.walk(tree)
        if (
            isinstance(node, ast.Name)
            and isinstance(node.ctx, (ast.Store, ast.Del))
            and node.id in protected_names
        )
        or (
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
            and node.name in protected_names
        )
    ]
    if (
        sorted(imported_modules("run_seed_tool")) != expected_imports
        or sorted(imported_modules("freeze_seed_inputs")) != expected_imports
        or protected_rebindings
        or dynamic_namespace_calls
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "seed capture or runner is not the shared imported authority"
        )
    function = _checked_seed_function(tree, function_name, relative)
    if any(
        isinstance(node, (ast.Return, ast.Yield, ast.YieldFrom))
        for node in ast.walk(function)
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "production wrapper has an early return or generator yield"
        )
    calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "run_seed_tool"
    ]
    if len(calls) != 1:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"{function_name} does not delegate exactly once"
        )
    call = calls[0]
    expected_arguments = ["manifest_path", "root", repr(tool_name), "arguments"]
    if [ast.unparse(argument) for argument in call.args] != expected_arguments:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "tool invocation arguments differ"
        )
    keywords = {
        keyword.arg: ast.unparse(keyword.value)
        for keyword in call.keywords
        if keyword.arg is not None
    }
    if keywords != {
        "timeout": "timeout",
        "frozen_seed": "seed_inputs",
        "runner": runner_name,
    }:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "caller-owned capture or runner forwarding differs"
        )

    parents = {
        child: parent
        for parent in ast.walk(function)
        for child in ast.iter_child_nodes(parent)
    }
    assignment = parents.get(call)
    if not (
        isinstance(assignment, ast.Assign)
        and len(assignment.targets) == 1
        and ast.unparse(assignment.targets[0]) == "result"
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "checked result is not the accepted tool result"
        )

    result_store_count = sum(
        isinstance(node, ast.Name)
        and isinstance(node.ctx, ast.Store)
        and node.id == "result"
        for node in ast.walk(function)
    )
    expected_result_stores = 2 if native_split else 1
    if result_store_count != expected_result_stores:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "tool result has an unchecked replacement binding"
        )

    execution_try: ast.Try | None = None
    if native_split:
        branch = parents.get(assignment)
        if not (
            isinstance(branch, ast.If)
            and ast.unparse(branch.test) == "native_snapshot is not None"
            and assignment in branch.orelse
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "native and checked execution paths are not distinct"
            )
        branch_parent = parents.get(branch)
        if not (
            isinstance(branch_parent, ast.Try)
            and branch in branch_parent.body
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "checked execution is nested below another control path"
            )
        execution_try = branch_parent
    else:
        assignment_parent = parents.get(assignment)
        if not (
            isinstance(assignment_parent, ast.Try)
            and assignment in assignment_parent.body
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "checked execution is not the direct kernel execution path"
            )
        execution_try = assignment_parent

    current = parents.get(execution_try)
    while current is not None and current is not function:
        if isinstance(
            current,
            (
                ast.If,
                ast.For,
                ast.AsyncFor,
                ast.While,
                ast.Match,
                ast.ExceptHandler,
                ast.Lambda,
                ast.FunctionDef,
                ast.AsyncFunctionDef,
            ),
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "checked execution is conditionally unreachable"
            )
        current = parents.get(current)

    freeze_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "freeze_seed_inputs"
    ]
    publication_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and ast.unparse(node.func) == publication_call
    ]
    publication_references = [
        node
        for node in ast.walk(function)
        if isinstance(node, (ast.Name, ast.Attribute))
        and ast.unparse(node) == publication_call
    ]
    alternate_publications = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and node not in publication_calls
        and ast.unparse(node) != "temporary_input.write_bytes(source_payload)"
        and (
            (
                isinstance(node.func, ast.Name)
                and node.func.id == "open"
            )
            or (
                isinstance(node.func, ast.Attribute)
                and node.func.attr
                in {
                    "replace",
                    "rename",
                    "move",
                    "copy",
                    "copy2",
                    "copyfile",
                    "write",
                    "write_bytes",
                    "write_text",
                    "writelines",
                    "link",
                    "symlink",
                    "hardlink_to",
                    "symlink_to",
                    "touch",
                }
            )
        )
    ]
    seed_store_count = sum(
        isinstance(node, ast.Name)
        and isinstance(node.ctx, ast.Store)
        and node.id == "seed_inputs"
        for node in ast.walk(function)
    )
    seed_loads = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Name)
        and isinstance(node.ctx, ast.Load)
        and node.id == "seed_inputs"
    ]
    freeze_assignment = (
        parents.get(freeze_calls[0]) if len(freeze_calls) == 1 else None
    )
    if (
        len(freeze_calls) != 1
        or not isinstance(freeze_assignment, ast.Assign)
        or len(freeze_assignment.targets) != 1
        or ast.unparse(freeze_assignment.targets[0]) != "seed_inputs"
        or seed_store_count != 1
        or len(seed_loads) != 1
        or not isinstance(parents.get(seed_loads[0]), ast.keyword)
        or parents[seed_loads[0]].arg != "frozen_seed"
        or len(publication_calls) != 1
        or len(publication_references) != 1
        or publication_references[0] is not publication_calls[0].func
        or alternate_publications
        or not freeze_calls[0].lineno < call.lineno < publication_calls[0].lineno
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "freeze, execution, and publication order differs"
        )

    publication_expression = parents.get(publication_calls[0])
    if not (
        isinstance(publication_expression, ast.Expr)
        and publication_expression.value is publication_calls[0]
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "publication is not one direct statement"
        )
    for checked_try in (
        node for node in ast.walk(function) if isinstance(node, ast.Try)
    ):
        if any(
            not handler.body or not isinstance(handler.body[-1], ast.Raise)
            for handler in checked_try.handlers
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "production wrapper suppresses a transaction failure"
            )
    child: ast.AST = publication_expression
    current = parents.get(child)
    while current is not None and current is not function:
        if isinstance(current, ast.Try) and child not in current.body:
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication is outside the successful try body"
            )
        if isinstance(
            current,
            (
                ast.If,
                ast.For,
                ast.AsyncFor,
                ast.While,
                ast.Match,
                ast.ExceptHandler,
                ast.Lambda,
                ast.FunctionDef,
                ast.AsyncFunctionDef,
            ),
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication is conditionally reachable"
            )
        child = current
        current = parents.get(current)


def _validate_checked_seed_runner_contract(root: Path) -> None:
    missing = [
        relative
        for relative in _CHECKED_SEED_RUNNER_FILES
        if not (root / relative).is_file()
    ]
    if missing:
        raise AuditError(
            f"checked-seed runner contract files are missing: {missing!r}"
        )
    trees = {
        relative: _read_checked_seed_runner_module(root, relative)
        for relative in _CHECKED_SEED_RUNNER_FILES
    }
    _validate_shared_seed_runner(
        trees["tools/bootstrap_toolchain.py"],
        "tools/bootstrap_toolchain.py",
    )
    _validate_checked_seed_wrapper(
        trees["tools/cupidc_kernel_compile.py"],
        "tools/cupidc_kernel_compile.py",
        "compile_kernel_source",
        "cupidc",
        "executor",
        "_replace_with_retry",
        False,
    )
    _validate_checked_seed_wrapper(
        trees["tools/cupidc_production_compile.py"],
        "tools/cupidc_production_compile.py",
        "compile_production_source",
        "cupidc",
        "active_executor",
        "os.replace",
        True,
    )
    _validate_checked_seed_wrapper(
        trees["tools/cupidld_user_link.py"],
        "tools/cupidld_user_link.py",
        "link_user_program",
        "cupidld",
        "active_runner",
        "os.replace",
        True,
    )


def build_audit(
    root: Path,
    make: str,
    target: str,
    supplemental_builds: list[tuple[str, str]] | None = None,
) -> dict[str, object]:
    root = root.resolve()
    if _is_checked_seed_runner_production_root(root):
        _validate_checked_seed_runner_contract(root)
    root_model = _collect_build_model(root, make, target, ".")
    supplemental_models = [
        _collect_build_model(root, make, supplemental_target, directory)
        for directory, supplemental_target in (supplemental_builds or [])
    ]
    models = [root_model, *supplemental_models]
    _validate_cupidobj_profile_manifest_delivery(
        root,
        root_model.transforms,
    )
    _validate_cupidc_kernel_compile_make_binding(
        root,
        make,
        root_model.transforms,
    )
    _validate_cupidc_production_make_bindings(root, make, models)

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
            language in {"c", "c_header"}
            and "cupid_c_compiler" in owners
        ):
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
        contracts["cupid_toolchain_fixed_point"] = (
            _cupid_toolchain_fixed_point_contract(root)
        )
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
    for index, token in enumerate(tokens):
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
        elif token == "-x":
            argument_profile.append(token)
        elif index > 0 and tokens[index - 1] == "-x":
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
    roots = [
        path
        for path in inputs
        if _language(path) in {"c", "cupid_c"}
    ]
    if len(roots) != 1:
        rendered = ", ".join(roots) if roots else "<none>"
        raise AuditError(
            f"CupidC active preprocessing expected exactly one "
            f"C translation-unit root for {output}; "
            f"found {len(roots)}: {rendered}"
        )
    return roots[0]


def _c_preprocessor_profile_for_c_transform(
    directory: str, transform: dict[str, object]
) -> str:
    output = str(transform.get("output", "<unknown>"))
    recipe_tokens = _c_preprocessor_compile_recipe_tokens(transform)
    if transform.get("tools") == ["cupid_c_compiler", "host_python"]:
        markers = _c_preprocessor_recipe_markers(
            transform,
            {"CUPIDC_KERNEL_COMPILE", "CUPIDC_PRODUCTION_COMPILE"},
        )
        root = _c_preprocessor_one_c_root(transform)
        if "CUPIDC_PRODUCTION_COMPILE" in markers:
            expected_markers = collections.Counter(
                {"CUPIDC_PRODUCTION_COMPILE": 1}
            )
            if directory == ".":
                if root not in {
                    "kernel/util/bin_programs_gen.cc",
                    "kernel/util/demos_programs_gen.cc",
                    "kernel/util/docs_programs_gen.cc",
                }:
                    raise AuditError(
                        "CupidC generated-install wrapper found an "
                        f"unapproved root: {root}"
                    )
                source_argument = "$<"
                output_argument = "$@"
                profile = "KERNEL_I386"
            elif directory == "user":
                if root not in {
                    "user/examples/cat.cc",
                    "user/examples/hello.cc",
                    "user/examples/ls.cc",
                }:
                    raise AuditError(
                        "CupidC user wrapper found an unapproved root: "
                        f"{root}"
                    )
                source_argument = "user/$<"
                output_argument = "user/$@"
                profile = "USER_I386"
            else:
                raise AuditError(
                    "CupidC production wrapper is outside an approved "
                    f"build root for {output}: {directory!r}"
                )
            expected_tokens = [
                "$(CUPIDC_PRODUCTION_COMPILE)",
                "--source",
                source_argument,
                "--output",
                output_argument,
            ]
        else:
            expected_markers = collections.Counter(
                {"CUPIDC_KERNEL_COMPILE": 1}
            )
            if directory != ".":
                raise AuditError(
                    "CupidC kernel compile wrapper is outside the root "
                    f"build for {output}: {directory!r}"
                )
            doom_compat_roots = {
                "kernel/doom/dglibc.cc",
                "kernel/doom/doom_libc_stubs.cc",
                "kernel/doom/doomgeneric_cupidos.cc",
            }
            if root in doom_compat_roots:
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--profile",
                    "doom-compat",
                    "--source",
                    root,
                    "--output",
                    output,
                ]
                profile = "DOOM_COMPAT_I386"
            elif root == "kernel/doom/i_sound_cupidos.cc":
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--profile",
                    "doom-tree",
                    "--source",
                    root,
                    "--output",
                    output,
                ]
                profile = "DOOM_TREE_I386"
            elif root.startswith("kernel/doom/src/"):
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--profile",
                    "doom-tree",
                    "--source",
                    "$<",
                    "--output",
                    "$@",
                ]
                profile = "DOOM_TREE_I386"
            else:
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--source",
                    root,
                    "--output",
                    output,
                ]
                profile = "KERNEL_I386"
        if markers != expected_markers:
            raise AuditError(
                "CupidC compile wrapper markers differ for "
                f"{output}: expected={dict(expected_markers)!r}, "
                f"actual={dict(sorted(markers.items()))!r}"
            )
        if recipe_tokens != expected_tokens:
            raise AuditError(
                "CupidC compile wrapper arguments differ for "
                f"{output}: expected={expected_tokens!r}, "
                f"actual={recipe_tokens!r}"
            )
        return profile
    if directory == ".":
        markers = _c_preprocessor_recipe_markers(
            transform,
            {
                "CC",
                "CFLAGS",
                "CFLAGS_DOOM",
                "CFLAGS_DOOM_TREE",
                "OPT",
            },
        )
        profiles = {
            "CFLAGS": "KERNEL_I386",
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
        unexpected_literal_flags = [
            flag
            for flag in literal_flags
            if flag not in {"-I../kernel/lang", "-x"}
        ]
        if unexpected_literal_flags:
            raise AuditError(
                f"CupidC active preprocessing found literal preprocessor "
                f"flag(s) outside the selected profile for {output}: "
                f"{unexpected_literal_flags!r}"
            )
        logical = _c_preprocessor_logical_path(
            _c_preprocessor_one_c_root(transform)
        )
        bridge_source = logical in _C_PP_HOSTED_BRIDGE_CASES
        expected_literal_flags = (
            ["-I../kernel/lang", "-x"]
            if bridge_source and logical.endswith(".cc")
            else ["-I../kernel/lang"]
            if bridge_source
            else ["-x"]
            if logical.endswith(".cc")
            else []
        )
        if literal_flags != expected_literal_flags:
            raise AuditError(
                f"CupidC active preprocessing hosted bridge recipe differs "
                f"for {logical}: expected_bridge={bridge_source!r}, "
                f"actual_flags={literal_flags!r}"
            )
        expected_argument_profile = ["CC", "CPPFLAGS"]
        if bridge_source:
            expected_argument_profile.append("-I../kernel/lang")
        expected_argument_profile.append("CFLAGS")
        if logical.endswith(".cc"):
            expected_argument_profile.extend(("-x", "c"))
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
                "HOSTED_I386_KERNEL_BRIDGE",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
                "/kernel/lang",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
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
            ("FREESTANDING_I386", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_TOOLCHAIN_64", "__SIZEOF_POINTER__", "8"),
            ("HOSTED_KERNEL_BRIDGE_64", "__SIZEOF_POINTER__", "8"),
            ("HOSTED_I386_LINUX", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_I386_KERNEL_BRIDGE", "__SIZEOF_POINTER__", "4"),
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
    contract_path = root / "toolchain" / "tests" / "cupidc_object_contract.cc"
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


def _cupid_toolchain_fixed_point_contract(
    root: Path,
) -> dict[str, object]:
    test_path = root / "tests" / "test_toolchain_cupidc_object.py"
    driver_path = root / "toolchain" / "cupidc_main.cc"
    linker_header_path = root / "toolchain" / "cupidld.h"
    linker_cli_path = root / "toolchain" / "cupidld_main.cc"
    linker_core_path = root / "toolchain" / "cupidld.cc"
    bootstrap_path = root / "tools" / "bootstrap_toolchain.py"
    contract_publisher_path = (
        root / "tools" / "cupidc_toolchain_contracts.py"
    )
    try:
        test_source = test_path.read_text(encoding="utf-8")
        driver_source = driver_path.read_text(encoding="utf-8")
        linker_header_source = linker_header_path.read_text(encoding="utf-8")
        linker_cli_source = linker_cli_path.read_text(encoding="utf-8")
        linker_core_source = linker_core_path.read_text(encoding="utf-8")
        bootstrap_source = bootstrap_path.read_text(encoding="utf-8")
        contract_publisher_source = contract_publisher_path.read_text(
            encoding="utf-8"
        )
        test_tree = ast.parse(test_source, filename=str(test_path))
        bootstrap_tree = ast.parse(
            bootstrap_source, filename=str(bootstrap_path)
        )
        contract_publisher_tree = ast.parse(
            contract_publisher_source,
            filename=str(contract_publisher_path),
        )
    except (OSError, SyntaxError) as exc:
        raise AuditError(
            "Cupid Toolchain fixed-point contract is unavailable"
        ) from exc

    def active_c_source(source: str) -> str:
        source = _mask_c_comments_preserve_literals(source)
        lines = source.splitlines(keepends=True)
        disabled_depth = 0
        active: list[str] = []
        for line in lines:
            directive = re.match(
                r"^\s*#\s*(if|ifdef|ifndef|endif)\b(.*)$", line
            )
            starts_disabled = (
                disabled_depth == 0
                and directive is not None
                and directive.group(1) == "if"
                and re.fullmatch(
                    r"\s*(?:0|\(\s*0\s*\))\s*",
                    directive.group(2),
                )
                is not None
            )
            if disabled_depth != 0 or starts_disabled:
                active.append(
                    "".join("\n" if char == "\n" else " " for char in line)
                )
            else:
                active.append(line)
            if starts_disabled:
                disabled_depth = 1
            elif disabled_depth != 0 and directive is not None:
                if directive.group(1) in {"if", "ifdef", "ifndef"}:
                    disabled_depth += 1
                elif directive.group(1) == "endif":
                    disabled_depth -= 1
        if disabled_depth != 0:
            raise AuditError(
                "Cupid Toolchain fixed-point PE32 source contract differs: "
                "a disabled preprocessor block is not closed"
            )
        return "".join(active)

    def sequence_positions(
        tokens: tuple[str, ...], expected: tuple[str, ...]
    ) -> list[int]:
        width = len(expected)
        return [
            index
            for index in range(len(tokens) - width + 1)
            if tokens[index : index + width] == expected
        ]

    def brace_depth(tokens: tuple[str, ...], end: int) -> int:
        depth = 0
        for token in tokens[:end]:
            if token == "{":
                depth += 1
            elif token == "}":
                depth -= 1
        return depth

    def c_tokens(source: str, path: Path) -> tuple[str, ...]:
        return _normalize_c_preprocessing_tokens(source, str(path), 1)

    def c_function_tokens(
        source: str,
        path: Path,
        signature: str,
    ) -> tuple[str, ...] | None:
        structure = _mask_c_noncode(source)
        matches = list(re.finditer(signature, structure, flags=re.MULTILINE))
        if len(matches) != 1:
            return None
        opening = structure.find("{", matches[0].start(), matches[0].end())
        if opening < 0:
            return None
        depth = 0
        closing = -1
        for index in range(opening, len(structure)):
            if structure[index] == "{":
                depth += 1
            elif structure[index] == "}":
                depth -= 1
                if depth == 0:
                    closing = index
                    break
        if closing < 0:
            return None
        return c_tokens(source[opening + 1 : closing], path)

    linker_header_active = active_c_source(linker_header_source)
    linker_cli_active = active_c_source(linker_cli_source)
    linker_core_active = active_c_source(linker_core_source)
    linker_core_preprocessing_tokens = c_tokens(
        linker_core_active, linker_core_path
    )
    image_enum_matches = re.findall(
        r"\btypedef\s+enum\s*\{([^{}]*)\}\s*"
        r"ctool_ld_image_kind_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    request_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*ctool_ld_request_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    import_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*"
        r"ctool_ld_pe32_import_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    result_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*ctool_ld_result_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    publication_ops_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*"
        r"cupidld_publication_ops_t\s*;",
        linker_cli_active,
        flags=re.DOTALL,
    )
    expected_image_enum = (
        "CTOOL_LD_IMAGE_ELF32",
        "=",
        "0",
        ",",
        "CTOOL_LD_IMAGE_PE32_FIXED",
    )
    expected_request_member = (
        "ctool_ld_image_kind_t",
        "image_kind",
        ";",
    )
    expected_import_members = (
        "ctool_string_t",
        "symbol_name",
        ";",
        "ctool_string_t",
        "library_name",
        ";",
        "ctool_string_t",
        "procedure_name",
        ";",
    )
    expected_import_request_members = (
        (
            "const",
            "ctool_ld_pe32_import_t",
            "*",
            "pe32_imports",
            ";",
        ),
        ("ctool_u32", "pe32_import_count", ";"),
    )
    expected_import_result_members = (
        ("ctool_u32", "imported_symbol_count", ";"),
        ("ctool_u32", "imported_library_count", ";"),
    )
    expected_verifier_member = (
        "ctool_status_t",
        "(",
        "*",
        "verify",
        ")",
        "(",
        "const",
        "char",
        "*",
        "candidate",
        ",",
        "ctool_bytes_t",
        "contents",
        ")",
        ";",
    )
    request_tokens = (
        c_tokens(request_matches[0], linker_header_path)
        if len(request_matches) == 1
        else ()
    )
    result_tokens = (
        c_tokens(result_matches[0], linker_header_path)
        if len(result_matches) == 1
        else ()
    )
    if (
        len(image_enum_matches) != 1
        or c_tokens(image_enum_matches[0], linker_header_path)
        != expected_image_enum
        or len(request_matches) != 1
        or len(
            sequence_positions(
                request_tokens,
                expected_request_member,
            )
        )
        != 1
        or len(import_matches) != 1
        or c_tokens(import_matches[0], linker_header_path)
        != expected_import_members
        or any(
            len(sequence_positions(request_tokens, member)) != 1
            for member in expected_import_request_members
        )
        or len(result_matches) != 1
        or any(
            len(sequence_positions(result_tokens, member)) != 1
            for member in expected_import_result_members
        )
        or linker_header_active.count("CTOOL_LD_DIAG_BAD_IMPORT") != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the public image, import, request, or result contract is absent"
        )
    if (
        len(publication_ops_matches) != 1
        or len(
            sequence_positions(
                c_tokens(publication_ops_matches[0], linker_cli_path),
                expected_verifier_member,
            )
        )
        != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the publication verifier operation is absent"
        )

    parse_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+int\s+cupidld_parse_cli\s*\([^;{}]*\)\s*\{",
    )
    import_parse_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+int\s+cupidld_parse_import\s*\([^;{}]*\)\s*\{",
    )
    main_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bint\s+main\s*\([^;{}]*\)\s*\{",
    )
    publication_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+ctool_status_t\s+cupidld_publish_output_with_ops\s*"
        r"\([^;{}]*\)\s*\{",
    )
    verifier_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+ctool_status_t\s+cupidld_publication_verify\s*"
        r"\([^;{}]*\)\s*\{",
    )
    publication_wrapper_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+ctool_status_t\s+cupidld_publish_output\s*"
        r"\([^;{}]*\)\s*\{",
    )
    accepted_machine = (
        "strcmp",
        "(",
        "cli",
        "->",
        "machine",
        ",",
        '"elf_i386"',
        ")",
        "!=",
        "0",
        "&&",
        "strcmp",
        "(",
        "cli",
        "->",
        "machine",
        ",",
        '"i386pe"',
        ")",
        "!=",
        "0",
    )
    rejected_script = (
        "strcmp",
        "(",
        "cli",
        "->",
        "machine",
        ",",
        '"i386pe"',
        ")",
        "==",
        "0",
        "||",
        "cli",
        "->",
        "have_text_address",
        "==",
        "CTOOL_TRUE",
        "||",
        "cli",
        "->",
        "entry",
        "!=",
        "(",
        "const",
        "char",
        "*",
        ")",
        "0",
    )
    image_kind_assignment = (
        "request",
        ".",
        "image_kind",
        "=",
        "strcmp",
        "(",
        "cli",
        ".",
        "machine",
        ",",
        '"i386pe"',
        ")",
        "==",
        "0",
        "?",
        "CTOOL_LD_IMAGE_PE32_FIXED",
        ":",
        "CTOOL_LD_IMAGE_ELF32",
        ";",
    )
    link_call = (
        "ctool_ld_link",
        "(",
        "job",
        ",",
        "&",
        "request",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
    )
    publication_dispatch = (
        "status",
        "=",
        "ctool_ld_link",
        "(",
        "job",
        ",",
        "&",
        "request",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
        ";",
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "status",
        "=",
        "cupidld_publish_output",
        "(",
        "native_paths",
        "[",
        "output_native_index",
        "]",
        ",",
        "ctool_buffer_view",
        "(",
        "output",
        ")",
        ")",
        ";",
        "}",
    )
    accepted_machine_positions = (
        sequence_positions(parse_tokens, accepted_machine)
        if parse_tokens is not None
        else []
    )
    rejected_script_positions = (
        sequence_positions(parse_tokens, rejected_script)
        if parse_tokens is not None
        else []
    )
    parse_is_exact = (
        parse_tokens is not None
        and len(accepted_machine_positions) == 1
        and brace_depth(parse_tokens, accepted_machine_positions[0]) == 0
        and len(rejected_script_positions) == 1
        and brace_depth(parse_tokens, rejected_script_positions[0]) == 1
    )
    assignment_positions = (
        sequence_positions(main_tokens, image_kind_assignment)
        if main_tokens is not None
        else []
    )
    link_positions = (
        sequence_positions(main_tokens, link_call)
        if main_tokens is not None
        else []
    )
    publication_positions = (
        sequence_positions(main_tokens, publication_dispatch)
        if main_tokens is not None
        else []
    )
    import_request_assignment = (
        "request",
        ".",
        "pe32_imports",
        "=",
        "imports",
        ";",
        "request",
        ".",
        "pe32_import_count",
        "=",
        "cli",
        ".",
        "import_count",
        ";",
    )
    import_assignment_positions = (
        sequence_positions(main_tokens, import_request_assignment)
        if main_tokens is not None
        else []
    )
    required_cli_import_fragments = (
        "--import IAT_SYMBOL=LIBRARY:PROCEDURE",
        'cupidld_take_value(argc, argv, &index, argument, "--import",',
        "cli->imports[cli->import_count] = value;",
        "cli->import_count++;",
        "if (cli->import_count != 0u && "
        'strcmp(cli->machine, "i386pe") != 0)',
        "equals = strchr(text, '=');",
        ": strchr(equals + 1, ':');",
        "import_out->symbol_name.data = text;",
        "import_out->library_name.data = equals + 1;",
        "import_out->procedure_name.data = colon + 1;",
        "cupidld_parse_import(cli.imports[index], &imports[index]) == 0",
    )
    missing_cli_import_fragments = [
        fragment
        for fragment in required_cli_import_fragments
        if linker_cli_active.count(fragment) != 1
    ]
    if (
        not parse_is_exact
        or import_parse_tokens is None
        or missing_cli_import_fragments
        or len(assignment_positions) != 1
        or len(import_assignment_positions) != 1
        or len(link_positions) != 1
        or len(publication_positions) != 1
        or assignment_positions[0] >= link_positions[0]
        or import_assignment_positions[0] >= link_positions[0]
        or brace_depth(main_tokens, assignment_positions[0]) != 0
        or brace_depth(main_tokens, import_assignment_positions[0]) != 0
        or brace_depth(main_tokens, publication_positions[0]) != 0
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the i386pe parser, import dispatch, or publication is absent"
        )

    publication_write = (
        "status",
        "=",
        "ops",
        "->",
        "write_all",
        "(",
        "&",
        "file",
        ",",
        "contents",
        ")",
        ";",
    )
    publication_close = (
        "ctool_status_t",
        "close_status",
        "=",
        "ops",
        "->",
        "close",
        "(",
        "&",
        "file",
        ")",
        ";",
    )
    publication_verify = (
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "status",
        "=",
        "ops",
        "->",
        "verify",
        "(",
        "candidate",
        ",",
        "contents",
        ")",
        ";",
        "}",
    )
    publication_replace = (
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "status",
        "=",
        "ops",
        "->",
        "replace",
        "(",
        "candidate",
        ",",
        "destination",
        ")",
        ";",
        "}",
    )
    publication_discard = (
        "if",
        "(",
        "status",
        "!=",
        "CTOOL_OK",
        ")",
        "{",
        "ops",
        "->",
        "discard",
        "(",
        "candidate",
        ")",
        ";",
        "}",
    )
    publication_wrapper = (
        "static",
        "const",
        "cupidld_publication_ops_t",
        "ops",
        "=",
        "{",
        "cupidld_publication_open",
        ",",
        "cupidld_publication_write_all",
        ",",
        "cupidld_publication_close",
        ",",
        "cupidld_publication_verify",
        ",",
        "cupidld_publication_replace",
        ",",
        "cupidld_publication_discard",
        "}",
        ";",
        "return",
        "cupidld_publish_output_with_ops",
        "(",
        "destination",
        ",",
        "contents",
        ",",
        "&",
        "ops",
        ")",
        ";",
    )
    publication_sequences = (
        publication_write,
        publication_close,
        publication_verify,
        publication_replace,
        publication_discard,
    )
    publication_sequence_positions = (
        [
            sequence_positions(publication_tokens, sequence)
            for sequence in publication_sequences
        ]
        if publication_tokens is not None
        else []
    )
    publication_order = (
        [positions[0] for positions in publication_sequence_positions]
        if len(publication_sequence_positions) == len(publication_sequences)
        and all(len(positions) == 1 for positions in publication_sequence_positions)
        else []
    )
    verifier_size_check = (
        "file_size",
        "<",
        "0l",
        "||",
        "(",
        "unsigned",
        "long",
        ")",
        "file_size",
        "!=",
        "contents",
        ".",
        "size",
    )
    verifier_read = (
        "fread",
        "(",
        "buffer",
        ",",
        "1u",
        ",",
        "(",
        "size_t",
        ")",
        "request",
        ",",
        "file",
        ")",
    )
    verifier_compare = (
        "memcmp",
        "(",
        "buffer",
        ",",
        "contents",
        ".",
        "data",
        "+",
        "total",
        ",",
        "count",
        ")",
        "!=",
        "0",
    )
    verifier_close = (
        "if",
        "(",
        "fclose",
        "(",
        "file",
        ")",
        "!=",
        "0",
        ")",
        "{",
        "status",
        "=",
        "CTOOL_ERR_IO",
        ";",
        "}",
        "return",
        "status",
        ";",
    )
    verifier_requirements = (
        verifier_size_check,
        verifier_read,
        verifier_compare,
        verifier_close,
    )
    verifier_positions = (
        [
            sequence_positions(verifier_tokens, requirement)
            for requirement in verifier_requirements
        ]
        if verifier_tokens is not None
        else []
    )
    verifier_order = (
        [positions[0] for positions in verifier_positions]
        if len(verifier_positions) == len(verifier_requirements)
        and all(len(positions) == 1 for positions in verifier_positions)
        else []
    )
    verifier_top_level_returns = (
        [
            index
            for index, token in enumerate(verifier_tokens)
            if token == "return" and brace_depth(verifier_tokens, index) == 0
        ]
        if verifier_tokens is not None
        else []
    )
    publication_top_level_returns = (
        [
            index
            for index, token in enumerate(publication_tokens)
            if token == "return"
            and brace_depth(publication_tokens, index) == 0
        ]
        if publication_tokens is not None
        else []
    )
    verified_replace_positions = (
        sequence_positions(
            publication_tokens,
            publication_verify + publication_replace,
        )
        if publication_tokens is not None
        else []
    )
    wrapper_positions = (
        sequence_positions(publication_wrapper_tokens, publication_wrapper)
        if publication_wrapper_tokens is not None
        else []
    )
    if (
        publication_tokens is None
        or publication_order != sorted(publication_order)
        or len(publication_order) != len(publication_sequences)
        or brace_depth(publication_tokens, publication_order[0]) != 0
        or brace_depth(publication_tokens, publication_order[1]) != 1
        or any(
            brace_depth(publication_tokens, position) != 0
            for position in publication_order[2:]
        )
        or len(verified_replace_positions) != 1
        or brace_depth(publication_tokens, verified_replace_positions[0]) != 0
        or len(publication_top_level_returns) != 1
        or publication_tokens[
            publication_top_level_returns[0] :
            publication_top_level_returns[0] + 3
        ]
        != ("return", "status", ";")
        or len(sequence_positions(publication_tokens, ("ops", "->", "verify")))
        != 2
        or len(
            sequence_positions(publication_tokens, ("ops", "->", "replace"))
        )
        != 2
        or len(verifier_positions) != len(verifier_requirements)
        or any(len(positions) != 1 for positions in verifier_positions)
        or verifier_order != sorted(verifier_order)
        or len(verifier_order) != len(verifier_requirements)
        or [brace_depth(verifier_tokens, position) for position in verifier_order]
        != [0, 1, 1, 0]
        or len(verifier_top_level_returns) != 1
        or verifier_tokens[
            verifier_top_level_returns[0] : verifier_top_level_returns[0] + 3
        ]
        != ("return", "status", ";")
        or verifier_tokens.count("CTOOL_OK") != 4
        or len(wrapper_positions) != 1
        or brace_depth(publication_wrapper_tokens, wrapper_positions[0]) != 0
        or re.findall(
            r"\bcupidld_publication_verify\b",
            _mask_c_noncode(linker_cli_active),
        ).count("cupidld_publication_verify")
        != 2
        or re.findall(
            r"\bcupidld_publication_replace\b",
            _mask_c_noncode(linker_cli_active),
        ).count("cupidld_publication_replace")
        != 2
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "publication does not verify the candidate before replacement"
        )

    serializer_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_serialize_pe32_fixed\s*"
        r"\([^;{}]*\)\s*\{",
    )
    linker_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bctool_status_t\s+ctool_ld_link\s*\([^;{}]*\)\s*\{",
    )
    image_rva_limit_guard = (
        "if",
        "(",
        "image_size",
        ">",
        "LD_PE_NAME_RVA_LIMIT",
        ")",
        "{",
        "status",
        "=",
        "ld_diagnostic",
        "(",
        "link",
        "->",
        "job",
        ",",
        "CTOOL_LD_DIAG_LIMIT",
        ",",
        "ctool_string",
        "(",
        '""',
        ")",
        ",",
        "0u",
        ",",
        "0u",
        ",",
        '"CupidLD PE32 image exceeds the 2 GiB RVA range"',
        ",",
        "CTOOL_ERR_LIMIT",
        ")",
        ";",
        "goto",
        "done",
        ";",
        "}",
    )
    serializer_requirements = (
        (
            "link",
            "->",
            "request",
            "->",
            "layout",
            ".",
            "kind",
            "!=",
            "CTOOL_LD_LAYOUT_FIXED_TEXT",
        ),
        (
            "link",
            "->",
            "request",
            "->",
            "layout",
            ".",
            "as",
            ".",
            "fixed_text",
            ".",
            "base_address",
            "!=",
            "LD_PE_TEXT_ADDRESS",
        ),
        ('"CupidLD PE32 requires text address 0x00401000"',),
        image_rva_limit_guard,
        ("ld_put_pe32_dos_header", "(", "output", ")"),
        ("ld_put_pe32_optional_header", "(",),
        ("ld_put_pe32_section_header", "(",),
        (
            "result_out",
            "->",
            "bytes",
            "=",
            "ctool_buffer_view",
            "(",
            "output",
            ")",
            ".",
            "size",
            ";",
        ),
    )
    emitted_count_initialization = (
        "ctool_u32",
        "emitted_section_count",
        "=",
        "0u",
        ";",
    )
    emitted_count_loop = (
        "for",
        "(",
        "index",
        "=",
        "0u",
        ";",
        "index",
        "<",
        "link",
        "->",
        "output_count",
        ";",
        "index",
        "++",
        ")",
        "{",
        "if",
        "(",
        "link",
        "->",
        "outputs",
        "[",
        "index",
        "]",
        ".",
        "size",
        "!=",
        "0u",
        ")",
        "{",
        "emitted_section_count",
        "++",
        ";",
        "}",
        "}",
    )
    emitted_count_guard = (
        "if",
        "(",
        "emitted_section_count",
        "==",
        "0u",
        "||",
        "emitted_section_count",
        ">",
        "5u",
        ")",
    )
    emitted_header_overflow = (
        "ld_multiply_overflows",
        "(",
        "emitted_section_count",
        ",",
        "LD_PE_SECTION_HEADER_SIZE",
        ")",
        "==",
        "CTOOL_TRUE",
    )
    emitted_header_extent = (
        "headers_end",
        "=",
        "LD_PE_DOS_HEADER_SIZE",
        "+",
        "LD_PE_SIGNATURE_SIZE",
        "+",
        "LD_PE_COFF_HEADER_SIZE",
        "+",
        "LD_PE_OPTIONAL_HEADER_SIZE",
        "+",
        "emitted_section_count",
        "*",
        "LD_PE_SECTION_HEADER_SIZE",
        ";",
    )
    zero_size_layout_skip = (
        "if",
        "(",
        "section",
        "->",
        "size",
        "==",
        "0u",
        ")",
        "{",
        "if",
        "(",
        "section",
        "->",
        "file_size",
        "!=",
        "0u",
        ")",
        "{",
        "status",
        "=",
        "CTOOL_ERR_INTERNAL",
        ";",
        "goto",
        "done",
        ";",
        "}",
        "section",
        "->",
        "file_offset",
        "=",
        "0u",
        ";",
        "continue",
        ";",
        "}",
    )
    emitted_overlap_check = (
        "if",
        "(",
        "have_previous_section",
        "==",
        "CTOOL_TRUE",
        "&&",
        "section",
        "->",
        "address",
        "<",
        "previous_section_end",
        ")",
        "{",
        "status",
        "=",
        "CTOOL_ERR_INPUT",
        ";",
        "goto",
        "done",
        ";",
        "}",
        "previous_section_end",
        "=",
        "end",
        ";",
        "have_previous_section",
        "=",
        "CTOOL_TRUE",
        ";",
    )
    emitted_coff_count = (
        "ctool_buffer_put_le16",
        "(",
        "output",
        ",",
        "(",
        "ctool_u16",
        ")",
        "emitted_section_count",
        ")",
    )
    zero_size_header_skip = (
        "if",
        "(",
        "section",
        "->",
        "size",
        "==",
        "0u",
        ")",
        "{",
        "continue",
        ";",
        "}",
    )
    emitted_result_count = (
        "result_out",
        "->",
        "output_section_count",
        "=",
        "emitted_section_count",
        ";",
    )
    emitted_section_requirements = (
        emitted_count_initialization,
        emitted_count_loop,
        emitted_count_guard,
        emitted_header_overflow,
        emitted_header_extent,
        zero_size_layout_skip,
        emitted_overlap_check,
        emitted_coff_count,
        zero_size_header_skip,
        emitted_result_count,
    )
    request_validation = (
        "request",
        "->",
        "image_kind",
        "!=",
        "CTOOL_LD_IMAGE_ELF32",
        "&&",
        "request",
        "->",
        "image_kind",
        "!=",
        "CTOOL_LD_IMAGE_PE32_FIXED",
    )
    serializer_dispatch = (
        "if",
        "(",
        "request",
        "->",
        "image_kind",
        "==",
        "CTOOL_LD_IMAGE_PE32_FIXED",
        ")",
        "{",
        "status",
        "=",
        "ld_serialize_pe32_fixed",
        "(",
        "&",
        "link",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
        ";",
        "}",
        "else",
        "{",
        "status",
        "=",
        "ld_serialize_elf32_exec",
        "(",
        "&",
        "link",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
        ";",
        "}",
    )
    serialization_guard = (
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "phase",
        "=",
        '"CupidLD executable serialization failed"',
        ";",
        *serializer_dispatch,
        "}",
    )
    core_identifiers = re.findall(
        r"\b[A-Za-z_][A-Za-z0-9_]*\b", _mask_c_noncode(linker_core_active)
    )
    serializer_is_exact = serializer_tokens is not None and all(
        len(sequence_positions(serializer_tokens, requirement)) == 1
        for requirement in serializer_requirements
    )
    emitted_section_positions = (
        [
            sequence_positions(serializer_tokens, requirement)
            for requirement in emitted_section_requirements
        ]
        if serializer_tokens is not None
        else []
    )
    emitted_section_order = (
        [positions[0] for positions in emitted_section_positions]
        if len(emitted_section_positions) == len(emitted_section_requirements)
        and all(len(positions) == 1 for positions in emitted_section_positions)
        else []
    )
    emitted_sections_are_exact = (
        serializer_tokens is not None
        and emitted_section_order == sorted(emitted_section_order)
        and len(emitted_section_order) == len(emitted_section_requirements)
        and [
            brace_depth(serializer_tokens, position)
            for position in emitted_section_order
        ]
        == [0, 0, 0, 0, 0, 1, 1, 1, 1, 1]
        and serializer_tokens.count("emitted_section_count") == 9
        and len(
            sequence_positions(
                serializer_tokens,
                (
                    "emitted_section_count",
                    "*",
                    "LD_PE_SECTION_HEADER_SIZE",
                ),
            )
        )
        == 2
        and len(
            sequence_positions(
                serializer_tokens,
                ("result_out", "->", "output_section_count"),
            )
        )
        == 1
    )
    validation_positions = (
        sequence_positions(linker_tokens, request_validation)
        if linker_tokens is not None
        else []
    )
    serialization_positions = (
        sequence_positions(linker_tokens, serialization_guard)
        if linker_tokens is not None
        else []
    )
    dispatch_is_exact = (
        linker_tokens is not None
        and len(validation_positions) == 1
        and brace_depth(linker_tokens, validation_positions[0]) == 0
        and len(serialization_positions) == 1
        and brace_depth(linker_tokens, serialization_positions[0]) == 0
        and core_identifiers.count("ld_serialize_pe32_fixed") == 2
        and core_identifiers.count("CTOOL_LD_IMAGE_PE32_FIXED") == 3
    )
    if (
        not serializer_is_exact
        or not emitted_sections_are_exact
        or not dispatch_is_exact
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the fixed serializer, emitted sections, or dispatch differ"
        )

    import_builder_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_prepare_pe32_imports\s*"
        r"\([^;{}]*\)\s*\{",
    )
    import_compare_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_i32\s+ld_pe32_import_compare\s*"
        r"\([^;{}]*\)\s*\{",
    )
    relocation_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_apply_relocations\s*"
        r"\([^;{}]*\)\s*\{",
    )
    optional_header_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_put_pe32_optional_header\s*"
        r"\([^;{}]*\)\s*\{",
    )
    import_pipeline = (
        (
            "status",
            "=",
            "ld_prepare_pe32_imports",
            "(",
            "&",
            "link",
            ")",
            ";",
        ),
        (
            "status",
            "=",
            "ld_finalize_globals",
            "(",
            "&",
            "link",
            ")",
            ";",
        ),
        (
            "status",
            "=",
            "ld_apply_relocations",
            "(",
            "&",
            "link",
            ")",
            ";",
        ),
    )
    import_pipeline_positions = (
        [sequence_positions(linker_tokens, step) for step in import_pipeline]
        if linker_tokens is not None
        else []
    )
    import_pipeline_order = (
        [positions[0] for positions in import_pipeline_positions]
        if len(import_pipeline_positions) == len(import_pipeline)
        and all(len(positions) == 1 for positions in import_pipeline_positions)
        else []
    )
    import_sort_call = (
        "ld_pe32_import_sort",
        "(",
        "link",
        "->",
        "pe32_imports",
        ",",
        "import_count",
        ")",
        ";",
    )
    import_sort_positions = (
        sequence_positions(import_builder_tokens, import_sort_call)
        if import_builder_tokens is not None
        else []
    )
    import_duplicate_guard = (
        "if",
        "(",
        "global",
        "->",
        "import_selected",
        "==",
        "CTOOL_TRUE",
        ")",
        "{",
        "return",
        "ld_pe32_import_error",
        "(",
        "link",
        ",",
        "index",
        ",",
        '"CupidLD PE32 imports contain the same IAT symbol twice"',
        ")",
        ";",
        "}",
    )
    import_duplicate_guard_positions = (
        sequence_positions(import_builder_tokens, import_duplicate_guard)
        if import_builder_tokens is not None
        else []
    )
    import_selected_assignment = (
        "global",
        "->",
        "import_selected",
        "=",
        "CTOOL_TRUE",
        ";",
    )
    import_selected_assignment_positions = (
        sequence_positions(import_builder_tokens, import_selected_assignment)
        if import_builder_tokens is not None
        else []
    )
    name_rva_limit_definition = (
        "#",
        "define",
        "LD_PE_NAME_RVA_LIMIT",
        "0x80000000u",
    )
    name_rva_limit_definition_positions = sequence_positions(
        linker_core_preprocessing_tokens, name_rva_limit_definition
    )
    import_table_rva_guard = (
        "if",
        "(",
        "address",
        "<",
        "LD_PE_IMAGE_BASE",
        "||",
        "address",
        "-",
        "LD_PE_IMAGE_BASE",
        ">=",
        "LD_PE_NAME_RVA_LIMIT",
        "||",
        "import_payload_size",
        ">",
        "LD_PE_NAME_RVA_LIMIT",
        "-",
        "(",
        "address",
        "-",
        "LD_PE_IMAGE_BASE",
        ")",
        ")",
        "{",
        "return",
        "ld_pe32_import_error",
        "(",
        "link",
        ",",
        "0u",
        ",",
        '"CupidLD PE32 import table exceeds the name RVA range"',
        ")",
        ";",
        "}",
    )
    import_table_rva_guard_positions = (
        sequence_positions(import_builder_tokens, import_table_rva_guard)
        if import_builder_tokens is not None
        else []
    )
    import_thunk_rva_guard = (
        "if",
        "(",
        "hint_rva",
        ">=",
        "LD_PE_NAME_RVA_LIMIT",
        ")",
        "{",
        "return",
        "ld_pe32_import_error",
        "(",
        "link",
        ",",
        "import_index",
        ",",
        '"CupidLD PE32 import thunk has the ordinal flag set"',
        ")",
        ";",
        "}",
    )
    import_thunk_rva_guard_positions = (
        sequence_positions(import_builder_tokens, import_thunk_rva_guard)
        if import_builder_tokens is not None
        else []
    )
    required_core_import_fragments = (
        "request->pe32_import_count != 0u &&",
        "request->pe32_imports == "
        "(const ctool_ld_pe32_import_t *)0",
        "CupidLD imports require the fixed PE32 image profile",
        'ld_begin_output(link, ctool_string(".idata"), address,',
        "section->flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;",
        "global->rank = LD_DEFINITION_IMPORT;",
        "global->value = address + import->iat_offset;",
        "link->pe32_import_directory_size = descriptor_size;",
        "link->pe32_iat_directory_rva =",
        "link->globals[global_index].rank == LD_DEFINITION_IMPORT &&",
        "relocation->type != CTOOL_ELF32_R_386_32 ||",
        "relocation->addend != 0",
        "CupidLD IAT symbols require an absolute zero-addend relocation",
        "if (directory == LD_PE_IMPORT_DIRECTORY)",
        "else if (directory == LD_PE_IAT_DIRECTORY)",
        "result_out->imported_symbol_count = link->pe32_import_count;",
        "result_out->imported_library_count = link->pe32_library_count;",
        "CupidLD PE32 imports use inconsistent library spelling",
        "CupidLD PE32 imports contain the same IAT symbol twice",
        "CupidLD PE32 imports contain the same procedure twice",
    )
    missing_core_import_fragments = [
        fragment
        for fragment in required_core_import_fragments
        if linker_core_active.count(fragment) < 1
    ]
    if (
        import_builder_tokens is None
        or import_compare_tokens is None
        or relocation_tokens is None
        or optional_header_tokens is None
        or missing_core_import_fragments
        or len(import_sort_positions) != 1
        or len(import_duplicate_guard_positions) != 1
        or len(import_selected_assignment_positions) != 1
        or len(name_rva_limit_definition_positions) != 1
        or len(import_table_rva_guard_positions) != 1
        or len(import_thunk_rva_guard_positions) != 1
        or import_duplicate_guard_positions[0]
        >= import_selected_assignment_positions[0]
        or import_pipeline_order != sorted(import_pipeline_order)
        or len(import_pipeline_order) != len(import_pipeline)
        or any(
            brace_depth(linker_tokens, position) != 1
            for position in import_pipeline_order
        )
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 import contract differs: "
            "canonical construction, relocation safety, or staged dispatch "
            "is absent"
        )

    assignments: dict[str, object] = {}
    for node in test_tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            continue
        if target.id not in {
            "CUPIDC_FIXED_POINT_SOURCES",
            "CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS",
            "CUPIDC_FIXED_POINT_LINK_ORDER",
            "CUPID_TOOLCHAIN_FIXED_POINT_SOURCES",
            "CUPID_TOOLCHAIN_FIXED_POINT_LINKS",
        }:
            continue
        try:
            assignments[target.id] = ast.literal_eval(node.value)
        except (TypeError, ValueError) as exc:
            raise AuditError(
                "Cupid Toolchain fixed-point manifest is not literal: "
                f"{target.id}"
            ) from exc

    expected_compiler_sources = (
        ("ctool", "/toolchain/ctool.cc", False),
        ("ctool_host", "/toolchain/ctool_host.cc", False),
        ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
        ("cupidc_type", "/toolchain/cupidc_type.cc", False),
        ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
        ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
        ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
        ("elf32", "/toolchain/elf32.cc", False),
        ("x86", "/toolchain/x86.cc", False),
        ("cupidc_main", "/toolchain/cupidc_main.cc", False),
        (
            "runtime",
            "/toolchain/hosted/i386-linux/runtime.cc",
            True,
        ),
    )
    expected_toolchain_sources = (
        (
            "runtime",
            "/toolchain/hosted/i386-linux/runtime.cc",
            True,
        ),
        ("ctool", "/toolchain/ctool.cc", False),
        ("ctool_host", "/toolchain/ctool_host.cc", False),
        ("elf32", "/toolchain/elf32.cc", False),
        ("x86", "/toolchain/x86.cc", False),
        ("cupidasm", "/toolchain/cupidasm.cc", False),
        ("cupidasm_main", "/toolchain/cupidasm_main.cc", False),
        ("cupiddis", "/toolchain/cupiddis.cc", False),
        ("cupiddis_main", "/toolchain/cupiddis_main.cc", False),
        ("cupidobj", "/toolchain/cupidobj.cc", False),
        ("cupidobj_main", "/toolchain/cupidobj_main.cc", False),
        ("cupidld", "/toolchain/cupidld.cc", False),
        ("cupidld_main", "/toolchain/cupidld_main.cc", False),
        ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
        ("cupidc_type", "/toolchain/cupidc_type.cc", False),
        ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
        ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
        ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
        ("cupidc_main", "/toolchain/cupidc_main.cc", False),
    )
    expected_include_arguments = (
        "-I",
        "/toolchain",
        "--include-angle",
        "/toolchain/hosted/i386-linux/include",
    )
    expected_link_order = (
        "start",
        "cupidc_main",
        "cupidc_emit",
        "cupidc_ir",
        "cupidc_frontend",
        "cupidc_type",
        "cupidc_pp",
        "ctool_host",
        "ctool",
        "elf32",
        "x86",
        "runtime",
    )
    expected_toolchain_links = (
        (
            "cupidasm",
            (
                "start",
                "cupidasm_main",
                "cupidasm",
                "ctool_host",
                "ctool",
                "elf32",
                "x86",
                "runtime",
            ),
        ),
        (
            "cupiddis",
            (
                "start",
                "cupiddis_main",
                "cupiddis",
                "ctool_host",
                "ctool",
                "elf32",
                "x86",
                "runtime",
            ),
        ),
        (
            "cupidld",
            (
                "start",
                "cupidld_main",
                "cupidld",
                "ctool_host",
                "ctool",
                "elf32",
                "runtime",
            ),
        ),
        (
            "cupidobj",
            (
                "start",
                "cupidobj_main",
                "cupidobj",
                "ctool_host",
                "ctool",
                "elf32",
                "runtime",
            ),
        ),
        ("cupidc", expected_link_order),
    )
    expected_assignments = {
        "CUPIDC_FIXED_POINT_SOURCES": expected_compiler_sources,
        "CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS":
            expected_include_arguments,
        "CUPIDC_FIXED_POINT_LINK_ORDER": expected_link_order,
        "CUPID_TOOLCHAIN_FIXED_POINT_SOURCES":
            expected_toolchain_sources,
        "CUPID_TOOLCHAIN_FIXED_POINT_LINKS":
            expected_toolchain_links,
    }
    for name, expected in expected_assignments.items():
        if assignments.get(name) != expected:
            raise AuditError(
                "Cupid Toolchain fixed-point manifest differs: "
                f"{name}"
            )

    required_driver_fragments = (
        "[--include-angle PATH]",
        "[-include FILE]",
        'if (strcmp(argument, "--include-angle") == 0)',
        "cli->include_forms[cli->include_count] = "
        "CTOOL_C_PP_INCLUDE_ANGLE;",
        '"-include", &value);',
        "context->include_roots[index].forms = "
        "context->include_forms[index];",
        "pp_request.forced_includes = context->forced_includes;",
        "pp_request.forced_include_count = context->forced_include_count;",
        "cupidc: --root requires logical include paths",
        "cupidc: --root requires logical forced include paths",
    )
    missing_driver_fragments = [
        fragment
        for fragment in required_driver_fragments
        if driver_source.count(fragment) != 1
    ]
    if missing_driver_fragments:
        raise AuditError(
            "Cupid Toolchain fixed-point driver does not retain its exact "
            f"include contract: {missing_driver_fragments!r}"
        )
    if (
        driver_source.count(
            "CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE"
        )
        != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point driver does not retain one "
            "quoted-and-angle -I form"
        )

    required_test_fragments = (
        "with ThreadPoolExecutor(max_workers=2) as executor:",
        'producers["cupidc"], arguments, timeout=300',
        "assembled = self.run_cupid_linux_tool(\n"
        '                    producers["cupidasm"],',
        "linked_stage = self.run_cupid_linux_tool(\n"
        '                        producers["cupidld"],',
        "stage_two_objects, stage_two_tools = build_stage(\n"
        '                generation_one_producers, "stage-two"\n'
        "            )",
        "stage_three_objects, stage_three_tools = build_stage(\n"
        '                stage_two_producers, "stage-three"\n'
        "            )",
        'stage_producers["stage-three"][producer_name],',
        "stage_three_objects[name].read_bytes(),\n"
        "                    stage_two_objects[name].read_bytes(),",
        'stage_three_objects["start"].read_bytes(),\n'
        '                stage_two_objects["start"].read_bytes(),',
        "stage_two_tools[tool_name].read_bytes(),\n"
        "                    generation_one_tool.read_bytes(),",
        "stage_three_tools[tool_name].read_bytes(),\n"
        "                    stage_two_tools[tool_name].read_bytes(),",
        "def run_stage_pair(",
        "stage_two_run = self.run_cupid_linux_tool(\n"
        '                    stage_two_tools[tool_name],',
        "stage_three_run = self.run_cupid_linux_tool(\n"
        '                    stage_three_tools[tool_name],',
        "stage_three_run.returncode,\n"
        "                    stage_two_run.returncode,",
        "stage_three_run.stdout,\n"
        "                    stage_two_run.stdout,",
        "stage_three_run.stderr,\n"
        "                    stage_two_run.stderr,",
        "for tool_name in generation_one_tools:\n"
        "                stage_two_help, _stage_three_help = run_stage_pair(\n"
        '                    tool_name, ["--help"]\n'
        "                )",
        "stage_three_valid.read_bytes(),\n"
        "                stage_two_valid.read_bytes(),",
        "stage_three_invalid_run.stderr,\n"
        "                stage_two_invalid_run.stderr,",
        "stage_three_failure.read_bytes(), failure_sentinel",
        "stage_two_assembly, _stage_three_assembly = run_stage_pair(",
        "stage_two_report, _stage_three_report = run_stage_pair(",
        "stage_two_nm, _stage_three_nm = run_stage_pair(",
        "stage_two_wrap, _stage_three_wrap = run_stage_pair(",
        "stage_two_text_wrap, _stage_three_text_wrap = run_stage_pair(",
        "stage_two_flat_run, _stage_three_flat_run = run_stage_pair(",
        "stage_two_ksyms_run, _stage_three_ksyms_run = run_stage_pair(",
        "invalid_ksyms_run, _invalid_ksyms_stage_three = run_stage_pair(",
        '"address is outside i386", invalid_ksyms_run.stderr',
        "stage_two_link, _stage_three_link = run_stage_pair(",
        "stage_two_script_link, _stage_three_script_link = run_stage_pair(",
        "invalid_asm_run, _invalid_asm_stage_three = run_stage_pair(",
        '"unknown Cupid ASM instruction mnemonic",\n'
        "                invalid_asm_run.stderr,",
        "missing_dis_run, _missing_dis_stage_three = run_stage_pair(",
        '"cupiddis: cannot load ", missing_dis_run.stderr',
        "malformed_dis_run, _malformed_dis_stage_three = run_stage_pair(",
        "malformed_link_run, _malformed_link_stage_three = run_stage_pair(",
        "missing_obj_run, _missing_obj_stage_three = run_stage_pair(",
        '"cupidobj: cannot load ", missing_obj_run.stderr',
    )
    missing_test_fragments = [
        fragment
        for fragment in required_test_fragments
        if test_source.count(fragment) != 1
    ]
    if missing_test_fragments:
        raise AuditError(
            "Cupid Toolchain fixed-point staged comparison differs: "
            f"{missing_test_fragments!r}"
        )

    behavior_functions = [
        node
        for node in bootstrap_tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "_run_behavior_checks"
    ]
    expected_behavior_matrix = {
        "failure_cases": 15,
        "help_cases": 5,
        "success_cases": 17,
    }
    expected_profile_failures = {
        "truncated": "snapshot is truncated",
        "unsafe-path": "repository path is invalid",
        "case-collision": "header path has a case collision",
    }
    if len(behavior_functions) != 1:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "_run_behavior_checks is not unique"
        )
    behavior_function = behavior_functions[0]
    behavior_source = (
        ast.get_source_segment(bootstrap_source, behavior_function) or ""
    )
    positive_profile_result: tuple[str, int] | None = None
    positive_profile_status: tuple[str, int, int] | None = None
    profile_failure_matrix: dict[str, str] | None = None
    profile_failure_loop: ast.For | None = None
    behavior_returns: list[object] = []
    for index, statement in enumerate(behavior_function.body):
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
            target = statement.targets[0]
            call = statement.value
            if (
                isinstance(target, ast.Name)
                and isinstance(call, ast.Call)
                and isinstance(call.func, ast.Name)
                and call.func.id == "_run_stage_pair"
            ):
                string_arguments = [
                    node.value
                    for node in ast.walk(call)
                    if isinstance(node, ast.Constant)
                    and isinstance(node.value, str)
                ]
                if "profile-manifest" in string_arguments:
                    if (
                        string_arguments.count("profile-manifest") != 2
                        or string_arguments.count("cupidobj") != 1
                    ):
                        raise AuditError(
                            "Cupid Toolchain fixed-point profile behavior "
                            "differs: a profile case does not run both stages"
                        )
                    if positive_profile_result is not None:
                        raise AuditError(
                            "Cupid Toolchain fixed-point profile behavior "
                            "differs: the positive stage pair is not unique"
                        )
                    positive_profile_result = (target.id, index)
            if (
                isinstance(target, ast.Name)
                and target.id == "profile_failure_cases"
            ):
                if not isinstance(statement.value, (ast.Tuple, ast.List)):
                    profile_failure_matrix = None
                    continue
                parsed_failures: dict[str, str] = {}
                for entry in statement.value.elts:
                    if not isinstance(entry, (ast.Tuple, ast.List)):
                        parsed_failures = {}
                        break
                    if len(entry.elts) != 3:
                        parsed_failures = {}
                        break
                    name = entry.elts[0]
                    diagnostic = entry.elts[2]
                    if (
                        not isinstance(name, ast.Constant)
                        or not isinstance(name.value, str)
                        or not isinstance(diagnostic, ast.Constant)
                        or not isinstance(diagnostic.value, str)
                        or name.value in parsed_failures
                    ):
                        parsed_failures = {}
                        break
                    parsed_failures[name.value] = diagnostic.value
                profile_failure_matrix = parsed_failures
        if (
            isinstance(statement, ast.Expr)
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_expect_status"
            and len(statement.value.args) >= 3
        ):
            result, status, label = statement.value.args[:3]
            if (
                isinstance(result, ast.Name)
                and isinstance(status, ast.Constant)
                and isinstance(status.value, int)
                and isinstance(label, ast.Constant)
                and isinstance(label.value, str)
                and label.value == "CupidObj profile manifest"
            ):
                positive_profile_status = (
                    result.id,
                    status.value,
                    index,
                )
        if (
            isinstance(statement, ast.For)
            and isinstance(statement.iter, ast.Name)
            and statement.iter.id == "profile_failure_cases"
        ):
            if profile_failure_loop is not None:
                raise AuditError(
                    "Cupid Toolchain fixed-point profile behavior differs: "
                    "the profile failure loop is not unique"
                )
            profile_failure_loop = statement
        if isinstance(statement, ast.Return):
            try:
                behavior_returns.append(ast.literal_eval(statement.value))
            except (TypeError, ValueError):
                behavior_returns.append(None)

    if behavior_returns != [expected_behavior_matrix]:
        raise AuditError(
            "Cupid Toolchain fixed-point behavior matrix differs: "
            f"expected {expected_behavior_matrix!r}"
        )
    if profile_failure_matrix != expected_profile_failures:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the profile failure matrix is incomplete"
        )
    if positive_profile_result is None or positive_profile_status is None:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the positive profile stage pair is absent"
        )
    result_name, stage_pair_index = positive_profile_result
    status_result, positive_status, status_index = positive_profile_status
    positive_checks = [
        statement
        for statement in behavior_function.body[status_index + 1:]
        if isinstance(statement, ast.If)
    ]
    positive_read_names = (
        {
            node.func.value.id
            for node in ast.walk(positive_checks[0].test)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "read_bytes"
            and isinstance(node.func.value, ast.Name)
        }
        if positive_checks
        else set()
    )
    if (
        status_result != result_name
        or positive_status != 0
        or stage_pair_index >= status_index
        or not positive_checks
        or len(positive_read_names) < 2
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the positive case does not compare both staged outputs"
        )
    if profile_failure_loop is None:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the profile failure loop is absent"
        )
    loop_target_names = [
        node.id
        for node in (
            profile_failure_loop.target.elts
            if isinstance(profile_failure_loop.target, (ast.Tuple, ast.List))
            else []
        )
        if isinstance(node, ast.Name)
    ]
    failure_stage_pairs: list[tuple[str, ast.Call]] = []
    failure_statuses: list[tuple[str, int]] = []
    failure_checks = [
        node for node in profile_failure_loop.body if isinstance(node, ast.If)
    ]
    for node in ast.walk(profile_failure_loop):
        if (
            isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and isinstance(node.value, ast.Call)
            and isinstance(node.value.func, ast.Name)
            and node.value.func.id == "_run_stage_pair"
        ):
            string_arguments = [
                child.value
                for child in ast.walk(node.value)
                if isinstance(child, ast.Constant)
                and isinstance(child.value, str)
            ]
            if "profile-manifest" in string_arguments:
                if (
                    string_arguments.count("profile-manifest") != 2
                    or string_arguments.count("cupidobj") != 1
                ):
                    raise AuditError(
                        "Cupid Toolchain fixed-point profile behavior differs: "
                        "the failure loop does not run both stages"
                    )
                failure_stage_pairs.append(
                    (node.targets[0].id, node.value)
                )
        if (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_expect_status"
            and len(node.args) >= 2
            and isinstance(node.args[0], ast.Name)
            and isinstance(node.args[1], ast.Constant)
            and isinstance(node.args[1].value, int)
        ):
            failure_statuses.append((node.args[0].id, node.args[1].value))
    if (
        len(loop_target_names) != 3
        or len(failure_stage_pairs) != 1
        or failure_statuses != [(failure_stage_pairs[0][0], 1)]
        or len(failure_checks) != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the failure cases are not one checked stage pair"
        )
    failure_check = failure_checks[0].test
    read_names = {
        node.func.value.id
        for node in ast.walk(failure_check)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "read_bytes"
        and isinstance(node.func.value, ast.Name)
    }
    sentinel_count = sum(
        1
        for node in ast.walk(failure_check)
        if isinstance(node, ast.Name) and node.id == "sentinel"
    )
    diagnostic_count = sum(
        1
        for node in ast.walk(failure_check)
        if isinstance(node, ast.Name) and node.id == loop_target_names[2]
    )
    if len(read_names) < 2 or sentinel_count < 2 or diagnostic_count < 1:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the failure loop does not diagnose and preserve both outputs"
        )

    def named_stage_pair(name: str) -> tuple[int, ast.Call] | None:
        matches = [
            (index, statement.value)
            for index, statement in enumerate(behavior_function.body)
            if isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == name
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_run_stage_pair"
        ]
        return matches[0] if len(matches) == 1 else None

    def named_status(name: str, expected: int, label: str) -> int | None:
        matches = [
            index
            for index, statement in enumerate(behavior_function.body)
            if isinstance(statement, ast.Expr)
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_expect_status"
            and len(statement.value.args) == 3
            and not statement.value.keywords
            and isinstance(statement.value.args[0], ast.Name)
            and statement.value.args[0].id == name
            and isinstance(statement.value.args[1], ast.Constant)
            and statement.value.args[1].value == expected
            and isinstance(statement.value.args[2], ast.Constant)
            and statement.value.args[2].value == label
        ]
        return matches[0] if len(matches) == 1 else None

    def command_shape(command: ast.expr) -> tuple[object, ...] | None:
        if not isinstance(command, (ast.List, ast.Tuple)):
            return None
        tokens: list[object] = []
        for token in command.elts:
            if isinstance(token, ast.Constant) and isinstance(token.value, str):
                tokens.append(("literal", token.value))
            elif isinstance(token, ast.Name):
                tokens.append(("name", token.id))
            else:
                return None
        return tuple(tokens)

    def stage_pair_commands(
        call: ast.Call,
    ) -> tuple[tuple[object, ...], tuple[object, ...]] | None:
        if (
            len(call.args) != 6
            or call.keywords
            or not isinstance(call.args[0], ast.Name)
            or call.args[0].id != "runner"
            or not isinstance(call.args[1], ast.Name)
            or call.args[1].id != "stage_two"
            or not isinstance(call.args[2], ast.Name)
            or call.args[2].id != "stage_three"
            or not isinstance(call.args[3], ast.Constant)
            or call.args[3].value != "cupidld"
        ):
            return None
        stage_two_command = command_shape(call.args[4])
        stage_three_command = command_shape(call.args[5])
        if stage_two_command is None or stage_three_command is None:
            return None
        return stage_two_command, stage_three_command

    def expected_pe32_command(
        text_address: str, output: str, link_object: str
    ) -> tuple[object, ...]:
        return (
            ("literal", "-m"),
            ("literal", "i386pe"),
            ("literal", "--text-address"),
            ("literal", text_address),
            ("literal", "--entry"),
            ("literal", "_start"),
            ("literal", "-o"),
            ("name", output),
            ("name", link_object),
        )

    def read_bytes_receiver(node: ast.expr) -> str | None:
        if (
            isinstance(node, ast.Call)
            and not node.args
            and not node.keywords
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "read_bytes"
            and isinstance(node.func.value, ast.Name)
        ):
            return node.func.value.id
        return None

    def result_attributes(test: ast.expr, result_name: str) -> set[str]:
        return {
            node.attr
            for node in ast.walk(test)
            if isinstance(node, ast.Attribute)
            and isinstance(node.value, ast.Name)
            and node.value.id == result_name
        }

    pe32_positive = named_stage_pair("pe32_result")
    pe32_positive_status = named_status(
        "pe32_result", 0, "CupidLD PE32 fixed image"
    )
    pe32_failure = named_stage_pair("invalid_pe32_result")
    pe32_failure_status = named_status(
        "invalid_pe32_result", 1, "CupidLD invalid PE32 text address"
    )
    windows_assembly = named_stage_pair("windows_assembly_result")
    windows_assembly_status = named_status(
        "windows_assembly_result", 0, "CupidASM Windows startup"
    )
    windows_compile = named_stage_pair("windows_compile_result")
    windows_compile_status = named_status(
        "windows_compile_result", 0, "CupidC Windows runtime contract"
    )
    windows_link = named_stage_pair("windows_link_result")
    windows_link_status = named_status(
        "windows_link_result", 0, "CupidLD imported Windows image"
    )
    invalid_import_assembly = named_stage_pair(
        "invalid_import_assembly_result"
    )
    invalid_import_assembly_status = named_status(
        "invalid_import_assembly_result",
        0,
        "CupidASM invalid import fixture",
    )
    invalid_import = named_stage_pair("invalid_import_result")
    invalid_import_status = named_status(
        "invalid_import_result", 1, "CupidLD direct IAT call"
    )
    windows_stage_pairs = (
        (windows_assembly, windows_assembly_status, "cupidasm"),
        (windows_compile, windows_compile_status, "cupidc"),
        (windows_link, windows_link_status, "cupidld"),
        (
            invalid_import_assembly,
            invalid_import_assembly_status,
            "cupidasm",
        ),
        (invalid_import, invalid_import_status, "cupidld"),
    )
    windows_stage_order = [
        pair[0]
        for pair, status, _tool in windows_stage_pairs
        if pair is not None and status is not None and pair[0] < status
    ]
    windows_stage_tools_match = all(
        pair is not None
        and len(pair[1].args) == 6
        and isinstance(pair[1].args[3], ast.Constant)
        and pair[1].args[3].value == tool
        for pair, _status, tool in windows_stage_pairs
    )
    if (
        pe32_positive is None
        or pe32_positive_status is None
        or pe32_failure is None
        or pe32_failure_status is None
        or pe32_positive[0] >= pe32_positive_status
        or pe32_failure[0] >= pe32_failure_status
        or len(windows_stage_order) != len(windows_stage_pairs)
        or windows_stage_order != sorted(windows_stage_order)
        or not windows_stage_tools_match
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the paired image, Windows runtime, or failure proof is absent"
        )
    def final_command_name(command: ast.expr) -> str | None:
        if (
            isinstance(command, (ast.List, ast.Tuple))
            and command.elts
            and isinstance(command.elts[-1], ast.Name)
        ):
            return command.elts[-1].id
        return None

    invalid_import_assembly_object_names = tuple(
        final_command_name(call.args[argument_index])
        for call in (invalid_import_assembly[1],)
        for argument_index in (4, 5)
        if len(call.args) == 6
    )
    invalid_import_link_object_names = tuple(
        final_command_name(invalid_import[1].args[argument_index])
        for argument_index in (4, 5)
        if len(invalid_import[1].args) == 6
    )
    if (
        invalid_import_assembly_object_names
        != (
            "stage_two_invalid_import_object",
            "stage_three_invalid_import_object",
        )
        or invalid_import_link_object_names
        != ("invalid_import_object", "invalid_import_object")
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the direct-IAT object pair or stable diagnostic path differs"
        )
    positive_commands = stage_pair_commands(pe32_positive[1])
    failure_commands = stage_pair_commands(pe32_failure[1])
    expected_positive_commands = (
        expected_pe32_command(
            "0x00401000", "stage_two_pe32", "stage_two_link_object"
        ),
        expected_pe32_command(
            "0x00401000", "stage_three_pe32", "stage_three_link_object"
        ),
    )
    expected_failure_commands = (
        expected_pe32_command(
            "0x00402000",
            "stage_two_pe32_failure",
            "stage_two_link_object",
        ),
        expected_pe32_command(
            "0x00402000",
            "stage_three_pe32_failure",
            "stage_three_link_object",
        ),
    )
    if (
        positive_commands != expected_positive_commands
        or failure_commands != expected_failure_commands
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the staged commands do not retain the fixed PE32 profile"
        )
    positive_checks = [
        (index, statement)
        for index, statement in enumerate(behavior_function.body)
        if index > pe32_positive_status
        if isinstance(statement, ast.If)
        and any(
            isinstance(node, ast.Name) and node.id == "pe32_result"
            for node in ast.walk(statement.test)
        )
    ]
    positive_byte_comparisons = (
        [
            (
                read_bytes_receiver(node.left),
                read_bytes_receiver(node.comparators[0]),
            )
            for node in ast.walk(positive_checks[0][1].test)
            if isinstance(node, ast.Compare)
            and len(node.ops) == 1
            and isinstance(node.ops[0], ast.NotEq)
            and len(node.comparators) == 1
            and read_bytes_receiver(node.left) is not None
            and read_bytes_receiver(node.comparators[0]) is not None
        ]
        if len(positive_checks) == 1
        else []
    )
    positive_result_attributes = (
        result_attributes(positive_checks[0][1].test, "pe32_result")
        if len(positive_checks) == 1
        else set()
    )
    validators = [
        (index, statement.value)
        for index, statement in enumerate(behavior_function.body)
        if isinstance(statement, ast.Expr)
        and isinstance(statement.value, ast.Call)
        and isinstance(statement.value.func, ast.Name)
        and statement.value.func.id == "_validate_static_i386_pe32"
    ]
    parser_functions = [
        statement
        for statement in bootstrap_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_validate_static_i386_pe32"
    ]
    parser_source = (
        ast.get_source_segment(bootstrap_source, parser_functions[0]) or ""
        if len(parser_functions) == 1
        else ""
    )
    required_import_parser_fragments = (
        ') = sections[".idata"]',
        "relative_rva = rva - idata_virtual_address",
        "size <= idata_raw_size - relative_rva",
        "size <= idata_virtual_size - relative_rva",
        "idata_raw_offset + relative_rva",
        "idata_raw_offset\n"
        "                    + min(idata_raw_size, idata_virtual_size)",
    )
    import_parser_is_confined = all(
        parser_source.count(fragment) == 1
        for fragment in required_import_parser_fragments
    ) and "for section in sections.values()" not in parser_source

    def expression_shape(source: str) -> str:
        return ast.dump(
            ast.parse(source, mode="eval").body,
            include_attributes=False,
        )

    def node_shape(node: ast.AST) -> str:
        return ast.dump(node, include_attributes=False)

    def immediate_raise_messages(node: ast.If) -> set[str]:
        return {
            child.value
            for statement in node.body
            if isinstance(statement, ast.Raise)
            for child in ast.walk(statement)
            if isinstance(child, ast.Constant)
            and isinstance(child.value, str)
        }

    def guarded_terms(message: str) -> frozenset[str] | None:
        if len(parser_functions) != 1:
            return None
        matches: list[frozenset[str]] = []
        for node in ast.walk(parser_functions[0]):
            if not isinstance(node, ast.If) or not any(
                message in candidate
                for candidate in immediate_raise_messages(node)
            ):
                continue
            terms = (
                node.test.values
                if isinstance(node.test, ast.BoolOp)
                and isinstance(node.test.op, ast.Or)
                else (node.test,)
            )
            matches.append(frozenset(node_shape(term) for term in terms))
        return matches[0] if len(matches) == 1 else None

    expected_guard_terms = {
        "noncanonical DOS stub": (
            "data[:len(_FIXED_PE32_DOS_STUB)] "
            "!= _FIXED_PE32_DOS_STUB",
        ),
        "no PE signature": (
            "data[pe_offset:pe_offset + 4] != b'PE\\0\\0'",
        ),
        "invalid PE32 COFF header": (
            "machine != 0x014C",
            "section_count == 0",
            "section_count > (5 if has_imports else 4)",
            "timestamp != 0",
            "symbol_table != 0",
            "symbol_count != 0",
            "optional_size != 0x00E0",
            "characteristics != 0x0103",
        ),
        "is not PE32": (
            "read_u16(optional_offset, 'PE32 magic') != 0x010B",
        ),
        "invalid PE32 image layout": (
            "linker_major != 0",
            "linker_minor != 0",
            "image_base != 0x00400000",
            "section_alignment != 0x1000",
            "file_alignment != 0x0200",
            "read_u16(optional_offset + 40, "
            "'PE32 OS major version') != 6",
            "read_u16(optional_offset + 42, "
            "'PE32 OS minor version') != 0",
            "read_u16(optional_offset + 44, "
            "'PE32 image major version') != 0",
            "read_u16(optional_offset + 46, "
            "'PE32 image minor version') != 0",
            "read_u16(optional_offset + 48, "
            "'PE32 subsystem major version') != 6",
            "read_u16(optional_offset + 50, "
            "'PE32 subsystem minor version') != 0",
            "read_u32(optional_offset + 52, 'PE32 Win32 version') != 0",
            "image_size == 0",
            "image_size % section_alignment != 0",
            "headers_size == 0",
            "headers_size % file_alignment != 0",
            "headers_size > len(data)",
            "checksum != 0",
            "subsystem != 3",
            "dll_characteristics != 0x0100",
            "stack_reserve != 0x00100000",
            "stack_commit != 0x00001000",
            "heap_reserve != 0x00100000",
            "heap_commit != 0x00001000",
            "loader_flags != 0",
            "directory_count != 16",
            "expected_entry < image_base",
            "entry_rva != expected_entry - image_base",
        ),
        "unexpected PE32 data directory": (
            "directory not in ((1, 12) if has_imports else ()) "
            "and entry != (0, 0)",
        ),
        "omits its PE32 import directories": (
            "has_imports and (directories[1] == (0, 0) "
            "or directories[12] == (0, 0))",
        ),
        "noncanonical PE32 header extent": (
            "headers_size != expected_headers_size",
            "any(data[section_table_end:headers_size])",
        ),
        "invalid PE32 section profile": (
            "expected is None",
            "expected[0] <= previous_section_rank",
            "section_characteristics != expected[1]",
            "raw_name != name.encode('ascii').ljust(8, b'\\0')",
            "read_u32(offset + 24, 'PE32 relocation offset') != 0",
            "read_u32(offset + 28, 'PE32 line offset') != 0",
            "read_u16(offset + 32, 'PE32 relocation count') != 0",
            "read_u16(offset + 34, 'PE32 line count') != 0",
        ),
        "has an empty PE32 section": (
            "virtual_size == 0",
        ),
        "noncanonical PE32 section address": (
            "virtual_address != expected_virtual_address",
        ),
        "PE32 section outside its image": (
            "virtual_end > image_size",
        ),
        "invalid PE32 file section": (
            "name == '.bss'",
            "raw_offset != expected_raw_offset",
            "raw_offset < headers_size",
            "raw_size != expected_section_raw_size",
            "virtual_size > raw_size",
        ),
        "nonzero PE32 section padding": (
            "any(data[raw_offset + virtual_size:raw_offset + raw_size])",
        ),
        "invalid empty PE32 section": (
            "raw_offset != 0",
            "name != '.bss'",
        ),
        "invalid PE32 image extent": (
            "previous_section_rank < 0",
            "expected_raw_offset != len(data)",
            "expected_image_size != image_size",
            "code_size != expected_code_size",
            "initialized_size != expected_initialized_size",
            "uninitialized_size != expected_uninitialized_size",
            "base_of_code != expected_base_of_code",
            "base_of_data != expected_base_of_data",
        ),
        "entry is not file-backed PE32 executable code": (
            "not entry_is_file_backed_executable",
        ),
        "omits its PE32 import section": (
            "'.idata' not in sections",
        ),
        "noncanonical PE32 import directory": (
            "import_rva != idata_virtual_address",
            "import_size != expected_import_size",
        ),
        "stateful PE32 import descriptor": (
            "timestamp != 0",
            "forwarder != 0",
        ),
        "noncanonical PE32 import lookup layout": (
            "lookup_rva != idata_virtual_address + cursor",
        ),
        "noncanonical PE32 import address layout": (
            "iat_rva != idata_virtual_address + cursor",
        ),
        "noncanonical PE32 import name layout": (
            "name_rva != idata_virtual_address + cursor",
        ),
        "unexpected PE32 import library": (
            "rva_string(name_rva, 'import library') != library",
            "data[library_offset:library_offset + len(encoded_library)] "
            "!= encoded_library",
        ),
        "unexpected PE32 import procedure": (
            "lookup != expected_hint_rva",
            "iat != lookup",
            "read_u16(hint_offset, 'PE32 import hint') != 0",
            "data[hint_offset + 2:hint_offset + 2 + "
            "len(encoded_procedure)] != encoded_procedure",
            "rva_string(expected_hint_rva + 2, 'import procedure') "
            "!= procedure",
        ),
        "unterminated PE32 import thunk table": (
            "read_u32(lookup_offset + len(procedures) * 4, "
            "'PE32 import lookup terminator') != 0",
            "read_u32(iat_offset + len(procedures) * 4, "
            "'PE32 import address terminator') != 0",
        ),
        "has nonzero PE32 import alignment": (
            "data[alignment_offset] != 0",
        ),
        "has no null PE32 import descriptor": (
            "any(data[descriptor_offset + len(expected_imports) * 20:"
            "descriptor_offset + (len(expected_imports) + 1) * 20])",
        ),
        "noncanonical PE32 import section extent": (
            "cursor != idata_virtual_size",
        ),
        "noncanonical PE32 IAT directory": (
            "directories[12] != (first_iat, iat_end - first_iat)",
        ),
    }
    parser_guards_match = all(
        guarded_terms(message)
        == frozenset(expression_shape(term) for term in terms)
        for message, terms in expected_guard_terms.items()
    )

    def assignment_shapes(name: str) -> list[str]:
        if len(parser_functions) != 1:
            return []
        return [
            node_shape(node.value)
            for node in ast.walk(parser_functions[0])
            if isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == name
        ]

    expected_field_assignments = {
        "data": "path.read_bytes()",
        "pe_offset": "read_u32(0x3C, 'DOS PE offset')",
        "optional_offset": "pe_offset + 24",
        "linker_major": "data[optional_offset + 2]",
        "linker_minor": "data[optional_offset + 3]",
        "code_size": "read_u32(optional_offset + 4, 'PE32 code size')",
        "initialized_size": (
            "read_u32(optional_offset + 8, 'PE32 initialized data size')"
        ),
        "uninitialized_size": (
            "read_u32(optional_offset + 12, 'PE32 uninitialized data size')"
        ),
        "entry_rva": "read_u32(optional_offset + 16, 'PE32 entry RVA')",
        "base_of_code": "read_u32(optional_offset + 20, 'PE32 code base')",
        "base_of_data": "read_u32(optional_offset + 24, 'PE32 data base')",
        "image_base": "read_u32(optional_offset + 28, 'PE32 image base')",
        "section_alignment": (
            "read_u32(optional_offset + 32, 'PE32 section alignment')"
        ),
        "file_alignment": (
            "read_u32(optional_offset + 36, 'PE32 file alignment')"
        ),
        "image_size": "read_u32(optional_offset + 56, 'PE32 image size')",
        "headers_size": "read_u32(optional_offset + 60, 'PE32 header size')",
        "checksum": "read_u32(optional_offset + 64, 'PE32 checksum')",
        "subsystem": "read_u16(optional_offset + 68, 'PE32 subsystem')",
        "dll_characteristics": (
            "read_u16(optional_offset + 70, 'PE32 DLL characteristics')"
        ),
        "stack_reserve": (
            "read_u32(optional_offset + 72, 'PE32 stack reserve')"
        ),
        "stack_commit": (
            "read_u32(optional_offset + 76, 'PE32 stack commit')"
        ),
        "heap_reserve": (
            "read_u32(optional_offset + 80, 'PE32 heap reserve')"
        ),
        "heap_commit": "read_u32(optional_offset + 84, 'PE32 heap commit')",
        "loader_flags": "read_u32(optional_offset + 88, 'PE32 loader flags')",
        "directory_count": (
            "read_u32(optional_offset + 92, 'PE32 directory count')"
        ),
        "section_offset": "optional_offset + optional_size",
        "section_table_end": "section_offset + section_count * 40",
        "expected_headers_size": (
            "(section_table_end + file_alignment - 1) "
            "// file_alignment * file_alignment"
        ),
        "section_characteristics": (
            "read_u32(offset + 36, 'PE32 section characteristics')"
        ),
        "expected_section_raw_size": (
            "(virtual_size + file_alignment - 1) "
            "// file_alignment * file_alignment"
        ),
        "expected_image_size": (
            "(greatest_virtual_end + section_alignment - 1) "
            "// section_alignment * section_alignment"
        ),
    }
    parser_fields_match = all(
        assignment_shapes(name) == [expression_shape(expression)]
        for name, expression in expected_field_assignments.items()
    )

    expected_sections_values: list[object] = []
    if len(parser_functions) == 1:
        for node in ast.walk(parser_functions[0]):
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "expected_sections"
            ):
                try:
                    expected_sections_values.append(ast.literal_eval(node.value))
                except (TypeError, ValueError):
                    expected_sections_values.append(None)

    expected_dos_stub = bytes.fromhex(
        "4d5a90000300000004000000ffff0000"
        "b8000000000000004000000000000000"
        "00000000000000000000000000000000"
        "00000000000000000000000080000000"
        "0e1fba0e00b409cd21b8014ccd215468"
        "69732070726f6772616d2063616e6e6f"
        "742062652072756e20696e20444f5320"
        "6d6f64652e0d0d0a2400000000000000"
    )
    dos_stub_values: list[bytes | None] = []
    for node in bootstrap_tree.body:
        if (
            not isinstance(node, ast.Assign)
            or len(node.targets) != 1
            or not isinstance(node.targets[0], ast.Name)
            or node.targets[0].id != "_FIXED_PE32_DOS_STUB"
            or not isinstance(node.value, ast.Call)
            or node.value.keywords
            or len(node.value.args) != 1
            or not isinstance(node.value.func, ast.Attribute)
            or node.value.func.attr != "fromhex"
            or not isinstance(node.value.func.value, ast.Name)
            or node.value.func.value.id != "bytes"
            or not isinstance(node.value.args[0], ast.Constant)
            or not isinstance(node.value.args[0].value, str)
        ):
            continue
        try:
            dos_stub_values.append(bytes.fromhex(node.value.args[0].value))
        except ValueError:
            dos_stub_values.append(None)

    parser_reads_image = (
        any(
            read_bytes_receiver(node) == "path"
            for node in ast.walk(parser_functions[0])
            if isinstance(node, ast.Call)
        )
        if len(parser_functions) == 1
        else False
    )
    parser_unpack_shapes = (
        {
            node_shape(node)
            for node in ast.walk(parser_functions[0])
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "struct"
            and node.func.attr == "unpack_from"
        }
        if len(parser_functions) == 1
        else set()
    )
    expected_unpack_shapes = {
        expression_shape("struct.unpack_from('<H', data, offset)"),
        expression_shape("struct.unpack_from('<I', data, offset)"),
        expression_shape(
            "struct.unpack_from('<HHIIIHH', data, pe_offset + 4)"
        ),
        expression_shape("struct.unpack_from('<IIII', data, offset + 8)"),
        expression_shape(
            "struct.unpack_from('<IIIII', data, "
            "descriptor_offset + library_index * 20)"
        ),
    }
    dos_range_indices = (
        [
            index
            for index, statement in enumerate(parser_functions[0].body)
            if isinstance(statement, ast.Expr)
            and node_shape(statement.value)
            == expression_shape(
                "require_range(0, len(_FIXED_PE32_DOS_STUB), 'DOS header')"
            )
        ]
        if len(parser_functions) == 1
        else []
    )
    dos_guard_indices = (
        [
            index
            for index, statement in enumerate(parser_functions[0].body)
            if isinstance(statement, ast.If)
            and any(
                "noncanonical DOS stub" in message
                for message in immediate_raise_messages(statement)
            )
        ]
        if len(parser_functions) == 1
        else []
    )
    validators_by_image = {
        validator.args[0].id: (index, validator)
        for index, validator in validators
        if validator.args
        and isinstance(validator.args[0], ast.Name)
    }
    fixed_validator = validators_by_image.get("stage_two_pe32")
    import_validator = validators_by_image.get("stage_two_windows_image")
    try:
        import_expectation = (
            ast.literal_eval(import_validator[1].args[2])
            if import_validator is not None
            and len(import_validator[1].args) == 3
            else None
        )
    except (TypeError, ValueError):
        import_expectation = None
    native_windows_indices = [
        index
        for index, statement in enumerate(behavior_function.body)
        if isinstance(statement, ast.If)
        and node_shape(statement.test)
        == expression_shape("os.name == 'nt'")
    ]
    required_windows_behavior_fragments = (
        "toolchain/hosted/i386-windows/start.asm",
        "toolchain/tests/hosted_i386_windows_contract.cc",
        "windows_assembly_result = _run_stage_pair(",
        "windows_compile_result = _run_stage_pair(",
        '"--freestanding",',
        "windows_link_result = _run_stage_pair(",
        '("__imp_ExitProcess", "KERNEL32.dll", "ExitProcess")',
        '("__imp_GetStdHandle", "KERNEL32.dll", "GetStdHandle")',
        '("__imp_WriteFile", "KERNEL32.dll", "WriteFile")',
        "windows_import_selectors = tuple(",
        'f"{slot}={library}:{procedure}"',
        "stage_two_windows_start.read_bytes()\n"
        "        != stage_three_windows_start.read_bytes()",
        "stage_two_windows_contract.read_bytes()\n"
        "        != stage_three_windows_contract.read_bytes()",
        "stage_two_windows_image.read_bytes()\n"
        "        != stage_three_windows_image.read_bytes()",
        "[str(stage_two_windows_image)]",
        "capture_output=True",
        "timeout=10",
        "native_result.returncode != 37",
        'native_result.stdout != b"Cupid-built Windows runtime: ok\\n"',
        "or native_result.stderr",
        'windows_loader: dict[str, object] = {"status": "not-run"}',
        '"return_code": native_result.returncode',
        'evidence_out["windows_runtime"] = {',
        '"artifacts": _artifact_inventory(',
        '"library": library',
        '"procedure": procedure',
        '"slot": slot',
        '"loader": windows_loader',
        "invalid_import_assembly_result = _run_stage_pair(",
        "call __imp_ExitProcess\\n",
        "stage_two_invalid_import_object.read_bytes()\n"
        "        != stage_three_invalid_import_object.read_bytes()",
        "invalid_import_object.write_bytes(\n"
        "        stage_two_invalid_import_object.read_bytes()\n"
        "    )",
        "invalid_import_result = _run_stage_pair(",
        "stage_two_invalid_import_image.write_bytes(sentinel)",
        "stage_three_invalid_import_image.write_bytes(sentinel)",
        "IAT symbols require an absolute zero-addend relocation",
        "stage_two_invalid_import_image.read_bytes() != sentinel",
        "stage_three_invalid_import_image.read_bytes() != sentinel",
    )
    missing_windows_behavior_fragments = [
        fragment
        for fragment in required_windows_behavior_fragments
        if behavior_source.count(fragment) != 1
    ]
    if (
        positive_byte_comparisons
        != [("stage_two_pe32", "stage_three_pe32")]
        or positive_result_attributes != {"stdout", "stderr"}
        or len(validators) != 2
        or len(positive_checks) != 1
        or fixed_validator is None
        or import_validator is None
        or not (
            pe32_positive_status
            < positive_checks[0][0]
            < fixed_validator[0]
            < import_validator[0]
            < pe32_failure[0]
        )
        or len(fixed_validator[1].args) != 2
        or fixed_validator[1].keywords
        or not isinstance(fixed_validator[1].args[1], ast.Constant)
        or fixed_validator[1].args[1].value != 0x00401000
        or import_validator[1].keywords
        or not isinstance(import_validator[1].args[1], ast.Constant)
        or import_validator[1].args[1].value != 0x00401000
        or import_expectation
        != ((
            "KERNEL32.dll",
            ("ExitProcess", "GetStdHandle", "WriteFile"),
        ),)
        or missing_windows_behavior_fragments
        or len(native_windows_indices) != 1
        or not (
            import_validator[0]
            < native_windows_indices[0]
            < invalid_import[0]
        )
        or len(parser_functions) != 1
        or not import_parser_is_confined
        or not parser_reads_image
        or parser_unpack_shapes != expected_unpack_shapes
        or not parser_fields_match
        or not parser_guards_match
        or expected_sections_values
        != [
            {
                ".text": (0, 0x60000020),
                ".rodata": (1, 0x40000040),
                ".data": (2, 0xC0000040),
                ".bss": (3, 0xC0000080),
            }
        ]
        or dos_stub_values != [expected_dos_stub]
        or len(dos_range_indices) != 1
        or len(dos_guard_indices) != 1
        or dos_range_indices[0] >= dos_guard_indices[0]
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the staged bytes or independent parser are not checked"
        )
    sentinel_writes = [
        statement.value.func.value.id
        for index, statement in enumerate(behavior_function.body)
        if index < pe32_failure[0]
        and isinstance(statement, ast.Expr)
        and isinstance(statement.value, ast.Call)
        and not statement.value.keywords
        and len(statement.value.args) == 1
        and isinstance(statement.value.args[0], ast.Name)
        and statement.value.args[0].id == "sentinel"
        and isinstance(statement.value.func, ast.Attribute)
        and statement.value.func.attr == "write_bytes"
        and isinstance(statement.value.func.value, ast.Name)
        and statement.value.func.value.id
        in {"stage_two_pe32_failure", "stage_three_pe32_failure"}
    ]
    failure_checks = [
        statement
        for index, statement in enumerate(behavior_function.body)
        if index > pe32_failure_status
        if isinstance(statement, ast.If)
        and any(
            isinstance(node, ast.Name) and node.id == "invalid_pe32_result"
            for node in ast.walk(statement.test)
        )
    ]
    failure_sentinel_checks = (
        {
            read_bytes_receiver(node.left)
            for node in ast.walk(failure_checks[0].test)
            if isinstance(node, ast.Compare)
            and len(node.ops) == 1
            and isinstance(node.ops[0], ast.NotEq)
            and len(node.comparators) == 1
            and isinstance(node.comparators[0], ast.Name)
            and node.comparators[0].id == "sentinel"
            and read_bytes_receiver(node.left) is not None
        }
        if len(failure_checks) == 1
        else set()
    )
    failure_result_attributes = (
        result_attributes(failure_checks[0].test, "invalid_pe32_result")
        if len(failure_checks) == 1
        else set()
    )
    failure_diagnostic_checks = (
        [
            node
            for node in ast.walk(failure_checks[0].test)
            if isinstance(node, ast.Compare)
            and len(node.ops) == 1
            and isinstance(node.ops[0], ast.NotIn)
            and len(node.comparators) == 1
            and isinstance(node.left, ast.Constant)
            and node.left.value
            == "CupidLD PE32 requires text address 0x00401000"
            and isinstance(node.comparators[0], ast.Attribute)
            and node.comparators[0].attr == "stderr"
            and isinstance(node.comparators[0].value, ast.Name)
            and node.comparators[0].value.id == "invalid_pe32_result"
        ]
        if len(failure_checks) == 1
        else []
    )
    if (
        sentinel_writes
        != ["stage_two_pe32_failure", "stage_three_pe32_failure"]
        or failure_sentinel_checks
        != {"stage_two_pe32_failure", "stage_three_pe32_failure"}
        or failure_result_attributes != {"stdout", "stderr"}
        or len(failure_diagnostic_checks) != 1
        or bootstrap_source.count('"cupidld help omits i386pe"') != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the semantic failure does not diagnose and preserve both outputs"
        )

    required_bootstrap_fragments = (
        "def freeze_source_inputs(",
        "destination = snapshot_root / name",
        "if frozen_data != data:",
        "def require_source_closures(",
        "require_frozen_source_snapshot(source_inputs, plan)",
        "live_source_root, plan, source_inputs.inventory",
        "source_inputs = freeze_source_inputs(",
        "private_source_root = source_inputs.root",
        "runner = ToolRunner(private_source_root)",
        'private_source_root / "stage-two",',
        'private_source_root / "stage-three",',
        "behavior_evidence: dict[str, object] = {}",
        "behavior = _run_behavior_checks(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            private_source_root,",
        "            behavior_evidence,\n"
        "        )",
        'windows_runtime = behavior_evidence.get("windows_runtime")',
        'report_path = private_source_root / "bootstrap-report.json"',
        '"windows_loader": windows_loader,',
        '"windows_runtime": windows_runtime,',
        'publication_root = private_workspace / "publication"',
        "for name in BOOTSTRAP_PUBLICATION_NAMES:",
        "(private_source_root / name).replace(",
        "publish_bootstrap_outputs(publication_root, output_root)",
    )
    missing_bootstrap_fragments = [
        fragment
        for fragment in required_bootstrap_fragments
        if bootstrap_source.count(fragment) != 1
    ]
    source_input_functions = [
        statement
        for statement in bootstrap_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_source_input_paths"
    ]
    source_input_strings = (
        [
            node.value
            for node in ast.walk(source_input_functions[0])
            if isinstance(node, ast.Constant)
            and isinstance(node.value, str)
        ]
        if len(source_input_functions) == 1
        else []
    )
    required_windows_source_inputs = (
        "toolchain/hosted/i386-windows/start.asm",
        "toolchain/tests/hosted_i386_windows_contract.cc",
    )
    windows_source_inputs_are_exact = all(
        source_input_strings.count(path) == 1
        for path in required_windows_source_inputs
    )
    publisher_windows_values: list[object] = []
    for statement in contract_publisher_tree.body:
        if (
            isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == "WINDOWS_RUNTIME_INPUTS"
        ):
            try:
                publisher_windows_values.append(ast.literal_eval(statement.value))
            except (TypeError, ValueError):
                publisher_windows_values.append(None)
    publisher_input_functions = [
        statement
        for statement in contract_publisher_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_contract_input_paths"
    ]
    expected_publisher_call = ast.dump(
        ast.parse(
            "paths.update(root / path for path in WINDOWS_RUNTIME_INPUTS)",
            mode="exec",
        ).body[0],
        include_attributes=False,
    )
    publisher_windows_calls = (
        [
            node
            for node in ast.walk(publisher_input_functions[0])
            if isinstance(node, ast.Expr)
            and ast.dump(node, include_attributes=False)
            == expected_publisher_call
        ]
        if len(publisher_input_functions) == 1
        else []
    )
    boundary_fragment = (
        "require_source_closures(source_inputs, source_root, plan)"
    )
    if (
        missing_bootstrap_fragments
        or not windows_source_inputs_are_exact
        or publisher_windows_values != [required_windows_source_inputs]
        or len(publisher_windows_calls) != 1
        or bootstrap_source.count(boundary_fragment) != 4
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point source freeze differs: "
            f"{missing_bootstrap_fragments!r}"
        )

    return {
        "status": "pass",
        "platform": "i386-linux",
        "tool_c_sources": len(expected_toolchain_sources),
        "compiler_c_sources": len(expected_compiler_sources),
        "strict_c_sources": sum(
            1
            for _name, _path, gnu in expected_toolchain_sources
            if not gnu
        ),
        "gnu_c_sources": sum(
            1
            for _name, _path, gnu in expected_toolchain_sources
            if gnu
        ),
        "include_roots": [
            {
                "path": "/toolchain",
                "forms": ["quoted", "angle"],
            },
            {
                "path": "/toolchain/hosted/i386-linux/include",
                "forms": ["angle"],
            },
        ],
        "link_objects": [
            {"tool": name, "objects": len(objects)}
            for name, objects in expected_toolchain_links
        ],
        "tool_images": len(expected_toolchain_links),
        "producer_tools": ["cupidc", "cupidasm", "cupidld"],
        "executed_tools": [
            name for name, _objects in expected_toolchain_links
        ],
        "compared_c_objects": len(expected_toolchain_sources),
        "compared_startup_objects": 1,
        "compared_tool_images": len(expected_toolchain_links),
        "help_cases": len(expected_toolchain_links),
        "success_behavior_cases": 17,
        "failure_behavior_cases": 15,
        "contract_manifest_inputs": 47,
        "source_head_capabilities": [
            "cupidld.pe32_fixed_image",
            "cupidld.pe32_imports",
            "cupid.windows_runtime_probe",
        ],
        "stages": ["generation-one", "stage-two", "stage-three"],
        "checked_seed_source_root": "private-captured",
        "checked_seed_source_boundary_checks": 4,
        "checked_seed_publication": "complete-bundle",
    }


def _c_preprocessor_user_wrapper_flags(root: Path) -> str:
    wrapper = root / "tools" / "cupidc_production_compile.py"
    try:
        module = ast.parse(wrapper.read_text(encoding="utf-8"))
    except (OSError, SyntaxError) as error:
        raise AuditError(
            f"CupidC user profile wrapper is unavailable: {error}"
        ) from error
    value = None
    for statement in module.body:
        if (
            isinstance(statement, ast.Assign)
            and any(
                isinstance(target, ast.Name)
                and target.id == "USER_I386_ARGUMENTS"
                for target in statement.targets
            )
        ):
            try:
                value = ast.literal_eval(statement.value)
            except (ValueError, TypeError) as error:
                raise AuditError(
                    "CupidC USER_I386_ARGUMENTS is not a literal tuple"
                ) from error
            break
    expected = ("--freestanding", "-I", "/user")
    if value != expected:
        raise AuditError(
            "CupidC user wrapper profile differs from the checked contract: "
            f"expected={expected!r}, actual={value!r}"
        )
    # CupidC targets i386 directly. Feed that implicit target into the common
    # profile validator beside the wrapper's explicit freestanding and include
    # settings.
    return "-m32 -ffreestanding -I /user"


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
        ("CFLAGS", "CFLAGS_DOOM", "CFLAGS_DOOM_TREE", "OPT"),
    )
    user_wrapper = root / "tools" / "cupidc_production_compile.py"
    if user_wrapper.is_file():
        user_variable = "USER_I386_ARGUMENTS"
        user_flags = _c_preprocessor_user_wrapper_flags(root)
    else:
        # Some focused audit fixtures model the former user Make profile and
        # intentionally omit the production wrapper. Keep those fixtures useful
        # for profile-drift diagnostics while requiring the wrapper in the real
        # production binding validator.
        user_variable = "CFLAGS"
        user_values = _read_evaluated_make_variables(
            root / "user", make, ("CFLAGS",)
        )
        user_flags = user_values["CFLAGS"]
    toolchain_values = _read_evaluated_make_variables(
        root / "toolchain", make, ("CPPFLAGS", "CFLAGS")
    )
    hosted_flags = (
        f"{toolchain_values['CPPFLAGS']} {toolchain_values['CFLAGS']}"
    )
    specifications = (
        ("KERNEL_I386", ".", "CFLAGS", root_values["CFLAGS"]),
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
        (
            "USER_I386",
            "user",
            user_variable,
            user_flags,
        ),
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
        implicit_function_flag = (
            "-Wno-implicit-function-declaration" in flags
        )
        expects_implicit_functions = next(
            policy.implicit_function_declarations == "CTOOL_TRUE"
            for policy in _C_PP_PROFILE_ROWS
            if policy.name == profile
        )
        if implicit_function_flag != expects_implicit_functions:
            raise AuditError(
                f"CupidC profile {profile} implicit-function policy differs "
                f"from Make {variable}: expected="
                f"{expects_implicit_functions!r}, "
                f"actual={implicit_function_flag!r}"
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
        "/toolchain/ctool_host.cc",
        "/toolchain/cupidasm_main.cc",
        "/toolchain/cupiddis_main.cc",
        "/toolchain/cupidc_main.cc",
        "/toolchain/cupidld_main.cc",
        "/toolchain/cupidobj_main.cc",
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


def _validate_user_syscall_abi_transform(
    directory: str,
    transform: dict[str, object],
) -> None:
    expected_inputs = [
        *USER_SYSCALL_ABI_AUDIT_INPUTS,
        "user/Makefile",
    ]
    if (
        directory != "user"
        or transform.get("output") != "user/test-syscall-abi"
        or transform.get("operation") != "verify_user_syscall_abi"
        or transform.get("tools") != ["host_python"]
    ):
        raise AuditError(
            "user syscall ABI verifier differs from its checked "
            "target, operation, or tool contract"
        )
    inputs = transform.get("inputs")
    if inputs != expected_inputs:
        raise AuditError(
            "user syscall ABI verifier inputs changed; "
            f"expected={expected_inputs!r}, actual={inputs!r}"
        )
    recipe = transform.get("recipe")
    if recipe != ["$(USER_SYSCALL_ABI)"]:
        raise AuditError(
            "user syscall ABI verifier recipe changed; "
            f"expected=['$(USER_SYSCALL_ABI)'], actual={recipe!r}"
        )
    markers = _c_preprocessor_recipe_markers(
        transform,
        {"USER_SYSCALL_ABI"},
    )
    if markers != collections.Counter({"USER_SYSCALL_ABI": 1}):
        raise AuditError(
            "user syscall ABI verifier recipe marker changed; "
            f"actual={dict(markers)!r}"
        )


def _validate_native_user_tools_transform(
    directory: str,
    transform: dict[str, object],
) -> None:
    expected_recipe = [
        "$(MAKE) -C ../toolchain build/cupidc.exe build/cupidld.exe"
    ]
    if (
        directory != "user"
        or transform.get("output") != "user/native-user-tools"
        or transform.get("operation") != "recursive_make"
        or transform.get("tools") != ["make"]
        or transform.get("inputs") != []
        or transform.get("recipe") != expected_recipe
    ):
        raise AuditError(
            "native Windows user-tool prerequisite differs from its checked "
            "recursive build contract"
        )


def _read_cupidc_kernel_wrapper(root: Path) -> tuple[str, ast.Module]:
    wrapper = root / "tools" / "cupidc_kernel_compile.py"
    try:
        source = wrapper.read_text(encoding="utf-8")
        return source, ast.parse(source, filename=str(wrapper))
    except (OSError, SyntaxError) as error:
        raise AuditError(
            f"CupidObj profile manifest wrapper is unavailable: {error}"
        ) from error


def _cupidobj_profile_argument_sets(
    tree: ast.Module,
) -> dict[str, tuple[str, ...]]:
    profile_names = (
        "DOOM_COMPAT_I386_ARGUMENTS",
        "DOOM_TREE_I386_ARGUMENTS",
    )
    arguments: dict[str, tuple[str, ...]] = {}
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name) or target.id not in profile_names:
            continue
        if target.id in arguments or not isinstance(statement.value, ast.Tuple):
            raise AuditError(
                "CupidObj profile manifest wrapper profile declarations "
                f"changed: {target.id} is not one literal tuple"
            )
        expanded: list[str] = []
        for item in statement.value.elts:
            if isinstance(item, ast.Constant) and isinstance(item.value, str):
                expanded.append(item.value)
                continue
            if (
                isinstance(item, ast.Starred)
                and isinstance(item.value, ast.Name)
                and item.value.id in arguments
            ):
                expanded.extend(arguments[item.value.id])
                continue
            raise AuditError(
                "CupidObj profile manifest wrapper profile declarations "
                f"changed: {target.id} contains a dynamic argument"
            )
        arguments[target.id] = tuple(expanded)
    missing = [name for name in profile_names if name not in arguments]
    if missing:
        raise AuditError(
            "CupidObj profile manifest wrapper profile declarations "
            f"changed: missing {missing!r}"
        )
    return arguments


def _cupidobj_profile_include_roots(
    root: Path,
    tree: ast.Module,
) -> tuple[Path, ...]:
    try:
        resolved_root = root.resolve(strict=True)
    except OSError as error:
        raise AuditError(
            f"CupidObj profile manifest root is unavailable: {error}"
        ) from error
    include_names = set()
    for profile, arguments in _cupidobj_profile_argument_sets(tree).items():
        for index, argument in enumerate(arguments):
            if argument != "-I":
                continue
            if index + 1 == len(arguments):
                raise AuditError(
                    "CupidObj profile manifest wrapper profile declarations "
                    f"changed: {profile} ends with -I"
                )
            include_name = arguments[index + 1]
            normalized = posixpath.normpath(include_name)
            if (
                not include_name.startswith("/")
                or "\\" in include_name
                or normalized != include_name
                or include_name == "/"
            ):
                raise AuditError(
                    "CupidObj profile manifest wrapper has an invalid "
                    f"include root: {include_name!r}"
                )
            include_names.add(include_name[1:])

    include_roots = []
    for include_name in sorted(include_names):
        include_root = resolved_root / include_name
        try:
            resolved_include = include_root.resolve(strict=True)
            resolved_include.relative_to(resolved_root)
        except (OSError, ValueError) as error:
            raise AuditError(
                "CupidObj profile manifest include root is unavailable: "
                f"/{include_name}"
            ) from error
        if not resolved_include.is_dir():
            raise AuditError(
                "CupidObj profile manifest include root is not a directory: "
                f"/{include_name}"
            )
        include_roots.append(resolved_include)
    return tuple(include_roots)


def _path_is_link_or_junction(path: Path) -> bool:
    if path.is_symlink():
        return True
    isjunction = getattr(os.path, "isjunction", None)
    return bool(isjunction is not None and isjunction(path))


def _cupidobj_profile_manifest_expected_inputs(root: Path) -> list[str]:
    _source, tree = _read_cupidc_kernel_wrapper(root)
    resolved_root = root.resolve(strict=True)
    profile_inputs = set()
    try:
        for include_root in _cupidobj_profile_include_roots(root, tree):
            for path in include_root.rglob("*"):
                relative_parts = path.relative_to(include_root).parts
                if any(part.startswith(".") for part in relative_parts):
                    continue
                if _path_is_link_or_junction(path):
                    raise AuditError(
                        "CupidObj profile manifest input may not be a link "
                        f"or junction: {path.relative_to(resolved_root).as_posix()}"
                    )
                if not path.is_file() or path.suffix not in {".h", ".inc"}:
                    continue
                try:
                    relative = path.resolve(strict=True).relative_to(
                        resolved_root
                    )
                except (OSError, ValueError) as error:
                    raise AuditError(
                        "CupidObj profile manifest input escapes the "
                        f"repository: {path}"
                    ) from error
                profile_inputs.add(relative.as_posix())
    except OSError as error:
        raise AuditError(
            f"CupidObj profile manifest inputs are unavailable: {error}"
        ) from error
    return [
        *sorted(profile_inputs),
        *_CUPIDOBJ_PROFILE_MANIFEST_CONTROL_INPUTS,
    ]


def _normalized_make_lines(source: str) -> list[str]:
    logical_source = re.sub(r"\\\r?\n[ \t]*", " ", source)
    return [
        " ".join(line.split())
        for line in logical_source.splitlines()
        if line.strip()
    ]


def _is_cupidobj_profile_manifest_production_root(root: Path) -> bool:
    return (
        all(
            (root / relative).is_file()
            for relative in _CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_FILES
        )
        and all(
            (root / relative).is_dir()
            for relative in _CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_DIRECTORIES
        )
    )


def _validate_cupidobj_profile_manifest_make_source(source: str) -> None:
    lines = _normalized_make_lines(source)
    required_lines = (
        "DOOM_CUPIDC_INPUT_MANIFEST := "
        "build/bootstrap/doom-cupidc-inputs.json",
        "$(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS) "
        "$(CHECKED_SEED_INPUTS) tools/cupidc_kernel_compile.py",
        "$(PYTHON) tools/cupidc_kernel_compile.py --root . "
        "--manifest $(BOOTSTRAP_SEED_MANIFEST) "
        "--write-profile-input-manifest $@",
    )
    changed = [line for line in required_lines if lines.count(line) != 1]
    if changed:
        raise AuditError(
            "CupidObj profile manifest Make contract changed; expected one "
            f"copy of each line: {changed!r}"
        )


def _cupidobj_wrapper_structure_error(detail: str) -> None:
    raise AuditError(
        f"CupidObj profile manifest wrapper structure changed: {detail}"
    )


def _single_cupidobj_wrapper_node(
    publisher: ast.AST,
    node_type: type[ast.AST],
    source: str,
    description: str,
) -> ast.AST:
    matches = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, node_type) and ast.unparse(node) == source
    ]
    if len(matches) != 1:
        _cupidobj_wrapper_structure_error(
            f"expected one {description}, found {len(matches)}"
        )
    return matches[0]


def _cupidobj_wrapper_nodes(
    publisher: ast.AST,
    node_type: type[ast.AST],
    source: str,
    description: str,
    count: int,
) -> list[ast.AST]:
    matches = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, node_type) and ast.unparse(node) == source
    ]
    if len(matches) != count:
        _cupidobj_wrapper_structure_error(
            f"expected {count} {description}, found {len(matches)}"
        )
    return sorted(
        matches,
        key=lambda node: (node.lineno, node.col_offset),
    )


def _single_cupidobj_wrapper_if(
    publisher: ast.AST,
    condition: str,
    description: str,
) -> ast.If:
    matches = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, ast.If) and ast.unparse(node.test) == condition
    ]
    if len(matches) != 1:
        _cupidobj_wrapper_structure_error(
            f"expected one {description}, found {len(matches)}"
        )
    return matches[0]


def _ast_contains(parent: ast.AST, child: ast.AST) -> bool:
    return any(node is child for node in ast.walk(parent))


def _ast_statements_contain(
    statements: list[ast.stmt],
    child: ast.AST,
) -> bool:
    return any(_ast_contains(statement, child) for statement in statements)


def _ast_node_is_statically_dead(
    node: ast.AST,
    publisher: ast.AST,
    parents: dict[ast.AST, ast.AST],
) -> bool:
    current = node
    while current is not publisher and current in parents:
        parent = parents[current]
        if isinstance(
            parent,
            (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda),
        ) and parent is not publisher:
            return True
        if isinstance(parent, ast.If) and isinstance(
            parent.test,
            ast.Constant,
        ) and isinstance(parent.test.value, bool):
            if (
                not parent.test.value
                and current in parent.body
                or parent.test.value
                and current in parent.orelse
            ):
                return True
        if (
            isinstance(parent, ast.While)
            and isinstance(parent.test, ast.Constant)
            and parent.test.value is False
            and current in parent.body
        ):
            return True
        current = parent
    return False


def _if_raises_kernel_compile_error(statement: ast.AST) -> bool:
    if not isinstance(statement, ast.If) or len(statement.body) != 1:
        return False
    raised = statement.body[0]
    return (
        isinstance(raised, ast.Raise)
        and isinstance(raised.exc, ast.Call)
        and isinstance(raised.exc.func, ast.Name)
        and raised.exc.func.id == "KernelCompileError"
    )


def _if_returns_false(statement: ast.AST) -> bool:
    if not isinstance(statement, ast.If) or len(statement.body) != 1:
        return False
    returned = statement.body[0]
    return (
        isinstance(returned, ast.Return)
        and isinstance(returned.value, ast.Constant)
        and returned.value.value is False
    )


def _validate_cupidobj_profile_manifest_wrapper(root: Path) -> None:
    _source, tree = _read_cupidc_kernel_wrapper(root)
    functions = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "write_profile_input_manifest"
    ]
    if len(functions) != 1:
        _cupidobj_wrapper_structure_error(
            f"expected one publisher, found {len(functions)}"
        )
    publisher = functions[0]

    output_directory = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "output_directory = _capture_profile_directory(resolved.parent, "
        "'profile output directory')",
        "output-directory capture",
    )
    publication_lock = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "publication_lock = _acquire_profile_manifest_lock(resolved)",
        "publication lock",
    )
    checked_seed = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "checked_seed = verify_seed_inputs(manifest_path)",
        "initial checked-seed verification",
    )
    capture = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "capture = _capture_profile_inputs(root)",
        "profile-input capture",
    )
    document = _single_cupidobj_wrapper_node(
        publisher,
        ast.Call,
        "_profile_input_document(root, capture)",
        "Python oracle document",
    )
    frozen_seed = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "frozen_seed = freeze_seed_inputs(manifest_path, private / "
        "'checked-seed')",
        "private checked-seed freeze",
    )
    frozen_seed_check = _single_cupidobj_wrapper_if(
        publisher,
        "frozen_seed.manifest_sha256 != checked_seed.manifest_sha256",
        "frozen-seed identity gate",
    )
    snapshot = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "snapshot_payload = _profile_snapshot_bytes(root, capture)",
        "profile snapshot",
    )
    snapshot_capture = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "snapshot_capture = _capture_profile_file(snapshot, 'CupidObj "
        "profile snapshot')",
        "profile-snapshot capture",
    )
    arguments = _single_cupidobj_wrapper_node(
        publisher,
        ast.AnnAssign,
        "arguments: tuple[str | Path, ...] = ('profile-manifest', "
        "snapshot.resolve(), '-o', candidate.resolve(strict=False))",
        "CupidObj profile-manifest argument vector",
    )
    seed_run = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "result = run_seed_tool(manifest_path, private, 'cupidobj', "
        "arguments, timeout=60, frozen_seed=frozen_seed)",
        "checked CupidObj invocation",
    )
    snapshot_recheck = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_require_profile_file_unchanged(snapshot_capture, 'CupidObj "
        "profile snapshot')",
        "profile-snapshot recheck",
    )
    checked_output = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "checked_output = _capture_profile_file(candidate, 'checked "
        "CupidObj profile manifest output')",
        "CupidObj candidate capture",
    )
    candidate_capture = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "candidate_capture = checked_output",
        "verified candidate selection",
    )
    parity_gate = _single_cupidobj_wrapper_if(
        publisher,
        "candidate_capture.payload != oracle_payload",
        "CupidObj/Python parity gate",
    )
    live_seed = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "live_seed = verify_seed_inputs(manifest_path)",
        "live checked-seed verification",
    )
    live_seed_check = _single_cupidobj_wrapper_if(
        publisher,
        "live_seed.manifest_sha256 != checked_seed.manifest_sha256",
        "live checked-seed identity gate",
    )
    profile_recheck = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_require_profile_inputs_unchanged(root, capture)",
        "live profile-input recheck",
    )
    directory_rechecks = _cupidobj_wrapper_nodes(
        publisher,
        ast.Expr,
        "_require_profile_directory_unchanged(output_directory)",
        "output-directory rechecks",
        3,
    )
    output_recheck = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_require_profile_output_unchanged(resolved, initial_output)",
        "live output recheck",
    )
    unchanged_gate = _single_cupidobj_wrapper_if(
        publisher,
        "initial_output is not None and initial_output.payload == "
        "candidate_capture.payload",
        "unchanged-output gate",
    )
    publication = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_replace_profile_candidate_with_retry(candidate_capture, resolved, "
        "initial_output, output_directory)",
        "checked candidate publication",
    )
    release = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_release_profile_manifest_lock(publication_lock)",
        "publication-lock release",
    )
    checked_branch = _single_cupidobj_wrapper_if(
        publisher,
        "manifest_path is not None",
        "checked-seed branch",
    )
    manifest_branch = _single_cupidobj_wrapper_if(
        publisher,
        "manifest_path is None",
        "checked CupidObj branch",
    )

    parents = {
        child: parent
        for parent in ast.walk(publisher)
        for child in ast.iter_child_nodes(parent)
    }
    ordered_nodes = (
        output_directory,
        publication_lock,
        directory_rechecks[0],
        checked_seed,
        capture,
        document,
        frozen_seed,
        frozen_seed_check,
        snapshot,
        snapshot_capture,
        arguments,
        seed_run,
        directory_rechecks[1],
        snapshot_recheck,
        checked_output,
        candidate_capture,
        parity_gate,
        live_seed,
        live_seed_check,
        profile_recheck,
        directory_rechecks[2],
        output_recheck,
        unchanged_gate,
        publication,
        release,
    )
    if any(
        _ast_node_is_statically_dead(node, publisher, parents)
        for node in ordered_nodes
    ):
        _cupidobj_wrapper_structure_error(
            "a required safety step is in statically dead code"
        )
    locations = [
        (node.lineno, node.col_offset) for node in ordered_nodes
    ]
    if locations != sorted(locations) or len(set(locations)) != len(locations):
        _cupidobj_wrapper_structure_error(
            "the safety steps are not in their checked order"
        )

    outer_tries = [
        statement
        for statement in publisher.body
        if isinstance(statement, ast.Try)
        and _ast_statements_contain(statement.finalbody, release)
    ]
    temporary_scopes = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, ast.With)
        and any(
            ast.unparse(item.context_expr)
            == "tempfile.TemporaryDirectory(prefix=f'.{resolved.name}.profile-', "
            "dir=resolved.parent)"
            for item in node.items
        )
    ]
    if len(outer_tries) != 1 or len(temporary_scopes) != 1:
        _cupidobj_wrapper_structure_error(
            "the publication try/finally or private workspace changed"
        )
    outer_try = outer_tries[0]
    temporary_scope = temporary_scopes[0]
    if (
        output_directory not in publisher.body
        or publication_lock not in publisher.body
        or directory_rechecks[0] not in outer_try.body
        or checked_branch not in outer_try.body
        or temporary_scope not in outer_try.body
        or release not in outer_try.finalbody
        or not _ast_statements_contain(checked_branch.body, checked_seed)
        or manifest_branch not in temporary_scope.body
        or not _ast_statements_contain(
            manifest_branch.orelse,
            seed_run,
        )
        or not all(
            _ast_statements_contain(manifest_branch.orelse, node)
            for node in (
                frozen_seed,
                frozen_seed_check,
                snapshot,
                snapshot_capture,
                arguments,
                directory_rechecks[1],
                snapshot_recheck,
                checked_output,
                candidate_capture,
                parity_gate,
                live_seed,
                live_seed_check,
            )
        )
        or not all(
            node in temporary_scope.body
            for node in (
                profile_recheck,
                directory_rechecks[2],
                output_recheck,
                unchanged_gate,
                publication,
            )
        )
    ):
        _cupidobj_wrapper_structure_error(
            "a safety step moved outside its checked control-flow scope"
        )
    if not all(
        _if_raises_kernel_compile_error(node)
        for node in (frozen_seed_check, parity_gate, live_seed_check)
    ) or not _if_returns_false(unchanged_gate):
        _cupidobj_wrapper_structure_error(
            "a safety gate no longer stops or preserves publication"
        )


def _validate_cupidobj_profile_manifest_delivery(
    root: Path,
    transforms: list[dict[str, object]],
) -> None:
    makefile = root / "Makefile"
    try:
        make_source = makefile.read_text(encoding="utf-8")
    except OSError as error:
        raise AuditError(f"could not read root Makefile: {error}") from error
    deliveries = [
        transform
        for transform in transforms
        if transform.get("output") == _CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
    ]
    if not deliveries and not _is_cupidobj_profile_manifest_production_root(
        root
    ):
        return

    _validate_cupidobj_profile_manifest_make_source(make_source)
    if len(deliveries) != 1:
        raise AuditError(
            "CupidObj profile manifest delivery must appear exactly once; "
            f"found {len(deliveries)}"
        )
    delivery = deliveries[0]
    if (
        delivery.get("operation") != "generate_profile_manifest"
        or delivery.get("tools") != ["cupid_object", "host_python"]
        or delivery.get("recipe") != _CUPIDOBJ_PROFILE_MANIFEST_RECIPE
    ):
        raise AuditError(
            "CupidObj profile manifest delivery differs from its checked "
            "operation, tools, or recipe"
        )
    expected_inputs = _cupidobj_profile_manifest_expected_inputs(root)
    inputs = delivery.get("inputs")
    if inputs != expected_inputs:
        actual_inputs = inputs if isinstance(inputs, list) else []
        missing = [path for path in expected_inputs if path not in actual_inputs]
        unexpected = [
            path for path in actual_inputs if path not in expected_inputs
        ]
        raise AuditError(
            "CupidObj profile manifest inputs changed; "
            f"missing={missing!r}, unexpected={unexpected!r}, "
            f"order_changed={not missing and not unexpected}"
        )
    _validate_cupidobj_profile_manifest_wrapper(root)


_CUPIDOBJ_INSTALL_SOURCE_DELIVERIES = {
    "kernel/util/bin_programs_gen.cc": (
        "$(CUPIDOBJ) install-source bin --bin $(BIN_CC_SRCS) "
        "--headers $(BIN_HDR_SRCS) --browser $(BROWSER_SUB_SRCS) -o $@",
        collections.Counter(
            {
                "CUPIDOBJ": 1,
                "BIN_CC_SRCS": 1,
                "BIN_HDR_SRCS": 1,
                "BROWSER_SUB_SRCS": 1,
            }
        ),
    ),
    "kernel/util/docs_programs_gen.cc": (
        "$(CUPIDOBJ) install-source docs --ctxt $(DOC_CTXT_SRCS) "
        "--doc-assets $(DOC_ASSET_SRCS) --home-assets "
        "$(HOME_ASSET_SRCS) -o $@",
        collections.Counter(
            {
                "CUPIDOBJ": 1,
                "DOC_CTXT_SRCS": 1,
                "DOC_ASSET_SRCS": 1,
                "HOME_ASSET_SRCS": 1,
            }
        ),
    ),
    "kernel/util/demos_programs_gen.cc": (
        "$(CUPIDOBJ) install-source demos --demos $(DEMO_ASM_SRCS) -o $@",
        collections.Counter({"CUPIDOBJ": 1, "DEMO_ASM_SRCS": 1}),
    ),
}


def _cupidobj_install_source_expected_content(
    transforms: list[dict[str, object]],
) -> dict[str, set[str]]:
    wrapped_text: set[str] = set()
    wrapped_binary: set[str] = set()
    for transform in transforms:
        operation = transform.get("operation")
        if operation not in {
            "wrap_text_as_elf32_relocatable",
            "wrap_binary_as_elf32_relocatable",
        }:
            continue
        inputs = transform.get("inputs")
        if not isinstance(inputs, list):
            continue
        destination = (
            wrapped_text
            if operation == "wrap_text_as_elf32_relocatable"
            else wrapped_binary
        )
        destination.update(
            path for path in inputs if isinstance(path, str)
        )

    bin_content = {
        path
        for path in wrapped_text
        if (
            path.startswith("bin/")
            and path.endswith((".cc", ".h"))
        )
    }
    documents = {
        path
        for path in wrapped_text
        if path.startswith("cupidos-txt/") and path.endswith(".CTXT")
    }
    demos = {
        path
        for path in wrapped_text
        if path.startswith("demos/") and path.endswith(".asm")
    }
    assets = {
        "image.bmp",
        "snail.bmp",
        "test.png",
        "file_example_JPG_1MB.jpg",
    }
    missing_assets = sorted(assets - wrapped_binary)
    if missing_assets:
        raise AuditError(
            "CupidObj install-source asset wraps changed; missing="
            f"{missing_assets!r}"
        )
    return {
        "kernel/util/bin_programs_gen.cc": bin_content,
        "kernel/util/docs_programs_gen.cc": documents | assets,
        "kernel/util/demos_programs_gen.cc": demos,
    }


def _validate_cupidobj_install_source_delivery(
    directory: str,
    transform: dict[str, object],
    expected_content: set[str],
) -> None:
    output = str(transform.get("output", "<unknown>"))
    contract = _CUPIDOBJ_INSTALL_SOURCE_DELIVERIES.get(output)
    expected_recipe = [contract[0]] if contract is not None else None
    if (
        directory != "."
        or contract is None
        or transform.get("operation") != "generate_install_source"
        or transform.get("tools") != ["cupid_object", "host_python"]
        or transform.get("recipe") != expected_recipe
    ):
        raise AuditError(
            "CupidObj install-source delivery differs from its checked "
            "target, operation, tool, or recipe contract"
        )
    inputs = transform.get("inputs")
    if not isinstance(inputs, list):
        raise AuditError("CupidObj install-source delivery inputs are absent")
    required_inputs = {
        "Makefile",
        "tools/bootstrap_toolchain.py",
        "bootstrap/seeds/i386-linux/manifest.json",
        "bootstrap/seeds/i386-linux/cupidasm.elf",
        "bootstrap/seeds/i386-linux/cupidc.elf",
        "bootstrap/seeds/i386-linux/cupiddis.elf",
        "bootstrap/seeds/i386-linux/cupidld.elf",
        "bootstrap/seeds/i386-linux/cupidobj.elf",
    }
    missing_inputs = sorted(required_inputs - set(inputs))
    if missing_inputs:
        raise AuditError(
            "CupidObj install-source delivery lost checked inputs: "
            f"{missing_inputs!r}"
        )
    content_inputs = [
        path for path in inputs if path not in required_inputs
    ]
    if len(content_inputs) != len(set(content_inputs)):
        raise AuditError(
            "CupidObj install-source delivery has duplicate content inputs"
        )
    content_set = set(content_inputs)
    if output == "kernel/util/bin_programs_gen.cc":
        programs = {
            path
            for path in content_set
            if path.startswith("bin/")
            and path.count("/") == 1
            and path.endswith(".cc")
        }
        headers = {
            path
            for path in content_set
            if path.startswith("bin/")
            and path.count("/") == 1
            and path.endswith(".h")
        }
        browser = {
            path
            for path in content_set
            if path.startswith("bin/browser/") and path.endswith(".cc")
        }
        expected_headers = {"bin/fat16.h", "bin/shell.h"}
        content_valid = (
            len(programs) == 105
            and headers == expected_headers
            and len(browser) == 22
            and content_set == programs | headers | browser
        )
    elif output == "kernel/util/docs_programs_gen.cc":
        documents = {
            path
            for path in content_set
            if path.startswith("cupidos-txt/") and path.endswith(".CTXT")
        }
        assets = {
            "image.bmp",
            "snail.bmp",
            "test.png",
            "file_example_JPG_1MB.jpg",
        }
        content_valid = (
            len(documents) == 19
            and assets.issubset(content_set)
            and content_set == documents | assets
        )
    else:
        demos = {
            path
            for path in content_set
            if path.startswith("demos/") and path.endswith(".asm")
        }
        content_valid = len(demos) == 22 and content_set == demos
    if not content_valid:
        raise AuditError(
            "CupidObj install-source delivery content inputs changed for "
            f"{output}: {sorted(content_set)!r}"
        )
    if content_set != expected_content:
        missing_content = sorted(expected_content - content_set)
        unexpected_content = sorted(content_set - expected_content)
        raise AuditError(
            "CupidObj install-source delivery content inputs changed for "
            f"{output}: missing={missing_content!r}, "
            f"unexpected={unexpected_content!r}"
        )
    expected_markers = contract[1]
    markers = _c_preprocessor_recipe_markers(
        transform, set(expected_markers)
    )
    if markers != expected_markers:
        raise AuditError(
            "CupidObj install-source delivery recipe markers changed; "
            f"actual={dict(markers)!r}"
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
        has_install_source_delivery = any(
            isinstance(transform, dict)
            and transform.get("output")
            in _CUPIDOBJ_INSTALL_SOURCE_DELIVERIES
            for transform in transforms
        )
        install_source_content = (
            _cupidobj_install_source_expected_content(transforms)
            if directory == "." and has_install_source_delivery
            else {}
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
                == "toolchain/build/cupidc-contracts/manifest.json"
            ):
                inputs = transform.get("inputs")
                if (
                    operation != "host_orchestration"
                    or tools != ["host_python"]
                    or not isinstance(inputs, list)
                    or not all(isinstance(path, str) for path in inputs)
                ):
                    raise AuditError(
                        "CupidC contract cohort transform differs from "
                        "the checked orchestration contract"
                    )
                closure_roots = [
                    _c_preprocessor_logical_path(path)
                    for path in inputs
                    if _language(path) in {"c", "cupid_c"}
                ]
                expected_closure = (
                    _C_PP_HOSTED_I386_STRICT_CASES
                    + _C_PP_HOSTED_I386_GNU_CASES
                    + _C_PP_TOOLCHAIN_CONTRACT_CASES
                )
                _c_preprocessor_require_exact_paths(
                    "toolchain contract closure",
                    closure_roots,
                    expected_closure,
                )
                required_inputs = {
                    "bootstrap/seeds/i386-linux/manifest.json",
                    "bootstrap/seeds/i386-linux/cupidasm.elf",
                    "bootstrap/seeds/i386-linux/cupidc.elf",
                    "bootstrap/seeds/i386-linux/cupiddis.elf",
                    "bootstrap/seeds/i386-linux/cupidld.elf",
                    "bootstrap/seeds/i386-linux/cupidobj.elf",
                    "link.ld",
                    "toolchain/hosted/i386-linux/start.asm",
                    "toolchain/hosted/i386-windows/start.asm",
                    "toolchain/Makefile",
                    "tools/bootstrap_toolchain.py",
                    "tools/cupidc_toolchain_contracts.py",
                }
                missing_inputs = sorted(required_inputs - set(inputs))
                if missing_inputs:
                    raise AuditError(
                        "CupidC contract cohort lost checked inputs: "
                        f"{missing_inputs!r}"
                    )
                recipe = transform.get("recipe")
                normalized_recipe = " ".join(
                    token
                    for token in "\n".join(
                        recipe if isinstance(recipe, list) else []
                    ).split()
                    if token != "\\"
                )
                expected_recipe = (
                    "$(PYTHON) ../tools/cupidc_toolchain_contracts.py build "
                    "--root .. --manifest "
                    "../bootstrap/seeds/i386-linux/manifest.json "
                    "--output $(CONTRACT_DIR)"
                )
                if (
                    not isinstance(recipe, list)
                    or not all(isinstance(line, str) for line in recipe)
                    or normalized_recipe != expected_recipe
                ):
                    raise AuditError(
                        "CupidC contract cohort recipe no longer invokes the "
                        "checked fixed-point builder"
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
                    + tuple(
                        path
                        for path in _C_PP_TOOLCHAIN_CONTRACT_CASES
                        if path not in _C_PP_HOSTED_BRIDGE_CASES
                        and path
                        != "/toolchain/tests/hosted_i386_windows_contract.cc"
                    )
                )
                active_by_profile["FREESTANDING_I386"].append(
                    "/toolchain/tests/hosted_i386_windows_contract.cc"
                )
                active_by_profile["HOSTED_I386_KERNEL_BRIDGE"].extend(
                    sorted(_C_PP_HOSTED_BRIDGE_CASES)
                )
                active_by_profile["HOSTED_I386_LINUX_GNU"].extend(
                    _C_PP_HOSTED_I386_GNU_CASES
                )
                continue
            if operation == "verify_user_syscall_abi":
                _validate_user_syscall_abi_transform(directory, transform)
                continue
            if (
                directory == "user"
                and output == "user/native-user-tools"
            ):
                _validate_native_user_tools_transform(directory, transform)
                continue
            if (
                directory == "."
                and output in _CUPIDOBJ_INSTALL_SOURCE_DELIVERIES
            ):
                _validate_cupidobj_install_source_delivery(
                    directory,
                    transform,
                    install_source_content[output],
                )
                continue
            if operation in {
                "compile_c_to_elf32_object",
                "compile_c_to_host_object",
            }:
                allowed_compile_tools = (
                    ["host_c_compiler"],
                    ["cupid_c_compiler", "host_python"],
                )
                if tools not in allowed_compile_tools:
                    raise AuditError(
                        f"CupidC active preprocessing compile transform has "
                        f"unexpected tools for {transform.get('output')}: {tools!r}"
                    )
                if (
                    tools == ["cupid_c_compiler", "host_python"]
                    and (
                        directory not in {".", "user"}
                        or operation != "compile_c_to_elf32_object"
                    )
                ):
                    raise AuditError(
                        "CupidC checked compile transform differs from its "
                        f"freestanding build contract for "
                        f"{transform.get('output')}: "
                        f"directory={directory!r}, operation={operation!r}"
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
            if operation == "recursive_make":
                if tools != ["make"]:
                    raise AuditError(
                        "CupidC recursive Make transform has unexpected tools "
                        f"for {transform.get('output')}: {tools!r}"
                    )
                # A parent Make rule names child-build sources as prerequisites,
                # but it does not deliver those sources into Cupid OS. The
                # supplemental child graph owns their compile classification.
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
            if tools != ["cupid_object", "host_python"]:
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
                "implicit_function_declarations": (
                    profile_policy.implicit_function_declarations
                    == "CTOOL_TRUE"
                ),
                "compatibility_pointer_conversions": (
                    profile_policy.compatibility_pointer_conversions
                    == "CTOOL_TRUE"
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
            f"{profile.gnu_extensions}, {profile.hosted_environment}, "
            f"{profile.implicit_function_declarations}, "
            f"{profile.compatibility_pointer_conversions})"
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
        "- Make conditionals use the canonical `OS=Windows_NT` graph and the "
        "C locale fixes wildcard order on every host. Direct Linux build "
        "tests cover the Linux execution branch.",
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
            if "tool_images" in contract:
                link_counts = ", ".join(
                    f"{entry['tool']}={entry['objects']}"
                    for entry in contract["link_objects"]
                )
                detail = (
                    f"{contract['tool_c_sources']} tool C sources "
                    f"({contract['strict_c_sources']} strict, "
                    f"{contract['gnu_c_sources']} GNU); "
                    f"{contract['tool_images']} tools "
                    f"({link_counts}); "
                    f"{contract['compared_c_objects']} C objects and "
                    f"{contract['compared_startup_objects']} startup object "
                    f"compared across stages; "
                    f"{contract['compared_tool_images']} tool images; "
                    f"{contract['success_behavior_cases']} success and "
                    f"{contract['failure_behavior_cases']} failure cases; "
                    f"{contract['platform']}"
                )
            elif "compiler_c_sources" in contract:
                detail = (
                    f"{contract['compiler_c_sources']} compiler C sources "
                    f"({contract['strict_c_sources']} strict, "
                    f"{contract['gnu_c_sources']} GNU); "
                    f"{contract['link_objects']} linked objects; "
                    f"{contract['compared_c_objects']} C objects and "
                    f"{contract['compared_startup_objects']} startup object "
                    f"compared across stages; "
                    f"{contract['compared_compiler_images']} compiler images; "
                    f"{contract['behavior_cases']} behavior cases; "
                    f"{contract['platform']}"
                )
            elif "tracked_translation_units" in contract:
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

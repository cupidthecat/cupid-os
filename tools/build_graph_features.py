## @file
#  @brief Scan C and assembly inputs for the build graph audit.
"""Collect source features and retain their original line evidence."""

from __future__ import annotations

import bisect
import collections
import re
from dataclasses import dataclass, field
from pathlib import Path


class AuditError(RuntimeError):
    """The supported build graph could not be inventoried."""


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


SOURCE_SUFFIXES = {
    ".c": "c",
    ".cc": "cupid_c",
    ".cup": "cupid_script",
    ".h": "c_header",
    ".asm": "assembly",
    ".s": "assembly",
}

def source_language(path: str) -> str | None:
    if path in {
        "toolchain/cupidbuild_compile_inputs.inc",
        "toolchain/cupidbuild_compile_profile.inc",
        "toolchain/cupidbuild_kernel_image.inc",
        "toolchain/cupidbuild_validation.inc",
    }:
        return "c_header"
    return SOURCE_SUFFIXES.get(Path(path).suffix.lower())


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
    "al",
    "ah",
    "ax",
    "eax",
    "bl",
    "bh",
    "bx",
    "ebx",
    "cl",
    "ch",
    "cx",
    "ecx",
    "dl",
    "dh",
    "dx",
    "edx",
    "si",
    "esi",
    "di",
    "edi",
    "sp",
    "esp",
    "bp",
    "ebp",
    "cs",
    "ds",
    "es",
    "fs",
    "gs",
    "ss",
    "cr0",
    "cr2",
    "cr3",
    "cr4",
    "dr0",
    "dr1",
    "dr2",
    "dr3",
    "dr6",
    "dr7",
    *(f"mm{index}" for index in range(8)),
    *(f"xmm{index}" for index in range(8)),
    *(f"st{index}" for index in range(8)),
}


def mask_c_noncode(text: str) -> str:
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


def mask_c_comments_preserve_literals(text: str) -> str:
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


def c_raw_logical_lines(text: str) -> list[tuple[int, str, str]]:
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


def c_logical_lines(text: str) -> list[tuple[int, str, str]]:
    """Return phase-two logical lines with their first physical location."""
    raw_logical_lines = c_raw_logical_lines(text)
    if not raw_logical_lines:
        return []

    masked_text = mask_c_noncode(
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


def normalize_c_preprocessing_tokens(
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
                width = _c_identifier_unit_width(
                    expression, end, initial=False
                )
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
                "c.preprocessor.function_macro",
                path,
                line_number,
                original_line,
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
            paste_count, stringify_count = _c_macro_operator_counts(
                replacement
            )
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
                    re.findall(r",\s*(?:##|%:%:)\s*__VA_ARGS__\b", replacement)
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
            while (
                operand < len(replacement) and replacement[operand].isspace()
            ):
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


def scan_c_features(
    path: str,
    text: str,
    language: str,
    collector: FeatureCollector,
) -> None:
    logical_lines = c_logical_lines(text)
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

        directive_match = re.match(r"\s*(?:#|%:)\s*([A-Za-z_]\w*)", code_line)
        if directive_match:
            directive = directive_match.group(1).lower()
            feature_id = (
                f"c.preprocessor.{directive}"
                if directive in C_PREPROCESSOR_DIRECTIVES
                or language != "cupid_c"
                else f"cupid_c.directive.{directive}"
            )
            collector.add(feature_id, path, line_number, original_line)
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
            prefix = code_line[: match.start()].rstrip()
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
        for builtin in sorted(
            set(re.findall(r"\b(__builtin_[A-Za-z_]\w*)\b", code_line))
        ):
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


def _scan_asm_features(
    path: str, text: str, collector: FeatureCollector
) -> None:
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
                "asm.label.local"
                if label.startswith(".")
                else "asm.label.global",
                path,
                line_number,
                original_line,
            )
            code_line = code_line[label_match.end() :].lstrip()
            if not code_line:
                continue

        bracketed_directive = _asm_bracketed_directive(code_line)
        scan_line = (
            bracketed_directive
            if bracketed_directive is not None
            else code_line
        )
        first_match = re.match(r"([^\s,]+)", scan_line)
        if first_match is None:
            continue
        first = first_match.group(1).lower()
        mnemonic: str | None = None
        if first.startswith("%"):
            collector.add(
                f"asm.preprocessor.{first[1:]}",
                path,
                line_number,
                original_line,
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
                    f"asm.directive.{directive}",
                    path,
                    line_number,
                    original_line,
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
                    if re.search(
                        rf"\b{data_directive}\b", code_line, re.IGNORECASE
                    ):
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
            collector.add(
                "asm.addressing.memory", path, line_number, original_line
            )
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
                "asm.addressing.segment_override",
                path,
                line_number,
                original_line,
            )
        for size in ("byte", "word", "dword", "qword", "tword"):
            if re.search(
                rf"\b{size}\s*(?:ptr\s*)?\[", code_line, re.IGNORECASE
            ):
                collector.add(
                    f"asm.addressing.size.{size}",
                    path,
                    line_number,
                    original_line,
                )
        if "$$" in code_line:
            collector.add(
                "asm.expression.section_origin",
                path,
                line_number,
                original_line,
            )
        elif "$" in code_line:
            collector.add(
                "asm.expression.current_offset",
                path,
                line_number,
                original_line,
            )
        if first != "extern":
            referenced_externals = [
                symbol
                for symbol in extern_symbols
                if re.search(
                    rf"(?<![\w.$@?]){re.escape(symbol)}(?![\w.$@?])", code_line
                )
            ]
            if referenced_externals:
                relocation = (
                    "pc_relative_external"
                    if mnemonic is not None
                    and (
                        mnemonic == "call"
                        or mnemonic == "jmp"
                        or mnemonic.startswith("j")
                    )
                    else "absolute_external"
                )
                collector.add(
                    f"asm.relocation.{relocation}",
                    path,
                    line_number,
                    original_line,
                    len(referenced_externals),
                )


def scan_source_features(
    path: str,
    source_path: Path,
    language: str,
    collector: FeatureCollector,
) -> None:
    text = source_path.read_text(encoding="utf-8", errors="replace")
    if language in {"c", "c_header", "cupid_c"}:
        scan_c_features(path, text, language, collector)
    elif language == "assembly":
        _scan_asm_features(path, text, collector)

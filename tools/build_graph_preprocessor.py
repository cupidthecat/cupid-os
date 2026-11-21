## @file
#  @brief Check the C preprocessing directives used in the active build.
"""Inventory include operands, line directives, conditions, pragmas, and Cupid #exe blocks."""

from __future__ import annotations

import collections
import re
from dataclasses import dataclass
from pathlib import Path

if __package__:
    from . import build_graph_features as source_features
else:
    import build_graph_features as source_features

AuditError = source_features.AuditError


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


def include_directives(
    text: str, display_path: str
) -> list[CIncludeDirective]:
    raw_logical_lines = source_features.c_raw_logical_lines(text)
    if not raw_logical_lines:
        return []
    logical_text = "\n".join(line for _, _, line in raw_logical_lines)
    source_lines = source_features.mask_c_comments_preserve_literals(logical_text).split("\n")
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
                    source_features.normalize_c_preprocessing_tokens(
                        operand, display_path, line_number
                    )
                )
                evidence = (
                    f"{marker}{directive} {normalized} at line {line_number}"
                )
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
                source_features.normalize_c_preprocessing_tokens(
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


def reject_pp_token_include(
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


def include_operands_contract(
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
        if source_features.source_language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        source_files += 1
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        for directive in include_directives(text, path):
            include_occurrences += 1
            if directive.kind == "quoted":
                direct_quoted_occurrences += 1
            elif directive.kind == "angle":
                direct_angle_occurrences += 1
            else:
                pp_token_operand_occurrences += 1
                reject_pp_token_include(path, directive)
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


def line_directives_contract(
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
        if source_features.source_language(path) in {"c", "c_header", "cupid_c"}
        and path.replace("\\", "/").split("/", 1)[0].casefold() == "templeos"
    )
    if temple_sources:
        raise AuditError(
            f"{temple_sources[0]}: TempleOS reference tree cannot be an "
            "active C preprocessing input"
        )
    for path in sorted(active_sources - generated_sources):
        if source_features.source_language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        source_files += 1
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = source_features.c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(
            code_line for _, _, code_line in logical_lines
        )
        directive_lines = source_features.mask_c_noncode(logical_text).split("\n")
        payload_lines = source_features.mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if len(directive_lines) != len(logical_lines) or len(
            payload_lines
        ) != len(logical_lines):
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
                tokens = source_features.normalize_c_preprocessing_tokens(
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
            tokens = source_features.normalize_c_preprocessing_tokens(
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


def conditionals_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_expression: dict[tuple[str, ...], list[dict[str, object]]] = (
        collections.defaultdict(list)
    )
    for path in sorted(active_sources - generated_sources):
        if source_features.source_language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = source_features.c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(
            code_line for _, _, code_line in logical_lines
        )
        directive_lines = source_features.mask_c_noncode(logical_text).split("\n")
        expression_lines = source_features.mask_c_comments_preserve_literals(
            logical_text
        ).split("\n")
        if len(directive_lines) != len(logical_lines) or len(
            expression_lines
        ) != len(logical_lines):
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
            match = re.match(r"\s*(?:#|%:)\s*(if|elif)\b", directive_line)
            if match is None:
                continue
            directive = match.group(1)
            tokens = source_features.normalize_c_preprocessing_tokens(
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


def pragmas_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[tuple[str, str, int | None], list[dict[str, object]]] = (
        collections.defaultdict(list)
    )
    once_occurrences = 0
    pack_push_occurrences = 0
    pack_pop_occurrences = 0
    pack_underflow_occurrences = 0
    unmatched_pack_pushes = 0
    max_pack_depth = 0
    for path in sorted(active_sources - generated_sources):
        if source_features.source_language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = source_features.c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(
            code_line for _, _, code_line in logical_lines
        )
        directive_lines = source_features.mask_c_noncode(logical_text).split("\n")
        payload_lines = source_features.mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if len(directive_lines) != len(logical_lines) or len(
            payload_lines
        ) != len(logical_lines):
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
                    f"{path}:{line_number}: unclassified active #pragma form: <empty>"
                )
            tokens = source_features.normalize_c_preprocessing_tokens(
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


def cupid_exe_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[tuple[str, str, int], list[dict[str, object]]] = (
        collections.defaultdict(list)
    )
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    max_conditional_depth = 0
    for path in sorted(active_sources - generated_sources):
        if source_features.source_language(path) not in {"cupid_c", "c_header"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = source_features.c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(
            code_line for _, _, code_line in logical_lines
        )
        directive_lines = source_features.mask_c_noncode(logical_text).split("\n")
        payload_lines = source_features.mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if len(directive_lines) != len(logical_lines) or len(
            payload_lines
        ) != len(logical_lines):
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
            match = re.match(r"\s*(#|%:)\s*([A-Za-z_]\w*)\b", directive_line)
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
                    source_features.normalize_c_preprocessing_tokens(
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

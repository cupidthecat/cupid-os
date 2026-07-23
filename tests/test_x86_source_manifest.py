import json
import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
AUDIT = REPO_ROOT / "docs" / "bootstrap" / "audits" / "active-build.json"
ASM_MANIFEST = REPO_ROOT / "toolchain" / "tests" / "x86_active_cases.inc"
INLINE_MANIFEST = REPO_ROOT / "toolchain" / "tests" / "x86_inline_cases.inc"

DIRECTIVES = {
    "section",
    "global",
    "extern",
    "bits",
    "org",
    "times",
    "db",
    "dw",
    "dd",
    "dq",
    "resb",
    "resw",
    "resd",
    "rb",
    "rw",
    "rd",
    "reserve",
    "equ",
    "include",
}

REGISTERS = {
    **{name: "r8" for name in "al cl dl bl ah ch dh bh".split()},
    **{name: "r16" for name in "ax cx dx bx sp bp si di".split()},
    **{name: "r32" for name in "eax ecx edx ebx esp ebp esi edi".split()},
    **{name: "seg" for name in "cs ds es fs gs ss".split()},
    **{name: "cr" for name in "cr0 cr1 cr2 cr3 cr4 cr5 cr6 cr7".split()},
    **{f"xmm{index}": "xmm" for index in range(8)},
}


def audit_data():
    return json.loads(AUDIT.read_text())


def active_asm_files(audit):
    paths = [
        REPO_ROOT / source["path"]
        for source in audit["sources"]
        if source["language"] == "assembly"
    ]
    return sorted(
        paths,
        key=lambda path: (
            path.relative_to(REPO_ROOT).parts[0] != "demos",
            path.relative_to(REPO_ROOT).as_posix(),
        ),
    )


def split_operands(text):
    result = []
    depth = 0
    start = 0
    for index, character in enumerate(text):
        if character == "[":
            depth += 1
        elif character == "]":
            depth -= 1
        elif character == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    if text.strip():
        result.append(text[start:].strip())
    return result


def operand_class(text, prefix):
    value = text.lower().strip()
    qualifier = ""
    match = re.match(r"^(byte|word|dword|qword)\s+(.*)$", value)
    if match:
        qualifier, value = match.groups()
    if value in REGISTERS:
        return REGISTERS[value]
    if value.startswith("st("):
        return "st"
    if "[" in value:
        base = "m" + {
            "byte": "8",
            "word": "16",
            "dword": "32",
            "qword": "64",
        }.get(qualifier, "")
        inner = value[value.find("[") + 1 : value.rfind("]")]
        address_registers = re.findall(
            r"\b(?:e?[abcd]x|e?[sd]i|e?[sb]p)\b", inner
        )
        if "*" in inner:
            shape = "sib"
        elif not address_registers:
            shape = "abs"
        elif re.search(r"[+-]", inner):
            shape = "base-disp"
        else:
            shape = "base"
        if prefix == "a32":
            shape += "-a32"
        return base + ":" + shape
    if ":" in value:
        return "far32" if qualifier == "dword" else "far"
    if re.match(r"^(?:[-+]?\d|[-+]?0x|'.*')", value):
        return "imm"
    return "ref"


def source_records(paths):
    records = []
    for path in paths:
        mode = 32 if path.parent.name == "demos" else None
        relative = path.relative_to(REPO_ROOT).as_posix()
        for line_number, raw in enumerate(
            path.read_text(errors="replace").splitlines(), 1
        ):
            source = raw.split(";", 1)[0].strip()
            if not source:
                continue
            bits = re.match(r"^\[?\s*bits\s+(16|32)\s*\]?$", source, re.I)
            if bits:
                mode = int(bits.group(1))
                continue
            if source.startswith("%") or re.search(r"\bequ\b", source, re.I):
                continue
            if ":" in source:
                left, right = source.split(":", 1)
                if re.match(r"^[.$A-Za-z_][\w.$@?]*$", left.strip()):
                    source = right.strip()
                if not source:
                    continue
            words = source.split(None, 1)
            opcode = words[0].lower().strip("[]")
            arguments = words[1] if len(words) > 1 else ""
            if (
                opcode in DIRECTIVES
                or opcode.startswith("[")
                or opcode == "align"
                or (arguments and arguments.split()[0].lower() in DIRECTIVES)
            ):
                continue
            prefix = ""
            if opcode in ("lock", "rep", "repe", "repne", "a32", "o32"):
                prefix = opcode
                words = arguments.split(None, 1)
                opcode = words[0].lower()
                arguments = words[1] if len(words) > 1 else ""
            records.append(
                (relative, line_number, mode, prefix, opcode, arguments)
            )
    return records


def signature(record):
    _, _, _, prefix, opcode, arguments = record
    semantic_prefix = (
        prefix + " " if prefix in ("lock", "rep", "repe", "repne") else ""
    )
    parts = (semantic_prefix + opcode,) + tuple(
        operand_class(value, prefix) for value in split_operands(arguments)
    )
    return parts[0] + "(" + ",".join(parts[1:]) + ")"


class X86SourceManifestTests(unittest.TestCase):
    def test_checked_manifest_matches_every_active_source_signature(self):
        audit = audit_data()
        paths = active_asm_files(audit)
        records = source_records(paths)
        selected = {}
        for record in records:
            selected.setdefault((record[2], signature(record)), record)

        self.assertEqual(len(paths), 27)
        self.assertEqual(len(records), 1244)
        self.assertEqual(len({record[4] for record in records}), 91)
        self.assertEqual(len({key[1] for key in selected}), 164)
        self.assertEqual(len(selected), 187)

        expected = {
            f"asm:{record[0]}:{record[1]}:bits{mode}:{case_signature}"
            for (mode, case_signature), record in selected.items()
        }
        actual = set()
        for line in ASM_MANIFEST.read_text().splitlines():
            match = re.match(r'^X86_ACTIVE_CASE\("([^"]+)"', line)
            if match is None:
                continue
            label = match.group(1)
            if label.startswith("asm:"):
                actual.add(label)

        self.assertEqual(actual, expected)

    def test_checked_inline_manifest_tracks_the_active_inline_source_cohort(self):
        audit = audit_data()
        feature = next(
            feature
            for feature in audit["features"]
            if feature["id"] == "c.extension.inline_assembly"
        )
        labels = []
        for line in INLINE_MANIFEST.read_text().splitlines():
            match = re.match(r'^X86_ACTIVE_CASE\("([^"]+)"', line)
            if match is not None:
                labels.append(match.group(1))

        self.assertTrue(all(label.startswith("inline:") for label in labels))
        self.assertEqual(len(labels), 129)
        self.assertEqual(len(feature["files"]), 36)
        self.assertEqual(feature["occurrences"], 207)

        spellings = {
            label.split(":", 2)[1].split("->", 1)[0] for label in labels
        }
        self.assertEqual(len(spellings), 96)

        source = "\n".join(
            (REPO_ROOT / path).read_text(errors="replace")
            for path in feature["files"]
        ).lower()
        for spelling in spellings:
            self.assertRegex(
                source,
                rf"(?<![a-z0-9_]){re.escape(spelling)}(?![a-z0-9_])",
            )


if __name__ == "__main__":
    unittest.main()

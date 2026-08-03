import re
import unittest
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
BINDING_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc.cc"

DECLARATION_RE = re.compile(
    r"(?m)^[ \t]*"
    r"(?P<return_type>(?:const[ \t]+)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:[ \t]*\*)?)"
    r"[ \t]*\(\*(?P<pointer>p_[A-Za-z0-9_]+)\)"
    r"[ \t]*\("
)
BIND_RE = re.compile(
    r"(?m)^[ \t]*(?P<macro>BIND(?:_T)?)"
    r"\(\s*\"(?P<name>[^\"]+)\""
    r"\s*,\s*(?P<pointer>p_[A-Za-z0-9_]+)"
    r"\s*,\s*(?P<parameter_count>[0-9]+)"
    r"(?:\s*,\s*(?P<return_type>TYPE_[A-Z0-9_]+))?"
    r"[ \t\r\n]*\);"
)


def _registration_body(source):
    marker = "static void cc_register_kernel_bindings("
    start = source.index(marker)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1:index]
    raise AssertionError("kernel binding registration has no closing brace")


def _expected_cupid_type(return_type):
    compact = re.sub(r"\s+", "", return_type)
    if compact == "void":
        return "TYPE_VOID"
    if compact == "float":
        return "TYPE_FLOAT"
    if compact == "double":
        return "TYPE_DOUBLE"
    if compact.endswith("*"):
        if compact in ("char*", "constchar*"):
            return "TYPE_CHAR_PTR"
        return "TYPE_PTR"
    if compact in ("uint32_t", "size_t", "swap_handle_t"):
        return "TYPE_UINT"
    return "TYPE_INT"


def _binding_contract(source):
    body = _registration_body(source)
    declarations = {}
    duplicate_declarations = []
    for match in DECLARATION_RE.finditer(body):
        pointer = match.group("pointer")
        if pointer in declarations:
            duplicate_declarations.append(pointer)
        declarations[pointer] = match.group("return_type")

    bindings = {}
    duplicate_bindings = []
    for match in BIND_RE.finditer(body):
        pointer = match.group("pointer")
        if pointer in bindings:
            duplicate_bindings.append(pointer)
        bindings[pointer] = {
            "macro": match.group("macro"),
            "name": match.group("name"),
            "return_type": match.group("return_type"),
        }

    violations = []
    for pointer, binding in bindings.items():
        declared_type = declarations.get(pointer)
        if declared_type is None:
            violations.append(
                f"{binding['name']}: missing declaration for {pointer}"
            )
            continue

        expected = _expected_cupid_type(declared_type)
        if expected == "TYPE_VOID":
            if binding["macro"] != "BIND":
                violations.append(
                    f"{binding['name']}: void declaration must use BIND"
                )
            if binding["return_type"] is not None:
                violations.append(
                    f"{binding['name']}: void binding published a result"
                )
        else:
            if binding["macro"] != "BIND_T":
                violations.append(
                    f"{binding['name']}: {declared_type.strip()} "
                    f"declaration used untyped BIND"
                )
            elif binding["return_type"] != expected:
                violations.append(
                    f"{binding['name']}: expected {expected}, got "
                    f"{binding['return_type']}"
                )

    for pointer in sorted(set(declarations) - set(bindings)):
        violations.append(f"{pointer}: declaration has no binding")
    for pointer in duplicate_declarations:
        violations.append(f"{pointer}: duplicate declaration")
    for pointer in duplicate_bindings:
        violations.append(f"{pointer}: duplicate binding")
    return declarations, bindings, violations


class PrivateCupidCKernelBindingTypeTests(unittest.TestCase):
    def test_every_kernel_binding_publishes_its_declared_result_type(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        declarations, bindings, violations = _binding_contract(source)
        self.assertEqual(violations, [])
        self.assertEqual(len(declarations), len(bindings))
        self.assertEqual(len(bindings), 510)
        published_types = Counter(
            binding["return_type"]
            if binding["macro"] == "BIND_T"
            else "TYPE_VOID"
            for binding in bindings.values()
        )
        self.assertEqual(
            published_types,
            Counter(
                {
                    "TYPE_INT": 205,
                    "TYPE_VOID": 191,
                    "TYPE_UINT": 40,
                    "TYPE_DOUBLE": 25,
                    "TYPE_FLOAT": 25,
                    "TYPE_CHAR_PTR": 19,
                    "TYPE_PTR": 5,
                }
            ),
        )

    def test_htonl_uses_unsigned_metadata_for_high_bit_results(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        declarations, bindings, _violations = _binding_contract(source)
        self.assertEqual(declarations["p_htonl"].strip(), "uint32_t")
        self.assertEqual(
            bindings["p_htonl"],
            {
                "macro": "BIND_T",
                "name": "htonl",
                "return_type": "TYPE_UINT",
            },
        )

    def test_narrow_unsigned_results_keep_integer_promotion_metadata(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        declarations, bindings, _violations = _binding_contract(source)
        for pointer in (
            "p_usb_device_class",
            "p_htons",
            "p_ntohs",
            "p_ac97_getmaster",
            "p_ac97_getpcm",
        ):
            with self.subTest(pointer=pointer):
                self.assertIn(
                    declarations[pointer].strip(), ("uint8_t", "uint16_t")
                )
                self.assertEqual(bindings[pointer]["return_type"], "TYPE_INT")

    def test_untyped_nonvoid_binding_names_the_bad_contract(self):
        fixture = """
static void cc_register_kernel_bindings(cc_state_t *cc) {
  int (*p_bad)(void) = returns_one;
  BIND("bad_result", p_bad, 0);
}
"""
        _declarations, _bindings, violations = _binding_contract(fixture)
        self.assertEqual(
            violations,
            ["bad_result: int declaration used untyped BIND"],
        )


if __name__ == "__main__":
    unittest.main()

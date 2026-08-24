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
    r"(?m)^[ \t]*(?P<macro>BIND(?:_T|_FIXED|_VARIADIC|_RETAINED)?)"
    r"\(\s*\"(?P<name>[^\"]+)\""
    r"\s*,\s*(?P<pointer>p_[A-Za-z0-9_]+)"
    r"\s*,\s*(?P<parameter_count>[0-9]+)"
    r"(?:\s*,\s*(?P<return_type>TYPE_[A-Z0-9_]+))?"
    r"(?P<parameter_types>(?:\s*,\s*TYPE_[A-Z0-9_]+)*)"
    r"(?:\s*,\s*[A-Za-z_][A-Za-z0-9_]*)?"
    r"[ \t\r\n]*\);"
)

GFXGUI_BINDINGS = {
    "gfx2d_effects_init": ("p_gfx2d_effects_init", 0, "TYPE_VOID"),
    "gfx2d_blur_box": ("p_gfx2d_blur_box", 5, "TYPE_VOID"),
    "gfx2d_blur_box_surface": (
        "p_gfx2d_blur_box_surface",
        2,
        "TYPE_VOID",
    ),
    "gfx2d_blur_gaussian": (
        "p_gfx2d_blur_gaussian",
        5,
        "TYPE_VOID",
    ),
    "gfx2d_blur_motion": ("p_gfx2d_blur_motion", 6, "TYPE_VOID"),
    "gfx2d_brightness": ("p_gfx2d_brightness", 5, "TYPE_VOID"),
    "gfx2d_contrast": ("p_gfx2d_contrast", 5, "TYPE_VOID"),
    "gfx2d_saturation": ("p_gfx2d_saturation", 5, "TYPE_VOID"),
    "gfx2d_hue_shift": ("p_gfx2d_hue_shift", 5, "TYPE_VOID"),
    "gfx2d_tint_ex": ("p_gfx2d_tint_ex", 7, "TYPE_VOID"),
    "gfx2d_edges": ("p_gfx2d_edges", 5, "TYPE_VOID"),
    "gfx2d_emboss": ("p_gfx2d_emboss", 5, "TYPE_VOID"),
    "gfx2d_posterize": ("p_gfx2d_posterize", 5, "TYPE_VOID"),
    "gfx2d_convolve_3x3": ("p_gfx2d_convolve_3x3", 6, "TYPE_VOID"),
    "gfx2d_convolve_5x5": ("p_gfx2d_convolve_5x5", 6, "TYPE_VOID"),
    "gfx2d_chromatic_aberration": (
        "p_gfx2d_chromatic_aberration",
        5,
        "TYPE_VOID",
    ),
    "gfx2d_scanlines_ex": ("p_gfx2d_scanlines_ex", 6, "TYPE_VOID"),
    "gfx2d_noise": ("p_gfx2d_noise", 6, "TYPE_VOID"),
    "gfx2d_assets_init": ("p_gfx2d_assets_init", 0, "TYPE_VOID"),
    "gfx2d_font_load": ("p_gfx2d_font_load", 1, "TYPE_INT"),
    "gfx2d_font_set_default": (
        "p_gfx2d_font_set_default",
        1,
        "TYPE_VOID",
    ),
    "gfx2d_text_ex": ("p_gfx2d_text_ex", 6, "TYPE_VOID"),
    "gfx2d_font_free": ("p_gfx2d_font_free", 1, "TYPE_VOID"),
    "gfx2d_transform_init": ("p_gfx2d_transform_init", 0, "TYPE_VOID"),
    "gfx2d_push_transform": ("p_gfx2d_push_transform", 0, "TYPE_VOID"),
    "gfx2d_pop_transform": ("p_gfx2d_pop_transform", 0, "TYPE_VOID"),
    "gfx2d_reset_transform": (
        "p_gfx2d_reset_transform",
        0,
        "TYPE_VOID",
    ),
    "gfx2d_translate": ("p_gfx2d_translate", 2, "TYPE_VOID"),
    "gfx2d_rotate": ("p_gfx2d_rotate", 1, "TYPE_VOID"),
    "gfx2d_scale": ("p_gfx2d_scale", 2, "TYPE_VOID"),
    "gfx2d_set_matrix": ("p_gfx2d_set_matrix", 1, "TYPE_VOID"),
    "gfx2d_get_matrix": ("p_gfx2d_get_matrix", 1, "TYPE_VOID"),
    "gfx2d_transform_point": (
        "p_gfx2d_transform_point",
        4,
        "TYPE_VOID",
    ),
    "gfx2d_text_transformed": (
        "p_gfx2d_text_transformed",
        5,
        "TYPE_VOID",
    ),
    "gui_widgets_init": ("p_gui_widgets_init", 0, "TYPE_VOID"),
    "gui_containers_init": ("p_gui_containers_init", 0, "TYPE_VOID"),
    "gui_menus_init": ("p_gui_menus_init", 0, "TYPE_VOID"),
    "gui_events_init": ("p_gui_events_init", 0, "TYPE_VOID"),
    "gui_themes_init": ("p_gui_themes_init", 0, "TYPE_VOID"),
    "ui_theme_set": ("p_ui_theme_set", 1, "TYPE_VOID"),
    "ui_theme_reset_default": (
        "p_ui_theme_reset_default",
        0,
        "TYPE_VOID",
    ),
    "ui_theme_load": ("p_ui_theme_load", 1, "TYPE_INT"),
    "ui_theme_save": ("p_ui_theme_save", 1, "TYPE_INT"),
    "ui_theme_windows95": ("p_ui_theme_windows95", 0, "TYPE_PTR"),
    "ui_theme_dark_mode": ("p_ui_theme_dark_mode", 0, "TYPE_PTR"),
    "ui_theme_pastel_dream": ("p_ui_theme_pastel_dream", 0, "TYPE_PTR"),
}

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
            "parameter_count": int(match.group("parameter_count")),
            "return_type": match.group("return_type"),
            "parameter_types": re.findall(
                r"TYPE_[A-Z0-9_]+", match.group("parameter_types")
            ),
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
            if binding["macro"] == "BIND_T":
                violations.append(
                    f"{binding['name']}: void declaration used BIND_T"
                )
            if binding["macro"] == "BIND" and binding["return_type"] is not None:
                violations.append(
                    f"{binding['name']}: void binding published a result"
                )
            if (
                binding["macro"] != "BIND"
                and binding["return_type"] != "TYPE_VOID"
            ):
                violations.append(
                    f"{binding['name']}: expected TYPE_VOID, got "
                    f"{binding['return_type']}"
                )
        else:
            if binding["macro"] == "BIND":
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
        self.assertEqual(len(bindings), 557)
        published_types = Counter(
            binding["return_type"]
            if binding["macro"] != "BIND"
            else "TYPE_VOID"
            for binding in bindings.values()
        )
        self.assertEqual(
            published_types,
            Counter(
                {
                    "TYPE_INT": 208,
                    "TYPE_VOID": 231,
                    "TYPE_UINT": 41,
                    "TYPE_DOUBLE": 25,
                    "TYPE_FLOAT": 25,
                    "TYPE_CHAR_PTR": 19,
                    "TYPE_PTR": 8,
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
                "parameter_count": 1,
                "return_type": "TYPE_UINT",
                "parameter_types": [],
            },
        )

    def test_gfxgui_frontier_has_the_exact_native_binding_contract(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        declarations, bindings, violations = _binding_contract(source)
        self.assertEqual(violations, [])
        self.assertEqual(len(GFXGUI_BINDINGS), 46)

        for name, (pointer, parameter_count, return_type) in (
            GFXGUI_BINDINGS.items()
        ):
            with self.subTest(name=name):
                self.assertIn(pointer, declarations)
                self.assertIn(pointer, bindings)
                binding = bindings[pointer]
                self.assertEqual(binding["name"], name)
                self.assertEqual(
                    binding["parameter_count"],
                    parameter_count,
                )
                self.assertEqual(
                    binding["return_type"]
                    if binding["macro"] != "BIND"
                    else "TYPE_VOID",
                    return_type,
                )

        for name in (
            "ui_theme_windows95",
            "ui_theme_dark_mode",
            "ui_theme_pastel_dream",
        ):
            pointer = GFXGUI_BINDINGS[name][0]
            with self.subTest(name=name):
                self.assertEqual(bindings[pointer]["macro"], "BIND_T")
                self.assertEqual(bindings[pointer]["return_type"], "TYPE_PTR")
                self.assertEqual(
                    re.sub(r"\s+", "", declarations[pointer]),
                    "constui_theme_t*",
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

    def test_libm_bindings_publish_fixed_floating_signatures(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        _declarations, bindings, violations = _binding_contract(source)
        self.assertEqual(violations, [])

        unary_double = {
            "sqrt", "sin", "cos", "tan", "atan", "fabs", "floor",
            "ceil", "round", "trunc", "exp", "exp2", "log", "log2",
            "asin", "acos", "sinh", "cosh", "tanh", "cbrt",
        }
        binary_double = {"atan2", "fmod", "pow", "hypot", "nextafter"}
        for name in sorted(unary_double | binary_double):
            with self.subTest(name=name):
                binding = next(
                    value for value in bindings.values()
                    if value["name"] == name
                )
                self.assertEqual(binding["macro"], "BIND_FIXED")
                self.assertEqual(binding["return_type"], "TYPE_DOUBLE")
                self.assertEqual(
                    binding["parameter_types"],
                    ["TYPE_DOUBLE"] * binding["parameter_count"],
                )

                float_binding = next(
                    value for value in bindings.values()
                    if value["name"] == name + "f"
                )
                self.assertEqual(float_binding["macro"], "BIND_FIXED")
                self.assertEqual(float_binding["return_type"], "TYPE_FLOAT")
                self.assertEqual(
                    float_binding["parameter_types"],
                    ["TYPE_FLOAT"] * float_binding["parameter_count"],
                )

    def test_unreviewed_bindings_use_the_named_legacy_registration_seam(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        body = _registration_body(source)
        self.assertIn("BIND_LEGACY_RESULT", body)
        self.assertIn("BIND_LEGACY_VOID", body)
        self.assertIn("cc_register_kernel_binding_legacy", body)

    def test_reviewed_scalar_bindings_publish_prototypes_and_variadic_state(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        _declarations, bindings, violations = _binding_contract(source)
        self.assertEqual(violations, [])
        by_name = {binding["name"]: binding for binding in bindings.values()}

        self.assertEqual(by_name["print"]["macro"], "BIND_FIXED")
        self.assertEqual(
            by_name["print"]["parameter_types"], ["TYPE_CHAR_PTR"]
        )
        self.assertEqual(by_name["strlen"]["macro"], "BIND_FIXED")
        self.assertEqual(
            by_name["memset"]["parameter_types"],
            ["TYPE_PTR", "TYPE_INT", "TYPE_UINT"],
        )
        self.assertEqual(
            by_name["serial_printf"],
            {
                "macro": "BIND_VARIADIC",
                "name": "serial_printf",
                "parameter_count": 1,
                "return_type": "TYPE_VOID",
                "parameter_types": ["TYPE_CHAR_PTR"],
            },
        )

    def test_icon_drawer_binding_publishes_its_nested_callback_handle(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        _declarations, bindings, violations = _binding_contract(source)
        self.assertEqual(violations, [])
        by_name = {binding["name"]: binding for binding in bindings.values()}

        self.assertEqual(by_name["set_icon_drawer"]["macro"], "BIND_RETAINED")
        self.assertEqual(by_name["set_icon_drawer"]["parameter_count"], 2)
        self.assertEqual(by_name["set_icon_drawer"]["return_type"], "TYPE_VOID")
        self.assertIn(
            "cc_retain_kernel_binding_callback_signature(\n"
            "            cc, &drawer_signature)",
            source,
        )
        self.assertIn(
            "setter_signature.param_types[1] = TYPE_FUNC_PTR;",
            source,
        )
        self.assertIn(
            "setter_signature.param_struct_indices[1] =",
            source,
        )

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

    def test_theme_accessor_rejects_integer_result_metadata(self):
        fixture = """
static void cc_register_kernel_bindings(cc_state_t *cc) {
  const ui_theme_t *(*p_bad)(void) = cc_ui_theme_windows95;
  BIND_T("ui_theme_windows95", p_bad, 0, TYPE_INT);
}
"""
        _declarations, _bindings, violations = _binding_contract(fixture)
        self.assertEqual(
            violations,
            ["ui_theme_windows95: expected TYPE_PTR, got TYPE_INT"],
        )


if __name__ == "__main__":
    unittest.main()

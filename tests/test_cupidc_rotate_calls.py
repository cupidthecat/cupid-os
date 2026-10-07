"""Check static rotate-call selection, ordinary call boundaries and execution."""
from tests.test_cupidc_frame_load import FrameToolCase, ROOT
import struct


def call_targets(data):
    offset = struct.unpack_from("<I", data, 32)[0]
    count = struct.unpack_from("<H", data, 48)[0]
    sections = [struct.unpack_from("<10I", data, offset + index * 40)
                for index in range(count)]
    functions, symbols = [], {}
    for section in sections:
        if section[1] != 2:
            continue
        names_section = sections[section[6]]
        names = data[names_section[4]:names_section[4] + names_section[5]]
        for index in range(section[5] // 16):
            name, value, size, info, _, target = struct.unpack_from(
                "<IIIBBH", data, section[4] + index * 16)
            label = names[name:names.index(0, name)].decode("ascii") if name else ""
            symbols[index] = label
            if label and info & 15 == 2 and target:
                functions.append((label, value, size, target))
    calls = {name: set() for name, _, _, _ in functions}
    for section in sections:
        if section[1] != 9:
            continue
        for index in range(section[5] // 8):
            location, info = struct.unpack_from("<II", data, section[4] + index * 8)
            if info & 255 != 2:
                continue
            for name, value, size, target in functions:
                if target == section[7] and value < location < value + size:
                    text = sections[target]
                    assert data[text[4] + location - 1] == 0xe8
                    calls[name].add(symbols[info >> 8])
    return calls


class CupidCRotateCallTests(FrameToolCase):
    fixture_source = '/toolchain/tests/cupidc_rotate_call_runtime.cc'
    compiler_flags = ('--gnu',)
    def setUp(self):
        super().setUp()
        self.calls = call_targets(self.object.read_bytes())
        self.run_tool("cupiddis", "--require-known", "--require-local-targets",
                      "--require-code-anchors", self.object)

    def test_static_helpers_fold_both_directions_and_argument_orders(self):
        for name, code in (("static_right", "5958d3c850"),
                           ("static_left", "5958d3c050"),
                           ("static_reversed", "5859d3c050"),
                           ("static_long", "5958d3c850")):
            with self.subTest(name=name):
                self.assertIn(bytes.fromhex(code), self.code[name])
                self.assertEqual(self.calls[name], set())

    def test_live_values_and_argument_evaluation_keep_their_stack_effects(self):
        self.assertIn(bytes.fromhex("5958d3c850"), self.code["surrounding_value"])
        self.assertEqual(self.calls["surrounding_value"], set())
        self.assertEqual(self.calls["evaluated_arguments"],
                         {"value_argument", "count_argument"})

    def test_constant_counts_select_immediate_rotations(self):
        self.assertIn(bytes.fromhex("58c1c80750"), self.code["static_right_7"])
        self.assertIn(bytes.fromhex("58c1c81f50"), self.code["static_right_31"])
        self.assertIn(bytes.fromhex("58c1c00150"), self.code["static_left_1"])
        for name in ("static_right_7", "static_left_1", "static_right_31"):
            self.assertEqual(self.calls[name], set())

    def test_undefined_count_boundaries_and_external_literals_keep_fallbacks(self):
        for name in ("static_zero", "static_32"):
            self.assertIn(bytes.fromhex("5958d3c850"), self.code[name])
        self.assertEqual(self.calls["external_constant"], {"external_right"})

    def test_external_noinline_qualified_and_indirect_calls_keep_cdecl(self):
        for name, target in (("external_call", "external_right"),
                             ("noinline_call", "retained_right"),
                             ("qualified_call", "qualified_right")):
            self.assertEqual(self.calls[name], {target})
        self.assertIn(bytes.fromhex("ffd0"), self.code["indirect_call"])

    def test_defined_counts_and_argument_side_effects_execute(self):
        self.execute_runtime()

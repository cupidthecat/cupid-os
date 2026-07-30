import os
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PARSER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_parse.cc"


def _extract_function(source, name):
    marker = f"{name}("
    name_start = source.index(marker)
    start = source.rfind("static ", 0, name_start)
    opening_brace = source.index("{", name_start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"could not find the end of {name}")


def _compiler_command():
    configured = os.environ.get("CC")
    if configured:
        command = shlex.split(configured, posix=os.name != "nt")
        if os.name == "nt":
            command = [
                token[1:-1]
                if len(token) >= 2
                and token[0] == token[-1]
                and token[0] in "\"'"
                else token
                for token in command
            ]
    else:
        candidates = (
            ("clang", "gcc", "cc")
            if os.name == "nt"
            else ("cc", "clang", "gcc")
        )
        command = next(
            (
                [resolved]
                for name in candidates
                if (resolved := shutil.which(name)) is not None
            ),
            [],
        )
    return command


def _is_nan(payload, width):
    if width == 4:
        return (
            payload & 0x7F800000 == 0x7F800000
            and payload & 0x007FFFFF != 0
        )
    return (
        payload & 0x7FF0000000000000 == 0x7FF0000000000000
        and payload & 0x000FFFFFFFFFFFFF != 0
    )


def _floating_value(payload, width):
    encoding = "<f" if width == 4 else "<d"
    return struct.unpack(encoding, payload.to_bytes(width, "little"))[0]


def _comparison_flags(left, right, width):
    if _is_nan(left, width) or _is_nan(right, width):
        return {"cf": 1, "pf": 1, "zf": 1}
    left_value = _floating_value(left, width)
    right_value = _floating_value(right, width)
    if left_value < right_value:
        return {"cf": 1, "pf": 0, "zf": 0}
    if left_value > right_value:
        return {"cf": 0, "pf": 0, "zf": 0}
    return {"cf": 0, "pf": 0, "zf": 1}


def _setcc(condition, flags):
    if condition == 0x92:
        return flags["cf"]
    if condition == 0x93:
        return 1 - flags["cf"]
    if condition == 0x94:
        return flags["zf"]
    if condition == 0x95:
        return 1 - flags["zf"]
    if condition == 0x96:
        return int(flags["cf"] or flags["zf"])
    if condition == 0x97:
        return int(not flags["cf"] and not flags["zf"])
    if condition == 0x9A:
        return flags["pf"]
    if condition == 0x9B:
        return 1 - flags["pf"]
    raise AssertionError(f"unsupported SETcc opcode 0x{condition:02x}")


def _emulate_comparison(code, left, right, width):
    cursor = 0
    if width == 8:
        if code[cursor] != 0x66:
            raise AssertionError("double comparison is missing its prefix")
        cursor += 1
    if code[cursor:cursor + 3] != b"\x0f\x2e\xc8":
        raise AssertionError("comparison did not use XMM1 as the left value")
    cursor += 3
    flags = _comparison_flags(left, right, width)

    values = {}
    combining_opcode = None
    while code[cursor:cursor + 3] != b"\x0f\xb6\xc0":
        if code[cursor] == 0x0F and code[cursor + 2] in (0xC0, 0xC2):
            register = "al" if code[cursor + 2] == 0xC0 else "dl"
            values[register] = _setcc(code[cursor + 1], flags)
            cursor += 3
            continue
        if code[cursor:cursor + 2] in (b"\x08\xd0", b"\x20\xd0"):
            combining_opcode = code[cursor]
            cursor += 2
            continue
        raise AssertionError(
            f"comparison left unproved bytes at offset {cursor}"
        )

    cursor += 3
    if cursor != len(code):
        raise AssertionError("comparison left trailing instruction bytes")
    if combining_opcode == 0x08:
        values["al"] |= values["dl"]
    elif combining_opcode == 0x20:
        values["al"] &= values["dl"]
    elif "dl" in values:
        raise AssertionError("comparison did not combine its parity result")
    return values["al"]


class PrivateCupidCFloatCompareEmitterTests(unittest.TestCase):
    def _compile_operand_classifier(self):
        compiler_command = _compiler_command()
        if not compiler_command:
            self.skipTest(
                "a host C compiler is required for the type oracle"
            )
        compiler = shutil.which(compiler_command[0])
        if compiler is None and not Path(compiler_command[0]).is_file():
            self.fail(
                f"configured C compiler was not found: "
                f"{compiler_command[0]}"
            )
        compiler_command[0] = compiler or compiler_command[0]

        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        classifier = _extract_function(
            parser_source, "cc_is_arithmetic_scalar_type"
        )
        harness = f"""
#include <stdio.h>

typedef enum {{
  TYPE_INT,
  TYPE_CHAR,
  TYPE_VOID,
  TYPE_PTR,
  TYPE_INT_PTR,
  TYPE_CHAR_PTR,
  TYPE_STRUCT,
  TYPE_STRUCT_PTR,
  TYPE_FUNC_PTR,
  TYPE_FLOAT,
  TYPE_DOUBLE,
  TYPE_FLOAT4,
  TYPE_DOUBLE2
}} cc_type_t;

{classifier}

int main(void) {{
  int type;
  for (type = TYPE_INT; type <= TYPE_DOUBLE2; type++)
    printf("%d\\n", cc_is_arithmetic_scalar_type((cc_type_t)type));
  return 0;
}}
"""

        with tempfile.TemporaryDirectory(
            prefix="cupidc-float-operand-type-"
        ) as temporary:
            root = Path(temporary)
            source = root / "oracle.cc"
            executable = root / (
                "oracle.exe" if os.name == "nt" else "oracle"
            )
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    *compiler_command,
                    "-x",
                    "c",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )
        return tuple(
            int(line) for line in run_result.stdout.strip().splitlines()
        )

    def _compile_active_emitter(self):
        compiler_command = _compiler_command()
        if not compiler_command:
            self.skipTest(
                "a host C compiler is required for the emitter oracle"
            )
        compiler = shutil.which(compiler_command[0])
        if compiler is None and not Path(compiler_command[0]).is_file():
            self.fail(
                f"configured C compiler was not found: "
                f"{compiler_command[0]}"
            )
        compiler_command[0] = compiler or compiler_command[0]

        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in (
                "emit8",
                "emit_movzx_eax_al",
                "emit_compare_xmm1_xmm0",
            )
        )
        harness = f"""
#include <stdint.h>
#include <stdio.h>

#define CC_MAX_CODE 64u

typedef enum {{
  CC_TOK_EQEQ,
  CC_TOK_NE,
  CC_TOK_LT,
  CC_TOK_GT,
  CC_TOK_LE,
  CC_TOK_GE
}} cc_token_type_t;

typedef struct {{
  uint8_t *code;
  uint32_t code_pos;
  int error;
}} cc_state_t;

{functions}

static void print_code(const char *width, const char *name,
                       cc_token_type_t operation) {{
  uint8_t code[CC_MAX_CODE] = {{0}};
  cc_state_t state = {{code, 0u, 0}};
  uint32_t index;
  emit_compare_xmm1_xmm0(
      &state, width[0] == 'd', operation);
  if (state.error)
    return;
  printf("%s-%s ", width, name);
  for (index = 0; index < state.code_pos; index++)
    printf("%02x", state.code[index]);
  putchar('\\n');
}}

int main(void) {{
  const char *names[] = {{"eq", "ne", "lt", "gt", "le", "ge"}};
  cc_token_type_t operations[] = {{
      CC_TOK_EQEQ, CC_TOK_NE, CC_TOK_LT,
      CC_TOK_GT, CC_TOK_LE, CC_TOK_GE}};
  uint32_t index;
  for (index = 0; index < 6u; index++) {{
    print_code("float", names[index], operations[index]);
    print_code("double", names[index], operations[index]);
  }}
  return 0;
}}
"""

        with tempfile.TemporaryDirectory(
            prefix="cupidc-float-compare-emitter-"
        ) as temporary:
            root = Path(temporary)
            source = root / "oracle.cc"
            executable = root / (
                "oracle.exe" if os.name == "nt" else "oracle"
            )
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    *compiler_command,
                    "-x",
                    "c",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )

        lines = run_result.stdout.strip().splitlines()
        self.assertEqual(len(lines), 12, run_result.stdout)
        return {
            name: bytes.fromhex(encoded)
            for name, encoded in (line.split(" ", 1) for line in lines)
        }

    def test_active_emitter_encodes_ordered_and_unordered_results(self):
        code = self._compile_active_emitter()
        suffixes = {
            "eq": "0f94c0 0f9bc2 20d0 0fb6c0",
            "ne": "0f95c0 0f9ac2 08d0 0fb6c0",
            "lt": "0f92c0 0f9bc2 20d0 0fb6c0",
            "gt": "0f97c0 0fb6c0",
            "le": "0f96c0 0f9bc2 20d0 0fb6c0",
            "ge": "0f93c0 0fb6c0",
        }
        for name, suffix in suffixes.items():
            with self.subTest(operation=name, width="float"):
                self.assertEqual(
                    code[f"float-{name}"],
                    bytes.fromhex("0f2ec8 " + suffix),
                )
            with self.subTest(operation=name, width="double"):
                self.assertEqual(
                    code[f"double-{name}"],
                    bytes.fromhex("660f2ec8 " + suffix),
                )

    def test_active_emitter_matches_c_comparison_semantics(self):
        code = self._compile_active_emitter()
        cases = (
            ("ordered-less", 0x3F800000, 0x40000000),
            ("ordered-equal", 0xC0200000, 0xC0200000),
            ("ordered-greater", 0x7F800000, 0x3F800000),
            ("signed-zero", 0x80000000, 0x00000000),
            ("quiet-nan-left", 0x7FC12345, 0x3F800000),
            ("signaling-nan-right", 0x3F800000, 0x7FA12345),
        )
        expected = {
            "ordered-less": (0, 1, 1, 0, 1, 0),
            "ordered-equal": (1, 0, 0, 0, 1, 1),
            "ordered-greater": (0, 1, 0, 1, 0, 1),
            "signed-zero": (1, 0, 0, 0, 1, 1),
            "quiet-nan-left": (0, 1, 0, 0, 0, 0),
            "signaling-nan-right": (0, 1, 0, 0, 0, 0),
        }
        operations = ("eq", "ne", "lt", "gt", "le", "ge")
        for case_name, left, right in cases:
            for operation, result in zip(operations, expected[case_name]):
                with self.subTest(case=case_name, operation=operation):
                    self.assertEqual(
                        _emulate_comparison(
                            code[f"float-{operation}"],
                            left,
                            right,
                            4,
                        ),
                        result,
                    )

        double_cases = (
            ("subnormal-less", 0x0000000000000001, 0x3FF0000000000000),
            ("negative-infinity", 0xFFF0000000000000, 0xBFF0000000000000),
            ("quiet-nan", 0x7FF8123456789ABC, 0x3FF0000000000000),
            ("signaling-nan", 0x7FF0123456789ABC, 0x3FF0000000000000),
        )
        double_expected = {
            "subnormal-less": (0, 1, 1, 0, 1, 0),
            "negative-infinity": (0, 1, 1, 0, 1, 0),
            "quiet-nan": (0, 1, 0, 0, 0, 0),
            "signaling-nan": (0, 1, 0, 0, 0, 0),
        }
        for case_name, left, right in double_cases:
            for operation, result in zip(
                operations, double_expected[case_name]
            ):
                with self.subTest(case=case_name, operation=operation):
                    self.assertEqual(
                        _emulate_comparison(
                            code[f"double-{operation}"],
                            left,
                            right,
                            8,
                        ),
                        result,
                    )

    def test_floating_operators_reject_non_arithmetic_operands(self):
        supported = self._compile_operand_classifier()
        self.assertEqual(
            supported,
            (
                1,  # int
                1,  # char
                0,  # void
                0,  # void pointer
                0,  # int pointer
                0,  # char pointer
                0,  # structure
                0,  # structure pointer
                0,  # function pointer
                1,  # float
                1,  # double
                0,  # float4
                0,  # double2
            ),
        )


if __name__ == "__main__":
    unittest.main()

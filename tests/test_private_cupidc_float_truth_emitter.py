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
BINDING_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc.cc"


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


def _truth_flags(payload, width):
    if _is_nan(payload, width):
        return {"pf": 1, "zf": 1}
    encoding = "<f" if width == 4 else "<d"
    value = struct.unpack(encoding, payload.to_bytes(width, "little"))[0]
    return {"pf": 0, "zf": int(value == 0.0)}


def _emulate_truth(code, payload, width):
    cursor = 0
    if code[cursor:cursor + 3] != b"\x0f\x57\xc9":
        raise AssertionError("truth lowering did not zero XMM1")
    cursor += 3
    if width == 8:
        if code[cursor] != 0x66:
            raise AssertionError("double truth test is missing its prefix")
        cursor += 1
    if code[cursor:cursor + 3] != b"\x0f\x2e\xc1":
        raise AssertionError("truth lowering did not compare XMM0 with zero")
    cursor += 3

    flags = _truth_flags(payload, width)
    if code[cursor:cursor + 3] != b"\x0f\x95\xc0":
        raise AssertionError("truth lowering did not materialize nonzero")
    al = 1 - flags["zf"]
    cursor += 3
    if code[cursor:cursor + 3] != b"\x0f\x9a\xc2":
        raise AssertionError("truth lowering did not materialize unordered")
    dl = flags["pf"]
    cursor += 3
    if code[cursor:cursor + 2] != b"\x08\xd0":
        raise AssertionError("truth lowering did not include unordered")
    al |= dl
    cursor += 2
    if code[cursor:cursor + 3] != b"\x0f\xb6\xc0":
        raise AssertionError("truth lowering did not normalize EAX")
    cursor += 3
    if cursor != len(code):
        raise AssertionError("truth lowering left trailing instruction bytes")
    return al


class PrivateCupidCFloatTruthEmitterTests(unittest.TestCase):
    def _compiler(self):
        command = _compiler_command()
        if not command:
            self.skipTest("a host C compiler is required for the oracle")
        compiler = shutil.which(command[0])
        if compiler is None and not Path(command[0]).is_file():
            self.fail(f"configured C compiler was not found: {command[0]}")
        command[0] = compiler or command[0]
        return command

    def _compile_active_emitter(self):
        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in (
                "emit8",
                "emit_movzx_eax_al",
                "emit_scalar_truth_xmm0",
            )
        )
        harness = f"""
#include <stdint.h>
#include <stdio.h>

#define CC_MAX_CODE 64u

typedef struct {{
  uint8_t code[CC_MAX_CODE];
  uint32_t code_pos;
  int error;
}} cc_state_t;

{functions}

int main(void) {{
  cc_state_t cc = {{0}};
  emit_scalar_truth_xmm0(&cc, 0);
  printf("%u:", cc.code_pos);
  for (uint32_t i = 0; i < cc.code_pos; i++)
    printf("%02x", cc.code[i]);
  putchar('\\n');

  cc.code_pos = 0;
  emit_scalar_truth_xmm0(&cc, 1);
  printf("%u:", cc.code_pos);
  for (uint32_t i = 0; i < cc.code_pos; i++)
    printf("%02x", cc.code[i]);
  putchar('\\n');
  return 0;
}}
"""
        with tempfile.TemporaryDirectory(
            prefix="cupidc-float-truth-emitter-"
        ) as temporary:
            root = Path(temporary)
            source = root / "oracle.cc"
            executable = root / (
                "oracle.exe" if os.name == "nt" else "oracle"
            )
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    *self._compiler(),
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

        encodings = []
        for line in run_result.stdout.strip().splitlines():
            count_text, bytes_text = line.split(":", 1)
            code = bytes.fromhex(bytes_text)
            self.assertEqual(int(count_text), len(code))
            encodings.append(code)
        return tuple(encodings)

    def _compile_truth_type_classifier(self):
        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        classifier = _extract_function(
            parser_source, "cc_is_scalar_truth_type"
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
  TYPE_DOUBLE2,
  TYPE_FLOAT_PTR,
  TYPE_DOUBLE_PTR
}} cc_type_t;

{classifier}

int main(void) {{
  int type;
  for (type = TYPE_INT; type <= TYPE_DOUBLE_PTR; type++)
    printf("%d\\n", cc_is_scalar_truth_type((cc_type_t)type));
  return 0;
}}
"""
        with tempfile.TemporaryDirectory(
            prefix="cupidc-float-truth-type-"
        ) as temporary:
            root = Path(temporary)
            source = root / "oracle.cc"
            executable = root / (
                "oracle.exe" if os.name == "nt" else "oracle"
            )
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    *self._compiler(),
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

    def _compile_truth_materializer_contract(self):
        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in (
                "cc_is_scalar_truth_type",
                "cc_materialize_scalar_truth",
            )
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
  TYPE_DOUBLE2,
  TYPE_FLOAT_PTR,
  TYPE_DOUBLE_PTR
}} cc_type_t;

typedef struct {{
  int error;
  int floating_emissions;
  int last_is_double;
  const char *message;
}} cc_state_t;

static void cc_error(cc_state_t *cc, const char *message) {{
  cc->error = 1;
  cc->message = message;
}}

static void emit_scalar_truth_xmm0(cc_state_t *cc, int is_double) {{
  cc->floating_emissions++;
  cc->last_is_double = is_double;
}}

{functions}

int main(void) {{
  cc_state_t vector = {{0}};
  int vector_result =
      cc_materialize_scalar_truth(&vector, TYPE_FLOAT4);
  printf("%d:%d:%s\\n", vector_result, vector.error, vector.message);

  cc_state_t scalar = {{0}};
  int scalar_result =
      cc_materialize_scalar_truth(&scalar, TYPE_DOUBLE);
  printf("%d:%d:%d:%d\\n", scalar_result, scalar.error,
         scalar.floating_emissions, scalar.last_is_double);
  return 0;
}}
"""
        with tempfile.TemporaryDirectory(
            prefix="cupidc-float-truth-materializer-"
        ) as temporary:
            root = Path(temporary)
            source = root / "oracle.cc"
            executable = root / (
                "oracle.exe" if os.name == "nt" else "oracle"
            )
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    *self._compiler(),
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
        return tuple(run_result.stdout.strip().splitlines())

    def test_emits_exact_scalar_truth_sequences(self):
        float_code, double_code = self._compile_active_emitter()
        self.assertEqual(
            float_code,
            bytes.fromhex("0f57c90f2ec10f95c00f9ac208d00fb6c0"),
        )
        self.assertEqual(
            double_code,
            bytes.fromhex("0f57c9660f2ec10f95c00f9ac208d00fb6c0"),
        )

    def test_truth_semantics_cover_zero_nonzero_infinity_and_nan(self):
        float_code, double_code = self._compile_active_emitter()
        cases = {
            4: (
                (0x00000000, 0),
                (0x80000000, 0),
                (0x00000001, 1),
                (0x80000001, 1),
                (0x3F800000, 1),
                (0xBF800000, 1),
                (0x7F800000, 1),
                (0xFF800000, 1),
                (0x7FC00001, 1),
                (0x7F800001, 1),
            ),
            8: (
                (0x0000000000000000, 0),
                (0x8000000000000000, 0),
                (0x0000000000000001, 1),
                (0x8000000000000001, 1),
                (0x3FF0000000000000, 1),
                (0xBFF0000000000000, 1),
                (0x7FF0000000000000, 1),
                (0xFFF0000000000000, 1),
                (0x7FF8000000000001, 1),
                (0x7FF0000000000001, 1),
            ),
        }
        for width, code in ((4, float_code), (8, double_code)):
            for payload, expected in cases[width]:
                with self.subTest(width=width, payload=hex(payload)):
                    self.assertEqual(
                        _emulate_truth(code, payload, width), expected
                    )

    def test_scalar_truth_type_boundary_rejects_aggregates_and_vectors(self):
        self.assertEqual(
            self._compile_truth_type_classifier(),
            (
                1,  # int
                1,  # char
                0,  # void
                1,  # void pointer
                1,  # int pointer
                1,  # char pointer
                0,  # struct by value
                1,  # struct pointer
                1,  # function pointer
                1,  # float
                1,  # double
                0,  # float4
                0,  # double2
                1,  # float pointer
                1,  # double pointer
            ),
        )

    def test_rejected_truth_type_has_a_useful_diagnostic_and_recovers(self):
        self.assertEqual(
            self._compile_truth_materializer_contract(),
            (
                "0:1:truth test requires a scalar operand",
                "1:0:1:1",
            ),
        )

    def test_active_control_bindings_publish_integer_results(self):
        source = BINDING_SOURCE.read_text(encoding="utf-8")
        bindings = (
            ("is_gui_mode", "p_is_gui", 0),
            ("gui_win_is_open", "p_gui_win_is_open", 1),
            ("gui_win_can_draw", "p_gui_win_can_draw", 1),
            ("gfx2d_should_quit", "p_gfx2d_should_quit", 0),
            ("confirm_dialog", "p_confirm_dlg", 1),
            ("input_dialog", "p_input_dlg", 3),
            ("popup_menu", "p_popup_menu", 4),
        )
        for name, pointer, parameter_count in bindings:
            with self.subTest(binding=name):
                self.assertIn(
                    (
                        f'BIND_T("{name}", {pointer}, '
                        f"{parameter_count}, TYPE_INT);"
                    ),
                    source,
                )
                self.assertNotIn(
                    f'BIND("{name}", {pointer}, {parameter_count});',
                    source,
                )


if __name__ == "__main__":
    unittest.main()

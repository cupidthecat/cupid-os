import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.test_private_cupidc_float_truth_emitter import (
    _compiler_command,
    _extract_function,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
PARSER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_parse.cc"


class PrivateCupidCFloatUpdateEmitterTests(unittest.TestCase):
    def _compiler(self):
        command = _compiler_command()
        if not command:
            self.skipTest("a host C compiler is required for the oracle")
        compiler = shutil.which(command[0])
        if compiler is None and not Path(command[0]).is_file():
            self.fail(f"configured C compiler was not found: {command[0]}")
        command[0] = compiler or command[0]
        return command

    def _compile_and_run(self, harness, prefix):
        with tempfile.TemporaryDirectory(prefix=prefix) as temporary:
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
            return run_result.stdout

    def _compile_active_update_emitter(self):
        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in (
                "emit8",
                "emit32",
                "emit_mov_eax_imm",
                "emit_cvtsi2ss",
                "emit_cvtsi2sd",
                "emit_sse_scalar_op",
                "emit_update_xmm0_scalar",
                "emit_update_xmm0_vector",
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

typedef enum {{
  TYPE_FLOAT4,
  TYPE_DOUBLE2
}} cc_type_t;

{functions}

static void print_case(int is_double, int decrement) {{
  cc_state_t cc = {{0}};
  emit_update_xmm0_scalar(&cc, is_double, decrement);
  printf("%u:", cc.code_pos);
  for (uint32_t i = 0; i < cc.code_pos; i++)
    printf("%02x", cc.code[i]);
  putchar('\\n');
}}

static void print_vector_case(cc_type_t type, int decrement) {{
  cc_state_t cc = {{0}};
  emit_update_xmm0_vector(&cc, type, decrement);
  printf("%u:", cc.code_pos);
  for (uint32_t i = 0; i < cc.code_pos; i++)
    printf("%02x", cc.code[i]);
  putchar('\\n');
}}

int main(void) {{
  print_case(0, 0);
  print_case(0, 1);
  print_case(1, 0);
  print_case(1, 1);
  print_vector_case(TYPE_FLOAT4, 0);
  print_vector_case(TYPE_FLOAT4, 1);
  print_vector_case(TYPE_DOUBLE2, 0);
  print_vector_case(TYPE_DOUBLE2, 1);
  return 0;
}}
"""
        output = self._compile_and_run(
            harness, "cupidc-float-update-emitter-"
        )
        encodings = []
        for line in output.strip().splitlines():
            count_text, bytes_text = line.split(":", 1)
            code = bytes.fromhex(bytes_text)
            self.assertEqual(int(count_text), len(code))
            encodings.append(code)
        return tuple(encodings)

    def _compile_update_validation_contract(self):
        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in (
                "cc_is_simd_value_type",
                "cc_is_direct_update_type",
                "cc_error_simd_update_target",
                "cc_validate_variable_update",
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
  TYPE_DOUBLE_PTR,
  TYPE_UINT,
  TYPE_UINT_PTR
}} cc_type_t;

typedef enum {{
  SYM_LOCAL,
  SYM_PARAM,
  SYM_GLOBAL,
  SYM_FUNC,
  SYM_KERNEL
}} cc_symbol_kind_t;

typedef struct {{
  cc_symbol_kind_t kind;
  cc_type_t type;
  int is_array;
  int is_const_qualified;
}} cc_symbol_t;

typedef struct {{
  int error;
  const char *message;
}} cc_state_t;

static void cc_error(cc_state_t *cc, const char *message) {{
  cc->error = 1;
  cc->message = message;
}}

{functions}

int main(void) {{
  cc_state_t vector_state = {{0}};
  cc_symbol_t vector = {{SYM_LOCAL, TYPE_FLOAT4, 0, 0}};
  int vector_result =
      cc_validate_variable_update(&vector_state, &vector);
  printf("%d:%d\\n", vector_result, vector_state.error);

  cc_state_t aggregate_state = {{0}};
  cc_symbol_t aggregate = {{SYM_LOCAL, TYPE_STRUCT, 0, 0}};
  int aggregate_result =
      cc_validate_variable_update(&aggregate_state, &aggregate);
  printf("%d:%d:%s\\n", aggregate_result, aggregate_state.error,
         aggregate_state.message);

  cc_state_t scalar_state = {{0}};
  cc_symbol_t scalar = {{SYM_GLOBAL, TYPE_DOUBLE, 0, 0}};
  int scalar_result =
      cc_validate_variable_update(&scalar_state, &scalar);
  printf("%d:%d\\n", scalar_result, scalar_state.error);

  cc_state_t const_vector_state = {{0}};
  cc_symbol_t const_vector = {{SYM_GLOBAL, TYPE_DOUBLE2, 0, 1}};
  int const_vector_result =
      cc_validate_variable_update(&const_vector_state, &const_vector);
  printf("%d:%d:%s\\n", const_vector_result, const_vector_state.error,
         const_vector_state.message);
  return 0;
}}
"""
        return tuple(
            self._compile_and_run(
                harness, "cupidc-float-update-validation-"
            ).strip().splitlines()
        )

    def test_emits_exact_float_and_double_update_sequences(self):
        (
            float_increment,
            float_decrement,
            double_increment,
            double_decrement,
            float4_increment,
            float4_decrement,
            double2_increment,
            double2_decrement,
        ) = self._compile_active_update_emitter()
        self.assertEqual(
            float_increment,
            bytes.fromhex("b801000000f30f2ac8f30f58c1"),
        )
        self.assertEqual(
            float_decrement,
            bytes.fromhex("b801000000f30f2ac8f30f5cc1"),
        )
        self.assertEqual(
            double_increment,
            bytes.fromhex("b801000000f20f2ac8f20f58c1"),
        )
        self.assertEqual(
            double_decrement,
            bytes.fromhex("b801000000f20f2ac8f20f5cc1"),
        )
        self.assertEqual(
            float4_increment,
            bytes.fromhex("b801000000f30f2ac80fc6c9000f58c1"),
        )
        self.assertEqual(
            float4_decrement,
            bytes.fromhex("b801000000f30f2ac80fc6c9000f5cc1"),
        )
        self.assertEqual(
            double2_increment,
            bytes.fromhex("b801000000f20f2ac8660fc6c900660f58c1"),
        )
        self.assertEqual(
            double2_decrement,
            bytes.fromhex("b801000000f20f2ac8660fc6c900660f5cc1"),
        )

    def test_accepts_whole_vectors_and_still_rejects_aggregates(self):
        self.assertEqual(
            self._compile_update_validation_contract(),
            (
                "1:0",
                "0:1:increment or decrement requires a scalar variable",
                "1:0",
                "0:1:SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
        )

    def test_every_private_variable_update_path_uses_the_typed_helper(self):
        source = PARSER_SOURCE.read_text(encoding="utf-8")
        self.assertEqual(source.count("cc_emit_variable_update("), 9)
        for old_opcode in (
            "emit8(cc, 0x40); /* inc eax */",
            "emit8(cc, 0x48); /* dec eax */",
        ):
            self.assertNotIn(old_opcode, source)


if __name__ == "__main__":
    unittest.main()

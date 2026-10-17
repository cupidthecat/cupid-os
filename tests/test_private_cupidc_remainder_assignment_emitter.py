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


class PrivateCupidCRemainderAssignmentEmitterTests(unittest.TestCase):
    def _compiler(self):
        command = _compiler_command()
        if not command:
            self.skipTest("a host C compiler is required for the oracle")
        compiler = shutil.which(command[0])
        if compiler is None and not Path(command[0]).is_file():
            self.fail(f"configured C compiler was not found: {command[0]}")
        command[0] = compiler or command[0]
        return command

    def _active_encodings(self):
        parser_source = PARSER_SOURCE.read_text(encoding="utf-8")
        functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in (
                "emit8",
                "cc_emit_compound_from_rhs_old",
            )
        )
        harness = f"""
#include <stdint.h>
#include <stdio.h>

#define CC_MAX_CODE 64u

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
  CC_TOK_PLUSEQ,
  CC_TOK_MINUSEQ,
  CC_TOK_STAREQ,
  CC_TOK_SLASHEQ,
  CC_TOK_PERCENTEQ,
  CC_TOK_ANDEQ,
  CC_TOK_OREQ,
  CC_TOK_XOREQ,
  CC_TOK_SHLEQ,
  CC_TOK_SHREQ
}} cc_token_type_t;

typedef struct {{
  uint8_t code[CC_MAX_CODE];
  uint32_t code_pos;
  int error;
}} cc_state_t;

{functions}

static void print_case(cc_type_t type) {{
  cc_state_t cc = {{0}};
  cc_emit_compound_from_rhs_old(&cc, CC_TOK_PERCENTEQ, type);
  printf("%u:", cc.code_pos);
  for (uint32_t index = 0; index < cc.code_pos; index++)
    printf("%02x", cc.code[index]);
  putchar('\\n');
}}

int main(void) {{
  print_case(TYPE_INT);
  print_case(TYPE_UINT);
  return 0;
}}
"""

        with tempfile.TemporaryDirectory(
            prefix="cupidc-remainder-assignment-emitter-"
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

    def test_emits_exact_signed_and_unsigned_remainder_sequences(self):
        signed, unsigned = self._active_encodings()
        self.assertEqual(signed, bytes.fromhex("89c189d899f7f989d0"))
        self.assertEqual(unsigned, bytes.fromhex("89c189d831d2f7f189d0"))


if __name__ == "__main__":
    unittest.main()

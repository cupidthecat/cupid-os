import os
import shutil
import struct
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


def _expect_bytes(code, cursor, expected, message):
    end = cursor + len(expected)
    if code[cursor:end] != expected:
        raise AssertionError(f"{message} at offset {cursor}")
    return end


def _signed_rel32(code, cursor):
    return int.from_bytes(code[cursor:cursor + 4], "little", signed=True)


def _decode_conversion(code, width):
    prefix = b"\xf3" if width == 4 else b"\xf2"
    compare_prefix = b"" if width == 4 else b"\x66"
    cursor = 0

    cursor = _expect_bytes(
        code,
        cursor,
        b"\xb8\x00\x00\x00\x40",
        "conversion did not materialize 2^30",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        prefix + b"\x0f\x2a\xc8",
        "conversion did not create its scalar threshold",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        prefix + b"\x0f\x58\xc9",
        "conversion did not double its threshold to 2^31",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        compare_prefix + b"\x0f\x2e\xc1",
        "conversion did not compare the source with 2^31",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        b"\x0f\x82",
        "conversion did not branch below 2^31",
    )
    low_target = cursor + 4 + _signed_rel32(code, cursor)
    cursor += 4

    cursor = _expect_bytes(
        code,
        cursor,
        prefix + b"\x0f\x5c\xc1",
        "upper-half conversion did not subtract 2^31",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        prefix + b"\x0f\x2c\xc0",
        "upper-half conversion did not truncate through signed EAX",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        b"\x35\x00\x00\x00\x80",
        "upper-half conversion did not restore bit 31",
    )
    cursor = _expect_bytes(
        code,
        cursor,
        b"\xe9",
        "upper-half conversion did not skip the lower path",
    )
    done_target = cursor + 4 + _signed_rel32(code, cursor)
    cursor += 4

    if cursor != low_target:
        raise AssertionError("lower-half branch does not reach its conversion")
    cursor = _expect_bytes(
        code,
        cursor,
        prefix + b"\x0f\x2c\xc0",
        "lower-half conversion did not truncate directly",
    )
    if cursor != done_target:
        raise AssertionError("upper-half branch does not reach the common end")
    if cursor != len(code):
        raise AssertionError("conversion left trailing instruction bytes")


def _floating_value(payload, width):
    encoding = "<f" if width == 4 else "<d"
    return struct.unpack(encoding, payload.to_bytes(width, "little"))[0]


def _emulate_conversion(code, payload, width):
    _decode_conversion(code, width)
    value = _floating_value(payload, width)
    if value < 2147483648.0:
        return int(value) & 0xFFFFFFFF
    return (int(value - 2147483648.0) ^ 0x80000000) & 0xFFFFFFFF


def _double_payload(value):
    return struct.unpack("<Q", struct.pack("<d", value))[0]


class PrivateCupidCFloatToUnsignedEmitterTests(unittest.TestCase):
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
                "emit32",
                "patch32",
                "emit_mov_eax_imm",
                "emit_jmp_placeholder",
                "emit_jcc_placeholder",
                "patch_jump",
                "emit_sse_scalar_op",
                "emit_cvtsi2ss",
                "emit_cvtsi2sd",
                "emit_cvttss2si",
                "emit_cvttsd2si",
                "emit_cvtfp_to_ui32",
            )
        )
        harness = f"""
#include <stdint.h>
#include <stdio.h>

#define CC_MAX_CODE 128u

typedef struct {{
  uint8_t *code;
  uint32_t code_pos;
  int error;
}} cc_state_t;

{functions}

static void print_code(int is_double) {{
  uint8_t code[CC_MAX_CODE] = {{0}};
  cc_state_t state = {{code, 0u, 0}};
  uint32_t index;
  emit_cvtfp_to_ui32(&state, is_double);
  if (state.error)
    return;
  printf("%s ", is_double ? "double" : "float");
  for (index = 0; index < state.code_pos; index++)
    printf("%02x", state.code[index]);
  putchar('\\n');
}}

int main(void) {{
  print_code(0);
  print_code(1);
  return 0;
}}
"""
        with tempfile.TemporaryDirectory(
            prefix="cupidc-float-to-unsigned-emitter-"
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

        return {
            name: bytes.fromhex(encoded)
            for name, encoded in (
                line.split(" ", 1)
                for line in run_result.stdout.strip().splitlines()
            )
        }

    def test_active_emitter_has_complete_lower_and_upper_paths(self):
        code = self._compile_active_emitter()
        self.assertEqual(
            code["float"],
            bytes.fromhex(
                "b800000040 f30f2ac8 f30f58c9 0f2ec1 0f8212000000 "
                "f30f5cc1 f30f2cc0 3500000080 e904000000 f30f2cc0"
            ),
        )
        self.assertEqual(
            code["double"],
            bytes.fromhex(
                "b800000040 f20f2ac8 f20f58c9 660f2ec1 0f8212000000 "
                "f20f5cc1 f20f2cc0 3500000080 e904000000 f20f2cc0"
            ),
        )
        _decode_conversion(code["float"], 4)
        _decode_conversion(code["double"], 8)

    def test_active_emitter_covers_the_full_defined_interval(self):
        code = self._compile_active_emitter()
        cases = {
            4: (
                (0xBF7FFFFF, 0x00000000),
                (0x80000000, 0x00000000),
                (0x3F7FFFFF, 0x00000000),
                (0x3FC00000, 0x00000001),
                (0x4EFFFFFF, 0x7FFFFF80),
                (0x4F000000, 0x80000000),
                (0x4F000001, 0x80000100),
                (0x4F7FFFFF, 0xFFFFFF00),
            ),
            8: (
                (_double_payload(-0.9999999999999999), 0x00000000),
                (_double_payload(-0.0), 0x00000000),
                (_double_payload(0.9999999999999999), 0x00000000),
                (_double_payload(1.9999999999999998), 0x00000001),
                (_double_payload(2147483647.9999998), 0x7FFFFFFF),
                (_double_payload(2147483648.0), 0x80000000),
                (_double_payload(2147483648.75), 0x80000000),
                (_double_payload(4294967295.9999995), 0xFFFFFFFF),
            ),
        }
        for width, name in ((4, "float"), (8, "double")):
            for payload, expected in cases[width]:
                with self.subTest(width=width, payload=hex(payload)):
                    self.assertEqual(
                        _emulate_conversion(code[name], payload, width),
                        expected,
                    )


if __name__ == "__main__":
    unittest.main()

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
PARSER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_parse.cc"


def _extract_function(source, name):
    signature = f"static void {name}("
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"could not find the end of {name}")


def _emulate_scalar_negation(code, payload, width):
    """Run the emitted instruction subset against one XMM0 payload."""
    cursor = 0
    stack = bytearray(b"\xa5" * 24)
    original_stack = bytes(stack)
    stack_pointer = 16
    xmm0 = payload.to_bytes(width, "little")

    if code[cursor:cursor + 3] != b"\x83\xec\x08":
        raise AssertionError("emitter did not reserve eight stack bytes")
    stack_pointer -= 8
    cursor += 3

    prefix = 0xF2 if width == 8 else 0xF3
    store = bytes((prefix, 0x0F, 0x11, 0x04, 0x24))
    if code[cursor:cursor + len(store)] != store:
        raise AssertionError("emitter did not store XMM0 at ESP")
    stack[stack_pointer:stack_pointer + width] = xmm0
    cursor += len(store)

    if code[cursor] != 0x81:
        raise AssertionError("emitter did not encode the sign-word XOR")
    cursor += 1
    if width == 8:
        addressing = b"\x74\x24\x04"
        sign_word_offset = 4
    else:
        addressing = b"\x34\x24"
        sign_word_offset = 0
    if code[cursor:cursor + len(addressing)] != addressing:
        raise AssertionError("emitter selected the wrong IEEE sign word")
    cursor += len(addressing)

    immediate = int.from_bytes(code[cursor:cursor + 4], "little")
    if immediate != 0x80000000:
        raise AssertionError("emitter did not isolate the IEEE sign bit")
    cursor += 4
    word_start = stack_pointer + sign_word_offset
    sign_word = int.from_bytes(stack[word_start:word_start + 4], "little")
    stack[word_start:word_start + 4] = (
        sign_word ^ immediate
    ).to_bytes(4, "little")

    load = bytes((prefix, 0x0F, 0x10, 0x04, 0x24))
    if code[cursor:cursor + len(load)] != load:
        raise AssertionError("emitter did not reload XMM0 from ESP")
    xmm0 = bytes(stack[stack_pointer:stack_pointer + width])
    cursor += len(load)

    if code[cursor:cursor + 3] != b"\x83\xc4\x08":
        raise AssertionError("emitter did not release its stack storage")
    stack_pointer += 8
    cursor += 3
    if cursor != len(code):
        raise AssertionError("emitter left unproved instruction bytes")
    if stack_pointer != 16:
        raise AssertionError("emitter changed the caller's stack pointer")
    if stack[:8] != original_stack[:8] or stack[16:] != original_stack[16:]:
        raise AssertionError("emitter wrote outside its stack reservation")
    return int.from_bytes(xmm0, "little")


class PrivateCupidCUnaryEmitterTests(unittest.TestCase):
    def _compile_active_emitter(self):
        configured = os.environ.get("CC")
        if configured:
            compiler_command = shlex.split(
                configured,
                posix=os.name != "nt",
            )
            if os.name == "nt":
                compiler_command = [
                    token[1:-1]
                    if len(token) >= 2
                    and token[0] == token[-1]
                    and token[0] in "\"'"
                    else token
                    for token in compiler_command
                ]
        else:
            candidates = (
                ("clang", "gcc", "cc")
                if os.name == "nt"
                else ("cc", "clang", "gcc")
            )
            compiler_command = next(
                (
                    [resolved]
                    for name in candidates
                    if (resolved := shutil.which(name)) is not None
                ),
                [],
            )
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
        function_names = (
            "emit8",
            "emit32",
            "emit_movss_esp_xmm",
            "emit_movsd_esp_xmm",
            "emit_movss_xmm_esp",
            "emit_movsd_xmm_esp",
            "emit_negate_xmm0_scalar",
        )
        active_functions = "\n\n".join(
            _extract_function(parser_source, name)
            for name in function_names
        )
        harness = f"""
#include <stdint.h>
#include <stdio.h>

#define CC_MAX_CODE 64u

typedef struct {{
  uint8_t *code;
  uint32_t code_pos;
  int error;
}} cc_state_t;

{active_functions}

static void print_code(const char *name, const cc_state_t *cc) {{
  uint32_t index;
  printf("%s ", name);
  for (index = 0; index < cc->code_pos; index++)
    printf("%02x", cc->code[index]);
  putchar('\\n');
}}

int main(void) {{
  uint8_t single_code[CC_MAX_CODE] = {{0}};
  uint8_t extended_code[CC_MAX_CODE] = {{0}};
  cc_state_t single = {{single_code, 0u, 0}};
  cc_state_t extended = {{extended_code, 0u, 0}};
  emit_negate_xmm0_scalar(&single, 0);
  emit_negate_xmm0_scalar(&extended, 1);
  if (single.error || extended.error)
    return 2;
  print_code("float", &single);
  print_code("double", &extended);
  return 0;
}}
"""

        with tempfile.TemporaryDirectory(
            prefix="cupidc-unary-emitter-"
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
        self.assertEqual(len(lines), 2, run_result.stdout)
        return {
            name: bytes.fromhex(encoded)
            for name, encoded in (line.split(" ", 1) for line in lines)
        }

    def test_active_emitter_encodes_the_checked_scalar_negation_sequences(
        self,
    ):
        code = self._compile_active_emitter()

        self.assertEqual(
            code["float"],
            bytes.fromhex(
                "83 ec 08 f3 0f 11 04 24 "
                "81 34 24 00 00 00 80 "
                "f3 0f 10 04 24 83 c4 08"
            ),
        )
        self.assertEqual(
            code["double"],
            bytes.fromhex(
                "83 ec 08 f2 0f 11 04 24 "
                "81 74 24 04 00 00 00 80 "
                "f2 0f 10 04 24 83 c4 08"
            ),
        )

    def test_active_emitter_flips_only_the_ieee_sign_bit(self):
        code = self._compile_active_emitter()
        cases = (
            ("float", 4, 0x3FC00000, 0xBFC00000),
            ("float", 4, 0x00000000, 0x80000000),
            ("float", 4, 0x80000000, 0x00000000),
            ("float", 4, 0x7F800000, 0xFF800000),
            ("float", 4, 0x7FC12345, 0xFFC12345),
            ("float", 4, 0x7FA12345, 0xFFA12345),
            ("float", 4, 0x00000001, 0x80000001),
            ("double", 8, 0x4002000000000000, 0xC002000000000000),
            ("double", 8, 0x0000000000000000, 0x8000000000000000),
            ("double", 8, 0x8000000000000000, 0x0000000000000000),
            ("double", 8, 0x7FF0000000000000, 0xFFF0000000000000),
            ("double", 8, 0x7FF8123456789ABC, 0xFFF8123456789ABC),
            ("double", 8, 0x7FF0123456789ABC, 0xFFF0123456789ABC),
            ("double", 8, 0x0000000000000001, 0x8000000000000001),
        )
        for kind, width, payload, expected in cases:
            with self.subTest(kind=kind, payload=f"0x{payload:x}"):
                self.assertEqual(
                    _emulate_scalar_negation(code[kind], payload, width),
                    expected,
                )


if __name__ == "__main__":
    unittest.main()

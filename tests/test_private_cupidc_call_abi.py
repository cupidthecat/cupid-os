import os
import shlex
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

from tests.test_private_cupidc_float_truth_emitter import _compiler_command


REPO_ROOT = Path(__file__).resolve().parents[1]
LEXER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_lex.cc"
PARSER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_parse.cc"
KERNEL_LANG = REPO_ROOT / "kernel" / "lang"


class PrivateCupidCCallAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        command = _compiler_command()
        if not command:
            raise unittest.SkipTest(
                "a host C compiler is required for the private CupidC oracle"
            )
        compiler = shutil.which(command[0])
        if compiler is None and not Path(command[0]).is_file():
            raise AssertionError(
                f"configured C compiler was not found: {command[0]}"
            )
        command[0] = compiler or command[0]
        cls.compiler_command = command

        if os.name == "nt":
            if shutil.which("wsl") is None:
                raise unittest.SkipTest(
                    "WSL is required to execute the i386 runtime oracle"
                )
        elif shutil.which("as") is None or shutil.which("ld") is None:
            raise unittest.SkipTest(
                "GNU as and ld are required to execute the i386 runtime oracle"
            )

        cls.driver_directory = tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-driver-", ignore_cleanup_errors=True
        )
        cls.driver_root = Path(cls.driver_directory.name)
        cls.driver = cls._build_compiler_driver()

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "driver_directory"):
            cls.driver_directory.cleanup()

    @classmethod
    def _build_compiler_driver(cls):
        (cls.driver_root / "types.h").write_text(
            textwrap.dedent(
                """
                #ifndef CUPID_TEST_TYPES_H
                #define CUPID_TEST_TYPES_H
                #include <stddef.h>
                #include <stdint.h>
                #endif
                """
            ),
            encoding="utf-8",
        )
        (cls.driver_root / "serial.h").write_text(
            textwrap.dedent(
                """
                #ifndef CUPID_TEST_SERIAL_H
                #define CUPID_TEST_SERIAL_H
                void serial_printf(const char *format, ...);
                #endif
                """
            ),
            encoding="utf-8",
        )
        (cls.driver_root / "kernel.h").write_text(
            textwrap.dedent(
                """
                #ifndef CUPID_TEST_KERNEL_H
                #define CUPID_TEST_KERNEL_H
                #include <stdint.h>
                void *kmalloc(uint32_t bytes);
                #endif
                """
            ),
            encoding="utf-8",
        )
        (cls.driver_root / "string.h").write_text(
            textwrap.dedent(
                """
                #ifndef CUPID_TEST_STRING_H
                #define CUPID_TEST_STRING_H
                #include <stddef.h>
                void *memcpy(void *destination, const void *source,
                             size_t bytes);
                void *memset(void *destination, int value, size_t bytes);
                int strcmp(const char *left, const char *right);
                #endif
                """
            ),
            encoding="utf-8",
        )
        harness = cls.driver_root / "driver.c"
        harness.write_text(
            textwrap.dedent(
                """
                #include <stdio.h>
                #include <stdlib.h>
                #include <stdarg.h>

                #include "cupidc.h"

                void serial_printf(const char *format, ...) {
                  va_list arguments;
                  va_start(arguments, format);
                  (void)vfprintf(stderr, format, arguments);
                  va_end(arguments);
                }

                void *kmalloc(uint32_t bytes) {
                  return malloc((size_t)bytes);
                }

                static void bind_kernel(cc_state_t *cc, const char *name,
                                        cc_type_t type, uint32_t address) {
                  cc_symbol_t *symbol =
                      cc_sym_add(cc, name, SYM_KERNEL, type);
                  if (symbol != NULL)
                    symbol->address = address;
                }

                static void bind_feature13_kernels(cc_state_t *cc) {
                  bind_kernel(cc, "repl_eval", TYPE_INT, 0x01001000u);
                  bind_kernel(cc, "serial_printf", TYPE_VOID, 0x01001010u);
                  bind_kernel(cc, "println", TYPE_VOID, 0x01001020u);
                  bind_kernel(cc, "fabs", TYPE_DOUBLE, 0x01001030u);
                  bind_kernel(cc, "sin", TYPE_DOUBLE, 0x01001040u);
                  bind_kernel(cc, "cos", TYPE_DOUBLE, 0x01001050u);
                  bind_kernel(cc, "sqrt", TYPE_DOUBLE, 0x01001060u);
                  bind_kernel(cc, "log", TYPE_DOUBLE, 0x01001070u);
                  bind_kernel(cc, "pow", TYPE_DOUBLE, 0x01001080u);
                  bind_kernel(cc, "tanh", TYPE_DOUBLE, 0x01001090u);
                  bind_kernel(cc, "cbrt", TYPE_DOUBLE, 0x010010a0u);
                  bind_kernel(cc, "atan2", TYPE_DOUBLE, 0x010010b0u);
                  bind_kernel(cc, "hypot", TYPE_DOUBLE, 0x010010c0u);
                  bind_kernel(cc, "exp", TYPE_DOUBLE, 0x010010d0u);
                }

                static char *read_source(const char *path) {
                  FILE *input = fopen(path, "rb");
                  long length;
                  char *source;
                  if (input == NULL || fseek(input, 0, SEEK_END) != 0)
                    return NULL;
                  length = ftell(input);
                  if (length < 0 || fseek(input, 0, SEEK_SET) != 0) {
                    fclose(input);
                    return NULL;
                  }
                  source = (char *)malloc((size_t)length + 1u);
                  if (source == NULL ||
                      fread(source, 1u, (size_t)length, input) !=
                          (size_t)length) {
                    free(source);
                    fclose(input);
                    return NULL;
                  }
                  source[length] = '\\0';
                  fclose(input);
                  return source;
                }

                static int write_output(const char *path, const uint8_t *data,
                                        uint32_t size) {
                  FILE *output = fopen(path, "wb");
                  if (output == NULL)
                    return 0;
                  if (size != 0u && fwrite(data, 1u, size, output) != size) {
                    fclose(output);
                    return 0;
                  }
                  return fclose(output) == 0;
                }

                int main(int argc, char **argv) {
                  cc_state_t *cc;
                  char *source;
                  if (argc != 4)
                    return 64;
                  source = read_source(argv[1]);
                  cc = (cc_state_t *)calloc(1u, sizeof(*cc));
                  if (source == NULL || cc == NULL)
                    return 65;
                  cc->code = (uint8_t *)calloc(1u, CC_MAX_CODE);
                  cc->data = (uint8_t *)calloc(1u, CC_MAX_DATA);
                  if (cc->code == NULL || cc->data == NULL)
                    return 66;
                  cc->code_base = CC_JIT_CODE_BASE;
                  cc->data_base = CC_JIT_DATA_BASE;
                  cc->jit_mode = 1;
                  cc_sym_init(cc);
                  bind_feature13_kernels(cc);
                  cc_lex_init(cc, source);
                  cc_parse_program(cc);
                  if (cc->error) {
                    (void)fprintf(stderr, "%s", cc->error_msg);
                    return 2;
                  }
                  if (!write_output(argv[2], cc->code, cc->code_pos) ||
                      !write_output(argv[3], cc->data, cc->data_pos))
                    return 67;
                  (void)printf("%u\\n", cc->entry_offset);
                  return 0;
                }
                """
            ),
            encoding="utf-8",
        )
        executable = cls.driver_root / (
            "driver.exe" if os.name == "nt" else "driver"
        )
        result = subprocess.run(
            [
                *cls.compiler_command,
                "-x",
                "c",
                "-std=c11",
                "-O0",
                "-I",
                str(cls.driver_root),
                "-I",
                str(KERNEL_LANG),
                str(LEXER_SOURCE),
                str(PARSER_SOURCE),
                str(harness),
                "-o",
                str(executable),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=60,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)
        return executable

    def _compile(self, root, source):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(textwrap.dedent(source), encoding="utf-8")
        result = subprocess.run(
            [str(self.driver), str(source_path), str(code_path), str(data_path)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _run_i386(self, root, entry_offset):
        (root / "runner.S").write_text(
            textwrap.dedent(
                f"""
                .section .text,"ax",@progbits
                .global _start
                _start:
                  call cupid_code + {entry_offset}
                  movl %eax, %ebx
                  movl $1, %eax
                  int $0x80

                .section .cupidcode,"ax",@progbits
                .global cupid_code
                cupid_code:
                  .incbin "code.bin"

                .section .cupiddata,"aw",@progbits
                .global cupid_data
                cupid_data:
                  .incbin "data.bin"
                """
            ),
            encoding="utf-8",
        )
        (root / "runner.ld").write_text(
            textwrap.dedent(
                """
                ENTRY(_start)
                SECTIONS {
                  . = 0x01000000;
                  .text : { *(.text) }
                  . = 0x01100000;
                  .cupidcode : { *(.cupidcode) }
                  . = 0x01200000;
                  .cupiddata : { *(.cupiddata) }
                  /DISCARD/ : { *(.note*) *(.comment*) }
                }
                """
            ),
            encoding="utf-8",
        )
        command = (
            "as --32 runner.S -o runner.o && "
            "ld -m elf_i386 -T runner.ld runner.o -o runner && "
            "./runner"
        )
        if os.name == "nt":
            drive, tail = os.path.splitdrive(str(root.resolve()))
            self.assertTrue(drive and len(drive) == 2, str(root))
            wsl_root = "/mnt/" + drive[0].lower() + tail.replace("\\", "/")
            invocation = [
                "wsl",
                "sh",
                "-lc",
                f"cd {shlex.quote(wsl_root)} && {command}",
            ]
        else:
            invocation = ["sh", "-lc", command]
        return subprocess.run(
            invocation,
            cwd=root,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )

    def _compile_and_run(self, source):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-runtime-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            compile_result, _code, _data = self._compile(root, source)
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            entry_offset = int(compile_result.stdout.strip())
            return self._run_i386(root, entry_offset)

    def test_two_double_parameters_keep_their_full_cdecl_width(self):
        result = self._compile_and_run(
            """
            double combine(double first, double second) {
              return first * 10.0 + second;
            }

            int main() {
              return (int)combine(3.0, 4.0);
            }
            """
        )
        self.assertEqual(result.returncode, 34, result.stdout + result.stderr)

    def test_double_then_integer_arguments_use_cdecl_source_order(self):
        result = self._compile_and_run(
            """
            double combine(double first, int second) {
              return first * 10.0 + (double)second;
            }

            int main() {
              return (int)combine(3.0, 4);
            }
            """
        )
        self.assertEqual(result.returncode, 34, result.stdout + result.stderr)

    def test_four_word_call_keeps_the_popup_menu_argument_order(self):
        result = self._compile_and_run(
            """
            int observe_popup(int x, int y, void *items, int count) {
              if (x != 220) return 1;
              if (y != 110) return 2;
              if (items == 0) return 3;
              if (count != 3) return 4;
              return 0;
            }

            int main() {
              int items[3];
              return observe_popup(220, 110, (void *)items, 3);
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_method_calls_layout_self_and_mixed_width_arguments(self):
        result = self._compile_and_run(
            """
            int recorded;

            class Probe {
              int Encode(double first, int second, double third) {
                return (int)first + second * 2 + (int)third * 4;
              }

              void Record(double first, int second, double third) {
                recorded =
                    (int)first + second * 2 + (int)third * 4;
              }
            };

            int main() {
              Probe probe;
              int expression_result = probe.Encode(1.0, 2, 3.0);
              probe.Record(2.0, 3, 4.0);
              return expression_result + recorded;
            }
            """
        )
        self.assertEqual(result.returncode, 41, result.stdout + result.stderr)

    def test_method_expressions_and_statements_convert_integer_zero_to_double(self):
        result = self._compile_and_run(
            """
            int recorded_kind;
            double recorded_number;
            int recorded_offset;
            int recorded_length;

            class Probe {
              int Inspect(int kind, double number, int offset, int length) {
                if (kind != 17) return 1;
                if (number != 0.0) return 2;
                if (offset != -31) return 3;
                if (length != 37) return 4;
                return 0;
              }

              void Record(int kind, double number,
                          int offset, int length) {
                recorded_kind = kind;
                recorded_number = number;
                recorded_offset = offset;
                recorded_length = length;
              }
            };

            int main() {
              Probe probe;
              int expression_status = probe.Inspect(17, 0, -31, 37);
              if (expression_status != 0) return expression_status;
              probe.Record(23, 0, -41, 47);
              if (recorded_kind != 23) return 5;
              if (recorded_number != 0.0) return 6;
              if (recorded_offset != -41) return 7;
              if (recorded_length != 47) return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_indirect_and_pointer_method_calls_share_the_mixed_width_layout(self):
        result = self._compile_and_run(
            """
            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int recorded;

            class Probe {
              int Encode(double first, int second, double third) {
                return (int)first + second * 2 + (int)third * 4;
              }

              void Record(double first, int second, double third) {
                recorded =
                    (int)first + second * 2 + (int)third * 4;
              }
            };

            int main() {
              int (*callback)(double first, int second) = combine;
              Probe probe;
              Probe *pointer = &probe;
              int indirect_result = callback(3.0, 4);
              int expression_result = pointer->Encode(1.0, 2, 3.0);
              pointer->Record(2.0, 3, 4.0);
              return indirect_result + expression_result + recorded;
            }
            """
        )
        self.assertEqual(result.returncode, 75, result.stdout + result.stderr)

    def test_alternating_widths_keep_source_evaluation_and_parameter_order(self):
        result = self._compile_and_run(
            """
            int sequence;

            int next_int() {
              sequence = sequence + 1;
              return sequence;
            }

            double next_double() {
              sequence = sequence + 1;
              return (double)sequence;
            }

            int capture(int first, double second, int third, double fourth) {
              return first + (int)second * 2 +
                  third * 4 + (int)fourth * 8;
            }

            int main() {
              sequence = 0;
              return capture(
                  next_int(), next_double(), next_int(), next_double()) +
                  sequence;
            }
            """
        )
        self.assertEqual(result.returncode, 53, result.stdout + result.stderr)

    def test_alternating_width_call_cleans_every_argument_byte(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-cleanup-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                int capture(int first, double second,
                            int third, double fourth) {
                  return first + (int)second + third + (int)fourth;
                }

                int main() {
                  return capture(1, 2.0, 3, 4.0);
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            main_code = code_path.read_bytes()[entry_offset:]

        cleanup_sites = [
            offset
            for offset in range(len(main_code) - 7)
            if main_code[offset] == 0xE8
            and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x18"
        ]
        self.assertEqual(
            len(cleanup_sites),
            1,
            "expected one call followed by ADD ESP, 24",
        )

    def test_variadic_float_tail_widens_and_cleans_its_double_slot(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-variadic-float-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                void consume(int marker, ...) {
                }

                int main() {
                  float fraction = 2.5f;
                  consume(7, fraction, 13);
                  return 0;
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            main_code = code_path.read_bytes()[entry_offset:]

        self.assertIn(
            b"\xf3\x0f\x5a\xc0",
            main_code,
            "expected CVTSS2SD for the variadic float argument",
        )
        cleanup_sites = [
            offset
            for offset in range(len(main_code) - 7)
            if main_code[offset] == 0xE8
            and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x10"
        ]
        self.assertEqual(
            len(cleanup_sites),
            1,
            "expected one variadic call followed by ADD ESP, 16",
        )

    def test_browser_token_call_converts_integer_zero_to_double(self):
        result = self._compile_and_run(
            """
            int seen_kind;
            double seen_number;
            int seen_offset;
            int seen_length;
            int seen_line;

            void emit_number_token(int kind, double number,
                                   int string_offset, int string_length,
                                   int line) {
              seen_kind = kind;
              seen_number = number;
              seen_offset = string_offset;
              seen_length = string_length;
              seen_line = line;
            }

            int main() {
              emit_number_token(17, 0, -31, 37, 43);
              if (seen_kind != 17) return 1;
              if (seen_number != 0.0) return 2;
              if (seen_offset != -31) return 3;
              if (seen_length != 37) return 4;
              if (seen_line != 43) return 5;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_prototype_before_use_preserves_mixed_width_call_metadata(self):
        result = self._compile_and_run(
            """
            int seen_kind;
            double seen_number;
            int seen_offset;
            int seen_length;
            int seen_line;

            void capture_token(int kind, double number,
                               int string_offset, int string_length,
                               int line);

            int main() {
              capture_token(23, 0, -41, 47, 53);
              if (seen_kind != 23) return 1;
              if (seen_number != 0.0) return 2;
              if (seen_offset != -41) return 3;
              if (seen_length != 47) return 4;
              if (seen_line != 53) return 5;
              return 0;
            }

            void capture_token(int kind, double number,
                               int string_offset, int string_length,
                               int line) {
              seen_kind = kind;
              seen_number = number;
              seen_offset = string_offset;
              seen_length = string_length;
              seen_line = line;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_character_values_cast_to_both_floating_widths(self):
        result = self._compile_and_run(
            """
            int main() {
              char *digits = "79";
              char seven = digits[0];
              float as_float = (float)seven;
              double as_double = (double)seven;
              float arithmetic_float =
                  (float)(digits[1] - digits[0]);
              double arithmetic_double =
                  (double)(digits[1] - digits[0]);
              if (as_float != 55.0f) return 1;
              if (as_double != 55.0) return 2;
              if (arithmetic_float != 2.0f) return 3;
              if (arithmetic_double != 2.0) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_character_assignment_reaches_scalar_and_array_fp_targets(self):
        result = self._compile_and_run(
            """
            float scalar_float;
            double scalar_double;
            float float_values[1];
            double double_values[1];

            int main() {
              char code = 'A';
              scalar_float = code;
              scalar_double = code;
              float_values[0] = code;
              double_values[0] = code;
              if (scalar_float != 65.0f) return 1;
              if (scalar_double != 65.0) return 2;
              if (float_values[0] != 65.0f) return 3;
              if (double_values[0] != 65.0) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_feature13_source_compiles_through_the_private_call_path(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-feature13-compile-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                (REPO_ROOT / "bin" / "feature13_double.cc").read_text(
                    encoding="utf-8"
                ),
            )
        self.assertEqual(
            result.returncode,
            0,
            result.stdout + result.stderr,
        )

    def test_global_fixed_float_and_double_arrays_keep_width_and_stride(self):
        result = self._compile_and_run(
            """
            float singles[3];
            double doubles[3];

            int main() {
              singles[0] = 1;
              singles[1] = 2.25;
              doubles[0] = singles[1];
              singles[2] = doubles[0];
              doubles[1] = 100.5;
              doubles[2] = doubles[0] + doubles[1];
              if (singles[0] != 1.0) return 1;
              if (singles[1] != 2.25) return 2;
              if (singles[2] != 2.25) return 3;
              if (doubles[0] != 2.25) return 4;
              if (doubles[1] != 100.5) return 5;
              if (doubles[2] != 102.75) return 6;
              if (sizeof(*singles) != 4) return 7;
              if (sizeof(*doubles) != 8) return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_floating_literals_keep_adjacent_double_storage_independent(self):
        result = self._compile_and_run(
            """
            float leading_float = 1.25f;
            double first_double = 12.5;
            double second_double = -3.25;
            float trailing_float = -4.5f;
            int sentinel = 73;

            int main() {
              if (leading_float != 1.25f) return 1;
              if (first_double != 12.5) return 2;
              if (second_double != -3.25) return 3;
              if (trailing_float != -4.5f) return 4;
              if (sentinel != 73) return 5;
              first_double = 9.5;
              if (second_double != -3.25) return 6;
              second_double = 7.75;
              if (first_double != 9.5) return 7;
              if (leading_float != 1.25f) return 8;
              if (trailing_float != -4.5f) return 9;
              if (sentinel != 73) return 10;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_local_and_static_fixed_floating_arrays_execute(self):
        result = self._compile_and_run(
            """
            int main() {
              float local_singles[2];
              double local_doubles[2];
              static float saved_singles[2];
              static double saved_doubles[2];
              local_singles[0] = 1.5;
              local_singles[1] = 2.5;
              local_doubles[0] = local_singles[0] + local_singles[1];
              local_doubles[1] = 8;
              saved_singles[1] = local_doubles[0];
              saved_doubles[1] = local_doubles[1] + saved_singles[1];
              if (local_doubles[0] != 4.0) return 1;
              if (saved_singles[1] != 4.0) return 2;
              if (saved_doubles[1] != 12.0) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_array_arithmetic_compound_assignments_execute(self):
        result = self._compile_and_run(
            """
            float singles[1];
            double doubles[1];

            int main() {
              singles[0] = 5.0;
              singles[0] += 3.0;
              singles[0] *= 2.0;
              singles[0] -= 4.0;
              singles[0] /= 3.0;
              doubles[0] = 20.0;
              doubles[0] += 2.0;
              doubles[0] *= 3.0;
              doubles[0] -= 6.0;
              doubles[0] /= 4.0;
              if (singles[0] != 4.0) return 1;
              if (doubles[0] != 15.0) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_function_returned_int_pointer_drops_stale_array_metadata(self):
        result = self._compile_and_run(
            """
            int numbers[3];
            double floating_values[1];

            int *numbers_view() {
              return numbers;
            }

            int main() {
              numbers[0] = 11;
              numbers[1] = 77;
              numbers[2] = 22;
              floating_values[0] = 3.5;
              floating_values;
              if (numbers_view()[1] != 77) return 1;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_string_subscript_drops_stale_floating_array_metadata(self):
        result = self._compile_and_run(
            """
            double floating_values[1];

            int main() {
              floating_values[0] = 2.5;
              floating_values;
              if ("AZ"[1] != 'Z') return 1;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_program_array_bounds_reject_nonpositive_sizes(self):
        cases = (
            ("global zero", "double values[0];"),
            ("global negative", "float values[1 - 2];"),
            (
                "local zero inner bound",
                "int main() { double values[2][0]; return 0; }",
            ),
            (
                "local negative",
                "int main() { float values[1 - 2]; return 0; }",
            ),
            (
                "block static zero",
                "int main() { static double values[0]; return 0; }",
            ),
            (
                "block static negative inner bound",
                "int main() { static float values[2][1 - 2]; return 0; }",
            ),
        )

        for label, source in cases:
            with self.subTest(storage=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-array-bound-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("array size must be positive", result.stderr)

    def test_array_byte_calculation_rejects_signed_overflow(self):
        cases = (
            ("global double", "double values[268435456];"),
            (
                "local float",
                "int main() { float values[536870912]; return 0; }",
            ),
            (
                "block static char alignment",
                "int main() { static char values[2147483647]; return 0; }",
            ),
            ("global two dimensional int", "int values[32768][32768];"),
        )

        for label, source in cases:
            with self.subTest(storage=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-array-overflow-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "array allocation size overflow", result.stderr
                )

    def test_multidimensional_floating_array_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-float-array-dimension-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                double table[2][2];
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "floating arrays support one dimension", result.stderr
        )

    def test_floating_array_bitwise_compound_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-float-array-compound-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                float values[1];

                int main() {
                  values[0] &= 1;
                  return 0;
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "bitwise/shift compound assignment not valid on FP arrays",
            result.stderr,
        )

    def test_fixed_simd_array_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-array-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                float4 vectors[2];
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("fixed SIMD arrays are not supported", result.stderr)

    def test_floating_struct_field_arrays_have_a_useful_diagnostic(self):
        cases = (
            ("struct float field", "struct Samples { float values[2]; };"),
            ("class double field", "class Samples { double values[2]; };"),
        )

        for label, source in cases:
            with self.subTest(declaration=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-floating-field-array-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "floating struct field arrays are not supported",
                    result.stderr,
                )

    def test_unsupported_call_argument_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void nothing() {
                }

                void consume(int value) {
                }

                void probe() {
                  consume(nothing());
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "cdecl call argument type is not supported", result.stderr
        )

    def test_unsupported_parameter_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-parameter-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void consume(float4 value) {
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cdecl parameter type is not supported", result.stderr)


if __name__ == "__main__":
    unittest.main()

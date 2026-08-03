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

    def test_parser_error_helper_stays_private(self):
        header = (KERNEL_LANG / "cupidc.h").read_text(encoding="utf-8")
        parser = PARSER_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("void cc_error(cc_state_t *cc, const char *msg);", header)
        self.assertIn(
            "static void cc_error(cc_state_t *cc, const char *msg)",
            parser,
        )

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
                #include <string.h>

                #include "cupidc.h"
                #include "string.h"

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

                static cc_state_t *new_compiler_state(void) {
                  cc_state_t *cc = (cc_state_t *)calloc(1u, sizeof(*cc));
                  if (cc == NULL)
                    return NULL;
                  cc->code = (uint8_t *)calloc(1u, CC_MAX_CODE);
                  cc->data = (uint8_t *)calloc(1u, CC_MAX_DATA);
                  if (cc->code == NULL || cc->data == NULL)
                    return NULL;
                  cc->code_base = CC_JIT_CODE_BASE;
                  cc->data_base = CC_JIT_DATA_BASE;
                  cc->jit_mode = 1;
                  cc_sym_init(cc);
                  bind_feature13_kernels(cc);
                  return cc;
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

                static int check_numeric_token_boundary(const char *source) {
                  cc_state_t *cc =
                      (cc_state_t *)calloc(1u, sizeof(*cc));
                  cc_token_t token;
                  if (cc == NULL)
                    return 68;
                  cc_lex_init(cc, source);

                  token = cc_lex_next(cc);
                  if (token.type != CC_TOK_ERROR) {
                    (void)fprintf(stderr,
                                  "overlong literal was not rejected\\n");
                    return 69;
                  }
                  (void)printf("%s\\n", token.text);

                  token = cc_lex_next(cc);
                  if (token.type != CC_TOK_SEMICOLON) {
                    (void)fprintf(stderr,
                                  "semicolon after literal was lost\\n");
                    return 70;
                  }

                  token = cc_lex_next(cc);
                  if (token.type != CC_TOK_FLIT ||
                      strcmp(token.text, "0.75") != 0) {
                    (void)fprintf(stderr,
                                  "lexer did not recover at the next literal\\n");
                    return 71;
                  }
                  (void)printf("%s\\n", token.text);

                  token = cc_lex_next(cc);
                  if (token.type != CC_TOK_EOF)
                    return 72;
                  return 0;
                }

                int main(int argc, char **argv) {
                  cc_state_t *cc;
                  char *source;
                  int repl_mode =
                      argc == 5 && strcmp(argv[4], "--repl") == 0;
                  int repl_rollback_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--repl-rollback") == 0;
                  int recovery_mode =
                      argc == 5 && strcmp(argv[4], "--recover") == 0;
                  if (argc == 3 &&
                      strcmp(argv[1], "--check-number-boundary") == 0)
                    return check_numeric_token_boundary(argv[2]);
                  if (argc != 4 && !repl_mode && !repl_rollback_mode &&
                      !recovery_mode)
                    return 64;
                  source = read_source(argv[1]);
                  cc = new_compiler_state();
                  if (source == NULL || cc == NULL)
                    return 65;
                  if (recovery_mode) {
                    char *retry_source = source;
                    while (*retry_source != 0 &&
                           (unsigned char)*retry_source != 30u)
                      retry_source++;
                    if ((unsigned char)*retry_source != 30u)
                      return 68;
                    *retry_source = 0;
                    retry_source++;
                    cc_lex_init(cc, source);
                    cc_parse_program(cc);
                    if (!cc->error)
                      return 69;
                    cc = new_compiler_state();
                    if (cc == NULL)
                      return 66;
                    cc_lex_init(cc, retry_source);
                    cc_parse_program(cc);
                  } else if (repl_mode || repl_rollback_mode) {
                    char *unit = source;
                    int unit_index = 0;
                    repl_state_t checkpoint;
                    memset(&checkpoint, 0, sizeof(checkpoint));
                    checkpoint.cc = cc;
                    while (unit != NULL) {
                      char *next = unit;
                      int is_expr = 0;
                      while (*next != 0 && (unsigned char)*next != 30u)
                        next++;
                      if ((unsigned char)*next == 30u) {
                        *next = 0;
                        next++;
                      } else {
                        next = NULL;
                      }
                      cc_lex_init(cc, unit);
                      while (!cc->error &&
                             cc_lex_peek(cc).type != CC_TOK_EOF)
                        cc_parse_repl_line(cc, &is_expr);
                      if (repl_rollback_mode && unit_index == 0) {
                        if (cc->error)
                          break;
                        cc_repl_checkpoint_structs(&checkpoint);
                      } else if (repl_rollback_mode && unit_index == 1) {
                        if (!cc->error)
                          return 73;
                        cc_repl_restore_structs(&checkpoint);
                        cc->error = 0;
                        cc->error_msg[0] = 0;
                      } else if (cc->error) {
                        break;
                      }
                      unit_index++;
                      unit = next;
                    }
                  } else {
                    cc_lex_init(cc, source);
                    cc_parse_program(cc);
                  }
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

    def _compile(self, root, source, *, repl=False):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(textwrap.dedent(source), encoding="utf-8")
        command = [
            str(self.driver),
            str(source_path),
            str(code_path),
            str(data_path),
        ]
        if repl:
            command.append("--repl")
        result = subprocess.run(
            command,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _compile_repl(self, root, units):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(
            "\x1e".join(textwrap.dedent(unit) for unit in units),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                str(self.driver),
                str(source_path),
                str(code_path),
                str(data_path),
                "--repl",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _compile_repl_after_struct_failure(self, root, units):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(
            "\x1e".join(textwrap.dedent(unit) for unit in units),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                str(self.driver),
                str(source_path),
                str(code_path),
                str(data_path),
                "--repl-rollback",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _compile_after_failure(self, root, failing_source, retry_source):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(
            textwrap.dedent(failing_source)
            + "\x1e"
            + textwrap.dedent(retry_source),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                str(self.driver),
                str(source_path),
                str(code_path),
                str(data_path),
                "--recover",
            ],
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

                .org 0x1010
                  jmp test_print
                .org 0x1020
                  jmp test_print
                .org 0x1100
                test_print:
                  pushl %ebp
                  movl %esp, %ebp
                  pushl %eax
                  pushl %ebx
                  pushl %ecx
                  pushl %edx
                  movl 8(%ebp), %ecx
                  xorl %edx, %edx
                1:
                  cmpb $0, (%ecx,%edx,1)
                  je 2f
                  incl %edx
                  jmp 1b
                2:
                  movl $4, %eax
                  movl $2, %ebx
                  int $0x80
                  popl %edx
                  popl %ecx
                  popl %ebx
                  popl %eax
                  leave
                  ret

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

    def _compile_and_run(self, source, repl=False):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-runtime-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            compile_result, _code, _data = self._compile(
                root, source, repl=repl
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            entry_offset = int(compile_result.stdout.strip())
            return self._run_i386(root, entry_offset)

    def _compile_repl_and_run(self, units):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-runtime-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            compile_result, _code, _data = self._compile_repl(root, units)
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

    def test_adjacent_string_literals_can_exceed_one_token(self):
        first = "a" * 700
        second = "b" * 700
        result = self._compile_and_run(
            f"""
            int main() {{
              char *text =
                  "{first}"
                  "{second}";
              if (text[0] != 'a') return 1;
              if (text[699] != 'a') return 2;
              if (text[700] != 'b') return 3;
              if (text[1399] != 'b') return 4;
              if (text[1400] != 0) return 5;
              return 0;
            }}
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_adjacent_string_initializer_uses_the_full_literal(self):
        first = "c" * 700
        second = "d" * 700
        result = self._compile_and_run(
            f"""
            char *text =
                "{first}"
                "{second}";

            int main() {{
              if (text[699] != 'c') return 1;
              if (text[700] != 'd') return 2;
              if (text[1399] != 'd') return 3;
              if (text[1400] != 0) return 4;
              return 0;
            }}
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_adjacent_string_initializer_uses_the_full_literal(self):
        first = "e" * 700
        second = "f" * 700
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-string-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                f'char *text = "{first}" "{second}";',
                repl=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            data = data_path.read_bytes()
        string_offset = int.from_bytes(data[:4], "little") - 0x01200000
        self.assertEqual(data[string_offset : string_offset + 700], b"e" * 700)
        self.assertEqual(
            data[string_offset + 700 : string_offset + 1400],
            b"f" * 700,
        )
        self.assertEqual(data[string_offset + 1400], 0)

    def test_struct_typedefs_keep_their_tag_and_member_layout(self):
        result = self._compile_and_run(
            """
            typedef struct TaggedPair {
              int left;
              int right;
            } TaggedPair;
            typedef TaggedPair PairAlias;
            typedef TaggedPair *TaggedPairPointer;

            typedef struct {
              int value;
            } AnonymousValue;

            typedef struct Node {
              int value;
              struct Node *next;
            } Node;

            int pair_sum(TaggedPairPointer pair) {
              return pair->left + pair->right;
            }

            int main() {
              PairAlias alias_value;
              struct TaggedPair tagged_value;
              AnonymousValue anonymous_value;
              Node first;
              Node second;
              alias_value.left = 7;
              alias_value.right = 11;
              tagged_value.left = 13;
              tagged_value.right = 17;
              anonymous_value.value = 19;
              first.value = 23;
              first.next = &second;
              second.value = 29;
              second.next = 0;
              if (pair_sum(&alias_value) != 18) return 1;
              if (pair_sum(&tagged_value) != 30) return 2;
              if (anonymous_value.value != 19) return 3;
              if (first.next->value != 29) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_record_fields_keeps_the_selected_storage(self):
        result = self._compile_and_run(
            """
            typedef struct TargetRef {
              int leading;
              int key_offset;
              int key_length;
              int trailing;
            } TargetRef;

            int write_key(int *offset, int *length) {
              *offset = 37;
              *length = 41;
              return 0;
            }

            int main() {
              TargetRef target;
              TargetRef *pointer = &target;
              target.leading = 11;
              target.trailing = 13;
              write_key(&target.key_offset, &target.key_length);
              if (target.key_offset != 37 || target.key_length != 41) return 1;
              write_key(&pointer->key_offset, &pointer->key_length);
              if (pointer->key_offset != 37 || pointer->key_length != 41) {
                return 2;
              }
              if (target.leading != 11 || target.trailing != 13) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_record_field_reports_an_unknown_member(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-record-field-address-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                """
                struct TargetRef { int key_offset; };
                int main() {
                  struct TargetRef target;
                  struct TargetRef *pointer = &target;
                  int *missing = &pointer->missing;
                  return missing != 0;
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unknown struct field", result.stderr)

    def test_repl_keeps_a_tagged_struct_typedef_for_later_lines(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-typedef-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile_repl(
                Path(temporary),
                (
                    """
                    typedef struct ReplPair {
                      int left;
                      int right;
                    } ReplPair;
                    """,
                    "ReplPair pair;",
                ),
            )
            data = data_path.read_bytes() if data_path.exists() else b""
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(len(data), 8)

    def test_repl_rolls_back_a_failed_definition_of_an_existing_tag(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = (
                self._compile_repl_after_struct_failure(
                    Path(temporary),
                    (
                        "struct Node;",
                        "typedef struct Node { int poisoned; };",
                        "typedef struct Node { int value; } Node;",
                        "Node recovered;",
                    ),
                )
            )
            data = data_path.read_bytes() if data_path.exists() else b""

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(data), 4)

        runtime_source = (KERNEL_LANG / "cupidc.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "cc_repl_checkpoint_structs(&repl_state);",
            runtime_source,
        )
        self.assertIn(
            "cc_repl_restore_structs(&repl_state);",
            runtime_source,
        )

    def test_tagged_struct_typedef_requires_an_alias(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-struct-typedef-alias-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                "typedef struct MissingAlias { int value; };",
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("expected typedef alias name", result.stderr)

    def test_tagged_struct_typedef_rejects_an_incomplete_value_field(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-struct-typedef-incomplete-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                """
                typedef struct Broken {
                  struct Pending value;
                } Broken;
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("field has incomplete struct type", result.stderr)
        self.assertNotIn("expected typedef alias name", result.stderr)

    def test_struct_declarations_reject_invalid_field_array_layouts(self):
        cases = (
            (
                "tagged-zero",
                "typedef struct Bad { int values[0]; } Bad;",
                "array size must be positive",
            ),
            (
                "anonymous-negative",
                "typedef struct { int values[-1]; } Bad;",
                "array size must be positive",
            ),
            (
                "tagged-overflow",
                "typedef struct Bad { int values[1073741824]; } Bad;",
                "array allocation size overflow",
            ),
            (
                "tagged-total-overflow",
                "typedef struct Bad { char a[2147483644]; char b[4]; } Bad;",
                "record size overflow",
            ),
            (
                "tagged-allocation-alignment-overflow",
                "typedef struct Bad { char a[2147483644]; char b[3]; } Bad; "
                "Bad value;",
                "record size overflow",
            ),
            (
                "anonymous-allocation-alignment-overflow",
                "typedef struct { char a[2147483644]; char b[3]; } Bad; "
                "Bad value;",
                "record size overflow",
            ),
            (
                "standalone-total-overflow",
                "struct Bad { char a[2147483644]; char b[4]; };",
                "record size overflow",
            ),
            (
                "standalone-local-alignment-overflow",
                "struct Bad { char a[2147483644]; char b[3]; }; "
                "int main() { struct Bad value; return 0; }",
                "record size overflow",
            ),
            (
                "class-alignment-overflow",
                "class Bad { char a[2147483644]; char b[3]; };",
                "record size overflow",
            ),
        )
        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-struct-array-{name}-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn("expected typedef alias name", result.stderr)
                self.assertNotIn("unexpected token", result.stderr)
                self.assertNotIn("[cupidc] Defined", result.stderr)

    def test_repl_struct_rejects_cumulative_field_size_overflow(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                "struct Bad { char a[2147483644]; char b[3]; }; "
                "struct Bad value;",
                repl=True,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("record size overflow", result.stderr)

    def test_repl_struct_global_respects_remaining_data_capacity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile_repl(
                Path(temporary),
                (
                    "char padding[8388600];",
                    "struct ReplWide { int words[4]; };",
                    "struct ReplWide value;",
                ),
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("data section overflow", result.stderr)

    def test_repl_enum_respects_remaining_data_capacity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-enum-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile_repl(
                Path(temporary),
                (
                    "char padding[8388608];",
                    "enum ReplEdge { Value=1 };",
                ),
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("data section overflow", result.stderr)

    def test_array_bound_expression_overflow_is_rejected_in_every_lane(self):
        cases = (
            (
                "record field",
                "struct Bad { char values[65536*65536+1]; };",
                False,
            ),
            ("global", "char values[65536*65536+1];", False),
            (
                "local",
                "int main() { char values[65536*65536+1]; return 0; }",
                False,
            ),
            ("REPL global", "char values[65536*65536+1];", True),
        )
        for label, source, repl in cases:
            with self.subTest(lane=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-expression-lane-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source, repl=repl
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "constant integer expression overflow", result.stderr
                )

    def test_cumulative_local_frame_size_never_wraps(self):
        cases = (
            (
                "arrays",
                "int main() {"
                " char a[1073741820]; char b[1073741820];"
                " char c[1073741820]; return 0; }",
            ),
            (
                "records",
                "struct Wide { char bytes[1073741820]; };"
                " int main() { struct Wide a; struct Wide b;"
                " struct Wide c; return 0; }",
            ),
            (
                "scalar after exact edge",
                "int main() { char a[1073741816]; char b[1073741816];"
                " int scalar; return 0; }",
            ),
            (
                "SIMD alignment",
                "int main() { char bytes[2147483620];"
                " float4 lanes; return 0; }",
            ),
        )
        for label, source in cases:
            with self.subTest(lane=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-local-frame-overflow-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("local frame size overflow", result.stderr)

    def test_constant_integer_expression_checks_every_arithmetic_operator(self):
        cases = (
            ("addition", "2147483647+1", "overflow"),
            ("subtraction", "(-2147483647-1)-1", "overflow"),
            ("multiplication", "65536*65536", "overflow"),
            ("division", "(-2147483647-1)/-1", "overflow"),
            ("negation", "-(-2147483647-1)", "overflow"),
            ("division by zero", "4/0", "division by zero"),
            ("decimal literal", "4294967297", "integer literal overflow"),
            (
                "hexadecimal literal",
                "0x100000001",
                "integer literal overflow",
            ),
        )
        for label, expression, diagnostic in cases:
            with self.subTest(operator=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-expression-operator-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), f"char values[{expression}];"
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)

    def test_uint32_maximum_integer_literals_remain_available(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-uint32-literal-edge-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile(
                Path(temporary),
                "int decimal = 4294967295u; int hexadecimal = 0xffffffffu;",
            )
            data = data_path.read_bytes()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(len(data), 8)
        self.assertEqual(data[:8], b"\xff" * 8)

    def test_hexadecimal_integer_literals_require_digits_and_recover(self):
        for literal in ("0x", "0xu"):
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(
                prefix="private-cupidc-hex-digits-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), f"int value={literal};"
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("expected hexadecimal digits", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-hex-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile_after_failure(
                Path(temporary),
                "int broken=0x;",
                "int recovered=7;",
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertEqual(
                data_path.read_bytes()[:4],
                bytes.fromhex("07000000"),
            )

    def test_unsigned_constant_expressions_use_uint32_wraparound(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-unsigned-constant-expression-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile(
                Path(temporary),
                "enum Edge {"
                " BelowSign=0x80000000u-1,"
                " AboveSign=0x7fffffffu+1u,"
                " Wrapped=0x80000000u*2u,"
                " Base=0x80000000u,"
                " BelowBase=Base-1"
                "};",
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            data = data_path.read_bytes()
        self.assertGreaterEqual(len(data), 20)
        self.assertEqual(
            data[:20],
            bytes.fromhex(
                "ffffff7f 00000080 00000000 00000080 ffffff7f"
            ),
        )

    def test_enum_maximum_only_overflows_for_an_implicit_successor(self):
        accepted = (
            "enum Edge { Maximum=2147483647, };",
            "enum Edge { Maximum=2147483647, Reset=0 };",
        )
        for repl in (False, True):
            for source in accepted:
                with self.subTest(repl=repl, source=source), \
                     tempfile.TemporaryDirectory(
                         prefix="private-cupidc-enum-maximum-",
                         ignore_cleanup_errors=True,
                     ) as temporary:
                    result, _code_path, _data_path = self._compile(
                        Path(temporary), source, repl=repl
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )

            with self.subTest(repl=repl, source="implicit overflow"), \
                 tempfile.TemporaryDirectory(
                     prefix="private-cupidc-enum-overflow-",
                     ignore_cleanup_errors=True,
                 ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary),
                    "enum Edge { Maximum=2147483647, Overflow };",
                    repl=repl,
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("enum value overflow", result.stderr)

    def test_single_string_token_overflow_has_a_useful_diagnostic(self):
        oversized = "x" * 1024
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-string-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                f'int main() {{ char *text = "{oversized}"; return text[0]; }}',
            )
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "string literal is too long; split it into adjacent literals",
            result.stderr,
        )
        diagnostic = (
            "string literal is too long; split it into adjacent literals"
        )
        self.assertEqual(
            result.stderr.count(f"[cupidc] error (line 1): {diagnostic}"),
            1,
            result.stderr,
        )
        self.assertEqual(
            result.stderr.count(f"CupidC Error (line 1): {diagnostic}"),
            1,
            result.stderr,
        )
        self.assertNotIn("x" * 128, result.stderr)

    def test_adjacent_string_reports_data_section_overflow(self):
        piece = '"' + ("z" * 1023) + '"\n'
        source = "int main() { char *text =\n" + (piece * 8201) + "; return 0; }"
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-string-data-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(Path(temporary), source)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("data section overflow", result.stderr)

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

    def test_feature14_source_executes_through_the_private_simd_path(self):
        result = self._compile_and_run(
            (REPO_ROOT / "bin" / "feature14_simd.cc").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for marker in (
            "[feature14-operator] PASS float=4 double=4",
            "[feature14-array] PASS global=2 local=2 static=2 "
            "sizeof=16 index=1",
            "[feature14-minmax] PASS nan=4 signed_zero=4",
            "[feature14-nan] PASS float_left=",
            "PASS feature14_simd",
        ):
            self.assertIn(marker, result.stderr)

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

    def test_decimal_literal_uses_the_nearest_binary64_payload(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-payload-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double value = 0.75;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:8]

        self.assertEqual(payload, bytes.fromhex("000000000000e83f"))

    def test_equivalent_exponent_literal_keeps_the_exact_binary64_payload(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-exponent-payload-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double value = 75e-2;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:8]

        self.assertEqual(payload, bytes.fromhex("000000000000e83f"))

    def test_decimal_literal_ties_round_to_even_at_each_width(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-ties-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                float float_down = 16777217.0f;
                float float_up = 16777219.0F;
                double double_down = 9007199254740993.0;
                double double_up = 9007199254740995.0;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:24]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "0000804b"
                "0200804b"
                "0000000000004043"
                "0200000000004043"
            ),
        )

    def test_decimal_literals_cover_subnormal_maximum_and_overflow_edges(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-edges-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                float float_minimum = 1.401298464324817e-45f;
                float float_maximum = 3.4028234663852886e38f;
                float float_overflow = 3.4028236e38f;
                double double_minimum = 5e-324;
                double double_maximum = 1.7976931348623157e308;
                double double_overflow = 1.7976931348623159e308;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:36]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "01000000"
                "ffff7f7f"
                "0000807f"
                "0100000000000000"
                "ffffffffffffef7f"
                "000000000000f07f"
            ),
        )

    def test_long_decimal_significands_resolve_binary64_halfway_cases(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-long-decimal-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double halfway =
                    1.00000000000000011102230246251565404236316680908203125;
                double above_halfway =
                    1.00000000000000011102230246251565404236316680908203126;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:16]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "000000000000f03f"
                "010000000000f03f"
            ),
        )

    def test_extreme_exponents_preserve_zero_sign_and_ieee_limits(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-extreme-decimal-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                float negative_zero = -0.0f;
                double negative_underflow = -1e-4000;
                double positive_underflow = 1e-4000;
                double positive_overflow = 1e4000;
                double zero_with_large_exponent =
                    0e999999999999999999999999999999999999;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:36]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "00000080"
                "0000000000000080"
                "0000000000000000"
                "000000000000f07f"
                "0000000000000000"
            ),
        )

    def test_decimal_literal_requires_exponent_digits(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-exponent-error-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                double value = 1e+;
                """,
            )

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "decimal floating literal exponent requires a digit",
            result.stderr,
        )

    def test_function_body_decimal_error_keeps_the_lexer_diagnostic(self):
        cases = (
            (
                "double value = 1e+;",
                "decimal floating literal exponent requires a digit",
            ),
            (
                f"double value = {'1.' + '0' * 94};",
                "numeric literal exceeds 95 characters",
            ),
        )
        for declaration, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                with tempfile.TemporaryDirectory(
                    prefix="private-cupidc-decimal-body-error-",
                    ignore_cleanup_errors=True,
                ) as temporary:
                    result, _code, _data = self._compile(
                        Path(temporary),
                        f"""
                        int main() {{
                          {declaration}
                          return 0;
                        }}
                        """,
                    )

                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                public_errors = [
                    line
                    for line in result.stderr.splitlines()
                    if line.startswith("CupidC Error")
                ]
                self.assertEqual(len(public_errors), 1, result.stderr)
                self.assertIn(diagnostic, public_errors[0])
                self.assertNotIn("expected expression", public_errors[0])

    def test_decimal_recovery_does_not_replace_the_first_diagnostic(self):
        overlong = "1." + "0" * 94
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-first-error-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                f"""
                int first() {{
                  double value = 1e+;
                  return 0;
                }}
                int second() {{
                  double value = {overlong};
                  return 0;
                }}
                """,
            )

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        public_errors = [
            line
            for line in result.stderr.splitlines()
            if line.startswith("CupidC Error")
        ]
        self.assertEqual(len(public_errors), 1, result.stderr)
        self.assertIn(
            "decimal floating literal exponent requires a digit",
            public_errors[0],
        )
        self.assertNotIn(
            "numeric literal exceeds 95 characters",
            public_errors[0],
        )
        self.assertNotIn("expected expression", public_errors[0])

    def test_decimal_literal_limit_accepts_95_characters_and_rejects_96(self):
        accepted_literal = "1." + "0" * 93
        rejected_literal = accepted_literal + "0"
        self.assertEqual(len(accepted_literal), 95)
        self.assertEqual(len(rejected_literal), 96)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-length-limit-",
            ignore_cleanup_errors=True,
        ) as temporary:
            accepted_result, _code, accepted_data = self._compile(
                Path(temporary),
                f"double value = {accepted_literal};\n",
            )
            self.assertEqual(
                accepted_result.returncode,
                0,
                accepted_result.stdout + accepted_result.stderr,
            )
            self.assertEqual(
                accepted_data.read_bytes()[:8],
                bytes.fromhex("000000000000f03f"),
            )
            result, _code, _data = self._compile(
                Path(temporary),
                f"double value = {rejected_literal};\n",
            )
            suffix_result, _code, _data = self._compile(
                Path(temporary),
                f"float value = {accepted_literal}f;\n",
            )

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "numeric literal exceeds 95 characters",
            result.stderr,
        )
        self.assertNotIn("unexpected token", result.stderr)
        self.assertNotIn("additional error", result.stderr)
        self.assertEqual(
            suffix_result.returncode,
            2,
            suffix_result.stdout + suffix_result.stderr,
        )
        self.assertIn(
            "numeric literal exceeds 95 characters",
            suffix_result.stderr,
        )

    def test_integer_suffix_counts_toward_the_numeric_literal_limit(self):
        accepted_literals = (
            "0" * 94 + "u",
            "0x" + "0" * 92 + "U",
        )
        rejected_literals = (
            "0" * 95 + "u",
            "0x" + "0" * 93 + "U",
        )
        for literal in accepted_literals:
            self.assertEqual(len(literal), 95)
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(
                prefix="private-cupidc-integer-suffix-limit-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, data_path = self._compile(
                    Path(temporary),
                    f"unsigned int value = {literal};\n",
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertEqual(data_path.read_bytes()[:4], b"\0" * 4)

        for literal in rejected_literals:
            self.assertEqual(len(literal), 96)
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(
                prefix="private-cupidc-integer-suffix-overflow-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary),
                    f"unsigned int value = {literal};\n",
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "numeric literal exceeds 95 characters",
                    result.stderr,
                )
                self.assertNotIn("unexpected token", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-integer-suffix-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile_after_failure(
                Path(temporary),
                f"unsigned int broken = {rejected_literals[0]};",
                "int recovered = 7;",
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            self.assertEqual(
                data_path.read_bytes()[:4],
                bytes.fromhex("07000000"),
            )

    def test_overlong_decimal_literal_keeps_the_following_token_available(self):
        literal = "1." + "0" * 94
        result = subprocess.run(
            [
                str(self.driver),
                "--check-number-boundary",
                f"{literal}; 0.75",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(
            result.stdout.splitlines(),
            ["numeric literal exceeds 95 characters", "0.75"],
        )

    def test_decimal_literal_unbounded_exponent_reaches_ieee_overflow(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double value = 1e4097;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:8]

        self.assertEqual(payload, bytes.fromhex("000000000000f07f"))

    def test_runtime_decimal_literals_keep_their_rounded_payloads(self):
        result = self._compile_and_run(
            """
            int main() {
              double wide;
              float narrow;
              wide = 0.75;
              narrow = 0.1f;
              if (*(int *)&wide != 0) return 1;
              if (*(int *)&narrow != 0x3dcccccd) return 2;
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

    def test_floating_pointers_keep_typed_indirect_lvalues(self):
        result = self._compile_and_run(
            """
            float singles[3];
            double doubles[3];
            float addressed_float;
            double addressed_double;
            int index_calls;
            int pointer_calls;

            int next_index() {
              index_calls += 1;
              return 1;
            }

            double *next_double_pointer() {
              pointer_calls += 1;
              return doubles;
            }

            void update(float *single_pointer, double *double_pointer) {
              *single_pointer = -0.0f;
              single_pointer[next_index()] = 3.0f;
              single_pointer[1] += 5.0;
              single_pointer[1] -= 2.0f;
              single_pointer[1] *= 4;
              single_pointer[1] /= 3.0;
              *double_pointer = 6.0;
              *double_pointer += 2.0f;
              *double_pointer -= 1;
              *double_pointer *= 3.0;
              *double_pointer /= 7.0;
              *next_double_pointer() += 4.0;
            }

            int main() {
              float *single_pointer = singles;
              double *double_pointer = doubles;
              float *addressed_float_pointer = &addressed_float;
              double *addressed_double_pointer = &addressed_double;
              update(single_pointer, double_pointer);
              *addressed_float_pointer = 2.75f;
              *addressed_double_pointer = 8.25;
              if (index_calls != 1) return 1;
              if (pointer_calls != 1) return 2;
              if (single_pointer[1] != 8.0f) return 3;
              if (double_pointer[0] != 7.0) return 4;
              if (sizeof(*single_pointer) != 4) return 5;
              if (sizeof(*double_pointer) != 8) return 6;
              if (*(int *)single_pointer != (-2147483647 - 1)) return 7;
              if (next_double_pointer()[0] != 7.0) return 8;
              if (pointer_calls != 2) return 9;
              if ((&addressed_float)[0] != 2.75f) return 10;
              if ((&addressed_double)[0] != 8.25) return 11;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_direct_simd_arithmetic_preserves_lane_values_and_operand_order(self):
        result = self._compile_and_run(
            """
            int main() {
              float4 float_left = {8.0f, 12.0f, 18.0f, 24.0f};
              float4 float_right = {2.0f, 3.0f, 6.0f, 8.0f};
              float4 float_result;
              double2 double_left = {20.0, 30.0};
              double2 double_right = {4.0, 5.0};
              double2 double_result;

              float_result = float_left + float_right;
              if (float_result.x != 10.0f || float_result.w != 32.0f)
                return 1;
              float_result = float_left - float_right;
              if (float_result.y != 9.0f || float_result.z != 12.0f)
                return 2;
              float_result = float_left * float_right;
              if (float_result.x != 16.0f || float_result.w != 192.0f)
                return 3;
              float_result = float_left / float_right;
              if (float_result.y != 4.0f || float_result.z != 3.0f)
                return 4;

              double_result = double_left + double_right;
              if (double_result.x != 24.0 || double_result.y != 35.0)
                return 5;
              double_result = double_left - double_right;
              if (double_result.x != 16.0 || double_result.y != 25.0)
                return 6;
              double_result = double_left * double_right;
              if (double_result.x != 80.0 || double_result.y != 150.0)
                return 7;
              double_result = double_left / double_right;
              if (double_result.x != 5.0 || double_result.y != 6.0)
                return 8;
              double2 fractional_left = {1.5, 2.5};
              double2 fractional_right = {0.5, 2.0};
              double_result = fractional_left * fractional_right;
              double fractional_x = double_result.x;
              int *fractional_bits = (int *)&fractional_x;
              if (fractional_bits[0] != 0) return 9;
              if (fractional_bits[1] != 0x3fe80000) return 10;
              if (double_result.y != 5.0) return 11;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_add_and_multiply_keep_source_order_in_machine_code(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-order-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                int main() {
                  float4 float_left;
                  float4 float_right;
                  float4 float_result;
                  double2 double_left;
                  double2 double_right;
                  double2 double_result;
                  float_result = float_left + float_right;
                  float_result = float_left * float_right;
                  float_result = _mm_add_ps(float_left, float_right);
                  float_result = _mm_mul_ps(float_left, float_right);
                  double_result = double_left + double_right;
                  double_result = double_left * double_right;
                  double_result = _mm_add_pd(double_left, double_right);
                  double_result = _mm_mul_pd(double_left, double_right);
                  return 0;
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            code = code_path.read_bytes()

        add_left = b"\x0f\x58\xc8\x0f\x28\xc1"
        mul_left = b"\x0f\x59\xc8\x0f\x28\xc1"
        self.assertEqual(code.count(add_left), 4)
        self.assertEqual(code.count(mul_left), 4)
        self.assertEqual(code.count(b"\x66" + add_left), 2)
        self.assertEqual(code.count(b"\x66" + mul_left), 2)
        self.assertNotIn(b"\x0f\x58\xc1", code)
        self.assertNotIn(b"\x0f\x59\xc1", code)

    def test_deeper_floating_pointers_have_a_useful_diagnostic(self):
        for declaration in ("float **values;", "double **values;"):
            with self.subTest(declaration=declaration), tempfile.TemporaryDirectory(
                prefix="private-cupidc-deep-floating-pointer-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), declaration
                )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "floating pointer depth greater than one is not supported",
                result.stderr,
            )

    def test_indirect_floating_updates_have_a_useful_diagnostic(self):
        cases = (
            "void probe(float *value) { ++*value; }",
            "void probe(double *value) { (*value)--; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-indirect-floating-update-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "indirect increment or decrement is not supported",
                result.stderr,
            )

    def test_floating_lvalue_failure_allows_same_process_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-floating-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            compile_result, _code, _data = self._compile_after_failure(
                root,
                "float **unsupported_pointer;",
                """
                double values[2];

                int main() {
                  double *pointer = values;
                  pointer[1] = 3.5;
                  *pointer = 1.25;
                  return pointer[1] == 3.5 && *pointer == 1.25 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            entry_offset = int(compile_result.stdout.strip())
            run_result = self._run_i386(root, entry_offset)
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_typed_floating_pointers_keep_direct_update_results(self):
        result = self._compile_and_run(
            """
            struct Pair {
              int first;
              int second;
            };

            int main() {
              char char_values[2];
              int int_values[2];
              float single_values[2];
              double double_values[2];
              struct Pair pair_values[2];
              char *char_pointer = char_values;
              int *int_pointer = int_values;
              float *single_pointer = single_values;
              double *double_pointer = double_values;
              struct Pair *pair_pointer = pair_values;
              char_values[1] = 5;
              int_values[1] = 7;
              single_values[0] = 1.0f;
              single_values[1] = 2.0f;
              double_values[0] = 3.0;
              double_values[1] = 4.0;
              pair_values[1].second = 11;
              char *old_char = char_pointer++;
              int *old_int = int_pointer++;
              float *old_single = single_pointer++;
              double *old_double = double_pointer++;
              struct Pair *old_pair = pair_pointer++;

              if (old_char != char_values) return 1;
              if (old_int != int_values) return 2;
              if (old_single != single_values) return 3;
              if (old_double != double_values) return 4;
              if (old_pair != pair_values) return 5;
              if (*char_pointer != 5) return 6;
              if (*int_pointer != 7) return 7;
              if (*single_pointer != 2.0f) return 8;
              if (*double_pointer != 4.0) return 9;
              if (pair_pointer->second != 11) return 10;
              if (--char_pointer != char_values) return 11;
              if (--int_pointer != int_values) return 12;
              if (--single_pointer != single_values) return 13;
              if (--double_pointer != double_values) return 14;
              if (--pair_pointer != pair_values) return 15;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_incomplete_struct_pointer_update_has_a_useful_diagnostic(self):
        cases = (
            "int main() { struct Pending *pointer; pointer++; return 0; }",
            "int main() { struct Pending *pointer; --pointer; return 0; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-incomplete-struct-pointer-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "struct pointer update requires a complete pointed-to type",
                result.stderr,
            )

    def test_floating_array_parameters_decay_to_typed_pointers(self):
        result = self._compile_and_run(
            """
            int check_function(float singles[], double doubles[]) {
              singles[1] = 3.25f;
              doubles[1] = 7.5;
              return singles[1] == 3.25f && doubles[1] == 7.5 ? 0 : 1;
            }

            class Probe {
              int Check(float singles[], double doubles[]) {
                singles[0] = 1.5f;
                doubles[0] = 9.25;
                return singles[0] == 1.5f && doubles[0] == 9.25 ? 0 : 2;
              }
            };

            int main() {
              float singles[2];
              double doubles[2];
              Probe probe;
              int function_status = check_function(singles, doubles);
              int method_status = probe.Check(singles, doubles);
              if (function_status != 0) return function_status;
              if (method_status != 0) return method_status;
              if (singles[1] != 3.25f || doubles[1] != 7.5) return 3;
              if (singles[0] != 1.5f || doubles[0] != 9.25) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_array_parameters_have_a_useful_diagnostic(self):
        cases = (
            "void probe(float4 values[]) {}",
            "class Probe { void Check(double2 values[]) {} };",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-parameter-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "SIMD array parameters are not supported", result.stderr
            )

    def test_unevaluated_sizeof_keeps_diagnostics_and_recovers(self):
        failing_source = "int main() { return sizeof(missing_value); }"
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-sizeof-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary), failing_source
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("undefined variable", result.stderr)
        self.assertNotIn("operand has no object size", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-sizeof-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            retry_result, _code, _data = self._compile_after_failure(
                root,
                failing_source,
                "int main() { double value = 4.5; return value == 4.5 ? 0 : 1; }",
            )
            self.assertEqual(
                retry_result.returncode,
                0,
                retry_result.stdout + retry_result.stderr,
            )
            entry_offset = int(retry_result.stdout.strip())
            run_result = self._run_i386(root, entry_offset)
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_fixed_simd_arrays_execute_across_supported_storage(self):
        result = self._compile_and_run(
            """
            float4 global_floats[3];
            int global_sentinel;
            double2 global_doubles[2];

            int touch_static_array() {
              static float4 saved[2];
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              if (saved[0].w == 0.0f) {
                saved[0] = seed;
                return 1;
              }
              saved[1] = saved[0] * seed;
              if (saved[1].x != 1.0f || saved[1].w != 16.0f)
                return 2;
              return 0;
            }

            int main() {
              float4 local_floats[2];
              double2 local_doubles[2];
              float4 float_seed = {8.0f, 12.0f, 18.0f, 24.0f};
              float4 float_step = {2.0f, 3.0f, 6.0f, 8.0f};
              double2 double_seed = {20.0, 30.0};
              double2 double_step = {4.0, 5.0};

              global_sentinel = 73;
              global_floats[0] = float_seed;
              global_floats[1] = float_step;
              global_floats[1] += float_seed;
              global_floats[1] -= float_step;
              global_floats[1] *= float_step;
              global_floats[1] /= float_step;
              global_doubles[0] = double_seed;
              global_doubles[1] = double_step;
              global_doubles[1] += double_seed;
              global_doubles[1] -= double_step;
              global_doubles[1] *= double_step;
              global_doubles[1] /= double_step;

              local_floats[0] = global_floats[0];
              local_floats[1] = global_floats[1];
              local_doubles[0] = global_doubles[0];
              local_doubles[1] = global_doubles[1];
              int selected = 0;
              local_floats[selected++] += float_step;

              if (selected != 1) return 1;
              if (local_floats[0].x != 10.0f ||
                  local_floats[1].w != 24.0f)
                return 2;
              if (local_doubles[0].x != 20.0 ||
                  local_doubles[1].y != 30.0)
                return 3;
              if (global_floats[2].z != 0.0f) return 4;
              if (sizeof(*global_floats) != 16) return 5;
              if (sizeof(*local_doubles) != 16) return 6;
              if (global_sentinel != 73) return 7;
              if (touch_static_array() != 1) return 8;
              if (touch_static_array() != 0) return 9;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_persistent_repl_simd_arrays_keep_values_between_lines(self):
        result = self._compile_and_run(
            """
            float4 persistent_floats[2];
            double2 persistent_doubles[2];
            int verify_persistent_vectors() {
              float4 float_seed = {2.0f, 4.0f, 6.0f, 8.0f};
              double2 double_seed = {3.0, 9.0};
              persistent_floats[1] = float_seed;
              persistent_doubles[1] = double_seed;
              persistent_floats[1] *= float_seed;
              persistent_doubles[1] += double_seed;
              if (persistent_floats[1].z != 36.0f) return 1;
              if (persistent_doubles[1].y != 18.0) return 2;
              if (sizeof(*persistent_floats) != 16) return 3;
              return 0;
            }
            verify_persistent_vectors();
            """,
            repl=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_min_max_keep_second_operand_nan_and_zero_rules(self):
        result = self._compile_and_run(
            """
            int main() {
              float float_nan = 0.0f / 0.0f;
              float float_positive_zero = 0.0f;
              float float_negative_zero = -float_positive_zero;
              float4 float_first = {
                float_nan, float_positive_zero,
                float_nan, float_positive_zero
              };
              float4 float_second = {
                5.0f, float_negative_zero,
                -7.0f, float_negative_zero
              };
              float4 float_min;
              float4 float_max;
              float float_lane;

              double double_nan = 0.0 / 0.0;
              double double_positive_zero = 0.0;
              double double_negative_zero = -double_positive_zero;
              double2 double_first = {double_nan, double_positive_zero};
              double2 double_second = {9.0, double_negative_zero};
              double2 double_min;
              double2 double_max;
              double double_lane;
              int *double_bits;

              float_min = _mm_min_ps(float_first, float_second);
              float_max = _mm_max_ps(float_first, float_second);
              if (float_min.x != 5.0f || float_max.z != -7.0f) return 1;
              float_lane = float_min.y;
              if (*(int *)&float_lane != (int)0x80000000) return 2;
              float_lane = float_max.w;
              if (*(int *)&float_lane != (int)0x80000000) return 3;

              double_min = _mm_min_pd(double_first, double_second);
              double_max = _mm_max_pd(double_first, double_second);
              if (double_min.x != 9.0 || double_max.x != 9.0) return 4;
              double_lane = double_min.y;
              double_bits = (int *)&double_lane;
              if (double_bits[1] != (int)0x80000000) return 5;
              double_lane = double_max.y;
              double_bits = (int *)&double_lane;
              if (double_bits[1] != (int)0x80000000) return 6;
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
            ("global SIMD zero", "float4 values[0];"),
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
            (
                "global zero third bound",
                "double values[2][2][0];",
            ),
            (
                "local negative third bound",
                "int main() { float values[2][2][1 - 2]; return 0; }",
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
            ("global SIMD", "double2 values[134217728];"),
            (
                "local float",
                "int main() { float values[536870912]; return 0; }",
            ),
            (
                "block static char alignment",
                "int main() { static char values[2147483647]; return 0; }",
            ),
            ("global two dimensional int", "int values[32768][32768];"),
            (
                "global three dimensional double",
                "double values[32768][32768][2];",
            ),
            (
                "block static three dimensional float",
                "int main() { static float values[32768][32768][2]; return 0; }",
            ),
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

    def test_global_floating_matrices_keep_row_and_scalar_widths(self):
        result = self._compile_and_run(
            """
            int leading_canary = 17;
            float singles[2][3];
            double doubles[2][2];
            int trailing_canary = 29;

            int main() {
              singles[0][0] = 1.25f;
              singles[1][2] = -3.5f;
              doubles[0][1] = 12.5;
              doubles[1][0] = singles[1][2];
              if (singles[0][0] != 1.25f) return 1;
              if (singles[1][2] != -3.5f) return 2;
              if (doubles[0][1] != 12.5) return 3;
              if (doubles[1][0] != -3.5) return 4;
              if (sizeof(*singles) != 12) return 5;
              if (sizeof(**singles) != 4) return 6;
              if (sizeof(*doubles) != 16) return 7;
              if (sizeof(**doubles) != 8) return 8;
              if (leading_canary != 17) return 9;
              if (trailing_canary != 29) return 10;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unevaluated_subscript_sizeof_keeps_remaining_row_size(self):
        result = self._compile_and_run(
            """
            int index_calls;
            float matrix[2][3];
            double cube[2][3][4];

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              if (sizeof(matrix[next_index()]) != 12) return 1;
              if (sizeof(cube[next_index()]) != 96) return 2;
              if (sizeof(cube[0][next_index()]) != 32) return 3;
              if (sizeof(matrix[0] == matrix[1]) != 4) return 4;
              if (sizeof(1 ? matrix[0] : matrix[1]) != 4) return 5;
              if (index_calls != 0) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_matrices_cover_local_static_and_three_dimensional_storage(self):
        result = self._compile_and_run(
            """
            float global_cube[2][2][2];
            int global_canary = 31;

            int main() {
              int leading_canary = 37;
              float local_matrix[2][3];
              double local_cube[2][2][2];
              int trailing_canary = 41;
              static double saved_matrix[2][2];
              static float saved_cube[2][2][2];

              global_cube[1][0][1] = 2.5f;
              global_cube[1][0][1] *= 2;
              local_matrix[1][2] = 7.25f;
              local_cube[0][1][1] = 9.5;
              saved_matrix[1][0] = local_matrix[1][2];
              saved_cube[1][1][0] = local_cube[0][1][1];
              saved_cube[1][1][0] += 0.5f;

              if (global_cube[1][0][1] != 5.0f) return 1;
              if (local_matrix[1][2] != 7.25f) return 2;
              if (local_cube[0][1][1] != 9.5) return 3;
              if (saved_matrix[1][0] != 7.25) return 4;
              if (saved_cube[1][1][0] != 10.0f) return 5;
              if (sizeof(*global_cube) != 16) return 6;
              if (sizeof(**global_cube) != 8) return 7;
              if (sizeof(***global_cube) != 4) return 8;
              if (sizeof(*local_cube) != 32) return 9;
              if (sizeof(**local_cube) != 16) return 10;
              if (sizeof(***local_cube) != 8) return 11;
              if (leading_canary != 37) return 12;
              if (trailing_canary != 41) return 13;
              if (global_canary != 31) return 14;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_floating_matrices_remain_typed_across_lines(self):
        result = self._compile_repl_and_run(
            (
                "double repl_matrix[2][3];",
                "float repl_cube[2][2][2];",
                "int repl_canary = 43;",
                """
                int probe() {
                  repl_matrix[1][2] = 6.5;
                  repl_matrix[1][2] += 1.0f;
                  repl_cube[1][0][1] = 4.25f;
                  if (repl_matrix[1][2] != 7.5) return 1;
                  if (repl_cube[1][0][1] != 4.25f) return 2;
                  if (sizeof(*repl_matrix) != 24) return 3;
                  if (sizeof(**repl_matrix) != 8) return 4;
                  if (sizeof(*repl_cube) != 16) return 5;
                  if (sizeof(**repl_cube) != 8) return 6;
                  if (sizeof(***repl_cube) != 4) return 7;
                  if (repl_canary != 43) return 8;
                  return 0;
                }
                """,
                "probe;",
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

    def test_multidimensional_simd_array_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-array-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                float4 vectors[2][2];
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("SIMD arrays support one dimension", result.stderr)

    def test_simd_operator_diagnostics_name_the_supported_boundary(self):
        cases = (
            (
                "different vector widths",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  double2 right = {1, 2};
                  float4 result;
                  result = left + right;
                  return 0;
                }
                """,
                "SIMD operator requires matching float4 or double2 operands",
            ),
            (
                "vector and scalar",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 result;
                  result = left + 1;
                  return 0;
                }
                """,
                "SIMD operator requires matching float4 or double2 operands",
            ),
            (
                "unsupported remainder",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 right = {1, 2, 3, 4};
                  float4 result;
                  result = left % right;
                  return 0;
                }
                """,
                "SIMD operator supports only +, -, *, and /",
            ),
            (
                "unsupported comparison",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 right = {1, 2, 3, 4};
                  if (left == right) return 1;
                  return 0;
                }
                """,
                "SIMD operator supports only +, -, *, and /",
            ),
        )

        for label, source, message in cases:
            with self.subTest(operation=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-operator-",
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
                self.assertIn(message, result.stderr)

    def test_simd_array_assignment_diagnostics_name_the_required_type(self):
        cases = (
            (
                "float4 from double2",
                """
                float4 values[1];
                int main() {
                  double2 value = {1, 2};
                  values[0] = value;
                  return 0;
                }
                """,
                "float4 array assignment requires a float4 value",
            ),
            (
                "unsupported bitwise compound",
                """
                double2 values[1];
                int main() {
                  double2 value = {1, 2};
                  values[0] &= value;
                  return 0;
                }
                """,
                "SIMD array compound assignment supports only +=, -=, *=, and /=",
            ),
        )

        for label, source, message in cases:
            with self.subTest(assignment=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-assignment-",
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
                self.assertIn(message, result.stderr)

    def test_simd_pointer_and_new_forms_have_useful_diagnostics(self):
        cases = (
            ("spelled pointer", "float4 *values;", "SIMD pointer types"),
            (
                "address of vector",
                """
                int main() {
                  float4 value = {1, 2, 3, 4};
                  &value;
                  return 0;
                }
                """,
                "SIMD pointer expressions",
            ),
            (
                "vector allocation",
                """
                int main() {
                  new double2[2];
                  return 0;
                }
                """,
                "SIMD allocation with new",
            ),
        )

        for label, source, message in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-pointer-",
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
                self.assertIn(message, result.stderr)

    def test_simd_struct_field_arrays_have_a_useful_diagnostic(self):
        cases = (
            ("struct SIMD field", "struct Samples { float4 values[2]; };"),
            ("class SIMD field", "class Samples { double2 values[2]; };"),
        )
        for label, source in cases:
            with self.subTest(declaration=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-diagnostic-",
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
                    "SIMD struct field arrays are not supported",
                    result.stderr,
                )

    def test_address_of_record_array_element_keeps_the_selected_object(self):
        result = self._compile_and_run(
            """
            struct Reading {
              float gain;
              double bias;
              float taps[3];
            };

            struct Reading readings[2];

            int main() {
              struct Reading *selected = &readings[1];
              readings[1].gain = 1.5f;
              readings[1].bias = 2.25;
              readings[1].taps[2] = 3.25f;
              selected->gain += 0.5f;
              selected->bias *= 2.0;
              if (selected != &readings[1]) return 1;
              if (selected->gain != 2.0f) return 2;
              if (selected->bias != 4.5) return 3;
              if (selected->taps[2] != 3.25f) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_array_row_has_a_useful_diagnostic(self):
        cases = (
            "int main() { float values[2][3]; float *row = &values[1]; }",
            (
                "int main() { double values[2][3][4]; "
                "double *row = &values[1][2]; }"
            ),
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-array-row-address-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "address of an array row is not supported", result.stderr
            )

    def test_floating_record_fields_keep_scalar_and_array_storage_typed(self):
        result = self._compile_and_run(
            """
            struct Sample {
              int leading;
              float reading;
              double values[2];
              float *reading_pointer;
              int trailing;
            };

            class Meter {
              int leading;
              double reading;
              float values[2];
              double *reading_pointer;
              int trailing;
            };

            struct Sample samples[2];
            Meter meter;
            int index_calls;

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              float pointer_singles[2];
              double pointer_doubles[2];
              struct Sample *sample_pointer = samples;
              Meter *meter_pointer = &meter;
              pointer_singles[1] = 6.25f;
              pointer_doubles[1] = 12.5;
              samples[0].leading = 11;
              samples[0].reading = -0.0f;
              samples[0].values[0] = 2.0;
              samples[0].trailing = 13;
              samples[next_index()].reading = 3.0f;
              samples[1].reading += 5.0;
              samples[1].reading -= 2;
              samples[1].reading *= 4.0f;
              samples[1].reading /= 3.0;
              samples[1].values[1] = 9.0;
              samples[1].values[1] += 3.0f;
              sample_pointer->values[1] = 7.5;
              sample_pointer->values[1] *= 2;
              sample_pointer->reading_pointer = pointer_singles;

              meter.leading = 17;
              meter.reading = 20.0;
              meter_pointer->reading /= 4.0f;
              meter.values[0] = 1.5f;
              meter_pointer->values[0] += 2;
              meter_pointer->reading_pointer = pointer_doubles;
              meter.trailing = 19;

              if (index_calls != 1) return 1;
              if (samples[1].reading != 8.0f) return 2;
              if (samples[1].values[1] != 12.0) return 3;
              if (sample_pointer->values[1] != 15.0) return 4;
              if ((1.0f / samples[0].reading) >= 0.0f) return 5;
              if (samples[0].leading != 11) return 6;
              if (samples[0].trailing != 13) return 7;
              if (meter.reading != 5.0) return 8;
              if (meter.values[0] != 3.5f) return 9;
              if (meter.leading != 17) return 10;
              if (meter.trailing != 19) return 11;
              if (sample_pointer->reading_pointer[1] != 6.25f) return 12;
              if (meter_pointer->reading_pointer[1] != 12.5) return 13;
              if (sizeof(samples[next_index()].values[1]) != 8) return 14;
              if (sizeof(meter.values[0]) != 4) return 15;
              if (index_calls != 1) return 16;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

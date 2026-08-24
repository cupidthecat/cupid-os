import os
import shlex
import shutil
import struct
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

from tests.test_private_cupidc_float_truth_emitter import _compiler_command


REPO_ROOT = Path(__file__).resolve().parents[1]
LEXER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_lex.cc"
PARSER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_parse.cc"
ELF_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_elf.cc"
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
        (cls.driver_root / "vfs.h").write_text(
            textwrap.dedent(
                """
                #ifndef CUPID_TEST_VFS_H
                #define CUPID_TEST_VFS_H
                #include <stdint.h>
                #define O_WRONLY 0x0001
                #define O_CREAT 0x0100
                #define O_TRUNC 0x0200
                int vfs_open(const char *path, uint32_t flags);
                int vfs_close(int fd);
                int vfs_write(int fd, const void *buffer, uint32_t count);
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

                static FILE *aot_output;

                int vfs_open(const char *path, uint32_t flags) {
                  (void)flags;
                  if (aot_output != NULL)
                    return -1;
                  aot_output = fopen(path, "wb");
                  return aot_output != NULL ? 3 : -1;
                }

                int vfs_write(int fd, const void *buffer, uint32_t count) {
                  size_t written;
                  if (fd != 3 || aot_output == NULL)
                    return -1;
                  written = fwrite(buffer, 1u, (size_t)count, aot_output);
                  return written == (size_t)count ? (int)written : -1;
                }

                int vfs_close(int fd) {
                  int result;
                  if (fd != 3 || aot_output == NULL)
                    return -1;
                  result = fclose(aot_output);
                  aot_output = NULL;
                  return result == 0 ? 0 : -1;
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

                static cc_state_t *new_compiler_state(int jit_mode) {
                  cc_state_t *cc = (cc_state_t *)calloc(1u, sizeof(*cc));
                  if (cc == NULL)
                    return NULL;
                  cc->code = (uint8_t *)calloc(1u, CC_MAX_CODE);
                  cc->data = (uint8_t *)calloc(1u, CC_MAX_DATA);
                  if (cc->code == NULL || cc->data == NULL)
                    return NULL;
                  cc->code_base =
                      jit_mode ? CC_JIT_CODE_BASE : CC_AOT_CODE_BASE;
                  cc->data_base =
                      jit_mode ? CC_JIT_DATA_BASE : CC_AOT_DATA_BASE;
                  cc->jit_mode = jit_mode;
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
                  int repl_patch_rollback_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--repl-patch-rollback") == 0;
                  int recovery_mode =
                      argc == 5 && strcmp(argv[4], "--recover") == 0;
                  int same_state_recovery_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--recover-same-state") == 0;
                  int recover_after_success_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--recover-after-success") == 0;
                  int double_failure_recovery_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--recover-two-failures") == 0;
                  int aot_mode =
                      argc == 5 && strcmp(argv[4], "--aot") == 0;
                  if (argc == 3 &&
                      strcmp(argv[1], "--check-number-boundary") == 0)
                    return check_numeric_token_boundary(argv[2]);
                  if (argc != 4 && !repl_mode && !repl_rollback_mode &&
                      !repl_patch_rollback_mode &&
                      !recovery_mode && !same_state_recovery_mode &&
                      !recover_after_success_mode &&
                      !double_failure_recovery_mode &&
                      !aot_mode)
                    return 64;
                  source = read_source(argv[1]);
                  cc = new_compiler_state(aot_mode ? 0 : 1);
                  if (source == NULL || cc == NULL)
                    return 65;
                  if (recover_after_success_mode ||
                      double_failure_recovery_mode) {
                    char *failing_source = source;
                    char *retry_source;
                    while (*failing_source != 0 &&
                           (unsigned char)*failing_source != 30u)
                      failing_source++;
                    if ((unsigned char)*failing_source != 30u)
                      return 68;
                    *failing_source = 0;
                    failing_source++;
                    retry_source = failing_source;
                    while (*retry_source != 0 &&
                           (unsigned char)*retry_source != 30u)
                      retry_source++;
                    if ((unsigned char)*retry_source != 30u)
                      return 68;
                    *retry_source = 0;
                    retry_source++;
                    cc_lex_init(cc, source);
                    cc_parse_program(cc);
                    if (double_failure_recovery_mode) {
                      if (!cc->error)
                        return 69;
                      (void)fprintf(stderr, "%s", cc->error_msg);
                      cc->error = 0;
                      cc->error_msg[0] = 0;
                      cc_lex_init(cc, failing_source);
                      cc_parse_program(cc);
                      if (!cc->error)
                        return 69;
                      (void)fprintf(stderr, "%s", cc->error_msg);
                      cc->error = 0;
                      cc->error_msg[0] = 0;
                    } else {
                      if (cc->error)
                        return 70;
                      cc_lex_init(cc, failing_source);
                      cc_parse_program(cc);
                      if (!cc->error)
                        return 69;
                      cc->error = 0;
                      cc->error_msg[0] = 0;
                    }
                    cc_lex_init(cc, retry_source);
                    cc_parse_program(cc);
                  } else if (recovery_mode || same_state_recovery_mode) {
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
                    if (same_state_recovery_mode) {
                      cc->error = 0;
                      cc->error_msg[0] = 0;
                    } else {
                      cc = new_compiler_state(1);
                      if (cc == NULL)
                        return 66;
                    }
                    cc_lex_init(cc, retry_source);
                    cc_parse_program(cc);
                  } else if (repl_mode || repl_rollback_mode ||
                             repl_patch_rollback_mode) {
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
                      if ((repl_rollback_mode || repl_patch_rollback_mode) &&
                          unit_index == 0) {
                        if (cc->error)
                          break;
                        checkpoint.code_committed = cc->code_pos;
                        checkpoint.data_committed = cc->data_pos;
                        checkpoint.sym_committed = cc->sym_count;
                        cc_repl_checkpoint_structs(&checkpoint);
                        checkpoint.typedef_committed = cc->typedef_count;
                        checkpoint.raw_function_pointer_signature_committed =
                            cc->raw_function_pointer_signature_count;
                        checkpoint.patch_committed = cc->patch_count;
                      } else if ((repl_rollback_mode ||
                                  repl_patch_rollback_mode) &&
                                 unit_index == 1) {
                        if (repl_patch_rollback_mode) {
                          if (!cc->error ||
                              cc->patch_count <= checkpoint.patch_committed)
                            return 73;
                        } else if (!cc->error &&
                                   cc->patch_count <=
                                       checkpoint.patch_committed) {
                          return 73;
                        }
                        cc->code_pos = checkpoint.code_committed;
                        cc->data_pos = checkpoint.data_committed;
                        cc->sym_count = checkpoint.sym_committed;
                        cc_repl_restore_structs(&checkpoint);
                        cc->typedef_count = checkpoint.typedef_committed;
                        cc->raw_function_pointer_signature_count =
                            checkpoint.raw_function_pointer_signature_committed;
                        cc->patch_count = checkpoint.patch_committed;
                        cc->has_entry = 0;
                        cc->entry_offset = 0;
                        cc->local_offset = 0;
                        cc->max_local_offset = 0;
                        cc->param_count = 0;
                        cc->control_depth = 0;
                        cc->statement_depth = 0;
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
                  if (aot_mode) {
                    if (cc_write_elf(cc, argv[2]) != 0)
                      return 67;
                  } else if (!write_output(argv[2], cc->code, cc->code_pos) ||
                             !write_output(argv[3], cc->data, cc->data_pos)) {
                    return 67;
                  }
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
                str(ELF_SOURCE),
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

    def _compile(self, root, source, *, repl=False, aot=False):
        source_path = root / "fixture.cc"
        code_path = root / ("program.elf" if aot else "code.bin")
        data_path = root / "data.bin"
        source_path.write_text(textwrap.dedent(source), encoding="utf-8")
        command = [
            str(self.driver),
            str(source_path),
            str(code_path),
            str(data_path),
        ]
        if aot:
            command.append("--aot")
        elif repl:
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

    def _compile_repl_after_struct_failure(
        self, root, units, *, require_patch=False
    ):
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
                (
                    "--repl-patch-rollback"
                    if require_patch
                    else "--repl-rollback"
                ),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _compile_after_failure(
        self, root, failing_source, retry_source, *, same_state=False
    ):
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
                "--recover-same-state" if same_state else "--recover",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _compile_after_intermediate_failure(
        self, root, successful_source, failing_source, retry_source
    ):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(
            textwrap.dedent(successful_source)
            + "\x1e"
            + textwrap.dedent(failing_source)
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
                "--recover-after-success",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )
        return result, code_path, data_path

    def _compile_after_two_failures(
        self, root, first_failure, second_failure, retry_source
    ):
        source_path = root / "fixture.cc"
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        source_path.write_text(
            textwrap.dedent(first_failure)
            + "\x1e"
            + textwrap.dedent(second_failure)
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
                "--recover-two-failures",
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

    def _extract_aot_segments(self, root, elf_path, entry_offset):
        image = elf_path.read_bytes()
        header_format = "<16sHHIIIIIHHHHHH"
        program_header_format = "<IIIIIIII"
        self.assertGreaterEqual(len(image), struct.calcsize(header_format))
        header = struct.unpack_from(header_format, image)
        self.assertEqual(header[0][:4], b"\x7fELF")
        self.assertEqual(header[1:3], (2, 3))
        self.assertEqual(header[4], 0x01100000 + entry_offset)
        program_header_offset = header[5]
        program_header_size = header[9]
        program_header_count = header[10]
        self.assertEqual(program_header_size, 32)
        self.assertIn(program_header_count, (1, 2))

        segments = {}
        for index in range(program_header_count):
            offset = program_header_offset + index * program_header_size
            self.assertLessEqual(offset + program_header_size, len(image))
            program_header = struct.unpack_from(
                program_header_format, image, offset
            )
            self.assertEqual(program_header[0], 1)
            file_offset = program_header[1]
            virtual_address = program_header[2]
            file_size = program_header[4]
            memory_size = program_header[5]
            self.assertGreaterEqual(memory_size, file_size)
            self.assertLessEqual(file_offset + file_size, len(image))
            segments[virtual_address] = image[
                file_offset : file_offset + file_size
            ]

        self.assertIn(0x01100000, segments)
        code_path = root / "code.bin"
        data_path = root / "data.bin"
        code_path.write_bytes(segments[0x01100000])
        data_path.write_bytes(segments.get(0x01200000, b""))
        return code_path, data_path

    def _compile_and_run(self, source, repl=False, *, aot=False):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-runtime-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            compile_result, code_path, _data = self._compile(
                root, source, repl=repl, aot=aot
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            entry_offset = int(compile_result.stdout.strip())
            if aot:
                self._extract_aot_segments(root, code_path, entry_offset)
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

    def test_simd_parameters_and_returns_survive_nested_calls(self):
        result = self._compile_and_run(
            """
            int float_marker;
            int double_marker;

            float4 merge_float4(float4 left, int marker, float4 right) {
              float_marker = marker;
              return left + right;
            }

            float4 merge_float4_three(float4 first, float4 second,
                                      float4 third) {
              return merge_float4(
                  merge_float4(first, 7, second), 11, third);
            }

            double2 merge_double2(double2 left, int marker, double2 right) {
              double_marker = marker;
              return left + right;
            }

            int main() {
              float4 first = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 second = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 third = {9.0f, 10.0f, 11.0f, 12.0f};
              double2 wide_first = {1.5, 2.5};
              double2 wide_second = {3.0, 4.0};
              float4 floats;
              double2 doubles;

              floats = merge_float4_three(first, second, third);
              doubles = merge_double2(wide_first, 13, wide_second);

              if (float_marker != 11 || double_marker != 13) return 1;
              if (floats.x != 15.0f || floats.y != 18.0f) return 2;
              if (floats.z != 21.0f || floats.w != 24.0f) return 3;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mixed_scalar_and_simd_call_keeps_order_and_raw_words(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-mixed-simd-call-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, code_path, _data = self._compile(
                root,
                """
                int sequence;
                int order_error;

                void advance(int expected) {
                  if (sequence != expected) order_error = 1;
                  sequence += 1;
                }

                float4 next_float4_first() {
                  float4 value = {1.0f, -2.0f, 3.0f, -4.0f};
                  advance(0);
                  return value;
                }

                double next_double() {
                  advance(1);
                  return 1.5;
                }

                double2 next_double2() {
                  double2 value = {3.25, -6.5};
                  advance(2);
                  return value;
                }

                int next_int() {
                  advance(3);
                  return 0x13579bdf;
                }

                float4 next_float4_last() {
                  float4 value = {9.0f, 10.0f, 11.0f, 12.0f};
                  advance(4);
                  return value;
                }

                int inspect(float4 first, double scalar, double2 packed,
                            int word, float4 last) {
                  float lane = first.x;
                  if (*(int *)&lane != 0x3f800000) return 1;
                  lane = first.y;
                  if (*(int *)&lane != (int)0xc0000000) return 2;
                  lane = first.z;
                  if (*(int *)&lane != 0x40400000) return 3;
                  lane = first.w;
                  if (*(int *)&lane != (int)0xc0800000) return 4;

                  int *bits = (int *)&scalar;
                  if (bits[0] != 0 || bits[1] != 0x3ff80000) return 5;
                  double wide_lane = packed.x;
                  bits = (int *)&wide_lane;
                  if (bits[0] != 0 || bits[1] != 0x400a0000) return 6;
                  wide_lane = packed.y;
                  bits = (int *)&wide_lane;
                  if (bits[0] != 0 ||
                      bits[1] != (int)0xc01a0000) return 7;
                  if (word != 0x13579bdf) return 8;

                  lane = last.x;
                  if (*(int *)&lane != 0x41100000) return 9;
                  lane = last.w;
                  if (*(int *)&lane != 0x41400000) return 10;
                  if (sequence != 5 || order_error != 0) return 11;
                  return 0;
                }

                int main() {
                  sequence = 0;
                  order_error = 0;
                  return inspect(
                      next_float4_first(), next_double(), next_double2(),
                      next_int(), next_float4_last());
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            entry_offset = int(result.stdout.strip())
            main_code = code_path.read_bytes()[entry_offset:]
            self.assertIn(
                b"\xe8",
                main_code,
                "expected a direct mixed-width call",
            )
            cleanup_sites = [
                offset
                for offset in range(len(main_code) - 7)
                if main_code[offset] == 0xE8
                and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x3c"
            ]
            self.assertEqual(
                len(cleanup_sites),
                1,
                "expected the mixed call to release 60 argument bytes",
            )
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_simd_calls_clean_the_complete_outgoing_area(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-call-cleanup-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                float4 merge_float4(float4 left, int marker, float4 right) {
                  if (marker == 0) return left;
                  return left + right;
                }

                double2 merge_double2(double2 left, int marker,
                                      double2 right) {
                  if (marker == 0) return left;
                  return left + right;
                }

                int main() {
                  float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
                  double2 wide_left = {1.0, 2.0};
                  double2 wide_right = {3.0, 4.0};
                  float4 floats;
                  double2 doubles;
                  floats = merge_float4(left, 7, right);
                  doubles = merge_double2(wide_left, 9, wide_right);
                  return (int)floats.x + (int)doubles.x;
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            code = code_path.read_bytes()
            main_code = code[entry_offset:]

        self.assertIn(
            b"\x0f\x10\x85\x08\x00\x00\x00",
            code,
            "expected the first vector at EBP + 8",
        )
        self.assertIn(
            b"\x8b\x85\x18\x00\x00\x00",
            code,
            "expected the scalar marker after the first vector",
        )
        self.assertIn(
            b"\x0f\x10\x85\x1c\x00\x00\x00",
            code,
            "expected the second vector after the scalar marker",
        )

        cleanup_sites = [
            offset
            for offset in range(len(main_code) - 7)
            if main_code[offset] == 0xE8
            and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x24"
        ]
        self.assertEqual(
            len(cleanup_sites),
            2,
            "expected both vector calls to release 36 argument bytes",
        )

    def test_active_simd_header_declarations_accept_vector_parameters(self):
        header = (REPO_ROOT / "kernel" / "cpu" / "simd_intrin.h").read_text(
            encoding="utf-8"
        )
        declarations = "\n".join(
            line for line in header.splitlines() if not line.startswith("#")
        )
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-header-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary), declarations + "\nint main() { return 0; }\n"
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_methods_keep_self_before_complete_vector_slots(self):
        result = self._compile_and_run(
            """
            int seen_direct;
            int seen_pointer;

            class Mixer {
              float4 Blend(float4 left, int marker, float4 right) {
                seen_direct = marker;
                return left + right;
              }

              double2 BlendWide(double2 left, int marker, double2 right) {
                seen_pointer = marker;
                return left + right;
              }
            };

            int main() {
              Mixer mixer;
              Mixer *pointer = &mixer;
              float4 float_left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 float_right = {5.0f, 6.0f, 7.0f, 8.0f};
              double2 double_left = {1.5, 2.5};
              double2 double_right = {3.0, 4.0};
              float4 floats;
              double2 doubles;

              floats = mixer.Blend(float_left, 17, float_right);
              doubles = pointer->BlendWide(
                  double_left, 19, double_right);

              if (seen_direct != 17 || seen_pointer != 19) return 1;
              if (floats.x != 6.0f || floats.w != 12.0f) return 2;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_void_simd_method_statements_use_the_shared_layout(self):
        result = self._compile_and_run(
            """
            int observations;

            class Observer {
              void Record(float4 floats, int marker, double2 doubles) {
                if (floats.x == 1.0f && floats.w == 4.0f &&
                    doubles.x == 5.0 && doubles.y == 7.0) {
                  observations += marker;
                }
              }
            };

            int main() {
              Observer observer;
              Observer *pointer = &observer;
              float4 floats = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 doubles = {5.0, 7.0};
              observer.Record(floats, 11, doubles);
              pointer->Record(floats, 13, doubles);
              return observations == 24 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_prototypes_preserve_results_before_definitions(self):
        result = self._compile_and_run(
            """
            float4 add_float4(float4 left, float4 right);
            double2 add_double2(double2 left, double2 right);

            int main() {
              float4 float_left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 float_right = {5.0f, 6.0f, 7.0f, 8.0f};
              double2 double_left = {1.5, 2.5};
              double2 double_right = {3.0, 4.0};
              float4 floats;
              double2 doubles;
              floats = add_float4(float_left, float_right);
              doubles = add_double2(double_left, double_right);
              if (floats.x != 6.0f || floats.w != 12.0f) return 1;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 2;
              return 0;
            }

            float4 add_float4(float4 left, float4 right) {
              return left + right;
            }

            double2 add_double2(double2 left, double2 right) {
              return left + right;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_parameter_updates_change_only_the_callee_copy(self):
        result = self._compile_and_run(
            """
            float4 increment_copy(float4 value) {
              value++;
              return value;
            }

            double2 decrement_copy(double2 value) {
              --value;
              return value;
            }

            int main() {
              float4 original = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 wide_original = {5.0, 7.0};
              float4 changed;
              double2 wide_changed;
              changed = increment_copy(original);
              wide_changed = decrement_copy(wide_original);
              if (original.x != 1.0f || original.w != 4.0f) return 1;
              if (wide_original.x != 5.0 || wide_original.y != 7.0) return 2;
              if (changed.x != 2.0f || changed.w != 5.0f) return 3;
              if (wide_changed.x != 4.0 || wide_changed.y != 6.0) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_const_simd_parameters_reject_mutation_before_recovery(self):
        cases = (
            (
                "float4 assignment",
                """
                int invalid(const float4 value, float4 other) {
                  value = other;
                  return 0;
                }
                """,
                "SIMD assignment requires a modifiable whole-vector lvalue",
            ),
            (
                "double2 update",
                """
                int invalid(const double2 value) {
                  value++;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
        )
        retry_source = """
            float4 identity(float4 value) {
              return value;
            }

            int main() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 result;
              result = identity(value);
              return result.y == 2.0f ? 0 : 1;
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-simd-parameter-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    0,
                    runtime.stdout + runtime.stderr,
                )

    def test_aot_zero_data_program_keeps_advertised_code_offset(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-zero-data-aot-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, elf_path, _data = self._compile(
                root,
                """
                int main() {
                  return 17;
                }
                """,
                aot=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            image = elf_path.read_bytes()
            header = struct.unpack_from("<16sHHIIIIIHHHHHH", image)
            self.assertEqual(header[5], 52)
            self.assertEqual(header[9], 32)
            self.assertEqual(header[10], 1)
            program_header = struct.unpack_from("<IIIIIIII", image, 52)
            self.assertEqual(program_header[0], 1)
            self.assertEqual(program_header[1], 0x80)
            self.assertEqual(program_header[2], 0x01100000)
            self._extract_aot_segments(root, elf_path, entry_offset)
            runtime = self._run_i386(root, entry_offset)
            self.assertEqual(
                runtime.returncode,
                17,
                runtime.stdout + runtime.stderr,
            )

    def test_aot_initialized_data_program_keeps_two_load_segments(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-data-aot-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, elf_path, _data = self._compile(
                root,
                """
                int value = 17;

                int main() {
                  return value;
                }
                """,
                aot=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            image = elf_path.read_bytes()
            header = struct.unpack_from("<16sHHIIIIIHHHHHH", image)
            self.assertEqual(header[5], 52)
            self.assertEqual(header[9], 32)
            self.assertEqual(header[10], 2)
            self._extract_aot_segments(root, elf_path, entry_offset)
            runtime = self._run_i386(root, entry_offset)
            self.assertEqual(
                runtime.returncode,
                17,
                runtime.stdout + runtime.stderr,
            )

    def test_simd_calls_execute_through_private_aot(self):
        result = self._compile_and_run(
            """
            float4 add_float4(float4 left, int marker, float4 right) {
              if (marker != 7) return left;
              return left + right;
            }

            double2 add_double2(double2 left, int marker, double2 right) {
              if (marker != 9) return left;
              return left + right;
            }

            int main() {
              float4 float_left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 float_right = {5.0f, 6.0f, 7.0f, 8.0f};
              double2 double_left = {1.5, 2.5};
              double2 double_right = {3.0, 4.0};
              float4 floats;
              double2 doubles;
              floats = add_float4(float_left, 7, float_right);
              doubles = add_double2(double_left, 9, double_right);
              if (floats.z != 10.0f || floats.w != 12.0f) return 1;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 2;
              return 0;
            }
            """,
            aot=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

    def test_fixed_integer_parameters_preserve_object_pointer_bits(self):
        result = self._compile_and_run(
            """
            int write_outputs(int value_out_address, int text_out_address) {
              int *value_out = value_out_address;
              char *text_out = text_out_address;
              *value_out = 37;
              text_out[0] = 'O';
              text_out[1] = 'S';
              text_out[2] = 0;
              return 0;
            }

            int main() {
              int value = 0;
              char text[3];
              if (write_outputs(&value, text) != 0) return 1;
              if (value != 37) return 2;
              if (text[0] != 'O' || text[1] != 'S' || text[2] != 0) {
                return 3;
              }
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_unsigned_parameter_preserves_object_pointer_bits(self):
        result = self._compile_and_run(
            """
            int write_output(unsigned int output_address) {
              int *output = output_address;
              *output = 41;
              return 0;
            }

            int main() {
              int value = 0;
              if (write_output(&value) != 0) return 1;
              return value == 41 ? 0 : 2;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pointer_to_narrow_parameter_is_rejected_before_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-pointer-word-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume_byte(char value) {
                }

                int invalid_call() {
                  int value = 0;
                  consume_byte(&value);
                  return 0;
                }
                """,
                """
                int read_word(int address) {
                  int *pointer = address;
                  return *pointer;
                }

                int main() {
                  int value = 43;
                  return read_word(&value) == 43 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_pointer_to_floating_parameter_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-pointer-float-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void consume_float(float value) {
                }

                int main() {
                  int value = 0;
                  consume_float(&value);
                  return 0;
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "cdecl argument type does not match fixed parameter",
            result.stderr,
        )

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

    def test_named_function_pointer_coercion_and_result_run_in_jit_and_aot(self):
        source = """
            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            double echo_double(double value) {
              return value;
            }

            int main() {
              int (*combine_callback)(double, int) = combine;
              double (*echo_callback)(double) = echo_double;
              int combined = combine_callback(3, 4.75f);
              double echoed = echo_callback(5);
              if (combined != 34) return 1;
              return echoed == 5.0 ? 0 : 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_raw_global_callbacks_match_active_spelling_in_jit_and_aot(self):
        panic_source = (
            REPO_ROOT / "kernel" / "core" / "panic.cc"
        ).read_text(encoding="utf-8")
        panic_header = (
            REPO_ROOT / "kernel" / "core" / "panic.h"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "static void (*panic_print)(const char*) = print;",
            panic_source,
        )
        self.assertIn(
            "void panic_set_output(void (*print_fn)(const char*), "
            "void (*putchar_fn)(char));",
            panic_header,
        )

        source = """
            int ready_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int observed_character;
            void observe(const char *text) {
              observed_character = *text;
            }

            static int (*empty_callback)(double, int) = 0;
            static int (*ready_callback)(double, int) = ready_target;
            static int (*later_callback)(double, int) = later_target;
            static void (*panic_style_output)(const char*) = observe;

            int later_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int main() {
              int result;
              if (empty_callback != 0) return 1;
              empty_callback = ready_target;
              result = empty_callback(3, 4.75f);
              empty_callback = 0;
              if (empty_callback != 0) return 2;
              panic_style_output("A");
              if (observed_character != 65) return 3;
              result += ready_callback(2, 5.5f);
              result += later_callback(1, 6.25f);
              return result;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    75,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_initializers_accept_explicit_function_addresses(self):
        source = """
            int ready_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int later_target(double value, int marker);
            static int (*ready_callback)(double, int) = (&ready_target);
            static int (*later_callback)(double, int) = &(later_target);

            int later_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int main() {
              int (*local_callback)(double, int) = &ready_target;
              local_callback = &later_target;
              return local_callback(3, 4.75f) +
                     ready_callback(2, 5.5f) +
                     later_callback(1, 6.25f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 75, result.stdout + result.stderr
                )

    def test_grouped_runtime_function_addresses_run_in_jit_and_aot(self):
        source = """
            int ready_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int later_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int main() {
              int marker = 7;
              int *marker_address = &(marker);
              int (*callback)(double, int) = &(ready_target);
              if (*marker_address != 7) return 1;
              int result = callback(3, 4.75f);
              callback = &((later_target));
              return result + callback(2, 5.5f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 59, result.stdout + result.stderr
                )

    def test_explicit_function_address_initializer_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-addressed-callback-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                double wrong(int value) { return value; }
                int invalid(void) {
                  int (*callback)(int) = &((wrong));
                  return callback(1);
                }
                int main() { return invalid(); }
                """,
                """
                int right(double value) { return (int)value; }
                int main() {
                  int (*callback)(double) = &(right);
                  return callback(9);
                }
                """,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "function-pointer initializer result does not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_raw_callback_parameter_keeps_mixed_width_slots_in_jit_and_aot(self):
        source = """
            int inspect(const uint8_t *entry, double scale, int marker) {
              if (entry == 0 || *entry != 7) return 1;
              return (int)(scale * 10.0) + marker;
            }

            int invoke(int (*callback)(const uint8_t *, double, int),
                       const uint8_t *entry) {
              return callback(entry, 4, 5.75f);
            }

            int main() {
              uint8_t entry = 7;
              return invoke(inspect, &entry);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    45,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_keeps_variadic_promotions_in_jit_and_aot(self):
        source = """
            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int invoke(int (*callback)(double, ...)) {
              return callback(6, 2.5f);
            }

            int main() {
              return invoke(inspect);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    6,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_keeps_record_identity_in_jit_and_aot(self):
        source = """
            struct Entry { int value; };

            int inspect(struct Entry *entry) {
              return entry->value;
            }

            int invoke(int (*callback)(struct Entry *),
                       struct Entry *entry) {
              return callback(entry);
            }

            int main() {
              struct Entry entry;
              entry.value = 13;
              return invoke(inspect, &entry);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    13,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_prototype_matches_definition(self):
        source = """
            int invoke(int (*callback)(double, int));

            int target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int invoke(int (*callback)(double, int)) {
              return callback(3, 4.75f);
            }

            int main() {
              return invoke(target);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    34,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_prototype_mismatch_recovers_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-raw-callback-prototype-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invoke(int (*callback)(int));
                int invoke(int (*callback)(double)) {
                  return callback(9);
                }
                int main() { return 0; }
                """,
                """
                int identity(int value) { return value; }
                int invoke(int (*callback)(int));
                int invoke(int (*callback)(int)) {
                  return callback(9);
                }
                int main() { return invoke(identity); }
                """,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "function declaration does not match prior declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )
    def test_raw_callback_parameter_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                double wrong(int value) { return value; }
                int invoke(int (*callback)(int)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "parameters",
                """
                int wrong(int value) { return value; }
                int invoke(int (*callback)(double)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                int wrong(struct Two *value) { return value->value; }
                int invoke(int (*callback)(struct One *)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "variadic boundary",
                """
                int wrong(double fixed) { return (int)fixed; }
                int invoke(int (*callback)(double, ...)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "too few arguments",
                """
                int invoke(int (*callback)(int, int)) {
                  return callback(1);
                }
                int main() { return 0; }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many arguments",
                """
                int invoke(int (*callback)(int)) {
                  return callback(1, 2);
                }
                int main() { return 0; }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            int add(int left, int right) { return left + right; }
            int invoke(int (*callback)(int, int)) {
              return callback(4, 5);
            }
            int main() { return invoke(add); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-parameter-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_raw_callback_signature_capacity_recovers_same_state(self):
        declarations = []
        for arity in range(33):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )
        failing_source = "\n".join(declarations) + "\nint main() { return 0; }"
        retry_source = """
            int identity(int value) { return value; }
            int invoke(int (*callback)(int)) { return callback(9); }
            int main() { return invoke(identity); }
        """

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-raw-callback-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                failing_source,
                retry_source,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "too many raw function-pointer signatures",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_repl_rolls_back_a_failed_raw_callback_signature(self):
        capacity_declarations = []
        for arity in range(32):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            capacity_declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-raw-callback-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "int poisoned(int (*callback)(double)) { "
                        "return missing_value; }",
                        "\n".join(capacity_declarations),
                        "int identity(int value) { return value; } "
                        "int invoke(int (*callback)(int)) { "
                        "return callback(9); } "
                        "int main() { return invoke(identity); }",
                    ),
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )
            runtime_source = (KERNEL_LANG / "cupidc.cc").read_text(
                encoding="utf-8"
            )
            self.assertIn(
                "repl_state.cc->raw_function_pointer_signature_count =",
                runtime_source,
            )
            self.assertIn(
                "repl_state.raw_function_pointer_signature_committed",
                runtime_source,
            )

    def test_repl_rolls_back_raw_signature_after_unresolved_patch(self):
        capacity_declarations = []
        for arity in range(32):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            capacity_declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-raw-callback-patch-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int missing(void);",
                        "{ int (*poisoned)(double) = 0; missing(); }",
                        "\n".join(capacity_declarations),
                        "int identity(int value) { return value; } "
                        "int invoke(int (*callback)(int)) { "
                        "return callback(9); } "
                        "int main() { return invoke(identity); }",
                    ),
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_repl_keeps_raw_callback_globals_for_later_units(self):
        result = self._compile_repl_and_run(
            (
                """
                int ready_target(double value, int marker) {
                  return (int)(value * 10.0) + marker;
                }
                """,
                "static int (*empty_callback)(double, int) = 0;",
                "static int (*ready_callback)(double, int) = ready_target;",
                "int later_target(double value, int marker);",
                "static int (*later_callback)(double, int) = later_target;",
                """
                int later_target(double value, int marker) {
                  return (int)(value * 10.0) + marker;
                }
                """,
                """
                int main() {
                  int result;
                  if (empty_callback != 0) return 1;
                  empty_callback = ready_target;
                  result = empty_callback(3, 4.75f);
                  empty_callback = 0;
                  if (empty_callback != 0) return 2;
                  result += ready_callback(2, 5.5f);
                  result += later_callback(1, 6.25f);
                  return result;
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 75, result.stdout + result.stderr)

    def test_raw_global_callback_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                double wrong(int value) { return value; }
                static int (*callback)(int) = wrong;
                int main() { return callback(1); }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "parameters",
                """
                int wrong(int value) { return value; }
                static int (*callback)(double) = wrong;
                int main() { return callback(1); }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
        )
        retry_source = """
            int right(double value) { return (int)value; }
            static int (*callback)(double) = right;
            int main() { return callback(9); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-global-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_named_function_pointer_record_pointer_result_keeps_identity(self):
        source = """
            struct Pair {
              int left;
              int right;
            };

            struct Pair values[2];

            struct Pair *select_pair(int index) {
              return &values[index];
            }

            int main() {
              struct Pair *(*callback)(int) = select_pair;
              values[0].left = 11;
              values[1].left = 33;
              values[1].right = 44;
              if (callback(1)->right != 44) return 1;
              return callback(0)[1].left == 33 ? 0 : 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_named_function_pointer_uses_typed_cdecl_slot_widths(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-layout-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, code_path, _data = self._compile(
                root,
                """
                int combine(double first, int second) {
                  return (int)first * 10 + second;
                }

                int main() {
                  int (*callback)(double, int) = combine;
                  return callback(3, 4.75f);
                }
                """,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            code = code_path.read_bytes()
            self.assertIn(b"\xff\xd0\x83\xc4\x0c", code)

    def test_named_function_pointer_matches_active_callback_widths(self):
        result = self._compile_and_run(
            """
            int inspect(const uint8_t *entry, uint32_t length, void *context) {
              if (entry == 0 || context != 0) return 1;
              return length;
            }

            int main() {
              uint8_t entry = 1;
              uint8_t length = 7;
              int (*callback)(const uint8_t *, uint32_t, void *) = inspect;
              return callback(&entry, length, 0);
            }
            """
        )
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_function_pointer_typedef_automatic_local_keeps_mixed_width_slots(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int main() {
              callback_t callback = combine;
              return callback(3, 4.75f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 34, result.stdout + result.stderr
                )

    def test_each_function_pointer_typedef_automatic_declarator_keeps_its_signature(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int subtract(double first, int second) {
              return (int)first * 10 - second;
            }

            int main() {
              callback_t first = combine, second = subtract;
              return first(3, 4.75f) + second(8, 2.25f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 112, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_method_parameter_keeps_mixed_width_slots(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            class Invoker {
              int Invoke(callback_t callback) {
                return callback(3, 4.75f);
              }
            };

            int main() {
              Invoker invoker;
              return invoker.Invoke(combine);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 34, result.stdout + result.stderr
                )

    def test_each_function_pointer_typedef_method_parameter_keeps_its_signature(self):
        source = """
            typedef int (*integer_cb)(int value);
            typedef int (*mixed_cb)(double first, int second);

            int identity(int value) {
              return value;
            }

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            class Invoker {
              int Invoke(integer_cb first, mixed_cb second) {
                return first(3) + second(4, 5.75f);
              }
            };

            int main() {
              Invoker invoker;
              return invoker.Invoke(identity, combine);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 48, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_automatic_and_method_calls_clean_exact_slots(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(double first, int second);
                int combine(double first, int second) { return second; }
                int main() {
                  callback_t callback = combine;
                  return callback(3, 4.75f);
                }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(double first, int second);
                int combine(double first, int second) { return second; }
                class Invoker {
                  int Invoke(callback_t callback) {
                    return callback(3, 4.75f);
                  }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(combine);
                }
                """,
            ),
        )
        for label, source in cases:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot), tempfile.TemporaryDirectory(
                    prefix="private-cupidc-callback-typedef-cleanup-",
                    ignore_cleanup_errors=True,
                ) as temporary:
                    result, code_path, _data = self._compile(
                        Path(temporary), source, aot=aot
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    self.assertEqual(
                        code_path.read_bytes().count(b"\xff\xd0\x83\xc4\x0c"),
                        1,
                    )

    def test_function_pointer_typedef_automatic_local_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*blend_cb)(float4 left, double marker,
                                       float4 right);

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            int main() {
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              blend_cb callback = blend;
              result = callback(left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_method_parameter_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*blend_cb)(float4 left, double marker,
                                       float4 right);

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            class Invoker {
              float4 Invoke(blend_cb callback, float4 left,
                            double marker, float4 right) {
                return callback(left, marker, right);
              }
            };

            int main() {
              Invoker invoker;
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              result = invoker.Invoke(blend, left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_automatic_and_method_forms_keep_null_and_erasure(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(int value);
                double wrong(double value) { return value; }
                int main() {
                  callback_t empty = sizeof(int) - 4;
                  callback_t erased = (void *)wrong;
                  if (empty != 0) return 1;
                  return erased != 0 ? 0 : 2;
                }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(int value);
                double wrong(double value) { return value; }
                class Inspector {
                  int Check(callback_t callback) {
                    return callback == 0 ? 3 : 5;
                  }
                };
                int main() {
                  Inspector inspector;
                  if (inspector.Check(sizeof(int) - 4) != 3) return 1;
                  return inspector.Check((void *)wrong) == 5 ? 0 : 2;
                }
                """,
            ),
        )
        for label, source in cases:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )

    def test_function_pointer_typedef_automatic_and_method_forms_fix_later_targets(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(double value);
                int main() {
                  callback_t callback = later;
                  return callback(6);
                }
                int later(double value) { return (int)value; }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(double value);
                class Invoker {
                  int Invoke(callback_t callback) {
                    return callback(6);
                  }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(later);
                }
                int later(double value) { return (int)value; }
                """,
            ),
        )
        for label, source in cases:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode, 6, result.stdout + result.stderr
                    )

    def test_function_pointer_typedef_automatic_initializer_mismatches_recover(self):
        cases = (
            (
                "result",
                """
                typedef int (*callback_t)(int value);
                double wrong(int value) { return value; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "parameter",
                """
                typedef int (*callback_t)(double value);
                int wrong(int value) { return value; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                typedef int (*callback_t)(struct One *value);
                int wrong(struct Two *value) { return value->value; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "variadic boundary",
                """
                typedef int (*callback_t)(double fixed, ...);
                int wrong(double fixed) { return (int)fixed; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            int main() {
              callback_t callback = add;
              return callback(4, 5);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-local-mismatch-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_function_pointer_typedef_automatic_call_arity_recovers(self):
        cases = (
            (
                "too few",
                """
                typedef int (*callback_t)(int left, int right);
                int add(int left, int right) { return left + right; }
                int main() {
                  callback_t callback = add;
                  return callback(1);
                }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many",
                """
                typedef int (*callback_t)(int value);
                int identity(int value) { return value; }
                int main() {
                  callback_t callback = identity;
                  return callback(1, 2);
                }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            int main() {
              callback_t callback = add;
              return callback(4, 5);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-local-arity-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_function_pointer_typedef_method_argument_mismatches_recover(self):
        cases = (
            (
                "result",
                """
                typedef int (*callback_t)(int value);
                double wrong(int value) { return value; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "parameter",
                """
                typedef int (*callback_t)(double value);
                int wrong(int value) { return value; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                typedef int (*callback_t)(struct One *value);
                int wrong(struct Two *value) { return value->value; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "variadic boundary",
                """
                typedef int (*callback_t)(double fixed, ...);
                int wrong(double fixed) { return (int)fixed; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            int identity(int value) { return value; }
            class Invoker {
              int Invoke(callback_t callback) { return callback(9); }
            };
            int main() {
              Invoker invoker;
              return invoker.Invoke(identity);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-method-mismatch-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_function_pointer_typedef_method_call_arity_recovers(self):
        cases = (
            (
                "too few",
                """
                typedef int (*callback_t)(int left, int right);
                class Invoker {
                  int Invoke(callback_t callback) { return callback(1); }
                };
                int main() { return 0; }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many",
                """
                typedef int (*callback_t)(int value);
                class Invoker {
                  int Invoke(callback_t callback) { return callback(1, 2); }
                };
                int main() { return 0; }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            class Invoker {
              int Invoke(callback_t callback) { return callback(4, 5); }
            };
            int main() {
              Invoker invoker;
              return invoker.Invoke(add);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-method-arity-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_function_pointer_typedef_automatic_and_method_later_mismatches_recover(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(double value);
                int main() {
                  callback_t callback = later;
                  return callback(6);
                }
                int later(int value) { return value; }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(double value);
                class Invoker {
                  int Invoke(callback_t callback) { return callback(6); }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(later);
                }
                int later(int value) { return value; }
                """,
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            int later(int value) { return value; }
            int main() {
              callback_t callback = later;
              return callback(9);
            }
        """
        for label, failing_source in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-later-mismatch-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(
                    "function definition does not match prior "
                    "function-pointer initializer",
                    result.stderr,
                )
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_failed_method_does_not_leak_callback_typedef_metadata(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-method-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*double_cb)(double value);
                class Invoker {
                  int Invoke(double_cb callback) {
                    return callback(4) + missing_value;
                  }
                };
                """,
                """
                typedef int (*integer_cb)(int value);
                int identity(int value) { return value; }
                class Invoker {
                  int Invoke(integer_cb callback) { return callback(9); }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(identity);
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_signature_erased_callback_alias_chain_keeps_legacy_initializer(self):
        source = """
        typedef double (*base_callback_t)(double);
        typedef base_callback_t alias_callback_t;

        double identity(double value) { return value; }

        int main() {
          alias_callback_t callback = identity;
          return callback != 0;
        }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 1, result.stdout + result.stderr
                )

    def test_active_usb_callbacks_use_typedef_automatic_locals(self):
        header = (REPO_ROOT / "kernel" / "usb" / "usb_hc.h").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "typedef void (*usb_complete_cb_t)(int status, usb_transfer_t *);",
            header,
        )

        for relative_path, poll_name in (
            ("uhci.cc", "uhci_poll_interrupts"),
            ("ehci.cc", "ehci_poll_interrupts"),
        ):
            with self.subTest(source=relative_path):
                source = (
                    REPO_ROOT / "kernel" / "usb" / relative_path
                ).read_text(encoding="utf-8")
                self.assertIn(f"void {poll_name}(void) {{", source)
                self.assertIn("usb_complete_cb_t         cb;", source)
                self.assertIn("c->interrupt_slots[i].cb = NULL;", source)
                self.assertIn("slot->cb = cb;", source)
                self.assertIn("usb_complete_cb_t cb = NULL;", source)
                self.assertIn("cb = slot->cb;", source)
                self.assertIn("if (deliver) cb(0, &local);", source)

    def test_active_callback_fields_use_direct_postfix_calls(self):
        timer_header = (REPO_ROOT / "drivers" / "timer.h").read_text(
            encoding="utf-8"
        )
        timer_source = (REPO_ROOT / "drivers" / "timer.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "typedef void (*timer_callback_t)(struct registers*, "
            "uint32_t channel);",
            timer_header,
        )
        self.assertIn(
            "timer_channels[i].callback(r, (uint32_t)i);",
            timer_source,
        )

        gui_header = (
            REPO_ROOT / "kernel" / "gui" / "gui_events.h"
        ).read_text(encoding="utf-8")
        gui_source = (
            REPO_ROOT / "kernel" / "gui" / "gui_events.cc"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "typedef void (*ui_event_callback_t)(ui_event_t *event, "
            "void *context);",
            gui_header,
        )
        self.assertIn("h->callback(ev, h->context);", gui_source)

    def test_typedef_callback_field_store_copy_and_call_run_in_jit_and_aot(self):
        source = """
            struct usb_transfer { int marker; };
            typedef struct usb_transfer usb_transfer_t;
            typedef void (*usb_complete_cb_t)(int status, usb_transfer_t *);

            typedef struct {
              usb_complete_cb_t cb;
            } interrupt_slot_t;

            typedef struct {
              interrupt_slot_t interrupt_slots[2];
            } controller_t;

            class completion_box_t {
              usb_complete_cb_t cb;
            };

            int observed;

            void complete(int status, usb_transfer_t *transfer) {
              observed = status + transfer->marker;
            }

            int deliver(controller_t *controller, int index,
                        usb_complete_cb_t supplied,
                        usb_transfer_t *transfer) {
              usb_complete_cb_t cb = 0;
              controller->interrupt_slots[index].cb = supplied;
              cb = controller->interrupt_slots[index].cb;
              controller->interrupt_slots[index].cb = 0;
              if (controller->interrupt_slots[index].cb != 0) return 1;
              cb(7, transfer);
              return observed;
            }

            int prepare_box(completion_box_t *box,
                            usb_complete_cb_t supplied) {
              usb_complete_cb_t copied = 0;
              box->cb = supplied;
              copied = box->cb;
              box->cb = 0;
              return copied != 0 && box->cb == 0;
            }

            int main() {
              controller_t controller;
              completion_box_t box;
              usb_transfer_t transfer;
              transfer.marker = 9;
              if (!prepare_box(&box, complete)) return 2;
              return deliver(&controller, 1, complete, &transfer);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_typedef_callback_field_postfix_call_runs_in_jit_and_aot(self):
        source = """
            struct Entry { int marker; };
            typedef int (*callback_t)(struct Entry *, double, int);

            struct Slot { callback_t callback; };
            struct Controller { struct Slot slots[2]; };

            int controller_calls;
            int index_calls;

            struct Controller *select_controller(
                    struct Controller *controller) {
              controller_calls++;
              return controller;
            }

            int select_slot(void) {
              index_calls++;
              return 1;
            }

            int target(struct Entry *entry, double value, int tag) {
              return entry->marker + (int)(value * 10.0) + tag;
            }

            int invoke(struct Controller *controller, struct Entry *entry) {
              return select_controller(controller)->slots[
                  select_slot()].callback(
                  entry, 3, 4.75f);
            }

            int main() {
              struct Controller controller;
              struct Entry entry;
              int result;
              entry.marker = 9;
              controller.slots[1].callback = target;
              result = invoke(&controller, &entry);
              if (controller_calls != 1 || index_calls != 1) return 1;
              return result;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 43, result.stdout + result.stderr
                )

    def test_callback_field_postfix_call_wins_over_same_named_method(self):
        source = """
            typedef int (*callback_t)(int);

            int add_one(int value) {
              return value + 1;
            }

            class Holder {
              callback_t Invoke;

              int Invoke(int value) {
                return value + 40;
              }
            };

            int main() {
              Holder holder;
              holder.Invoke = add_one;
              return holder.Invoke(8);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 9, result.stdout + result.stderr
                )

    def test_raw_callback_struct_field_runs_in_jit_and_aot(self):
        source = """
            struct Entry { int value; };

            struct Holder {
              int (*callback)(struct Entry *, int);
            };

            int add(struct Entry *entry, int amount) {
              return entry->value + amount;
            }

            int main() {
              struct Entry entry;
              struct Holder holder;
              int (*copied)(struct Entry *, int) = 0;
              entry.value = 9;
              holder.callback = add;
              copied = holder.callback;
              holder.callback = 0;
              if (holder.callback != 0) return 1;
              return copied(&entry, 7);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_raw_callback_class_field_runs_in_jit_and_aot(self):
        source = """
            class Holder {
              int (*callback)(int, double, int);
            };

            int add(int left, double middle, int right) {
              return left + (int)middle + right;
            }

            int invoke(Holder *holder,
                       int (*supplied)(int, double, int)) {
              int (*copied)(int, double, int) = 0;
              holder->callback = supplied;
              copied = holder->callback;
              holder->callback = 0;
              if (holder->callback != 0) return 1;
              return copied(7, 5.0, 4);
            }

            int main() {
              Holder holder;
              return invoke(&holder, add);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_raw_callback_field_postfix_call_keeps_simd_result(self):
        source = """
            struct Holder {
              float4 (*callback)(float4, double, float4);
            };

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            int main() {
              struct Holder holder;
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              holder.callback = blend;
              result = holder.callback(left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_raw_variadic_callback_field_postfix_call_promotes_tail(self):
        source = """
            struct Holder {
              int (*callback)(double, ...);
            };

            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int main() {
              struct Holder holder;
              holder.callback = inspect;
              return holder.callback(6, 2.5f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 6, result.stdout + result.stderr
                )

    def test_raw_callback_typedef_struct_fields_run_in_jit_and_aot(self):
        source = """
            typedef struct {
              void *context;
              int (*allocate)(void *, int);
              void (*release)(void *, int);
            } allocator_t;

            int released;

            int allocate_value(void *context, int amount) {
              return *(int *)context + amount;
            }

            void release_value(void *context, int amount) {
              released = *(int *)context - amount;
            }

            int main() {
              int base = 11;
              allocator_t allocator;
              int (*allocate)(void *, int) = 0;
              void (*release)(void *, int) = 0;
              allocator.context = &base;
              allocator.allocate = allocate_value;
              allocator.release = release_value;
              allocate = allocator.allocate;
              release = allocator.release;
              release(allocator.context, 4);
              return allocate(allocator.context, 2) + released - 4;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_typedef_callback_field_arrays_run_in_jit_and_aot(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            struct Holder {
              callback_t callbacks[2];
            };

            int selected;
            int stored;
            int observed;

            int select_slot(void) {
              selected++;
              return 1;
            }

            int select_store(void) {
              stored++;
              return 0;
            }

            int combine(double first, int second) {
              observed = (int)first * 10 + second;
              return observed;
            }

            int subtract(double first, int second) {
              return (int)first * 10 - second;
            }

            int main(void) {
              struct Holder holder;
              callback_t copied = 0;
              holder.callbacks[select_store()] = combine;
              holder.callbacks[1] = subtract;
              if (stored != 1) return 1;
              holder.callbacks[0](2, 5);
              if (observed != 25) return 2;
              copied = holder.callbacks[0];
              if (copied(3, 4) != 34) return 3;
              if (holder.callbacks[select_slot()](5, 2) != 48) return 4;
              if (selected != 1) return 5;
              return 83;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 83, result.stdout + result.stderr
                )

    def test_typedef_callback_field_arrays_cover_other_record_paths(self):
        records = (
            (
                "class",
                "class Holder { callback_t callbacks[2]; };",
                "Holder",
            ),
            (
                "anonymous-typedef",
                "typedef struct { callback_t callbacks[2]; } Holder;",
                "Holder",
            ),
        )
        for label, declaration, holder_type in records:
            source = f"""
                typedef int (*callback_t)(int value);
                {declaration}

                int plus_one(int value) {{ return value + 1; }}
                int minus_two(int value) {{ return value - 2; }}

                int main(void) {{
                  {holder_type} holder;
                  holder.callbacks[0] = plus_one;
                  holder.callbacks[1] = minus_two;
                  return holder.callbacks[0](4) + holder.callbacks[1](9);
                }}
            """
            for aot in (False, True):
                with self.subTest(record=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode, 12, result.stdout + result.stderr
                    )

        repl_result = self._compile_repl_and_run(
            (
                "typedef int (*callback_t)(int value);",
                "struct Holder { callback_t callbacks[2]; };",
                "int plus_one(int value) { return value + 1; }",
                "int minus_two(int value) { return value - 2; }",
                "struct Holder holder;",
                """
                int main(void) {
                  holder.callbacks[0] = plus_one;
                  holder.callbacks[1] = minus_two;
                  return holder.callbacks[0](4) + holder.callbacks[1](9);
                }
                """,
            )
        )
        self.assertEqual(
            repl_result.returncode,
            12,
            repl_result.stdout + repl_result.stderr,
        )

    def test_typedef_callback_field_arrays_retain_variadic_return_abi(self):
        source = """
            typedef double (*callback_t)(double, ...);

            struct Holder {
              callback_t callbacks[2];
            };

            double keep(double value, ...) {
              return value + 0.5;
            }

            int main(void) {
              struct Holder holder;
              holder.callbacks[0] = keep;
              return (int)holder.callbacks[0](8.5, 1.25f, 3);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 9, result.stdout + result.stderr
                )

    def test_doom_wipe_callback_table_keeps_active_raw_array_shape(self):
        source = (
            REPO_ROOT / "kernel" / "doom" / "src" / "f_wipe.cc"
        ).read_text(encoding="utf-8")

        for fragment in (
            "static int (*wipes[])(int, int, int)",
            "(*wipes[wipeno*3])(width, height, ticks);",
            "(*wipes[wipeno*3+1])(width, height, ticks);",
            "(*wipes[wipeno*3+2])(width, height, ticks);",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, source)

    def test_block_static_raw_callback_arrays_run_in_jit_and_aot(self):
        source = """
            int wipe_zero_start(int width, int height, int ticks) {
              return 100 + width + height + ticks;
            }

            int wipe_zero_tick(int width, int height, int ticks) {
              return 200 + width + height + ticks;
            }

            int wipe_zero_end(int width, int height, int ticks) {
              return 300 + width + height + ticks;
            }

            int wipe_one_start(int width, int height, int ticks) {
              return 400 + width + height + ticks;
            }

            int wipe_one_tick(int width, int height, int ticks);

            int wipe_one_end(int width, int height, int ticks) {
              return 600 + width + height + ticks;
            }

            int replacement(int width, int height, int ticks) {
              return 900 + width + height + ticks;
            }

            int dispatch(int wipeno, int phase, int replace) {
              static int (*wipes[])(int, int, int) = {
                wipe_zero_start, wipe_zero_tick, wipe_zero_end,
                wipe_one_start, wipe_one_tick, wipe_one_end
              };
              if (replace) wipes[wipeno*3+1] = replacement;
              if (phase == 0)
                return (*wipes[wipeno*3])(4, 2, 1);
              if (phase == 1)
                return (*wipes[wipeno*3+1])(4, 2, 1);
              return (*wipes[wipeno*3+2])(4, 2, 1);
            }

            int wipe_one_tick(int width, int height, int ticks) {
              return 500 + width + height + ticks;
            }

            int main(void) {
              if (dispatch(0, 0, 0) != 107) return 1;
              if (dispatch(0, 1, 0) != 207) return 2;
              if (dispatch(0, 2, 0) != 307) return 3;
              if (dispatch(1, 0, 0) != 407) return 4;
              if (dispatch(1, 1, 0) != 507) return 5;
              if (dispatch(1, 2, 0) != 607) return 6;
              if (dispatch(1, 1, 1) != 907) return 7;
              if (dispatch(1, 1, 0) != 907) return 8;
              return 83;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 83, result.stdout + result.stderr
                )

    def test_block_static_raw_callback_scalar_runs_in_jit_and_aot(self):
        source = """
            int later_target(int value);
            int replacement(int value) { return value + 9; }

            int dispatch(int replace) {
              static int (*callback)(int) = later_target;
              if (replace) callback = replacement;
              return callback(9);
            }

            int later_target(int value) { return value + 1; }

            int main(void) {
              if (dispatch(0) != 10) return 1;
              if (dispatch(1) != 18) return 2;
              if (dispatch(0) != 18) return 3;
              return 85;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 85, result.stdout + result.stderr
                )

    def test_raw_callback_arrays_keep_mixed_width_calls_and_one_store_index(self):
        source = """
            int call_index_calls;
            int store_index_calls;

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int replacement(double first, int second) {
              return (int)first * 20 + second;
            }

            int next_store_index(void) {
              store_index_calls += 1;
              return 0;
            }

            int next_call_index(void) {
              call_index_calls += 1;
              return 0;
            }

            int main(void) {
              static int (*callbacks[])(double, int) = { combine };
              int initial = (*callbacks[next_call_index()])(2.5, 4);
              callbacks[next_store_index()] = replacement;
              if (call_index_calls != 1) return 1;
              if (store_index_calls != 1) return 2;
              if (initial != 24) return 3;
              return callbacks[0](2.5, 4);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 44, result.stdout + result.stderr
                )

    def test_file_scope_raw_callback_arrays_run_in_jit_and_aot(self):
        source = """
            int plus_one(int value) { return value + 1; }

            int (*inferred_callbacks[])(int) = {
              (&plus_one), later_global,
            };
            int (*fixed_callbacks[3])(int) = { (&plus_one), 0 };
            int (*zeroed_callbacks[1])(int);

            int later_global(int value) { return value + 4; }

            int main(void) {
              int assigned_result;
              int (*copied)(int) = inferred_callbacks[1];
              if (fixed_callbacks[1] != 0) return 1;
              if (fixed_callbacks[2] != 0) return 2;
              if (zeroed_callbacks[0] != 0) return 3;
              if (sizeof(inferred_callbacks) != 8) return 4;
              if (sizeof(fixed_callbacks) != 12) return 5;
              if (sizeof(zeroed_callbacks) != 4) return 6;
              fixed_callbacks[2] = later_global;
              assigned_result = fixed_callbacks[2](5);
              fixed_callbacks[2] = 0;
              if (inferred_callbacks[0](3) != 4) return 7;
              if ((*inferred_callbacks[1])(3) != 7) return 8;
              if (copied(3) != 7) return 9;
              if (assigned_result != 9) return 10;
              if (fixed_callbacks[2] != 0) return 11;
              return 84;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 84, result.stdout + result.stderr
                )

    def test_raw_callback_arrays_keep_variadic_double_results(self):
        source = """
            double keep(double value, ...) { return value + 0.5; }

            int main(void) {
              static double (*callbacks[])(double, ...) = { keep };
              return (int)(*callbacks[0])(8.5, 1.25f, 3);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 9, result.stdout + result.stderr
                )

    def test_raw_callback_array_elements_flow_through_typed_values(self):
        source = """
            int add_one(int value) { return value + 1; }

            int invoke(int (*callback)(int), int value) {
              return callback(value);
            }

            int main(void) {
              static int (*callbacks[])(int) = { add_one };
              int (*copied)(int) = callbacks[0];
              return invoke(callbacks[0], 8) + copied(8);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 18, result.stdout + result.stderr
                )

    def test_raw_callback_arrays_keep_record_pointer_identity(self):
        source = """
            struct Pair { int value; };

            struct Pair *choose(struct Pair *value) { return value; }

            struct Pair *invoke(
                struct Pair *(*callback)(struct Pair *),
                struct Pair *value) {
              return callback(value);
            }

            int main(void) {
              static struct Pair *(*callbacks[])(struct Pair *) = {
                choose
              };
              struct Pair *(*copied)(struct Pair *) = callbacks[0];
              struct Pair pair;
              pair.value = 23;
              return invoke(callbacks[0], &pair)->value +
                     copied(&pair)->value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 46, result.stdout + result.stderr
                )

    def test_repl_keeps_raw_callback_array_signature(self):
        result = self._compile_repl_and_run(
            (
                "int later_repl(int value); "
                "int (*callbacks[])(int) = { later_repl, 0 };",
                "int later_repl(int value) { return value + 4; }",
                "int main(void) { "
                "if (callbacks[1] != 0) return 1; "
                "return (*callbacks[0])(8); }",
            )
        )
        self.assertEqual(result.returncode, 12, result.stdout + result.stderr)

    def test_repl_accepts_raw_callback_array_without_a_semicolon(self):
        result = self._compile_repl_and_run(
            (
                "int plus_one(int value) { return value + 1; }",
                "int (*callbacks[])(int) = { plus_one }",
                "int main(void) { return callbacks[0](8); }",
            )
        )
        self.assertEqual(result.returncode, 9, result.stdout + result.stderr)

    def test_raw_callback_array_failures_recover_same_state(self):
        cases = (
            (
                "unsized-without-initializer",
                "int (*callbacks[])(int); int main(void) { return 0; }",
                "unsized raw function-pointer array requires an initializer",
            ),
            (
                "empty-unsized-initializer",
                "int (*callbacks[])(int) = {}; int main(void) { return 0; }",
                "unsized raw function-pointer array requires at least one initializer",
            ),
            (
                "excess-fixed-initializers",
                """
                int first(int value) { return value; }
                int second(int value) { return value + 1; }
                int (*callbacks[1])(int) = { first, second };
                int main(void) { return 0; }
                """,
                "too many initializers for raw function-pointer array",
            ),
            (
                "initializer-without-braces",
                """
                int right(int value) { return value; }
                int (*callbacks[1])(int) = right;
                int main(void) { return 0; }
                """,
                "raw function-pointer array initializer requires braces",
            ),
            (
                "incompatible-initializer",
                """
                int later_compatible(int value);
                double wrong_result(int value) { return value; }
                int (*callbacks[])(int) = {
                  later_compatible, wrong_result
                };
                int later_compatible(int value) { return value; }
                int main(void) { return 0; }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "automatic-storage",
                """
                int main(void) {
                  int (*callbacks[2])(int);
                  return 0;
                }
                """,
                "automatic raw function-pointer arrays are not supported",
            ),
            (
                "parameter-storage",
                """
                int invoke(int (*callbacks[2])(int)) { return 0; }
                int main(void) { return 0; }
                """,
                "raw function-pointer array parameters are not supported",
            ),
            (
                "nonpositive-size",
                "int (*callbacks[0])(int); int main(void) { return 0; }",
                "raw function-pointer array size must be positive",
            ),
            (
                "multiple-dimensions",
                "int (*callbacks[2][2])(int); int main(void) { return 0; }",
                "raw function-pointer arrays support one dimension",
            ),
            (
                "indexed-call-too-few-arguments",
                """
                int combine(int first, int second) {
                  return first + second;
                }
                int (*callbacks[])(int, int) = { combine };
                int main(void) { return callbacks[0](1); }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "indexed-call-too-many-arguments",
                """
                int identity(int value) { return value; }
                int (*callbacks[])(int) = { identity };
                int main(void) { return callbacks[0](1, 2); }
                """,
                "function-pointer call has too many arguments",
            ),
            (
                "incompatible-indexed-store",
                """
                int right(int value) { return value; }
                int wrong(double value) { return (int)value; }
                int (*callbacks[])(int) = { right };
                int main(void) {
                  callbacks[0] = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "incompatible-indexed-store-result",
                """
                int right(int value) { return value; }
                double wrong(int value) { return value; }
                int (*callbacks[])(int) = { right };
                int main(void) {
                  callbacks[0] = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment result does not match destination",
            ),
            (
                "incompatible-indexed-argument",
                """
                int target(double value) { return (int)value; }
                int invoke(int (*callback)(int), int value) {
                  return callback(value);
                }
                int (*callbacks[])(double) = { target };
                int main(void) { return invoke(callbacks[0], 9); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "incompatible-indexed-declaration-initializer",
                """
                int target(double value) { return (int)value; }
                int (*callbacks[])(double) = { target };
                int main(void) {
                  int (*copied)(int) = callbacks[0];
                  return copied(9);
                }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "incompatible-indexed-argument-result",
                """
                double target(int value) { return value; }
                int invoke(int (*callback)(int), int value) {
                  return callback(value);
                }
                double (*callbacks[])(int) = { target };
                int main(void) { return invoke(callbacks[0], 9); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "incompatible-indexed-declaration-initializer-result",
                """
                double target(int value) { return value; }
                double (*callbacks[])(int) = { target };
                int main(void) {
                  int (*copied)(int) = callbacks[0];
                  return copied(9);
                }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "incompatible-indexed-argument-record-identity",
                """
                struct Pair { int value; };
                struct Other { int value; };
                struct Pair *choose(struct Pair *value) { return value; }
                struct Other *invoke(
                    struct Other *(*callback)(struct Other *),
                    struct Other *value) {
                  return callback(value);
                }
                struct Pair *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Other value;
                  return invoke(callbacks[0], &value)->value;
                }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "incompatible-indexed-initializer-record-identity",
                """
                struct Pair { int value; };
                struct Other { int value; };
                struct Pair *choose(struct Pair *value) { return value; }
                struct Pair *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Other *(*copied)(struct Other *) = callbacks[0];
                  struct Other value;
                  return copied(&value)->value;
                }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "incompatible-indexed-argument-parameter-record-identity",
                """
                struct Result { int value; };
                struct Pair { int value; };
                struct Other { int value; };
                struct Result output;
                struct Result *choose(struct Pair *value) { return &output; }
                struct Result *invoke(
                    struct Result *(*callback)(struct Other *),
                    struct Other *value) {
                  return callback(value);
                }
                struct Result *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Other value;
                  return invoke(callbacks[0], &value)->value;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "incompatible-indexed-initializer-parameter-record-identity",
                """
                struct Result { int value; };
                struct Pair { int value; };
                struct Other { int value; };
                struct Result output;
                struct Result *choose(struct Pair *value) { return &output; }
                struct Result *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Result *(*copied)(struct Other *) = callbacks[0];
                  struct Other value;
                  return copied(&value)->value;
                }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
        )
        retry_source = """
            int right(int value) { return value; }
            int main(void) {
              static int (*callbacks[])(int) = { right };
              return callbacks[0](9);
            }
        """

        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-array-recovery-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_repl_rolls_back_a_partial_raw_callback_array_initializer(self):
        capacity_declarations = []
        for arity in range(32):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            capacity_declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-raw-array-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "int later_poisoned(double value); "
                        "double wrong_result(double value) { return value; } "
                        "int (*poisoned[])(double) = { "
                        "later_poisoned, wrong_result };",
                        "int sentinel = 77; "
                        "int later_poisoned(double value) { "
                        "return (int)value; } "
                        + "\n".join(capacity_declarations),
                        "int recovered(int value) { return value; } "
                        "int (*callbacks[])(int) = { recovered }; "
                        "int main(void) { "
                        "if (baseline != 0 || sentinel != 77) return 1; "
                        "return callbacks[0](9); }",
                    ),
                    require_patch=True,
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "function-pointer initializer result does not match declaration",
                result.stderr,
            )
            self.assertEqual(data_path.stat().st_size, 12)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_repl_keeps_raw_callback_struct_field_signature(self):
        result = self._compile_repl_and_run(
            (
                "struct Holder { int (*callback)(int); };",
                "int add_seven(int value) { return value + 7; }",
                """
                int main() {
                  struct Holder holder;
                  int (*copied)(int) = 0;
                  holder.callback = add_seven;
                  copied = holder.callback;
                  return copied(9);
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 16, result.stdout + result.stderr)

    def test_repl_rolls_back_failed_raw_callback_struct_field(self):
        capacity_declarations = []
        for arity in range(32):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            capacity_declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-raw-field-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "struct Poisoned { int (*callback)(double); }; "
                        "int broken(void) { return missing_value; }",
                        "\n".join(capacity_declarations),
                        "int identity(int value) { return value; } "
                        "int invoke(int (*callback)(int)) { "
                        "return callback(9); } "
                        "int main() { return invoke(identity); }",
                    ),
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_raw_callback_field_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                struct Holder { int (*callback)(int); };
                double wrong(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.callback = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment result does not match destination",
            ),
            (
                "parameters",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                class Holder {
                  int (*callback)(struct Expected *);
                };
                int wrong(struct Wrong *value) { return value->value; }
                int main() {
                  Holder holder;
                  holder.callback = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "missing-name",
                """
                struct Holder { int (*)(int); };
                int main() { return 0; }
                """,
                "expected function pointer name",
            ),
            (
                "raw-array",
                """
                struct Holder { int (*callbacks[2])(int); };
                int main() { return 0; }
                """,
                "raw function-pointer arrays are not supported; use a callback typedef",
            ),
        )
        retry_source = """
            struct Holder { int (*callback)(int); };
            int right(int value) { return value; }
            int main() {
              struct Holder holder;
              int (*copied)(int) = 0;
              holder.callback = right;
              copied = holder.callback;
              return copied(9);
            }
        """

        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-field-recovery-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_raw_callback_field_capacity_recovers_same_state(self):
        declarations = []
        for arity in range(33):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            declarations.append(
                f"struct Holder{arity} {{ "
                f"int (*callback)({parameters}); }};"
            )
        failing_source = (
            "\n".join(declarations) + "\nint main() { return 0; }"
        )
        retry_source = """
            struct Holder { int (*callback)(int); };
            int right(int value) { return value; }
            int main() {
              struct Holder holder;
              int (*copied)(int) = 0;
              holder.callback = right;
              copied = holder.callback;
              return copied(9);
            }
        """

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-raw-callback-field-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                failing_source,
                retry_source,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "too many raw function-pointer signatures",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_callback_field_postfix_call_failures_recover_same_state(self):
        cases = (
            (
                "too-few",
                """
                typedef int (*callback_t)(int, int);
                struct Holder { callback_t callback; };
                int add(int left, int right) { return left + right; }
                int main() {
                  struct Holder holder;
                  holder.callback = add;
                  return holder.callback(1);
                }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too-many",
                """
                struct Holder { int (*callback)(int); };
                int identity(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.callback = identity;
                  return holder.callback(1, 2);
                }
                """,
                "function-pointer call has too many arguments",
            ),
            (
                "fixed-type",
                """
                struct Holder { int (*callback)(double); };
                int inspect(double value) { return (int)value; }
                int main() {
                  struct Holder holder;
                  int value = 7;
                  holder.callback = inspect;
                  return holder.callback(&value);
                }
                """,
                "cdecl argument type does not match fixed parameter",
            ),
            (
                "array-too-few",
                """
                typedef int (*callback_t)(int, int);
                struct Holder { callback_t callbacks[2]; };
                int add(int first, int second) { return first + second; }
                int main() {
                  struct Holder holder;
                  holder.callbacks[0] = add;
                  return holder.callbacks[0](1);
                }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "array-too-many",
                """
                typedef int (*callback_t)(int);
                struct Holder { callback_t callbacks[2]; };
                int identity(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.callbacks[0] = identity;
                  return holder.callbacks[0](1, 2);
                }
                """,
                "function-pointer call has too many arguments",
            ),
            (
                "array-fixed-type",
                """
                typedef int (*callback_t)(double);
                struct Holder { callback_t callbacks[2]; };
                int inspect(double value) { return (int)value; }
                int main() {
                  struct Holder holder;
                  int value = 7;
                  holder.callbacks[0] = inspect;
                  return holder.callbacks[0](&value);
                }
                """,
                "cdecl argument type does not match fixed parameter",
            ),
        )
        retry_source = """
            struct Holder { int (*callback)(int); };
            int identity(int value) { return value; }
            int main() {
              struct Holder holder;
              holder.callback = identity;
              return holder.callback(9);
            }
        """

        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-field-call-recovery-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_typedef_callback_field_mismatches_recover_same_state(self):
        cases = (
            (
                "field-result",
                """
                typedef int (*callback_t)(int value);
                struct Holder { callback_t cb; };

                double wrong(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.cb = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment result does not match destination",
            ),
            (
                "field-store",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                typedef int (*callback_t)(struct Expected *);
                struct Holder { callback_t cb; };

                int wrong(struct Wrong *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  holder.cb = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "field-copy",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                typedef int (*expected_callback_t)(struct Expected *);
                typedef int (*wrong_callback_t)(struct Wrong *);
                struct Holder { expected_callback_t cb; };

                int right(struct Expected *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  wrong_callback_t callback = 0;
                  holder.cb = right;
                  callback = holder.cb;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "compound-store",
                """
                struct Entry { int value; };
                typedef int (*callback_t)(struct Entry *);
                struct Holder { callback_t cb; };

                int right(struct Entry *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  holder.cb += right;
                  return 0;
                }
                """,
                "function-pointer assignment requires plain =",
            ),
            (
                "callback-array-field-store",
                """
                typedef int (*callback_t)(int value);
                struct Holder { callback_t callbacks[2]; };

                int wrong(double value) { return (int)value; }
                int main() {
                  struct Holder holder;
                  holder.callbacks[0] = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "callback-array-field-copy",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                typedef int (*expected_callback_t)(struct Expected *value);
                typedef int (*wrong_callback_t)(struct Wrong *value);
                struct Holder { expected_callback_t callbacks[2]; };

                int right(struct Expected *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  wrong_callback_t callback = 0;
                  holder.callbacks[0] = right;
                  callback = holder.callbacks[0];
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
        )
        retry_source = """
            struct Entry { int value; };
            typedef int (*callback_t)(struct Entry *);
            struct Holder { callback_t cb; };

            int right(struct Entry *value) { return value->value; }
            int main() {
              struct Holder holder;
              struct Entry entry;
              callback_t callback = 0;
              entry.value = 9;
              holder.cb = right;
              callback = holder.cb;
              return callback(&entry);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-field-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_function_pointer_typedef_parameter_matches_active_iso_callback_widths(self):
        iso_source = (REPO_ROOT / "kernel" / "fs" / "iso9660.cc").read_text(
            encoding="utf-8"
        )
        for fragment in (
            "typedef int (*susp_entry_cb)(const uint8_t *entry, "
            "uint32_t elen, void *ctx);",
            "static int susp_walk(block_device_t *bdev,\n"
            "                     const uint8_t *su, uint32_t su_len,\n"
            "                     susp_entry_cb cb, void *ctx)",
            "uint8_t elen = cur[i + 2];",
            "int rc = cb(cur + i, elen, ctx);",
            "static int nm_entry_cb(const uint8_t *e, uint32_t elen, "
            "void *ctx)",
            "(void)susp_walk(bdev, su, su_len, nm_entry_cb, &nc);",
        ):
            with self.subTest(active_iso_fragment=fragment):
                self.assertIn(fragment, iso_source)

        source = """
            typedef int (*susp_entry_cb)(const uint8_t *entry,
                                         uint32_t elen, void *ctx);

            int inspect(const uint8_t *entry, uint32_t elen, void *ctx) {
              if (entry == 0 || ctx != 0) return 1;
              return elen;
            }

            int susp_walk(susp_entry_cb cb);

            int susp_walk(susp_entry_cb cb) {
              uint8_t entry = 1;
              uint8_t elen = 7;
              return cb(&entry, elen, 0);
            }

            int forward_walk(susp_entry_cb cb) {
              return susp_walk(cb);
            }

            int main() {
              return forward_walk(inspect);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 7, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_global_matches_active_doom_callback(self):
        doom_header = (
            REPO_ROOT / "kernel" / "doom" / "src" / "v_video.h"
        ).read_text(encoding="utf-8")
        doom_source = (
            REPO_ROOT / "kernel" / "doom" / "src" / "v_video.cc"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "typedef boolean (*vpatchclipfunc_t)(patch_t *, int, int);",
            doom_header,
        )
        for fragment in (
            "static vpatchclipfunc_t patchclip_callback = NULL;",
            "void V_SetPatchClipCallback(vpatchclipfunc_t func)",
            "patchclip_callback = func;",
            "if(!patchclip_callback(patch, x, y))",
        ):
            with self.subTest(active_doom_fragment=fragment):
                self.assertIn(fragment, doom_source)

        source = """
            typedef int boolean;
            typedef boolean (*vpatchclipfunc_t)(int *patch, int x, int y);

            static vpatchclipfunc_t patchclip_callback = ((void *)0);

            boolean allow_patch(int *patch, int x, int y) {
              return *patch + x + y == 12;
            }

            void V_SetPatchClipCallback(vpatchclipfunc_t func) {
              patchclip_callback = func;
            }

            int main() {
              int patch = 3;
              if (patchclip_callback != 0) return 1;
              V_SetPatchClipCallback(allow_patch);
              if (!patchclip_callback(&patch, 4, 5)) return 2;
              V_SetPatchClipCallback(0);
              return patchclip_callback == 0 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_global_assignment_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-global-assignment-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*callback_t)(double value);
                callback_t callback = 0;

                int wrong(int value) { return value; }

                int main() {
                  callback = wrong;
                  return 1;
                }
                """,
                """
                typedef int (*callback_t)(double value);
                callback_t callback = 0;

                int right(double value) { return (int)value; }

                int main() {
                  callback = right;
                  return callback(9);
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer assignment parameters do not match destination",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_global_callback_initializes_from_defined_function_in_jit_and_aot(self):
        source = """
            typedef int (*callback_t)(int value);

            int target(int value) { return value + 4; }
            callback_t callback = ((target));

            int main() { return callback(7); }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 11, result.stdout + result.stderr
                )

    def test_global_callback_initializes_from_later_function_in_jit_and_aot(self):
        source = """
            typedef int (*callback_t)(int value);

            callback_t callback = target;
            int target(int value) { return value + 5; }

            int main() { return callback(8); }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 13, result.stdout + result.stderr
                )

    def test_global_callback_initializes_from_kernel_binding_in_jit_and_aot(self):
        source = """
            typedef void (*printer_t)(char *text);

            void serial_printf(char *text);
            printer_t callback = serial_printf;

            int main() { return callback == serial_printf; }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 1, result.stdout + result.stderr
                )

    def test_global_callback_initializer_errors_recover_without_leaking_state(self):
        cases = (
            (
                "parameter mismatch",
                """
                typedef int (*callback_t)(double value);
                int wrong(int value) { return value; }
                callback_t callback = wrong;
                int main() { return callback(1); }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "unresolved function",
                """
                typedef int (*callback_t)(int value);
                int missing(int value);
                callback_t callback = missing;
                int main() { return callback(1); }
                """,
                "Unresolved symbol: missing",
            ),
            (
                "computed expression",
                """
                typedef int (*callback_t)(int value);
                int left(int value) { return value; }
                int right(int value) { return value + 1; }
                int choose;
                callback_t callback = choose ? left : right;
                int main() { return callback(1); }
                """,
                "global function-pointer initializer requires a function or null",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            callback_t callback = target;
            int target(int value) { return value + 1; }
            int main() { return callback(8); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-global-initializer-error-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_global_callback_later_definition_mismatch_restores_data_fixup(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-global-later-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*callback_t)(double value);
                callback_t callback = later;
                int later(int value) { return value; }
                int main() { return callback(1); }
                """,
                """
                typedef int (*callback_t)(int value);
                callback_t callback = later;
                int later(int value) { return value; }
                int main() { return callback(9); }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior "
                "function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_global_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*callback_t)(float4 left, int marker,
                                         float4 right);
            callback_t callback = ((void *)0);
            int calls;

            float4 merge(float4 left, int marker, float4 right) {
              calls += 1;
              if (marker != 7) return left;
              return left + right;
            }

            void set_callback(callback_t value) {
              callback = value;
            }

            int main() {
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              set_callback(merge);
              result = callback(left, 7, right);
              set_callback(0);
              if (callback != 0 || calls != 1) return 1;
              if (result.x != 6.0f || result.y != 8.0f) return 2;
              if (result.z != 10.0f || result.w != 12.0f) return 3;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*blend_cb)(float4 left, double marker,
                                       float4 right);

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            int invoke_blend(blend_cb cb) {
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              result = cb(left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }

            int main() {
              return invoke_blend(blend);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_keeps_variadic_promotions(self):
        source = """
            typedef int (*inspect_cb)(double fixed, ...);

            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int invoke_inspect(inspect_cb cb) {
              return cb(6, 2.5f);
            }

            int main() {
              return invoke_inspect(inspect);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 6, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_mismatch_recovers_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-parameter-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*susp_entry_cb)(const uint8_t *entry,
                                             uint32_t elen, void *ctx);

                int wrong(double value) {
                  return (int)value;
                }

                int susp_walk(susp_entry_cb cb) {
                  uint8_t entry = 1;
                  return cb(&entry, 7, 0);
                }

                int main() {
                  return susp_walk(wrong);
                }
                """,
                """
                typedef int (*susp_entry_cb)(const uint8_t *entry,
                                             uint32_t elen, void *ctx);

                int inspect(const uint8_t *entry, uint32_t elen,
                            void *ctx) {
                  if (entry == 0 || ctx != 0) return 1;
                  return elen;
                }

                int susp_walk(susp_entry_cb cb) {
                  uint8_t entry = 1;
                  return cb(&entry, 7, 0);
                }

                int main() {
                  return susp_walk(inspect);
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer argument parameters do not match parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 7, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_parameter_checks_full_signature(self):
        cases = (
            (
                "result",
                """
                typedef int (*callback_t)(int value);
                double wrong(int value) { return value; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "variadic",
                """
                typedef int (*callback_t)(double fixed, ...);
                int wrong(double fixed) { return (int)fixed; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                typedef struct One *(*callback_t)(struct One *value);
                struct Two *wrong(struct Two *value) { return value; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "nonzero integer",
                """
                typedef int (*callback_t)(int value);
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(1); }
                """,
                "function-pointer argument requires a function, zero, or "
                "explicit pointer cast",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            int right(int value) { return value; }
            int invoke(callback_t cb) { return cb(9); }
            int main() { return invoke(right); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-full-signature-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_function_pointer_typedef_parameter_keeps_null_and_erased_forms(self):
        source = """
            typedef int (*callback_t)(int value);

            double wrong(double value) {
              return value;
            }

            int inspect(callback_t cb) {
              return cb == 0 ? 3 : 5;
            }

            int main() {
              if (inspect(1 - 1) != 3) return 1;
              return inspect((void *)wrong) == 5 ? 0 : 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_prototype_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-prototype-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*integer_cb)(int value);
                typedef int (*double_cb)(double value);
                int invoke(integer_cb cb);
                int invoke(double_cb cb) { return 0; }
                int main() { return 0; }
                """,
                """
                typedef int (*callback_t)(int value);
                int right(int value) { return value; }
                int invoke(callback_t cb);
                int invoke(callback_t cb) { return cb(4); }
                int main() { return invoke(right); }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function declaration does not match prior declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 4, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_argument_constrains_later_definition(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-later-definition-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*callback_t)(double value);
                int invoke(callback_t cb) { return cb(6); }
                int main() { return invoke(later); }
                int later(int value) { return value; }
                """,
                """
                typedef int (*callback_t)(double value);
                int invoke(callback_t cb) { return cb(6); }
                int main() { return invoke(later); }
                int later(double value) { return (int)value; }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior "
                "function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 6, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_parameter_call_arity_recovers_same_state(self):
        cases = (
            (
                "too few",
                """
                typedef int (*callback_t)(int left, int right);
                int invoke(callback_t cb) { return cb(1); }
                int main() { return 0; }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many",
                """
                typedef int (*callback_t)(int value);
                int invoke(callback_t cb) { return cb(1, 2); }
                int main() { return 0; }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            int invoke(callback_t cb) { return cb(4, 5); }
            int main() { return invoke(add); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-arity-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_function_pointer_typedef_parameter_capacity_accepts_32_and_recovers(self):
        accepted_parameters = ", ".join(
            f"int value{index}" for index in range(32)
        )
        accepted_arguments = ", ".join(
            str(index + 1) for index in range(32)
        )
        accepted_source = f"""
            typedef int (*callback_t)({accepted_parameters});

            int target({accepted_parameters}) {{
              return value0 + value31;
            }}

            int invoke(callback_t cb) {{
              return cb({accepted_arguments});
            }}

            int main() {{
              return invoke(target);
            }}
        """

        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(accepted_source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    33,
                    result.stdout + result.stderr,
                )

        rejected_parameters = ", ".join(
            f"int value{index}" for index in range(33)
        )
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-parameter-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            recovery, _code, _data = self._compile_after_failure(
                root,
                f"typedef int (*callback_t)({rejected_parameters});",
                accepted_source,
                same_state=True,
            )
            self.assertEqual(
                recovery.returncode,
                0,
                recovery.stdout + recovery.stderr,
            )
            self.assertIn(
                "too many function-pointer parameters",
                recovery.stderr,
            )
            runtime = self._run_i386(root, int(recovery.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                33,
                runtime.stdout + runtime.stderr,
            )

    def test_function_pointer_typedef_parameter_keeps_record_parameter_identity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-record-parameter-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                struct One { int value; };
                struct Two { int value; };
                typedef int (*callback_t)(struct One *value);
                int wrong(struct Two *value) { return value->value; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                """
                struct One { int value; };
                typedef int (*callback_t)(struct One *value);
                int right(struct One *value) { return value->value; }
                int invoke(callback_t cb) {
                  struct One value;
                  value.value = 11;
                  return cb(&value);
                }
                int main() { return invoke(right); }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer argument parameters do not match parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 11, runtime.stdout + runtime.stderr
            )

    def test_named_variadic_function_pointer_converts_fixed_and_tail_slots(self):
        source = """
            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int main() {
              int (*callback)(double, ...) = inspect;
              return callback(6, 2.5f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 6, result.stdout + result.stderr
                )

    def test_named_function_pointer_keeps_nested_active_pointer_slots(self):
        result = self._compile_and_run(
            """
            int inspect(void *drawer, char *rows, int marker) {
              if (drawer == 0 || rows == 0) return 1;
              return marker;
            }

            int draw(int x, int y) {
              return x + y;
            }

            int main() {
              char rows[6];
              int (*callback)(void (*)(int, int), uint8_t (*)[6], int) =
                  (void *)inspect;
              return callback(draw, rows, 9);
            }
            """
        )
        self.assertEqual(result.returncode, 9, result.stdout + result.stderr)

    def test_named_function_pointer_initializer_parameter_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-param-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int transform(double value) {
                  return (int)value;
                }

                int invalid_initializer() {
                  int (*callback)(int) = transform;
                  return callback(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(7) == 7 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer parameters do not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_initializer_result_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                double transform(int value) {
                  return (double)value;
                }

                int invalid_initializer() {
                  int (*callback)(int) = transform;
                  return callback(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(9) == 9 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer result does not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_initializer_keeps_record_result_identity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-record-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                struct First { int value; };
                struct Second { int value; };
                struct First first;

                struct First *select_first(void) {
                  return &first;
                }

                int invalid_initializer() {
                  struct Second *(*callback)(void) = select_first;
                  return callback()->value;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(4) == 4 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer result does not match declaration",
            result.stderr,
        )

    def test_named_function_pointer_initializer_variadic_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-variadic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int inspect(int value, ...) {
                  return value;
                }

                int invalid_initializer() {
                  int (*callback)(int) = inspect;
                  return callback(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(5) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer parameters do not match declaration",
            result.stderr,
        )

    def test_explicit_cast_erases_function_pointer_initializer_signature(self):
        result = self._compile_and_run(
            """
            double transform(double value) {
              return value;
            }

            int main() {
              int (*callback)(int) = (void *)transform;
              return callback != 0 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_grouped_null_and_cast_function_pointer_initializers_run(self):
        source = """
            int observed;

            int identity(int value) {
              observed = value;
              return value;
            }

            int main() {
              int (*empty)(int) = ((void *)0);
              int (*callback)(int) = ((void *)identity);
              if (empty != 0) return 1;
              if (callback(12) != 12) return 2;
              return observed == 12 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_conditional_explicit_casts_erase_only_the_selected_value(self):
        source = """
            int choose_second;
            int first(int value) { return value + 1; }
            int second(int value) { return value + 2; }

            int main() {
              choose_second = 1;
              int (*callback)(int) =
                  choose_second ? (void *)second : (void *)first;
              int (*left_nullable)(int) =
                  choose_second ? (void *)first : 0;
              choose_second = 0;
              int (*right_nullable)(int) =
                  choose_second ? 0 : (void *)second;
              if (callback(8) != 10) return 1;
              if (left_nullable(8) != 9) return 2;
              return right_nullable(8) == 10 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-conditional-cast-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int choose_object;
                int object;
                int identity(int value) { return value; }
                int invalid(void) {
                  int (*callback)(int) =
                      choose_object ? (void *)identity : &object;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(6) == 6 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_integer_constant_expression_zero_is_a_null_callback(self):
        source = r"""
            int choose_target;
            int identity(int value) { return value; }

            int main() {
              int (*negative_zero)(int) = -0;
              int (*positive_zero)(int) = +0;
              int (*character_zero)(int) = '\0';
              int (*cast_zero)(int) = (int)0;
              int (*difference_zero)(int) = 1 - 1;
              int (*sizeof_zero)(int) = sizeof(int) - 4;
              choose_target = 1;
              int (*left_target)(int) =
                  choose_target ? identity : -0;
              choose_target = 0;
              int (*right_target)(int) =
                  choose_target ? 1 - 1 : identity;
              if (negative_zero != 0 || positive_zero != 0 ||
                  character_zero != 0 || cast_zero != 0 ||
                  difference_zero != 0 || sizeof_zero != 0) return 1;
              if (left_target(9) != 9) return 2;
              return right_target(11) == 11 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-nonconstant-zero-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int value;
                int invalid(void) {
                  int (*callback)(int) = 1 ? 0 : value;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main(void) {
                  int (*callback)(int) = identity;
                  return callback(8) == 8 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_cast_in_condition_does_not_erase_selected_object_pointer(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-cast-provenance-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int value;
                int target(int input) { return input; }

                int invalid_initializer() {
                  int (*callback)(int) =
                      (void *)target ? &value : (void *)target;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(5) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_cast_inside_subscript_does_not_erase_selected_object_pointer(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-child-cast-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int target(int value) { return value; }

                int invalid_initializer(void) {
                  char bytes[8];
                  int (*callback)(int) =
                      &bytes[sizeof((void *)target)];
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(4) == 4 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_function_pointer_initializer_patches_a_later_function_address(self):
        source = """
            double later(double value);

            int main() {
              double (*callback)(double) = later;
              return callback(6) == 6.0 ? 0 : 1;
            }

            double later(double value) {
              return value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_callback_target_definition_must_match_its_prior_prototype(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-prototype-definition-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                double later(double value);

                int invalid_initializer() {
                  double (*callback)(double) = later;
                  return callback != 0;
                }

                int later(int value) {
                  return value;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(1) == 1 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function declaration does not match prior declaration",
            result.stderr,
        )

    def test_rejected_forward_initializer_rolls_back_its_address_patch(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-patch-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int later(double value);

                int invalid_initializer() {
                  int (*callback)(int) = later;
                  return 0;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(4) == 4 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer parameters do not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_prescanned_callback_signature_is_checked_at_later_definition(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-later-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invalid_initializer() {
                  double (*callback)(double) = later;
                  return callback != 0;
                }

                int later(int value) {
                  return value;
                }
                """,
                """
                double later(double value) {
                  return value;
                }

                int main() {
                  double (*callback)(double) = later;
                  return callback(3) == 3.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_malformed_later_callback_declaration_restores_provisional_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-malformed-later-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invalid_initializer() {
                  double (*callback)(double) = later;
                  return callback != 0;
                }

                double later(double) {
                  return 0.0;
                }
                """,
                """
                double later(double value) {
                  return value;
                }

                int main() {
                  double (*callback)(double) = later;
                  return callback(11) == 11.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("expected parameter name", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_function_restores_inferred_target_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-inference-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invalid_initializer(void) {
                  int (*callback)(int) = later;
                  return missing_value;
                }

                int later(int value);
                """,
                """
                double later(double value) {
                  return value;
                }

                int main() {
                  double (*callback)(double) = later;
                  return callback(14) == 14.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_program_restores_a_committed_function_definition(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-program-symbol-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                double later(double value) { return value + 1.0; }
                """,
                """
                double later(double value);
                int broken(void) { return missing_value; }
                """,
                """
                int main() {
                  double (*callback)(double) = later;
                  return callback(4) == 5.0 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_program_restores_a_preexisting_function_prototype(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-prototype-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                double later(double value);
                """,
                """
                double later(double value) { return value + 1.0; }
                int broken(void) { return missing_value; }
                """,
                """
                double later(double value) { return value + 3.0; }
                int main(void) {
                  double (*callback)(double) = later;
                  return callback(4) == 7.0 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_program_restores_the_committed_start_thunk(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-start-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                int marker;
                marker = 1;
                """,
                """
                marker = 2;
                missing();
                """,
                """
                int main(void) {
                  marker = 0;
                  __start();
                  return marker == 1 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("[cupidc] Unresolved symbol: missing", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_defined_start_thunk_rejects_an_incompatible_callback_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-start-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                int marker;
                marker = 1;
                """,
                """
                int capture(void) {
                  void (*wrong)(int) = __start;
                  return wrong != 0;
                }
                """,
                """
                int main(void) {
                  void (*right)(void) = __start;
                  marker = 0;
                  right();
                  return marker == 1 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer parameters do not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_implicit_start_thunk_has_a_complete_void_callback_signature(self):
        source = """
            int marker;
            marker = 1;

            int capture(void) {
              void (*callback)(void) = __start;
              return callback != 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                with tempfile.TemporaryDirectory(
                    prefix="private-cupidc-start-signature-output-",
                    ignore_cleanup_errors=True,
                ) as temporary:
                    result, code_path, _data = self._compile(
                        Path(temporary), source, aot=aot
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                    self.assertGreater(code_path.stat().st_size, 0)

    def test_matching_prototype_preserves_a_kernel_binding(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-kernel-prototype-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void serial_printf(char *text);
                int main(void) {
                  serial_printf("callback prototype");
                  return 0;
                }
                """,
                aot=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_failed_method_restores_inferred_callback_signatures(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-method-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                class Broken {
                  int Fail(void) {
                    int (*callback)(int) = later;
                    return missing_value;
                  }
                };

                int later(int value);
                """,
                """
                double later(double value) { return value + 2.0; }

                int main() {
                  double (*callback)(double) = later;
                  return callback(5) == 7.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_method_restores_control_context_before_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-method-control-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_two_failures(
                root,
                """
                class Broken {
                  int Fail(void) {
                    while (missing_condition) {}
                    return 0;
                  }
                };
                """,
                """
                int main(void) {
                  break;
                  return 0;
                }
                """,
                """
                int main(void) { return 0; }
                """,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("undefined variable", result.stderr)
        self.assertIn("break outside loop or switch", result.stderr)

    def test_failed_callback_unit_discards_every_new_forward_patch(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-program-patches-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int pending(void);

                int invalid_initializer() {
                  double (*callback)(double) = later;
                  pending();
                  return callback != 0;
                }

                int later(int value) {
                  return value;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(13) == 13 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_prescanned_matching_callback_runs_in_jit_and_aot(self):
        source = """
            int observed;

            int main() {
              int (*callback)(int) = later;
              if (callback(8) != 8) return 1;
              return observed == 8 ? 0 : 2;
            }

            int later(int value) {
              observed = value;
              return value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_provisional_callback_signature_refines_from_empty_to_typed(self):
        source = """
            int observed;

            int main() {
              int (*loose)() = later;
              int (*typed)(int) = later;
              if (typed(10) != 10) return 1;
              return loose != 0 && observed == 10 ? 0 : 2;
            }

            int later(int value) {
              observed = value;
              return value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_callable_provenance_does_not_survive_logical_not(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-unary-provenance-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int target(int value) { return value; }
                int invalid_initializer() {
                  int (*callback)(int) = !target;
                  return 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(6) == 6 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_conditional_function_pointer_keeps_compatible_signatures(self):
        source = """
            int choose_second;

            int main() {
              int (*callback)(int) =
                  choose_second ? later_second : later_first;
              return callback(7) == 8 ? 0 : 1;
            }

            int later_first(int value) {
              return value + 1;
            }

            int later_second(int value) {
              return value + 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_conditional_function_pointer_accepts_null_in_either_arm(self):
        source = """
            int choose_null;
            int observed;

            int target(int value) {
              observed = value;
              return value;
            }

            int main() {
              choose_null = 0;
              int (*empty)(int) = choose_null ? target : 0;
              int (*callback)(int) = choose_null ? 0 : target;
              if (empty != 0) return 1;
              if (callback(15) != 15) return 2;
              return observed == 15 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_conditional_function_pointer_checks_every_later_arm(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-conditional-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int flag;

                int invalid_initializer() {
                  double (*callback)(double) = flag ? later_a : later_b;
                  return callback != 0;
                }

                double later_a(double value) { return value; }
                int later_b(int value) { return value; }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(8) == 8 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function definition does not match prior function-pointer initializer",
            result.stderr,
        )

    def test_named_function_pointer_copy_checks_retained_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-copy-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int inspect(double value) {
                  return (int)value;
                }

                int invalid_copy() {
                  int (*first)(double) = inspect;
                  int (*second)(int) = first;
                  return second(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*first)(int) = identity;
                  int (*second)(int) = first;
                  return second(7) == 7 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer parameters do not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_function_pointer_initializer_rejects_non_callable_values(self):
        cases = (
            ("floating value", "1.0", "floating"),
            ("nonzero integer", "7", "integer"),
            ("object pointer", "&value", "pointer"),
        )
        for label, initializer, declaration in cases:
            with self.subTest(initializer=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-function-pointer-value-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    f"""
                    int invalid_initializer() {{
                      int value = 1;
                      int (*callback)(int) = {initializer};
                      return value;
                    }}
                    """,
                    """
                    int identity(int value) {
                      return value;
                    }

                    int main() {
                      int (*callback)(int) = identity;
                      return callback(2) == 2 ? 0 : 1;
                    }
                    """,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(
                    "function-pointer initializer requires a function, zero, or explicit pointer cast",
                    result.stderr,
                )
                self.assertNotIn(declaration + " conversion", result.stderr)

    def test_function_pointer_initializer_accepts_zero(self):
        result = self._compile_and_run(
            """
            int main() {
              int (*callback)(int) = 0;
              return callback == 0 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mutable_enum_storage_is_not_proven_as_a_null_callback(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-enum-zero-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                enum { NIL = 0 };

                int invalid_initializer() {
                  NIL = 7;
                  int (*callback)(int) = NIL;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(3) == 3 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_function_pointer_initializer_checks_record_parameter_identity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-record-parameter-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                struct First { int value; };
                struct Second { int value; };

                int inspect(struct First *value) {
                  return value->value;
                }

                int invalid_initializer() {
                  int (*callback)(struct Second *) = inspect;
                  return 0;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(6) == 6 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer parameters do not match declaration",
            result.stderr,
        )

    def test_named_function_pointer_type_failure_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int inspect(double value) {
                  return (int)value;
                }

                int invalid_call() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  int (*callback)(double) = inspect;
                  return callback(value);
                }
                """,
                """
                int inspect(double value) {
                  return (int)value;
                }

                int main() {
                  int (*callback)(double) = inspect;
                  return callback(7) == 7 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_arity_failure_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-arity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int invalid_call() {
                  int (*callback)(double, int) = combine;
                  return callback(1);
                }
                """,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int main() {
                  int (*callback)(double, int) = combine;
                  return callback(2, 3) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer call has too few arguments", result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_excess_arity_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-excess-arity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int invalid_call() {
                  int (*callback)(double, int) = combine;
                  return callback(1, 2, 3);
                }
                """,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int main() {
                  int (*callback)(double, int) = combine;
                  return callback(2, 3) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer call has too many arguments", result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_struct_result_is_rejected_and_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-struct-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                struct Pair {
                  int value;
                };

                int invalid_call() {
                  struct Pair (*callback)(void);
                  return 0;
                }
                """,
                """
                int main() {
                  int (*callback)(void);
                  return callback == 0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer struct result is not supported; use pointer result",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_array_result_is_rejected_and_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-array-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int Row[2];

                int invalid_call() {
                  Row (*callback)(void);
                  return 0;
                }
                """,
                """
                int main() {
                  int (*callback)(void);
                  return callback == 0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer array result is not supported", result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

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

    def test_tagged_struct_typedef_declares_multiple_value_and_pointer_aliases(self):
        result = self._compile_and_run(
            """
            typedef struct TaggedPair {
              int left;
              int right;
            } TaggedPair, PairAlias, *TaggedPairPointer;

            int pair_sum(TaggedPairPointer pair) {
              return pair->left + pair->right;
            }

            int main() {
              TaggedPair tagged;
              PairAlias alias;
              tagged.left = 5;
              tagged.right = 7;
              alias.left = 11;
              alias.right = 13;
              if (pair_sum(&tagged) != 12) return 1;
              if (pair_sum(&alias) != 24) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_struct_typedef_keeps_each_declarator_pointer_depth(self):
        result = self._compile_and_run(
            """
            typedef struct ReversePair {
              int left;
              int right;
            } *ReversePairPointer, ReversePairValue;

            int main() {
              ReversePairValue value;
              ReversePairPointer pointer = &value;
              value.left = 17;
              value.right = 19;
              if (pointer->left != 17) return 1;
              if (pointer->right != 19) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_allocates_and_indexes_an_automatic_object(self):
        result = self._compile_and_run(
            """
            typedef int IntTriple[3];

            int main() {
              IntTriple values, others;
              values[0] = 29;
              values[1] = 31;
              values[2] = 37;
              others[0] = 41;
              others[1] = 43;
              others[2] = 47;
              if (sizeof(values) != 12) return 1;
              if (sizeof(others) != 12) return 2;
              if (values[0] + values[1] + values[2] != 97) return 3;
              return others[0] + others[1] + others[2] == 131 ? 0 : 4;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sizeof_fixed_array_typedef_reports_the_complete_type_size(self):
        result = self._compile_and_run(
            """
            typedef char SevenBytes[7];
            typedef struct Word { int value; } TwoWords[2];

            int main() {
              SevenBytes bytes;
              if (sizeof(SevenBytes) != 7) return 1;
              if (sizeof(TwoWords) != 8) return 2;
              if (sizeof(bytes) != 7) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_struct_array_typedef_allocates_a_complete_global_object(self):
        result = self._compile_and_run(
            """
            typedef struct Cell {
              int value;
            } CellArray[2], Cell;

            CellArray cells;

            int main() {
              Cell scalar;
              cells[0].value = 41;
              cells[1].value = 43;
              scalar.value = 47;
              if (sizeof(cells) != 8) return 1;
              if (cells[0].value + cells[1].value != 84) return 2;
              return scalar.value == 47 ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_keeps_its_shape_in_a_struct_field(self):
        result = self._compile_and_run(
            """
            typedef int IntTriple[3];
            typedef struct Holder {
              int leading;
              IntTriple values;
              int trailing;
            } Holder;

            int main() {
              Holder holder;
              holder.leading = 53;
              holder.values[0] = 59;
              holder.values[1] = 61;
              holder.values[2] = 67;
              holder.trailing = 71;
              if (sizeof(holder) != 20) return 1;
              if (holder.leading != 53 || holder.trailing != 71) return 2;
              return holder.values[0] + holder.values[1] +
                         holder.values[2] == 187 ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_record_members_keep_complete_element_types(self):
        result = self._compile_and_run(
            """
            typedef int Pair[2];
            typedef struct Word {
              int value;
            } Words[2], Word;
            typedef struct Holder {
              Pair values;
              Words words;
            } Holder;

            Holder holders[2];

            int main() {
              Holder holder;
              Holder *pointer = &holder;
              Holder *array_pointer = &holders[0];

              holder.values[0] = 11;
              pointer->values[1] = 13;
              holder.words[0].value = 17;
              pointer->words[1].value = 19;
              holders[1].words[0].value = 23;
              array_pointer[1].words[1].value = 29;

              if (sizeof(holder.values) != 8) return 1;
              if (sizeof(pointer->values) != 8) return 2;
              if (sizeof(holder.words) != 8) return 3;
              if (sizeof(holder.words[0]) != 4) return 4;
              if (sizeof(pointer->words[1]) != 4) return 5;
              if (holder.values[0] != 11 || pointer->values[1] != 13)
                return 6;
              if (holder.words[0].value != 17 ||
                  pointer->words[1].value != 19)
                return 7;
              if (holders[1].words[0].value != 23 ||
                  array_pointer[1].words[1].value != 29)
                return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_keeps_its_shape_in_a_standalone_struct(self):
        result = self._compile_and_run(
            """
            typedef char BytePair[2];
            struct Envelope {
              int leading;
              BytePair bytes;
              int trailing;
            };

            int main() {
              struct Envelope envelope;
              envelope.leading = 107;
              envelope.bytes[0] = 'O';
              envelope.bytes[1] = 'S';
              envelope.trailing = 109;
              if (sizeof(envelope) != 12) return 1;
              if (envelope.leading != 107 || envelope.trailing != 109) {
                return 2;
              }
              return envelope.bytes[0] == 'O' &&
                     envelope.bytes[1] == 'S' ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_keeps_its_shape_in_a_class_field(self):
        result = self._compile_and_run(
            """
            typedef int IntPair[2];
            class Packet {
              int leading;
              IntPair values;
              int trailing;
            };

            int main() {
              Packet packet;
              packet.leading = 127;
              packet.values[0] = 131;
              packet.values[1] = 137;
              packet.trailing = 139;
              if (sizeof(packet) != 16) return 1;
              if (packet.leading != 127 || packet.trailing != 139) return 2;
              return packet.values[0] + packet.values[1] == 268 ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_alias_chain_allocates_block_static_storage(self):
        result = self._compile_and_run(
            """
            typedef int IntPair[2];
            typedef IntPair PairAlias;

            int pair_sum() {
              static PairAlias values;
              values[0] = 73;
              values[1] = 79;
              if (sizeof(values) != 8) return -1;
              return values[0] + values[1];
            }

            int main() {
              return pair_sum() == 152 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_parameter_decays_to_an_element_pointer(self):
        result = self._compile_and_run(
            """
            typedef int IntTriple[3];

            int sum_triple(IntTriple values) {
              return values[0] + values[1] + values[2];
            }

            int main() {
              IntTriple values;
              values[0] = 83;
              values[1] = 89;
              values[2] = 97;
              return sum_triple(values) == 269 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_method_parameter_uses_the_same_decay(self):
        result = self._compile_and_run(
            """
            typedef int IntPair[2];

            class Accumulator {
              int Sum(IntPair values) {
                return values[0] + values[1];
              }
            };

            int main() {
              IntPair values;
              Accumulator accumulator;
              values[0] = 101;
              values[1] = 103;
              return accumulator.Sum(values) == 204 ? 0 : 1;
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

    def test_repl_keeps_multiple_anonymous_struct_typedef_aliases(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-multiple-typedef-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile_repl(
                Path(temporary),
                (
                    """
                    typedef struct {
                      int left;
                      int right;
                    } ReplPair, PairAlias, *ReplPairPointer;
                    """,
                    "ReplPair first;",
                    "PairAlias second;",
                    "ReplPairPointer pointer;",
                ),
            )
            data = data_path.read_bytes() if data_path.exists() else b""

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(data), 20)

    def test_repl_keeps_fixed_array_typedef_shape_for_later_units(self):
        result = self._compile_repl_and_run(
            (
                "typedef char ByteBlock[5];",
                "ByteBlock bytes;",
                """
                int main() {
                  bytes[0] = 'C';
                  bytes[4] = 'd';
                  if (sizeof(bytes) != 5) return 1;
                  if (bytes[0] != 'C') return 2;
                  return bytes[4] == 'd' ? 0 : 3;
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_keeps_function_pointer_typedef_for_later_parameter_unit(self):
        result = self._compile_repl_and_run(
            (
                "typedef int (*entry_cb)(const uint8_t *, uint32_t, void *);",
                """
                int inspect(const uint8_t *entry, uint32_t elen, void *ctx) {
                  if (entry == 0 || ctx != 0) return 1;
                  return elen;
                }
                """,
                """
                int invoke(entry_cb cb) {
                  uint8_t entry = 1;
                  return cb(&entry, 7, 0);
                }
                """,
                "int main() { return invoke(inspect); }",
            )
        )
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_repl_struct_field_keeps_a_fixed_array_typedef_shape(self):
        result = self._compile_repl_and_run(
            (
                "typedef int ReplPair[2];",
                "struct ReplHolder { int leading; ReplPair values; "
                "int trailing; };",
                "struct ReplHolder holder;",
                """
                int main() {
                  holder.leading = 149;
                  holder.values[0] = 151;
                  holder.values[1] = 157;
                  holder.trailing = 163;
                  if (sizeof(holder) != 16) return 1;
                  if (holder.leading != 149 || holder.trailing != 163) {
                    return 2;
                  }
                  return holder.values[0] + holder.values[1] == 308 ? 0 : 3;
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

    def test_multiple_typedef_aliases_report_capacity_and_allow_recovery(self):
        aliases = ", ".join(f"Alias{index}" for index in range(17))
        failing_source = f"typedef int {aliases};"
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-typedef-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            failure, _code_path, _data_path = self._compile(
                root, failing_source
            )
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                failing_source,
                """
                typedef struct Recovered { int value; }
                    Recovered, *RecoveredPointer;
                int main() {
                  Recovered value;
                  RecoveredPointer pointer = &value;
                  pointer->value = 23;
                  return value.value == 23 ? 0 : 1;
                }
                """,
            )

        self.assertEqual(failure.returncode, 2, failure.stdout + failure.stderr)
        self.assertIn("too many typedef aliases", failure.stderr)
        self.assertEqual(
            recovery.returncode,
            0,
            recovery.stdout + recovery.stderr,
        )

    def test_multiple_typedef_aliases_reject_a_trailing_comma_and_recover(self):
        failing_source = (
            "typedef struct Broken { int value; } Broken,;"
        )
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-typedef-trailing-comma-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            failure, _code_path, _data_path = self._compile(
                root, failing_source
            )
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                failing_source,
                """
                typedef struct { int value; } Value, *ValuePointer;
                int main() {
                  Value value;
                  ValuePointer pointer = &value;
                  pointer->value = 179;
                  return value.value == 179 ? 0 : 1;
                }
                """,
            )

        self.assertEqual(failure.returncode, 2, failure.stdout + failure.stderr)
        self.assertIn("expected typedef alias name", failure.stderr)
        self.assertNotIn("unexpected token", failure.stderr)
        self.assertEqual(
            recovery.returncode,
            0,
            recovery.stdout + recovery.stderr,
        )

    def test_fixed_array_typedefs_report_invalid_declarators_and_recover(self):
        cases = (
            (
                "zero",
                "typedef int Empty[0];",
                "array size must be positive",
            ),
            (
                "unsized",
                "typedef int Pending[];",
                "typedef array size is required",
            ),
            (
                "overflow",
                "typedef int Huge[1073741824];",
                "array allocation size overflow",
            ),
            (
                "incomplete-struct",
                "struct Pending; typedef struct Pending PendingArray[2];",
                "array of incomplete struct type",
            ),
            (
                "multidimensional",
                "typedef int Matrix[2][3];",
                "multidimensional typedef arrays are not supported",
            ),
            (
                "pointer-to-array",
                "typedef int Pair[2]; typedef Pair *PairPointer;",
                "pointer to typedef array is not supported",
            ),
            (
                "additional-dimension",
                "typedef int Pair[2]; Pair matrix[3];",
                "array declarator after typedef array is not supported",
            ),
        )

        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-array-typedef-{name}-",
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
                self.assertNotIn("unexpected token", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-array-typedef-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            recovery, _code_path, _data_path = self._compile_after_failure(
                Path(temporary),
                "typedef int Broken[0];",
                """
                typedef int Pair[2];
                int main() {
                  Pair values;
                  values[0] = 167;
                  values[1] = 173;
                  return values[0] + values[1] == 340 ? 0 : 1;
                }
                """,
            )
        self.assertEqual(
            recovery.returncode,
            0,
            recovery.stdout + recovery.stderr,
        )

    def test_fixed_array_typedefs_reject_unrepresented_type_uses(self):
        cases = (
            (
                "function-return",
                "typedef int Pair[2]; Pair broken() { return 0; }",
                "function return type cannot be an array",
            ),
            (
                "struct-function-return",
                "typedef struct Word { int value; } Words[2]; "
                "Words broken() { return 0; }",
                "function return type cannot be an array",
            ),
            (
                "method-return",
                "typedef int Pair[2]; "
                "class Broken { Pair Value() { return 0; } };",
                "method return type cannot be an array",
            ),
            (
                "cast-target",
                "typedef int Pair[2]; int main() { return (Pair)7; }",
                "cast target cannot be an array type",
            ),
            (
                "new-target",
                "typedef int Pair[2]; int main() { return new Pair; }",
                "new does not support typedef array types",
            ),
        )

        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-array-typedef-use-{name}-",
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
                self.assertNotIn("unexpected token", result.stderr)

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

    def test_unsigned_enumerators_and_unary_results_keep_their_type(self):
        result = self._compile_and_run(
            """
            enum RuntimeEdge { HighEnumerator = 0x80000000u };

            int main() {
              uint32_t high = 0x80000000u;
              if (!(HighEnumerator > 1u)) return 1;
              if ((double)HighEnumerator != 2147483648.0) return 2;
              if (!(~0u > 1u)) return 3;
              if (!(+high > 1u)) return 4;
              if (!(-1u > 1u)) return 5;
              if ((double)~0u != 4294967295.0) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_relations_survive_local_and_parameter_loads(self):
        result = self._compile_and_run(
            """
            typedef unsigned int RuntimeU32;

            int compare_edges(RuntimeU32 high, RuntimeU32 low) {
              if (!(high >= low)) return 1;
              if (!(high > low)) return 2;
              if (low >= high) return 3;
              if (low > high) return 4;
              if (!(low <= high)) return 5;
              if (!(low < high)) return 6;
              return 0;
            }

            int main() {
              RuntimeU32 high = 0x80000000u;
              uint32_t low = 1u;
              return compare_edges(high, low);
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_relations_survive_array_field_and_pointer_loads(self):
        result = self._compile_and_run(
            """
            typedef unsigned int RuntimeU32;
            typedef struct EdgeBox {
              RuntimeU32 value;
            } EdgeBox;

            RuntimeU32 edge_values[2];

            int pointer_is_above(RuntimeU32 *value, RuntimeU32 floor) {
              return *value > floor;
            }

            int main() {
              EdgeBox box;
              edge_values[0] = 0x80000000u;
              edge_values[1] = 0xffffffffu;
              box.value = edge_values[1];
              if (!(edge_values[0] >= 1u)) return 1;
              if (!(box.value > edge_values[0])) return 2;
              if (!pointer_is_above(&edge_values[0], 1u)) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_division_remainder_and_right_shift_execute(self):
        result = self._compile_and_run(
            """
            int main() {
              uint32_t maximum = 0xffffffffu;
              uint32_t high_bit = 0x80000000u;
              uint32_t quotient = maximum;
              uint32_t shifted = high_bit;

              if (maximum / 2u != 2147483647u) return 1;
              if (maximum % 2u != 1u) return 2;
              if (high_bit >> 31u != 1u) return 3;

              quotient /= 2u;
              shifted >>= 31u;
              if (quotient != 2147483647u) return 4;
              if (shifted != 1u) return 5;

              if (-9 / 2 != -4) return 6;
              if (-9 >> 1 != -5) return 7;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_compound_right_shift_uses_the_promoted_left_operand_type(self):
        result = self._compile_and_run(
            """
            int main() {
              int negative = -8;
              unsigned int unsigned_count = 1u;
              unsigned int high_bit = 0x80000000u;
              int signed_count = 31;

              negative >>= unsigned_count;
              if (negative != -4) return 1;

              high_bit >>= signed_count;
              if (high_bit != 1u) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_values_convert_exactly_to_float_and_double(self):
        result = self._compile_and_run(
            """
            double global_maximum_double = 0xffffffffu;
            float global_high_float = 0x80000000u;

            double take_double(double value) { return value; }
            float take_float(float value) { return value; }
            uint32_t return_maximum() { return 0xffffffffu; }

            int main() {
              uint32_t zero = 0u;
              uint32_t below_sign = 0x7fffffffu;
              uint32_t high_bit = 0x80000000u;
              uint32_t maximum = return_maximum();
              double zero_double = (double)zero;
              double below_double = below_sign;
              double high_double = (double)high_bit;
              double maximum_double = take_double(maximum);
              float high_float = (float)high_bit;
              float maximum_float = take_float(maximum);

              if (zero_double != 0.0) return 1;
              if (below_double != 2147483647.0) return 2;
              if (high_double != 2147483648.0) return 3;
              if (maximum_double != 4294967295.0) return 4;
              if (high_float != 2147483648.0f) return 5;
              if (maximum_float != 4294967296.0f) return 6;
              if (maximum + 0.25 != 4294967295.25) return 7;
              if (0.25 + maximum != 4294967295.25) return 8;
              if ((double)(maximum & 0x80000000u) != 2147483648.0)
                return 9;
              if ((double)(high_bit >> 1u) != 1073741824.0) return 10;
              if ((double)-3 != -3.0) return 11;
              if (global_maximum_double != 4294967295.0) return 12;
              if (global_high_float != 2147483648.0f) return 13;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_conditional_type_does_not_depend_on_arm_order(self):
        result = self._compile_and_run(
            """
            int main() {
              int choose_true = 1;
              if (!((choose_true ? 0xffffffffu : 1) > 2)) return 1;
              if (!((choose_true ? 1 : 0xffffffffu) < -1)) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sizeof_produces_unsigned_size_t_arithmetic(self):
        result = self._compile_and_run(
            """
            int main() {
              return sizeof(int) - 5 > 1 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_remainder_assignment_preserves_signedness_and_lvalue_identity(self):
        result = self._compile_and_run(
            """
            int global_signed = -31;
            uint32_t global_unsigned = 0xfffffffeu;
            int pointer_target = -23;
            int pointer_calls;
            int index_calls;
            int divisor_calls;

            struct Pair {
              int signed_value;
              uint32_t unsigned_value;
            };

            struct Pair primary_record;
            struct Pair secondary_record;
            struct Pair *selected_record;

            int *select_pointer_target() {
              pointer_calls += 1;
              return &pointer_target;
            }

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int next_divisor() {
              divisor_calls += 1;
              return 4;
            }

            int retarget_record() {
              selected_record = &secondary_record;
              return 6;
            }

            int main() {
              int signed_local = -17;
              uint32_t unsigned_local = 0xffffffffu;
              uint32_t unsigned_pointer_target = 0x80000005u;
              uint32_t *unsigned_pointer = &unsigned_pointer_target;
              int signed_values[2];
              uint32_t unsigned_values[2];

              signed_values[1] = -19;
              unsigned_values[0] = 0xfffffffeu;
              primary_record.signed_value = -29;
              primary_record.unsigned_value = 0x80000001u;
              secondary_record.signed_value = 88;
              selected_record = &primary_record;

              global_signed %= 7;
              global_unsigned %= 7;
              signed_local %= 5;
              unsigned_local %= 10;
              *select_pointer_target() %= 6;
              *unsigned_pointer %= 4;
              signed_values[next_index()] %= next_divisor();
              unsigned_values[0] %= 16;
              primary_record.unsigned_value %= 7;
              selected_record->signed_value %= retarget_record();

              if (global_signed != -3) return 1;
              if (global_unsigned != 2u) return 2;
              if (signed_local != -2) return 3;
              if (unsigned_local != 5u) return 4;
              if (pointer_target != -5 || pointer_calls != 1) return 5;
              if (unsigned_pointer_target != 1u) return 6;
              if (signed_values[1] != -3) return 7;
              if (index_calls != 1 || divisor_calls != 1) return 8;
              if (unsigned_values[0] != 14u) return 9;
              if (primary_record.unsigned_value != 3u) return 10;
              if (primary_record.signed_value != -5) return 11;
              if (secondary_record.signed_value != 88) return 12;
              if (selected_record != &secondary_record) return 13;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_remainder_assignment_reports_and_recovers(self):
        failing_source = """
            int main() {
              float value = 5.0f;
              value %= 2;
              return 0;
            }
        """
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-remainder-assignment-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            failure, _code_path, _data_path = self._compile(
                root, failing_source
            )
            self.assertEqual(
                failure.returncode,
                2,
                failure.stdout + failure.stderr,
            )
            self.assertIn(
                "remainder compound assignment requires an integer lvalue",
                failure.stderr,
            )

            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                failing_source,
                """
                int main() {
                  uint32_t value = 0xffffffffu;
                  value %= 10u;
                  return value == 5u ? 0 : 1;
                }
                """,
            )
            self.assertEqual(
                recovery.returncode,
                0,
                recovery.stdout + recovery.stderr,
            )
            run_result = self._run_i386(root, int(recovery.stdout.strip()))
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_unsigned_function_and_method_returns_convert_to_float_lanes(self):
        result = self._compile_and_run(
            """
            double maximum_as_double() { return 0xffffffffu; }
            float high_as_float() { return 0x80000000u; }

            class Converter {
              double Maximum() { return 0xffffffffu; }
              float High() { return 0x80000000u; }
            };

            int main() {
              Converter converter;
              Converter *pointer = &converter;
              if (maximum_as_double() != 4294967295.0) return 1;
              if (high_as_float() != 2147483648.0f) return 2;
              if (converter.Maximum() != 4294967295.0) return 3;
              if (pointer->High() != 2147483648.0f) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_values_convert_to_unsigned_words_across_runtime_forms(self):
        result = self._compile_and_run(
            """
            uint32_t global_from_float = 4294967040.0f;
            uint32_t global_from_double = 4294967295.9999995;

            uint32_t take_unsigned(uint32_t value) { return value; }
            uint32_t return_unsigned() { return 2147483648.75; }
            uint32_t block_static_unsigned() {
              static uint32_t value = 4294967295.75;
              return value;
            }

            struct Box { uint32_t value; };

            int main() {
              uint32_t cast_negative = (uint32_t)-0.9999999999999999;
              uint32_t cast_low = (uint32_t)2147483647.9999998;
              uint32_t initialized_high = 2147483648.75;
              uint32_t assigned_maximum;
              uint32_t pointed_value = 1u;
              uint32_t *pointer = &pointed_value;
              uint32_t values[3];
              struct Box box;

              assigned_maximum = 4294967295.9999995;
              *pointer = -0.75f;
              values[0] = 0.99999994f;
              values[1] = 2147483904.0f;
              values[2] = 4294967040.0f;
              box.value = 2147483649.75;

              if (cast_negative != 0u) return 1;
              if (cast_low != 0x7fffffffu) return 2;
              if (initialized_high != 0x80000000u) return 3;
              if (assigned_maximum != 0xffffffffu) return 4;
              if (pointed_value != 0u) return 5;
              if (values[0] != 0u) return 6;
              if (values[1] != 0x80000100u) return 7;
              if (values[2] != 0xffffff00u) return 8;
              if (box.value != 0x80000001u) return 9;
              if (take_unsigned(4294967295.75) != 0xffffffffu) return 10;
              if (return_unsigned() != 0x80000000u) return 11;
              if (block_static_unsigned() != 0xffffffffu) return 12;
              if (global_from_float != 0xffffff00u) return 13;
              if (global_from_double != 0xffffffffu) return 14;
              if ((uint32_t)-0.0 != 0u) return 15;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_unsigned_type_specifiers_report_and_recover(self):
        cases = (
            (
                "floating type",
                "unsigned double broken;",
                "unsigned requires an integer type",
            ),
            (
                "conflicting signs",
                "signed unsigned int broken;",
                "type cannot be both signed and unsigned",
            ),
        )
        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-unsigned-type-{name}-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-unsigned-type-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                "unsigned double broken;",
                """
                int main() {
                  uint32_t high = 0x80000000u;
                  return high > 1u ? 0 : 1;
                }
                """,
            )
            self.assertEqual(
                recovery.returncode,
                0,
                recovery.stdout + recovery.stderr,
            )
            run_result = self._run_i386(root, int(recovery.stdout.strip()))
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_unsupported_floating_to_word_forms_report_and_recover(self):
        cases = (
            (
                "compound assignment",
                "int main() { uint32_t value = 3u; value *= 0.5; return 0; }",
                "floating compound assignment to unsigned is not supported",
            ),
            (
                "pointer target",
                "int main() { uint32_t *value = (uint32_t *)1.5; return 0; }",
                "floating to pointer conversion is not supported",
            ),
            (
                "vector source",
                "int main() { double2 value; return (uint32_t)value; }",
                "conversion to unsigned requires a scalar word or floating value",
            ),
        )
        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix="private-cupidc-float-to-word-form-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-float-to-unsigned-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                "int main() { uint32_t value = 3u; value *= 0.5; return 0; }",
                """
                int main() {
                  uint32_t maximum = 4294967295.9999995;
                  return maximum == 0xffffffffu ? 0 : 1;
                }
                """,
            )
            self.assertEqual(
                recovery.returncode,
                0,
                recovery.stdout + recovery.stderr,
            )
            run_result = self._run_i386(root, int(recovery.stdout.strip()))
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
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

    def test_feature13_derived_aot_source_executes_through_the_elf_path(self):
        result = self._compile_and_run(
            (
                REPO_ROOT / "bin" / "feature13_derived_aot.cc"
            ).read_text(encoding="utf-8"),
            aot=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "[feature13-derived-aot] PASS score=%d once=%d zero=%x",
            result.stderr,
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
            "[feature14-matrix] PASS global=2 local=2 static=2 "
            "sizes=8 index=6 unevaluated=2 canary=4",
            "[feature14-update] PASS direct=6 leaves=3 once=6 payload=8",
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6",
            "[feature14-callback] PASS float4=4 double2=2 calls=2",
            "[feature14-callback-typedef] PASS float4=4 calls=1",
            "[feature14-callback-automatic] PASS local=4 method=4 calls=2",
            "[feature14-callback-field] PASS stored=1 copied=1 cleared=1 "
            "float4=4 calls=1",
            "[feature14-callback-field-call] PASS typedef=1 raw=1 "
            "float4=4 once=1 calls=2",
            "[feature14-callback-raw-array] PASS modes=2 phases=3 "
            "calls=12 stored=1 persistent=1",
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

    def test_whole_simd_updates_cover_direct_storage_and_postfix_payloads(self):
        result = self._compile_and_run(
            """
            float4 global_float;
            double2 global_double;

            int update_static_vectors() {
              static float4 saved_float;
              static double2 saved_double;
              static float4 saved_float_array[1];
              static double2 saved_double_matrix[1][1];
              float4 float_seed = {3.0f, 4.0f, 5.0f, 6.0f};
              double2 double_seed = {7.0, 8.0};
              saved_float = float_seed;
              saved_double = double_seed;
              saved_float_array[0] = float_seed;
              saved_double_matrix[0][0] = double_seed;
              ++saved_float;
              saved_double--;
              saved_float_array[0]--;
              ++saved_double_matrix[0][0];
              if (saved_float.x != 4.0f || saved_float.w != 7.0f)
                return 1;
              if (saved_double.x != 6.0 || saved_double.y != 7.0)
                return 2;
              if (saved_float_array[0].x != 2.0f ||
                  saved_float_array[0].w != 5.0f)
                return 3;
              if (saved_double_matrix[0][0].x != 8.0 ||
                  saved_double_matrix[0][0].y != 9.0)
                return 4;
              return 0;
            }

            int main() {
              int nan_bits = 0x7fc12345;
              float nan_value = *(float *)&nan_bits;
              float zero = 0.0f;
              float negative_zero = -zero;
              float4 float_seed = {
                nan_value, negative_zero, 10.0f, -4.0f
              };
              double double_nan;
              int *double_nan_bits = (int *)&double_nan;
              double_nan_bits[0] = 0x89abcdef;
              double_nan_bits[1] = 0x7ff81234;
              double2 double_seed = {double_nan, -0.0};
              float4 old_float;
              double2 old_double;
              float old_float_nan;
              float old_float_zero;
              double old_double_nan;
              double old_double_zero;
              int *old_double_bits;
              float4 local_float;
              double2 local_double;

              local_float = float_seed;
              local_double = double_seed;
              ++local_float;
              local_double--;
              if (local_float.z != 11.0f || local_float.w != -3.0f)
                return 1;
              if (local_double.y != -1.0) return 2;

              global_float = float_seed;
              global_double = double_seed;
              old_float = global_float++;
              old_double = global_double--;

              old_float_nan = old_float.x;
              old_float_zero = old_float.y;
              if (*(int *)&old_float_nan != nan_bits) return 3;
              if (*(int *)&old_float_zero != (int)0x80000000) return 4;
              if (global_float.z != 11.0f || global_float.w != -3.0f)
                return 5;

              old_double_nan = old_double.x;
              old_double_zero = old_double.y;
              old_double_bits = (int *)&old_double_nan;
              if (old_double_bits[0] != 0x89abcdef ||
                  old_double_bits[1] != 0x7ff81234)
                return 6;
              old_double_bits = (int *)&old_double_zero;
              if (old_double_bits[0] != 0 ||
                  old_double_bits[1] != (int)0x80000000)
                return 7;
              if (global_double.y != -1.0) return 8;
              if (update_static_vectors() != 0) return 9;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_whole_simd_updates_cover_indexed_array_leaves_once(self):
        result = self._compile_and_run(
            """
            float4 global_line[2];
            float4 global_payload[1];
            double2 global_matrix[2][2];
            float4 global_cube[2][2][2];
            int outer_calls;
            int middle_calls;
            int inner_calls;

            int next_outer() { outer_calls += 1; return 1; }
            int next_middle() { middle_calls += 1; return 0; }
            int next_inner() { inner_calls += 1; return 1; }

            int main() {
              float4 float_seed = {2.0f, 4.0f, 6.0f, 8.0f};
              double2 double_seed = {10.0, 20.0};
              float4 old_float;
              float4 old_payload;
              double2 old_double;
              int nan_bits = 0x7fc54321;
              float nan_value = *(float *)&nan_bits;
              float zero = 0.0f;
              float negative_zero = -zero;
              float4 payload_seed = {
                nan_value, negative_zero, 12.0f, -8.0f
              };
              float old_payload_nan;
              float old_payload_zero;

              global_line[1] = float_seed;
              global_payload[0] = payload_seed;
              global_matrix[1][0] = double_seed;
              global_cube[1][0][1] = float_seed;

              old_float = global_line[next_outer()]++;
              old_payload = global_payload[0]++;
              old_double = --global_matrix[next_outer()][next_middle()];
              ++global_cube[next_outer()][next_middle()][next_inner()];

              if (old_float.x != 2.0f || old_float.w != 8.0f) return 1;
              old_payload_nan = old_payload.x;
              old_payload_zero = old_payload.y;
              if (*(int *)&old_payload_nan != nan_bits) return 2;
              if (*(int *)&old_payload_zero != (int)0x80000000) return 3;
              if (global_line[1].x != 3.0f ||
                  global_line[1].w != 9.0f)
                return 4;
              if (old_double.x != 9.0 || old_double.y != 19.0) return 5;
              if (global_matrix[1][0].x != 9.0 ||
                  global_matrix[1][0].y != 19.0)
                return 6;
              if (global_cube[1][0][1].x != 3.0f ||
                  global_cube[1][0][1].w != 9.0f)
                return 7;
              if (outer_calls != 3 || middle_calls != 2 || inner_calls != 1)
                return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_whole_simd_updates_execute_through_private_aot(self):
        result = self._compile_and_run(
            """
            float4 values[1];
            int main() {
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 old;
              values[0] = seed;
              old = values[0]++;
              if (old.x != 1.0f || old.w != 4.0f) return 1;
              if (values[0].x != 2.0f || values[0].w != 5.0f) return 2;
              return 0;
            }
            """,
            aot=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_whole_simd_updates_keep_persistent_state(self):
        result = self._compile_repl_and_run(
            (
                "float4 repl_float;",
                "double2 repl_double[1];",
                """
                int update_repl_vectors() {
                  float4 float_seed = {4.0f, 5.0f, 6.0f, 7.0f};
                  double2 double_seed = {8.0, 9.0};
                  float4 old_float;
                  repl_float = float_seed;
                  repl_double[0] = double_seed;
                  old_float = repl_float++;
                  --repl_double[0];
                  if (old_float.x != 4.0f || old_float.w != 7.0f)
                    return 1;
                  return 0;
                }
                """,
                """
                int verify_repl_vectors() {
                  if (update_repl_vectors() != 0) return 1;
                  if (repl_float.x != 5.0f || repl_float.w != 8.0f)
                    return 2;
                  if (repl_double[0].x != 7.0 ||
                      repl_double[0].y != 8.0)
                    return 3;
                  return 0;
                }
                verify_repl_vectors;
                """,
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_const_simd_update_is_rejected_before_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-const-simd-update-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    Path(temporary),
                    (
                        "typedef const float4 ReplConstVector; "
                        "ReplConstVector repl_value;",
                        "repl_value++;",
                        "ReplConstVector preserved_value; preserved_value.x;",
                        "float4 mutable_value; ++mutable_value;",
                    ),
                )
            )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "SIMD increment or decrement requires a modifiable "
            "whole-vector lvalue",
            result.stderr,
        )

    def test_const_simd_assignments_recover_in_the_same_state(self):
        cases = (
            (
                "direct assignment",
                """
                int main() {
                  const float4 destination;
                  float4 source;
                  destination = source;
                  return 0;
                }
                """,
            ),
            (
                "indexed compound assignment",
                """
                int main() {
                  const double2 destination[1][1];
                  double2 source;
                  destination[0][0] += source;
                  return 0;
                }
                """,
            ),
        )
        retry_source = "int main() { float4 value; value += value; return 0; }"
        for label, failing_source in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-simd-assignment-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    failing_source,
                    retry_source,
                    same_state=True,
                )
            self.assertEqual(
                result.returncode,
                0,
                f"{label}: {result.stdout}{result.stderr}",
            )
            self.assertIn(
                "SIMD assignment requires a modifiable whole-vector lvalue",
                result.stderr,
                label,
            )

    def test_typedef_const_simd_values_remain_readable(self):
        result = self._compile_and_run(
            """
            typedef const float4 ConstVector;
            typedef ConstVector ConstVectorAlias;

            int main() {
              ConstVectorAlias value = {1.0f, 2.0f, 3.0f, 4.0f};
              ConstVectorAlias values[1];
              float4 copy;
              copy = value;
              if (copy.x != 1.0f || copy.w != 4.0f) return 1;
              copy = values[0];
              if (copy.x != 0.0f || copy.w != 0.0f) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_typedef_const_simd_mutations_recover_in_the_same_state(self):
        assignment_message = (
            "SIMD assignment requires a modifiable whole-vector lvalue"
        )
        update_message = (
            "SIMD increment or decrement requires a modifiable "
            "whole-vector lvalue"
        )
        cases = (
            (
                "alias-chain direct assignment",
                """
                typedef const float4 ConstVector;
                typedef ConstVector ConstVectorAlias;
                int main() {
                  ConstVectorAlias destination;
                  float4 source;
                  destination = source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "direct compound assignment",
                """
                typedef const double2 ConstVector;
                int main() {
                  ConstVector destination;
                  double2 source;
                  destination += source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "direct prefix update",
                """
                typedef const float4 ConstVector;
                int main() {
                  ConstVector value;
                  ++value;
                  return 0;
                }
                """,
                update_message,
            ),
            (
                "indexed assignment",
                """
                typedef const float4 ConstVector;
                int main() {
                  ConstVector values[1];
                  float4 source;
                  values[0] = source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "alias-chain indexed compound assignment",
                """
                typedef const double2 ConstVector;
                typedef ConstVector ConstVectorAlias;
                int main() {
                  ConstVectorAlias values[1][1];
                  double2 source;
                  values[0][0] += source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "typedef-array indexed postfix update",
                """
                typedef const float4 ConstVector;
                typedef ConstVector ConstVectorArray[1];
                int main() {
                  ConstVectorArray values;
                  values[0]++;
                  return 0;
                }
                """,
                update_message,
            ),
        )
        retry_source = "int main() { float4 value; value++; return 0; }"
        for label, failing_source, message in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-typedef-const-simd-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    failing_source,
                    retry_source,
                    same_state=True,
                )
            self.assertEqual(
                result.returncode,
                0,
                f"{label}: {result.stdout}{result.stderr}",
            )
            self.assertIn(message, result.stderr, label)

    def test_simd_update_rejections_recover_in_the_same_state(self):
        cases = (
            (
                "const automatic vector",
                """
                int main() {
                  const float4 value = {1, 2, 3, 4};
                  ++value;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "trailing-const global vector",
                """
                double2 const value;
                int main() {
                  value--;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "const block-static vector",
                """
                int main() {
                  static const double2 value;
                  value++;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "const one-dimensional vector leaf",
                """
                int main() {
                  const float4 values[1];
                  values[0]--;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "const three-dimensional vector leaf",
                """
                int main() {
                  const float4 values[1][1][1];
                  --values[0][0][0];
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "computed vector",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 right = {5, 6, 7, 8};
                  (left + right)++;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "vector lane",
                """
                int main() {
                  float4 value = {1, 2, 3, 4};
                  value.x++;
                  return 0;
                }
                """,
                "SIMD lane increment or decrement is not supported",
            ),
            (
                "incomplete row",
                """
                int main() {
                  double2 values[2][2];
                  values[0]++;
                  return 0;
                }
                """,
                "SIMD array row values are not supported",
            ),
            (
                "prefix incomplete row",
                """
                int main() {
                  float4 values[2][2];
                  ++values[0];
                  return 0;
                }
                """,
                "SIMD array row values are not supported",
            ),
            (
                "record field",
                """
                struct Sample { float4 value; };
                int main() {
                  struct Sample sample;
                  sample.value++;
                  return 0;
                }
                """,
                "SIMD record-field increment or decrement is not supported",
            ),
            (
                "SIMD pointer",
                "float4 *values;",
                "SIMD pointer types are not supported",
            ),
            (
                "call result",
                """
                float4 make_value() {
                  float4 value = {1, 2, 3, 4};
                  return value;
                }
                int main() { return make_value()++; }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
        )
        retry_source = "int main() { float4 value = {1, 2, 3, 4}; ++value; return value.x != 2.0f; }"
        for label, failing_source, message in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-update-recovery-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    failing_source,
                    retry_source,
                    same_state=True,
                )
            self.assertEqual(
                result.returncode,
                0,
                f"{label}: {result.stdout}{result.stderr}",
            )
            self.assertIn(message, result.stderr, label)

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

    def test_invalid_derived_updates_report_useful_diagnostics(self):
        cases = (
            (
                "void probe(int *value) { ++*value; }",
                "indirect increment or decrement is not supported",
            ),
            (
                "void probe(float value) { ++(1.0f + value); }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(float value) { ++sizeof(value); }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(int index) { float values[2]; ++&values[index]; }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(int index) { float values[2][2]; ++values[index]; }",
                "increment or decrement requires a scalar variable",
            ),
        )
        for source, diagnostic in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-derived-update-error-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)

    def test_rejected_derived_update_allows_recovery_in_the_same_compiler_state(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-derived-update-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                "void probe(int *value) { ++*value; }",
                """
                float value;

                int main() {
                  float *pointer = &value;
                  value = 2.0f;
                  return ++*pointer == 3.0f && value == 3.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            run_result = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_indirect_floating_updates_preserve_results_and_evaluate_once(self):
        result = self._compile_and_run(
            """
            float pointed_float = -0.0f;
            double pointed_double = 8.5;
            int pointer_calls;

            float *next_float_pointer() {
              pointer_calls += 1;
              return &pointed_float;
            }

            double *next_double_pointer() {
              pointer_calls += 1;
              return &pointed_double;
            }

            int main() {
              float old_float = (*next_float_pointer())++;
              double new_double = --*next_double_pointer();

              if (pointer_calls != 2) return 1;
              if (*(int *)&old_float != (int)0x80000000) return 2;
              if (pointed_float != 1.0f) return 3;
              if (new_double != 7.5) return 4;
              if (pointed_double != 7.5) return 5;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_indexed_floating_updates_keep_the_selected_element(self):
        result = self._compile_and_run(
            """
            float singles[2];
            double doubles[2];
            int index_calls;

            int next_index() {
              int result = index_calls;
              index_calls += 1;
              return result;
            }

            int main() {
              singles[0] = 2.25f;
              doubles[1] = 4.5;

              float new_single = ++singles[next_index()];
              double old_double = doubles[next_index()]--;

              if (index_calls != 2) return 1;
              if (new_single != 3.25f || singles[0] != 3.25f) return 2;
              if (old_double != 4.5 || doubles[1] != 3.5) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_member_floating_updates_keep_the_selected_storage(self):
        result = self._compile_and_run(
            """
            struct Sample {
              float single;
              double wide;
              float taps[2];
              double nan_value;
            };

            struct Sample sample;
            int record_calls;
            int index_calls;

            struct Sample *next_sample() {
              record_calls += 1;
              return &sample;
            }

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              int nan_words[2];
              int *old_nan_words;
              sample.single = 1.5f;
              sample.wide = 4.25;
              sample.taps[1] = -0.0f;
              nan_words[0] = 0x12345678;
              nan_words[1] = 0x7ff81234;
              sample.nan_value = *(double *)nan_words;

              float new_single = ++next_sample()->single;
              double old_wide = sample.wide--;
              float old_tap = sample.taps[next_index()]++;
              double old_nan = sample.nan_value++;
              old_nan_words = (int *)&old_nan;

              if (record_calls != 1 || index_calls != 1) return 1;
              if (new_single != 2.5f || sample.single != 2.5f) return 2;
              if (old_wide != 4.25 || sample.wide != 3.25) return 3;
              if (*(int *)&old_tap != (int)0x80000000) return 4;
              if (sample.taps[1] != 1.0f) return 5;
              if (old_nan_words[0] != 0x12345678 ||
                  old_nan_words[1] != 0x7ff81234) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_indexed_record_updates_evaluate_each_index_once_in_both_modes(
        self,
    ):
        source = """
            struct Reading {
              float single;
              double wide;
            };

            struct Reading records[2];
            int index_calls;

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              records[1].single = -0.0f;
              records[1].wide = 6.25;

              float new_single = ++records[next_index()].single;
              if (index_calls != 1) return 1;
              index_calls = 0;
              double old_wide = records[next_index()].wide--;
              if (index_calls != 1) return 2;

              if (new_single != 1.0f) return 3;
              if (records[1].single != 1.0f) return 4;
              if (old_wide != 6.25 || records[1].wide != 5.25) return 5;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(mode="aot" if aot else "jit"):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )

    def test_derived_floating_update_statements_use_the_same_lvalue_path(self):
        result = self._compile_and_run(
            """
            struct Sample { double value; };

            float values[1];
            struct Sample sample;
            int index_calls;
            int initializer_calls;

            int next_index() {
              index_calls += 1;
              return 0;
            }

            int begin_loop() {
              initializer_calls += 1;
              return 0;
            }

            int main() {
              int loop_count = 0;
              values[0] = 1.25f;
              sample.value = 6.5;
              values[next_index()]++;
              --values[0];
              values[0]--;
              ++values[0];
              sample.value--;
              ++sample.value;
              sample.value++;
              --sample.value;
              for (begin_loop(); loop_count < 2; values[0]++)
                loop_count += 1;
              if (initializer_calls != 1 || index_calls != 1 ||
                  values[0] != 3.25f) return 1;
              return sample.value == 6.5 ? 0 : 2;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_grouped_direct_floating_updates_keep_lvalue_identity(self):
        result = self._compile_and_run(
            """
            int main() {
              float single = 2.5f;
              double wide = 6.5;
              float old_single = (single)++;
              double old_wide = ((wide))--;
              float new_single = ++(single);
              double new_wide = --((wide));

              if (old_single != 2.5f || single != 4.5f) return 1;
              if (old_wide != 6.5 || wide != 4.5) return 2;
              if (new_single != 4.5f || new_wide != 4.5) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

    def test_repl_simd_matrices_keep_row_metadata_between_units(self):
        result = self._compile_repl_and_run(
            (
                "float4 repl_matrix[2][3];",
                "double2 repl_cube[2][2][1];",
                "int repl_canary = 53;",
                """
                int verify_repl_vectors() {
                  float4 float_seed = {2.0f, 4.0f, 6.0f, 8.0f};
                  double2 double_seed = {3.0, 9.0};
                  repl_matrix[1][2] = float_seed;
                  repl_matrix[1][2] *= float_seed;
                  repl_cube[1][0][0] = double_seed;
                  repl_cube[1][0][0] += double_seed;
                  if (repl_matrix[1][2].z != 36.0f) return 1;
                  if (repl_cube[1][0][0].y != 18.0) return 2;
                  if (sizeof(*repl_matrix) != 48) return 3;
                  if (sizeof(**repl_matrix) != 16) return 4;
                  if (sizeof(*repl_cube) != 32) return 5;
                  if (sizeof(**repl_cube) != 16) return 6;
                  if (sizeof(***repl_cube) != 16) return 7;
                  if (repl_canary != 53) return 8;
                  return 0;
                }
                """,
                "verify_repl_vectors;",
            )
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
            ("global SIMD zero inner bound", "float4 values[2][0];"),
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
            (
                "local SIMD negative third bound",
                "int main() { double2 values[2][2][1 - 2]; return 0; }",
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
                "global two dimensional SIMD",
                "float4 values[32768][4096];",
            ),
            (
                "block static three dimensional SIMD",
                "int main() { static double2 values[4096][4096][8]; return 0; }",
            ),
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

    def test_two_dimensional_simd_arrays_keep_vector_rows(self):
        result = self._compile_and_run(
            """
            int leading_canary = 31;
            float4 vectors[2][2];
            int trailing_canary = 37;
            int sizeof_index_calls;

            int next_sizeof_index() {
              sizeof_index_calls += 1;
              return 1;
            }

            int main() {
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              vectors[1][0] = seed;
              vectors[1][0] += seed;
              if (vectors[1][0].x != 2.0f) return 1;
              if (vectors[1][0].w != 8.0f) return 2;
              if (vectors[0][1].z != 0.0f) return 3;
              if (sizeof(*vectors) != 32) return 4;
              if (sizeof(**vectors) != 16) return 5;
              if (sizeof(vectors[next_sizeof_index()]) != 32) return 6;
              if (sizeof(vectors[0][next_sizeof_index()]) != 16) return 7;
              if (sizeof_index_calls != 0) return 8;
              if (leading_canary != 31) return 9;
              if (trailing_canary != 37) return 10;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_matrices_cover_local_static_and_three_dimensional_storage(self):
        result = self._compile_and_run(
            """
            float4 global_cube[2][2][2];
            int global_canary = 41;
            int outer_calls;
            int middle_calls;
            int inner_calls;

            int next_outer() {
              outer_calls += 1;
              return 1;
            }

            int next_middle() {
              middle_calls += 1;
              return 0;
            }

            int next_inner() {
              inner_calls += 1;
              return 1;
            }

            int touch_static_cube() {
              static double2 saved_cube[2][2][2];
              double2 seed = {3.0, 9.0};
              saved_cube[1][1][0] += seed;
              if (saved_cube[1][1][0].x != 3.0) return 1;
              if (saved_cube[1][1][0].y != 9.0) return 2;
              if (sizeof(*saved_cube) != 64) return 3;
              if (sizeof(**saved_cube) != 32) return 4;
              if (sizeof(***saved_cube) != 16) return 5;
              return 0;
            }

            int main() {
              int leading_canary = 43;
              double2 local_matrix[2][3];
              int trailing_canary = 47;
              float4 seed = {8.0f, 12.0f, 18.0f, 24.0f};
              float4 step = {2.0f, 3.0f, 6.0f, 8.0f};
              double2 local_seed = {20.0, 30.0};
              double2 local_step = {4.0, 5.0};

              global_cube[1][0][1] = seed;
              global_cube[next_outer()][next_middle()][next_inner()] += step;
              global_cube[1][0][1] -= step;
              global_cube[1][0][1] *= step;
              global_cube[1][0][1] /= step;
              local_matrix[1][2] = local_seed;
              local_matrix[1][2] += local_step;
              local_matrix[1][2] -= local_step;
              local_matrix[1][2] *= local_step;
              local_matrix[1][2] /= local_step;

              if (global_cube[next_outer()][next_middle()][next_inner()].x !=
                  8.0f)
                return 1;
              if (outer_calls != 2 || middle_calls != 2 || inner_calls != 2)
                return 2;
              if (global_cube[1][0][1].w != 24.0f) return 3;
              if (global_cube[0][1][1].z != 0.0f) return 4;
              if (local_matrix[1][2].x != 20.0) return 5;
              if (local_matrix[1][2].y != 30.0) return 6;
              if (sizeof(*global_cube) != 64) return 7;
              if (sizeof(**global_cube) != 32) return 8;
              if (sizeof(***global_cube) != 16) return 9;
              if (sizeof(*local_matrix) != 48) return 10;
              if (sizeof(**local_matrix) != 16) return 11;
              if (touch_static_cube() != 0) return 12;
              if (leading_canary != 43 || trailing_canary != 47) return 13;
              if (global_canary != 41) return 14;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unit_inner_simd_dimensions_retain_array_rank(self):
        result = self._compile_and_run(
            """
            float4 global_matrix[2][1];

            int main() {
              static float4 saved[2][1];
              double2 local_cube[2][2][1];
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 wide = {5.0, 7.0};

              global_matrix[1][0] = seed;
              global_matrix[1][0] += seed;
              saved[1][0] = seed;
              local_cube[1][0][0] = wide;
              local_cube[1][0][0] *= wide;

              if (global_matrix[1][0].x != 2.0f) return 1;
              if (global_matrix[1][0].w != 8.0f) return 2;
              if (local_cube[1][0][0].x != 25.0) return 3;
              if (local_cube[1][0][0].y != 49.0) return 4;
              if (sizeof(*global_matrix) != 16) return 5;
              if (sizeof(**global_matrix) != 16) return 6;
              if (sizeof(global_matrix[0]) != 16) return 7;
              if (sizeof((global_matrix[0])) != 16) return 8;
              if (sizeof(*local_cube) != 32) return 9;
              if (sizeof(**local_cube) != 16) return 10;
              if (sizeof(***local_cube) != 16) return 11;
              if (saved[1][0].x != 1.0f) return 12;
              if (saved[1][0].w != 4.0f) return 13;
              if (sizeof(*saved) != 16) return 14;
              if (sizeof(**saved) != 16) return 15;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_array_row_assignment_requires_every_subscript(self):
        cases = (
            "int main() { float4 values[2][2]; float4 value; "
            "values[1] = value; return 0; }",
            "int main() { float4 values[2][1]; float4 value; "
            "values[1] = value; return 0; }",
            "int main() { double2 values[2][2][2]; double2 value; "
            "values[1][0] = value; return 0; }",
            "int main() { double2 values[2][2][1]; double2 value; "
            "values[1][0] = value; return 0; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-row-assignment-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "SIMD array assignment requires every subscript",
                result.stderr,
            )

    def test_parenthesized_simd_rows_continue_subscript_chains(self):
        result = self._compile_and_run(
            """
            float4 matrix[2][2];
            double2 cube[2][2][2];

            int main() {
              float4 float_seed = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 double_seed = {5.0, 7.0};

              matrix[1][0] = float_seed;
              cube[1][0][1] = double_seed;

              if ((matrix[1])[0].z != 3.0f) return 1;
              if (((cube[1])[0])[1].y != 7.0) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_array_rows_do_not_escape_as_untyped_pointers(self):
        cases = (
            "int main() { float4 values[2][2]; return values[0] + 2 != "
            "values[1]; }",
            "int main() { double2 values[2][2][2]; "
            "return values[0][0] + 2 != values[0][1]; }",
            "int main() { float4 values[2][2]; if (values[0]) return 1; "
            "return 0; }",
            "int main() { float4 values[2][2]; return !values[0]; }",
            "int main() { float4 values[2][2]; return (int*)values[0] "
            "!= 0; }",
            "int main() { float4 values[2][2]; return (values[0]); }",
            "int main() { float4 values[2][2]; return (values[0]) + 2 "
            "!= values[1]; }",
            "int consume_row(int value) { return value; } "
            "int main() { float4 values[2][2]; "
            "return consume_row((values[0])); }",
            "int main() { float4 values[2][2]; return (int*)(values[0]) "
            "!= 0; }",
            "int main() { float4 values[2][2]; if ((values[0])) return 1; "
            "return 0; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-row-escape-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "SIMD array row values are not supported",
                result.stderr,
            )

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

    def test_simd_argument_mismatch_reports_the_fixed_type(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-argument-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                void consume(float4 value) {
                }

                int invalid_call() {
                  double2 value = {1.0, 2.0};
                  consume(value);
                  return 0;
                }
                """,
                """
                float4 identity(float4 value) {
                  return value;
                }

                int main() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 result;
                  result = identity(value);
                  return result.w == 4.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(Path(temporary), entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_variadic_simd_rejection_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-variadic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume(float4 fixed, ...);
                float4 fixed_value;
                float4 invalid_value;
                consume(fixed_value, invalid_value);
                """,
                """
                float4 identity(float4 value) {
                  return value;
                }

                int main() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 result;
                  result = identity(value);
                  return result.z == 3.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD call arguments require a fixed parameter type",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_variadic_call_accepts_fixed_simd_and_scalar_tail_values(self):
        result = self._compile_and_run(
            """
            int inspect(float4 narrow, double2 wide, int marker, ...) {
              if (narrow.x != 1.0f || narrow.w != 4.0f) return 1;
              if (wide.x != 5.0 || wide.y != 7.0) return 2;
              return marker == 9 ? 0 : 3;
            }

            int main() {
              float4 narrow = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 wide = {5.0, 7.0};
              return inspect(narrow, wide, 9, 11, 2.5f);
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unprototyped_simd_rejection_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-unprototyped-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume();
                double2 invalid_value;
                consume(invalid_value);
                """,
                """
                double2 identity(double2 value) {
                  return value;
                }

                int main() {
                  double2 value = {3.0, 5.0};
                  double2 result;
                  result = identity(value);
                  return result.y == 5.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD call arguments require a fixed parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_typed_function_pointer_simd_argument_uses_declared_slot(self):
        result = self._compile_and_run(
            """
            float observed;

            void consume(float4 value) {
              observed = value.w;
            }

            int main() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              void (*callback)(float4 value) = consume;
              callback(value);
              return observed == 4.0f ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_erased_pointer_simd_argument_is_rejected_without_metadata(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-function-pointer-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume(float4 value) {
                }

                int invalid_call() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  void *callback = consume;
                  callback(value);
                  return 0;
                }
                """,
                """
                float4 identity(float4 value) {
                  return value;
                }

                int main() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 result;
                  result = identity(value);
                  return result.w == 4.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD call arguments require a fixed parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_prototyped_function_pointer_simd_result_uses_xmm0(self):
        result = self._compile_and_run(
            """
            float4 make_value() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              return value;
            }

            int main() {
              float4 (*callback)(void) = make_value;
              float4 result;
              result = callback();
              return result.z == 3.0f ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_function_pointer_simd_result_is_rejected_without_metadata(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-function-pointer-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                float4 make_value() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  return value;
                }

                int invalid_call() {
                  float4 (*callback)() = make_value;
                  float4 result;
                  result = callback();
                  return result.x == 1.0f ? 0 : 1;
                }
                """,
                """
                double2 identity(double2 value) {
                  return value;
                }

                int main() {
                  double2 value = {3.0, 5.0};
                  double2 result;
                  result = identity(value);
                  return result.y == 5.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD function-pointer returns are not supported",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_bare_simd_return_recovers_with_a_focused_diagnostic(self):
        cases = (
            ("float4", "float4"),
            ("double2", "double2"),
        )
        retry_source = """
            float4 identity(float4 value) {
              return value;
            }

            int main() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 result;
              result = identity(value);
              return result.z == 3.0f ? 0 : 1;
            }
        """
        for label, return_type in cases:
            with self.subTest(return_type=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-bare-simd-return-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    f"""
                    {return_type} invalid_return() {{
                      return;
                    }}
                    """,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "SIMD return requires a matching float4 or double2 value",
                    result.stderr,
                )
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    0,
                    runtime.stdout + runtime.stderr,
                )

    def test_mismatched_simd_return_recovers_with_a_focused_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-return-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                float4 invalid_return() {
                  double2 value = {1.0, 2.0};
                  return value;
                }
                """,
                """
                double2 identity(double2 value) {
                  return value;
                }

                int main() {
                  double2 value = {3.0, 5.0};
                  double2 result;
                  result = identity(value);
                  return result.y == 5.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD return requires a matching float4 or double2 value",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_aggregate_parameter_keeps_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-aggregate-parameter-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                struct Pair {
                  int first;
                  int second;
                };

                void consume(struct Pair value) {
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cdecl parameter type is not supported", result.stderr)


if __name__ == "__main__":
    unittest.main()

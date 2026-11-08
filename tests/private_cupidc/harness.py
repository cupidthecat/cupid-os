"""Shared native driver and execution helpers."""

import os
import re
import shutil
import struct
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

from tests.test_private_cupidc_float_truth_emitter import _compiler_command
from tests.windows_i386_oracle import WindowsI386Oracle


REPO_ROOT = Path(__file__).resolve().parents[2]
LEXER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_lex.cc"
PARSER_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_parse.cc"
ELF_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc_elf.cc"
KERNEL_LANG = REPO_ROOT / "kernel" / "lang"

STORAGE_BINDING_NAMES = (
    "vfs_open", "vfs_close", "vfs_read", "vfs_write", "vfs_seek",
    "vfs_mkdir", "vfs_unlink", "vfs_rename",
    "swap_init", "swap_kmalloc", "swap_pin", "swap_unpin", "swap_free",
)
NETWORK_BINDING_NAMES = (
    "close", "dns_resolve", "htonl", "ip_parse", "listen", "ntohl",
    "recv", "send", "setsockopt", "sock_avail", "sock_state", "socket",
)
HALFWORD_BINDING_NAMES = (
    "htons", "ntohs", "bind", "connect", "accept", "sendto", "recvfrom", "udp_send_raw",
)


def _native_binding_registration(names, address_base, function_name):
    """Run the real registrations against fixed-address native ABI oracles."""
    source = (KERNEL_LANG / "cupidc.cc").read_text(encoding="utf-8")
    start = source.index("#define BIND_LEGACY_RESULT(")
    end = source.index("  /* Console output */", start)
    fragments = [source[start:end]]
    for index, name in enumerate(names):
        pointer = "p_" + name
        declaration = re.search(
            rf"(?m)^  (?P<result>[^\n;]+?)\s*\(\*{pointer}\)"
            rf"\((?P<parameters>[^;]+?)\)\s*=\s*[^;]+;",
            source,
        )
        registration = re.search(
            rf'(?m)^  BIND(?:_T|_FIXED)?\("{name}",\s*{pointer},[^;]+;',
            source,
        )
        if declaration is None or registration is None:
            raise AssertionError(f"missing native declaration for {name}")
        result = declaration.group("result").strip()
        parameters = declaration.group("parameters")
        address = address_base + 0x80 * index
        fragments.append(
            f"  {result} (*{pointer})({parameters}) = "
            f"({result} (*)({parameters}))(uintptr_t)0x{address:08x}u;\n"
            + registration.group(0)
        )
    fragments.extend(
        "#undef " + name
        for name in (
            "BIND_RETAINED", "BIND_VARIADIC", "BIND_FIXED", "BIND_SIGNATURE",
            "BIND", "BIND_T", "BIND_LEGACY_VOID", "BIND_LEGACY_RESULT",
        )
    )
    return (
        f"static void {function_name}(cc_state_t *cc) {{\n"
        + "\n".join(fragments) + "\n}\n"
    )


def _storage_binding_registration():
    return "typedef uint32_t swap_handle_t;\n" + _native_binding_registration(
        STORAGE_BINDING_NAMES, 0x01001400, "bind_storage_kernels"
    )


def _network_binding_registration():
    return _native_binding_registration(
        NETWORK_BINDING_NAMES, 0x01002000, "bind_network_kernels"
    )


def _halfword_binding_registration():
    registration = _native_binding_registration(
        HALFWORD_BINDING_NAMES, 0x01002800, "bind_halfword_kernels"
    )
    return registration.replace("#undef BIND_FIXED", '''
  uint16_t (*p_Device_ReadPort)(void *) = (uint16_t (*)(void *))(uintptr_t)0x01002c00u;
  BIND_FIXED("Device_ReadPort", p_Device_ReadPort, 1, TYPE_USHORT, TYPE_PTR);
  int16_t (*p_Device_ReadSigned)(void *) = (int16_t (*)(void *))(uintptr_t)0x01002c80u;
  BIND_FIXED("Device_ReadSigned", p_Device_ReadSigned, 1, TYPE_SHORT, TYPE_PTR);
#undef BIND_FIXED''')


def _halfword_native_assembly():
    # Read complete cdecl slots independently of the descriptors, and poison
    # unused return bits so uint16_t result conversion cannot pass by accident.
    swap = "cmpl $4660, 4(%esp)\n  jne 9f\n  movl $0xa5a53412, %eax\n  ret"
    endpoint = ("cmpl $31, 4(%esp)\n  jne 9f\n"
                "  cmpl $0x80000001, 8(%esp)\n  jne 9f\n"
                "  cmpl $4660, 12(%esp)\n  jne 9f")
    peer = ("movl 8(%esp), %ecx\n  movl $0x80000002, (%ecx)\n"
            "  movl 12(%esp), %ecx\n  movw $65000, (%ecx)")
    recv_peer = ("movl 16(%esp), %ecx\n  movl $0x80000003, (%ecx)\n"
                 "  movl 20(%esp), %ecx\n  movw $65001, (%ecx)")
    cases = (
        (swap, None), (swap, None), (endpoint, -7), (endpoint, -8),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  " + peer, 43),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  cmpl $0x80000003, 12(%esp)\n  jne 9f\n"
         "  cmpl $0x80000001, 16(%esp)\n  jne 9f\n  cmpl $4660, 20(%esp)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f", 47),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  cmpl $0x80000004, 12(%esp)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f\n  movb $67, (%ecx)\n  " + recv_peer, -9),
        ("cmpl $0x80000001, 4(%esp)\n  jne 9f\n  cmpl $4660, 8(%esp)\n  jne 9f\n"
         "  cmpl $65535, 12(%esp)\n  jne 9f\n  cmpl $0x80000005, 20(%esp)\n  jne 9f\n"
         "  movl 16(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f", -10),
    )
    return "\n".join(
        f".org 0x{0x2800 + index * 0x80:x}\nhalfword_{name}:\n  {checks}\n"
        + (f"  movl ${result}, %eax\n  ret\n" if result is not None else "")
        + "9:\n  movl $-71, %eax\n  ret\n"
        for index, (name, (checks, result)) in enumerate(zip(HALFWORD_BINDING_NAMES, cases))
    ) + '''
.org 0x2c00
halfword_device_readport:
  movl 4(%esp), %ecx
  cmpl $4951, (%ecx)
  jne 9f
  movl $0xa5a5fde8, %eax
  ret
9:
  movl $-71, %eax
  ret
.org 0x2c80
halfword_device_readsigned:
  movl 4(%esp), %ecx
  cmpl $4951, (%ecx)
  jne 9f
  movl $0xa5a5ffff, %eax
  ret
9:
  movl $-71, %eax
  ret
'''


def _network_native_assembly():
    # Offsets and widths come from the native i386 declarations, independently
    # of the fixed descriptors copied into the compiler driver above.
    cases = (
        ("cmpl $31, 4(%esp)\n  jne 9f", 0),
        ("movl 4(%esp), %ecx\n  cmpb $101, (%ecx)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  movl $0x80000002, (%ecx)", 0),
        ("movl 4(%esp), %eax\n  bswap %eax\n  ret", None),
        ("movl 4(%esp), %ecx\n  cmpb $49, (%ecx)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  movl $0x80000003, (%ecx)", 0),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  cmpl $3, 8(%esp)\n  jne 9f", 0),
        ("movl 4(%esp), %eax\n  bswap %eax\n  ret", None),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  cmpl $0x80000002, 12(%esp)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f\n  movb $67, (%ecx)", -7),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  cmpl $0x80000003, 12(%esp)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f", 43),
        ("cmpl $31, 4(%esp)\n  jne 9f\n  cmpl $-1, 8(%esp)\n  jne 9f\n"
         "  cmpl $2, 12(%esp)\n  jne 9f\n  cmpl $0x80000004, 20(%esp)\n  jne 9f\n"
         "  movl 16(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f", -9),
        ("cmpl $31, 4(%esp)\n  jne 9f", 2147483647),
        ("cmpl $31, 4(%esp)\n  jne 9f", -3),
        ("cmpl $2, 4(%esp)\n  jne 9f", 31),
    )
    return "\n".join(
        f".org 0x{0x2000 + index * 0x80:x}\nnetwork_{name}:\n  {checks}\n"
        + (f"  movl ${result}, %eax\n  ret\n" if result is not None else "")
        + "9:\n  movl $-71, %eax\n  ret\n"
        for index, (name, (checks, result)) in enumerate(zip(NETWORK_BINDING_NAMES, cases))
    )


def _storage_native_assembly():
    cases = (
        ("movl 4(%esp), %ecx\n  cmpb $47, (%ecx)\n  jne 9f\n"
         "  cmpl $0x80000001, 8(%esp)\n  jne 9f", 31),
        ("cmpl $31, 4(%esp)\n  jne 9f", 0),
        ("cmpl $31, 4(%esp)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f\n"
         "  cmpl $0x80000002, 12(%esp)\n  jne 9f", 43),
        ("cmpl $31, 4(%esp)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $53, (%ecx)\n  jne 9f\n"
         "  cmpl $1, 12(%esp)\n  jne 9f", 1),
        ("cmpl $31, 4(%esp)\n  jne 9f\n"
         "  cmpl $-7, 8(%esp)\n  jne 9f\n"
         "  cmpl $2, 12(%esp)\n  jne 9f", 42),
        ("movl 4(%esp), %ecx\n  cmpb $47, (%ecx)\n  jne 9f", 0),
        ("movl 4(%esp), %ecx\n  cmpb $47, (%ecx)\n  jne 9f", 0),
        ("movl 4(%esp), %ecx\n  cmpb $98, 1(%ecx)\n  jne 9f\n"
         "  movl 8(%esp), %ecx\n  cmpb $97, 1(%ecx)\n  jne 9f", 0),
        ("movl 4(%esp), %ecx\n  cmpb $47, (%ecx)\n  jne 9f\n"
         "  cmpl $0x80000003, 8(%esp)\n  jne 9f", 0),
        ("cmpl $500, 4(%esp)\n  jne 9f", "0x80000001"),
        ("cmpl $0x80000001, 4(%esp)\n  jne 9f", "test_heap"),
        ("cmpl $0x80000001, 4(%esp)\n  jne 9f\n"
         "  incl test_free_calls", 0),
        ("cmpl $0x80000001, 4(%esp)\n  jne 9f\n"
         "  addl $2, test_free_calls", 0),
    )
    return "\n".join(
        f".org 0x{0x1400 + index * 0x80:x}\n"
        f"test_{STORAGE_BINDING_NAMES[index]}:\n  {checks}\n"
        f"  movl ${result}, %eax\n  ret\n9:\n  movl $-71, %eax\n  ret\n"
        for index, (checks, result) in enumerate(cases)
    )


class PrivateCupidCHarness(unittest.TestCase):
    def _assert_halfword_runtime(self, source):
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

        if os.name != "nt" and (shutil.which("as") is None or shutil.which("ld") is None):
            raise unittest.SkipTest(
                "GNU as and ld are required to execute the i386 runtime oracle"
            )

        cls.driver_directory = tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-driver-", ignore_cleanup_errors=True
        )
        cls.driver_root = Path(cls.driver_directory.name)
        cls.addClassCleanup(cls.driver_directory.cleanup)
        if os.name == "nt":
            cls.windows_oracle = WindowsI386Oracle(cls.driver_root)
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
                static int include_record_callback_binding;

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

                static int bind_fixed_kernel(
                    cc_state_t *cc, const char *name, uint32_t address,
                    cc_type_t result_type, int parameter_count,
                    int is_variadic,
                    cc_type_t parameter0, cc_type_t parameter1,
                    cc_type_t parameter2, cc_type_t parameter3) {
                  cc_function_pointer_signature_t signature;
                  memset(&signature, 0, sizeof(signature));
                  memset(signature.param_struct_indices, -1,
                         sizeof(signature.param_struct_indices));
                  signature.return_type = result_type;
                  signature.return_struct_index = -1;
                  signature.param_count = parameter_count;
                  signature.param_types[0] = (uint8_t)parameter0;
                  signature.param_types[1] = (uint8_t)parameter1;
                  signature.param_types[2] = (uint8_t)parameter2;
                  signature.param_types[3] = (uint8_t)parameter3;
                  signature.has_param_types = 1;
                  signature.is_variadic = is_variadic;
                  return cc_register_kernel_binding(
                      cc, name, address, &signature);
                }

                static void init_binding_signature(
                    cc_function_pointer_signature_t *signature,
                    cc_type_t result_type, int parameter_count) {
                  memset(signature, 0, sizeof(*signature));
                  memset(signature->param_struct_indices, -1,
                         sizeof(signature->param_struct_indices));
                  signature->return_type = result_type;
                  signature->return_struct_index = -1;
                  signature->param_count = parameter_count;
                  signature->has_param_types = 1;
                }

                static int bind_nested_kernel(
                    cc_state_t *cc, const char *name, uint32_t address,
                    const cc_function_pointer_signature_t *callback) {
                  cc_function_pointer_signature_t signature;
                  int callback_handle =
                      cc_retain_kernel_binding_callback_signature(
                          cc, callback);
                  if (callback_handle < 0)
                    return 0;
                  init_binding_signature(&signature, TYPE_VOID, 2);
                  signature.param_types[0] = TYPE_INT;
                  signature.param_types[1] = TYPE_FUNC_PTR;
                  signature.param_struct_indices[1] =
                      (int8_t)callback_handle;
                  return cc_register_kernel_binding(
                      cc, name, address, &signature);
                }

                static void bind_feature13_kernels(cc_state_t *cc) {
                  bind_kernel(cc, "repl_eval", TYPE_INT, 0x01001000u);
                  bind_kernel(cc, "serial_printf", TYPE_VOID, 0x01001010u);
                  bind_kernel(cc, "println", TYPE_VOID, 0x01001020u);
                  bind_kernel(cc, "fabs", TYPE_DOUBLE, 0x01001030u);
                  bind_kernel(cc, "sin", TYPE_DOUBLE, 0x01001040u);
                  bind_kernel(cc, "cos", TYPE_DOUBLE, 0x01001050u);
                  if (!bind_fixed_kernel(
                          cc, "sqrt", 0x01001060u, TYPE_DOUBLE, 1, 0,
                          TYPE_DOUBLE, TYPE_VOID, TYPE_VOID, TYPE_VOID))
                    return;
                  bind_kernel(cc, "log", TYPE_DOUBLE, 0x01001070u);
                  bind_kernel(cc, "pow", TYPE_DOUBLE, 0x01001080u);
                  bind_kernel(cc, "tanh", TYPE_DOUBLE, 0x01001090u);
                  bind_kernel(cc, "cbrt", TYPE_DOUBLE, 0x010010a0u);
                  bind_kernel(cc, "atan2", TYPE_DOUBLE, 0x010010b0u);
                  bind_kernel(cc, "hypot", TYPE_DOUBLE, 0x010010c0u);
                  bind_kernel(cc, "exp", TYPE_DOUBLE, 0x010010d0u);
                  if (!bind_fixed_kernel(
                          cc, "native_float", 0x01001200u, TYPE_FLOAT, 1, 0,
                          TYPE_FLOAT, TYPE_VOID, TYPE_VOID, TYPE_VOID))
                    return;
                  if (!bind_fixed_kernel(
                          cc, "native_mixed", 0x01001220u, TYPE_INT, 4, 0,
                          TYPE_INT, TYPE_DOUBLE, TYPE_FLOAT, TYPE_INT))
                    return;
                  if (!bind_fixed_kernel(
                          cc, "native_variadic", 0x01001240u, TYPE_INT, 1, 1,
                          TYPE_INT, TYPE_VOID, TYPE_VOID, TYPE_VOID))
                    return;
                  if (!bind_fixed_kernel(
                          cc, "kmalloc", 0x01001300u, TYPE_PTR, 1, 0,
                          TYPE_UINT, TYPE_VOID, TYPE_VOID, TYPE_VOID))
                    return;
                  if (!bind_fixed_kernel(
                          cc, "kfree", 0x01001360u, TYPE_VOID, 1, 0,
                          TYPE_PTR, TYPE_VOID, TYPE_VOID, TYPE_VOID))
                    return;
                  if (!bind_fixed_kernel(
                          cc, "memset", 0x01001380u, TYPE_PTR, 3, 0,
                          TYPE_PTR, TYPE_INT, TYPE_UINT, TYPE_VOID))
                    return;
                  if (!bind_fixed_kernel(
                          cc, "native_free_count", 0x010013c0u, TYPE_INT, 0, 0,
                          TYPE_VOID, TYPE_VOID, TYPE_VOID, TYPE_VOID))
                    return;
                  {
                    cc_function_pointer_signature_t callback;
                    init_binding_signature(&callback, TYPE_VOID, 2);
                    callback.param_types[0] = TYPE_INT;
                    callback.param_types[1] = TYPE_INT;
                    if (!bind_nested_kernel(
                            cc, "set_icon_drawer", 0x01001280u, &callback))
                      return;
                  }
                }

                static int bind_record_callback_kernel(cc_state_t *cc) {
                  cc_function_pointer_signature_t callback;
                  if (cc->struct_count != 0)
                    return 0;
                  memcpy(cc->structs[0].name, "BindingRecord",
                         sizeof("BindingRecord"));
                  cc->structs[0].align = 4;
                  memcpy(cc->structs[1].name, "OtherRecord",
                         sizeof("OtherRecord"));
                  cc->structs[1].align = 4;
                  cc->struct_count = 2;
                  init_binding_signature(&callback, TYPE_VOID, 1);
                  callback.param_types[0] = TYPE_STRUCT_PTR;
                  callback.param_struct_indices[0] = 0;
                  return bind_nested_kernel(
                      cc, "native_record_drawer", 0x010012a0u,
                      &callback);
                }

                static cc_state_t *new_compiler_state(int jit_mode);

                static int check_binding_registration_rollback(int jit_mode) {
                  cc_state_t *cc = new_compiler_state(jit_mode);
                  cc_function_pointer_signature_t signature;
                  int symbol_checkpoint;
                  if (cc == NULL || cc->error)
                    return 74;

                  symbol_checkpoint = cc->sym_count;
                  memset(&signature, 0, sizeof(signature));
                  memset(signature.param_struct_indices, -1,
                         sizeof(signature.param_struct_indices));
                  signature.return_type = TYPE_INT;
                  signature.return_struct_index = -1;
                  signature.param_count = 1;
                  signature.param_types[0] = 0xffu;
                  signature.has_param_types = 1;
                  if (cc_register_kernel_binding(
                          cc, "malformed_binding", 0x01001260u,
                          &signature) || !cc->error ||
                      cc->sym_count != symbol_checkpoint)
                    return 75;

                  cc->error = 0;
                  cc->error_msg[0] = 0;
                  signature.param_count = CC_MAX_PARAMS + 1;
                  signature.param_types[0] = TYPE_INT;
                  if (cc_register_kernel_binding(
                          cc, "capacity_binding", 0x01001260u,
                          &signature) || !cc->error ||
                      cc->sym_count != symbol_checkpoint)
                    return 76;

                  cc->error = 0;
                  cc->error_msg[0] = 0;
                  signature.param_count = 1;
                  if (!cc_register_kernel_binding(
                          cc, "recovered_binding", 0x01001260u,
                          &signature) || cc->error ||
                      cc->sym_count != symbol_checkpoint + 1)
                    return 77;
                  if (!cc->symbols[symbol_checkpoint].has_param_types ||
                      cc->symbols[symbol_checkpoint].param_count != 1 ||
                      cc->symbols[symbol_checkpoint].param_types[0] !=
                          TYPE_INT)
                    return 78;
                  return 0;
                }

                static int check_nested_binding_signature_limits(
                    int jit_mode) {
                  cc_state_t *cc = new_compiler_state(jit_mode);
                  cc_function_pointer_signature_t signature;
                  int handle;
                  int raw_checkpoint;
                  int depth_index;
                  if (cc == NULL || cc->error)
                    return 79;

                  init_binding_signature(&signature, TYPE_VOID, 1);
                  signature.param_types[0] = TYPE_FUNC_PTR;
                  signature.param_struct_indices[0] = 127;
                  raw_checkpoint =
                      cc->raw_function_pointer_signature_count;
                  if (cc_retain_kernel_binding_callback_signature(
                          cc, &signature) >= 0 || !cc->error ||
                      cc->raw_function_pointer_signature_count !=
                          raw_checkpoint)
                    return 80;

                  cc->error = 0;
                  cc->error_msg[0] = 0;
                  init_binding_signature(&signature, TYPE_VOID, 2);
                  signature.param_types[0] = TYPE_INT;
                  signature.param_types[1] = TYPE_INT;
                  handle = cc_retain_kernel_binding_callback_signature(
                      cc, &signature);
                  if (handle < 0 || cc->error)
                    return 81;

                  for (depth_index = 0;
                       depth_index < CC_MAX_FUNCTION_POINTER_SIGNATURE_DEPTH;
                       depth_index++) {
                    init_binding_signature(&signature, TYPE_VOID, 1);
                    signature.param_types[0] = TYPE_FUNC_PTR;
                    signature.param_struct_indices[0] = (int8_t)handle;
                    handle =
                        cc_retain_kernel_binding_callback_signature(
                            cc, &signature);
                    if (handle < 0 || cc->error)
                      return 82;
                  }
                  raw_checkpoint =
                      cc->raw_function_pointer_signature_count;
                  init_binding_signature(&signature, TYPE_VOID, 1);
                  signature.param_types[0] = TYPE_FUNC_PTR;
                  signature.param_struct_indices[0] = (int8_t)handle;
                  if (cc_retain_kernel_binding_callback_signature(
                          cc, &signature) >= 0 || !cc->error ||
                      cc->raw_function_pointer_signature_count !=
                          raw_checkpoint)
                    return 83;

                  cc->error = 0;
                  cc->error_msg[0] = 0;
                  init_binding_signature(&signature, TYPE_VOID, 1);
                  signature.param_types[0] = TYPE_FUNC_PTR;
                  signature.param_struct_indices[0] =
                      (int8_t)(handle - 1);
                  if (cc_retain_kernel_binding_callback_signature(
                          cc, &signature) != handle || cc->error ||
                      cc->raw_function_pointer_signature_count !=
                          raw_checkpoint)
                    return 84;
                  return 0;
                }

                static int check_nested_binding_signature_capacity(
                    int jit_mode) {
                  cc_state_t *cc = new_compiler_state(jit_mode);
                  cc_function_pointer_signature_t signature;
                  int arity;
                  int parameter_index;
                  int retained[CC_MAX_PARAMS];
                  int extra_retained;
                  int raw_checkpoint;
                  if (cc == NULL || cc->error)
                    return 85;

                  for (arity = 0; arity < CC_MAX_PARAMS; arity++) {
                    init_binding_signature(&signature, TYPE_VOID, arity);
                    for (parameter_index = 0;
                         parameter_index < arity; parameter_index++)
                      signature.param_types[parameter_index] = TYPE_INT;
                    retained[arity] =
                        cc_retain_kernel_binding_callback_signature(
                            cc, &signature);
                    if (retained[arity] < 0 || cc->error)
                      return 86;
                  }
                  init_binding_signature(&signature, TYPE_INT, 0);
                  extra_retained =
                      cc_retain_kernel_binding_callback_signature(
                          cc, &signature);
                  if (extra_retained < 0 || cc->error)
                    return 87;
                  raw_checkpoint =
                      cc->raw_function_pointer_signature_count;
                  init_binding_signature(&signature, TYPE_INT, 1);
                  signature.param_types[0] = TYPE_INT;
                  if (cc_retain_kernel_binding_callback_signature(
                          cc, &signature) >= 0 || !cc->error ||
                      cc->raw_function_pointer_signature_count !=
                          raw_checkpoint)
                    return 88;

                  cc->error = 0;
                  cc->error_msg[0] = 0;
                  init_binding_signature(&signature, TYPE_VOID, 0);
                  if (cc_retain_kernel_binding_callback_signature(
                          cc, &signature) != retained[0] || cc->error ||
                      cc->raw_function_pointer_signature_count !=
                          raw_checkpoint)
                    return 89;
                  return 0;
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
                  if (!cc->error && include_record_callback_binding &&
                      !bind_record_callback_kernel(cc))
                    return NULL;
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
                  int repl_patch_signature_rollback_mode =
                      argc == 5 &&
                      strcmp(argv[4],
                             "--repl-patch-signature-rollback") == 0;
                  int recovery_mode =
                      argc == 5 && strcmp(argv[4], "--recover") == 0;
                  int same_state_recovery_mode =
                      argc == 5 &&
                      (strcmp(argv[4], "--recover-same-state") == 0 ||
                       strcmp(argv[4],
                              "--recover-same-state-record") == 0);
                  int record_binding_mode =
                      argc == 5 &&
                      strcmp(argv[4],
                             "--recover-same-state-record") == 0;
                  int recover_after_success_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--recover-after-success") == 0;
                  int double_failure_recovery_mode =
                      argc == 5 &&
                      strcmp(argv[4], "--recover-two-failures") == 0;
                  int aot_mode =
                      argc == 5 && strcmp(argv[4], "--aot") == 0;
                  if ((argc == 2 ||
                       (argc == 3 && strcmp(argv[2], "--aot") == 0)) &&
                      strcmp(argv[1], "--binding-registration-rollback") == 0)
                    return check_binding_registration_rollback(argc == 2);
                  if ((argc == 2 ||
                       (argc == 3 && strcmp(argv[2], "--aot") == 0)) &&
                      strcmp(argv[1], "--nested-binding-signature-limits") == 0)
                    return check_nested_binding_signature_limits(argc == 2);
                  if ((argc == 2 ||
                       (argc == 3 && strcmp(argv[2], "--aot") == 0)) &&
                      strcmp(argv[1], "--nested-binding-signature-capacity") == 0)
                    return check_nested_binding_signature_capacity(argc == 2);
                  if (argc == 3 &&
                      strcmp(argv[1], "--check-number-boundary") == 0)
                    return check_numeric_token_boundary(argv[2]);
                  if (argc != 4 && !repl_mode && !repl_rollback_mode &&
                      !repl_patch_rollback_mode &&
                      !repl_patch_signature_rollback_mode &&
                      !recovery_mode && !same_state_recovery_mode &&
                      !recover_after_success_mode &&
                      !double_failure_recovery_mode &&
                      !aot_mode)
                    return 64;
                  source = read_source(argv[1]);
                  include_record_callback_binding = record_binding_mode;
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
                             repl_patch_rollback_mode ||
                             repl_patch_signature_rollback_mode) {
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
                      if ((repl_rollback_mode || repl_patch_rollback_mode ||
                           repl_patch_signature_rollback_mode) &&
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
                                  repl_patch_rollback_mode ||
                                  repl_patch_signature_rollback_mode) &&
                                 unit_index == 1) {
                        if (repl_patch_signature_rollback_mode) {
                          if (!cc->error ||
                              cc->patch_count <= checkpoint.patch_committed ||
                              cc->raw_function_pointer_signature_count <=
                                  checkpoint.raw_function_pointer_signature_committed)
                            return 73;
                        } else if (repl_patch_rollback_mode) {
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
        harness_source = harness.read_text(encoding="utf-8")
        harness_source = harness_source.replace(
            "static void bind_feature13_kernels(cc_state_t *cc) {",
            _storage_binding_registration()
            + _network_binding_registration()
            + _halfword_binding_registration()
            + "\nstatic void bind_feature13_kernels(cc_state_t *cc) {\n"
            + "  bind_storage_kernels(cc);\n  bind_network_kernels(cc);\n  bind_halfword_kernels(cc);",
            1,
        )
        harness.write_text(harness_source, encoding="utf-8")
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
                "-I",
                str(REPO_ROOT / "toolchain"),
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
        self, root, units, *, require_patch=False,
        require_patch_and_raw_signature=False
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
                    "--repl-patch-signature-rollback"
                    if require_patch_and_raw_signature
                    else (
                        "--repl-patch-rollback"
                        if require_patch
                        else "--repl-rollback"
                    )
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
        self, root, failing_source, retry_source, *, same_state=False,
        record_binding=False
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
                (
                    "--recover-same-state-record"
                    if record_binding
                    else (
                        "--recover-same-state" if same_state else "--recover"
                    )
                ),
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
                .org 0x1060
                test_sqrt:
                  movsd 4(%esp), %xmm0
                  ret
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

                .org 0x1200
                test_native_float:
                  movss 4(%esp), %xmm0
                  ret

                .org 0x1220
                test_native_mixed:
                  movl 4(%esp), %eax
                  cvttsd2si 8(%esp), %ecx
                  addl %ecx, %eax
                  cvttss2si 16(%esp), %ecx
                  addl %ecx, %eax
                  addl 20(%esp), %eax
                  ret

                .org 0x1240
                test_native_variadic:
                  movl 4(%esp), %eax
                  addl 8(%esp), %eax
                  cvttsd2si 12(%esp), %ecx
                  addl %ecx, %eax
                  ret

                .org 0x1280
                test_set_icon_drawer:
                  cmpl $0, 4(%esp)
                  jl 1f
                  movl 8(%esp), %eax
                  pushl $7
                  pushl 8(%esp)
                  call *%eax
                  addl $8, %esp
                1:
                  ret

                .org 0x1300
                test_kmalloc:
                  movl 4(%esp), %ecx
                  testl %ecx, %ecx
                  jz 1f
                  cmpl $4096, %ecx
                  ja 1f
                  movl test_heap_next, %eax
                  addl $3, %ecx
                  andl $-4, %ecx
                  addl %eax, %ecx
                  cmpl $test_heap_end, %ecx
                  ja 1f
                  movl %ecx, test_heap_next
                  ret
                1:
                  xorl %eax, %eax
                  ret

                .org 0x1360
                test_kfree:
                  movl 4(%esp), %eax
                  testl %eax, %eax
                  jz 1f
                  incl test_free_calls
                1:
                  ret

                .org 0x1380
                test_memset:
                  pushl %edi
                  movl 8(%esp), %edi
                  movl %edi, %eax
                  movb 12(%esp), %dl
                  movl 16(%esp), %ecx
                1:
                  testl %ecx, %ecx
                  jz 2f
                  movb %dl, (%edi)
                  incl %edi
                  decl %ecx
                  jmp 1b
                2:
                  popl %edi
                  ret

                .org 0x13c0
                test_native_free_count:
                  movl test_free_calls, %eax
                  ret

                {_storage_native_assembly()}
                {_network_native_assembly()}
                {_halfword_native_assembly()}

                .section .testruntime,"aw",@progbits
                test_heap_next:
                  .long test_heap
                test_free_calls:
                  .long 0
                test_heap:
                  .space 4096
                test_heap_end:

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
                  . = 0x01008000;
                  .testruntime : { *(.testruntime) }
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
            return self.windows_oracle.run(root, entry_offset)
        invocation = ["sh", "-c", command]
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

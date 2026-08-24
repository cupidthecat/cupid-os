"""Produce a deterministic inventory of Cupid OS build and language inputs."""

from __future__ import annotations

import argparse
import ast
import bisect
import collections
import hashlib
import json
import os
import posixpath
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


def _stable_ast_shape(value: object) -> str:
    """Return an AST shape that is stable across supported Python versions."""
    if isinstance(value, ast.AST):
        fields = []
        for name, child in ast.iter_fields(value):
            if child is None or child == []:
                continue
            fields.append(f"{name}={_stable_ast_shape(child)}")
        return f"{value.__class__.__name__}({', '.join(fields)})"
    if isinstance(value, list):
        return f"[{', '.join(_stable_ast_shape(item) for item in value)}]"
    return repr(value)


def _stable_ast_fingerprint(node: ast.AST) -> str:
    return hashlib.sha256(_stable_ast_shape(node).encode("utf-8")).hexdigest()


SCHEMA = "cupid.build-graph-audit.v1"
SOURCE_SUFFIX_OWNERSHIP_POLICY = (
    "docs/bootstrap/c-source-suffix-ownership.json"
)
SOURCE_SUFFIX_OWNERSHIP_SCHEMA = "cupid.c-source-suffix-ownership.v1"
SOURCE_SUFFIXES = {
    ".c": "c",
    ".cc": "cupid_c",
    ".cup": "cupid_script",
    ".h": "c_header",
    ".asm": "assembly",
    ".s": "assembly",
}

# Keep the checked graph independent of the host running the audit. The
# Windows branch stays canonical so platform-specific declarations remain
# visible, while the C locale keeps Make wildcard ordering stable. Direct
# Linux builds cover the Linux execution branch.
CANONICAL_MAKE_VARIABLES = ("OS=Windows_NT",)
LINUX_BOOTSTRAP_SEED_INPUTS = (
    "bootstrap/seeds/i386-linux/manifest.json",
    "bootstrap/seeds/i386-linux/cupidasm.elf",
    "bootstrap/seeds/i386-linux/cupidc.elf",
    "bootstrap/seeds/i386-linux/cupiddis.elf",
    "bootstrap/seeds/i386-linux/cupidld.elf",
    "bootstrap/seeds/i386-linux/cupidobj.elf",
)
WINDOWS_PRODUCTION_SEED_INPUTS = (
    "bootstrap/seeds/i386-windows/manifest.json",
    "bootstrap/seeds/i386-windows/cupidasm.exe",
    "bootstrap/seeds/i386-windows/cupidc.exe",
    "bootstrap/seeds/i386-windows/cupiddis.exe",
    "bootstrap/seeds/i386-windows/cupidld.exe",
    "bootstrap/seeds/i386-windows/cupidobj.exe",
)
ARTIFACT_SIZE_CONTRACT_BUILD_INPUTS = (
    "Makefile",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "tools/artifact_size_contract.py",
    "tools/artifact_size_policy.py",
    "tools/bootstrap_toolchain.py",
)
ARTIFACT_SIZE_CONTRACT_TRANSFORM_INPUTS = frozenset(
    {
        "boot/boot.bin",
        "kernel/kernel.bin",
        "kernel/kernel.elf",
        "kernel/kernel.elf.pass1",
        "bootstrap/artifact-size-policy.json",
        *ARTIFACT_SIZE_CONTRACT_BUILD_INPUTS,
        *LINUX_BOOTSTRAP_SEED_INPUTS,
        *WINDOWS_PRODUCTION_SEED_INPUTS,
    }
)
ARTIFACT_SIZE_CONTRACT_RECIPE = ["$(ARTIFACT_SIZE_CONTRACT)"]
TOOLCHAIN_MANIFEST_CONTRACT_BUILD_INPUTS = (
    "toolchain/Makefile",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "tools/artifact_size_policy.py",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/toolchain_manifest_contract.py",
)
TOOLCHAIN_MANIFEST_CONTRACT_ARTIFACT_INPUTS = (
    "toolchain/build/cupidc-contracts/core-contract.elf",
    "toolchain/build/cupidc-contracts/user-syscall-abi-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-pp-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-type-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-frontend-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-ir-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-object-contract.elf",
    "toolchain/build/cupidc-contracts/elf32-contract.elf",
    "toolchain/build/cupidc-contracts/x86-contract.elf",
    "toolchain/build/cupidc-contracts/cupiddis-contract.elf",
    "toolchain/build/cupidc-contracts/cupidasm-contract.elf",
    "toolchain/build/cupidc-contracts/cupidasm-demos-contract.elf",
    "toolchain/build/cupidc-contracts/cupidasm-kernel-elf-contract.elf",
    "toolchain/build/cupidc-contracts/cupidobj-contract.elf",
    "toolchain/build/cupidc-contracts/cupidld-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-runtime-contract.elf",
    "toolchain/build/cupidc-contracts/cupidc-cupidasm.elf",
    "toolchain/build/cupidc-contracts/cupidc-cupiddis.elf",
    "toolchain/build/cupidc-contracts/cupidc-cupidld.elf",
    "toolchain/build/cupidc-contracts/cupidc-cupidobj.elf",
    "toolchain/build/cupidc-contracts/cupidc-cupidc.elf",
)
TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS = (
    "kernel/core/syscall.cc",
    "kernel/core/syscall.h",
    "kernel/core/types.h",
    "kernel/fs/vfs.h",
    "kernel/lang/as_elf.cc",
    "kernel/lang/as_elf.h",
    "kernel/network/socket.h",
    "toolchain/Makefile",
    "toolchain/ctool.h",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.h",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.h",
    "toolchain/cupidld.h",
    "toolchain/cupidobj.h",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "toolchain/tests/core_contract.cc",
    "toolchain/tests/cupidasm_contract.cc",
    "toolchain/tests/cupidasm_demos_contract.cc",
    "toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "toolchain/tests/cupidc_exact_decimal_literal_fixture.h",
    "toolchain/tests/cupidc_frontend_contract.cc",
    "toolchain/tests/cupidc_ir_contract.cc",
    "toolchain/tests/cupidc_kernel_simd_fixture.h",
    "toolchain/tests/cupidc_object_contract.cc",
    "toolchain/tests/cupidc_pp_active_cases.inc",
    "toolchain/tests/cupidc_pp_conditional_cases.inc",
    "toolchain/tests/cupidc_pp_contract.cc",
    "toolchain/tests/cupidc_static_long_double_arithmetic_fixture.h",
    "toolchain/tests/cupidc_static_long_double_control_fixture.h",
    "toolchain/tests/cupidc_static_long_double_integer_fixture.h",
    "toolchain/tests/cupidc_type_contract.cc",
    "toolchain/tests/cupiddis_contract.cc",
    "toolchain/tests/cupidld_contract.cc",
    "toolchain/tests/cupidobj_contract.cc",
    "toolchain/tests/elf32_contract.cc",
    "toolchain/tests/hosted_i386_runtime_contract.cc",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "toolchain/tests/user_syscall_abi_contract.cc",
    "toolchain/tests/x86_active_cases.inc",
    "toolchain/tests/x86_catalogue_contract.inc",
    "toolchain/tests/x86_contract.cc",
    "toolchain/tests/x86_inline_cases.inc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
    "user/cupid.h",
)
TOOLCHAIN_MANIFEST_BOOTSTRAP_INPUTS = (
    "link.ld",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.cc",
    "toolchain/cupidasm.h",
    "toolchain/cupidasm_main.cc",
    "toolchain/cupidbuild.cc",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_host.cc",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidbuild_main.cc",
    "toolchain/cupidc_emit.cc",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.cc",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.cc",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_main.cc",
    "toolchain/cupidc_pp.cc",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.cc",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.cc",
    "toolchain/cupiddis.h",
    "toolchain/cupiddis_main.cc",
    "toolchain/cupidld.cc",
    "toolchain/cupidld.h",
    "toolchain/cupidld_main.cc",
    "toolchain/cupidobj.cc",
    "toolchain/cupidobj.h",
    "toolchain/cupidobj_main.cc",
    "toolchain/elf32.cc",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
)
TOOLCHAIN_MANIFEST_CONTRACT_TRANSFORM_INPUTS = frozenset(
    {
        "toolchain/Makefile",
        "toolchain/build/cupidc-contracts/manifest.json",
        *TOOLCHAIN_MANIFEST_CONTRACT_ARTIFACT_INPUTS,
        *TOOLCHAIN_MANIFEST_CONTRACT_BUILD_INPUTS,
        *TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS,
        *TOOLCHAIN_MANIFEST_BOOTSTRAP_INPUTS,
        *LINUX_BOOTSTRAP_SEED_INPUTS,
        *WINDOWS_PRODUCTION_SEED_INPUTS,
    }
)
TOOL_MARKERS = (
    ("build-iso --seed-manifest", "cupid_object"),
    ("image --seed-manifest", "cupid_object"),
    ("gen-big --seed-manifest", "cupid_assembler"),
    ("assemble-bootloader --seed-manifest", "cupid_assembler"),
    ("assemble-bootloader --seed-manifest", "cupid_disassembler"),
    ("assemble-smp-trampoline --seed-manifest", "cupid_assembler"),
    ("assemble-smp-trampoline --seed-manifest", "cupid_disassembler"),
    ("assemble-cupidasm-object --seed-manifest", "cupid_assembler"),
    ("assemble-cupidasm-object --seed-manifest", "cupid_disassembler"),
    ("validate-code --seed-manifest", "cupid_disassembler"),
    ("validate-code --seed-manifest", "cupid_object"),
    ("mksyms --seed-manifest", "cupid_disassembler"),
    ("mksyms --seed-manifest", "cupid_object"),
    ("embed-jpeg --seed-manifest", "cupid_object"),
    ("$(CUPIDDIS)", "cupid_disassembler"),
    ("$(CUPIDDIS)", "host_python"),
    ("$(CUPIDASM)", "cupid_assembler"),
    ("$(CUPIDASM)", "host_python"),
    ("$(CUPIDLD_USER_LINK)", "cupid_disassembler"),
    ("$(CUPIDLD_USER_LINK)", "cupid_linker"),
    ("$(CUPIDLD_USER_LINK)", "host_python"),
    ("$(CUPIDLD)", "cupid_linker"),
    ("$(CUPIDLD)", "host_python"),
    ("$(CUPIDOBJ)", "cupid_object"),
    ("$(CUPIDOBJ)", "host_python"),
    ("$(CUPIDC_PRODUCTION_COMPILE)", "cupid_c_compiler"),
    ("$(CUPIDC_PRODUCTION_COMPILE)", "host_python"),
    ("$(CUPIDC_KERNEL_COMPILE)", "cupid_c_compiler"),
    ("$(CUPIDC_KERNEL_COMPILE)", "host_python"),
    ("$(CC)", "host_c_compiler"),
    ("$(ASM)", "nasm"),
    ("$(LD)", "host_linker"),
    ("$(OBJCOPY)", "host_object_copy"),
    ("$(NM)", "host_symbol_reader"),
    ("$(ARTIFACT_SIZE_CONTRACT)", "cupid_assembler"),
    ("$(ARTIFACT_SIZE_CONTRACT)", "cupid_c_compiler"),
    ("$(ARTIFACT_SIZE_CONTRACT)", "cupid_c_contract"),
    ("$(ARTIFACT_SIZE_CONTRACT)", "cupid_linker"),
    ("$(ARTIFACT_SIZE_CONTRACT)", "host_python"),
    ("$(TOOLCHAIN_MANIFEST_CONTRACT)", "cupid_assembler"),
    ("$(TOOLCHAIN_MANIFEST_CONTRACT)", "cupid_c_compiler"),
    ("$(TOOLCHAIN_MANIFEST_CONTRACT)", "cupid_c_contract"),
    ("$(TOOLCHAIN_MANIFEST_CONTRACT)", "cupid_linker"),
    ("$(TOOLCHAIN_MANIFEST_CONTRACT)", "host_python"),
    ("cupidc_toolchain_contracts.py build", "cupid_assembler"),
    ("cupidc_toolchain_contracts.py build", "cupid_c_compiler"),
    ("cupidc_toolchain_contracts.py build", "cupid_c_contract"),
    ("cupidc_toolchain_contracts.py build", "cupid_linker"),
    ("$(USER_SYSCALL_ABI)", "cupid_assembler"),
    ("$(USER_SYSCALL_ABI)", "cupid_c_compiler"),
    ("$(USER_SYSCALL_ABI)", "cupid_c_contract"),
    ("$(USER_SYSCALL_ABI)", "cupid_linker"),
    ("$(USER_SYSCALL_ABI)", "host_python"),
    ("$(PYTHON)", "host_python"),
    ("$(MAKE)", "make"),
)
USER_SYSCALL_ABI_SOURCE_INPUTS = (
    "kernel/core/types.h",
    "kernel/core/syscall.h",
    "kernel/core/syscall.cc",
    "kernel/fs/vfs.h",
    "kernel/network/socket.h",
    "user/cupid.h",
)
USER_SYSCALL_ABI_NATIVE_BUILD_INPUTS = (
    "kernel/core/syscall.cc",
    "kernel/core/syscall.h",
    "kernel/core/types.h",
    "kernel/fs/vfs.h",
    "kernel/network/socket.h",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/user_syscall_abi_contract.cc",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
    "user/cupid.h",
)
USER_SYSCALL_ABI_PUBLICATION_INPUTS = (
    "kernel/core/syscall.cc",
    "kernel/core/syscall.h",
    "kernel/core/types.h",
    "kernel/fs/vfs.h",
    "kernel/lang/as_elf.cc",
    "kernel/lang/as_elf.h",
    "kernel/network/socket.h",
    "toolchain/Makefile",
    "toolchain/ctool.h",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.h",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.h",
    "toolchain/cupidld.h",
    "toolchain/cupidobj.h",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "toolchain/tests/core_contract.cc",
    "toolchain/tests/cupidasm_contract.cc",
    "toolchain/tests/cupidasm_demos_contract.cc",
    "toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "toolchain/tests/cupidc_exact_decimal_literal_fixture.h",
    "toolchain/tests/cupidc_frontend_contract.cc",
    "toolchain/tests/cupidc_ir_contract.cc",
    "toolchain/tests/cupidc_kernel_simd_fixture.h",
    "toolchain/tests/cupidc_object_contract.cc",
    "toolchain/tests/cupidc_pp_active_cases.inc",
    "toolchain/tests/cupidc_pp_conditional_cases.inc",
    "toolchain/tests/cupidc_pp_contract.cc",
    "toolchain/tests/cupidc_static_long_double_arithmetic_fixture.h",
    "toolchain/tests/cupidc_static_long_double_control_fixture.h",
    "toolchain/tests/cupidc_static_long_double_integer_fixture.h",
    "toolchain/tests/cupidc_type_contract.cc",
    "toolchain/tests/cupiddis_contract.cc",
    "toolchain/tests/cupidld_contract.cc",
    "toolchain/tests/cupidobj_contract.cc",
    "toolchain/tests/elf32_contract.cc",
    "toolchain/tests/hosted_i386_runtime_contract.cc",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "toolchain/tests/user_syscall_abi_contract.cc",
    "toolchain/tests/x86_active_cases.inc",
    "toolchain/tests/x86_catalogue_contract.inc",
    "toolchain/tests/x86_contract.cc",
    "toolchain/tests/x86_inline_cases.inc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
    "user/cupid.h",
)
USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS = (
    "link.ld",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.cc",
    "toolchain/cupidasm.h",
    "toolchain/cupidasm_main.cc",
    "toolchain/cupidbuild.cc",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_host.cc",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidbuild_main.cc",
    "toolchain/cupidc_emit.cc",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.cc",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.cc",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_main.cc",
    "toolchain/cupidc_pp.cc",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.cc",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.cc",
    "toolchain/cupiddis.h",
    "toolchain/cupiddis_main.cc",
    "toolchain/cupidld.cc",
    "toolchain/cupidld.h",
    "toolchain/cupidld_main.cc",
    "toolchain/cupidobj.cc",
    "toolchain/cupidobj.h",
    "toolchain/cupidobj_main.cc",
    "toolchain/elf32.cc",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
)
TOOLCHAIN_CONTRACT_LINUX_INPUTS = tuple(
    sorted(
        set(USER_SYSCALL_ABI_PUBLICATION_INPUTS)
        | set(USER_SYSCALL_ABI_BOOTSTRAP_SOURCE_INPUTS)
        | set(LINUX_BOOTSTRAP_SEED_INPUTS)
    )
)
TOOLCHAIN_CONTRACT_CUPIDASM_OWNERSHIP_INPUTS = tuple(
    path
    for path in TOOLCHAIN_CONTRACT_LINUX_INPUTS
    if Path(path).suffix.lower() in {".asm", ".s"}
)
USER_SYSCALL_ABI_CHECKED_SEED_INPUTS = WINDOWS_PRODUCTION_SEED_INPUTS
USER_SYSCALL_ABI_AUDIT_INPUTS = tuple(
    sorted(
        set(USER_SYSCALL_ABI_NATIVE_BUILD_INPUTS)
        | set(USER_SYSCALL_ABI_CHECKED_SEED_INPUTS)
    )
)
CUPIDC_KERNEL_CONTROL_FILES = (
    "tools/cupidc_kernel_compile.py",
    "tools/kernel_cupidc_frontier.py",
    "tools/bootstrap_toolchain.py",
    "bootstrap/seeds/i386-windows/manifest.json",
)
_CUPIDOBJ_PROFILE_MANIFEST_OUTPUT = (
    "build/bootstrap/doom-cupidc-inputs.json"
)
_CUPIDOBJ_PROFILE_MANIFEST_RECIPE = [
    "$(PYTHON) tools/cupidc_kernel_compile.py --root . \\",
    "--manifest $(PRODUCTION_SEED_MANIFEST) \\",
    "--write-profile-input-manifest $@",
]
_CUPIDOBJ_PROFILE_MANIFEST_CONTROL_INPUTS = (
    "Makefile",
    "tools/bootstrap_toolchain.py",
    *WINDOWS_PRODUCTION_SEED_INPUTS,
    "tools/cupidc_kernel_compile.py",
)
_CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_FILES = (
    "tools/cupidc_kernel_compile.py",
    "toolchain/cupidobj.cc",
    "CONTEXT.md",
    "docs/bootstrap/README.md",
)
_CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_DIRECTORIES = (
    "kernel/doom/src",
)
CUPIDC_PRODUCTION_CONTROL_FILES = (
    "tools/cupidc_production_compile.py",
    "tools/cupidc_production_frontier.py",
    "tools/cupidld_user_link.py",
    "tools/native_user_toolchain.py",
)
EXCLUDED_SOURCE_TREES = {".agents", ".git", "__pycache__", "build", "templeos"}

# These relations cannot be inferred from byte identity: the older sources have
# diverged from the active implementation or changed language/path. Keep the
# project-specific audit decisions explicit and validate the target against the
# active graph before reporting one.
KNOWN_SOURCE_RELATIONS = {
    "bin/cupidc.c": ("historical_copy_of", "kernel/lang/cupidc.cc"),
    "bin/cupidc_lex.c": ("historical_copy_of", "kernel/lang/cupidc_lex.cc"),
    "bin/cupidc_parse.c": ("historical_copy_of", "kernel/lang/cupidc_parse.cc"),
    "bin/fat16.c": ("historical_copy_of", "kernel/fs/fat16.cc"),
    "bin/fat16_vfs.c": ("historical_copy_of", "kernel/fs/fat16_vfs.cc"),
    "bin/kernel.c": ("historical_copy_of", "kernel/core/kernel.cc"),
    "bin/terminal_app.c": ("historical_copy_of", "kernel/gui/terminal_app.cc"),
    "demos/paint.cc": ("superseded_by", "bin/paint.cc"),
    "kernel/core/scheduler.c": ("superseded_by", "kernel/core/process.cc"),
    "kernel/core/scheduler.h": ("superseded_by", "kernel/core/process.h"),
    "kernel/gui/notepad.c": ("superseded_by", "bin/notepad.cc"),
    "kernel/gui/terminal_ansi.c": ("superseded_by", "kernel/gui/ansi.cc"),
}

KNOWN_UNREACHABLE_SOURCE_POLICIES = {
    "kernel/lang/cupidc_runtime.c": (
        "dormant",
        "unlinked runtime draft outside the supported build roots",
    ),
    "tests/kernel_exec_contract.c": (
        "host_fixture",
        "native kernel behavior fixture compiled by the host test harness",
    ),
    "tests/kernel_process_contract.c": (
        "host_fixture",
        "native kernel behavior fixture compiled by the host test harness",
    ),
    "tests/usb_interrupt_ownership_contract.c": (
        "host_fixture",
        "native USB behavior fixture compiled by the host test harness",
    ),
    "tests/usb_msc_lifetime_contract.c": (
        "host_fixture",
        "native USB behavior fixture compiled by the host test harness",
    ),
    "tests/usb_reconciliation_runtime.c": (
        "host_fixture",
        "native USB behavior fixture compiled by the host test harness",
    ),
    "toolchain/tests/elf32_oracle.c": (
        "host_oracle",
        "optional host compiler input for ELF32 reader comparison",
    ),
    "toolchain/tests/cupiddis_kernel_adapter_contract.cc": (
        "host_oracle",
        "native public kernel-adapter contract outside production build roots",
    ),
}
KNOWN_ACTIVE_ASSEMBLY_POLICIES: dict[str, tuple[str, str]] = {}
ACTIVE_ASSEMBLY_POLICY_CLASSIFICATIONS = {"host_fixture", "host_oracle"}

SOURCE_SUFFIX_POLICY_KEYS = {
    "residual_c_sources",
    "runtime_delivery_sources",
    "schema",
    "unreachable_cupid_c_sources",
}
SOURCE_SUFFIX_CLASSIFICATIONS = {
    "dormant",
    "exact_duplicate",
    "explicitly_excluded",
    "historical_copy",
    "host_fixture",
    "host_oracle",
    "not_reached",
    "superseded",
}


class AuditError(RuntimeError):
    """The supported build graph could not be inventoried."""


@dataclass
class MakeRule:
    """One expanded target rule from GNU Make's database."""

    prerequisites: list[str] = field(default_factory=list)
    recipe: list[str] = field(default_factory=list)


@dataclass
class BuildModel:
    """One supported Make root normalized to repository-relative paths."""

    directory: str
    root_target: str
    rules: dict[str, MakeRule]
    reachable: set[str]
    direct_sources: set[str]
    generated_sources: set[str]
    forced_sources: set[str]
    includes_by_source: dict[str, list[str]]
    include_search_paths: list[str]
    transforms: list[dict[str, object]]


@dataclass(frozen=True)
class CIncludeDirective:
    """One source-written C include operand after phases two and three."""

    line: int
    marker: str
    raw: str
    normalized: str
    kind: str
    spelling: str | None
    conditional_stack: tuple[str, ...]


@dataclass(frozen=True)
class CPreprocessorProfile:
    """Named policy for one exact active preprocessing cohort."""

    name: str
    mode: str
    gnu_extensions: str
    hosted_environment: str
    implicit_function_declarations: str
    compatibility_pointer_conversions: str


@dataclass(frozen=True)
class CPreprocessorActiveCasesManifest:
    """Checked X-macro inputs for active CupidC preprocessing jobs."""

    profiles: tuple[CPreprocessorProfile, ...]
    include_roots: tuple[tuple[str, str, str], ...]
    macros: tuple[tuple[str, str, str], ...]
    forced_includes: tuple[tuple[str, str], ...]
    active_cases: tuple[tuple[str, str], ...]
    generated_cases: tuple[tuple[str, str], ...]
    include_only: tuple[tuple[str, str], ...]
    non_roots: tuple[tuple[str, str], ...]
    deferred_hosted: tuple[tuple[str, str], ...]


_C_PP_INCLUDE_BOTH = (
    "(CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE)"
)
_C_PP_PROFILE_ROWS = (
    CPreprocessorProfile(
        name="KERNEL_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="DOOM_COMPAT_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_TRUE",
        compatibility_pointer_conversions="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="DOOM_TREE_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_TRUE",
        compatibility_pointer_conversions="CTOOL_TRUE",
    ),
    CPreprocessorProfile(
        name="USER_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="FREESTANDING_I386",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="CUPID_RUNTIME",
        mode="CTOOL_C_PP_MODE_CUPID",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_FALSE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_TOOLCHAIN_64",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_KERNEL_BRIDGE_64",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_LINUX",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_WINDOWS",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_KERNEL_BRIDGE",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_FALSE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
    CPreprocessorProfile(
        name="HOSTED_I386_LINUX_GNU",
        mode="CTOOL_C_PP_MODE_C11",
        gnu_extensions="CTOOL_TRUE",
        hosted_environment="CTOOL_TRUE",
        implicit_function_declarations="CTOOL_FALSE",
        compatibility_pointer_conversions="CTOOL_FALSE",
    ),
)
_C_PP_HOSTED_PROFILES = frozenset(
    profile.name
    for profile in _C_PP_PROFILE_ROWS
    if profile.hosted_environment == "CTOOL_TRUE"
)
_C_PP_KERNEL_INCLUDE_ROOTS = (
    "/kernel",
    "/kernel/audio",
    "/kernel/core",
    "/kernel/cpu",
    "/kernel/crypto",
    "/kernel/doom",
    "/kernel/fs",
    "/kernel/gfx",
    "/kernel/gui",
    "/kernel/lang",
    "/kernel/mm",
    "/kernel/network",
    "/kernel/smp",
    "/kernel/tls",
    "/kernel/usb",
    "/kernel/util",
    "/drivers",
    "/toolchain",
)
_C_PP_DOOM_EXTRA_INCLUDE_ROOTS = (
    "/kernel/doom/src",
    "/kernel/doom/src/include_stubs",
)
_C_PP_COMMON_I386_MACROS = (
    ("__GNUC__", "1"),
    ("__SIZEOF_POINTER__", "4"),
    ("__ORDER_LITTLE_ENDIAN__", "1234"),
    ("__ORDER_BIG_ENDIAN__", "4321"),
    ("__ORDER_PDP_ENDIAN__", "3412"),
    ("__BYTE_ORDER__", "__ORDER_LITTLE_ENDIAN__"),
)
_C_PP_ACTIVE_COUNTS = {
    "KERNEL_I386": 156,
    "DOOM_COMPAT_I386": 3,
    "DOOM_TREE_I386": 80,
    "USER_I386": 3,
    "FREESTANDING_I386": 1,
    "CUPID_RUNTIME": 108,
    "HOSTED_TOOLCHAIN_64": 0,
    "HOSTED_KERNEL_BRIDGE_64": 0,
    "HOSTED_I386_LINUX": 38,
    "HOSTED_I386_WINDOWS": 9,
    "HOSTED_I386_KERNEL_BRIDGE": 2,
    "HOSTED_I386_LINUX_GNU": 3,
}
_C_PP_HOSTED_I386_STRICT_CASES = (
    "/toolchain/ctool.cc",
    "/toolchain/ctool_host.cc",
    "/toolchain/cupidbuild.cc",
    "/toolchain/cupidbuild_host.cc",
    "/toolchain/cupidbuild_main.cc",
    "/toolchain/cupidasm.cc",
    "/toolchain/cupidasm_main.cc",
    "/toolchain/cupidc_emit.cc",
    "/toolchain/cupidc_frontend.cc",
    "/toolchain/cupidc_ir.cc",
    "/toolchain/cupidc_main.cc",
    "/toolchain/cupidc_pp.cc",
    "/toolchain/cupidc_type.cc",
    "/toolchain/cupiddis.cc",
    "/toolchain/cupiddis_main.cc",
    "/toolchain/cupidld.cc",
    "/toolchain/cupidld_main.cc",
    "/toolchain/cupidobj.cc",
    "/toolchain/cupidobj_main.cc",
    "/toolchain/elf32.cc",
    "/toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "/toolchain/x86.cc",
)
_C_PP_HOSTED_I386_GNU_CASES = (
    "/toolchain/hosted/i386-linux/runtime.cc",
    "/toolchain/hosted/i386-windows/runtime.cc",
    "/toolchain/tests/hosted_i386_runtime_contract.cc",
)
_C_PP_HOSTED_I386_WINDOWS_CASES = (
    "/toolchain/ctool_host.cc",
    "/toolchain/cupidbuild.cc",
    "/toolchain/cupidbuild_host.cc",
    "/toolchain/cupidbuild_main.cc",
    "/toolchain/cupidasm_main.cc",
    "/toolchain/cupidc_main.cc",
    "/toolchain/cupidld_main.cc",
    "/toolchain/cupidobj_main.cc",
    "/toolchain/hosted/i386-windows/publication_runtime.cc",
)
_C_PP_TOOLCHAIN_CONTRACT_CASES = (
    "/kernel/lang/as_elf.cc",
    "/toolchain/tests/core_contract.cc",
    "/toolchain/tests/cupidasm_contract.cc",
    "/toolchain/tests/cupidasm_demos_contract.cc",
    "/toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "/toolchain/tests/cupidc_frontend_contract.cc",
    "/toolchain/tests/cupidc_ir_contract.cc",
    "/toolchain/tests/cupidc_object_contract.cc",
    "/toolchain/tests/cupidc_pp_contract.cc",
    "/toolchain/tests/cupidc_type_contract.cc",
    "/toolchain/tests/cupiddis_contract.cc",
    "/toolchain/tests/cupidld_contract.cc",
    "/toolchain/tests/cupidobj_contract.cc",
    "/toolchain/tests/elf32_contract.cc",
    "/toolchain/tests/hosted_i386_windows_contract.cc",
    "/toolchain/tests/toolchain_manifest_contract.cc",
    "/toolchain/tests/user_syscall_abi_contract.cc",
    "/toolchain/tests/x86_contract.cc",
)
_C_PP_GENERATED_KERNEL_CASES = (
    "/kernel/cpu/ksyms_data.cc",
    "/kernel/util/bin_programs_gen.cc",
    "/kernel/util/demos_programs_gen.cc",
    "/kernel/util/docs_programs_gen.cc",
)
_C_PP_NON_ROOT_HEADERS = (
    "/bin/fat16.h",
    "/bin/shell.h",
)
_C_PP_DEFERRED_HOSTED_CASES: tuple[str, ...] = ()
_C_PP_HOSTED_BRIDGE_CASES = frozenset(
    {
        "/kernel/lang/as_elf.cc",
        "/toolchain/tests/cupidasm_kernel_elf_contract.cc",
    }
)


@dataclass
class FeatureEvidence:
    """Aggregated, source-located evidence for one language requirement."""

    occurrences: int = 0
    files: set[str] = field(default_factory=set)
    examples: list[dict[str, object]] = field(default_factory=list)


class FeatureCollector:
    """Collect stable feature identifiers through one small interface."""

    def __init__(self) -> None:
        self._features: dict[str, FeatureEvidence] = {}
        self._by_source: dict[str, set[str]] = {}

    def add(
        self,
        feature_id: str,
        path: str,
        line: int,
        text: str,
        occurrences: int = 1,
    ) -> None:
        if occurrences <= 0:
            return
        evidence = self._features.setdefault(feature_id, FeatureEvidence())
        evidence.occurrences += occurrences
        evidence.files.add(path)
        self._by_source.setdefault(path, set()).add(feature_id)
        example = {"path": path, "line": line, "text": text.strip()[:160]}
        if example not in evidence.examples and len(evidence.examples) < 3:
            evidence.examples.append(example)

    def for_source(self, path: str) -> list[str]:
        return sorted(self._by_source.get(path, set()))

    def inventory(self) -> list[dict[str, object]]:
        return [
            {
                "id": feature_id,
                "category": feature_id.split(".", 2)[1],
                "occurrences": evidence.occurrences,
                "files": sorted(evidence.files),
                "examples": evidence.examples,
            }
            for feature_id, evidence in sorted(self._features.items())
        ]


C_KEYWORD_FEATURES = {
    "_Alignas": "c.type.alignment_specifier",
    "_Alignof": "c.expression.alignof",
    "_Atomic": "c.type.atomic",
    "_Bool": "c.type.bool",
    "_Complex": "c.type.complex",
    "_Generic": "c.expression.generic_selection",
    "_Imaginary": "c.type.imaginary",
    "_Noreturn": "c.function.noreturn",
    "_Static_assert": "c.declaration.static_assert",
    "_Thread_local": "c.storage.thread_local",
    "auto": "c.storage.auto",
    "break": "c.control.break",
    "case": "c.control.case",
    "char": "c.type.char",
    "const": "c.qualifier.const",
    "continue": "c.control.continue",
    "default": "c.control.default",
    "do": "c.control.do",
    "double": "c.type.double",
    "else": "c.control.else",
    "enum": "c.type.enum",
    "extern": "c.storage.extern",
    "float": "c.type.float",
    "for": "c.control.for",
    "goto": "c.control.goto",
    "if": "c.control.if",
    "inline": "c.storage.inline",
    "int": "c.type.int",
    "long": "c.type.long",
    "register": "c.storage.register",
    "restrict": "c.qualifier.restrict",
    "return": "c.control.return",
    "short": "c.type.short",
    "signed": "c.type.signed",
    "sizeof": "c.expression.sizeof",
    "static": "c.storage.static",
    "struct": "c.type.struct",
    "switch": "c.control.switch",
    "typedef": "c.type.typedef",
    "union": "c.type.union",
    "unsigned": "c.type.unsigned",
    "void": "c.type.void",
    "volatile": "c.qualifier.volatile",
    "while": "c.control.while",
}

GNU_C_OPERATOR_FEATURES = {
    "__alignof": "c.extension.gnu_alignof",
    "__alignof__": "c.extension.gnu_alignof",
}

CUPID_TYPE_TOKENS = {
    "Bool": "bool",
    "F64": "f64",
    "I8": "i8",
    "I16": "i16",
    "I32": "i32",
    "I64": "i64",
    "U0": "u0",
    "U8": "u8",
    "U16": "u16",
    "U32": "u32",
    "U64": "u64",
    "double2": "double2",
    "float4": "float4",
}

CUPID_KEYWORD_FEATURES = {
    "class": "cupid_c.declaration.class",
    "del": "cupid_c.expression.del",
    "new": "cupid_c.expression.new",
    "noreg": "cupid_c.storage.noreg",
    "reg": "cupid_c.storage.reg",
}

C_PREPROCESSOR_DIRECTIVES = {
    "define",
    "elif",
    "else",
    "endif",
    "error",
    "if",
    "ifdef",
    "ifndef",
    "include",
    "line",
    "pragma",
    "undef",
}

ASM_DIRECTIVES = {
    "align",
    "bits",
    "common",
    "db",
    "dd",
    "dq",
    "dt",
    "dw",
    "endstruc",
    "equ",
    "extern",
    "global",
    "incbin",
    "istruc",
    "org",
    "rb",
    "rd",
    "reserve",
    "resb",
    "resd",
    "resq",
    "rest",
    "resw",
    "rw",
    "section",
    "segment",
    "struc",
    "times",
}

ASM_PREFIXES = {"a16", "a32", "lock", "o16", "o32", "rep", "repe", "repne"}
ASM_REGISTERS = {
    "al", "ah", "ax", "eax", "bl", "bh", "bx", "ebx",
    "cl", "ch", "cx", "ecx", "dl", "dh", "dx", "edx",
    "si", "esi", "di", "edi", "sp", "esp", "bp", "ebp",
    "cs", "ds", "es", "fs", "gs", "ss", "cr0", "cr2", "cr3", "cr4",
    "dr0", "dr1", "dr2", "dr3", "dr6", "dr7",
    *(f"mm{index}" for index in range(8)),
    *(f"xmm{index}" for index in range(8)),
    *(f"st{index}" for index in range(8)),
}


def _normalized_relative(root: Path, path: Path) -> str | None:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return None


def _language(path: str) -> str | None:
    return SOURCE_SUFFIXES.get(Path(path).suffix.lower())


def _canonical_make_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    return environment


def _run_make_database(root: Path, make: str, target: str) -> str:
    # GNU Make executes recipes containing $(MAKE) even under -n.  Replace the
    # recursive command while printing the database so a missing hosted Cupid
    # tool cannot append a nested Makefile database and overwrite this root's
    # `all` rule during parsing.  Recipes remain unexpanded in `-p` output.
    result = subprocess.run(
        [
            make,
            *CANONICAL_MAKE_VARIABLES,
            "MAKE=:",
            "--no-print-directory",
            "-prRn",
            target,
        ],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=_canonical_make_environment(),
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(
            f"GNU Make could not expand target {target!r}: {detail}"
        )
    return result.stdout


def _audit_python_make_variable() -> str:
    executable = str(Path(sys.executable).resolve())
    shell_word = (
        subprocess.list2cmdline([executable])
        if sys.platform == "win32"
        else shlex.quote(executable)
    )
    return f"PYTHON={shell_word}"


def _read_make_json_list(root: Path, make: str, target: str) -> list[str]:
    result = subprocess.run(
        [
            make,
            *CANONICAL_MAKE_VARIABLES,
            _audit_python_make_variable(),
            "--no-print-directory",
            "-s",
            target,
        ],
        cwd=root,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=_canonical_make_environment(),
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(f"GNU Make target {target!r} failed: {detail}")
    for line in reversed(result.stdout.splitlines()):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, list) and all(isinstance(item, str) for item in value):
            return value
    raise AuditError(f"GNU Make target {target!r} did not emit a JSON string list")


def _parse_make_rules(database: str) -> dict[str, MakeRule]:
    marker = "# Files"
    if marker not in database:
        raise AuditError("GNU Make database did not contain a Files section")

    rules: dict[str, MakeRule] = {}
    current_targets: list[str] = []
    in_files = False
    for raw_line in database.splitlines():
        line = raw_line.rstrip()
        if line == marker:
            in_files = True
            continue
        if not in_files:
            continue
        if line.startswith("# files hash-table stats:"):
            break
        if line.startswith("\t"):
            command = line.lstrip()
            for target in current_targets:
                rules[target].recipe.append(command)
            continue
        if not line or line[0].isspace() or line.startswith("#"):
            continue
        if ":" not in line:
            continue

        target_text, prerequisite_text = line.split(":", 1)
        if "=" in target_text:
            continue
        current_targets = target_text.split()
        prerequisites = [
            item
            for item in prerequisite_text.split()
            if item not in {"|", "FORCE"}
        ]
        for target in current_targets:
            rules[target] = MakeRule(prerequisites=list(prerequisites))

    return rules


def _reachable_rules(rules: dict[str, MakeRule], target: str) -> set[str]:
    if target not in rules:
        raise AuditError(f"GNU Make database has no target named {target!r}")
    reachable: set[str] = set()
    pending = [target]
    while pending:
        current = pending.pop()
        if current in reachable:
            continue
        reachable.add(current)
        rule = rules.get(current)
        if rule is None:
            continue
        pending.extend(
            prerequisite
            for prerequisite in rule.prerequisites
            if prerequisite in rules and prerequisite not in reachable
        )
    return reachable


def _c_include_directives(
    text: str, display_path: str
) -> list[CIncludeDirective]:
    raw_logical_lines = _c_raw_logical_lines(text)
    if not raw_logical_lines:
        return []
    logical_text = "\n".join(line for _, _, line in raw_logical_lines)
    source_lines = _mask_c_comments_preserve_literals(logical_text).split("\n")
    if len(source_lines) != len(raw_logical_lines):
        raise AuditError("C masking changed the logical line count")

    directives: list[CIncludeDirective] = []
    conditional_stack: list[str] = []
    conditional_pattern = re.compile(
        r"^\s*(#|%:)\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$"
    )
    include_pattern = re.compile(r"^\s*(#|%:)\s*include\b(.*)$")
    for (line_number, _original_line, raw_line), source_line in zip(
        raw_logical_lines, source_lines, strict=True
    ):
        conditional_match = conditional_pattern.match(source_line)
        if conditional_match is not None:
            marker, directive, remainder = conditional_match.groups()
            operand = remainder.strip()
            if directive in {"if", "ifdef", "ifndef", "elif"} and operand:
                normalized = " ".join(
                    _normalize_c_preprocessing_tokens(
                        operand, display_path, line_number
                    )
                )
                evidence = f"{marker}{directive} {normalized} at line {line_number}"
            else:
                evidence = f"{marker}{directive} at line {line_number}"
            if directive in {"if", "ifdef", "ifndef"}:
                conditional_stack.append(evidence)
            elif directive in {"elif", "else"} and conditional_stack:
                conditional_stack[-1] = evidence
            elif directive == "endif" and conditional_stack:
                conditional_stack.pop()
            continue

        include_match = include_pattern.match(source_line)
        if include_match is None:
            continue
        marker, remainder = include_match.groups()
        operand = remainder.strip()
        quoted_match = re.fullmatch(r'"([^"\r\n]+)"\s*', operand)
        angle_match = re.fullmatch(r"<([^>\r\n]+)>\s*", operand)
        if quoted_match is not None:
            kind = "quoted"
            spelling: str | None = quoted_match.group(1)
            normalized_operand = f'"{spelling}"'
        elif angle_match is not None:
            kind = "angle"
            spelling = angle_match.group(1)
            normalized_operand = f"<{spelling}>"
        else:
            kind = "pp_tokens"
            spelling = None
            tokens = (
                _normalize_c_preprocessing_tokens(
                    operand, display_path, line_number
                )
                if operand
                else ()
            )
            normalized_operand = " ".join(tokens) if tokens else "<empty>"
        directives.append(
            CIncludeDirective(
                line=line_number,
                marker=marker,
                raw=raw_line.strip()[:160],
                normalized=normalized_operand,
                kind=kind,
                spelling=spelling,
                conditional_stack=tuple(conditional_stack),
            )
        )
    return directives


def _reject_pp_token_include(
    display_path: str, directive: CIncludeDirective
) -> None:
    conditional = (
        " > ".join(directive.conditional_stack)
        if directive.conditional_stack
        else "<unconditional>"
    )
    raise AuditError(
        f"{display_path}:{directive.line}: macro-expanded #include operand "
        "cannot be represented by the deterministic include closure; "
        f"marker={directive.marker!r}; raw={directive.raw!r}; "
        f"normalized={directive.normalized!r}; conditional={conditional!r}"
    )


def _declared_includes(
    path: Path, language: str, display_path: str | None = None
) -> list[tuple[str, str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise AuditError(f"could not read source input {path}: {exc}") from exc
    if language == "assembly":
        return [
            (match.group(1), "assembly")
            for match in re.finditer(
                r'^\s*%include\s+["\']([^"\']+)["\']',
                text,
                flags=re.MULTILINE | re.IGNORECASE,
            )
        ]
    source_name = display_path or path.as_posix()
    includes: list[tuple[str, str]] = []
    for directive in _c_include_directives(text, source_name):
        if directive.kind == "pp_tokens":
            _reject_pp_token_include(source_name, directive)
        if directive.spelling is None:
            raise AuditError(
                f"{source_name}:{directive.line}: include spelling is absent"
            )
        includes.append((directive.spelling, directive.kind))
    return includes


def _make_include_configuration(root: Path) -> tuple[list[str], list[str]]:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return [], []
    text = makefile.read_text(encoding="utf-8", errors="replace")
    logical_text = re.sub(r"\\\r?\n", " ", text)
    include_paths: list[str] = []
    forced_includes: list[str] = []
    for match in re.finditer(r"(?:^|[\s=])-I\s*([^\s]+)", logical_text):
        value = match.group(1).strip('"\'').replace("\\", "/")
        value = re.sub(r"^\./", "", value)
        if "$" not in value and value not in include_paths:
            include_paths.append(value)
    for match in re.finditer(
        r"(?:^|[\s=])-include\s+([^\s]+)", logical_text
    ):
        value = match.group(1).strip('"\'').replace("\\", "/")
        value = re.sub(r"^\./", "", value)
        if "$" not in value and value not in forced_includes:
            forced_includes.append(value)
    return include_paths, forced_includes


def _read_evaluated_make_variables(
    root: Path, make: str, variables: tuple[str, ...]
) -> dict[str, str]:
    target = "__cupid_audit_profile_values__"
    value_names = [f"__CUPID_AUDIT_VALUE_{index}" for index in range(len(variables))]
    origin_names = [
        f"__CUPID_AUDIT_ORIGIN_{index}" for index in range(len(variables))
    ]
    overlay_lines = []
    for index, variable in enumerate(variables):
        overlay_lines.append(f"{value_names[index]} := $({variable})")
        overlay_lines.append(f"{origin_names[index]} := $(origin {variable})")
    overlay_lines.extend((f".PHONY: {target}", f"{target}:", ""))
    result = subprocess.run(
        [
            make,
            *CANONICAL_MAKE_VARIABLES,
            "MAKE=:",
            "--no-print-directory",
            "-prRn",
            "-f",
            "Makefile",
            "-f",
            "-",
            target,
        ],
        cwd=root,
        input="\n".join(overlay_lines),
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        env=_canonical_make_environment(),
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise AuditError(
            f"GNU Make could not evaluate CupidC profile variables in "
            f"{root}: {detail}"
        )
    wanted = set(value_names) | set(origin_names)
    evaluated: dict[str, str] = {}
    assignment_pattern = re.compile(
        r"^(__CUPID_AUDIT_(?:VALUE|ORIGIN)_[0-9]+)\s*:=\s?(.*)$"
    )
    for line in result.stdout.splitlines():
        match = assignment_pattern.match(line)
        if match is not None and match.group(1) in wanted:
            evaluated[match.group(1)] = match.group(2)
    missing = sorted(wanted - set(evaluated))
    if missing:
        raise AuditError(
            f"GNU Make omitted CupidC profile sentinel(s): {missing!r}"
        )
    values: dict[str, str] = {}
    for index, variable in enumerate(variables):
        if evaluated[origin_names[index]] == "undefined":
            raise AuditError(
                f"missing Make variable in CupidC preprocessing profile: "
                f"{variable}"
            )
        values[variable] = evaluated[value_names[index]]
    return values


def _validate_cupidc_kernel_compile_make_binding(
    root: Path,
    make: str,
    transforms: list[dict[str, object]],
) -> None:
    if not any(
        "cupid_c_compiler" in transform.get("tools", [])
        for transform in transforms
    ):
        return

    values = _read_evaluated_make_variables(
        root,
        make,
        (
            "PYTHON",
            "CUPIDC_KERNEL_COMPILE",
            "PRODUCTION_SEED_MANIFEST",
        ),
    )
    try:
        python_tokens = shlex.split(values["PYTHON"])
        wrapper_tokens = shlex.split(values["CUPIDC_KERNEL_COMPILE"])
    except ValueError as error:
        raise AuditError(
            f"CupidC kernel wrapper Make binding cannot be tokenized: {error}"
        ) from error
    if not python_tokens:
        raise AuditError("CupidC kernel wrapper has an empty PYTHON binding")

    executable = Path(
        python_tokens[0].replace("\\", "/")
    ).name.lower()
    if executable.endswith(".exe"):
        executable = executable[:-4]
    if re.fullmatch(r"(?:py|python(?:[0-9]+(?:\.[0-9]+)*)?)", executable) is None:
        raise AuditError(
            "CupidC kernel wrapper PYTHON binding is not a Python launcher: "
            f"{python_tokens[0]!r}"
        )
    if len(python_tokens) != 1:
        raise AuditError(
            "CupidC kernel wrapper PYTHON binding must contain only the "
            f"Python launcher: found {python_tokens!r}"
        )

    expected = [
        python_tokens[0],
        "tools/cupidc_kernel_compile.py",
        "--root",
        ".",
        "--manifest",
        values["PRODUCTION_SEED_MANIFEST"],
    ]
    if wrapper_tokens != expected:
        raise AuditError(
            "CupidC kernel wrapper Make binding differs from the checked "
            f"command: expected {expected!r}, found {wrapper_tokens!r}"
        )


def _python_make_tokens(value: str, label: str) -> list[str]:
    try:
        tokens = shlex.split(value)
    except ValueError as error:
        raise AuditError(f"{label} cannot be tokenized: {error}") from error
    if len(tokens) != 1:
        raise AuditError(
            f"{label} must contain only a Python launcher: {tokens!r}"
        )
    executable = Path(tokens[0].replace("\\", "/")).name.lower()
    if executable.endswith(".exe"):
        executable = executable[:-4]
    if re.fullmatch(
        r"(?:py|python(?:[0-9]+(?:\.[0-9]+)*)?)", executable
    ) is None:
        raise AuditError(f"{label} is not a Python launcher: {tokens[0]!r}")
    return tokens


def _validate_cupidc_production_make_bindings(
    root: Path,
    make: str,
    models: list[BuildModel],
) -> None:
    for model in models:
        recipe_text = "\n".join(
            line
            for transform in model.transforms
            for line in transform.get("recipe", [])
            if isinstance(line, str)
        )
        uses_compile = "$(CUPIDC_PRODUCTION_COMPILE)" in recipe_text
        uses_link = "$(CUPIDLD_USER_LINK)" in recipe_text
        if not uses_compile and not uses_link:
            continue
        make_root = root if model.directory == "." else root / model.directory
        variables = ["PYTHON", "PRODUCTION_SEED_MANIFEST"]
        if uses_compile:
            variables.append("CUPIDC_PRODUCTION_COMPILE")
        if uses_link:
            variables.append("CUPIDLD_USER_LINK")
        values = _read_evaluated_make_variables(
            make_root, make, tuple(variables)
        )
        python_tokens = _python_make_tokens(
            values["PYTHON"],
            f"CupidC production PYTHON binding in {model.directory}",
        )
        if model.directory == ".":
            expected_compile = [
                python_tokens[0],
                "tools/cupidc_production_compile.py",
                "--root",
                ".",
                "--cohort",
                "generated-install",
                "--manifest",
                values["PRODUCTION_SEED_MANIFEST"],
            ]
            if uses_link:
                raise AuditError(
                    "checked user linker may not run from the root build"
                )
        elif model.directory == "user":
            expected_compile = [
                python_tokens[0],
                "../tools/cupidc_production_compile.py",
                "--root",
                "..",
                "--cohort",
                "user",
                "--manifest",
                values["PRODUCTION_SEED_MANIFEST"],
            ]
        else:
            raise AuditError(
                "CupidC production wrapper is bound in an unsupported "
                f"Make root: {model.directory}"
            )
        if uses_compile:
            try:
                actual_compile = shlex.split(
                    values["CUPIDC_PRODUCTION_COMPILE"]
                )
            except ValueError as error:
                raise AuditError(
                    "CupidC production wrapper binding cannot be tokenized: "
                    f"{error}"
                ) from error
            if actual_compile != expected_compile:
                raise AuditError(
                    "CupidC production wrapper binding differs from the "
                    f"checked command in {model.directory}: "
                    f"expected={expected_compile!r}, "
                    f"actual={actual_compile!r}"
                )
        if uses_link:
            if model.directory != "user":
                raise AuditError(
                    "checked user linker is outside the user build"
                )
            expected_link = [
                python_tokens[0],
                "../tools/cupidld_user_link.py",
                "--root",
                "..",
                "--manifest",
                values["PRODUCTION_SEED_MANIFEST"],
            ]
            try:
                actual_link = shlex.split(values["CUPIDLD_USER_LINK"])
            except ValueError as error:
                raise AuditError(
                    "CupidLD user wrapper binding cannot be tokenized: "
                    f"{error}"
                ) from error
            if actual_link != expected_link:
                raise AuditError(
                    "CupidLD user wrapper binding differs from the checked "
                    f"command: expected={expected_link!r}, "
                    f"actual={actual_link!r}"
                )


def _make_preprocessor_flags(
    expanded: str, variable: str
) -> tuple[list[str], list[str], dict[str, str], set[str]]:
    if "$(" in expanded or "${" in expanded:
        raise AuditError(
            f"unmodeled Make reference/function remains in CupidC profile "
            f"{variable}: {expanded!r}"
        )
    try:
        tokens = shlex.split(expanded, posix=True)
    except ValueError as exc:
        raise AuditError(
            f"could not tokenize Make variable {variable}: {exc}"
        ) from exc
    include_paths: list[str] = []
    forced_includes: list[str] = []
    defines: dict[str, str] = {}
    flags: set[str] = set()
    index = 0
    while index < len(tokens):
        token = tokens[index]
        flags.add(token)
        if token == "-I":
            index += 1
            if index >= len(tokens):
                raise AuditError(f"missing -I operand in Make variable {variable}")
            include_paths.append(tokens[index])
        elif token.startswith("-I") and len(token) > 2:
            include_paths.append(token[2:])
        elif token == "-include":
            index += 1
            if index >= len(tokens):
                raise AuditError(
                    f"missing -include operand in Make variable {variable}"
                )
            forced_includes.append(tokens[index])
        elif token == "-D":
            index += 1
            if index >= len(tokens):
                raise AuditError(f"missing -D operand in Make variable {variable}")
            definition = tokens[index]
            macro_name, separator, replacement = definition.partition("=")
            defines[macro_name] = replacement if separator else "1"
        elif token.startswith("-D") and len(token) > 2:
            macro_name, separator, replacement = token[2:].partition("=")
            defines[macro_name] = replacement if separator else "1"
        index += 1
    return include_paths, forced_includes, defines, flags


def _make_flag_logical_path(directory: str, path: str) -> str:
    normalized = path.replace("\\", "/")
    if "$" in normalized or re.match(r"^[A-Za-z]:/", normalized):
        raise AuditError(
            f"non-logical Make path in CupidC preprocessing profile: {path!r}"
        )
    if normalized.startswith("/"):
        relative = normalized[1:]
    else:
        relative = posixpath.normpath(
            normalized
            if directory == "."
            else posixpath.join(directory, normalized)
        )
    if relative in {"", ".", ".."} or relative.startswith("../"):
        raise AuditError(
            f"Make path escapes CupidC preprocessing profile: {path!r}"
        )
    if relative.startswith("./"):
        relative = relative[2:]
    return f"/{relative}"


def _resolve_include(
    root: Path,
    source_path: Path,
    include: str,
    kind: str,
    include_paths: list[str],
) -> str | None:
    search_roots: list[Path] = []
    if kind in {"quoted", "assembly"}:
        search_roots.append(source_path.parent)
    search_roots.extend(root / path for path in include_paths)
    search_roots.append(root)
    for search_root in search_roots:
        candidate = search_root / include
        if not candidate.is_file():
            continue
        normalized = _normalized_relative(root, candidate)
        if normalized is not None and _language(normalized) is not None:
            return normalized
    return None


def _include_closure(
    root: Path,
    direct_sources: set[str],
    include_paths: list[str],
    opaque_sources: set[str] | None = None,
) -> dict[str, list[str]]:
    includes_by_source: dict[str, list[str]] = {}
    opaque_sources = opaque_sources or set()
    pending = sorted(direct_sources)
    seen: set[str] = set()
    while pending:
        relative = pending.pop()
        if relative in seen:
            continue
        seen.add(relative)
        language = _language(relative)
        if language not in {"c", "cupid_c", "c_header", "assembly"}:
            continue
        if relative in opaque_sources:
            includes_by_source[relative] = []
            continue
        source_path = root / relative
        if not source_path.is_file():
            continue

        resolved: list[str] = []
        for include, kind in _declared_includes(
            source_path, language, relative
        ):
            included_relative = _resolve_include(
                root,
                source_path,
                include,
                kind,
                include_paths,
            )
            if included_relative is None or _language(included_relative) is None:
                continue
            resolved.append(included_relative)
            if included_relative not in seen:
                pending.append(included_relative)
        includes_by_source[relative] = sorted(set(resolved))
    return includes_by_source


def _recipe_tokens(recipe: list[str]) -> list[str]:
    return [
        token
        for token in "\n".join(recipe).split()
        if token != "\\"
    ]


def _tools_for_recipe(recipe: list[str]) -> list[str]:
    tokens = _recipe_tokens(recipe)
    joined = " ".join(tokens)
    tools = []
    if "--write-profile-input-manifest" in tokens:
        tools.append("cupid_object")
    for marker, tool in TOOL_MARKERS:
        if marker in joined and tool not in tools:
            tools.append(tool)
    if recipe and not tools:
        return ["host_shell"]
    return tools


def _artifact_coverage_contract(
    root: Path,
    make: str,
    rules: dict[str, MakeRule],
    transforms: list[dict[str, object]],
) -> dict[str, object] | None:
    target = "print-bootstrap-artifacts"
    if target not in rules:
        return None
    declared = set(_read_make_json_list(root, make, target))
    linked_objects = {
        str(source)
        for transform in transforms
        if {"host_linker", "cupid_linker"}.intersection(transform["tools"])
        for source in transform["inputs"]
        if str(source).endswith(".o")
    }
    missing = sorted(linked_objects - declared)
    return {
        "status": "pass" if not missing else "fail",
        "declared_artifacts": len(declared),
        "linked_objects": len(linked_objects),
        "missing_link_inputs": missing,
    }


def _prefix_repo_path(directory: str, path: str) -> str:
    normalized = path.replace("\\", "/")
    if directory in {"", "."} or normalized.startswith("/"):
        return posixpath.normpath(normalized)
    if re.match(r"^[A-Za-z]:/", normalized):
        return normalized
    if normalized.startswith("./"):
        normalized = normalized[2:]
    return posixpath.normpath(f"{directory.rstrip('/')}/{normalized}")


def _operation_for_recipe(
    recipe: list[str],
    tools: list[str],
    output: str,
    c_object_operation: str,
    inputs: list[str],
) -> str:
    joined = " ".join(recipe).lower()
    if (
        "$(toolchain_manifest_contract)" in joined
        and "cupid_c_contract" in tools
    ):
        return "verify_toolchain_manifest"
    if (
        "$(artifact_size_contract)" in joined
        and "cupid_c_contract" in tools
    ):
        return "verify_artifact_size_policy"
    if (
        "cupidc_toolchain_contracts.py build" in joined
        and tools
        == [
            "cupid_assembler",
            "cupid_c_compiler",
            "cupid_c_contract",
            "cupid_linker",
            "host_python",
        ]
    ):
        return "generate_toolchain_manifest"
    if (
        "cupid_object" in tools
        and "--write-profile-input-manifest"
        in (token.lower() for token in _recipe_tokens(recipe))
    ):
        return "generate_profile_manifest"
    if "hostbuild.py build-iso " in joined:
        return "package_iso9660_image"
    if "hostbuild.py image " in joined:
        return "package_disk_image"
    if (
        "hostbuild.py validate-code " in joined
        and "--output" in _recipe_tokens(recipe)
        and "cupid_disassembler" in tools
        and "cupid_object" in tools
    ):
        return "extract_raw_binary"
    if (
        "hostbuild.py assemble-bootloader " in joined
        and "cupid_assembler" in tools
        and "cupid_disassembler" in tools
    ):
        return "assemble_flat_binary"
    if (
        "hostbuild.py assemble-smp-trampoline " in joined
        and "cupid_assembler" in tools
        and "cupid_disassembler" in tools
    ):
        return "assemble_flat_binary"
    if (
        "hostbuild.py assemble-cupidasm-object " in joined
        and "cupid_assembler" in tools
        and "cupid_disassembler" in tools
    ):
        return "assemble_elf32_relocatable"
    if (
        "gen-big" in joined
        and "--seed-manifest" in joined
        and "cupid_assembler" in tools
    ):
        return "assemble_flat_binary"
    if " mksyms " in f" {joined} " and "cupid_object" in tools:
        return "generate_ksyms_source"
    if (
        posixpath.basename(output.replace("\\", "/"))
        == "test-syscall-abi"
        and any(
            posixpath.normpath(path.replace("\\", "/")).endswith(
                "tools/user_syscall_abi.py"
            )
            for path in inputs
        )
    ):
        return "verify_user_syscall_abi"
    if "host_c_compiler" in tools or "cupid_c_compiler" in tools:
        if output.lower().endswith((".o", ".obj")) or re.search(
            r"(?:^|\s)-c(?:\s|$)", joined
        ):
            return c_object_operation
        return "compile_and_link_host_executable"
    if "nasm" in tools or "cupid_assembler" in tools:
        if re.search(r"(?:^|\s)-f\s+bin(?:\s|$)", joined):
            return "assemble_flat_binary"
        if re.search(r"(?:^|\s)-f\s+elf32(?:\s|$)", joined):
            return "assemble_elf32_relocatable"
        return "assemble"
    if "host_linker" in tools or "cupid_linker" in tools:
        return "link_elf32_executable"
    if "host_object_copy" in tools or "cupid_object" in tools:
        install_source_command = re.compile(
            r"^[@+-]*\$\(cupidobj\)\s+install-source(?:\s|$)"
        )
        if any(
            install_source_command.search(command.strip().lower())
            for command in recipe
        ):
            return "generate_install_source"
        if re.search(r"(?:^|\s)wrap-text(?:\s|$)", joined):
            return "wrap_text_as_elf32_relocatable"
        if (
            ("-i binary" in joined and "-o elf32-i386" in joined)
            or re.search(r"(?:^|\s)wrap(?:\s|$)", joined)
            or re.search(r"(?:^|\s)embed-jpeg(?:\s|$)", joined)
        ):
            return "wrap_binary_as_elf32_relocatable"
        if re.search(r"(?:^|\s)-o\s+binary(?:\s|$)", joined) or re.search(
            r"(?:^|\s)flat(?:\s|$)", joined
        ):
            return "extract_raw_binary"
        return "transform_object"
    if "host_python" in tools:
        if " mksyms " in joined or (
            " gen-" in joined
            and output.lower().endswith((".c", ".cc"))
        ):
            return "generate_c_source"
        if " gen-" in joined:
            return "generate_binary_fixture"
        return "host_orchestration"
    if "make" in tools:
        return "recursive_make"
    return "host_command"


def _build_transforms(
    directory: str,
    reachable: set[str],
    rules: dict[str, MakeRule],
) -> list[dict[str, object]]:
    transforms = []
    host_object_outputs = {
        prerequisite
        for local_output in reachable
        if "host_c_compiler" in _tools_for_recipe(rules[local_output].recipe)
        and not local_output.lower().endswith((".o", ".obj"))
        and not re.search(
            r"(?:^|\s)-c(?:\s|$)",
            " ".join(rules[local_output].recipe).lower(),
        )
        for prerequisite in rules[local_output].prerequisites
        if prerequisite.lower().endswith((".o", ".obj"))
    }
    for local_output in sorted(reachable):
        rule = rules[local_output]
        if not rule.recipe:
            continue
        tools = _tools_for_recipe(rule.recipe)
        transforms.append(
            {
                "output": _prefix_repo_path(directory, local_output),
                "inputs": [
                    _prefix_repo_path(directory, item)
                    for item in dict.fromkeys(rule.prerequisites)
                ],
                "tools": tools,
                "operation": _operation_for_recipe(
                    rule.recipe,
                    tools,
                    local_output,
                    (
                        "compile_c_to_host_object"
                        if local_output in host_object_outputs
                        else "compile_c_to_elf32_object"
                    ),
                    rule.prerequisites,
                ),
                "recipe": rule.recipe,
            }
        )
    return transforms


def _collect_build_model(
    root: Path,
    make: str,
    target: str,
    directory: str,
) -> BuildModel:
    normalized_directory = directory.replace("\\", "/").strip("/") or "."
    build_root = root if normalized_directory == "." else root / normalized_directory
    if not (build_root / "Makefile").is_file():
        raise AuditError(
            f"supplemental build directory has no Makefile: {normalized_directory}"
        )

    database = _run_make_database(build_root, make, target)
    rules = _parse_make_rules(database)
    reachable = _reachable_rules(rules, target)
    graph_sources = {
        item
        for rule_target in reachable
        for item in [rule_target, *rules[rule_target].prerequisites]
        if _language(item) is not None
    }
    generated_local = {
        item
        for item in graph_sources
        if item in rules and bool(rules[item].recipe)
    }
    direct_local = {
        item
        for item in graph_sources
        if (build_root / item).is_file() or item in generated_local
    }
    include_paths, forced_include_names = _make_include_configuration(build_root)
    forced_local: set[str] = set()
    for include in forced_include_names:
        resolved = _resolve_include(
            build_root,
            build_root / "Makefile",
            include,
            "angle",
            include_paths,
        )
        if resolved is not None:
            forced_local.add(resolved)
    includes_local = _include_closure(
        build_root,
        direct_local | forced_local,
        include_paths,
        generated_local,
    )

    return BuildModel(
        directory=normalized_directory,
        root_target=target,
        rules=rules,
        reachable=reachable,
        direct_sources={
            _prefix_repo_path(normalized_directory, path) for path in direct_local
        },
        generated_sources={
            _prefix_repo_path(normalized_directory, path) for path in generated_local
        },
        forced_sources={
            _prefix_repo_path(normalized_directory, path) for path in forced_local
        },
        includes_by_source={
            _prefix_repo_path(normalized_directory, source): [
                _prefix_repo_path(normalized_directory, included)
                for included in includes
            ]
            for source, includes in includes_local.items()
        },
        include_search_paths=[
            _prefix_repo_path(normalized_directory, path) for path in include_paths
        ],
        transforms=_build_transforms(normalized_directory, reachable, rules),
    )


def _source_digest(path: Path) -> str:
    """Hash source/control text under the repository's canonical LF policy."""
    content = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return hashlib.sha256(content).hexdigest()


def _source_cohort(path: str, language: str | None, generated: bool) -> str:
    if generated:
        if path == "kernel/cpu/ksyms_data.cc":
            return "generated_symbol_table"
        return "generated_install_table"
    if path.startswith("user/examples/"):
        return "user_program"
    if path.startswith("user/"):
        return "user_runtime_interface"
    if path == "boot/boot.asm":
        return "boot_assembly"
    if path in {
        "kernel/cpu/isr.asm",
        "kernel/core/context_switch.asm",
        "kernel/smp/smp_trampoline.S",
    }:
        return "kernel_assembly"
    if path.startswith("demos/") and language == "assembly":
        return "cupid_asm_demo"
    if path.startswith("bin/browser/") and language == "cupid_c":
        return "cupid_c_browser_fragment"
    if path.startswith("bin/") and language == "cupid_c":
        return "cupid_c_program"
    if path.startswith("bin/") and language == "c_header":
        return "cupid_c_runtime_header"
    if path.startswith("kernel/doom/src/"):
        return "vendored_doom"
    if path.startswith("kernel/doom/"):
        return "doom_port"
    basename = Path(path).name
    if path.startswith("toolchain/tests/"):
        return "toolchain_contract"
    if path.startswith("toolchain/") and basename.startswith("ctool_host"):
        return "toolchain_host_adapter"
    if path.startswith("toolchain/") and basename in {
        "cupiddis.cc",
        "cupiddis.h",
        "cupiddis_main.cc",
    }:
        return "cupiddis"
    if path.startswith("toolchain/") and basename in {
        "cupidasm.cc",
        "cupidasm.h",
        "cupidasm_main.cc",
    }:
        return "cupidasm"
    if path.startswith("toolchain/"):
        return "toolchain_core"
    if path.startswith("kernel/lang/") and basename.startswith("ctool_kernel"):
        return "toolchain_kernel_adapter"
    if path.startswith("kernel/lang/") and basename.startswith("cupidc"):
        return "cupidc"
    if path.startswith("kernel/lang/") and (
        basename in {"as.c", "as.cc", "as.h"} or basename.startswith("as_")
    ):
        return "cupidasm"
    if path.startswith("kernel/lang/") and basename in {
        "dis.c",
        "dis.cc",
        "dis.h",
    }:
        return "cupiddis"
    if path.startswith("drivers/"):
        return "driver"
    if path.startswith("kernel/"):
        parts = path.split("/")
        return f"kernel_{parts[1]}" if len(parts) > 2 else "kernel"
    return "project_source"


def _roadmap(
    sources: list[dict[str, object]],
    features: list[dict[str, object]],
) -> dict[str, object]:
    feature_by_id = {str(feature["id"]): feature for feature in features}
    sources_by_cohort: dict[str, list[str]] = collections.defaultdict(list)
    for source in sources:
        sources_by_cohort[str(source["cohort"])].append(str(source["path"]))

    definitions = [
        (
            "host_runnable_toolchain_core",
            "Establish a host-runnable shared Cupid Toolchain core",
            (
                "toolchain_core",
                "toolchain_host_adapter",
                "toolchain_kernel_adapter",
                "toolchain_contract",
                "cupidc",
                "cupidasm",
                "cupiddis",
            ),
            (),
            "The shared foundations cross hosted and kernel adapters; CupidDis and hosted CupidASM consume them, while CupidC and the CupidASM kernel adapter remain.",
        ),
        (
            "elf32_relocatable_interchange",
            "Emit and consume deterministic ELF32 relocatable objects",
            ("generated_install_table", "generated_symbol_table"),
            (
                "c.output.elf32_relocatable",
                "cupid_c.output.elf32_relocatable",
                "asm.output.elf32_relocatable",
            ),
            "Every compiled C unit and two kernel assembly units cross the ELF32 ET_REL seam.",
        ),
        (
            "shared_i386_abi_and_instruction_model",
            "Share one i386 ABI and instruction model",
            ("kernel_assembly",),
            ("c.extension.inline_assembly", "asm.instruction.", "asm.register."),
            "C code generation, assembly encoding, and disassembly exercise the same 16/32-bit machine domain.",
        ),
        (
            "cupiddis_object_inspection",
            "Make CupidDis inspect raw and ELF32 relocatable output",
            ("cupiddis",),
            ("asm.output.", "asm.relocation."),
            "Assembler migration needs independent sections, symbols, relocation, and instruction evidence.",
        ),
        (
            "cupidasm_source_controls_and_expressions",
            "Implement the active Cupid ASM directives and expression language",
            ("boot_assembly", "kernel_assembly", "cupid_asm_demo"),
            ("asm.directive.", "asm.expression.", "asm.preprocessor."),
            "BITS, ORG, data/reserve forms, times, includes, %define, and label arithmetic gate real sources.",
        ),
        (
            "cupidasm_encoding_and_raw_parity",
            "Reach byte parity for boot and trampoline binaries",
            ("boot_assembly", "kernel_assembly"),
            ("asm.addressing.", "asm.prefix.", "asm.output.flat_binary"),
            "Fixed boot offsets require complete 16/32-bit encoding and ModRM/SIB/address-size behavior.",
        ),
        (
            "cupidasm_symbols_and_relocations",
            "Emit ELF32 sections, symbols, and i386 relocations",
            ("kernel_assembly",),
            ("asm.directive.global", "asm.directive.extern", "asm.relocation."),
            "ISR and context-switch objects must interoperate with host- and CupidC-produced objects.",
        ),
        (
            "cupidc_preprocessor",
            "Implement the active C and Cupid C preprocessing contract",
            (),
            ("c.preprocessor.", "cupid_c.directive."),
            "Includes, forced headers, conditionals, macro rescanning, paste/stringify, and packing affect every C cohort.",
        ),
        (
            "cupidc_c11_types_initializers_and_abi",
            "Implement freestanding C11 type, initializer, and cdecl semantics",
            (),
            ("c.type.", "c.declarator.", "c.initializer.", "c.qualifier."),
            "Kernel and user sources require ILP32 layout, 64-bit arithmetic, callbacks, aggregates, and volatile semantics.",
        ),
        (
            "cupidc_platform_extensions",
            "Implement required GNU attributes and extended inline assembly",
            (),
            ("c.extension.attribute.", "c.extension.inline_assembly"),
            "Core platform and tool sources directly depend on attributes, constraints, clobbers, and privileged instructions.",
        ),
        (
            "cupidc_doom_compatibility",
            "Compile the complete Doom and compatibility cohort",
            ("doom_port", "vendored_doom"),
            (),
            "Vendored Doom adds relaxed diagnostics and legacy declaration/callback compatibility without weakening strict C mode.",
        ),
        (
            "cupid_mode_production_and_extensions",
            "Scale Cupid mode across embedded programs and browser fragments",
            ("cupid_c_program", "cupid_c_browser_fragment"),
            ("cupid_c.",),
            "Production-sized globals/includes come before demo-only class, allocation, register, and SIMD extensions.",
        ),
    ]

    capability_priorities = []
    for identifier, title, cohorts, feature_prefixes, rationale in definitions:
        cohort_paths = {
            path for cohort in cohorts for path in sources_by_cohort.get(cohort, [])
        }
        matched_features = sorted(
            feature_id
            for feature_id in feature_by_id
            if any(
                feature_id == prefix or feature_id.startswith(prefix)
                for prefix in feature_prefixes
            )
        )
        feature_paths = {
            str(path)
            for feature_id in matched_features
            for path in feature_by_id[feature_id]["files"]
        }
        evidence_paths = sorted(cohort_paths | feature_paths)
        if not evidence_paths and not matched_features:
            continue
        capability_priorities.append(
            {
                "rank": len(capability_priorities) + 1,
                "id": identifier,
                "title": title,
                "rationale": rationale,
                "cohorts": list(cohorts),
                "feature_ids": matched_features,
                "source_count": len(evidence_paths),
                "sample_sources": evidence_paths[:12],
            }
        )

    cohort_definitions = [
        (
            "toolchain_sources",
            (
                "toolchain_core",
                "toolchain_host_adapter",
                "toolchain_kernel_adapter",
                "toolchain_contract",
                "cupidc",
                "cupidasm",
                "cupiddis",
            ),
            "Bootstrap the tools that transfer ownership to every later cohort.",
        ),
        (
            "boot_and_kernel_assembly",
            ("boot_assembly", "kernel_assembly"),
            "Keep the four boot and kernel transforms plus the ISO lane fixture CupidASM-owned while retaining NASM only as an optional parity oracle.",
        ),
        (
            "kernel_and_drivers",
            tuple(
                sorted(
                    cohort
                    for cohort in sources_by_cohort
                    if cohort == "driver"
                    or (cohort.startswith("kernel_") and cohort not in {"kernel_assembly"})
                )
            ),
            "Move foundational strict C before vendored compatibility cohorts.",
        ),
        (
            "doom_and_vendored_c",
            ("doom_port", "vendored_doom"),
            "Preserve upstream behavior under a deliberate compatibility mode.",
        ),
        (
            "user_programs",
            ("user_program", "user_runtime_interface"),
            "Keep the checked-seed CupidC and CupidLD user build reproducible on Linux and Windows, keep the native Windows oracle explicit, then stage its validated executables deliberately.",
        ),
        (
            "embedded_cupid_sources",
            (
                "cupid_c_runtime_header",
                "cupid_c_program",
                "cupid_c_browser_fragment",
                "cupid_asm_demo",
            ),
            "Keep runtime CupidC/CupidASM regression corpora active through the host migration.",
        ),
    ]
    source_cohort_order = []
    for identifier, cohorts, rationale in cohort_definitions:
        paths = sorted(
            {path for cohort in cohorts for path in sources_by_cohort.get(cohort, [])}
        )
        if not paths:
            continue
        source_cohort_order.append(
            {
                "rank": len(source_cohort_order) + 1,
                "id": identifier,
                "cohorts": list(cohorts),
                "source_count": len(paths),
                "sample_sources": paths[:12],
                "rationale": rationale,
            }
        )
    return {
        "capability_priorities": capability_priorities,
        "source_cohort_order": source_cohort_order,
    }


def _abi_inventory(root: Path, transforms: list[dict[str, object]]) -> dict[str, object] | None:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return None
    make_text = makefile.read_text(encoding="utf-8", errors="replace")
    if "-m32" not in make_text or (
        "elf_i386" not in make_text and "elf32-i386" not in make_text
    ):
        return None

    linker_script = root / "link.ld"
    linker_record = None
    if linker_script.is_file():
        text = linker_script.read_text(encoding="utf-8", errors="replace")
        features = []
        for token in ("ALIGN", "ASSERT", "COMMON", "ENTRY", "SECTIONS"):
            if re.search(rf"\b{token}\b", text):
                features.append(token)
        if re.search(r"\*\s*\(", text):
            features.append("input_section_wildcards")
        if re.search(r"(?m)^\s*\.\s*=", text):
            features.append("location_counter")
        if re.search(r"(?m)^\s*[A-Za-z_]\w*\s*=", text):
            features.append("symbol_definitions")
        output_sections = sorted(
            set(
                re.findall(
                    r"(?m)^\s*(\.[A-Za-z_][\w.]*)\s*(?:ALIGN\([^)]*\)\s*)?:",
                    text,
                )
            )
        )
        linker_inputs = {
            str(source)
            for transform in transforms
            if transform["operation"] == "link_elf32_executable"
            for source in transform["inputs"]
        }
        linker_record = {
            "path": "link.ld",
            "sha256": _source_digest(linker_script),
            "features": sorted(features),
            "output_sections": output_sections,
            "declared_make_prerequisite": "link.ld" in linker_inputs,
        }

    return {
        "architecture": "i386",
        "endianness": "little",
        "data_model": "ILP32",
        "plain_char": "signed",
        "calling_convention": "cdecl",
        "stack_alignment_bytes": 16 if "-mstackrealign" in make_text else 4,
        "frame_pointer_preserved": "-fno-omit-frame-pointer" in make_text,
        "object_interchange": "ELF32 ET_REL",
        "final_kernel_container": "ELF32 ET_EXEC",
        "required_relocations": ["R_386_32", "R_386_PC32"],
        "linker_script": linker_record,
        "referenced_by_link_flags": "link.ld" in make_text,
    }


def _provenance(
    root: Path,
    models: list[BuildModel],
    sources: list[dict[str, object]],
) -> dict[str, object]:
    generator = Path(__file__).resolve()
    control_files = []
    for model in models:
        relative = (
            "Makefile"
            if model.directory == "."
            else f"{model.directory}/Makefile"
        )
        path = root / relative
        control_files.append(
            {
                "path": relative,
                "sha256": _source_digest(path),
            }
        )
    source_suffix_policy = root / SOURCE_SUFFIX_OWNERSHIP_POLICY
    if source_suffix_policy.is_file():
        control_files.append(
            {
                "path": SOURCE_SUFFIX_OWNERSHIP_POLICY,
                "sha256": _source_digest(source_suffix_policy),
            }
        )
    if any(
        "cupid_c_compiler" in transform.get("tools", [])
        for model in models
        for transform in model.transforms
    ):
        for relative in CUPIDC_KERNEL_CONTROL_FILES:
            path = root / relative
            if not path.is_file():
                raise AuditError(
                    "CupidC ownership control file is missing: "
                    f"{relative}"
                )
            control_files.append(
                {
                    "path": relative,
                    "sha256": _source_digest(path),
                }
            )
    if any(
        any(
            marker in "\n".join(transform.get("recipe", []))
            for marker in (
                "$(CUPIDC_PRODUCTION_COMPILE)",
                "$(CUPIDLD_USER_LINK)",
            )
        )
        for model in models
        for transform in model.transforms
    ):
        for relative in CUPIDC_PRODUCTION_CONTROL_FILES:
            path = root / relative
            if not path.is_file():
                raise AuditError(
                    "CupidC production ownership control file is missing: "
                    f"{relative}"
                )
            control_files.append(
                {
                    "path": relative,
                    "sha256": _source_digest(path),
                }
            )
    aggregate = hashlib.sha256()
    for source in sources:
        aggregate.update(
            (
                f"{source['path']}\0{source['origin']}\0"
                f"{source['sha256'] or 'generated'}\n"
            ).encode("utf-8")
        )
    return {
        "generator": {
            "path": "tools/build_graph_audit.py",
            "sha256": _source_digest(generator),
        },
        "control_files": control_files,
        "active_source_digest": aggregate.hexdigest(),
        "text_hash_policy": "canonical_lf",
    }


def _is_excluded_source_path(path: str) -> bool:
    return any(part.lower() in EXCLUDED_SOURCE_TREES for part in Path(path).parts)


def _tracked_paths(root: Path) -> list[str] | None:
    probe = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--show-toplevel"],
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if probe.returncode != 0:
        return None
    try:
        git_root = Path(probe.stdout.strip()).resolve()
    except OSError:
        return None
    if git_root != root.resolve():
        return None
    listing = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z"],
        capture_output=True,
    )
    if listing.returncode != 0:
        detail = listing.stderr.decode("utf-8", errors="replace").strip()
        raise AuditError(f"git could not enumerate tracked sources: {detail}")
    return [
        item.decode("utf-8", errors="surrogateescape").replace("\\", "/")
        for item in listing.stdout.split(b"\0")
        if item
    ]


def _source_universe(root: Path) -> list[str]:
    candidates = _tracked_paths(root)
    if candidates is None:
        candidates = [
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file()
        ]
    return sorted(
        path
        for path in candidates
        if not _is_excluded_source_path(path)
        and _language(path) is not None
        and (root / path).is_file()
    )


def _load_source_suffix_ownership_policy(root: Path) -> dict[str, object]:
    relative = SOURCE_SUFFIX_OWNERSHIP_POLICY
    path = root / relative
    if not path.is_file():
        if _is_checked_seed_runner_production_root(root):
            raise AuditError(
                f"source suffix ownership policy is missing: {relative}"
            )
        return {
            "path": None,
            "sha256": None,
            "residual_c_sources": {},
            "runtime_delivery_sources": [],
            "unreachable_cupid_c_sources": {},
        }

    def reject_duplicate_keys(
        pairs: list[tuple[str, object]],
    ) -> dict[str, object]:
        decoded: dict[str, object] = {}
        for key, value in pairs:
            if key in decoded:
                raise AuditError(
                    f"source suffix ownership policy repeats key: {key}"
                )
            decoded[key] = value
        return decoded

    try:
        decoded = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=reject_duplicate_keys,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AuditError(
            f"source suffix ownership policy is invalid: {relative}: {error}"
        ) from error
    if not isinstance(decoded, dict):
        raise AuditError("source suffix ownership policy root must be an object")
    if set(decoded) != SOURCE_SUFFIX_POLICY_KEYS:
        missing = sorted(SOURCE_SUFFIX_POLICY_KEYS - set(decoded))
        unknown = sorted(set(decoded) - SOURCE_SUFFIX_POLICY_KEYS)
        raise AuditError(
            "source suffix ownership policy fields differ: "
            f"missing={missing!r}, unknown={unknown!r}"
        )
    if decoded["schema"] != SOURCE_SUFFIX_OWNERSHIP_SCHEMA:
        raise AuditError(
            "source suffix ownership policy schema differs: "
            f"expected={SOURCE_SUFFIX_OWNERSHIP_SCHEMA!r}, "
            f"actual={decoded['schema']!r}"
        )

    residual = decoded["residual_c_sources"]
    deliveries = decoded["runtime_delivery_sources"]
    unreachable_cupid_c = decoded["unreachable_cupid_c_sources"]
    if not isinstance(residual, dict) or not all(
        isinstance(key, str) and isinstance(value, str)
        for key, value in residual.items()
    ):
        raise AuditError(
            "source suffix ownership residual_c_sources must be a string map"
        )
    if not isinstance(deliveries, list) or not all(
        isinstance(value, str) for value in deliveries
    ):
        raise AuditError(
            "source suffix ownership runtime_delivery_sources must be a "
            "string list"
        )
    if not isinstance(unreachable_cupid_c, dict) or not all(
        isinstance(key, str) and isinstance(value, str)
        for key, value in unreachable_cupid_c.items()
    ):
        raise AuditError(
            "source suffix ownership unreachable_cupid_c_sources must be a "
            "string map"
        )

    def validate_path(value: str, suffix: str, subject: str) -> None:
        normalized = posixpath.normpath(value)
        if (
            not value
            or "\\" in value
            or posixpath.isabs(value)
            or re.match(r"^[A-Za-z]:", value) is not None
            or normalized != value
            or value.startswith("../")
            or Path(value).suffix.lower() != suffix
        ):
            raise AuditError(
                f"source suffix ownership {subject} path is invalid: {value!r}"
            )

    if list(residual) != sorted(residual):
        raise AuditError(
            "source suffix ownership residual_c_sources must be path-sorted"
        )
    if deliveries != sorted(deliveries) or len(deliveries) != len(set(deliveries)):
        raise AuditError(
            "source suffix ownership runtime_delivery_sources must be a "
            "unique path-sorted list"
        )
    if list(unreachable_cupid_c) != sorted(unreachable_cupid_c):
        raise AuditError(
            "source suffix ownership unreachable_cupid_c_sources must be "
            "path-sorted"
        )
    for policy_path, role in residual.items():
        validate_path(policy_path, ".c", "residual C")
        if role != "active_host" and role not in SOURCE_SUFFIX_CLASSIFICATIONS:
            raise AuditError(
                "source suffix ownership residual C role is invalid: "
                f"{policy_path}: {role}"
            )
    for policy_path in deliveries:
        validate_path(policy_path, ".cc", "runtime delivery")
    for policy_path, classification in unreachable_cupid_c.items():
        validate_path(policy_path, ".cc", "unreachable Cupid C")
        if classification not in SOURCE_SUFFIX_CLASSIFICATIONS:
            raise AuditError(
                "source suffix ownership unreachable Cupid C classification "
                f"is invalid: {policy_path}: {classification}"
            )
    overlap = sorted(set(deliveries).intersection(unreachable_cupid_c))
    if overlap:
        raise AuditError(
            "source suffix ownership policy assigns active and unreachable "
            f"roles to the same path: {', '.join(overlap)}"
        )

    return {
        "path": relative,
        "sha256": _source_digest(path),
        "residual_c_sources": residual,
        "runtime_delivery_sources": deliveries,
        "unreachable_cupid_c_sources": unreachable_cupid_c,
    }


def _explicitly_excluded_sources(root: Path) -> set[str]:
    makefile = root / "Makefile"
    if not makefile.is_file():
        return set()
    text = makefile.read_text(encoding="utf-8", errors="replace")
    excluded: set[str] = set()
    for match in re.finditer(r"\$\(\s*filter-out\s+([^,\n]+),", text):
        for token in match.group(1).replace("\\", " ").split():
            normalized = token.replace("\\", "/")
            if "$" not in normalized and "%" not in normalized:
                if _language(normalized) is not None:
                    excluded.add(normalized)
    return excluded


def _unreachable_inventory(
    root: Path,
    active_sources: set[str],
) -> list[dict[str, object]]:
    universe = _source_universe(root)
    digests = {path: _source_digest(root / path) for path in universe}
    by_digest: dict[str, list[str]] = collections.defaultdict(list)
    for path, digest in digests.items():
        by_digest[digest].append(path)
    explicitly_excluded = _explicitly_excluded_sources(root)

    inventory = []
    for path in sorted(set(universe) - active_sources):
        duplicate_paths = sorted(
            candidate
            for candidate in by_digest[digests[path]]
            if candidate != path
        )
        relations = []
        known_relation = KNOWN_SOURCE_RELATIONS.get(path)
        known_policy = KNOWN_UNREACHABLE_SOURCE_POLICIES.get(path)
        if known_relation is not None and known_relation[1] in active_sources:
            relations.append(
                {
                    "kind": known_relation[0],
                    "path": known_relation[1],
                    "evidence": "audited project source relationship",
                }
            )
        relations.extend(
            {
                "kind": "exact_content_match",
                "path": candidate,
                "evidence": "canonical source SHA-256 equality",
            }
            for candidate in duplicate_paths
        )
        if path in explicitly_excluded:
            classification = "explicitly_excluded"
            reason = "listed in a Make filter-out expression"
        elif any(
            relation["kind"] == "historical_copy_of" for relation in relations
        ):
            classification = "historical_copy"
            reason = "diverged historical copy of an active implementation"
        elif any(relation["kind"] == "superseded_by" for relation in relations):
            classification = "superseded"
            reason = "replaced by the recorded active implementation"
        elif known_policy is not None:
            classification, reason = known_policy
        elif duplicate_paths:
            classification = "exact_duplicate"
            reason = "content SHA-256 matches another source-like file"
        else:
            classification = "not_reached"
            reason = "not reachable from the supported Make target or include closure"
        inventory.append(
            {
                "path": path,
                "language": _language(path),
                "classification": classification,
                "reason": reason,
                "duplicate_of": duplicate_paths,
                "relations": relations,
                "lines": len(
                    (root / path).read_text(
                        encoding="utf-8", errors="replace"
                    ).splitlines()
                ),
                "sha256": digests[path],
            }
        )
    return inventory


def _c_source_ownership_contract(
    sources: list[dict[str, object]],
    unreachable_sources: list[dict[str, object]],
    policy: dict[str, object],
    runtime_owner_evidence: dict[str, str],
    strict_unreachable_cupid_c_ownership: bool,
    complete_supported_graph: bool,
) -> dict[str, object]:
    active_tracked_c = sorted(
        (
            source
            for source in sources
            if source["origin"] == "tracked" and source["language"] == "c"
        ),
        key=lambda source: str(source["path"]),
    )
    active_tracked_cupid_c = sorted(
        (
            source
            for source in sources
            if source["origin"] == "tracked"
            and source["language"] == "cupid_c"
        ),
        key=lambda source: str(source["path"]),
    )
    unreachable_tracked_c = sorted(
        (
            source
            for source in unreachable_sources
            if source["language"] == "c"
        ),
        key=lambda source: str(source["path"]),
    )
    unreachable_tracked_cupid_c = sorted(
        (
            source
            for source in unreachable_sources
            if source["language"] == "cupid_c"
        ),
        key=lambda source: str(source["path"]),
    )
    active_c_by_path = {
        str(source["path"]): source for source in active_tracked_c
    }
    active_cupid_c_by_path = {
        str(source["path"]): source for source in active_tracked_cupid_c
    }
    unreachable_c_by_path = {
        str(source["path"]): source for source in unreachable_tracked_c
    }
    unreachable_cupid_c_by_path = {
        str(source["path"]): source
        for source in unreachable_tracked_cupid_c
    }
    residual_policy = policy["residual_c_sources"]
    delivery_policy = policy["runtime_delivery_sources"]
    unreachable_cupid_c_policy = policy["unreachable_cupid_c_sources"]
    assert isinstance(residual_policy, dict)
    assert isinstance(delivery_policy, list)
    assert isinstance(unreachable_cupid_c_policy, dict)

    for path, role in residual_policy.items():
        active = active_c_by_path.get(path)
        unreachable = unreachable_c_by_path.get(path)
        if active is None and unreachable is None:
            raise AuditError(
                f"source suffix ownership policy path is missing: {path}"
            )
        if role == "active_host":
            if active is None or active["runtime_owner"] is not None:
                raise AuditError(
                    "source suffix ownership policy expected active host C: "
                    f"{path}"
                )
            if "host_c_compiler" not in active["build_owners"]:
                raise AuditError(
                    "source suffix ownership active host C lacks a host "
                    f"compiler edge: {path}"
                )
        elif unreachable is None or unreachable["classification"] != role:
            actual = (
                "active"
                if active is not None
                else str(unreachable["classification"])
                if unreachable is not None
                else "missing"
            )
            raise AuditError(
                "source suffix ownership residual C classification differs: "
                f"{path}: expected={role}, actual={actual}"
            )

    for path in delivery_policy:
        source = active_cupid_c_by_path.get(path)
        if source is None:
            raise AuditError(
                f"source suffix ownership policy path is missing: {path}"
            )
        owners = set(source["build_owners"])
        if owners != {"cupid_object", "host_python"}:
            raise AuditError(
                "source suffix runtime delivery policy lacks the exact "
                f"CupidObj-only ownership edge: {path}"
            )

    for path, classification in unreachable_cupid_c_policy.items():
        source = unreachable_cupid_c_by_path.get(path)
        if source is None:
            raise AuditError(
                f"source suffix ownership policy path is missing: {path}"
            )
        if source["classification"] != classification:
            raise AuditError(
                "source suffix ownership unreachable Cupid C classification "
                f"differs: {path}: expected={classification}, "
                f"actual={source['classification']}"
            )

    if complete_supported_graph:
        unknown_residual_c = sorted(
            (set(active_c_by_path) | set(unreachable_c_by_path))
            - set(residual_policy)
        )
        if unknown_residual_c:
            raise AuditError(
                "tracked .c source lacks explicit residual ownership policy: "
                + ", ".join(unknown_residual_c)
            )
        unknown_unreachable_cupid_c = sorted(
            set(unreachable_cupid_c_by_path) - set(unreachable_cupid_c_policy)
        )
        if unknown_unreachable_cupid_c:
            raise AuditError(
                "unreachable tracked .cc source lacks explicit ownership "
                "policy: "
                + ", ".join(unknown_unreachable_cupid_c)
            )

    cupidc_owned = [
        str(source["path"])
        for source in active_tracked_c
        if source["runtime_owner"] == "CupidC"
    ]
    if cupidc_owned:
        noun = "source" if len(cupidc_owned) == 1 else "sources"
        raise AuditError(
            f"CupidC-owned tracked .c {noun} must use .cc: "
            + ", ".join(cupidc_owned)
        )
    unproven_active_cupid_c = [
        str(source["path"])
        for source in active_tracked_cupid_c
        if source["runtime_owner"] != "CupidC"
    ]
    if unproven_active_cupid_c:
        subject = (
            "source lacks"
            if len(unproven_active_cupid_c) == 1
            else "sources lack"
        )
        raise AuditError(
            f"tracked .cc {subject} independent CupidC ownership evidence: "
            + ", ".join(unproven_active_cupid_c)
        )
    for source in (
        unreachable_tracked_cupid_c
        if strict_unreachable_cupid_c_ownership
        else []
    ):
        path = str(source["path"])
        if path in unreachable_cupid_c_policy:
            continue
        has_explicit_relation = any(
            relation["kind"] in {"historical_copy_of", "superseded_by"}
            for relation in source["relations"]
        )
        if source["classification"] == "explicitly_excluded" or has_explicit_relation:
            continue
        raise AuditError(
            "unreachable tracked .cc source lacks explicit ownership policy: "
            f"{path}"
        )
    return {
        "status": "pass",
        "policy": {
            "path": policy["path"],
            "sha256": policy["sha256"],
            "residual_c_sources": len(residual_policy),
            "runtime_delivery_sources": len(delivery_policy),
            "unreachable_cupid_c_sources": len(unreachable_cupid_c_policy),
        },
        "tracked_c_sources": len(active_tracked_c) + len(unreachable_tracked_c),
        "active_tracked_c_sources": len(active_tracked_c),
        "cupidc_owned_tracked_c_sources": 0,
        "unreachable_tracked_c_sources": len(unreachable_tracked_c),
        "tracked_cupid_c_sources": (
            len(active_tracked_cupid_c) + len(unreachable_tracked_cupid_c)
        ),
        "active_tracked_cupid_c_sources": len(active_tracked_cupid_c),
        "proven_cupidc_owned_tracked_cupid_c_sources": len(
            active_tracked_cupid_c
        ),
        "unreachable_tracked_cupid_c_sources": len(
            unreachable_tracked_cupid_c
        ),
        "cupid_c_ownership_evidence": dict(
            sorted(
                collections.Counter(
                    runtime_owner_evidence[str(source["path"])]
                    for source in active_tracked_cupid_c
                ).items()
            )
        ),
        "active": [
            {
                "path": source["path"],
                "build_owners": source["build_owners"],
                "runtime_owner": source["runtime_owner"],
            }
            for source in active_tracked_c
        ],
        "unreachable": [
            {
                "path": source["path"],
                "classification": source["classification"],
            }
            for source in unreachable_tracked_c
        ],
        "unreachable_cupid_c": [
            {
                "path": source["path"],
                "classification": source["classification"],
                "policy": (
                    "source_suffix_policy"
                    if source["path"] in unreachable_cupid_c_policy
                    else "tracked_source_relation"
                    if source["relations"]
                    else "make_filter_out"
                ),
            }
            for source in unreachable_tracked_cupid_c
        ],
    }


def _active_assembly_ownership_contract(
    sources: list[dict[str, object]],
) -> dict[str, object]:
    active = sorted(
        (source for source in sources if source["language"] == "assembly"),
        key=lambda source: str(source["path"]),
    )
    active_by_path = {str(source["path"]): source for source in active}
    explicit_classifications = []
    for path, (classification, reason) in sorted(
        KNOWN_ACTIVE_ASSEMBLY_POLICIES.items()
    ):
        if classification not in ACTIVE_ASSEMBLY_POLICY_CLASSIFICATIONS:
            raise AuditError(
                "active assembly ownership classification is invalid: "
                f"{path}: {classification}"
            )
        source = active_by_path.get(path)
        if source is None:
            raise AuditError(
                f"active assembly ownership policy path is missing: {path}"
            )
        if source["build_owners"]:
            raise AuditError(
                "active assembly ownership policy path already has a build "
                f"owner: {path}"
            )
        explicit_classifications.append(
            {
                "path": path,
                "classification": classification,
                "reason": reason,
            }
        )
    ownerless = [source for source in active if not source["build_owners"]]
    uncategorized = [
        source
        for source in ownerless
        if str(source["path"]) not in KNOWN_ACTIVE_ASSEMBLY_POLICIES
    ]
    if uncategorized:
        paths = ", ".join(str(source["path"]) for source in uncategorized)
        subject = "source has" if len(uncategorized) == 1 else "sources have"
        raise AuditError(
            f"active assembly {subject} no build owner or explicit "
            f"classification: {paths}"
        )
    cupidasm_owned = [
        source for source in active if source["runtime_owner"] == "CupidASM"
    ]
    other_owned = [
        source
        for source in active
        if source["build_owners"] and source["runtime_owner"] != "CupidASM"
    ]
    startup_paths = set(TOOLCHAIN_CONTRACT_CUPIDASM_OWNERSHIP_INPUTS)
    return {
        "status": "pass",
        "active_sources": len(active),
        "cupidasm_owned_sources": len(cupidasm_owned),
        "other_owned_sources": len(other_owned),
        "ownerless_sources": len(ownerless),
        "explicit_classifications": explicit_classifications,
        "toolchain_startup_sources": sum(
            str(source["path"]) in startup_paths for source in active
        ),
    }


def _propagate_assembly_include_owners(
    source_build_owners: dict[str, set[str]],
    includes_by_source: dict[str, list[str]],
) -> None:
    """Give nested assembly includes the owners of their including source."""
    changed = True
    while changed:
        changed = False
        for source, includes in includes_by_source.items():
            if _language(source) != "assembly":
                continue
            owners = source_build_owners.get(source, set())
            if not owners:
                continue
            for included in includes:
                if _language(included) != "assembly":
                    continue
                before = len(source_build_owners[included])
                source_build_owners[included].update(owners)
                if len(source_build_owners[included]) != before:
                    changed = True


def _mask_c_noncode(text: str) -> str:
    """Mask comments and literals while retaining code positions and line numbers."""
    output: list[str] = []
    index = 0
    state = "code"
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            if char in {'"', "'"}:
                output.append(" ")
                index += 1
                state = "string" if char == '"' else "character"
                continue
            output.append(char)
            index += 1
            continue
        if state == "line_comment":
            output.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
            continue
        if state == "block_comment":
            if char == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
                continue
            output.append("\n" if char == "\n" else " ")
            index += 1
            continue
        if char == "\\" and following:
            output.append(" ")
            output.append("\n" if following == "\n" else " ")
            index += 2
            continue
        delimiter = '"' if state == "string" else "'"
        output.append("\n" if char == "\n" else " ")
        index += 1
        if char == delimiter:
            state = "code"
    return "".join(output)


def _mask_c_comments_preserve_literals(text: str) -> str:
    """Mask comments while retaining literal preprocessing-token spelling."""
    output: list[str] = []
    index = 0
    state = "code"
    delimiter = ""
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            output.append(char)
            index += 1
            if char in {'"', "'"}:
                delimiter = char
                state = "literal"
            continue
        if state == "line_comment":
            output.append("\n" if char == "\n" else " ")
            index += 1
            if char == "\n":
                state = "code"
            continue
        if state == "block_comment":
            if char == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
                continue
            output.append("\n" if char == "\n" else " ")
            index += 1
            continue
        output.append(char)
        index += 1
        if char == "\\" and index < len(text):
            escaped = text[index]
            output.append(escaped)
            index += 1
        elif char == delimiter:
            state = "code"
    return "".join(output)


def _add_regex_feature(
    collector: FeatureCollector,
    feature_id: str,
    path: str,
    line_number: int,
    original_line: str,
    code_line: str,
    pattern: str,
    flags: int = 0,
) -> None:
    occurrences = len(re.findall(pattern, code_line, flags=flags))
    collector.add(feature_id, path, line_number, original_line, occurrences)


def _c_physical_lines(text: str) -> list[tuple[str, bool]]:
    """Split only the CR/LF sequences that phase two treats as newlines."""
    lines: list[tuple[str, bool]] = []
    start = 0
    index = 0
    while index < len(text):
        if text[index] not in {"\r", "\n"}:
            index += 1
            continue
        lines.append((text[start:index], True))
        if (
            text[index] == "\r"
            and index + 1 < len(text)
            and text[index + 1] == "\n"
        ):
            index += 2
        else:
            index += 1
        start = index
    if start < len(text):
        lines.append((text[start:], False))
    return lines


def _c_raw_logical_lines(text: str) -> list[tuple[int, str, str]]:
    """Return phase-two logical lines before comments and literals are masked."""
    raw_lines = _c_physical_lines(text)
    raw_logical_lines: list[tuple[int, str, str]] = []
    chunks: list[str] = []
    start_index = 0
    for index, (raw_body, terminated) in enumerate(raw_lines):
        if not chunks:
            start_index = index
        continued = terminated and raw_body.endswith("\\")
        chunks.append(raw_body[:-1] if continued else raw_body)
        if continued:
            continue
        raw_logical_lines.append(
            (
                start_index + 1,
                raw_lines[start_index][0],
                "".join(chunks),
            )
        )
        chunks = []

    if chunks:
        raw_logical_lines.append(
            (
                start_index + 1,
                raw_lines[start_index][0],
                "".join(chunks),
            )
        )
    return raw_logical_lines


def _c_logical_lines(text: str) -> list[tuple[int, str, str]]:
    """Return phase-two logical lines with their first physical location."""
    raw_logical_lines = _c_raw_logical_lines(text)
    if not raw_logical_lines:
        return []

    masked_text = _mask_c_noncode(
        "\n".join(code_line for _, _, code_line in raw_logical_lines)
    )
    masked_lines = masked_text.split("\n")
    if len(masked_lines) != len(raw_logical_lines):
        raise AuditError("C masking changed the logical line count")
    return [
        (line_number, original_line, masked_line)
        for (line_number, original_line, _), masked_line in zip(
            raw_logical_lines, masked_lines, strict=True
        )
    ]


_C_PP_PUNCTUATORS = tuple(
    sorted(
        {
            "%:%:",
            ">>=",
            "<<=",
            "...",
            "##",
            "->",
            "++",
            "--",
            "<<",
            ">>",
            "<=",
            ">=",
            "==",
            "!=",
            "&&",
            "||",
            "*=",
            "/=",
            "%=",
            "+=",
            "-=",
            "&=",
            "^=",
            "|=",
            "<:",
            ":>",
            "<%",
            "%>",
            "%:",
            "[",
            "]",
            "(",
            ")",
            "{",
            "}",
            ".",
            "&",
            "*",
            "+",
            "-",
            "~",
            "!",
            "/",
            "%",
            "<",
            ">",
            "^",
            "|",
            "?",
            ":",
            ";",
            "=",
            ",",
            "#",
        },
        key=lambda spelling: (-len(spelling), spelling),
    )
)


def _c_ucn_width(text: str, index: int) -> int:
    if index + 2 > len(text) or text[index] != "\\":
        return 0
    marker = text[index + 1]
    digits = 4 if marker == "u" else 8 if marker == "U" else 0
    if digits == 0 or index + 2 + digits > len(text):
        return 0
    spelling = text[index + 2 : index + 2 + digits]
    valid = all(char in "0123456789abcdefABCDEF" for char in spelling)
    return 2 + digits if valid else 0


def _c_identifier_unit_width(text: str, index: int, initial: bool) -> int:
    char = text[index]
    if char == "_" or char.isalpha() or (not initial and char.isdigit()):
        return 1
    return _c_ucn_width(text, index)


def _c_literal_end(text: str, index: int) -> int:
    delimiter_index = index
    if text.startswith("u8", index) and index + 2 < len(text):
        delimiter_index = index + 2
    elif text[index] in {"L", "u", "U"} and index + 1 < len(text):
        delimiter_index = index + 1
    if text[delimiter_index] not in {'"', "'"}:
        return index
    delimiter = text[delimiter_index]
    cursor = delimiter_index + 1
    while cursor < len(text):
        if text[cursor] == "\\":
            if cursor + 1 >= len(text) or text[cursor + 1] in {"\r", "\n"}:
                return index
            cursor += 2
            continue
        if text[cursor] == delimiter:
            return cursor + 1
        if text[cursor] in {"\r", "\n"}:
            return index
        cursor += 1
    return index


def _c_pp_number_end(text: str, index: int) -> int:
    cursor = index + 1
    while cursor < len(text):
        char = text[cursor]
        if char == "." or char == "_" or char.isalnum():
            cursor += 1
            continue
        ucn_width = _c_ucn_width(text, cursor)
        if ucn_width != 0:
            cursor += ucn_width
            continue
        if char in {"+", "-"} and text[cursor - 1] in {"e", "E", "p", "P"}:
            cursor += 1
            continue
        break
    return cursor


def _normalize_c_preprocessing_tokens(
    expression: str, path: str, line: int
) -> tuple[str, ...]:
    tokens: list[str] = []
    index = 0
    while index < len(expression):
        if expression[index].isspace():
            index += 1
            continue
        literal_end = _c_literal_end(expression, index)
        if literal_end != index:
            tokens.append(expression[index:literal_end])
            index = literal_end
            continue
        identifier_width = _c_identifier_unit_width(
            expression, index, initial=True
        )
        if identifier_width != 0:
            end = index + identifier_width
            while end < len(expression):
                width = _c_identifier_unit_width(expression, end, initial=False)
                if width == 0:
                    break
                end += width
            tokens.append(expression[index:end])
            index = end
            continue
        if expression[index].isdigit() or (
            expression[index] == "."
            and index + 1 < len(expression)
            and expression[index + 1].isdigit()
        ):
            end = _c_pp_number_end(expression, index)
            tokens.append(expression[index:end])
            index = end
            continue
        punctuator = next(
            (
                spelling
                for spelling in _C_PP_PUNCTUATORS
                if expression.startswith(spelling, index)
            ),
            None,
        )
        if punctuator is not None:
            tokens.append(punctuator)
            index += len(punctuator)
            continue
        excerpt = expression[index : index + 12]
        raise AuditError(
            f"{path}:{line}: unrecognized preprocessing token at {excerpt!r}"
        )
    if not tokens:
        raise AuditError(f"{path}:{line}: conditional expression is empty")
    return tuple(tokens)


def _scan_c_macro_features(
    path: str,
    logical_lines: list[tuple[int, str, str]],
    collector: FeatureCollector,
) -> None:
    for line_number, original_line, code_line in logical_lines:
        macro_match = re.match(
            r"\s*(?:#|%:)\s*define\s+[A-Za-z_]\w*\(([^)]*)\)", code_line
        )
        if macro_match:
            collector.add(
                "c.preprocessor.function_macro", path, line_number, original_line
            )
            if "..." in macro_match.group(1):
                collector.add(
                    "c.preprocessor.variadic_macro",
                    path,
                    line_number,
                    original_line,
                )
        define_match = re.match(
            r"\s*(?:#|%:)\s*define\s+[A-Za-z_]\w*(?:\([^)]*\))?\s*(.*)$",
            code_line,
        )
        if define_match:
            replacement = define_match.group(1)
            paste_count, stringify_count = _c_macro_operator_counts(replacement)
            collector.add(
                "c.preprocessor.token_paste",
                path,
                line_number,
                original_line,
                paste_count,
            )
            collector.add(
                "c.preprocessor.stringify",
                path,
                line_number,
                original_line,
                stringify_count,
            )
            collector.add(
                "c.preprocessor.gnu_variadic_comma_elision",
                path,
                line_number,
                original_line,
                len(
                    re.findall(
                        r",\s*(?:##|%:%:)\s*__VA_ARGS__\b", replacement
                    )
                ),
            )


def _c_macro_operator_counts(replacement: str) -> tuple[int, int]:
    """Count paste and parameter-stringify tokens with C longest matching."""
    paste_count = 0
    stringify_count = 0
    index = 0
    while index < len(replacement):
        if replacement.startswith("%:%:", index):
            paste_count += 1
            index += 4
            continue
        if replacement.startswith("##", index):
            paste_count += 1
            index += 2
            continue
        width = 0
        if replacement.startswith("%:", index):
            width = 2
        elif replacement[index] == "#":
            width = 1
        if width != 0:
            operand = index + width
            while operand < len(replacement) and replacement[operand].isspace():
                operand += 1
            if operand < len(replacement) and (
                replacement[operand] == "_"
                or "A" <= replacement[operand] <= "Z"
                or "a" <= replacement[operand] <= "z"
            ):
                stringify_count += 1
            index += width
            continue
        index += 1
    return paste_count, stringify_count


def _c_attribute_names(contents: str) -> list[str]:
    items: list[str] = []
    start = 0
    depth = 0
    for index, char in enumerate(contents):
        if char == "(":
            depth += 1
        elif char == ")" and depth != 0:
            depth -= 1
        elif char == "," and depth == 0:
            items.append(contents[start:index])
            start = index + 1
    items.append(contents[start:])
    names: list[str] = []
    for item in items:
        match = re.match(r"\s*([A-Za-z_]\w*)\b", item)
        if match is not None:
            names.append(match.group(1).strip("_").lower())
    return names


def _scan_c_attributes(
    path: str,
    logical_lines: list[tuple[int, str, str]],
    collector: FeatureCollector,
) -> None:
    code = "\n".join(code_line for _, _, code_line in logical_lines)
    line_starts: list[int] = []
    offset = 0
    for _, _, code_line in logical_lines:
        line_starts.append(offset)
        offset += len(code_line) + 1
    cursor = 0
    introducer = re.compile(r"\b__attribute(?:__)?\b")
    while True:
        match = introducer.search(code, cursor)
        if match is None:
            return
        position = match.end()
        while position < len(code) and code[position].isspace():
            position += 1
        if position >= len(code) or code[position] != "(":
            cursor = match.end()
            continue
        position += 1
        while position < len(code) and code[position].isspace():
            position += 1
        if position >= len(code) or code[position] != "(":
            cursor = match.end()
            continue
        contents_start = position + 1
        position = contents_start
        depth = 0
        contents_end: int | None = None
        group_end: int | None = None
        while position < len(code):
            char = code[position]
            if char == "(":
                depth += 1
            elif char == ")":
                if depth != 0:
                    depth -= 1
                else:
                    close = position + 1
                    while close < len(code) and code[close].isspace():
                        close += 1
                    if close < len(code) and code[close] == ")":
                        contents_end = position
                        group_end = close + 1
                        break
            position += 1
        if contents_end is None or group_end is None:
            cursor = match.end()
            continue
        line_index = bisect.bisect_right(line_starts, match.start()) - 1
        line_number, original_line, _ = logical_lines[line_index]
        counts = collections.Counter(
            _c_attribute_names(code[contents_start:contents_end])
        )
        for name in sorted(counts):
            collector.add(
                f"c.extension.attribute.{name}",
                path,
                line_number,
                original_line,
                counts[name],
            )
        cursor = group_end


def _scan_c_features(
    path: str,
    text: str,
    language: str,
    collector: FeatureCollector,
) -> None:
    logical_lines = _c_logical_lines(text)
    _scan_c_macro_features(path, logical_lines, collector)
    _scan_c_attributes(path, logical_lines, collector)
    for line_number, original_line, code_line in logical_lines:
        tokens = re.findall(r"\b[A-Za-z_]\w*\b", code_line)
        for token in sorted(set(tokens)):
            feature_id = C_KEYWORD_FEATURES.get(token)
            if feature_id is not None:
                collector.add(
                    feature_id,
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )
            feature_id = GNU_C_OPERATOR_FEATURES.get(token)
            if feature_id is not None:
                collector.add(
                    feature_id,
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )
            if language == "cupid_c" and token in CUPID_TYPE_TOKENS:
                collector.add(
                    f"cupid_c.type.{CUPID_TYPE_TOKENS[token]}",
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )
            if language == "cupid_c" and token in CUPID_KEYWORD_FEATURES:
                collector.add(
                    CUPID_KEYWORD_FEATURES[token],
                    path,
                    line_number,
                    original_line,
                    tokens.count(token),
                )

        directive_match = re.match(
            r"\s*(?:#|%:)\s*([A-Za-z_]\w*)", code_line
        )
        if directive_match:
            directive = directive_match.group(1).lower()
            feature_id = (
                f"c.preprocessor.{directive}"
                if directive in C_PREPROCESSOR_DIRECTIVES or language != "cupid_c"
                else f"cupid_c.directive.{directive}"
            )
            collector.add(
                feature_id, path, line_number, original_line
            )
        if re.match(r"\s*(?:#|%:)\s*pragma\s+pack\b", code_line):
            collector.add(
                "c.preprocessor.pragma.pack", path, line_number, original_line
            )
        if re.match(r"\s*(?:#|%:)\s*pragma\s+once\b", code_line):
            collector.add(
                "c.preprocessor.pragma.once", path, line_number, original_line
            )

        _add_regex_feature(
            collector,
            "c.declarator.function_pointer",
            path,
            line_number,
            original_line,
            code_line,
            r"\(\s*\*\s*[A-Za-z_]\w*\s*\)\s*\(",
        )
        _add_regex_feature(
            collector,
            "c.declarator.unsized_array",
            path,
            line_number,
            original_line,
            code_line,
            r"\b[A-Za-z_]\w*\s*\[\s*\]\s*(?:[;,=])",
        )
        _add_regex_feature(
            collector,
            "c.declarator.variadic",
            path,
            line_number,
            original_line,
            code_line,
            r"\.\.\.",
        )
        _add_regex_feature(
            collector,
            "c.type.long_long",
            path,
            line_number,
            original_line,
            code_line,
            r"\blong\s+long\b",
        )
        bit_field_count = int(
            re.fullmatch(
                r"\s*(?!(?:case|default)\b)"
                r"(?:(?:const|volatile|signed|unsigned|short|long|_Atomic)\s+)*"
                r"(?:(?:struct|union|enum)\s+[A-Za-z_]\w*|[A-Za-z_]\w*)"
                r"(?:\s+|\s*\*+\s*)(?:[A-Za-z_]\w*)?\s*"
                r":\s*(?:\d+|[A-Za-z_]\w*)\s*;\s*",
                code_line,
            )
            is not None
        )
        collector.add(
            "c.declarator.bit_field",
            path,
            line_number,
            original_line,
            bit_field_count,
        )
        designated_count = len(
            re.findall(
                r"(?:^|[{,])\s*(?:\.[A-Za-z_]\w*|\[[^\]]+\])\s*=",
                code_line,
            )
        )
        collector.add(
            "c.initializer.designated",
            path,
            line_number,
            original_line,
            designated_count,
        )
        compound_count = 0
        for match in re.finditer(
            r"\(\s*(?:(?:struct|union)\s+)?[A-Za-z_]\w*(?:\s*\*)?\s*\)\s*\{",
            code_line,
        ):
            prefix = code_line[:match.start()].rstrip()
            if prefix.endswith((")", "]")):
                continue
            if prefix and (prefix[-1].isalnum() or prefix[-1] == "_"):
                preceding = re.search(r"([A-Za-z_]\w*)$", prefix)
                if preceding is None or preceding.group(1) != "return":
                    continue
            compound_count += 1
        collector.add(
            "c.expression.compound_literal",
            path,
            line_number,
            original_line,
            compound_count,
        )
        _add_regex_feature(
            collector,
            "c.extension.inline_assembly",
            path,
            line_number,
            original_line,
            code_line,
            r"\b(?:asm|__asm|__asm__)\b",
        )
        if language == "cupid_c":
            _add_regex_feature(
                collector,
                "cupid_c.extension.asm_block",
                path,
                line_number,
                original_line,
                code_line,
                r"\basm\s*\{",
            )
        _add_regex_feature(
            collector,
            "c.extension.statement_expression",
            path,
            line_number,
            original_line,
            code_line,
            r"\(\s*\{",
        )
        _add_regex_feature(
            collector,
            "c.extension.typeof",
            path,
            line_number,
            original_line,
            code_line,
            r"\b(?:typeof|__typeof|__typeof__)\b",
        )
        for builtin in sorted(set(re.findall(r"\b(__builtin_[A-Za-z_]\w*)\b", code_line))):
            collector.add(
                f"c.extension.builtin.{builtin.removeprefix('__builtin_').lower()}",
                path,
                line_number,
                original_line,
                code_line.count(builtin),
            )
def _mask_asm_strings(line: str) -> str:
    """Replace quoted ASM data with spaces while preserving source positions."""
    output = list(line)
    quote: str | None = None
    escaped = False
    for index, char in enumerate(line):
        if quote is None:
            if char in {'"', "'"}:
                quote = char
                output[index] = " "
            continue

        output[index] = " "
        if escaped:
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == quote:
            quote = None
    return "".join(output)


def _strip_asm_comment(line: str) -> str:
    comment_index = _mask_asm_strings(line).find(";")
    return line if comment_index < 0 else line[:comment_index]


def _asm_bracketed_directive(line: str) -> str | None:
    match = re.fullmatch(
        r"\[\s*(bits|org)\b[^\]]*\]\s*", line, flags=re.IGNORECASE
    )
    return match.group(1).lower() if match is not None else None


def _scan_asm_features(path: str, text: str, collector: FeatureCollector) -> None:
    extern_symbols: set[str] = set()
    for raw_line in text.splitlines():
        declaration = re.match(
            r"^\s*extern\s+(.+)$",
            _strip_asm_comment(raw_line),
            flags=re.IGNORECASE,
        )
        if declaration:
            extern_symbols.update(
                re.findall(r"[A-Za-z_.$?][\w.$@?]*", declaration.group(1))
            )

    for line_number, original_line in enumerate(text.splitlines(), start=1):
        code_line = _strip_asm_comment(original_line).strip()
        if not code_line:
            continue

        label_match = re.match(r"^([.$?A-Za-z_][\w.$@?]*):", code_line)
        if label_match:
            label = label_match.group(1)
            collector.add(
                "asm.label.local" if label.startswith(".") else "asm.label.global",
                path,
                line_number,
                original_line,
            )
            code_line = code_line[label_match.end():].lstrip()
            if not code_line:
                continue

        bracketed_directive = _asm_bracketed_directive(code_line)
        scan_line = (
            bracketed_directive if bracketed_directive is not None else code_line
        )
        first_match = re.match(r"([^\s,]+)", scan_line)
        if first_match is None:
            continue
        first = first_match.group(1).lower()
        mnemonic: str | None = None
        if first.startswith("%"):
            collector.add(
                f"asm.preprocessor.{first[1:]}", path, line_number, original_line
            )
        else:
            tokens = re.findall(r"[A-Za-z_][\w.$@?]*", scan_line.lower())
            directive = None
            if first in ASM_DIRECTIVES:
                directive = first
            elif len(tokens) >= 2 and tokens[1] in ASM_DIRECTIVES:
                directive = tokens[1]
            if directive is not None:
                collector.add(
                    f"asm.directive.{directive}", path, line_number, original_line
                )
            if directive is None:
                if first in ASM_PREFIXES:
                    collector.add(
                        f"asm.prefix.{first}", path, line_number, original_line
                    )
                instruction_index = 1 if first in ASM_PREFIXES else 0
                if instruction_index < len(tokens):
                    mnemonic = tokens[instruction_index]
                    if mnemonic not in ASM_DIRECTIVES:
                        collector.add(
                            f"asm.instruction.{mnemonic}",
                            path,
                            line_number,
                            original_line,
                        )
            if first == "times":
                for data_directive in ("db", "dw", "dd", "dq", "dt"):
                    if re.search(rf"\b{data_directive}\b", code_line, re.IGNORECASE):
                        collector.add(
                            f"asm.directive.{data_directive}",
                            path,
                            line_number,
                            original_line,
                        )

        words = re.findall(r"\b[A-Za-z_][A-Za-z_0-9]*\b", code_line.lower())
        for register in sorted(set(words) & ASM_REGISTERS):
            collector.add(
                f"asm.register.{register}",
                path,
                line_number,
                original_line,
                words.count(register),
            )
        memory_line = _mask_asm_strings(code_line)
        memory_operands = re.findall(r"\[[^\[\]]*\]", memory_line)
        if bracketed_directive is None and memory_operands:
            collector.add("asm.addressing.memory", path, line_number, original_line)
            if any(
                re.search(r"\*\s*(?:2|4|8)\b", operand)
                for operand in memory_operands
            ):
                collector.add(
                    "asm.addressing.base_index_scale",
                    path,
                    line_number,
                    original_line,
                )
        if re.search(r"\b(?:cs|ds|es|fs|gs|ss)\s*:", code_line, re.IGNORECASE):
            collector.add(
                "asm.addressing.segment_override", path, line_number, original_line
            )
        for size in ("byte", "word", "dword", "qword", "tword"):
            if re.search(rf"\b{size}\s*(?:ptr\s*)?\[", code_line, re.IGNORECASE):
                collector.add(
                    f"asm.addressing.size.{size}", path, line_number, original_line
                )
        if "$$" in code_line:
            collector.add(
                "asm.expression.section_origin", path, line_number, original_line
            )
        elif "$" in code_line:
            collector.add(
                "asm.expression.current_offset", path, line_number, original_line
            )
        if first != "extern":
            referenced_externals = [
                symbol
                for symbol in extern_symbols
                if re.search(rf"(?<![\w.$@?]){re.escape(symbol)}(?![\w.$@?])", code_line)
            ]
            if referenced_externals:
                relocation = (
                    "pc_relative_external"
                    if mnemonic is not None
                    and (mnemonic == "call" or mnemonic == "jmp" or mnemonic.startswith("j"))
                    else "absolute_external"
                )
                collector.add(
                    f"asm.relocation.{relocation}",
                    path,
                    line_number,
                    original_line,
                    len(referenced_externals),
                )


def _scan_source_features(
    path: str,
    source_path: Path,
    language: str,
    collector: FeatureCollector,
) -> None:
    text = source_path.read_text(encoding="utf-8", errors="replace")
    if language in {"c", "c_header", "cupid_c"}:
        _scan_c_features(path, text, language, collector)
    elif language == "assembly":
        _scan_asm_features(path, text, collector)


def _c_preprocessor_include_operands_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    source_files = 0
    include_occurrences = 0
    direct_quoted_occurrences = 0
    direct_angle_occurrences = 0
    pp_token_operand_occurrences = 0
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    max_conditional_depth = 0
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        source_files += 1
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        for directive in _c_include_directives(text, path):
            include_occurrences += 1
            if directive.kind == "quoted":
                direct_quoted_occurrences += 1
            elif directive.kind == "angle":
                direct_angle_occurrences += 1
            else:
                pp_token_operand_occurrences += 1
                _reject_pp_token_include(path, directive)
            if directive.marker == "#":
                ordinary_marker_occurrences += 1
            else:
                digraph_marker_occurrences += 1
            max_conditional_depth = max(
                max_conditional_depth, len(directive.conditional_stack)
            )
    return {
        "status": "pass",
        "source_files": source_files,
        "include_occurrences": include_occurrences,
        "direct_quoted_occurrences": direct_quoted_occurrences,
        "direct_angle_occurrences": direct_angle_occurrences,
        "pp_token_operand_occurrences": pp_token_operand_occurrences,
        "ordinary_marker_occurrences": ordinary_marker_occurrences,
        "digraph_marker_occurrences": digraph_marker_occurrences,
        "max_conditional_depth": max_conditional_depth,
    }


def _c_preprocessor_line_directives_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[
        tuple[str, str, bool | None, int], list[dict[str, object]]
    ] = collections.defaultdict(list)
    source_files = 0
    named_line_occurrences = 0
    direct_line_occurrences = 0
    pp_token_line_occurrences = 0
    filename_occurrences = 0
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    numeric_marker_occurrences = 0
    max_conditional_depth = 0
    temple_sources = sorted(
        path
        for path in active_sources
        if _language(path) in {"c", "c_header", "cupid_c"}
        and path.replace("\\", "/").split("/", 1)[0].casefold()
        == "templeos"
    )
    if temple_sources:
        raise AuditError(
            f"{temple_sources[0]}: TempleOS reference tree cannot be an "
            "active C preprocessing input"
        )
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        source_files += 1
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        payload_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(payload_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        conditional_depth = 0
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            payload_line,
        ) in zip(
            logical_lines,
            directive_lines,
            payload_lines,
            strict=True,
        ):
            conditional_match = re.match(
                r"\s*(?:#|%:)\s*(if|ifdef|ifndef|endif)\b",
                directive_line,
            )
            if conditional_match is not None:
                if conditional_match.group(1) == "endif":
                    conditional_depth = max(0, conditional_depth - 1)
                else:
                    conditional_depth += 1
                continue

            match = re.match(r"\s*(#|%:)\s*line\b", directive_line)
            if match is None:
                numeric_match = re.match(
                    r"\s*(#|%:)\s*([0-9]+)(?=\s|$)", directive_line
                )
                if numeric_match is None:
                    continue
                marker, line_number_token = numeric_match.groups()
                payload = (
                    line_number_token + payload_line[numeric_match.end() :]
                )
                tokens = _normalize_c_preprocessing_tokens(
                    payload, path, line_number
                )
                has_filename = (
                    len(tokens) >= 2
                    and tokens[1].startswith('"')
                    and tokens[1].endswith('"')
                )
                numeric_marker_occurrences += 1
                max_conditional_depth = max(
                    max_conditional_depth, conditional_depth
                )
                by_form[
                    (
                        "numeric_marker",
                        marker,
                        has_filename,
                        conditional_depth,
                    )
                ].append(
                    {
                        "path": path,
                        "line": line_number,
                        "text": raw_line.strip()[:160],
                        "operand": " ".join(tokens),
                    }
                )
                continue
            marker = match.group(1)
            payload = payload_line[match.end() :]
            if not payload.strip():
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #line form: "
                    "empty operand"
                )
            tokens = _normalize_c_preprocessing_tokens(
                payload, path, line_number
            )
            direct_decimal = re.fullmatch(r"[0-9]+", tokens[0]) is not None
            has_filename = (
                len(tokens) == 2
                and tokens[1].startswith('"')
                and tokens[1].endswith('"')
            )
            if direct_decimal and len(tokens) == 1:
                form = "direct_decimal"
                direct_line_occurrences += 1
            elif direct_decimal and has_filename:
                form = "direct_decimal_filename"
                direct_line_occurrences += 1
                filename_occurrences += 1
            else:
                form = "pp_tokens"
                pp_token_line_occurrences += 1
                # Expansion decides whether the final standard form contains
                # a filename; the source audit deliberately does not evaluate
                # macros independently from the CupidC corpus harness.
                has_filename = None
            named_line_occurrences += 1
            if marker == "#":
                ordinary_marker_occurrences += 1
            else:
                digraph_marker_occurrences += 1
            max_conditional_depth = max(
                max_conditional_depth, conditional_depth
            )
            by_form[(form, marker, has_filename, conditional_depth)].append(
                {
                    "path": path,
                    "line": line_number,
                    "text": raw_line.strip()[:160],
                    "operand": " ".join(tokens),
                }
            )

    forms = []
    for (form, marker, has_filename, conditional_depth), evidence in sorted(
        by_form.items(), key=lambda item: item[0]
    ):
        forms.append(
            {
                "form": form,
                "marker": marker,
                "has_filename": has_filename,
                "conditional_depth": conditional_depth,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    return {
        "status": "pass",
        "source_files": source_files,
        "named_line_occurrences": named_line_occurrences,
        "direct_line_occurrences": direct_line_occurrences,
        "pp_token_line_occurrences": pp_token_line_occurrences,
        "filename_occurrences": filename_occurrences,
        "ordinary_marker_occurrences": ordinary_marker_occurrences,
        "digraph_marker_occurrences": digraph_marker_occurrences,
        "numeric_marker_occurrences": numeric_marker_occurrences,
        "max_conditional_depth": max_conditional_depth,
        "forms": forms,
    }


def _c_preprocessor_conditionals_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_expression: dict[tuple[str, ...], list[dict[str, object]]] = (
        collections.defaultdict(list)
    )
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        expression_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(expression_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            expression_line,
        ) in zip(
            logical_lines,
            directive_lines,
            expression_lines,
            strict=True,
        ):
            match = re.match(
                r"\s*(?:#|%:)\s*(if|elif)\b", directive_line
            )
            if match is None:
                continue
            directive = match.group(1)
            tokens = _normalize_c_preprocessing_tokens(
                expression_line[match.end() :], path, line_number
            )
            by_expression[tokens].append(
                {
                    "path": path,
                    "line": line_number,
                    "directive": directive,
                    "text": raw_line.strip()[:160],
                }
            )

    expressions: list[dict[str, object]] = []
    if_occurrences = 0
    elif_occurrences = 0
    directive_expression_pairs = 0
    for tokens, evidence in sorted(
        by_expression.items(), key=lambda item: item[0]
    ):
        if_count = sum(item["directive"] == "if" for item in evidence)
        elif_count = sum(item["directive"] == "elif" for item in evidence)
        if_occurrences += if_count
        elif_occurrences += elif_count
        directive_expression_pairs += int(if_count != 0) + int(elif_count != 0)
        expressions.append(
            {
                "expression": " ".join(tokens),
                "if_occurrences": if_count,
                "elif_occurrences": elif_count,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    return {
        "status": "pass",
        "if_occurrences": if_occurrences,
        "elif_occurrences": elif_occurrences,
        "expression_occurrences": if_occurrences + elif_occurrences,
        "unique_expressions": len(expressions),
        "directive_expression_pairs": directive_expression_pairs,
        "expressions": expressions,
    }


def _c_preprocessor_pragmas_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[
        tuple[str, str, int | None], list[dict[str, object]]
    ] = collections.defaultdict(list)
    once_occurrences = 0
    pack_push_occurrences = 0
    pack_pop_occurrences = 0
    pack_underflow_occurrences = 0
    unmatched_pack_pushes = 0
    max_pack_depth = 0
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"c", "c_header", "cupid_c"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        payload_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(payload_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        pack_depth = 0
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            payload_line,
        ) in zip(
            logical_lines,
            directive_lines,
            payload_lines,
            strict=True,
        ):
            if re.search(r"\b_Pragma\b", directive_line):
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #pragma form: "
                    "_Pragma operator"
                )
            match = re.match(r"\s*(?:#|%:)\s*pragma\b", directive_line)
            if match is None:
                continue
            payload = payload_line[match.end() :]
            if not payload.strip():
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #pragma form: "
                    "<empty>"
                )
            tokens = _normalize_c_preprocessing_tokens(
                payload, path, line_number
            )
            form: str
            action: str
            alignment: int | None
            if tokens == ("once",):
                form = "once"
                action = "once"
                alignment = None
                once_occurrences += 1
            elif tokens == ("pack", "(", "pop", ")"):
                form = "pack(pop)"
                action = "pack_pop"
                alignment = None
                pack_pop_occurrences += 1
                if pack_depth == 0:
                    pack_underflow_occurrences += 1
                else:
                    pack_depth -= 1
            elif (
                len(tokens) == 6
                and tokens[:4] == ("pack", "(", "push", ",")
                and tokens[5] == ")"
                and tokens[4] in {"1", "2", "4", "8", "16"}
            ):
                alignment = int(tokens[4])
                form = f"pack(push, {alignment})"
                action = "pack_push"
                pack_push_occurrences += 1
                pack_depth += 1
                max_pack_depth = max(max_pack_depth, pack_depth)
            else:
                raise AuditError(
                    f"{path}:{line_number}: unclassified active #pragma form: "
                    f"{' '.join(tokens)}"
                )
            by_form[(form, action, alignment)].append(
                {
                    "path": path,
                    "line": line_number,
                    "text": raw_line.strip()[:160],
                }
            )
        unmatched_pack_pushes += pack_depth

    forms: list[dict[str, object]] = []
    for (form, action, alignment), evidence in sorted(by_form.items()):
        forms.append(
            {
                "form": form,
                "action": action,
                "alignment": alignment,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    pack_occurrences = pack_push_occurrences + pack_pop_occurrences
    return {
        "status": "pass",
        "pragma_occurrences": once_occurrences + pack_occurrences,
        "once_occurrences": once_occurrences,
        "pack_occurrences": pack_occurrences,
        "pack_push_occurrences": pack_push_occurrences,
        "pack_pop_occurrences": pack_pop_occurrences,
        "pack_balanced": (
            pack_underflow_occurrences == 0 and unmatched_pack_pushes == 0
        ),
        "max_pack_depth": max_pack_depth,
        "pack_underflow_occurrences": pack_underflow_occurrences,
        "unmatched_pack_pushes": unmatched_pack_pushes,
        "forms": forms,
    }


def _c_preprocessor_cupid_exe_contract(
    root: Path,
    active_sources: set[str],
    generated_sources: set[str],
) -> dict[str, object]:
    by_form: dict[
        tuple[str, str, int], list[dict[str, object]]
    ] = collections.defaultdict(list)
    ordinary_marker_occurrences = 0
    digraph_marker_occurrences = 0
    max_conditional_depth = 0
    for path in sorted(active_sources - generated_sources):
        if _language(path) not in {"cupid_c", "c_header"}:
            continue
        text = (root / path).read_text(encoding="utf-8", errors="replace")
        logical_lines = _c_raw_logical_lines(text)
        if not logical_lines:
            continue
        logical_text = "\n".join(code_line for _, _, code_line in logical_lines)
        directive_lines = _mask_c_noncode(logical_text).split("\n")
        payload_lines = _mask_c_comments_preserve_literals(logical_text).split(
            "\n"
        )
        if (
            len(directive_lines) != len(logical_lines)
            or len(payload_lines) != len(logical_lines)
        ):
            raise AuditError("C masking changed the logical line count")
        conditional_depth = 0
        for (
            (line_number, _original_line, raw_line),
            directive_line,
            payload_line,
        ) in zip(
            logical_lines,
            directive_lines,
            payload_lines,
            strict=True,
        ):
            match = re.match(
                r"\s*(#|%:)\s*([A-Za-z_]\w*)\b", directive_line
            )
            if match is None:
                continue
            marker, directive = match.groups()
            if directive in {"if", "ifdef", "ifndef"}:
                conditional_depth += 1
                continue
            if directive == "endif":
                conditional_depth = max(0, conditional_depth - 1)
                continue
            if directive.casefold() != "exe":
                continue
            payload = payload_line[match.end() :]
            try:
                tokens = (
                    _normalize_c_preprocessing_tokens(
                        payload, path, line_number
                    )
                    if payload.strip()
                    else ()
                )
            except AuditError:
                raise AuditError(
                    f"{path}:{line_number}: unclassified active Cupid #exe "
                    f"form: {payload.strip()[:80]}"
                ) from None
            if directive != "exe" or not tokens or tokens[0] != "{":
                rendered = " ".join(tokens) if tokens else "<empty>"
                raise AuditError(
                    f"{path}:{line_number}: unclassified active Cupid #exe "
                    f"form: {directive} {rendered}"
                )
            if conditional_depth != 0:
                raise AuditError(
                    f"{path}:{line_number}: unclassified active Cupid #exe "
                    f"form: conditional depth {conditional_depth}"
                )
            max_conditional_depth = max(
                max_conditional_depth, conditional_depth
            )
            by_form[("block", marker, conditional_depth)].append(
                {
                    "path": path,
                    "line": line_number,
                    "text": raw_line.strip()[:160],
                }
            )
            if marker == "#":
                ordinary_marker_occurrences += 1
            else:
                digraph_marker_occurrences += 1

    forms: list[dict[str, object]] = []
    for (form, marker, conditional_depth), evidence in sorted(by_form.items()):
        forms.append(
            {
                "form": form,
                "marker": marker,
                "conditional_depth": conditional_depth,
                "occurrences": len(evidence),
                "files": sorted({str(item["path"]) for item in evidence}),
                "evidence": evidence,
            }
        )
    exe_occurrences = ordinary_marker_occurrences + digraph_marker_occurrences
    return {
        "status": "pass",
        "exe_occurrences": exe_occurrences,
        "block_occurrences": exe_occurrences,
        "ordinary_marker_occurrences": ordinary_marker_occurrences,
        "digraph_marker_occurrences": digraph_marker_occurrences,
        "max_conditional_depth": max_conditional_depth,
        "forms": forms,
    }


def _scan_build_features(
    transforms: list[dict[str, object]],
    collector: FeatureCollector,
) -> None:
    for transform in transforms:
        operation = str(transform["operation"])
        for source in transform["inputs"]:
            path = str(source)
            language = _language(path)
            feature_id = None
            if language == "c" and operation == "compile_c_to_elf32_object":
                feature_id = "c.output.elf32_relocatable"
            elif (
                language == "cupid_c"
                and operation == "compile_c_to_elf32_object"
            ):
                feature_id = "cupid_c.output.elf32_relocatable"
            elif language == "assembly" and operation == "assemble_flat_binary":
                feature_id = "asm.output.flat_binary"
            elif language == "assembly" and operation == "assemble_elf32_relocatable":
                feature_id = "asm.output.elf32_relocatable"
            elif language == "assembly" and operation in {
                "wrap_binary_as_elf32_relocatable",
                "wrap_text_as_elf32_relocatable",
            }:
                feature_id = "asm.delivery.embedded_source"
            elif language == "cupid_c" and operation in {
                "wrap_binary_as_elf32_relocatable",
                "wrap_text_as_elf32_relocatable",
            }:
                feature_id = "cupid_c.delivery.embedded_source"
            elif language == "c_header" and operation in {
                "wrap_binary_as_elf32_relocatable",
                "wrap_text_as_elf32_relocatable",
            }:
                feature_id = "cupid_c.delivery.embedded_header"
            if feature_id is not None:
                collector.add(
                    feature_id,
                    path,
                    1,
                    f"{operation} -> {transform['output']}",
                )


_CHECKED_SEED_RUNNER_FILES = (
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_kernel_compile.py",
    "tools/cupidc_production_compile.py",
    "tools/cupidld_user_link.py",
    "tools/hostbuild.py",
)


def _is_checked_seed_runner_production_root(root: Path) -> bool:
    return all(
        (root / relative).is_file()
        for relative in (
            "Makefile",
            "toolchain/cupidobj.cc",
            "bootstrap/seeds/i386-linux/manifest.json",
        )
    ) and all(
        (root / relative).is_dir()
        for relative in (
            "kernel/doom/src",
            "user/examples",
        )
    )


def _read_checked_seed_runner_module(
    root: Path,
    relative: str,
) -> ast.Module:
    path = root / relative
    try:
        return ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    except (OSError, SyntaxError) as error:
        raise AuditError(
            f"checked-seed runner contract file is unavailable: {relative}: "
            f"{error}"
        ) from error


def _checked_seed_function(
    tree: ast.Module,
    name: str,
    relative: str,
) -> ast.FunctionDef:
    functions = [
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == name
    ]
    if len(functions) != 1:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"expected one {name} function"
        )
    function = functions[0]
    if function.decorator_list or any(
        isinstance(node, (ast.Yield, ast.YieldFrom))
        for node in ast.walk(function)
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"{name} is decorated or yields instead of running directly"
        )
    return function


def _validate_shared_seed_runner(
    tree: ast.Module,
    relative: str,
) -> None:
    function = _checked_seed_function(tree, "run_seed_tool", relative)
    provider_rebindings = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Name)
        and isinstance(node.ctx, (ast.Store, ast.Del))
        and node.id == "run_seed_tool"
    ]
    provider_namespace_mutations = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and (
            (
                isinstance(node.func, ast.Name)
                and node.func.id
                in {
                    "globals",
                    "locals",
                    "exec",
                    "eval",
                    "setattr",
                    "delattr",
                    "__import__",
                }
            )
            or (
                isinstance(node.func, ast.Attribute)
                and node.func.attr
                in {"__setitem__", "__setattr__", "__delitem__"}
            )
        )
    ]
    if provider_rebindings or provider_namespace_mutations:
        raise AuditError(
            "checked-seed runner contract changed: exported runner authority "
            "is rebound dynamically"
        )
    keyword_defaults = {
        argument.arg: default
        for argument, default in zip(
            function.args.kwonlyargs,
            function.args.kw_defaults,
            strict=True,
        )
    }
    runner_default = keyword_defaults.get("runner")
    if not (
        isinstance(runner_default, ast.Constant)
        and runner_default.value is None
    ):
        raise AuditError(
            "checked-seed runner contract changed: runner is not one "
            "optional keyword-only injection"
        )

    nested = [
        statement
        for statement in function.body
        if isinstance(statement, ast.FunctionDef)
        and statement.name == "run_frozen"
    ]
    if len(nested) != 1:
        raise AuditError(
            "checked-seed runner contract changed: frozen execution helper "
            "is not unique"
        )
    run_frozen = nested[0]
    if run_frozen.decorator_list or any(
        isinstance(node, (ast.Yield, ast.YieldFrom))
        for node in ast.walk(run_frozen)
    ):
        raise AuditError(
            "checked-seed runner contract changed: frozen execution helper "
            "is decorated or yields"
        )
    body = run_frozen.body
    returns = [
        node for node in ast.walk(run_frozen) if isinstance(node, ast.Return)
    ]
    if len(returns) != 1 or returns[0] not in body:
        raise AuditError(
            "checked-seed runner contract changed: frozen execution has an "
            "alternate return path"
        )
    run_parents = {
        child: parent
        for parent in ast.walk(run_frozen)
        for child in ast.iter_child_nodes(parent)
    }
    seed_input_names = [
        node
        for node in ast.walk(run_frozen)
        if isinstance(node, ast.Name) and node.id == "seed_inputs"
    ]
    seed_input_load_parents = sorted(
        ast.unparse(run_parents[node])
        for node in seed_input_names
        if isinstance(node.ctx, ast.Load)
    )
    if seed_input_load_parents != [
        "seed_inputs.manifest_sha256",
        "seed_inputs.tools",
    ] or any(
        isinstance(node.ctx, (ast.Store, ast.Del)) for node in seed_input_names
    ):
        raise AuditError(
            "checked-seed runner contract changed: frozen seed capture is "
            "read or mutated outside its executable and manifest checks"
        )

    tool_runs = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "run"
    ]
    if (
        len(tool_runs) != 1
        or ast.unparse(tool_runs[0].func) != "active_runner.run"
    ):
        raise AuditError(
            "checked-seed runner contract changed: frozen dispatch has an "
            "unchecked tool execution path"
        )

    frozen_branches = [
        statement
        for statement in function.body
        if isinstance(statement, ast.If)
        and ast.unparse(statement.test) == "frozen_seed is not None"
    ]
    if not (
        len(frozen_branches) == 1
        and not frozen_branches[0].orelse
        and len(frozen_branches[0].body) == 1
        and isinstance(frozen_branches[0].body[0], ast.Return)
        and ast.unparse(frozen_branches[0].body[0].value)
        == "run_frozen(frozen_seed)"
    ):
        raise AuditError(
            "checked-seed runner contract changed: supplied frozen capture "
            "does not enter the checked helper directly"
        )
    frozen_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and ast.unparse(node.func) == "run_frozen"
    ]
    if sorted(ast.unparse(call) for call in frozen_calls) != [
        "run_frozen(freeze_seed_inputs(manifest_path, Path(temporary)))",
        "run_frozen(frozen_seed)",
    ]:
        raise AuditError(
            "checked-seed runner contract changed: frozen capture dispatch "
            "has another route"
        )
    fallback_paths = [
        statement
        for statement in function.body
        if isinstance(statement, ast.With)
        and len(statement.items) == 1
        and ast.unparse(statement.items[0].context_expr).startswith(
            "tempfile.TemporaryDirectory("
        )
    ]
    if not (
        len(fallback_paths) == 1
        and len(fallback_paths[0].body) == 1
        and isinstance(fallback_paths[0].body[0], ast.Return)
        and ast.unparse(fallback_paths[0].body[0].value)
        == "run_frozen(freeze_seed_inputs(manifest_path, Path(temporary)))"
    ):
        raise AuditError(
            "checked-seed runner contract changed: new seed capture does not "
            "enter the checked helper directly"
        )

    def store_count(name: str) -> int:
        return sum(
            isinstance(node, ast.Name)
            and isinstance(node.ctx, ast.Store)
            and node.id == name
            for node in ast.walk(run_frozen)
        )

    for name in ("executable", "active_runner", "result", "live_seed"):
        if store_count(name) != 1:
            raise AuditError(
                "checked-seed runner contract changed: "
                f"{name} is not one immutable execution binding"
            )

    def direct_index(predicate) -> int:
        matches = [
            index
            for index, statement in enumerate(body)
            if predicate(statement)
        ]
        if len(matches) != 1:
            raise AuditError(
                "checked-seed runner contract changed: execution and live "
                "cohort checks are not one ordered path"
            )
        return matches[0]

    executable_index = direct_index(
        lambda statement: isinstance(statement, ast.Try)
        and len(statement.body) == 1
        and ast.unparse(statement.body[0])
        == "executable = seed_inputs.tools[tool_name]"
    )
    runner_index = direct_index(
        lambda statement: isinstance(statement, ast.Assign)
        and ast.unparse(statement)
        == "active_runner = runner if runner is not None else ToolRunner(root)"
    )
    execution_index = direct_index(
        lambda statement: isinstance(statement, ast.Try)
        and len(statement.body) == 1
        and ast.unparse(statement.body[0])
        == "result = active_runner.run(executable, arguments, timeout)"
    )
    live_index = direct_index(
        lambda statement: isinstance(statement, ast.Try)
        and len(statement.body) == 1
        and ast.unparse(statement.body[0])
        == "live_seed = _load_seed_inputs(manifest_path, None)"
    )
    comparison_index = direct_index(
        lambda statement: isinstance(statement, ast.If)
        and ast.unparse(statement.test)
        == "live_seed.manifest_sha256 != seed_inputs.manifest_sha256"
        and not statement.orelse
        and len(statement.body) == 1
        and isinstance(statement.body[0], ast.Raise)
        and isinstance(statement.body[0].exc, ast.Call)
        and ast.unparse(statement.body[0].exc.func) == "BootstrapError"
    )
    return_index = direct_index(
        lambda statement: isinstance(statement, ast.Return)
        and ast.unparse(statement.value) == "result"
    )
    for checked_try in (body[execution_index], body[live_index]):
        if any(
            not handler.body or not isinstance(handler.body[-1], ast.Raise)
            for handler in checked_try.handlers
        ):
            raise AuditError(
                "checked-seed runner contract changed: execution or live "
                "cohort failure does not raise"
            )
    if not (
        executable_index
        < runner_index
        < execution_index
        < live_index
        < comparison_index
        < return_index
    ):
        raise AuditError(
            "checked-seed runner contract changed: live cohort validation "
            "does not precede success"
        )


def _validate_checked_seed_wrapper(
    tree: ast.Module,
    relative: str,
    function_name: str,
    tool_name: str,
    runner_name: str,
    publication_call: str,
    native_split: bool,
    guard_tool_name: str | None = None,
) -> None:
    dynamic_namespace_calls = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and (
            (
                isinstance(node.func, ast.Name)
                and node.func.id
                in {
                    "globals",
                    "locals",
                    "exec",
                    "eval",
                    "setattr",
                    "delattr",
                    "__import__",
                }
            )
            or (
                isinstance(node.func, ast.Attribute)
                and node.func.attr
                in {"__setitem__", "__setattr__", "__delitem__"}
            )
        )
    ]
    expected_imports = ["bootstrap_toolchain", "tools.bootstrap_toolchain"]

    def imported_modules(name: str) -> list[str | None]:
        return [
            node.module
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
            for imported in node.names
            if imported.name == name and imported.asname is None
        ]

    protected_names = {"run_seed_tool", "freeze_seed_inputs"}
    protected_rebindings = [
        node
        for node in ast.walk(tree)
        if (
            isinstance(node, ast.Name)
            and isinstance(node.ctx, (ast.Store, ast.Del))
            and node.id in protected_names
        )
        or (
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
            and node.name in protected_names
        )
    ]
    if (
        sorted(imported_modules("run_seed_tool")) != expected_imports
        or sorted(imported_modules("freeze_seed_inputs")) != expected_imports
        or protected_rebindings
        or dynamic_namespace_calls
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "seed capture or runner is not the shared imported authority"
        )
    function = _checked_seed_function(tree, function_name, relative)
    if any(
        isinstance(node, (ast.Return, ast.Yield, ast.YieldFrom))
        for node in ast.walk(function)
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "production wrapper has an early return or generator yield"
        )
    calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "run_seed_tool"
    ]
    expected_call_count = 2 if guard_tool_name is not None else 1
    if len(calls) != expected_call_count:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"{function_name} has an unexpected checked tool count"
        )
    matching_calls = [
        call
        for call in calls
        if len(call.args) >= 3
        and ast.unparse(call.args[2]) == repr(tool_name)
    ]
    if len(matching_calls) != 1:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            f"{tool_name} invocation is not unique"
        )
    call = matching_calls[0]
    expected_arguments = ["manifest_path", "root", repr(tool_name), "arguments"]
    if [ast.unparse(argument) for argument in call.args] != expected_arguments:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "tool invocation arguments differ"
        )
    keywords = {
        keyword.arg: ast.unparse(keyword.value)
        for keyword in call.keywords
        if keyword.arg is not None
    }
    if keywords != {
        "timeout": "timeout",
        "frozen_seed": "seed_inputs",
        "runner": runner_name,
    }:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "caller-owned capture or runner forwarding differs"
        )

    parents = {
        child: parent
        for parent in ast.walk(function)
        for child in ast.iter_child_nodes(parent)
    }
    guard_call = None
    guard_assignment = None
    if guard_tool_name is not None:
        guard_calls = [
            candidate
            for candidate in calls
            if len(candidate.args) >= 3
            and ast.unparse(candidate.args[2]) == repr(guard_tool_name)
        ]
        if len(guard_calls) != 1:
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                f"{guard_tool_name} invocation is not unique"
            )
        guard_call = guard_calls[0]
        if [ast.unparse(argument) for argument in guard_call.args] != [
            "manifest_path",
            "root",
            repr(guard_tool_name),
            "('--require-known', '--require-local-targets', "
            "'--require-code-anchors', temporary_output)",
        ]:
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication guard arguments differ"
            )
        guard_keywords = {
            keyword.arg: ast.unparse(keyword.value)
            for keyword in guard_call.keywords
            if keyword.arg is not None
        }
        if guard_keywords != {
            "timeout": "timeout",
            "frozen_seed": "seed_inputs",
            "runner": runner_name,
        }:
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication guard does not share the frozen runner"
            )
        guard_assignment = parents.get(guard_call)
        if not (
            isinstance(guard_assignment, ast.Assign)
            and len(guard_assignment.targets) == 1
            and ast.unparse(guard_assignment.targets[0]) == "inspected"
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication guard result is not retained"
            )
        guard_try = parents.get(guard_assignment)
        guard_branch = parents.get(guard_try)
        if not (
            isinstance(guard_try, ast.Try)
            and guard_assignment in guard_try.body
            and isinstance(guard_branch, ast.If)
            and ast.unparse(guard_branch.test) == "native_snapshot is None"
            and guard_try in guard_branch.body
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication guard is conditionally unreachable"
            )

        guard_checks = {
            ast.unparse(statement.test): statement
            for statement in guard_branch.body
            if isinstance(statement, ast.If)
        }
        for expected_test in (
            "inspected.returncode != 0",
            "inspected.stdout",
            "inspected.stderr",
        ):
            statement = guard_checks.get(expected_test)
            if statement is None or not statement.body or not isinstance(
                statement.body[-1], ast.Raise
            ):
                raise AuditError(
                    f"checked-seed runner contract changed in {relative}: "
                    "publication guard result is not checked"
                )

        capture_calls = [
            node
            for node in ast.walk(function)
            if isinstance(node, ast.Call)
            and ast.unparse(node.func) == "_capture_private_candidate"
        ]
        capture_assignments = {
            ast.unparse(parent.targets[0]): (candidate, parent)
            for candidate in capture_calls
            if isinstance((parent := parents.get(candidate)), ast.Assign)
            and len(parent.targets) == 1
        }
        initial_capture = capture_assignments.get(
            "(candidate_payload, candidate_identity)"
        )
        inspected_capture = capture_assignments.get(
            "(inspected_payload, inspected_identity)"
        )
        if (
            len(capture_calls) != 2
            or initial_capture is None
            or inspected_capture is None
            or [ast.unparse(arg) for arg in initial_capture[0].args]
            != [
                "temporary_output",
                repr("CupidLD candidate changed before validation"),
            ]
            or [ast.unparse(arg) for arg in inspected_capture[0].args]
            != [
                "temporary_output",
                repr("CupidDis candidate changed while it was inspected"),
            ]
            or inspected_capture[1] not in guard_branch.body
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication candidate capture differs"
            )

        validation_calls = [
            node
            for node in ast.walk(function)
            if isinstance(node, ast.Call)
            and ast.unparse(node.func) == "validate_user_executable_bytes"
            and [ast.unparse(arg) for arg in node.args]
            == ["candidate_payload"]
        ]
        candidate_comparison = guard_checks.get(
            "inspected_identity != candidate_identity or "
            "inspected_payload != candidate_payload"
        )
        publication_writes = [
            node
            for node in ast.walk(function)
            if isinstance(node, ast.Call)
            and ast.unparse(node)
            == "publication_output.write_bytes(candidate_payload)"
        ]
        if (
            len(validation_calls) != 1
            or candidate_comparison is None
            or not candidate_comparison.body
            or not isinstance(candidate_comparison.body[-1], ast.Raise)
            or len(publication_writes) != 1
            or not initial_capture[0].lineno
            < validation_calls[0].lineno
            < guard_call.lineno
            < inspected_capture[0].lineno
            < candidate_comparison.lineno
            < publication_writes[0].lineno
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "validated inspection bytes are not frozen for publication"
            )

    assignment = parents.get(call)
    if not (
        isinstance(assignment, ast.Assign)
        and len(assignment.targets) == 1
        and ast.unparse(assignment.targets[0]) == "result"
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "checked result is not the accepted tool result"
        )

    result_store_count = sum(
        isinstance(node, ast.Name)
        and isinstance(node.ctx, ast.Store)
        and node.id == "result"
        for node in ast.walk(function)
    )
    expected_result_stores = 2 if native_split else 1
    if result_store_count != expected_result_stores:
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "tool result has an unchecked replacement binding"
        )

    execution_try: ast.Try | None = None
    if native_split:
        branch = parents.get(assignment)
        if not (
            isinstance(branch, ast.If)
            and ast.unparse(branch.test) == "native_snapshot is not None"
            and assignment in branch.orelse
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "native and checked execution paths are not distinct"
            )
        branch_parent = parents.get(branch)
        if not (
            isinstance(branch_parent, ast.Try)
            and branch in branch_parent.body
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "checked execution is nested below another control path"
            )
        execution_try = branch_parent
    else:
        assignment_parent = parents.get(assignment)
        if not (
            isinstance(assignment_parent, ast.Try)
            and assignment in assignment_parent.body
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "checked execution is not the direct kernel execution path"
            )
        execution_try = assignment_parent

    current = parents.get(execution_try)
    while current is not None and current is not function:
        if isinstance(
            current,
            (
                ast.If,
                ast.For,
                ast.AsyncFor,
                ast.While,
                ast.Match,
                ast.ExceptHandler,
                ast.Lambda,
                ast.FunctionDef,
                ast.AsyncFunctionDef,
            ),
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "checked execution is conditionally unreachable"
            )
        current = parents.get(current)

    freeze_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "freeze_seed_inputs"
    ]
    publication_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and ast.unparse(node.func) == publication_call
    ]
    publication_references = [
        node
        for node in ast.walk(function)
        if isinstance(node, (ast.Name, ast.Attribute))
        and ast.unparse(node) == publication_call
    ]
    alternate_publications = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and node not in publication_calls
        and ast.unparse(node)
        not in {
            "temporary_input.write_bytes(source_payload)",
            "publication_output.write_bytes(candidate_payload)",
        }
        and (
            (
                isinstance(node.func, ast.Name)
                and node.func.id == "open"
            )
            or (
                isinstance(node.func, ast.Attribute)
                and node.func.attr
                in {
                    "replace",
                    "rename",
                    "move",
                    "copy",
                    "copy2",
                    "copyfile",
                    "write",
                    "write_bytes",
                    "write_text",
                    "writelines",
                    "link",
                    "symlink",
                    "hardlink_to",
                    "symlink_to",
                    "touch",
                }
            )
        )
    ]
    seed_store_count = sum(
        isinstance(node, ast.Name)
        and isinstance(node.ctx, ast.Store)
        and node.id == "seed_inputs"
        for node in ast.walk(function)
    )
    seed_loads = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Name)
        and isinstance(node.ctx, ast.Load)
        and node.id == "seed_inputs"
    ]
    freeze_assignment = (
        parents.get(freeze_calls[0]) if len(freeze_calls) == 1 else None
    )
    if (
        len(freeze_calls) != 1
        or not isinstance(freeze_assignment, ast.Assign)
        or len(freeze_assignment.targets) != 1
        or ast.unparse(freeze_assignment.targets[0]) != "seed_inputs"
        or seed_store_count != 1
        or len(seed_loads) != expected_call_count
        or any(
            not isinstance(parents.get(seed_load), ast.keyword)
            or parents[seed_load].arg != "frozen_seed"
            for seed_load in seed_loads
        )
        or len(publication_calls) != 1
        or len(publication_references) != 1
        or publication_references[0] is not publication_calls[0].func
        or (
            guard_call is not None
            and ast.unparse(publication_calls[0])
            != "os.replace(publication_output, output)"
        )
        or alternate_publications
        or not freeze_calls[0].lineno < call.lineno < publication_calls[0].lineno
        or (
            guard_call is not None
            and not call.lineno < guard_call.lineno < publication_calls[0].lineno
        )
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "freeze, execution, and publication order differs"
        )

    publication_expression = parents.get(publication_calls[0])
    if not (
        isinstance(publication_expression, ast.Expr)
        and publication_expression.value is publication_calls[0]
    ):
        raise AuditError(
            f"checked-seed runner contract changed in {relative}: "
            "publication is not one direct statement"
        )
    for checked_try in (
        node for node in ast.walk(function) if isinstance(node, ast.Try)
    ):
        if any(
            not handler.body or not isinstance(handler.body[-1], ast.Raise)
            for handler in checked_try.handlers
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "production wrapper suppresses a transaction failure"
            )
    child: ast.AST = publication_expression
    current = parents.get(child)
    while current is not None and current is not function:
        if isinstance(current, ast.Try) and child not in current.body:
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication is outside the successful try body"
            )
        if isinstance(
            current,
            (
                ast.If,
                ast.For,
                ast.AsyncFor,
                ast.While,
                ast.Match,
                ast.ExceptHandler,
                ast.Lambda,
                ast.FunctionDef,
                ast.AsyncFunctionDef,
            ),
        ):
            raise AuditError(
                f"checked-seed runner contract changed in {relative}: "
                "publication is conditionally reachable"
            )
        child = current
        current = parents.get(current)


def _validate_checked_code_publication(
    tree: ast.Module,
    relative: str,
) -> None:
    function = _checked_seed_function(tree, "validate_code", relative)
    parents = {
        child: parent
        for parent in ast.walk(function)
        for child in ast.iter_child_nodes(parent)
    }
    runner_calls = [
        node
        for node in ast.walk(function)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Name)
        and node.func.id == "run_seed_tool"
    ]
    assignments: dict[str, tuple[ast.Assign, ast.Call]] = {}
    for call in runner_calls:
        assignment = parents.get(call)
        if not (
            isinstance(assignment, ast.Assign)
            and assignment.value is call
            and len(assignment.targets) == 1
            and isinstance(assignment.targets[0], ast.Name)
        ):
            continue
        assignments[assignment.targets[0].id] = (assignment, call)
    if len(runner_calls) != 3 or set(assignments) != {
        "result",
        "linked_validation",
        "flattened",
    }:
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "code publication does not have one broad disassembly, one linked "
            "target validation, and one flattening call"
        )

    def positional(call: ast.Call) -> list[str]:
        return [ast.unparse(argument) for argument in call.args]

    broad_assignment, broad_call = assignments["result"]
    linked_assignment, linked_call = assignments["linked_validation"]
    flat_assignment, flat_call = assignments["flattened"]
    if positional(broad_call) != [
        "live_seed_manifest",
        "private_root",
        "'cupiddis'",
        "('--require-known', *logical_paths)",
    ]:
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "broad code validation arguments differ"
        )
    if positional(linked_call) != [
        "live_seed_manifest",
        "private_root",
        "'cupiddis'",
        "('--require-known', '--require-local-targets', "
        "'--require-code-anchors', "
        "'kernel/kernel.elf.pass1', 'kernel/kernel.elf')",
    ]:
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked kernel code validation arguments differ"
        )
    if positional(flat_call) != [
        "live_seed_manifest",
        "private_root",
        "'cupidobj'",
        "('flat', 'kernel/kernel.elf', '-o', candidate_output.logical)",
    ]:
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "kernel flattening arguments differ"
        )
    linked_keywords = {
        keyword.arg: ast.unparse(keyword.value)
        for keyword in linked_call.keywords
        if keyword.arg is not None
    }
    flat_keywords = {
        keyword.arg: ast.unparse(keyword.value)
        for keyword in flat_call.keywords
        if keyword.arg is not None
    }
    if linked_keywords != {"timeout": "600", "frozen_seed": "frozen_seed"} or (
        flat_keywords != {"timeout": "300", "frozen_seed": "frozen_seed"}
    ):
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked validation and flattening do not share the frozen seed"
        )

    broad_keywords = [
        (keyword.arg, ast.unparse(keyword.value))
        for keyword in broad_call.keywords
    ]
    if broad_keywords != [
        ("timeout", "300"),
        (
            None,
            "{'frozen_seed': frozen_seed} if frozen_seed is not None else {}",
        ),
    ]:
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "broad validation does not use the output transaction's frozen seed"
        )

    def require_unconditional_success_path(node: ast.AST) -> None:
        child: ast.AST = node
        current = parents.get(child)
        while current is not None and current is not function:
            if isinstance(
                current,
                (
                    ast.If,
                    ast.For,
                    ast.AsyncFor,
                    ast.While,
                    ast.Match,
                    ast.ExceptHandler,
                    ast.Lambda,
                    ast.FunctionDef,
                    ast.AsyncFunctionDef,
                ),
            ):
                raise AuditError(
                    "checked-seed runner contract changed in tools/hostbuild.py: "
                    "code validation or flattening is conditionally unreachable"
                )
            if isinstance(current, (ast.With, ast.AsyncWith)):
                allowed_exit_stack = (
                    isinstance(current, ast.With)
                    and len(current.items) == 1
                    and isinstance(current.items[0].context_expr, ast.Call)
                    and isinstance(
                        current.items[0].context_expr.func,
                        ast.Name,
                    )
                    and current.items[0].context_expr.func.id == "ExitStack"
                    and not current.items[0].context_expr.args
                    and not current.items[0].context_expr.keywords
                    and isinstance(current.items[0].optional_vars, ast.Name)
                    and current.items[0].optional_vars.id == "stack"
                )
                if not allowed_exit_stack:
                    raise AuditError(
                        "checked-seed runner contract changed in "
                        "tools/hostbuild.py: code validation or flattening "
                        "moved under an unproved context manager"
                    )
            if isinstance(current, (ast.Try, ast.TryStar)):
                handlers_reraise = current.handlers and all(
                    len(handler.body) == 1
                    and isinstance(handler.body[0], ast.Raise)
                    for handler in current.handlers
                )
                if (
                    child not in current.body
                    or current.orelse
                    or current.finalbody
                    or not handlers_reraise
                ):
                    raise AuditError(
                        "checked-seed runner contract changed in "
                        "tools/hostbuild.py: code validation or flattening "
                        "moved onto a suppressible path"
                    )
            child = current
            current = parents.get(current)

    for assignment in (broad_assignment, linked_assignment, flat_assignment):
        require_unconditional_success_path(assignment)

    result_guards = {
        ast.unparse(node.test): node
        for node in ast.walk(function)
        if isinstance(node, ast.If)
        and node.lineno > linked_assignment.lineno
        and node.lineno < flat_assignment.lineno
    }
    if not {
        "linked_validation.stdout",
        "linked_validation.returncode != 0",
    }.issubset(result_guards) or not (
        broad_assignment.lineno
        < linked_assignment.lineno
        < flat_assignment.lineno
    ):
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked validation is not accepted before kernel flattening"
        )

    stdout_guard = result_guards["linked_validation.stdout"]
    status_guard = result_guards["linked_validation.returncode != 0"]
    require_unconditional_success_path(stdout_guard)
    require_unconditional_success_path(status_guard)
    if not (
        len(stdout_guard.body) == 1
        and isinstance(stdout_guard.body[0], ast.Raise)
        and len(status_guard.body) == 1
        and isinstance(status_guard.body[0], ast.Return)
    ):
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked validation output or status does not block flattening"
        )

    linked_try = parents.get(linked_assignment)
    if not (
        isinstance(linked_try, ast.Try)
        and len(linked_try.handlers) == 1
        and isinstance(linked_try.handlers[0].type, ast.Name)
        and linked_try.handlers[0].type.id == "BootstrapError"
        and len(linked_try.handlers[0].body) == 1
        and isinstance(linked_try.handlers[0].body[0], ast.Raise)
    ):
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked validation runner failures do not block flattening"
        )

    drift_calls: dict[str, ast.Call] = {}
    for node in ast.walk(function):
        if not (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id
            in {
                "_require_code_inputs_unchanged",
                "_require_code_seed_inputs_unchanged",
            }
            and linked_assignment.lineno < node.lineno < flat_assignment.lineno
        ):
            continue
        drift_calls[node.func.id] = node
    if set(drift_calls) != {
        "_require_code_inputs_unchanged",
        "_require_code_seed_inputs_unchanged",
    }:
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked validation drift guards are incomplete"
        )
    input_drift = drift_calls["_require_code_inputs_unchanged"]
    seed_drift = drift_calls["_require_code_seed_inputs_unchanged"]
    require_unconditional_success_path(input_drift)
    require_unconditional_success_path(seed_drift)
    if (
        positional(input_drift)
        != ["repository_root", "manifest_snapshot", "snapshots"]
        or {
            keyword.arg: ast.unparse(keyword.value)
            for keyword in input_drift.keywords
        }
        != {
            "activity": "'CupidDis linked-code validation'",
            "tool_stderr": "linked_stderr",
        }
        or positional(seed_drift) != ["repository_root", "seed_snapshots"]
        or {
            keyword.arg: ast.unparse(keyword.value)
            for keyword in seed_drift.keywords
        }
        != {"tool_stderr": "linked_stderr"}
    ):
        raise AuditError(
            "checked-seed runner contract changed in tools/hostbuild.py: "
            "linked validation drift guards do not bind the frozen cohort"
        )


def _validate_checked_seed_runner_contract(root: Path) -> None:
    missing = [
        relative
        for relative in _CHECKED_SEED_RUNNER_FILES
        if not (root / relative).is_file()
    ]
    if missing:
        raise AuditError(
            f"checked-seed runner contract files are missing: {missing!r}"
        )
    trees = {
        relative: _read_checked_seed_runner_module(root, relative)
        for relative in _CHECKED_SEED_RUNNER_FILES
    }
    _validate_shared_seed_runner(
        trees["tools/bootstrap_toolchain.py"],
        "tools/bootstrap_toolchain.py",
    )
    _validate_checked_seed_wrapper(
        trees["tools/cupidc_kernel_compile.py"],
        "tools/cupidc_kernel_compile.py",
        "compile_kernel_source",
        "cupidc",
        "executor",
        "_replace_with_retry",
        False,
    )
    _validate_checked_seed_wrapper(
        trees["tools/cupidc_production_compile.py"],
        "tools/cupidc_production_compile.py",
        "compile_production_source",
        "cupidc",
        "active_executor",
        "os.replace",
        True,
    )
    _validate_checked_seed_wrapper(
        trees["tools/cupidld_user_link.py"],
        "tools/cupidld_user_link.py",
        "link_user_program",
        "cupidld",
        "active_runner",
        "os.replace",
        True,
        "cupiddis",
    )
    _validate_checked_code_publication(
        trees["tools/hostbuild.py"],
        "tools/hostbuild.py",
    )


def build_audit(
    root: Path,
    make: str,
    target: str,
    supplemental_builds: list[tuple[str, str]] | None = None,
) -> dict[str, object]:
    root = root.resolve()
    production_root = _is_checked_seed_runner_production_root(root)
    if production_root:
        _validate_checked_seed_runner_contract(root)
    source_suffix_policy = _load_source_suffix_ownership_policy(root)
    complete_supported_graph = production_root and {
        ("user", "all"),
        ("toolchain", "all"),
    }.issubset(set(supplemental_builds or []))
    strict_unreachable_cupid_c_ownership = (
        complete_supported_graph or not production_root
    )
    runtime_delivery_policy = set(
        source_suffix_policy["runtime_delivery_sources"]
    )
    root_model = _collect_build_model(root, make, target, ".")
    supplemental_models = [
        _collect_build_model(root, make, supplemental_target, directory)
        for directory, supplemental_target in (supplemental_builds or [])
    ]
    models = [root_model, *supplemental_models]
    _validate_cupidobj_profile_manifest_delivery(
        root,
        root_model.transforms,
    )
    _validate_cupidc_kernel_compile_make_binding(
        root,
        make,
        root_model.transforms,
    )
    _validate_cupidc_production_make_bindings(root, make, models)

    direct_sources = set().union(*(model.direct_sources for model in models))
    generated_sources = set().union(*(model.generated_sources for model in models))
    forced_sources = set().union(*(model.forced_sources for model in models))
    includes_by_source: dict[str, list[str]] = {}
    build_memberships: dict[str, set[str]] = collections.defaultdict(set)
    for model in models:
        model_sources = set(model.direct_sources | model.forced_sources)
        model_sources.update(model.includes_by_source)
        for included in model.includes_by_source.values():
            model_sources.update(included)
        for source, includes in model.includes_by_source.items():
            includes_by_source[source] = sorted(
                set(includes_by_source.get(source, [])) | set(includes)
            )
        for source in model_sources:
            build_memberships[source].add(model.directory)

    all_sources = set(direct_sources | forced_sources)
    all_sources.update(includes_by_source)
    for included in includes_by_source.values():
        all_sources.update(included)

    all_transforms = [
        transform for model in models for transform in model.transforms
    ]
    source_build_owners: dict[str, set[str]] = collections.defaultdict(set)
    for transform in all_transforms:
        ownership_inputs = transform["inputs"]
        if transform.get("operation") in {
            "generate_toolchain_manifest",
            "verify_toolchain_manifest",
        }:
            build_inputs = set(TOOLCHAIN_MANIFEST_CONTRACT_BUILD_INPUTS)
            ownership_inputs = [
                source for source in ownership_inputs if source in build_inputs
            ]
        for source in ownership_inputs:
            if source in all_sources:
                source_build_owners[source].update(transform["tools"])
    for source in _toolchain_contract_cupidc_ownership_inputs(models):
        if source in all_sources:
            source_build_owners[source].add("cupid_c_contract")
    for source in _toolchain_contract_cupidasm_ownership_inputs(models):
        if source in all_sources:
            source_build_owners[source].add("cupid_assembler")
    _propagate_assembly_include_owners(
        source_build_owners,
        includes_by_source,
    )

    feature_collector = FeatureCollector()
    for relative in sorted(all_sources):
        language = _language(relative)
        if language is not None and relative not in generated_sources:
            _scan_source_features(
                relative,
                root / relative,
                language,
                feature_collector,
            )
    _scan_build_features(all_transforms, feature_collector)

    sources = []
    runtime_owner_evidence_by_path: dict[str, str] = {}
    for relative in sorted(all_sources):
        path = root / relative
        generated = relative in generated_sources
        language = _language(relative)
        owners = sorted(source_build_owners.get(relative, set()))
        runtime_owner = None
        runtime_owner_evidence = None
        if language == "cupid_c" and "cupid_c_compiler" in owners:
            runtime_owner = "CupidC"
            runtime_owner_evidence = "checked_cupidc_compile"
        elif language == "cupid_c" and "cupid_c_contract" in owners:
            runtime_owner = "CupidC"
            runtime_owner_evidence = "checked_cupidc_contract"
        elif language == "cupid_c" and relative in runtime_delivery_policy:
            runtime_owner = "CupidC"
            runtime_owner_evidence = "explicit_runtime_delivery_policy"
        elif (
            language in {"c", "c_header"}
            and "cupid_c_compiler" in owners
        ):
            runtime_owner = "CupidC"
            runtime_owner_evidence = "checked_cupidc_compile"
        elif (
            language == "assembly"
            and (
                "cupid_assembler" in owners
                or (
                    {"host_object_copy", "cupid_object"}.intersection(owners)
                    and "nasm" not in owners
                )
            )
        ):
            runtime_owner = "CupidASM"
            runtime_owner_evidence = "active_cupidasm_edge"
        elif language == "c_header" and {
            "host_object_copy",
            "cupid_object",
        }.intersection(owners):
            runtime_owner = "CupidC"
            runtime_owner_evidence = "cupidobj_runtime_delivery"
        if runtime_owner_evidence is not None:
            runtime_owner_evidence_by_path[relative] = runtime_owner_evidence
        sources.append(
            {
                "path": relative,
                "language": language,
                "origin": "generated" if generated else "tracked",
                "cohort": _source_cohort(relative, language, generated),
                "reachability": (
                    "direct_build_input"
                    if relative in direct_sources
                    else "forced_include"
                    if relative in forced_sources
                    else "transitive_include"
                ),
                "builds": sorted(build_memberships.get(relative, set())),
                "build_owners": owners,
                "runtime_owner": runtime_owner,
                "includes": includes_by_source.get(relative, []),
                "features": feature_collector.for_source(relative),
                "lines": (
                    None
                    if generated
                    else len(path.read_text(encoding="utf-8", errors="replace").splitlines())
                ),
                "sha256": None if generated else _source_digest(path),
            }
        )

    unreachable_sources = _unreachable_inventory(root, all_sources)
    contracts: dict[str, object] = {}
    contracts["c_source_ownership"] = _c_source_ownership_contract(
        sources,
        unreachable_sources,
        source_suffix_policy,
        runtime_owner_evidence_by_path,
        strict_unreachable_cupid_c_ownership,
        complete_supported_graph,
    )
    contracts["assembly_source_ownership"] = (
        _active_assembly_ownership_contract(sources)
    )
    artifact_contract = _artifact_coverage_contract(
        root,
        make,
        root_model.rules,
        root_model.transforms,
    )
    if artifact_contract is not None:
        contracts["bootstrap_artifact_coverage"] = artifact_contract
    contracts["c_preprocessor_include_operands"] = (
        _c_preprocessor_include_operands_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    contracts["c_preprocessor_line_directives"] = (
        _c_preprocessor_line_directives_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    contracts["c_preprocessor_conditionals"] = (
        _c_preprocessor_conditionals_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    contracts["c_preprocessor_pragmas"] = _c_preprocessor_pragmas_contract(
        root,
        all_sources,
        generated_sources,
    )
    contracts["c_preprocessor_cupid_exe"] = (
        _c_preprocessor_cupid_exe_contract(
            root,
            all_sources,
            generated_sources,
        )
    )
    feature_inventory = feature_collector.inventory()
    roadmap = _roadmap(sources, feature_inventory)
    abi = _abi_inventory(root, all_transforms)
    provenance = _provenance(root, models, sources)

    audit = {
        "schema": SCHEMA,
        "abi": abi,
        "provenance": provenance,
        "build": {
            "directory": root_model.directory,
            "root_target": target,
            "include_search_paths": root_model.include_search_paths,
            "forced_includes": sorted(root_model.forced_sources),
            "transforms": root_model.transforms,
        },
        "supplemental_builds": [
            {
                "directory": model.directory,
                "root_target": model.root_target,
                "include_search_paths": model.include_search_paths,
                "forced_includes": sorted(model.forced_sources),
                "transforms": model.transforms,
            }
            for model in supplemental_models
        ],
        "contracts": contracts,
        "features": feature_inventory,
        "roadmap": roadmap,
        "sources": sources,
        "unreachable_sources": unreachable_sources,
        "summary": {
            "active_sources": len(sources),
            "unreachable_sources": len(unreachable_sources),
            "features": len(feature_inventory),
            "transforms": len(all_transforms),
        },
    }
    if {model.directory for model in models} == {".", "user", "toolchain"}:
        _validate_c_preprocessor_make_profiles(root, make)
        _validate_hosted_i386_contract_profiles(root)
        contracts["cupid_toolchain_fixed_point"] = (
            _cupid_toolchain_fixed_point_contract(root)
        )
        active_manifest = _c_preprocessor_active_cases_manifest(audit)
        contracts["c_preprocessor_translation_units"] = (
            _c_preprocessor_translation_unit_contract(active_manifest)
        )
    return audit


def _c_preprocessor_logical_path(path: str) -> str:
    normalized = path.replace("\\", "/")
    if (
        not normalized
        or normalized.startswith("/")
        or re.match(r"^[A-Za-z]:/", normalized)
        or posixpath.normpath(normalized) != normalized
        or normalized == ".."
        or normalized.startswith("../")
    ):
        raise AuditError(
            f"CupidC active preprocessing case is not repository-relative: {path!r}"
        )
    return f"/{normalized}"


def _c_preprocessor_compile_recipe_tokens(
    transform: dict[str, object],
) -> list[str]:
    output = str(transform.get("output", "<unknown>"))
    recipe = transform.get("recipe")
    if (
        not isinstance(recipe, list)
        or len(recipe) != 1
        or not isinstance(recipe[0], str)
        or "\n" in recipe[0]
        or "\r" in recipe[0]
    ):
        raise AuditError(
            f"CupidC active preprocessing compile recipe is not exactly one "
            f"command for {output}"
        )
    try:
        tokens = shlex.split(recipe[0], comments=False, posix=True)
        uncommented_tokens = shlex.split(
            recipe[0], comments=True, posix=True
        )
    except ValueError as exc:
        raise AuditError(
            f"could not tokenize CupidC compile recipe for {output}: {exc}"
        ) from exc
    if tokens != uncommented_tokens:
        raise AuditError(
            f"CupidC active preprocessing compile recipe contains a shell "
            f"comment for {output}"
        )
    if not tokens:
        raise AuditError(
            f"CupidC active preprocessing compile recipe is empty for {output}"
        )
    return tokens


def _c_preprocessor_require_compiler_invocation(
    transform: dict[str, object],
    tokens: list[str],
    expected_argument_profile: list[str],
    subject: str,
) -> None:
    output = str(transform.get("output", "<unknown>"))
    if tokens[0] not in {"$(CC)", "${CC}"}:
        raise AuditError(
            f"CupidC active preprocessing compile recipe does not invoke "
            f"$(CC) directly for {output}: {tokens[0]!r}"
        )
    recipe = transform.get("recipe")
    if not isinstance(recipe, list) or not isinstance(recipe[0], str):
        raise AuditError(
            f"CupidC active preprocessing compile recipe is absent for {output}"
        )
    argument_profile: list[str] = []
    for token in tokens[1:]:
        if token == "$<":
            continue
        if "`" in token:
            raise AuditError(
                f"CupidC active preprocessing compile recipe has unmodeled "
                f"shell substitution for {output}: {token!r}"
            )
        if any(character in token for character in ";&|<>"):
            raise AuditError(
                f"CupidC active preprocessing compile recipe has unmodeled "
                f"shell control for {output}: {token!r}"
            )
    for index, token in enumerate(tokens):
        marker_match = re.fullmatch(
            r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)|"
            r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}",
            token,
        )
        if marker_match is not None:
            marker = marker_match.group(1) or marker_match.group(2)
            if re.search(
                rf"(?<!\S){re.escape(token)}(?!\S)", recipe[0]
            ) is None:
                raise AuditError(
                    f"CupidC active preprocessing compiler argument profile "
                    f"differs for {subject}: Make marker {marker!r} is not an "
                    f"unquoted argument"
                )
            argument_profile.append(marker)
        elif token == "-I../kernel/lang":
            argument_profile.append(token)
        elif token == "-x":
            argument_profile.append(token)
        elif index > 0 and tokens[index - 1] == "-x":
            argument_profile.append(token)
    if argument_profile != expected_argument_profile:
        raise AuditError(
            f"CupidC active preprocessing compiler argument profile differs "
            f"for {subject}: expected={expected_argument_profile!r}, "
            f"actual={argument_profile!r}"
        )


def _c_preprocessor_recipe_markers(
    transform: dict[str, object], allowed: set[str]
) -> collections.Counter[str]:
    output = str(transform.get("output", "<unknown>"))
    recipe = transform.get("recipe")
    if not isinstance(recipe, list) or not all(
        isinstance(line, str) for line in recipe
    ):
        raise AuditError(
            f"CupidC active preprocessing recipe is absent for {output}"
        )
    recipe_text = "\n".join(recipe)
    markers: collections.Counter[str] = collections.Counter()
    automatic_variables = frozenset("@%<?^+|*")
    index = 0
    while index < len(recipe_text):
        if recipe_text[index] != "$":
            index += 1
            continue
        if index + 1 >= len(recipe_text):
            raise AuditError(
                f"CupidC active preprocessing found an unmodeled recipe "
                f"dollar reference for {output}: trailing '$'"
            )
        opener = recipe_text[index + 1]
        if opener in "({":
            closer = ")" if opener == "(" else "}"
            end = recipe_text.find(closer, index + 2)
            if end < 0:
                raise AuditError(
                    f"CupidC active preprocessing found an unmodeled recipe "
                    f"Make reference/function for {output}: "
                    f"{recipe_text[index:]!r}"
                )
            reference = recipe_text[index : end + 1]
            name = recipe_text[index + 2 : end]
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
                raise AuditError(
                    f"CupidC active preprocessing found an unmodeled recipe "
                    f"Make reference/function for {output}: {reference!r}"
                )
            markers[name] += 1
            index = end + 1
            continue
        if opener in automatic_variables:
            index += 2
            continue
        raise AuditError(
            f"CupidC active preprocessing found an unmodeled recipe dollar "
            f"reference for {output}: {recipe_text[index:index + 2]!r}"
        )
    unknown = sorted(set(markers) - allowed)
    if unknown:
        raise AuditError(
            f"CupidC active preprocessing found unknown recipe marker(s) for "
            f"{output}: {', '.join(unknown)}"
        )
    return markers


def _c_preprocessor_literal_recipe_flags(
    transform: dict[str, object], tokens: list[str] | None = None
) -> list[str]:
    if tokens is None:
        recipe = transform.get("recipe")
        if not isinstance(recipe, list):
            return []
        tokens = []
        for line in recipe:
            if not isinstance(line, str):
                continue
            try:
                tokens.extend(shlex.split(line, posix=True))
            except ValueError as exc:
                raise AuditError(
                    f"could not tokenize CupidC compile recipe for "
                    f"{transform.get('output')}: {exc}"
                ) from exc
    safe_recipe_flags = {"-c", "-o", "-Os"}
    return sorted(
        token
        for token in tokens
        if (token.startswith("-") and token not in safe_recipe_flags)
        or token.startswith("@")
        or re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", token) is not None
    )


def _c_preprocessor_one_c_root(transform: dict[str, object]) -> str:
    output = str(transform.get("output", "<unknown>"))
    inputs = transform.get("inputs")
    if not isinstance(inputs, list) or not all(
        isinstance(path, str) for path in inputs
    ):
        raise AuditError(
            f"CupidC active preprocessing inputs are absent for {output}"
        )
    roots = [
        path
        for path in inputs
        if _language(path) in {"c", "cupid_c"}
    ]
    if len(roots) != 1:
        rendered = ", ".join(roots) if roots else "<none>"
        raise AuditError(
            f"CupidC active preprocessing expected exactly one "
            f"C translation-unit root for {output}; "
            f"found {len(roots)}: {rendered}"
        )
    return roots[0]


def _c_preprocessor_profile_for_c_transform(
    directory: str, transform: dict[str, object]
) -> str:
    output = str(transform.get("output", "<unknown>"))
    recipe_tokens = _c_preprocessor_compile_recipe_tokens(transform)
    if transform.get("tools") == ["cupid_c_compiler", "host_python"]:
        markers = _c_preprocessor_recipe_markers(
            transform,
            {"CUPIDC_KERNEL_COMPILE", "CUPIDC_PRODUCTION_COMPILE"},
        )
        root = _c_preprocessor_one_c_root(transform)
        if "CUPIDC_PRODUCTION_COMPILE" in markers:
            expected_markers = collections.Counter(
                {"CUPIDC_PRODUCTION_COMPILE": 1}
            )
            if directory == ".":
                if root not in {
                    "kernel/util/bin_programs_gen.cc",
                    "kernel/util/demos_programs_gen.cc",
                    "kernel/util/docs_programs_gen.cc",
                }:
                    raise AuditError(
                        "CupidC generated-install wrapper found an "
                        f"unapproved root: {root}"
                    )
                source_argument = "$<"
                output_argument = "$@"
                profile = "KERNEL_I386"
            elif directory == "user":
                if root not in {
                    "user/examples/cat.cc",
                    "user/examples/hello.cc",
                    "user/examples/ls.cc",
                }:
                    raise AuditError(
                        "CupidC user wrapper found an unapproved root: "
                        f"{root}"
                    )
                source_argument = "user/$<"
                output_argument = "user/$@"
                profile = "USER_I386"
            else:
                raise AuditError(
                    "CupidC production wrapper is outside an approved "
                    f"build root for {output}: {directory!r}"
                )
            expected_tokens = [
                "$(CUPIDC_PRODUCTION_COMPILE)",
                "--source",
                source_argument,
                "--output",
                output_argument,
            ]
        else:
            expected_markers = collections.Counter(
                {"CUPIDC_KERNEL_COMPILE": 1}
            )
            if directory != ".":
                raise AuditError(
                    "CupidC kernel compile wrapper is outside the root "
                    f"build for {output}: {directory!r}"
                )
            doom_compat_roots = {
                "kernel/doom/dglibc.cc",
                "kernel/doom/doom_libc_stubs.cc",
                "kernel/doom/doomgeneric_cupidos.cc",
            }
            if root in doom_compat_roots:
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--profile",
                    "doom-compat",
                    "--source",
                    root,
                    "--output",
                    output,
                ]
                profile = "DOOM_COMPAT_I386"
            elif root == "kernel/doom/i_sound_cupidos.cc":
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--profile",
                    "doom-tree",
                    "--source",
                    root,
                    "--output",
                    output,
                ]
                profile = "DOOM_TREE_I386"
            elif root.startswith("kernel/doom/src/"):
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--profile",
                    "doom-tree",
                    "--source",
                    "$<",
                    "--output",
                    "$@",
                ]
                profile = "DOOM_TREE_I386"
            else:
                expected_tokens = [
                    "$(CUPIDC_KERNEL_COMPILE)",
                    "--source",
                    root,
                    "--output",
                    output,
                ]
                profile = "KERNEL_I386"
        if markers != expected_markers:
            raise AuditError(
                "CupidC compile wrapper markers differ for "
                f"{output}: expected={dict(expected_markers)!r}, "
                f"actual={dict(sorted(markers.items()))!r}"
            )
        if recipe_tokens != expected_tokens:
            raise AuditError(
                "CupidC compile wrapper arguments differ for "
                f"{output}: expected={expected_tokens!r}, "
                f"actual={recipe_tokens!r}"
            )
        return profile
    if directory == ".":
        markers = _c_preprocessor_recipe_markers(
            transform,
            {
                "CC",
                "CFLAGS",
                "CFLAGS_DOOM",
                "CFLAGS_DOOM_TREE",
                "OPT",
            },
        )
        profiles = {
            "CFLAGS": "KERNEL_I386",
            "CFLAGS_DOOM": "DOOM_COMPAT_I386",
            "CFLAGS_DOOM_TREE": "DOOM_TREE_I386",
        }
    elif directory == "user":
        markers = _c_preprocessor_recipe_markers(
            transform, {"CC", "CFLAGS"}
        )
        profiles = {"CFLAGS": "USER_I386"}
    elif directory == "toolchain":
        markers = _c_preprocessor_recipe_markers(
            transform, {"CC", "CPPFLAGS", "CFLAGS"}
        )
        expected_markers = collections.Counter(
            {"CC": 1, "CPPFLAGS": 1, "CFLAGS": 1}
        )
        if markers != expected_markers:
            raise AuditError(
                f"CupidC active preprocessing hosted recipe markers differ "
                f"for {output}: "
                f"expected={dict(sorted(expected_markers.items()))!r}, "
                f"actual={dict(sorted(markers.items()))!r}"
            )
        profiles = {"CFLAGS": "HOSTED_TOOLCHAIN_64"}
    else:
        raise AuditError(
            f"CupidC active preprocessing has no profile for supported build "
            f"directory {directory!r} ({output})"
        )
    selected_markers = [marker for marker in profiles if marker in markers]
    selected = [profiles[marker] for marker in selected_markers]
    if len(selected) != 1:
        rendered = ", ".join(sorted(markers)) if markers else "<none>"
        raise AuditError(
            f"CupidC active preprocessing expected exactly one profile recipe "
            f"marker for {output}; found {len(selected)} in: {rendered}"
        )
    selected_marker = selected_markers[0]
    if directory != "toolchain":
        expected_markers = collections.Counter(
            {"CC": 1, selected_marker: 1}
        )
        if directory == "." and "OPT" in markers:
            expected_markers["OPT"] = 1
        if markers != expected_markers:
            raise AuditError(
                f"CupidC active preprocessing recipe markers differ for "
                f"{output}: "
                f"expected={dict(sorted(expected_markers.items()))!r}, "
                f"actual={dict(sorted(markers.items()))!r}"
            )
    literal_flags = _c_preprocessor_literal_recipe_flags(
        transform, recipe_tokens
    )
    if directory == "toolchain":
        unexpected_literal_flags = [
            flag
            for flag in literal_flags
            if flag not in {"-I../kernel/lang", "-x"}
        ]
        if unexpected_literal_flags:
            raise AuditError(
                f"CupidC active preprocessing found literal preprocessor "
                f"flag(s) outside the selected profile for {output}: "
                f"{unexpected_literal_flags!r}"
            )
        logical = _c_preprocessor_logical_path(
            _c_preprocessor_one_c_root(transform)
        )
        bridge_source = logical in _C_PP_HOSTED_BRIDGE_CASES
        expected_literal_flags = (
            ["-I../kernel/lang", "-x"]
            if bridge_source and logical.endswith(".cc")
            else ["-I../kernel/lang"]
            if bridge_source
            else ["-x"]
            if logical.endswith(".cc")
            else []
        )
        if literal_flags != expected_literal_flags:
            raise AuditError(
                f"CupidC active preprocessing hosted bridge recipe differs "
                f"for {logical}: expected_bridge={bridge_source!r}, "
                f"actual_flags={literal_flags!r}"
            )
        expected_argument_profile = ["CC", "CPPFLAGS"]
        if bridge_source:
            expected_argument_profile.append("-I../kernel/lang")
        expected_argument_profile.append("CFLAGS")
        if logical.endswith(".cc"):
            expected_argument_profile.extend(("-x", "c"))
        _c_preprocessor_require_compiler_invocation(
            transform, recipe_tokens, expected_argument_profile, logical
        )
        return (
            "HOSTED_KERNEL_BRIDGE_64"
            if bridge_source
            else selected[0]
        )
    if literal_flags:
        raise AuditError(
            f"CupidC active preprocessing found literal preprocessor "
            f"flag(s) outside the selected profile for {output}: "
            f"{literal_flags!r}"
        )
    expected_argument_profile = ["CC", selected_marker]
    if directory == "." and "OPT" in markers:
        expected_argument_profile.append("OPT")
    _c_preprocessor_require_compiler_invocation(
        transform, recipe_tokens, expected_argument_profile, output
    )
    return selected[0]


def _c_preprocessor_profile_configuration() -> tuple[
    tuple[tuple[str, str, str], ...],
    tuple[tuple[str, str, str], ...],
    tuple[tuple[str, str], ...],
]:
    include_roots: list[tuple[str, str, str]] = []
    for profile in ("KERNEL_I386", "DOOM_COMPAT_I386", "DOOM_TREE_I386"):
        include_roots.extend(
            (profile, root, _C_PP_INCLUDE_BOTH)
            for root in _C_PP_KERNEL_INCLUDE_ROOTS
        )
        if profile != "KERNEL_I386":
            include_roots.extend(
                (profile, root, _C_PP_INCLUDE_BOTH)
                for root in _C_PP_DOOM_EXTRA_INCLUDE_ROOTS
            )
    include_roots.append(
        ("USER_I386", "/user", _C_PP_INCLUDE_BOTH)
    )
    include_roots.extend(
        (
            ("HOSTED_TOOLCHAIN_64", "/toolchain", _C_PP_INCLUDE_BOTH),
            (
                "HOSTED_KERNEL_BRIDGE_64",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_KERNEL_BRIDGE_64",
                "/kernel/lang",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_LINUX",
                "/toolchain/hosted/i386-linux/include",
                "CTOOL_C_PP_INCLUDE_ANGLE",
            ),
            (
                "HOSTED_I386_WINDOWS",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_WINDOWS",
                "/toolchain/hosted/i386-linux/include",
                "CTOOL_C_PP_INCLUDE_ANGLE",
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
                "/kernel/lang",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_KERNEL_BRIDGE",
                "/toolchain/hosted/i386-linux/include",
                "CTOOL_C_PP_INCLUDE_ANGLE",
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain",
                _C_PP_INCLUDE_BOTH,
            ),
            (
                "HOSTED_I386_LINUX_GNU",
                "/toolchain/hosted/i386-linux/include",
                "CTOOL_C_PP_INCLUDE_ANGLE",
            ),
        )
    )

    macros: list[tuple[str, str, str]] = []
    for profile in (
        "KERNEL_I386",
        "DOOM_COMPAT_I386",
        "DOOM_TREE_I386",
        "USER_I386",
    ):
        macros.extend(
            (profile, name, replacement)
            for name, replacement in _C_PP_COMMON_I386_MACROS
        )
        if profile in {
            "KERNEL_I386",
            "DOOM_COMPAT_I386",
            "DOOM_TREE_I386",
        }:
            macros.append((profile, "__SSE2__", "1"))
        if profile == "KERNEL_I386":
            macros.append((profile, "DEBUG", "1"))
        elif profile == "DOOM_TREE_I386":
            macros.extend(
                (
                    (profile, "DEFAULT_SAVEGAMEDIR", '"/home/doom/"'),
                    (profile, "DOOM_PORT_CUPIDOS", "1"),
                )
            )
    macros.extend(
        (
            ("FREESTANDING_I386", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_TOOLCHAIN_64", "__SIZEOF_POINTER__", "8"),
            ("HOSTED_KERNEL_BRIDGE_64", "__SIZEOF_POINTER__", "8"),
            ("HOSTED_I386_LINUX", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_I386_WINDOWS", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_I386_WINDOWS", "_WIN32", "1"),
            ("HOSTED_I386_KERNEL_BRIDGE", "__SIZEOF_POINTER__", "4"),
            ("HOSTED_I386_LINUX_GNU", "__SIZEOF_POINTER__", "4"),
        )
    )
    forced_includes = (
        ("DOOM_TREE_I386", "/kernel/doom/dglibc_compat.h"),
    )
    return tuple(include_roots), tuple(macros), forced_includes


_C_PP_I386_MODELED_FLAGS = frozenset(
    {
        "--target=i386-unknown-elf",
        "-m32",
        "-mfpmath=sse",
        "-msse",
        "-msse2",
        "-mstackrealign",
        "-fno-pie",
        "-fno-stack-protector",
        "-nostdlib",
        "-nostdinc",
        "-ffreestanding",
        "-fno-asynchronous-unwind-tables",
        "-fno-unwind-tables",
        "-c",
        "-fno-omit-frame-pointer",
        "-static",
    }
)
_C_PP_COMMON_MODELED_FLAGS = frozenset(
    {
        "-O2",
        "-pedantic",
        "-Werror",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wpointer-arith",
        "-Wcast-qual",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wwrite-strings",
        "-Wno-gnu-zero-variadic-macro-arguments",
        "-Wno-strict-prototypes",
        "-Wno-implicit-int-conversion",
        "-Wno-sign-conversion",
        "-Wno-unused",
        "-Wno-unused-result",
        "-Wno-implicit-function-declaration",
        "-Wno-sign-compare",
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-type-limits",
        "-Wno-missing-field-initializers",
        "-I",
        "-D",
        "-include",
    }
)


def _c_preprocessor_unmodeled_flags(
    flags: set[str], profile_flags: frozenset[str] | set[str] = frozenset()
) -> list[str]:
    modeled_flags = set(_C_PP_COMMON_MODELED_FLAGS)
    modeled_flags.update(profile_flags)
    return sorted(
        flag
        for flag in flags
        if flag not in modeled_flags
        and not (flag.startswith("-I") and len(flag) > 2)
        and not (flag.startswith("-D") and len(flag) > 2)
    )


def _validate_hosted_i386_contract_profiles(root: Path) -> None:
    contract_path = root / "toolchain" / "tests" / "cupidc_object_contract.cc"
    try:
        source = contract_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise AuditError(
            "CupidC hosted i386 source-profile contract is unavailable: "
            f"{contract_path}"
        ) from exc

    array_start = (
        "static const host_tool_source_case_t source_cases[] = {"
    )
    array_end = "  static const ctool_u32 cupidasm_objects[] = {"
    start = source.find(array_start)
    end = source.find(array_end, start + len(array_start))
    if start < 0 or end < 0:
        raise AuditError(
            "CupidC hosted i386 source-profile table shape changed"
        )
    table = source[start:end]
    row_pattern = re.compile(
        r'\{\s*"(?P<source>/[^"]+)"\s*,\s*"(?P<object>/[^"]+)"\s*,\s*'
        r"(?P<kind>HOST_TOOL_SOURCE_C|HOST_TOOL_SOURCE_ASSEMBLY)\s*,\s*"
        r"(?P<gnu>CTOOL_TRUE|CTOOL_FALSE)\s*\}",
        re.DOTALL,
    )
    rows = [
        (
            match.group("source"),
            match.group("kind"),
            match.group("gnu"),
        )
        for match in row_pattern.finditer(table)
    ]
    actual: dict[str, tuple[str, str]] = {}
    for path, kind, gnu_extensions in rows:
        if path in actual:
            raise AuditError(
                "CupidC hosted i386 source-profile table duplicates "
                f"{path}"
            )
        actual[path] = (kind, gnu_extensions)
    expected = {
        path: ("HOST_TOOL_SOURCE_C", "CTOOL_FALSE")
        for path in _C_PP_HOSTED_I386_STRICT_CASES
    }
    expected.update(
        {
            path: ("HOST_TOOL_SOURCE_C", "CTOOL_TRUE")
            for path in _C_PP_HOSTED_I386_GNU_CASES
        }
    )
    expected["/toolchain/hosted/i386-linux/start.asm"] = (
        "HOST_TOOL_SOURCE_ASSEMBLY",
        "CTOOL_FALSE",
    )
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        changed = sorted(
            path
            for path in set(actual) & set(expected)
            if actual[path] != expected[path]
        )
        raise AuditError(
            "CupidC hosted i386 source-profile rows differ from the checked "
            f"contract: missing={missing!r}, unexpected={unexpected!r}, "
            f"changed={changed!r}"
        )

    emitter_start = source.find(
        "static ctool_status_t emit_hosted_i386_source_with_extensions("
    )
    emitter_end = source.find(
        "\nstatic ctool_status_t emit_hosted_i386_source(",
        emitter_start + 1,
    )
    if emitter_start < 0 or emitter_end < 0:
        raise AuditError(
            "CupidC hosted i386 profile emitter shape changed"
        )
    emitter = source[emitter_start:emitter_end]
    required_emitter_fragments = (
        "pp_request = profile->request;",
        "pp_request.gnu_extensions = gnu_extensions;",
        "ctool_c_preprocess(job, source, &pp_request, &tape)",
        "parse_request.gnu_extensions = gnu_extensions;",
    )
    missing_fragments = [
        fragment
        for fragment in required_emitter_fragments
        if emitter.count(fragment) != 1
    ]
    if missing_fragments:
        raise AuditError(
            "CupidC hosted i386 profile emitter does not forward the checked "
            f"GNU mode: {missing_fragments!r}"
        )
    compile_loop = source[end:]
    if compile_loop.count("source_cases[index].gnu_extensions") != 2:
        raise AuditError(
            "CupidC hosted i386 compile loop does not consume each checked "
            "source profile for both emissions"
        )


def _cupid_toolchain_fixed_point_contract(
    root: Path,
) -> dict[str, object]:
    test_path = root / "tests" / "test_toolchain_cupidc_object.py"
    driver_path = root / "toolchain" / "cupidc_main.cc"
    linker_header_path = root / "toolchain" / "cupidld.h"
    linker_cli_path = root / "toolchain" / "cupidld_main.cc"
    linker_core_path = root / "toolchain" / "cupidld.cc"
    bootstrap_path = root / "tools" / "bootstrap_toolchain.py"
    contract_publisher_path = (
        root / "tools" / "cupidc_toolchain_contracts.py"
    )
    windows_runtime_contract_path = (
        root
        / "toolchain"
        / "tests"
        / "hosted_i386_windows_runtime_contract.cc"
    )
    windows_publication_header_path = (
        root / "toolchain/hosted/i386-linux/include/windows.h"
    )
    windows_publication_runtime_path = (
        root / "toolchain/hosted/i386-windows/publication_runtime.cc"
    )
    windows_publication_start_path = (
        root / "toolchain/hosted/i386-windows/publication_start.asm"
    )
    try:
        test_source = test_path.read_text(encoding="utf-8")
        driver_source = driver_path.read_text(encoding="utf-8")
        linker_header_source = linker_header_path.read_text(encoding="utf-8")
        linker_cli_source = linker_cli_path.read_text(encoding="utf-8")
        linker_core_source = linker_core_path.read_text(encoding="utf-8")
        bootstrap_source = bootstrap_path.read_text(encoding="utf-8")
        contract_publisher_source = contract_publisher_path.read_text(
            encoding="utf-8"
        )
        windows_runtime_contract_source = (
            windows_runtime_contract_path.read_text(encoding="utf-8")
        )
        windows_publication_header_source = (
            windows_publication_header_path.read_text(encoding="utf-8")
        )
        windows_publication_runtime_source = (
            windows_publication_runtime_path.read_text(encoding="utf-8")
        )
        windows_publication_start_source = (
            windows_publication_start_path.read_text(encoding="utf-8")
        )
        test_tree = ast.parse(test_source, filename=str(test_path))
        bootstrap_tree = ast.parse(
            bootstrap_source, filename=str(bootstrap_path)
        )
        contract_publisher_tree = ast.parse(
            contract_publisher_source,
            filename=str(contract_publisher_path),
        )
    except (OSError, SyntaxError) as exc:
        raise AuditError(
            "Cupid Toolchain fixed-point contract is unavailable"
        ) from exc

    def active_c_source(source: str) -> str:
        source = _mask_c_comments_preserve_literals(source)
        lines = source.splitlines(keepends=True)
        disabled_depth = 0
        active: list[str] = []
        for line in lines:
            directive = re.match(
                r"^\s*#\s*(if|ifdef|ifndef|endif)\b(.*)$", line
            )
            starts_disabled = (
                disabled_depth == 0
                and directive is not None
                and directive.group(1) == "if"
                and re.fullmatch(
                    r"\s*(?:0|\(\s*0\s*\))\s*",
                    directive.group(2),
                )
                is not None
            )
            if disabled_depth != 0 or starts_disabled:
                active.append(
                    "".join("\n" if char == "\n" else " " for char in line)
                )
            else:
                active.append(line)
            if starts_disabled:
                disabled_depth = 1
            elif disabled_depth != 0 and directive is not None:
                if directive.group(1) in {"if", "ifdef", "ifndef"}:
                    disabled_depth += 1
                elif directive.group(1) == "endif":
                    disabled_depth -= 1
        if disabled_depth != 0:
            raise AuditError(
                "Cupid Toolchain fixed-point PE32 source contract differs: "
                "a disabled preprocessor block is not closed"
            )
        return "".join(active)

    active_windows_runtime_contract_source = active_c_source(
        windows_runtime_contract_source
    )
    required_windows_runtime_contract_fragments = (
        "static int allocator_contract(void)",
        "allocation = (unsigned char *)calloc(16u, 1u);",
        "replacement = (unsigned char *)realloc(allocation, 64u);",
        "overflow = calloc(CUPID_RUNTIME_UINT_MAX, 2u);",
        "overflow != (void *)0 || errno != ENOMEM",
        "static int file_contract(const char *output_path,",
        'stream = fopen(output_path, "ab");',
        "fseek(stream, 0L, 0) != 0 ||",
        "memcmp(contents, expected, 8u) != 0",
        "fopen_s((FILE **)0, output_path, \"rb\") != EINVAL",
        "fopen_s(&stream, missing_path, \"rb\") != ENOENT",
        "static int directory_contract(void)",
        "getcwd(directory, sizeof(directory)) != directory",
        "_getcwd(small, 1) != (char *)0 || errno != ERANGE",
        "getcwd((char *)0, sizeof(directory)) != (char *)0 ||",
        "result = allocator_contract();\n"
        "  if (result == 0) {\n"
        "    result = file_contract(argv[5], argv[6]);\n"
        "  }\n"
        "  if (result == 0) {\n"
        "    result = directory_contract();\n"
        "  }",
    )
    if any(
        active_windows_runtime_contract_source.count(fragment) != 1
        for fragment in required_windows_runtime_contract_fragments
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point Windows runtime contract differs"
        )

    def sequence_positions(
        tokens: tuple[str, ...], expected: tuple[str, ...]
    ) -> list[int]:
        width = len(expected)
        return [
            index
            for index in range(len(tokens) - width + 1)
            if tokens[index : index + width] == expected
        ]

    def brace_depth(tokens: tuple[str, ...], end: int) -> int:
        depth = 0
        for token in tokens[:end]:
            if token == "{":
                depth += 1
            elif token == "}":
                depth -= 1
        return depth

    def c_tokens(source: str, path: Path) -> tuple[str, ...]:
        return _normalize_c_preprocessing_tokens(source, str(path), 1)

    def c_function_tokens(
        source: str,
        path: Path,
        signature: str,
    ) -> tuple[str, ...] | None:
        structure = _mask_c_noncode(source)
        matches = list(re.finditer(signature, structure, flags=re.MULTILINE))
        if len(matches) != 1:
            return None
        opening = structure.find("{", matches[0].start(), matches[0].end())
        if opening < 0:
            return None
        depth = 0
        closing = -1
        for index in range(opening, len(structure)):
            if structure[index] == "{":
                depth += 1
            elif structure[index] == "}":
                depth -= 1
                if depth == 0:
                    closing = index
                    break
        if closing < 0:
            return None
        return c_tokens(source[opening + 1 : closing], path)

    def c_function_has_live_sequences(
        source: str,
        path: Path,
        signature: str,
        snippets: tuple[str, ...],
    ) -> bool:
        function_tokens = c_function_tokens(source, path, signature)
        if function_tokens is None:
            return False
        positions: list[int] = []
        for snippet in snippets:
            expected = c_tokens(snippet, path)
            matches = sequence_positions(function_tokens, expected)
            if (
                len(matches) != 1
                or brace_depth(function_tokens, matches[0]) != 0
            ):
                return False
            positions.append(matches[0])
        return positions == sorted(positions)

    def token_digest(tokens: tuple[str, ...] | None) -> str | None:
        if tokens is None:
            return None
        return hashlib.sha256("\0".join(tokens).encode("utf-8")).hexdigest()

    active_windows_publication_header = active_c_source(
        windows_publication_header_source
    )
    active_windows_publication_runtime = active_c_source(
        windows_publication_runtime_source
    )
    windows_publication_asm_lines = tuple(
        " ".join(line.split())
        for raw_line in windows_publication_start_source.splitlines()
        for line in (raw_line.split(";", 1)[0].strip(),)
        if line
    )
    windows_publication_sources_match = (
        token_digest(
            c_tokens(
                active_windows_publication_header,
                windows_publication_header_path,
            )
        )
        == "813ffb624fc3ed5ff84bd837895c3a04886c5f80a10f403ef2510b0df1d1c423"
        and token_digest(
            c_tokens(
                active_windows_publication_runtime,
                windows_publication_runtime_path,
            )
        )
        == "536fa0a609ddaf6fe90c3fb0696c8e66823284634b75811f03d275427187ad0c"
        and hashlib.sha256(
            "\n".join(windows_publication_asm_lines).encode("utf-8")
        ).hexdigest()
        == "88a5d8973c1f08a655486f7a5daaa711128713202d257fc04872f58304712e80"
    )
    if not windows_publication_sources_match:
        raise AuditError(
            "Cupid Toolchain fixed-point Windows publication contract differs"
        )

    windows_runtime_function_contracts = (
        (
            r"\bstatic\s+int\s+allocator_contract\s*"
            r"\(\s*void\s*\)\s*\{",
            "659f1cefcfe89461826d8a0ed85eed854ae523ad9c160c2754c1d426451d33c3",
            (
                "allocation = (unsigned char *)calloc(16u, 1u); "
                "if (allocation == (unsigned char *)0) { return 11; }",
                "for (index = 0u; index < 16u; index++) { "
                "if (allocation[index] != 0u) { free(allocation); "
                "return 12; } allocation[index] = "
                "(unsigned char)(index + 1u); }",
                "replacement = (unsigned char *)realloc(allocation, 64u); "
                "if (replacement == (unsigned char *)0) { "
                "free(allocation); return 13; }",
                "for (index = 0u; index < 16u; index++) { "
                "if (replacement[index] != "
                "(unsigned char)(index + 1u)) { free(replacement); "
                "return 14; } }",
                "free(replacement); errno = 0; overflow = "
                "calloc(CUPID_RUNTIME_UINT_MAX, 2u); "
                "if (overflow != (void *)0 || errno != ENOMEM) { "
                "free(overflow); return 15; } return 0;",
            ),
        ),
        (
            r"\bstatic\s+int\s+file_contract\s*"
            r"\(\s*const\s+char\s*\*\s*output_path\s*,\s*"
            r"const\s+char\s*\*\s*missing_path\s*\)\s*\{",
            "b76b9f30fea04b33b20c96f7756ad7dccf599a1d5025b9d9aa6c410f1e8bd9cc",
            (
                "if (fopen_s(&stream, output_path, \"wb\") != 0 || "
                "stream == (FILE *)0) { return 21; }",
                "if (fwrite(first, 1u, 4u, stream) != 4u || "
                "fclose(stream) != 0) { return 22; }",
                "stream = fopen(output_path, \"ab\"); "
                "if (stream == (FILE *)0) { return 23; }",
                "if (fseek(stream, 0L, 0) != 0 || "
                "fwrite(appended, 1u, 4u, stream) != 4u || "
                "fclose(stream) != 0) { return 24; }",
                "stream = fopen(output_path, \"rb\"); "
                "if (stream == (FILE *)0) { return 25; }",
                "(void)memset(contents, 0, sizeof(contents)); "
                "if (fread(contents, 1u, 8u, stream) != 8u || "
                "fread(contents + 8, 1u, 1u, stream) != 0u || "
                "ferror(stream) != 0 || fclose(stream) != 0 || "
                "memcmp(contents, expected, 8u) != 0) { return 26; }",
                "errno = 0; if (fopen_s((FILE **)0, output_path, \"rb\") "
                "!= EINVAL || errno != EINVAL) { return 27; }",
                "errno = 0; stream = (FILE *)0; "
                "if (fopen_s(&stream, missing_path, \"rb\") != ENOENT || "
                "stream != (FILE *)0 || errno != ENOENT) { return 28; } "
                "return 0;",
            ),
        ),
        (
            r"\bstatic\s+int\s+directory_contract\s*"
            r"\(\s*void\s*\)\s*\{",
            "a60288a1df3c5cbe3253073f26a4c22ed72061b58f7959c5350223447fd92716",
            (
                "if (getcwd(directory, sizeof(directory)) != directory || "
                "directory[0] == '\\0') { return 31; }",
                "errno = 0; if (_getcwd(small, 1) != (char *)0 || "
                "errno != ERANGE) { return 32; }",
                "errno = 0; if (getcwd((char *)0, sizeof(directory)) != "
                "(char *)0 || errno != EINVAL) { return 33; } return 0;",
            ),
        ),
    )
    if any(
        not c_function_has_live_sequences(
            active_windows_runtime_contract_source,
            windows_runtime_contract_path,
            signature,
            snippets,
        )
        or token_digest(
            c_function_tokens(
                active_windows_runtime_contract_source,
                windows_runtime_contract_path,
                signature,
            )
        )
        != expected_digest
        for signature, expected_digest, snippets
        in windows_runtime_function_contracts
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point Windows runtime contract differs"
        )

    windows_runtime_main_tokens = c_function_tokens(
        active_windows_runtime_contract_source,
        windows_runtime_contract_path,
        r"\bint\s+main\s*\(\s*int\s+argc\s*,\s*char\s*\*\*\s*argv\s*\)\s*\{",
    )
    windows_runtime_main_sequences = (
        c_tokens(
            "result = allocator_contract();",
            windows_runtime_contract_path,
        ),
        c_tokens(
            "if (result == 0) { result = file_contract(argv[5], argv[6]); }",
            windows_runtime_contract_path,
        ),
        c_tokens(
            "if (result == 0) { result = directory_contract(); }",
            windows_runtime_contract_path,
        ),
    )
    windows_runtime_main_positions = (
        [
            sequence_positions(windows_runtime_main_tokens, sequence)
            for sequence in windows_runtime_main_sequences
        ]
        if windows_runtime_main_tokens is not None
        else []
    )
    if (
        token_digest(windows_runtime_main_tokens)
        != "c6c0bbe07e559de9de38198ad8a921b9539a598ed7a3eee503da11cb47c3ed1f"
        or
        len(windows_runtime_main_positions) != 3
        or any(len(positions) != 1 for positions in windows_runtime_main_positions)
        or not all(
            brace_depth(windows_runtime_main_tokens, positions[0]) == 0
            for positions in windows_runtime_main_positions
        )
        or not (
            windows_runtime_main_positions[0][0]
            < windows_runtime_main_positions[1][0]
            < windows_runtime_main_positions[2][0]
        )
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point Windows runtime contract differs"
        )

    linker_header_active = active_c_source(linker_header_source)
    linker_cli_active = active_c_source(linker_cli_source)
    linker_core_active = active_c_source(linker_core_source)
    linker_core_preprocessing_tokens = c_tokens(
        linker_core_active, linker_core_path
    )
    image_enum_matches = re.findall(
        r"\btypedef\s+enum\s*\{([^{}]*)\}\s*"
        r"ctool_ld_image_kind_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    request_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*ctool_ld_request_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    import_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*"
        r"ctool_ld_pe32_import_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    result_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*ctool_ld_result_t\s*;",
        linker_header_active,
        flags=re.DOTALL,
    )
    publication_ops_matches = re.findall(
        r"\btypedef\s+struct\s*\{([^{}]*)\}\s*"
        r"cupidld_publication_ops_t\s*;",
        linker_cli_active,
        flags=re.DOTALL,
    )
    expected_image_enum = (
        "CTOOL_LD_IMAGE_ELF32",
        "=",
        "0",
        ",",
        "CTOOL_LD_IMAGE_PE32_FIXED",
    )
    expected_request_member = (
        "ctool_ld_image_kind_t",
        "image_kind",
        ";",
    )
    expected_import_members = (
        "ctool_string_t",
        "symbol_name",
        ";",
        "ctool_string_t",
        "library_name",
        ";",
        "ctool_string_t",
        "procedure_name",
        ";",
    )
    expected_import_request_members = (
        (
            "const",
            "ctool_ld_pe32_import_t",
            "*",
            "pe32_imports",
            ";",
        ),
        ("ctool_u32", "pe32_import_count", ";"),
    )
    expected_import_result_members = (
        ("ctool_u32", "imported_symbol_count", ";"),
        ("ctool_u32", "imported_library_count", ";"),
    )
    expected_verifier_member = (
        "ctool_status_t",
        "(",
        "*",
        "verify",
        ")",
        "(",
        "const",
        "char",
        "*",
        "candidate",
        ",",
        "ctool_bytes_t",
        "contents",
        ")",
        ";",
    )
    request_tokens = (
        c_tokens(request_matches[0], linker_header_path)
        if len(request_matches) == 1
        else ()
    )
    result_tokens = (
        c_tokens(result_matches[0], linker_header_path)
        if len(result_matches) == 1
        else ()
    )
    if (
        len(image_enum_matches) != 1
        or c_tokens(image_enum_matches[0], linker_header_path)
        != expected_image_enum
        or len(request_matches) != 1
        or len(
            sequence_positions(
                request_tokens,
                expected_request_member,
            )
        )
        != 1
        or len(import_matches) != 1
        or c_tokens(import_matches[0], linker_header_path)
        != expected_import_members
        or any(
            len(sequence_positions(request_tokens, member)) != 1
            for member in expected_import_request_members
        )
        or len(result_matches) != 1
        or any(
            len(sequence_positions(result_tokens, member)) != 1
            for member in expected_import_result_members
        )
        or linker_header_active.count("CTOOL_LD_DIAG_BAD_IMPORT") != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the public image, import, request, or result contract is absent"
        )
    if (
        len(publication_ops_matches) != 1
        or len(
            sequence_positions(
                c_tokens(publication_ops_matches[0], linker_cli_path),
                expected_verifier_member,
            )
        )
        != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the publication verifier operation is absent"
        )

    parse_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+int\s+cupidld_parse_cli\s*\([^;{}]*\)\s*\{",
    )
    import_parse_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+int\s+cupidld_parse_import\s*\([^;{}]*\)\s*\{",
    )
    main_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bint\s+main\s*\([^;{}]*\)\s*\{",
    )
    publication_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+ctool_status_t\s+cupidld_publish_output_with_ops\s*"
        r"\([^;{}]*\)\s*\{",
    )
    verifier_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+ctool_status_t\s+cupidld_publication_verify\s*"
        r"\([^;{}]*\)\s*\{",
    )
    publication_wrapper_tokens = c_function_tokens(
        linker_cli_active,
        linker_cli_path,
        r"\bstatic\s+ctool_status_t\s+cupidld_publish_output\s*"
        r"\([^;{}]*\)\s*\{",
    )
    accepted_machine = (
        "strcmp",
        "(",
        "cli",
        "->",
        "machine",
        ",",
        '"elf_i386"',
        ")",
        "!=",
        "0",
        "&&",
        "strcmp",
        "(",
        "cli",
        "->",
        "machine",
        ",",
        '"i386pe"',
        ")",
        "!=",
        "0",
    )
    rejected_script = (
        "strcmp",
        "(",
        "cli",
        "->",
        "machine",
        ",",
        '"i386pe"',
        ")",
        "==",
        "0",
        "||",
        "cli",
        "->",
        "have_text_address",
        "==",
        "CTOOL_TRUE",
        "||",
        "cli",
        "->",
        "entry",
        "!=",
        "(",
        "const",
        "char",
        "*",
        ")",
        "0",
    )
    image_kind_assignment = (
        "request",
        ".",
        "image_kind",
        "=",
        "strcmp",
        "(",
        "cli",
        ".",
        "machine",
        ",",
        '"i386pe"',
        ")",
        "==",
        "0",
        "?",
        "CTOOL_LD_IMAGE_PE32_FIXED",
        ":",
        "CTOOL_LD_IMAGE_ELF32",
        ";",
    )
    link_call = (
        "ctool_ld_link",
        "(",
        "job",
        ",",
        "&",
        "request",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
    )
    publication_dispatch = (
        "status",
        "=",
        "ctool_ld_link",
        "(",
        "job",
        ",",
        "&",
        "request",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
        ";",
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "status",
        "=",
        "cupidld_publish_output",
        "(",
        "native_paths",
        "[",
        "output_native_index",
        "]",
        ",",
        "ctool_buffer_view",
        "(",
        "output",
        ")",
        ")",
        ";",
        "}",
    )
    accepted_machine_positions = (
        sequence_positions(parse_tokens, accepted_machine)
        if parse_tokens is not None
        else []
    )
    rejected_script_positions = (
        sequence_positions(parse_tokens, rejected_script)
        if parse_tokens is not None
        else []
    )
    parse_is_exact = (
        parse_tokens is not None
        and len(accepted_machine_positions) == 1
        and brace_depth(parse_tokens, accepted_machine_positions[0]) == 0
        and len(rejected_script_positions) == 1
        and brace_depth(parse_tokens, rejected_script_positions[0]) == 1
    )
    assignment_positions = (
        sequence_positions(main_tokens, image_kind_assignment)
        if main_tokens is not None
        else []
    )
    link_positions = (
        sequence_positions(main_tokens, link_call)
        if main_tokens is not None
        else []
    )
    publication_positions = (
        sequence_positions(main_tokens, publication_dispatch)
        if main_tokens is not None
        else []
    )
    import_request_assignment = (
        "request",
        ".",
        "pe32_imports",
        "=",
        "imports",
        ";",
        "request",
        ".",
        "pe32_import_count",
        "=",
        "cli",
        ".",
        "import_count",
        ";",
    )
    import_assignment_positions = (
        sequence_positions(main_tokens, import_request_assignment)
        if main_tokens is not None
        else []
    )
    required_cli_import_fragments = (
        "--import IAT_SYMBOL=LIBRARY:PROCEDURE",
        'cupidld_take_value(argc, argv, &index, argument, "--import",',
        "cli->imports[cli->import_count] = value;",
        "cli->import_count++;",
        "if (cli->import_count != 0u && "
        'strcmp(cli->machine, "i386pe") != 0)',
        "equals = strchr(text, '=');",
        ": strchr(equals + 1, ':');",
        "import_out->symbol_name.data = text;",
        "import_out->library_name.data = equals + 1;",
        "import_out->procedure_name.data = colon + 1;",
        "cupidld_parse_import(cli.imports[index], &imports[index]) == 0",
    )
    missing_cli_import_fragments = [
        fragment
        for fragment in required_cli_import_fragments
        if linker_cli_active.count(fragment) != 1
    ]
    if (
        not parse_is_exact
        or import_parse_tokens is None
        or missing_cli_import_fragments
        or len(assignment_positions) != 1
        or len(import_assignment_positions) != 1
        or len(link_positions) != 1
        or len(publication_positions) != 1
        or assignment_positions[0] >= link_positions[0]
        or import_assignment_positions[0] >= link_positions[0]
        or brace_depth(main_tokens, assignment_positions[0]) != 0
        or brace_depth(main_tokens, import_assignment_positions[0]) != 0
        or brace_depth(main_tokens, publication_positions[0]) != 0
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the i386pe parser, import dispatch, or publication is absent"
        )

    publication_write = (
        "status",
        "=",
        "ops",
        "->",
        "write_all",
        "(",
        "&",
        "file",
        ",",
        "contents",
        ")",
        ";",
    )
    publication_close = (
        "ctool_status_t",
        "close_status",
        "=",
        "ops",
        "->",
        "close",
        "(",
        "&",
        "file",
        ")",
        ";",
    )
    publication_verify = (
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "status",
        "=",
        "ops",
        "->",
        "verify",
        "(",
        "candidate",
        ",",
        "contents",
        ")",
        ";",
        "}",
    )
    publication_replace = (
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "status",
        "=",
        "ops",
        "->",
        "replace",
        "(",
        "candidate",
        ",",
        "destination",
        ")",
        ";",
        "}",
    )
    publication_discard = (
        "if",
        "(",
        "status",
        "!=",
        "CTOOL_OK",
        ")",
        "{",
        "ops",
        "->",
        "discard",
        "(",
        "candidate",
        ")",
        ";",
        "}",
    )
    publication_wrapper = (
        "static",
        "const",
        "cupidld_publication_ops_t",
        "ops",
        "=",
        "{",
        "cupidld_publication_open",
        ",",
        "cupidld_publication_write_all",
        ",",
        "cupidld_publication_close",
        ",",
        "cupidld_publication_verify",
        ",",
        "cupidld_publication_replace",
        ",",
        "cupidld_publication_discard",
        "}",
        ";",
        "return",
        "cupidld_publish_output_with_ops",
        "(",
        "destination",
        ",",
        "contents",
        ",",
        "&",
        "ops",
        ")",
        ";",
    )
    publication_sequences = (
        publication_write,
        publication_close,
        publication_verify,
        publication_replace,
        publication_discard,
    )
    publication_sequence_positions = (
        [
            sequence_positions(publication_tokens, sequence)
            for sequence in publication_sequences
        ]
        if publication_tokens is not None
        else []
    )
    publication_order = (
        [positions[0] for positions in publication_sequence_positions]
        if len(publication_sequence_positions) == len(publication_sequences)
        and all(len(positions) == 1 for positions in publication_sequence_positions)
        else []
    )
    verifier_size_check = (
        "file_size",
        "<",
        "0l",
        "||",
        "(",
        "unsigned",
        "long",
        ")",
        "file_size",
        "!=",
        "contents",
        ".",
        "size",
    )
    verifier_read = (
        "fread",
        "(",
        "buffer",
        ",",
        "1u",
        ",",
        "(",
        "size_t",
        ")",
        "request",
        ",",
        "file",
        ")",
    )
    verifier_compare = (
        "memcmp",
        "(",
        "buffer",
        ",",
        "contents",
        ".",
        "data",
        "+",
        "total",
        ",",
        "count",
        ")",
        "!=",
        "0",
    )
    verifier_close = (
        "if",
        "(",
        "fclose",
        "(",
        "file",
        ")",
        "!=",
        "0",
        ")",
        "{",
        "status",
        "=",
        "CTOOL_ERR_IO",
        ";",
        "}",
        "return",
        "status",
        ";",
    )
    verifier_requirements = (
        verifier_size_check,
        verifier_read,
        verifier_compare,
        verifier_close,
    )
    verifier_positions = (
        [
            sequence_positions(verifier_tokens, requirement)
            for requirement in verifier_requirements
        ]
        if verifier_tokens is not None
        else []
    )
    verifier_order = (
        [positions[0] for positions in verifier_positions]
        if len(verifier_positions) == len(verifier_requirements)
        and all(len(positions) == 1 for positions in verifier_positions)
        else []
    )
    verifier_top_level_returns = (
        [
            index
            for index, token in enumerate(verifier_tokens)
            if token == "return" and brace_depth(verifier_tokens, index) == 0
        ]
        if verifier_tokens is not None
        else []
    )
    publication_top_level_returns = (
        [
            index
            for index, token in enumerate(publication_tokens)
            if token == "return"
            and brace_depth(publication_tokens, index) == 0
        ]
        if publication_tokens is not None
        else []
    )
    verified_replace_positions = (
        sequence_positions(
            publication_tokens,
            publication_verify + publication_replace,
        )
        if publication_tokens is not None
        else []
    )
    wrapper_positions = (
        sequence_positions(publication_wrapper_tokens, publication_wrapper)
        if publication_wrapper_tokens is not None
        else []
    )
    if (
        publication_tokens is None
        or publication_order != sorted(publication_order)
        or len(publication_order) != len(publication_sequences)
        or brace_depth(publication_tokens, publication_order[0]) != 0
        or brace_depth(publication_tokens, publication_order[1]) != 1
        or any(
            brace_depth(publication_tokens, position) != 0
            for position in publication_order[2:]
        )
        or len(verified_replace_positions) != 1
        or brace_depth(publication_tokens, verified_replace_positions[0]) != 0
        or len(publication_top_level_returns) != 1
        or publication_tokens[
            publication_top_level_returns[0] :
            publication_top_level_returns[0] + 3
        ]
        != ("return", "status", ";")
        or len(sequence_positions(publication_tokens, ("ops", "->", "verify")))
        != 2
        or len(
            sequence_positions(publication_tokens, ("ops", "->", "replace"))
        )
        != 2
        or len(verifier_positions) != len(verifier_requirements)
        or any(len(positions) != 1 for positions in verifier_positions)
        or verifier_order != sorted(verifier_order)
        or len(verifier_order) != len(verifier_requirements)
        or [brace_depth(verifier_tokens, position) for position in verifier_order]
        != [0, 1, 1, 0]
        or len(verifier_top_level_returns) != 1
        or verifier_tokens[
            verifier_top_level_returns[0] : verifier_top_level_returns[0] + 3
        ]
        != ("return", "status", ";")
        or verifier_tokens.count("CTOOL_OK") != 4
        or len(wrapper_positions) != 1
        or brace_depth(publication_wrapper_tokens, wrapper_positions[0]) != 0
        or re.findall(
            r"\bcupidld_publication_verify\b",
            _mask_c_noncode(linker_cli_active),
        ).count("cupidld_publication_verify")
        != 2
        or re.findall(
            r"\bcupidld_publication_replace\b",
            _mask_c_noncode(linker_cli_active),
        ).count("cupidld_publication_replace")
        != 2
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "publication does not verify the candidate before replacement"
        )

    serializer_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_serialize_pe32_fixed\s*"
        r"\([^;{}]*\)\s*\{",
    )
    linker_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bctool_status_t\s+ctool_ld_link\s*\([^;{}]*\)\s*\{",
    )
    image_rva_limit_guard = (
        "if",
        "(",
        "image_size",
        ">",
        "LD_PE_NAME_RVA_LIMIT",
        ")",
        "{",
        "status",
        "=",
        "ld_diagnostic",
        "(",
        "link",
        "->",
        "job",
        ",",
        "CTOOL_LD_DIAG_LIMIT",
        ",",
        "ctool_string",
        "(",
        '""',
        ")",
        ",",
        "0u",
        ",",
        "0u",
        ",",
        '"CupidLD PE32 image exceeds the 2 GiB RVA range"',
        ",",
        "CTOOL_ERR_LIMIT",
        ")",
        ";",
        "goto",
        "done",
        ";",
        "}",
    )
    serializer_requirements = (
        (
            "link",
            "->",
            "request",
            "->",
            "layout",
            ".",
            "kind",
            "!=",
            "CTOOL_LD_LAYOUT_FIXED_TEXT",
        ),
        (
            "link",
            "->",
            "request",
            "->",
            "layout",
            ".",
            "as",
            ".",
            "fixed_text",
            ".",
            "base_address",
            "!=",
            "LD_PE_TEXT_ADDRESS",
        ),
        ('"CupidLD PE32 requires text address 0x00401000"',),
        image_rva_limit_guard,
        ("ld_put_pe32_dos_header", "(", "output", ")"),
        ("ld_put_pe32_optional_header", "(",),
        ("ld_put_pe32_section_header", "(",),
        (
            "result_out",
            "->",
            "bytes",
            "=",
            "ctool_buffer_view",
            "(",
            "output",
            ")",
            ".",
            "size",
            ";",
        ),
    )
    emitted_count_initialization = (
        "ctool_u32",
        "emitted_section_count",
        "=",
        "0u",
        ";",
    )
    emitted_count_loop = (
        "for",
        "(",
        "index",
        "=",
        "0u",
        ";",
        "index",
        "<",
        "link",
        "->",
        "output_count",
        ";",
        "index",
        "++",
        ")",
        "{",
        "if",
        "(",
        "link",
        "->",
        "outputs",
        "[",
        "index",
        "]",
        ".",
        "size",
        "!=",
        "0u",
        ")",
        "{",
        "emitted_section_count",
        "++",
        ";",
        "}",
        "}",
    )
    emitted_count_guard = (
        "if",
        "(",
        "emitted_section_count",
        "==",
        "0u",
        "||",
        "emitted_section_count",
        ">",
        "5u",
        ")",
    )
    emitted_header_overflow = (
        "ld_multiply_overflows",
        "(",
        "emitted_section_count",
        ",",
        "LD_PE_SECTION_HEADER_SIZE",
        ")",
        "==",
        "CTOOL_TRUE",
    )
    emitted_header_extent = (
        "headers_end",
        "=",
        "LD_PE_DOS_HEADER_SIZE",
        "+",
        "LD_PE_SIGNATURE_SIZE",
        "+",
        "LD_PE_COFF_HEADER_SIZE",
        "+",
        "LD_PE_OPTIONAL_HEADER_SIZE",
        "+",
        "emitted_section_count",
        "*",
        "LD_PE_SECTION_HEADER_SIZE",
        ";",
    )
    zero_size_layout_skip = (
        "if",
        "(",
        "section",
        "->",
        "size",
        "==",
        "0u",
        ")",
        "{",
        "if",
        "(",
        "section",
        "->",
        "file_size",
        "!=",
        "0u",
        ")",
        "{",
        "status",
        "=",
        "CTOOL_ERR_INTERNAL",
        ";",
        "goto",
        "done",
        ";",
        "}",
        "section",
        "->",
        "file_offset",
        "=",
        "0u",
        ";",
        "continue",
        ";",
        "}",
    )
    emitted_overlap_check = (
        "if",
        "(",
        "have_previous_section",
        "==",
        "CTOOL_TRUE",
        "&&",
        "section",
        "->",
        "address",
        "<",
        "previous_section_end",
        ")",
        "{",
        "status",
        "=",
        "CTOOL_ERR_INPUT",
        ";",
        "goto",
        "done",
        ";",
        "}",
        "previous_section_end",
        "=",
        "end",
        ";",
        "have_previous_section",
        "=",
        "CTOOL_TRUE",
        ";",
    )
    emitted_coff_count = (
        "ctool_buffer_put_le16",
        "(",
        "output",
        ",",
        "(",
        "ctool_u16",
        ")",
        "emitted_section_count",
        ")",
    )
    zero_size_header_skip = (
        "if",
        "(",
        "section",
        "->",
        "size",
        "==",
        "0u",
        ")",
        "{",
        "continue",
        ";",
        "}",
    )
    emitted_result_count = (
        "result_out",
        "->",
        "output_section_count",
        "=",
        "emitted_section_count",
        ";",
    )
    emitted_section_requirements = (
        emitted_count_initialization,
        emitted_count_loop,
        emitted_count_guard,
        emitted_header_overflow,
        emitted_header_extent,
        zero_size_layout_skip,
        emitted_overlap_check,
        emitted_coff_count,
        zero_size_header_skip,
        emitted_result_count,
    )
    request_validation = (
        "request",
        "->",
        "image_kind",
        "!=",
        "CTOOL_LD_IMAGE_ELF32",
        "&&",
        "request",
        "->",
        "image_kind",
        "!=",
        "CTOOL_LD_IMAGE_PE32_FIXED",
    )
    serializer_dispatch = (
        "if",
        "(",
        "request",
        "->",
        "image_kind",
        "==",
        "CTOOL_LD_IMAGE_PE32_FIXED",
        ")",
        "{",
        "status",
        "=",
        "ld_serialize_pe32_fixed",
        "(",
        "&",
        "link",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
        ";",
        "}",
        "else",
        "{",
        "status",
        "=",
        "ld_serialize_elf32_exec",
        "(",
        "&",
        "link",
        ",",
        "output",
        ",",
        "&",
        "result",
        ")",
        ";",
        "}",
    )
    serialization_guard = (
        "if",
        "(",
        "status",
        "==",
        "CTOOL_OK",
        ")",
        "{",
        "phase",
        "=",
        '"CupidLD executable serialization failed"',
        ";",
        *serializer_dispatch,
        "}",
    )
    core_identifiers = re.findall(
        r"\b[A-Za-z_][A-Za-z0-9_]*\b", _mask_c_noncode(linker_core_active)
    )
    serializer_is_exact = serializer_tokens is not None and all(
        len(sequence_positions(serializer_tokens, requirement)) == 1
        for requirement in serializer_requirements
    )
    emitted_section_positions = (
        [
            sequence_positions(serializer_tokens, requirement)
            for requirement in emitted_section_requirements
        ]
        if serializer_tokens is not None
        else []
    )
    emitted_section_order = (
        [positions[0] for positions in emitted_section_positions]
        if len(emitted_section_positions) == len(emitted_section_requirements)
        and all(len(positions) == 1 for positions in emitted_section_positions)
        else []
    )
    emitted_sections_are_exact = (
        serializer_tokens is not None
        and emitted_section_order == sorted(emitted_section_order)
        and len(emitted_section_order) == len(emitted_section_requirements)
        and [
            brace_depth(serializer_tokens, position)
            for position in emitted_section_order
        ]
        == [0, 0, 0, 0, 0, 1, 1, 1, 1, 1]
        and serializer_tokens.count("emitted_section_count") == 9
        and len(
            sequence_positions(
                serializer_tokens,
                (
                    "emitted_section_count",
                    "*",
                    "LD_PE_SECTION_HEADER_SIZE",
                ),
            )
        )
        == 2
        and len(
            sequence_positions(
                serializer_tokens,
                ("result_out", "->", "output_section_count"),
            )
        )
        == 1
    )
    validation_positions = (
        sequence_positions(linker_tokens, request_validation)
        if linker_tokens is not None
        else []
    )
    serialization_positions = (
        sequence_positions(linker_tokens, serialization_guard)
        if linker_tokens is not None
        else []
    )
    dispatch_is_exact = (
        linker_tokens is not None
        and len(validation_positions) == 1
        and brace_depth(linker_tokens, validation_positions[0]) == 0
        and len(serialization_positions) == 1
        and brace_depth(linker_tokens, serialization_positions[0]) == 0
        and core_identifiers.count("ld_serialize_pe32_fixed") == 2
        and core_identifiers.count("CTOOL_LD_IMAGE_PE32_FIXED") == 3
    )
    if (
        not serializer_is_exact
        or not emitted_sections_are_exact
        or not dispatch_is_exact
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 source contract differs: "
            "the fixed serializer, emitted sections, or dispatch differ"
        )

    import_builder_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_prepare_pe32_imports\s*"
        r"\([^;{}]*\)\s*\{",
    )
    import_compare_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_i32\s+ld_pe32_import_compare\s*"
        r"\([^;{}]*\)\s*\{",
    )
    relocation_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_apply_relocations\s*"
        r"\([^;{}]*\)\s*\{",
    )
    optional_header_tokens = c_function_tokens(
        linker_core_active,
        linker_core_path,
        r"\bstatic\s+ctool_status_t\s+ld_put_pe32_optional_header\s*"
        r"\([^;{}]*\)\s*\{",
    )
    import_pipeline = (
        (
            "status",
            "=",
            "ld_prepare_pe32_imports",
            "(",
            "&",
            "link",
            ")",
            ";",
        ),
        (
            "status",
            "=",
            "ld_finalize_globals",
            "(",
            "&",
            "link",
            ")",
            ";",
        ),
        (
            "status",
            "=",
            "ld_apply_relocations",
            "(",
            "&",
            "link",
            ")",
            ";",
        ),
    )
    import_pipeline_positions = (
        [sequence_positions(linker_tokens, step) for step in import_pipeline]
        if linker_tokens is not None
        else []
    )
    import_pipeline_order = (
        [positions[0] for positions in import_pipeline_positions]
        if len(import_pipeline_positions) == len(import_pipeline)
        and all(len(positions) == 1 for positions in import_pipeline_positions)
        else []
    )
    import_sort_call = (
        "ld_pe32_import_sort",
        "(",
        "link",
        "->",
        "pe32_imports",
        ",",
        "import_count",
        ")",
        ";",
    )
    import_sort_positions = (
        sequence_positions(import_builder_tokens, import_sort_call)
        if import_builder_tokens is not None
        else []
    )
    import_duplicate_guard = (
        "if",
        "(",
        "global",
        "->",
        "import_selected",
        "==",
        "CTOOL_TRUE",
        ")",
        "{",
        "return",
        "ld_pe32_import_error",
        "(",
        "link",
        ",",
        "index",
        ",",
        '"CupidLD PE32 imports contain the same IAT symbol twice"',
        ")",
        ";",
        "}",
    )
    import_duplicate_guard_positions = (
        sequence_positions(import_builder_tokens, import_duplicate_guard)
        if import_builder_tokens is not None
        else []
    )
    import_selected_assignment = (
        "global",
        "->",
        "import_selected",
        "=",
        "CTOOL_TRUE",
        ";",
    )
    import_selected_assignment_positions = (
        sequence_positions(import_builder_tokens, import_selected_assignment)
        if import_builder_tokens is not None
        else []
    )
    name_rva_limit_definition = (
        "#",
        "define",
        "LD_PE_NAME_RVA_LIMIT",
        "0x80000000u",
    )
    name_rva_limit_definition_positions = sequence_positions(
        linker_core_preprocessing_tokens, name_rva_limit_definition
    )
    import_table_rva_guard = (
        "if",
        "(",
        "address",
        "<",
        "LD_PE_IMAGE_BASE",
        "||",
        "address",
        "-",
        "LD_PE_IMAGE_BASE",
        ">=",
        "LD_PE_NAME_RVA_LIMIT",
        "||",
        "import_payload_size",
        ">",
        "LD_PE_NAME_RVA_LIMIT",
        "-",
        "(",
        "address",
        "-",
        "LD_PE_IMAGE_BASE",
        ")",
        ")",
        "{",
        "return",
        "ld_pe32_import_error",
        "(",
        "link",
        ",",
        "0u",
        ",",
        '"CupidLD PE32 import table exceeds the name RVA range"',
        ")",
        ";",
        "}",
    )
    import_table_rva_guard_positions = (
        sequence_positions(import_builder_tokens, import_table_rva_guard)
        if import_builder_tokens is not None
        else []
    )
    import_thunk_rva_guard = (
        "if",
        "(",
        "hint_rva",
        ">=",
        "LD_PE_NAME_RVA_LIMIT",
        ")",
        "{",
        "return",
        "ld_pe32_import_error",
        "(",
        "link",
        ",",
        "import_index",
        ",",
        '"CupidLD PE32 import thunk has the ordinal flag set"',
        ")",
        ";",
        "}",
    )
    import_thunk_rva_guard_positions = (
        sequence_positions(import_builder_tokens, import_thunk_rva_guard)
        if import_builder_tokens is not None
        else []
    )
    required_core_import_fragments = (
        "request->pe32_import_count != 0u &&",
        "request->pe32_imports == "
        "(const ctool_ld_pe32_import_t *)0",
        "CupidLD imports require the fixed PE32 image profile",
        'ld_begin_output(link, ctool_string(".idata"), address,',
        "section->flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;",
        "global->rank = LD_DEFINITION_IMPORT;",
        "global->value = address + import->iat_offset;",
        "link->pe32_import_directory_size = descriptor_size;",
        "link->pe32_iat_directory_rva =",
        "link->globals[global_index].rank == LD_DEFINITION_IMPORT &&",
        "relocation->type != CTOOL_ELF32_R_386_32 ||",
        "relocation->addend != 0",
        "CupidLD IAT symbols require an absolute zero-addend relocation",
        "if (directory == LD_PE_IMPORT_DIRECTORY)",
        "else if (directory == LD_PE_IAT_DIRECTORY)",
        "result_out->imported_symbol_count = link->pe32_import_count;",
        "result_out->imported_library_count = link->pe32_library_count;",
        "CupidLD PE32 imports use inconsistent library spelling",
        "CupidLD PE32 imports contain the same IAT symbol twice",
        "CupidLD PE32 imports contain the same procedure twice",
    )
    missing_core_import_fragments = [
        fragment
        for fragment in required_core_import_fragments
        if linker_core_active.count(fragment) < 1
    ]
    if (
        import_builder_tokens is None
        or import_compare_tokens is None
        or relocation_tokens is None
        or optional_header_tokens is None
        or missing_core_import_fragments
        or len(import_sort_positions) != 1
        or len(import_duplicate_guard_positions) != 1
        or len(import_selected_assignment_positions) != 1
        or len(name_rva_limit_definition_positions) != 1
        or len(import_table_rva_guard_positions) != 1
        or len(import_thunk_rva_guard_positions) != 1
        or import_duplicate_guard_positions[0]
        >= import_selected_assignment_positions[0]
        or import_pipeline_order != sorted(import_pipeline_order)
        or len(import_pipeline_order) != len(import_pipeline)
        or any(
            brace_depth(linker_tokens, position) != 1
            for position in import_pipeline_order
        )
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 import contract differs: "
            "canonical construction, relocation safety, or staged dispatch "
            "is absent"
        )

    assignments: dict[str, object] = {}
    for node in test_tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            continue
        if target.id not in {
            "CUPIDC_FIXED_POINT_SOURCES",
            "CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS",
            "CUPIDC_FIXED_POINT_LINK_ORDER",
            "CUPID_TOOLCHAIN_FIXED_POINT_SOURCES",
            "CUPID_TOOLCHAIN_FIXED_POINT_LINKS",
        }:
            continue
        try:
            assignments[target.id] = ast.literal_eval(node.value)
        except (TypeError, ValueError) as exc:
            raise AuditError(
                "Cupid Toolchain fixed-point manifest is not literal: "
                f"{target.id}"
            ) from exc

    expected_compiler_sources = (
        ("ctool", "/toolchain/ctool.cc", False),
        ("ctool_host", "/toolchain/ctool_host.cc", False),
        ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
        ("cupidc_type", "/toolchain/cupidc_type.cc", False),
        ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
        ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
        ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
        ("elf32", "/toolchain/elf32.cc", False),
        ("x86", "/toolchain/x86.cc", False),
        ("cupidc_main", "/toolchain/cupidc_main.cc", False),
        (
            "runtime",
            "/toolchain/hosted/i386-linux/runtime.cc",
            True,
        ),
    )
    expected_toolchain_sources = (
        (
            "runtime",
            "/toolchain/hosted/i386-linux/runtime.cc",
            True,
        ),
        ("ctool", "/toolchain/ctool.cc", False),
        ("ctool_host", "/toolchain/ctool_host.cc", False),
        ("elf32", "/toolchain/elf32.cc", False),
        ("x86", "/toolchain/x86.cc", False),
        ("cupidasm", "/toolchain/cupidasm.cc", False),
        ("cupidasm_main", "/toolchain/cupidasm_main.cc", False),
        ("cupiddis", "/toolchain/cupiddis.cc", False),
        ("cupiddis_main", "/toolchain/cupiddis_main.cc", False),
        ("cupidobj", "/toolchain/cupidobj.cc", False),
        ("cupidobj_main", "/toolchain/cupidobj_main.cc", False),
        ("cupidld", "/toolchain/cupidld.cc", False),
        ("cupidld_main", "/toolchain/cupidld_main.cc", False),
        ("cupidc_pp", "/toolchain/cupidc_pp.cc", False),
        ("cupidc_type", "/toolchain/cupidc_type.cc", False),
        ("cupidc_frontend", "/toolchain/cupidc_frontend.cc", False),
        ("cupidc_ir", "/toolchain/cupidc_ir.cc", False),
        ("cupidc_emit", "/toolchain/cupidc_emit.cc", False),
        ("cupidc_main", "/toolchain/cupidc_main.cc", False),
    )
    expected_include_arguments = (
        "-I",
        "/toolchain",
        "--include-angle",
        "/toolchain/hosted/i386-linux/include",
    )
    expected_link_order = (
        "start",
        "cupidc_main",
        "cupidc_emit",
        "cupidc_ir",
        "cupidc_frontend",
        "cupidc_type",
        "cupidc_pp",
        "ctool_host",
        "ctool",
        "elf32",
        "x86",
        "runtime",
    )
    expected_toolchain_links = (
        (
            "cupidasm",
            (
                "start",
                "cupidasm_main",
                "cupidasm",
                "ctool_host",
                "ctool",
                "elf32",
                "x86",
                "runtime",
            ),
        ),
        (
            "cupiddis",
            (
                "start",
                "cupiddis_main",
                "cupiddis",
                "ctool_host",
                "ctool",
                "elf32",
                "x86",
                "runtime",
            ),
        ),
        (
            "cupidld",
            (
                "start",
                "cupidld_main",
                "cupidld",
                "ctool_host",
                "ctool",
                "elf32",
                "runtime",
            ),
        ),
        (
            "cupidobj",
            (
                "start",
                "cupidobj_main",
                "cupidobj",
                "ctool_host",
                "ctool",
                "elf32",
                "runtime",
            ),
        ),
        ("cupidc", expected_link_order),
    )
    expected_assignments = {
        "CUPIDC_FIXED_POINT_SOURCES": expected_compiler_sources,
        "CUPIDC_FIXED_POINT_INCLUDE_ARGUMENTS":
            expected_include_arguments,
        "CUPIDC_FIXED_POINT_LINK_ORDER": expected_link_order,
        "CUPID_TOOLCHAIN_FIXED_POINT_SOURCES":
            expected_toolchain_sources,
        "CUPID_TOOLCHAIN_FIXED_POINT_LINKS":
            expected_toolchain_links,
    }
    for name, expected in expected_assignments.items():
        if assignments.get(name) != expected:
            raise AuditError(
                "Cupid Toolchain fixed-point manifest differs: "
                f"{name}"
            )

    required_driver_fragments = (
        "[--include-angle PATH]",
        "[-include FILE]",
        'if (strcmp(argument, "--include-angle") == 0)',
        "cli->include_forms[cli->include_count] = "
        "CTOOL_C_PP_INCLUDE_ANGLE;",
        '"-include", &value);',
        "context->include_roots[index].forms = "
        "context->include_forms[index];",
        "pp_request.forced_includes = context->forced_includes;",
        "pp_request.forced_include_count = context->forced_include_count;",
        "cupidc: --root requires logical include paths",
        "cupidc: --root requires logical forced include paths",
    )
    missing_driver_fragments = [
        fragment
        for fragment in required_driver_fragments
        if driver_source.count(fragment) != 1
    ]
    if missing_driver_fragments:
        raise AuditError(
            "Cupid Toolchain fixed-point driver does not retain its exact "
            f"include contract: {missing_driver_fragments!r}"
        )
    if (
        driver_source.count(
            "CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE"
        )
        != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point driver does not retain one "
            "quoted-and-angle -I form"
        )

    required_test_fragments = (
        "with ThreadPoolExecutor(max_workers=2) as executor:",
        'producers["cupidc"], arguments, timeout=900',
        "assembled = self.run_cupid_linux_tool(\n"
        '                    producers["cupidasm"],',
        "linked_stage = self.run_cupid_linux_tool(\n"
        '                        producers["cupidld"],',
        "stage_two_objects, stage_two_tools = build_stage(\n"
        '                generation_one_producers, "stage-two"\n'
        "            )",
        "stage_three_objects, stage_three_tools = build_stage(\n"
        '                stage_two_producers, "stage-three"\n'
        "            )",
        'stage_producers["stage-three"][producer_name],',
        "stage_three_objects[name].read_bytes(),\n"
        "                    stage_two_objects[name].read_bytes(),",
        'stage_three_objects["start"].read_bytes(),\n'
        '                stage_two_objects["start"].read_bytes(),',
        "stage_two_tools[tool_name].read_bytes(),\n"
        "                    generation_one_tool.read_bytes(),",
        "stage_three_tools[tool_name].read_bytes(),\n"
        "                    stage_two_tools[tool_name].read_bytes(),",
        "def run_stage_pair(",
        "stage_two_run = self.run_cupid_linux_tool(\n"
        '                    stage_two_tools[tool_name],',
        "stage_three_run = self.run_cupid_linux_tool(\n"
        '                    stage_three_tools[tool_name],',
        "stage_three_run.returncode,\n"
        "                    stage_two_run.returncode,",
        "stage_three_run.stdout,\n"
        "                    stage_two_run.stdout,",
        "stage_three_run.stderr,\n"
        "                    stage_two_run.stderr,",
        "for tool_name in generation_one_tools:\n"
        "                stage_two_help, _stage_three_help = run_stage_pair(\n"
        '                    tool_name, ["--help"]\n'
        "                )",
        "stage_three_valid.read_bytes(),\n"
        "                stage_two_valid.read_bytes(),",
        "stage_three_invalid_run.stderr,\n"
        "                stage_two_invalid_run.stderr,",
        "stage_three_failure.read_bytes(), failure_sentinel",
        "stage_two_assembly, _stage_three_assembly = run_stage_pair(",
        "stage_two_report, _stage_three_report = run_stage_pair(",
        "stage_two_nm, _stage_three_nm = run_stage_pair(",
        "stage_two_wrap, _stage_three_wrap = run_stage_pair(",
        "stage_two_text_wrap, _stage_three_text_wrap = run_stage_pair(",
        "stage_two_flat_run, _stage_three_flat_run = run_stage_pair(",
        "stage_two_ksyms_run, _stage_three_ksyms_run = run_stage_pair(",
        "invalid_ksyms_run, _invalid_ksyms_stage_three = run_stage_pair(",
        '"address is outside i386", invalid_ksyms_run.stderr',
        "stage_two_link, _stage_three_link = run_stage_pair(",
        "stage_two_script_link, _stage_three_script_link = run_stage_pair(",
        "invalid_asm_run, _invalid_asm_stage_three = run_stage_pair(",
        '"unknown Cupid ASM instruction mnemonic",\n'
        "                invalid_asm_run.stderr,",
        "missing_dis_run, _missing_dis_stage_three = run_stage_pair(",
        '"cupiddis: cannot load ", missing_dis_run.stderr',
        "malformed_dis_run, _malformed_dis_stage_three = run_stage_pair(",
        "malformed_link_run, _malformed_link_stage_three = run_stage_pair(",
        "missing_obj_run, _missing_obj_stage_three = run_stage_pair(",
        '"cupidobj: cannot load ", missing_obj_run.stderr',
    )
    missing_test_fragments = [
        fragment
        for fragment in required_test_fragments
        if test_source.count(fragment) != 1
    ]
    if missing_test_fragments:
        raise AuditError(
            "Cupid Toolchain fixed-point staged comparison differs: "
            f"{missing_test_fragments!r}"
        )

    behavior_functions = [
        node
        for node in bootstrap_tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "_run_behavior_checks"
    ]
    expected_behavior_matrix = {
        "failure_cases": 21,
        "help_cases": 5,
        "success_cases": 22,
    }
    expected_profile_failures = {
        "truncated": "snapshot is truncated",
        "unsafe-path": "repository path is invalid",
        "case-collision": "header path has a case collision",
    }
    if len(behavior_functions) != 1:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "_run_behavior_checks is not unique"
        )
    behavior_function = behavior_functions[0]
    linked_code_policy_helper_names = (
        "_check_relocatable_local_target_behavior",
        "_check_executable_local_target_behavior",
        "_check_executable_code_anchor_behavior",
    )
    linked_code_policy_helper_functions = {
        name: [
            node
            for node in bootstrap_tree.body
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            and node.name == name
        ]
        for name in linked_code_policy_helper_names
    }
    if any(
        len(functions) != 1
        for functions in linked_code_policy_helper_functions.values()
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point linked-code policy helpers "
            "differ: all three helpers must be unique"
        )

    def live_linked_code_policy_call_count(
        function: ast.FunctionDef | ast.AsyncFunctionDef,
        helper_name: str,
    ) -> int:
        parents = {
            child: parent
            for parent in ast.walk(function)
            for child in ast.iter_child_nodes(parent)
        }
        return sum(
            1
            for node in ast.walk(function)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == helper_name
            and not _ast_node_is_statically_dead(node, function, parents)
        )

    if any(
        live_linked_code_policy_call_count(behavior_function, helper_name) != 1
        for helper_name in linked_code_policy_helper_names
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point linked-code policy calls "
            "differ: _run_behavior_checks must call all three helpers once"
        )
    behavior_source = (
        ast.get_source_segment(bootstrap_source, behavior_function) or ""
    )
    positive_profile_result: tuple[str, int] | None = None
    positive_profile_status: tuple[str, int, int] | None = None
    profile_failure_matrix: dict[str, str] | None = None
    profile_failure_loop: ast.For | None = None
    behavior_returns: list[object] = []
    for index, statement in enumerate(behavior_function.body):
        if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
            target = statement.targets[0]
            call = statement.value
            if (
                isinstance(target, ast.Name)
                and isinstance(call, ast.Call)
                and isinstance(call.func, ast.Name)
                and call.func.id == "_run_stage_pair"
            ):
                string_arguments = [
                    node.value
                    for node in ast.walk(call)
                    if isinstance(node, ast.Constant)
                    and isinstance(node.value, str)
                ]
                if "profile-manifest" in string_arguments:
                    if (
                        string_arguments.count("profile-manifest") != 2
                        or string_arguments.count("cupidobj") != 1
                    ):
                        raise AuditError(
                            "Cupid Toolchain fixed-point profile behavior "
                            "differs: a profile case does not run both stages"
                        )
                    if positive_profile_result is not None:
                        raise AuditError(
                            "Cupid Toolchain fixed-point profile behavior "
                            "differs: the positive stage pair is not unique"
                        )
                    positive_profile_result = (target.id, index)
            if (
                isinstance(target, ast.Name)
                and target.id == "profile_failure_cases"
            ):
                if not isinstance(statement.value, (ast.Tuple, ast.List)):
                    profile_failure_matrix = None
                    continue
                parsed_failures: dict[str, str] = {}
                for entry in statement.value.elts:
                    if not isinstance(entry, (ast.Tuple, ast.List)):
                        parsed_failures = {}
                        break
                    if len(entry.elts) != 3:
                        parsed_failures = {}
                        break
                    name = entry.elts[0]
                    diagnostic = entry.elts[2]
                    if (
                        not isinstance(name, ast.Constant)
                        or not isinstance(name.value, str)
                        or not isinstance(diagnostic, ast.Constant)
                        or not isinstance(diagnostic.value, str)
                        or name.value in parsed_failures
                    ):
                        parsed_failures = {}
                        break
                    parsed_failures[name.value] = diagnostic.value
                profile_failure_matrix = parsed_failures
        if (
            isinstance(statement, ast.Expr)
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_expect_status"
            and len(statement.value.args) >= 3
        ):
            result, status, label = statement.value.args[:3]
            if (
                isinstance(result, ast.Name)
                and isinstance(status, ast.Constant)
                and isinstance(status.value, int)
                and isinstance(label, ast.Constant)
                and isinstance(label.value, str)
                and label.value == "CupidObj profile manifest"
            ):
                positive_profile_status = (
                    result.id,
                    status.value,
                    index,
                )
        if (
            isinstance(statement, ast.For)
            and isinstance(statement.iter, ast.Name)
            and statement.iter.id == "profile_failure_cases"
        ):
            if profile_failure_loop is not None:
                raise AuditError(
                    "Cupid Toolchain fixed-point profile behavior differs: "
                    "the profile failure loop is not unique"
                )
            profile_failure_loop = statement
        if isinstance(statement, ast.Return):
            try:
                behavior_returns.append(ast.literal_eval(statement.value))
            except (TypeError, ValueError):
                behavior_returns.append(None)

    if behavior_returns != [expected_behavior_matrix]:
        raise AuditError(
            "Cupid Toolchain fixed-point behavior matrix differs: "
            f"expected {expected_behavior_matrix!r}"
        )
    if profile_failure_matrix != expected_profile_failures:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the profile failure matrix is incomplete"
        )
    if positive_profile_result is None or positive_profile_status is None:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the positive profile stage pair is absent"
        )
    result_name, stage_pair_index = positive_profile_result
    status_result, positive_status, status_index = positive_profile_status
    positive_checks = [
        statement
        for statement in behavior_function.body[status_index + 1:]
        if isinstance(statement, ast.If)
    ]
    positive_read_names = (
        {
            node.func.value.id
            for node in ast.walk(positive_checks[0].test)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "read_bytes"
            and isinstance(node.func.value, ast.Name)
        }
        if positive_checks
        else set()
    )
    if (
        status_result != result_name
        or positive_status != 0
        or stage_pair_index >= status_index
        or not positive_checks
        or len(positive_read_names) < 2
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the positive case does not compare both staged outputs"
        )
    if profile_failure_loop is None:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the profile failure loop is absent"
        )
    loop_target_names = [
        node.id
        for node in (
            profile_failure_loop.target.elts
            if isinstance(profile_failure_loop.target, (ast.Tuple, ast.List))
            else []
        )
        if isinstance(node, ast.Name)
    ]
    failure_stage_pairs: list[tuple[str, ast.Call]] = []
    failure_statuses: list[tuple[str, int]] = []
    failure_checks = [
        node for node in profile_failure_loop.body if isinstance(node, ast.If)
    ]
    for node in ast.walk(profile_failure_loop):
        if (
            isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and isinstance(node.value, ast.Call)
            and isinstance(node.value.func, ast.Name)
            and node.value.func.id == "_run_stage_pair"
        ):
            string_arguments = [
                child.value
                for child in ast.walk(node.value)
                if isinstance(child, ast.Constant)
                and isinstance(child.value, str)
            ]
            if "profile-manifest" in string_arguments:
                if (
                    string_arguments.count("profile-manifest") != 2
                    or string_arguments.count("cupidobj") != 1
                ):
                    raise AuditError(
                        "Cupid Toolchain fixed-point profile behavior differs: "
                        "the failure loop does not run both stages"
                    )
                failure_stage_pairs.append(
                    (node.targets[0].id, node.value)
                )
        if (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_expect_status"
            and len(node.args) >= 2
            and isinstance(node.args[0], ast.Name)
            and isinstance(node.args[1], ast.Constant)
            and isinstance(node.args[1].value, int)
        ):
            failure_statuses.append((node.args[0].id, node.args[1].value))
    if (
        len(loop_target_names) != 3
        or len(failure_stage_pairs) != 1
        or failure_statuses != [(failure_stage_pairs[0][0], 1)]
        or len(failure_checks) != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the failure cases are not one checked stage pair"
        )
    failure_check = failure_checks[0].test
    read_names = {
        node.func.value.id
        for node in ast.walk(failure_check)
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "read_bytes"
        and isinstance(node.func.value, ast.Name)
    }
    sentinel_count = sum(
        1
        for node in ast.walk(failure_check)
        if isinstance(node, ast.Name) and node.id == "sentinel"
    )
    diagnostic_count = sum(
        1
        for node in ast.walk(failure_check)
        if isinstance(node, ast.Name) and node.id == loop_target_names[2]
    )
    if len(read_names) < 2 or sentinel_count < 2 or diagnostic_count < 1:
        raise AuditError(
            "Cupid Toolchain fixed-point profile behavior differs: "
            "the failure loop does not diagnose and preserve both outputs"
        )

    def named_stage_pair(name: str) -> tuple[int, ast.Call] | None:
        matches = [
            (index, statement.value)
            for index, statement in enumerate(behavior_function.body)
            if isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == name
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_run_stage_pair"
        ]
        return matches[0] if len(matches) == 1 else None

    def named_status(name: str, expected: int, label: str) -> int | None:
        matches = [
            index
            for index, statement in enumerate(behavior_function.body)
            if isinstance(statement, ast.Expr)
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_expect_status"
            and len(statement.value.args) == 3
            and not statement.value.keywords
            and isinstance(statement.value.args[0], ast.Name)
            and statement.value.args[0].id == name
            and isinstance(statement.value.args[1], ast.Constant)
            and statement.value.args[1].value == expected
            and isinstance(statement.value.args[2], ast.Constant)
            and statement.value.args[2].value == label
        ]
        return matches[0] if len(matches) == 1 else None

    def command_shape(command: ast.expr) -> tuple[object, ...] | None:
        if not isinstance(command, (ast.List, ast.Tuple)):
            return None
        tokens: list[object] = []
        for token in command.elts:
            if isinstance(token, ast.Constant) and isinstance(token.value, str):
                tokens.append(("literal", token.value))
            elif isinstance(token, ast.Name):
                tokens.append(("name", token.id))
            else:
                return None
        return tuple(tokens)

    def stage_pair_commands(
        call: ast.Call,
    ) -> tuple[tuple[object, ...], tuple[object, ...]] | None:
        if (
            len(call.args) != 6
            or call.keywords
            or not isinstance(call.args[0], ast.Name)
            or call.args[0].id != "runner"
            or not isinstance(call.args[1], ast.Name)
            or call.args[1].id != "stage_two"
            or not isinstance(call.args[2], ast.Name)
            or call.args[2].id != "stage_three"
            or not isinstance(call.args[3], ast.Constant)
            or call.args[3].value != "cupidld"
        ):
            return None
        stage_two_command = command_shape(call.args[4])
        stage_three_command = command_shape(call.args[5])
        if stage_two_command is None or stage_three_command is None:
            return None
        return stage_two_command, stage_three_command

    def expected_pe32_command(
        text_address: str, output: str, link_object: str
    ) -> tuple[object, ...]:
        return (
            ("literal", "-m"),
            ("literal", "i386pe"),
            ("literal", "--text-address"),
            ("literal", text_address),
            ("literal", "--entry"),
            ("literal", "_start"),
            ("literal", "-o"),
            ("name", output),
            ("name", link_object),
        )

    def read_bytes_receiver(node: ast.expr) -> str | None:
        if (
            isinstance(node, ast.Call)
            and not node.args
            and not node.keywords
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "read_bytes"
            and isinstance(node.func.value, ast.Name)
        ):
            return node.func.value.id
        return None

    def result_attributes(test: ast.expr, result_name: str) -> set[str]:
        return {
            node.attr
            for node in ast.walk(test)
            if isinstance(node, ast.Attribute)
            and isinstance(node.value, ast.Name)
            and node.value.id == result_name
        }

    pe32_positive = named_stage_pair("pe32_result")
    pe32_positive_status = named_status(
        "pe32_result", 0, "CupidLD PE32 fixed image"
    )
    pe32_failure = named_stage_pair("invalid_pe32_result")
    pe32_failure_status = named_status(
        "invalid_pe32_result", 1, "CupidLD invalid PE32 text address"
    )
    windows_assembly = named_stage_pair("windows_assembly_result")
    windows_assembly_status = named_status(
        "windows_assembly_result", 0, "CupidASM Windows startup"
    )
    windows_compile = named_stage_pair("windows_compile_result")
    windows_compile_status = named_status(
        "windows_compile_result", 0, "CupidC Windows runtime contract"
    )
    windows_link = named_stage_pair("windows_link_result")
    windows_link_status = named_status(
        "windows_link_result", 0, "CupidLD imported Windows image"
    )
    windows_tool_compile = named_stage_pair(
        "windows_tool_compile_result"
    )
    windows_tool_compile_status = named_status(
        "windows_tool_compile_result", 0, "CupidC Windows tool runtime"
    )
    windows_tool_assembly = named_stage_pair(
        "windows_tool_assembly_result"
    )
    windows_tool_assembly_status = named_status(
        "windows_tool_assembly_result", 0, "CupidASM Windows tool startup"
    )
    windows_host_adapter = named_stage_pair(
        "windows_host_adapter_result"
    )
    windows_host_adapter_status = named_status(
        "windows_host_adapter_result", 0, "CupidC Windows host adapter"
    )
    windows_publication_compile = named_stage_pair(
        "windows_publication_compile_result"
    )
    windows_publication_compile_status = named_status(
        "windows_publication_compile_result",
        0,
        "CupidC Windows publication runtime",
    )
    windows_publication_assembly = named_stage_pair(
        "windows_publication_assembly_result"
    )
    windows_publication_assembly_status = named_status(
        "windows_publication_assembly_result",
        0,
        "CupidASM Windows publication startup",
    )
    windows_cupiddis_link = named_stage_pair(
        "windows_cupiddis_link_result"
    )
    windows_cupiddis_link_status = named_status(
        "windows_cupiddis_link_result", 0, "CupidLD Windows CupidDis"
    )
    windows_runtime_contract_compile = named_stage_pair(
        "windows_runtime_contract_compile_result"
    )
    windows_runtime_contract_compile_status = named_status(
        "windows_runtime_contract_compile_result",
        0,
        "CupidC Windows runtime contract",
    )
    windows_runtime_contract_link = named_stage_pair(
        "windows_runtime_contract_link_result"
    )
    windows_runtime_contract_link_status = named_status(
        "windows_runtime_contract_link_result",
        0,
        "CupidLD Windows runtime contract",
    )
    invalid_import_assembly = named_stage_pair(
        "invalid_import_assembly_result"
    )
    invalid_import_assembly_status = named_status(
        "invalid_import_assembly_result",
        0,
        "CupidASM invalid import fixture",
    )
    invalid_import = named_stage_pair("invalid_import_result")
    invalid_import_status = named_status(
        "invalid_import_result", 1, "CupidLD direct IAT call"
    )
    windows_stage_pairs = (
        (windows_assembly, windows_assembly_status, "cupidasm"),
        (windows_compile, windows_compile_status, "cupidc"),
        (windows_link, windows_link_status, "cupidld"),
        (
            windows_tool_compile,
            windows_tool_compile_status,
            "cupidc",
        ),
        (
            windows_tool_assembly,
            windows_tool_assembly_status,
            "cupidasm",
        ),
        (
            windows_host_adapter,
            windows_host_adapter_status,
            "cupidc",
        ),
        (
            windows_publication_compile,
            windows_publication_compile_status,
            "cupidc",
        ),
        (
            windows_publication_assembly,
            windows_publication_assembly_status,
            "cupidasm",
        ),
        (
            windows_cupiddis_link,
            windows_cupiddis_link_status,
            "cupidld",
        ),
        (
            windows_runtime_contract_compile,
            windows_runtime_contract_compile_status,
            "cupidc",
        ),
        (
            windows_runtime_contract_link,
            windows_runtime_contract_link_status,
            "cupidld",
        ),
        (
            invalid_import_assembly,
            invalid_import_assembly_status,
            "cupidasm",
        ),
        (invalid_import, invalid_import_status, "cupidld"),
    )
    windows_stage_order = [
        pair[0]
        for pair, status, _tool in windows_stage_pairs
        if pair is not None and status is not None and pair[0] < status
    ]
    windows_stage_tools_match = all(
        pair is not None
        and len(pair[1].args) in (6, 7)
        and isinstance(pair[1].args[3], ast.Constant)
        and pair[1].args[3].value == tool
        for pair, _status, tool in windows_stage_pairs
    )
    if (
        pe32_positive is None
        or pe32_positive_status is None
        or pe32_failure is None
        or pe32_failure_status is None
        or pe32_positive[0] >= pe32_positive_status
        or pe32_failure[0] >= pe32_failure_status
        or len(windows_stage_order) != len(windows_stage_pairs)
        or windows_stage_order != sorted(windows_stage_order)
        or not windows_stage_tools_match
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the paired image, Windows runtime, or failure proof is absent"
        )
    def final_command_name(command: ast.expr) -> str | None:
        if (
            isinstance(command, (ast.List, ast.Tuple))
            and command.elts
            and isinstance(command.elts[-1], ast.Name)
        ):
            return command.elts[-1].id
        return None

    invalid_import_assembly_object_names = tuple(
        final_command_name(call.args[argument_index])
        for call in (invalid_import_assembly[1],)
        for argument_index in (4, 5)
        if len(call.args) == 6
    )
    invalid_import_link_object_names = tuple(
        final_command_name(invalid_import[1].args[argument_index])
        for argument_index in (4, 5)
        if len(invalid_import[1].args) == 6
    )
    if (
        invalid_import_assembly_object_names
        != (
            "stage_two_invalid_import_object",
            "stage_three_invalid_import_object",
        )
        or invalid_import_link_object_names
        != ("invalid_import_object", "invalid_import_object")
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the direct-IAT object pair or stable diagnostic path differs"
        )
    positive_commands = stage_pair_commands(pe32_positive[1])
    failure_commands = stage_pair_commands(pe32_failure[1])
    expected_positive_commands = (
        expected_pe32_command(
            "0x00401000", "stage_two_pe32", "stage_two_link_object"
        ),
        expected_pe32_command(
            "0x00401000", "stage_three_pe32", "stage_three_link_object"
        ),
    )
    expected_failure_commands = (
        expected_pe32_command(
            "0x00402000",
            "stage_two_pe32_failure",
            "stage_two_link_object",
        ),
        expected_pe32_command(
            "0x00402000",
            "stage_three_pe32_failure",
            "stage_three_link_object",
        ),
    )
    if (
        positive_commands != expected_positive_commands
        or failure_commands != expected_failure_commands
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the staged commands do not retain the fixed PE32 profile"
        )
    positive_checks = [
        (index, statement)
        for index, statement in enumerate(behavior_function.body)
        if index > pe32_positive_status
        if isinstance(statement, ast.If)
        and any(
            isinstance(node, ast.Name) and node.id == "pe32_result"
            for node in ast.walk(statement.test)
        )
    ]
    positive_byte_comparisons = (
        [
            (
                read_bytes_receiver(node.left),
                read_bytes_receiver(node.comparators[0]),
            )
            for node in ast.walk(positive_checks[0][1].test)
            if isinstance(node, ast.Compare)
            and len(node.ops) == 1
            and isinstance(node.ops[0], ast.NotEq)
            and len(node.comparators) == 1
            and read_bytes_receiver(node.left) is not None
            and read_bytes_receiver(node.comparators[0]) is not None
        ]
        if len(positive_checks) == 1
        else []
    )
    positive_result_attributes = (
        result_attributes(positive_checks[0][1].test, "pe32_result")
        if len(positive_checks) == 1
        else set()
    )
    validators = [
        (index, statement.value)
        for index, statement in enumerate(behavior_function.body)
        if isinstance(statement, ast.Expr)
        and isinstance(statement.value, ast.Call)
        and isinstance(statement.value.func, ast.Name)
        and statement.value.func.id == "_validate_static_i386_pe32"
    ]
    parser_functions = [
        statement
        for statement in bootstrap_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_validate_static_i386_pe32_bytes"
    ]
    parser_wrappers = [
        statement
        for statement in bootstrap_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_validate_static_i386_pe32"
    ]
    parser_source = (
        ast.get_source_segment(bootstrap_source, parser_functions[0]) or ""
        if len(parser_functions) == 1
        else ""
    )
    required_import_parser_fragments = (
        ') = sections[".idata"]',
        "relative_rva = rva - idata_virtual_address",
        "size <= idata_raw_size - relative_rva",
        "size <= idata_virtual_size - relative_rva",
        "idata_raw_offset + relative_rva",
        "idata_raw_offset\n"
        "                    + min(idata_raw_size, idata_virtual_size)",
    )
    import_parser_is_confined = all(
        parser_source.count(fragment) == 1
        for fragment in required_import_parser_fragments
    ) and "for section in sections.values()" not in parser_source

    def expression_shape(source: str) -> str:
        return _stable_ast_shape(ast.parse(source, mode="eval").body)

    def node_shape(node: ast.AST) -> str:
        return _stable_ast_shape(node)

    def node_fingerprint(node: ast.AST) -> str:
        return _stable_ast_fingerprint(node)

    def named_top_level_guards(result_name: str) -> list[ast.If]:
        return [
            statement
            for statement in behavior_function.body
            if isinstance(statement, ast.If)
            and any(
                isinstance(node, ast.Name) and node.id == result_name
                for node in ast.walk(statement.test)
            )
        ]

    expected_windows_publication_assignment_fingerprints = {
        "windows_cupidld_imports": (
            "85333e6ef0dfa77ed7e1702ae797b7a7cd0fc792932728a13763720155fb6e7d"
        ),
        "windows_native_tool_imports": (
            "8a35ff3c7fa095c80c59889ffd61235444bdf37bbc5f19d75b0845105ff9cfec"
        ),
        "windows_native_stage_two_extras": (
            "a1d98cbc8f08a7a48e0bda82ba1cf3b4a69ebbcd7b36d4bf04e6dd71ca86595b"
        ),
        "windows_native_stage_three_extras": (
            "c54f05df4bc30c1664a0eaadd42479e756771a49eabd352245ed779b87d0f3f8"
        ),
    }
    windows_publication_assignments_match = all(
        [
            node_fingerprint(statement)
            for statement in behavior_function.body
            if isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == name
        ]
        == [expected]
        for name, expected
        in expected_windows_publication_assignment_fingerprints.items()
    )

    publication_compile_guards = named_top_level_guards(
        "windows_publication_compile_result"
    )
    publication_assembly_guards = named_top_level_guards(
        "windows_publication_assembly_result"
    )
    windows_publication_stage_shapes_match = (
        windows_publication_compile is not None
        and node_fingerprint(windows_publication_compile[1])
        == "80071204c8881b9db1ce19c5fd36857553d623ff2e5ef642f326902bc61c2f7a"
        and windows_publication_assembly is not None
        and node_fingerprint(windows_publication_assembly[1])
        == "54b318306cd4b845fcc15d048bc5ac067212c6e0dd6c489fe270f86aff49ae32"
        and len(publication_compile_guards) == 1
        and node_fingerprint(publication_compile_guards[0])
        == "d254e1a6613eb2f486f79dfbff871f8382803d0479e3837b8352d5b233e9c356"
        and len(publication_assembly_guards) == 1
        and node_fingerprint(publication_assembly_guards[0])
        == "bdc9027b53a956023d2484764e01f2d2f1d98533aed6272107f6f0b91f113c59"
    )

    native_windows_blocks = [
        (index, statement)
        for index, statement in enumerate(behavior_function.body)
        if isinstance(statement, ast.If)
        and node_shape(statement.test) == expression_shape("os.name == 'nt'")
    ]
    expected_native_windows_block_fingerprints = (
        "086d3cc5fd6ea99b8aad00ad20792d4cc6fa3ed30df1d9949b443d0a5e3c9c26",
        "e99702dccec790d0028f4b82eb69065930b102ef6863c8252e1646f8f2b91b01",
        "5cdaf953ccf38b4a6c16e47dbe9da1af531f6ae1d487c4f4ae59353e7209fb3a",
    )
    native_windows_control_flow_matches = (
        len(native_windows_blocks) == 4
        and tuple(
            node_fingerprint(block)
            for _index, block in native_windows_blocks[1:]
        )
        == expected_native_windows_block_fingerprints
    )

    def behavior_guard_terms(
        statements: list[ast.stmt],
        message: str | tuple[str, ...],
    ) -> frozenset[str] | None:
        matches: list[frozenset[str]] = []
        for node in statements:
            if not isinstance(node, ast.If):
                continue
            matched_raise = False
            for statement in node.body:
                if not isinstance(statement, ast.Raise) or statement.exc is None:
                    continue
                constants = tuple(
                    child.value
                    for child in ast.walk(statement.exc)
                    if isinstance(child, ast.Constant)
                    and isinstance(child.value, str)
                )
                if isinstance(message, str):
                    matched_raise = message in constants
                else:
                    matched_raise = all(fragment in constants for fragment in message)
                if matched_raise:
                    break
            if not matched_raise:
                continue
            terms = (
                node.test.values
                if isinstance(node.test, ast.BoolOp)
                and isinstance(node.test.op, ast.Or)
                else (node.test,)
            )
            matches.append(frozenset(node_shape(term) for term in terms))
        return matches[0] if len(matches) == 1 else None

    expected_native_guard_terms = {
        "cupiddis": (
            "reference_disassembly.returncode != 0",
            "reference_disassembly.stderr",
            "native_help.returncode != 0",
            "'usage: cupiddis' not in native_help.stdout",
            "native_help.stderr",
            "native_disassembly.returncode != 0",
            "native_disassembly.stdout != reference_disassembly.stdout",
            "native_disassembly.stderr",
            "reference_valid_target.returncode != 0",
            "reference_valid_target.stdout",
            "reference_valid_target.stderr",
            "native_valid_target.returncode != 0",
            "native_valid_target.stdout",
            "native_valid_target.stderr",
            "reference_invalid_target.returncode != 1",
            "reference_invalid_target.stdout",
            "'1 of 1 direct relative targets invalid' not in "
            "reference_invalid_target.stderr",
            "'1 outside image' not in reference_invalid_target.stderr",
            "native_invalid_target.returncode != 1",
            "native_invalid_target.stdout",
            "'1 of 1 direct relative targets invalid' not in "
            "native_invalid_target.stderr",
            "'1 outside image' not in native_invalid_target.stderr",
            "native_missing.returncode != 1",
            "native_missing.stdout",
            "'cannot load' not in native_missing.stderr",
            "'not_found' not in native_missing.stderr",
        ),
        "runtime_contract": (
            "native_contract.returncode != 0",
            "native_contract.stdout != "
            "'Cupid-built Windows tool runtime: ok\\n'",
            "native_contract.stderr",
            "not contract_output.is_file()",
            "contract_output.read_bytes() != b'headtail'",
            "native_contract_failure.returncode != 41",
            "native_contract_failure.stdout",
            "'windows runtime arguments: bad' not in "
            "native_contract_failure.stderr",
        ),
        "native_tools": (
            "reference_help.returncode != 0",
            "reference_help.stderr",
            "native_help.returncode != 0",
            "native_help.stdout != reference_help.stdout",
            "native_help.stderr",
            "native_success.returncode != 0",
            "native_success.stdout",
            "native_success.stderr",
            "native_output.read_bytes() != reference_output.read_bytes()",
            "native_failure.returncode != 1",
            "native_failure.stdout",
            "failure_diagnostic not in native_failure.stderr",
            "failure_output.read_bytes() != sentinel",
        ),
        "cupidld": (
            "reference_cupidld_help.returncode != 0",
            "reference_cupidld_help.stderr",
            "native_cupidld_help.returncode != 0",
            "native_cupidld_help.stdout != reference_cupidld_help.stdout",
            "native_cupidld_help.stderr",
            "native_cupidld_success.returncode != 0",
            "native_cupidld_success.stdout",
            "native_cupidld_success.stderr",
            "native_cupidld_output.read_bytes() != "
            "stage_two_windows_runtime_contract_image.read_bytes()",
            "occupied_cupidld_candidate.read_bytes() != b'occupied'",
            "remaining_cupidld_candidates != [occupied_cupidld_candidate]",
            "native_cupidld_failure.returncode != 1",
            "native_cupidld_failure.stdout",
            "'cupidld: link failed (io)' not in "
            "native_cupidld_failure.stderr",
            "not blocked_cupidld_output.is_dir()",
            "remaining_blocked_candidates",
        ),
    }
    native_tool_loops = (
        [
            statement
            for statement in native_windows_blocks[1][1].body
            if isinstance(statement, ast.For)
        ]
        if len(native_windows_blocks) == 4
        else []
    )
    native_guards_match = (
        len(native_windows_blocks) == 4
        and len(native_tool_loops) == 1
        and behavior_guard_terms(
            native_windows_blocks[2][1].body,
            "Cupid-built Windows CupidDis behavior differs"
        )
        == frozenset(
            expression_shape(term)
            for term in expected_native_guard_terms["cupiddis"]
        )
        and behavior_guard_terms(
            native_windows_blocks[3][1].body,
            "Cupid-built Windows runtime contract behavior differs"
        )
        == frozenset(
            expression_shape(term)
            for term in expected_native_guard_terms["runtime_contract"]
        )
        and behavior_guard_terms(
            native_tool_loops[0].body,
            ("Cupid-built Windows ", " behavior differs")
        )
        == frozenset(
            expression_shape(term)
            for term in expected_native_guard_terms["native_tools"]
        )
        and behavior_guard_terms(
            native_windows_blocks[1][1].body,
            "Cupid-built Windows CupidLD publication behavior differs",
        )
        == frozenset(
            expression_shape(term)
            for term in expected_native_guard_terms["cupidld"]
        )
    )
    cupiddis_raw_argument_shapes: list[str] = []
    runtime_contract_argument_shapes: list[str] = []
    if len(native_windows_blocks) == 4:
        for node in native_windows_blocks[2][1].body:
            if (
                isinstance(node, ast.AnnAssign)
                and isinstance(node.target, ast.Name)
                and node.target.id == "raw_arguments"
                and node.value is not None
            ):
                cupiddis_raw_argument_shapes.append(node_shape(node.value))
        for node in native_windows_blocks[3][1].body:
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "contract_arguments"
            ):
                runtime_contract_argument_shapes.append(node_shape(node.value))
    native_workloads_match = (
        cupiddis_raw_argument_shapes
        == [expression_shape(
            "['--raw', '--mode', '32', '--base', '0', "
            "windows_cupiddis_input]"
        )]
        and runtime_contract_argument_shapes
        == [expression_shape(
            "['plain', 'space arg', 'quote\"arg', 'trailing\\\\', "
            "str(contract_output), str(contract_missing)]"
        )]
    )

    windows_helper_functions = [
        node
        for node in bootstrap_tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "_build_windows_tool_image"
    ]
    windows_helper_source = (
        ast.get_source_segment(bootstrap_source, windows_helper_functions[0])
        or ""
        if len(windows_helper_functions) == 1
        else ""
    )
    helper_stage_pairs = (
        [
            statement.value
            for statement in windows_helper_functions[0].body
            if isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == "link_result"
            and isinstance(statement.value, ast.Call)
            and isinstance(statement.value.func, ast.Name)
            and statement.value.func.id == "_run_stage_pair"
        ]
        if len(windows_helper_functions) == 1
        else []
    )
    helper_stage_tools = tuple(sorted(
        call.args[3].value
        for call in helper_stage_pairs
        if len(call.args) >= 4
        and isinstance(call.args[3], ast.Constant)
        and isinstance(call.args[3].value, str)
    ))
    helper_status_count = sum(
        1
        for node in windows_helper_functions[0].body
        if isinstance(node, ast.Expr)
        and isinstance(node.value, ast.Call)
        and isinstance(node.value.func, ast.Name)
        and node.value.func.id == "_expect_status"
    ) if len(windows_helper_functions) == 1 else 0
    helper_relocatable_count = sum(
        1
        for loop in windows_helper_functions[0].body
        if isinstance(loop, ast.For)
        for node in loop.body
        if isinstance(node, ast.Expr)
        and isinstance(node.value, ast.Call)
        and isinstance(node.value.func, ast.Name)
        and node.value.func.id == "_validate_i386_relocatable"
    ) if len(windows_helper_functions) == 1 else 0
    helper_pe_count = sum(
        1
        for node in windows_helper_functions[0].body
        if isinstance(node, ast.Expr)
        and isinstance(node.value, ast.Call)
        and isinstance(node.value.func, ast.Name)
        and node.value.func.id == "_validate_static_i386_pe32"
    ) if len(windows_helper_functions) == 1 else 0
    helper_source_values: list[object] = []
    helper_replacement_shapes: list[str] = []
    helper_loop_compile_shapes: list[tuple[str, ...]] = []
    if len(windows_helper_functions) == 1:
        for node in windows_helper_functions[0].body:
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "windows_sources"
            ):
                try:
                    helper_source_values.append(ast.literal_eval(node.value))
                except (TypeError, ValueError):
                    helper_source_values.append(None)
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "replacement_names"
            ):
                helper_replacement_shapes.append(node_shape(node.value))
            if isinstance(node, ast.For):
                compile_calls = [
                    statement.value
                    for statement in node.body
                    if isinstance(statement, ast.Assign)
                    and len(statement.targets) == 1
                    and isinstance(statement.targets[0], ast.Name)
                    and statement.targets[0].id == "compile_result"
                    and isinstance(statement.value, ast.Call)
                    and isinstance(statement.value.func, ast.Name)
                    and statement.value.func.id == "_run_stage_pair"
                ]
                for call in compile_calls:
                    helper_loop_compile_shapes.append(
                        tuple(node_shape(argument) for argument in call.args)
                    )
    native_plan_values: list[object] = []
    native_check_shapes: list[str] = []
    for node in behavior_function.body:
        if (
            isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == "windows_native_tool_plans"
        ):
            try:
                native_plan_values.append(ast.literal_eval(node.value))
            except (TypeError, ValueError):
                native_plan_values.append(None)
    if len(native_windows_blocks) == 4:
        for node in native_windows_blocks[1][1].body:
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "native_checks"
            ):
                native_check_shapes.append(node_shape(node.value))
    native_build_loops = [
        node
        for node in behavior_function.body
        if isinstance(node, ast.For)
        and ast.unparse(node.target) == "(tool_name, link_objects)"
        and node_shape(node.iter)
        == expression_shape("windows_native_tool_plans.items()")
    ]
    helper_invocation_count = (
        sum(
            1
            for node in native_build_loops[0].body
            for call in ast.walk(node)
            if isinstance(call, ast.Call)
            and isinstance(call.func, ast.Name)
            and call.func.id == "_build_windows_tool_image"
        )
        if len(native_build_loops) == 1
        else 0
    )
    native_execution_loop_matches = (
        len(native_tool_loops) == 1
        and ast.unparse(native_tool_loops[0].target)
        == "(tool_name, (success_arguments, failure_arguments, "
        "native_output, failure_output, reference_output, "
        "failure_diagnostic))"
        and node_shape(native_tool_loops[0].iter)
        == expression_shape("native_checks.items()")
    )
    expected_native_plans = {
        "cupidasm": (
            "cupidasm_main", "cupidasm", "ctool_host", "ctool", "elf32",
            "x86",
        ),
        "cupidc": (
            "cupidc_main", "cupidc_emit", "cupidc_ir", "cupidc_frontend",
            "cupidc_type", "cupidc_pp", "ctool_host", "ctool", "elf32",
            "x86",
        ),
        "cupidld": (
            "publication_start", "cupidld_main", "cupidld", "ctool_host",
            "ctool", "elf32", "publication_runtime",
        ),
        "cupidobj": (
            "cupidobj_main", "cupidobj", "ctool_host", "ctool", "elf32",
        ),
    }
    expected_native_checks_shape = expression_shape(
        "{'cupidasm': ("
        "['-f', 'bin', str(assembly_source), '-o', "
        "str(behavior_root / 'native-cupidasm.bin')], "
        "['-f', 'bin', str(windows_invalid_assembly), '-o', "
        "str(behavior_root / 'native-cupidasm-failure.bin')], "
        "behavior_root / 'native-cupidasm.bin', "
        "behavior_root / 'native-cupidasm-failure.bin', stage_two_binary, "
        "'unknown Cupid ASM instruction mnemonic'), "
        "'cupidc': (['-c', str(valid_source), '-o', "
        "str(behavior_root / 'native-cupidc.o')], "
        "['-c', str(invalid_source), '-o', "
        "str(behavior_root / 'native-cupidc-failure.o')], "
        "behavior_root / 'native-cupidc.o', "
        "behavior_root / 'native-cupidc-failure.o', stage_two_valid, "
        "'error CT'), "
        "'cupidobj': (['wrap', str(asset), '--stem', 'fixed_point_asset', "
        "'--section', '.rodata', '--readonly', '-o', "
        "str(behavior_root / 'native-cupidobj.o')], "
        "['wrap', str(missing_native_input), '--stem', "
        "'missing_native_input', '-o', "
        "str(behavior_root / 'native-cupidobj-failure.o')], "
        "behavior_root / 'native-cupidobj.o', "
        "behavior_root / 'native-cupidobj-failure.o', stage_two_wrapped, "
        "'not_found')}"
    )
    helper_compile_loops = (
        [node for node in windows_helper_functions[0].body if isinstance(node, ast.For)]
        if len(windows_helper_functions) == 1
        else []
    )
    expected_helper_compile_loop = ast.parse(
        """for source_name in replacement_names:
    stage_two_object = behavior_root / f"stage-three-windows-{tool_name}-{source_name}.o"
    stage_three_object = behavior_root / f"stage-four-windows-{tool_name}-{source_name}.o"
    compile_result = _run_stage_pair(
        runner, stage_two, stage_three, "cupidc",
        ["--root", source_root, "-D", "_WIN32=1", "-c",
         f"/toolchain/{source_name}.cc", "-I", "/toolchain",
         "--include-angle", "/toolchain/hosted/i386-linux/include",
         "-o", _logical_path(source_root, stage_two_object)],
        ["--root", source_root, "-D", "_WIN32=1", "-c",
         f"/toolchain/{source_name}.cc", "-I", "/toolchain",
         "--include-angle", "/toolchain/hosted/i386-linux/include",
         "-o", _logical_path(source_root, stage_three_object)],
        360,
    )
    _expect_status(compile_result, 0, f"CupidC Windows {tool_name} {source_name}")
    if (compile_result.stdout or compile_result.stderr or
            stage_two_object.read_bytes() != stage_three_object.read_bytes()):
        raise BootstrapError(f"CupidC Windows {tool_name} {source_name} differs")
    _validate_i386_relocatable(stage_two_object)
    stage_two_replacements[source_name] = stage_two_object
    stage_three_replacements[source_name] = stage_three_object
    compile_artifacts[f"stage-three-{source_name}"] = stage_two_object
    compile_artifacts[f"stage-four-{source_name}"] = stage_three_object
"""
    ).body[0]
    helper_link_functions = (
        [
            node
            for node in windows_helper_functions[0].body
            if isinstance(node, ast.FunctionDef) and node.name == "link_arguments"
        ]
        if len(windows_helper_functions) == 1
        else []
    )
    expected_helper_link_function = ast.parse(
        """def link_arguments(image: Path, stage: Stage, start: Path,
                       runtime: Path, replacements: dict[str, Path],
                       selectors: Sequence[str]) -> list[str | Path]:
    arguments: list[str | Path] = [
        "-m", "i386pe", "--text-address", "0x00401000",
        "--entry", "_start",
    ]
    for selector in selectors:
        arguments.extend(("--import", selector))
    arguments.extend(("-o", image, start))
    for name in link_objects:
        arguments.append(replacements[name] if name in replacements
                         else stage.objects[name])
    arguments.append(runtime)
    return arguments
"""
    ).body[0]
    helper_link_result_shapes = [
        node_shape(node)
        for node in windows_helper_functions[0].body
        if isinstance(node, ast.Assign)
        and len(node.targets) == 1
        and isinstance(node.targets[0], ast.Name)
        and node.targets[0].id == "link_result"
    ] if len(windows_helper_functions) == 1 else []
    expected_helper_link_result = ast.parse(
        """link_result = _run_stage_pair(
    runner, stage_two, stage_three, "cupidld",
    link_arguments(stage_two_image, stage_two, stage_two_start,
                   stage_two_runtime, stage_two_replacements,
                   tuple(reversed(import_selectors))),
    link_arguments(stage_three_image, stage_three, stage_three_start,
                   stage_three_runtime, stage_three_replacements,
                   import_selectors),
    180,
)
"""
    ).body[0]
    helper_link_guard_shapes = [
        node_shape(node)
        for node in windows_helper_functions[0].body
        if isinstance(node, ast.If)
        and any(
            isinstance(child, ast.Name) and child.id == "link_result"
            for child in ast.walk(node.test)
        )
    ] if len(windows_helper_functions) == 1 else []
    expected_helper_link_guard = ast.parse(
        """if (link_result.stdout or link_result.stderr or
        stage_two_image.read_bytes() != stage_three_image.read_bytes()):
    raise BootstrapError(f"Cupid-built Windows {tool_name} differs")
"""
    ).body[0]
    expected_native_build_loop = ast.parse(
        """for tool_name, link_objects in windows_native_tool_plans.items():
    stage_two_native_tool, _stage_three_native_tool, artifacts = _build_windows_tool_image(
        runner, source_root, behavior_root, stage_two, stage_three, tool_name,
        link_objects, windows_native_tool_imports[tool_name],
        stage_two_windows_tool_start, stage_three_windows_tool_start,
        stage_two_windows_tool_runtime, stage_three_windows_tool_runtime,
        stage_two_windows_host_adapter, stage_three_windows_host_adapter,
        windows_native_stage_two_extras.get(tool_name, {}),
        windows_native_stage_three_extras.get(tool_name, {}),
    )
    windows_native_tool_images[tool_name] = stage_two_native_tool
    windows_native_tool_artifacts[tool_name] = artifacts
"""
    ).body[0]
    required_windows_helper_fragments = (
        '"cupidasm": ("cupidasm_main",)',
        '"cupidc": ("cupidc_main",)',
        '"cupidld": ("cupidld_main",)',
        '"cupidobj": ("cupidobj_main",)',
        '"_WIN32=1"',
        'f"/toolchain/{source_name}.cc"',
        '"i386pe",\n            "--text-address",\n            "0x00401000",',
        "stage_two_object.read_bytes() != stage_three_object.read_bytes()",
        "tuple(reversed(import_selectors))",
        "stage_two_image.read_bytes() != stage_three_image.read_bytes()",
        "((\"KERNEL32.dll\", tuple(item[2] for item in windows_imports)),)",
        "return stage_two_image, stage_three_image, _artifact_inventory(",
    )
    windows_helper_matches = (
        len(windows_helper_functions) == 1
        and node_fingerprint(windows_helper_functions[0])
        == "a82a769d5cb11f8fbeed7ec6a4915cd0983bb34f636c6ec74516768d6c726aa2"
        and helper_stage_tools == ("cupidld",)
        and helper_status_count == 1
        and helper_relocatable_count == 1
        and helper_pe_count == 1
        and helper_source_values
        == [{
            "cupidasm": ("cupidasm_main",),
            "cupidc": ("cupidc_main",),
            "cupidld": ("cupidld_main",),
            "cupidobj": ("cupidobj_main",),
        }]
        and helper_replacement_shapes
        == [expression_shape("windows_sources.get(tool_name, ())")]
        and helper_loop_compile_shapes
        == [(
            expression_shape("runner"),
            expression_shape("stage_two"),
            expression_shape("stage_three"),
            expression_shape("'cupidc'"),
            expression_shape(
                "['--root', source_root, '-D', '_WIN32=1', '-c', "
                "f'/toolchain/{source_name}.cc', '-I', '/toolchain', "
                "'--include-angle', "
                "'/toolchain/hosted/i386-linux/include', '-o', "
                "_logical_path(source_root, stage_two_object)]"
            ),
            expression_shape(
                "['--root', source_root, '-D', '_WIN32=1', '-c', "
                "f'/toolchain/{source_name}.cc', '-I', '/toolchain', "
                "'--include-angle', "
                "'/toolchain/hosted/i386-linux/include', '-o', "
                "_logical_path(source_root, stage_three_object)]"
            ),
            expression_shape("360"),
        )]
        and len(helper_compile_loops) == 1
        and node_shape(helper_compile_loops[0])
        == node_shape(expected_helper_compile_loop)
        and len(helper_link_functions) == 1
        and node_shape(helper_link_functions[0])
        == node_shape(expected_helper_link_function)
        and helper_link_result_shapes == [node_shape(expected_helper_link_result)]
        and helper_link_guard_shapes == [node_shape(expected_helper_link_guard)]
        and native_plan_values == [expected_native_plans]
        and native_check_shapes == [expected_native_checks_shape]
        and helper_invocation_count == 1
        and len(native_build_loops) == 1
        and node_shape(native_build_loops[0])
        == node_shape(expected_native_build_loop)
        and native_execution_loop_matches
        and all(
            windows_helper_source.count(fragment)
            == (2 if fragment in {'"_WIN32=1"', 'f"/toolchain/{source_name}.cc"'} else 1)
            for fragment in required_windows_helper_fragments
        )
    )

    def immediate_raise_messages(node: ast.If) -> set[str]:
        return {
            child.value
            for statement in node.body
            if isinstance(statement, ast.Raise)
            for child in ast.walk(statement)
            if isinstance(child, ast.Constant)
            and isinstance(child.value, str)
        }

    def guarded_terms(message: str) -> frozenset[str] | None:
        if len(parser_functions) != 1:
            return None
        matches: list[frozenset[str]] = []
        for node in ast.walk(parser_functions[0]):
            if not isinstance(node, ast.If) or not any(
                message in candidate
                for candidate in immediate_raise_messages(node)
            ):
                continue
            terms = (
                node.test.values
                if isinstance(node.test, ast.BoolOp)
                and isinstance(node.test.op, ast.Or)
                else (node.test,)
            )
            matches.append(frozenset(node_shape(term) for term in terms))
        return matches[0] if len(matches) == 1 else None

    expected_guard_terms = {
        "noncanonical DOS stub": (
            "data[:len(_FIXED_PE32_DOS_STUB)] "
            "!= _FIXED_PE32_DOS_STUB",
        ),
        "no PE signature": (
            "data[pe_offset:pe_offset + 4] != b'PE\\0\\0'",
        ),
        "invalid PE32 COFF header": (
            "machine != 0x014C",
            "section_count == 0",
            "section_count > (5 if has_imports else 4)",
            "timestamp != 0",
            "symbol_table != 0",
            "symbol_count != 0",
            "optional_size != 0x00E0",
            "characteristics != 0x0103",
        ),
        "is not PE32": (
            "read_u16(optional_offset, 'PE32 magic') != 0x010B",
        ),
        "invalid PE32 image layout": (
            "linker_major != 0",
            "linker_minor != 0",
            "image_base != 0x00400000",
            "section_alignment != 0x1000",
            "file_alignment != 0x0200",
            "read_u16(optional_offset + 40, "
            "'PE32 OS major version') != 6",
            "read_u16(optional_offset + 42, "
            "'PE32 OS minor version') != 0",
            "read_u16(optional_offset + 44, "
            "'PE32 image major version') != 0",
            "read_u16(optional_offset + 46, "
            "'PE32 image minor version') != 0",
            "read_u16(optional_offset + 48, "
            "'PE32 subsystem major version') != 6",
            "read_u16(optional_offset + 50, "
            "'PE32 subsystem minor version') != 0",
            "read_u32(optional_offset + 52, 'PE32 Win32 version') != 0",
            "image_size == 0",
            "image_size % section_alignment != 0",
            "headers_size == 0",
            "headers_size % file_alignment != 0",
            "headers_size > len(data)",
            "checksum != 0",
            "subsystem != 3",
            "dll_characteristics != 0x0100",
            "stack_reserve != 0x00100000",
            "stack_commit != 0x00100000",
            "heap_reserve != 0x00100000",
            "heap_commit != 0x00001000",
            "loader_flags != 0",
            "directory_count != 16",
            "expected_entry < image_base",
            "entry_rva != expected_entry - image_base",
        ),
        "unexpected PE32 data directory": (
            "directory not in ((1, 12) if has_imports else ()) "
            "and entry != (0, 0)",
        ),
        "omits its PE32 import directories": (
            "has_imports and (directories[1] == (0, 0) "
            "or directories[12] == (0, 0))",
        ),
        "noncanonical PE32 header extent": (
            "headers_size != expected_headers_size",
            "any(data[section_table_end:headers_size])",
        ),
        "invalid PE32 section profile": (
            "expected is None",
            "expected[0] <= previous_section_rank",
            "section_characteristics != expected[1]",
            "raw_name != name.encode('ascii').ljust(8, b'\\0')",
            "read_u32(offset + 24, 'PE32 relocation offset') != 0",
            "read_u32(offset + 28, 'PE32 line offset') != 0",
            "read_u16(offset + 32, 'PE32 relocation count') != 0",
            "read_u16(offset + 34, 'PE32 line count') != 0",
        ),
        "has an empty PE32 section": (
            "virtual_size == 0",
        ),
        "noncanonical PE32 section address": (
            "virtual_address != expected_virtual_address",
        ),
        "PE32 section outside its image": (
            "virtual_end > image_size",
        ),
        "invalid PE32 file section": (
            "name == '.bss'",
            "raw_offset != expected_raw_offset",
            "raw_offset < headers_size",
            "raw_size != expected_section_raw_size",
            "virtual_size > raw_size",
        ),
        "nonzero PE32 section padding": (
            "any(data[raw_offset + virtual_size:raw_offset + raw_size])",
        ),
        "invalid empty PE32 section": (
            "raw_offset != 0",
            "name != '.bss'",
        ),
        "invalid PE32 image extent": (
            "previous_section_rank < 0",
            "expected_raw_offset != len(data)",
            "expected_image_size != image_size",
            "code_size != expected_code_size",
            "initialized_size != expected_initialized_size",
            "uninitialized_size != expected_uninitialized_size",
            "base_of_code != expected_base_of_code",
            "base_of_data != expected_base_of_data",
        ),
        "entry is not file-backed PE32 executable code": (
            "not entry_is_file_backed_executable",
        ),
        "omits its PE32 import section": (
            "'.idata' not in sections",
        ),
        "noncanonical PE32 import directory": (
            "import_rva != idata_virtual_address",
            "import_size != expected_import_size",
        ),
        "stateful PE32 import descriptor": (
            "timestamp != 0",
            "forwarder != 0",
        ),
        "noncanonical PE32 import lookup layout": (
            "lookup_rva != idata_virtual_address + cursor",
        ),
        "noncanonical PE32 import address layout": (
            "iat_rva != idata_virtual_address + cursor",
        ),
        "noncanonical PE32 import name layout": (
            "name_rva != idata_virtual_address + cursor",
        ),
        "unexpected PE32 import library": (
            "rva_string(name_rva, 'import library') != library",
            "data[library_offset:library_offset + len(encoded_library)] "
            "!= encoded_library",
        ),
        "unexpected PE32 import procedure": (
            "lookup != expected_hint_rva",
            "iat != lookup",
            "read_u16(hint_offset, 'PE32 import hint') != 0",
            "data[hint_offset + 2:hint_offset + 2 + "
            "len(encoded_procedure)] != encoded_procedure",
            "rva_string(expected_hint_rva + 2, 'import procedure') "
            "!= procedure",
        ),
        "unterminated PE32 import thunk table": (
            "read_u32(lookup_offset + len(procedures) * 4, "
            "'PE32 import lookup terminator') != 0",
            "read_u32(iat_offset + len(procedures) * 4, "
            "'PE32 import address terminator') != 0",
        ),
        "has nonzero PE32 import alignment": (
            "data[alignment_offset] != 0",
        ),
        "has no null PE32 import descriptor": (
            "any(data[descriptor_offset + len(expected_imports) * 20:"
            "descriptor_offset + (len(expected_imports) + 1) * 20])",
        ),
        "noncanonical PE32 import section extent": (
            "cursor != idata_virtual_size",
        ),
        "noncanonical PE32 IAT directory": (
            "directories[12] != (first_iat, iat_end - first_iat)",
        ),
    }
    parser_guards_match = all(
        guarded_terms(message)
        == frozenset(expression_shape(term) for term in terms)
        for message, terms in expected_guard_terms.items()
    )

    def assignment_shapes(name: str) -> list[str]:
        if len(parser_functions) != 1:
            return []
        return [
            node_shape(node.value)
            for node in ast.walk(parser_functions[0])
            if isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id == name
        ]

    expected_field_assignments = {
        "pe_offset": "read_u32(0x3C, 'DOS PE offset')",
        "optional_offset": "pe_offset + 24",
        "linker_major": "data[optional_offset + 2]",
        "linker_minor": "data[optional_offset + 3]",
        "code_size": "read_u32(optional_offset + 4, 'PE32 code size')",
        "initialized_size": (
            "read_u32(optional_offset + 8, 'PE32 initialized data size')"
        ),
        "uninitialized_size": (
            "read_u32(optional_offset + 12, 'PE32 uninitialized data size')"
        ),
        "entry_rva": "read_u32(optional_offset + 16, 'PE32 entry RVA')",
        "base_of_code": "read_u32(optional_offset + 20, 'PE32 code base')",
        "base_of_data": "read_u32(optional_offset + 24, 'PE32 data base')",
        "image_base": "read_u32(optional_offset + 28, 'PE32 image base')",
        "section_alignment": (
            "read_u32(optional_offset + 32, 'PE32 section alignment')"
        ),
        "file_alignment": (
            "read_u32(optional_offset + 36, 'PE32 file alignment')"
        ),
        "image_size": "read_u32(optional_offset + 56, 'PE32 image size')",
        "headers_size": "read_u32(optional_offset + 60, 'PE32 header size')",
        "checksum": "read_u32(optional_offset + 64, 'PE32 checksum')",
        "subsystem": "read_u16(optional_offset + 68, 'PE32 subsystem')",
        "dll_characteristics": (
            "read_u16(optional_offset + 70, 'PE32 DLL characteristics')"
        ),
        "stack_reserve": (
            "read_u32(optional_offset + 72, 'PE32 stack reserve')"
        ),
        "stack_commit": (
            "read_u32(optional_offset + 76, 'PE32 stack commit')"
        ),
        "heap_reserve": (
            "read_u32(optional_offset + 80, 'PE32 heap reserve')"
        ),
        "heap_commit": "read_u32(optional_offset + 84, 'PE32 heap commit')",
        "loader_flags": "read_u32(optional_offset + 88, 'PE32 loader flags')",
        "directory_count": (
            "read_u32(optional_offset + 92, 'PE32 directory count')"
        ),
        "section_offset": "optional_offset + optional_size",
        "section_table_end": "section_offset + section_count * 40",
        "expected_headers_size": (
            "(section_table_end + file_alignment - 1) "
            "// file_alignment * file_alignment"
        ),
        "section_characteristics": (
            "read_u32(offset + 36, 'PE32 section characteristics')"
        ),
        "expected_section_raw_size": (
            "(virtual_size + file_alignment - 1) "
            "// file_alignment * file_alignment"
        ),
        "expected_image_size": (
            "(greatest_virtual_end + section_alignment - 1) "
            "// section_alignment * section_alignment"
        ),
    }
    parser_fields_match = all(
        assignment_shapes(name) == [expression_shape(expression)]
        for name, expression in expected_field_assignments.items()
    )

    expected_sections_values: list[object] = []
    if len(parser_functions) == 1:
        for node in ast.walk(parser_functions[0]):
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == "expected_sections"
            ):
                try:
                    expected_sections_values.append(ast.literal_eval(node.value))
                except (TypeError, ValueError):
                    expected_sections_values.append(None)

    expected_dos_stub = bytes.fromhex(
        "4d5a90000300000004000000ffff0000"
        "b8000000000000004000000000000000"
        "00000000000000000000000000000000"
        "00000000000000000000000080000000"
        "0e1fba0e00b409cd21b8014ccd215468"
        "69732070726f6772616d2063616e6e6f"
        "742062652072756e20696e20444f5320"
        "6d6f64652e0d0d0a2400000000000000"
    )
    dos_stub_values: list[bytes | None] = []
    for node in bootstrap_tree.body:
        if (
            not isinstance(node, ast.Assign)
            or len(node.targets) != 1
            or not isinstance(node.targets[0], ast.Name)
            or node.targets[0].id != "_FIXED_PE32_DOS_STUB"
            or not isinstance(node.value, ast.Call)
            or node.value.keywords
            or len(node.value.args) != 1
            or not isinstance(node.value.func, ast.Attribute)
            or node.value.func.attr != "fromhex"
            or not isinstance(node.value.func.value, ast.Name)
            or node.value.func.value.id != "bytes"
            or not isinstance(node.value.args[0], ast.Constant)
            or not isinstance(node.value.args[0].value, str)
        ):
            continue
        try:
            dos_stub_values.append(bytes.fromhex(node.value.args[0].value))
        except ValueError:
            dos_stub_values.append(None)

    parser_reads_image = False
    if len(parser_wrappers) == 1:
        wrapper_calls = [
            node.value
            for node in parser_wrappers[0].body
            if isinstance(node, ast.Expr)
            and isinstance(node.value, ast.Call)
            and isinstance(node.value.func, ast.Name)
            and node.value.func.id == "_validate_static_i386_pe32_bytes"
        ]
        parser_reads_image = (
            len(wrapper_calls) == 1
            and len(wrapper_calls[0].args) == 4
            and not wrapper_calls[0].keywords
            and node_shape(wrapper_calls[0].args[0])
            == expression_shape("path.read_bytes()")
            and node_shape(wrapper_calls[0].args[1])
            == expression_shape("path.name")
            and node_shape(wrapper_calls[0].args[2])
            == expression_shape("expected_entry")
            and node_shape(wrapper_calls[0].args[3])
            == expression_shape("expected_imports")
        )
    parser_unpack_shapes = (
        {
            node_shape(node)
            for node in ast.walk(parser_functions[0])
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and isinstance(node.func.value, ast.Name)
            and node.func.value.id == "struct"
            and node.func.attr == "unpack_from"
        }
        if len(parser_functions) == 1
        else set()
    )
    expected_unpack_shapes = {
        expression_shape("struct.unpack_from('<H', data, offset)"),
        expression_shape("struct.unpack_from('<I', data, offset)"),
        expression_shape(
            "struct.unpack_from('<HHIIIHH', data, pe_offset + 4)"
        ),
        expression_shape("struct.unpack_from('<IIII', data, offset + 8)"),
        expression_shape(
            "struct.unpack_from('<IIIII', data, "
            "descriptor_offset + library_index * 20)"
        ),
    }
    dos_range_indices = (
        [
            index
            for index, statement in enumerate(parser_functions[0].body)
            if isinstance(statement, ast.Expr)
            and node_shape(statement.value)
            == expression_shape(
                "require_range(0, len(_FIXED_PE32_DOS_STUB), 'DOS header')"
            )
        ]
        if len(parser_functions) == 1
        else []
    )
    dos_guard_indices = (
        [
            index
            for index, statement in enumerate(parser_functions[0].body)
            if isinstance(statement, ast.If)
            and any(
                "noncanonical DOS stub" in message
                for message in immediate_raise_messages(statement)
            )
        ]
        if len(parser_functions) == 1
        else []
    )
    validators_by_image = {
        validator.args[0].id: (index, validator)
        for index, validator in validators
        if validator.args
        and isinstance(validator.args[0], ast.Name)
    }
    fixed_validator = validators_by_image.get("stage_two_pe32")
    import_validator = validators_by_image.get("stage_two_windows_image")
    cupiddis_validator = validators_by_image.get(
        "stage_two_windows_cupiddis"
    )
    runtime_contract_validator = validators_by_image.get(
        "stage_two_windows_runtime_contract_image"
    )
    try:
        import_expectation = (
            ast.literal_eval(import_validator[1].args[2])
            if import_validator is not None
            and len(import_validator[1].args) == 3
            else None
        )
    except (TypeError, ValueError):
        import_expectation = None
    native_windows_indices = [
        index
        for index, statement in enumerate(behavior_function.body)
        if isinstance(statement, ast.If)
        and node_shape(statement.test)
        == expression_shape("os.name == 'nt'")
    ]

    def direct_behavior_assignment(name: str) -> ast.AST | None:
        matches = [
            statement.value
            for statement in behavior_function.body
            if isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == name
        ]
        return matches[0] if len(matches) == 1 else None

    windows_cupiddis_compile = direct_behavior_assignment(
        "windows_cupiddis_main_compile_result"
    )
    windows_cupiddis_stage_three_replacements = direct_behavior_assignment(
        "stage_two_windows_cupiddis_replacements"
    )
    windows_cupiddis_stage_four_replacements = direct_behavior_assignment(
        "stage_three_windows_cupiddis_replacements"
    )
    windows_cupiddis_profile_matches = (
        windows_cupiddis_compile is not None
        and node_shape(windows_cupiddis_compile)
        == expression_shape(
            "_run_stage_pair(runner, stage_two, stage_three, 'cupidc', "
            "['--root', source_root, '-D', '_WIN32=1', '-c', "
            "'/toolchain/cupiddis_main.cc', '-I', '/toolchain', "
            "'--include-angle', '/toolchain/hosted/i386-linux/include', "
            "'-o', _logical_path(source_root, "
            "stage_two_windows_cupiddis_main)], ['--root', source_root, "
            "'-D', '_WIN32=1', '-c', '/toolchain/cupiddis_main.cc', "
            "'-I', '/toolchain', '--include-angle', "
            "'/toolchain/hosted/i386-linux/include', '-o', "
            "_logical_path(source_root, "
            "stage_three_windows_cupiddis_main)], 360)"
        )
        and windows_cupiddis_stage_three_replacements is not None
        and node_shape(windows_cupiddis_stage_three_replacements)
        == expression_shape(
            "{'cupiddis_main': stage_two_windows_cupiddis_main, "
            "'ctool_host': stage_two_windows_host_adapter}"
        )
        and windows_cupiddis_stage_four_replacements is not None
        and node_shape(windows_cupiddis_stage_four_replacements)
        == expression_shape(
            "{'cupiddis_main': stage_three_windows_cupiddis_main, "
            "'ctool_host': stage_three_windows_host_adapter}"
        )
    )
    required_windows_behavior_fragments = (
        "toolchain/hosted/i386-windows/start.asm",
        "toolchain/tests/hosted_i386_windows_contract.cc",
        "windows_assembly_result = _run_stage_pair(",
        "windows_compile_result = _run_stage_pair(",
        '"--freestanding",',
        "windows_link_result = _run_stage_pair(",
        '("__imp_ExitProcess", "KERNEL32.dll", "ExitProcess")',
        '("__imp_GetStdHandle", "KERNEL32.dll", "GetStdHandle")',
        '("__imp_WriteFile", "KERNEL32.dll", "WriteFile")',
        "windows_import_selectors = tuple(",
        'f"{slot}={library}:{procedure}"',
        "stage_two_windows_start.read_bytes()\n"
        "        != stage_three_windows_start.read_bytes()",
        "stage_two_windows_contract.read_bytes()\n"
        "        != stage_three_windows_contract.read_bytes()",
        "stage_two_windows_image.read_bytes()\n"
        "        != stage_three_windows_image.read_bytes()",
        "[str(stage_two_windows_image)]",
        "capture_output=True",
        "timeout=10",
        "native_result.returncode != 37",
        'native_result.stdout != b"Cupid-built Windows runtime: ok\\n"',
        "or native_result.stderr",
        'windows_loader: dict[str, object] = {"status": "not-run"}',
        '"return_code": native_result.returncode',
        "windows_tool_compile_result = _run_stage_pair(",
        "windows_tool_assembly_result = _run_stage_pair(",
        "windows_host_adapter_result = _run_stage_pair(",
        "windows_publication_compile_result = _run_stage_pair(",
        "windows_publication_assembly_result = _run_stage_pair(",
        "toolchain/hosted/i386-windows/publication_start.asm",
        '"__imp_DeleteFileA", "KERNEL32.dll", "DeleteFileA"',
        '"__imp_FlushFileBuffers",',
        '"__imp_GetFullPathNameA",',
        '"__imp_MoveFileExA", "KERNEL32.dll", "MoveFileExA"',
        '"_WIN32=1",',
        '"/toolchain/ctool_host.cc",',
        "windows_cupiddis_link_result = _run_stage_pair(",
        "stage_two_windows_cupiddis.read_bytes()\n"
        "        != stage_three_windows_cupiddis.read_bytes()",
        '[str(stage_two_windows_cupiddis), "--help"]',
        '"Cupid-built Windows CupidDis behavior differs"',
        '"cupiddis": {',
        '"stage-four-main": (\n'
        '                            stage_three_windows_cupiddis_main\n'
        '                        )',
        '"stage-three-main": stage_two_windows_cupiddis_main',
        "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
        "windows_runtime_contract_compile_result = _run_stage_pair(",
        "stage_two_windows_runtime_contract.read_bytes()\n"
        "        != stage_three_windows_runtime_contract.read_bytes()",
        "windows_runtime_contract_link_result = _run_stage_pair(",
        "stage_two_windows_runtime_contract_image.read_bytes()\n"
        "        != stage_three_windows_runtime_contract_image.read_bytes()",
        'str(stage_two_windows_runtime_contract_image)',
        '"Cupid-built Windows runtime contract behavior differs"',
        '"runtime_contract": {',
        "_build_windows_tool_image(",
        '"Cupid-built Windows {tool_name} behavior differs"',
        '"Cupid-built Windows CupidLD publication behavior differs"',
        '"native_tools": {',
        'evidence_out["windows_runtime"] = {',
        '"artifacts": _artifact_inventory(',
        '"library": library',
        '"procedure": procedure',
        '"slot": slot',
        '"loader": windows_loader',
        "invalid_import_assembly_result = _run_stage_pair(",
        "call __imp_ExitProcess\\n",
        "stage_two_invalid_import_object.read_bytes()\n"
        "        != stage_three_invalid_import_object.read_bytes()",
        "invalid_import_object.write_bytes(\n"
        "        stage_two_invalid_import_object.read_bytes()\n"
        "    )",
        "invalid_import_result = _run_stage_pair(",
        "stage_two_invalid_import_image.write_bytes(sentinel)",
        "stage_three_invalid_import_image.write_bytes(sentinel)",
        "IAT symbols require an absolute zero-addend relocation",
        "stage_two_invalid_import_image.read_bytes() != sentinel",
        "stage_three_invalid_import_image.read_bytes() != sentinel",
    )
    repeated_windows_behavior_fragments = {
        '("__imp_ExitProcess", "KERNEL32.dll", "ExitProcess")': 2,
        '("__imp_GetStdHandle", "KERNEL32.dll", "GetStdHandle")': 2,
        '("__imp_WriteFile", "KERNEL32.dll", "WriteFile")': 2,
        'f"{slot}={library}:{procedure}"': 2,
        "capture_output=True": 14,
        '"artifacts": _artifact_inventory(': 3,
        '"library": library': 3,
        '"procedure": procedure': 3,
        '"slot": slot': 3,
        "str(stage_two_windows_runtime_contract_image)": 2,
        '"_WIN32=1",': 6,
        '"/toolchain/ctool_host.cc",': 2,
    }
    missing_windows_behavior_fragments = [
        (
            f"{fragment} (expected "
            f"{repeated_windows_behavior_fragments.get(fragment, 1)}, "
            f"found {behavior_source.count(fragment)})"
        )
        for fragment in required_windows_behavior_fragments
        if behavior_source.count(fragment)
        != repeated_windows_behavior_fragments.get(fragment, 1)
    ]
    behavior_generation_labels_match = (
        "stage-two-" not in behavior_source
        and "stage-three-" in behavior_source
        and "stage-four-" in behavior_source
        and "stage-two-" not in windows_helper_source
        and "stage-three-" in windows_helper_source
        and "stage-four-" in windows_helper_source
    )
    if (
        positive_byte_comparisons
        != [("stage_two_pe32", "stage_three_pe32")]
        or positive_result_attributes != {"stdout", "stderr"}
        or len(validators) != 4
        or len(positive_checks) != 1
        or fixed_validator is None
        or import_validator is None
        or cupiddis_validator is None
        or runtime_contract_validator is None
        or not (
            pe32_positive_status
            < positive_checks[0][0]
            < fixed_validator[0]
            < import_validator[0]
            < cupiddis_validator[0]
            < runtime_contract_validator[0]
            < pe32_failure[0]
        )
        or len(fixed_validator[1].args) != 2
        or fixed_validator[1].keywords
        or not isinstance(fixed_validator[1].args[1], ast.Constant)
        or fixed_validator[1].args[1].value != 0x00401000
        or import_validator[1].keywords
        or not isinstance(import_validator[1].args[1], ast.Constant)
        or import_validator[1].args[1].value != 0x00401000
        or cupiddis_validator[1].keywords
        or len(cupiddis_validator[1].args) != 3
        or not isinstance(cupiddis_validator[1].args[1], ast.Constant)
        or cupiddis_validator[1].args[1].value != 0x00401000
        or runtime_contract_validator[1].keywords
        or len(runtime_contract_validator[1].args) != 3
        or not isinstance(
            runtime_contract_validator[1].args[1], ast.Constant
        )
        or runtime_contract_validator[1].args[1].value != 0x00401000
        or import_expectation
        != ((
            "KERNEL32.dll",
            ("ExitProcess", "GetStdHandle", "WriteFile"),
        ),)
        or not windows_helper_matches
        or not windows_publication_stage_shapes_match
        or not windows_publication_assignments_match
        or not native_guards_match
        or not native_workloads_match
        or not native_windows_control_flow_matches
        or not windows_cupiddis_profile_matches
        or not behavior_generation_labels_match
        or missing_windows_behavior_fragments
        or len(native_windows_indices) != 4
        or not (
            import_validator[0]
            < native_windows_indices[0]
            < cupiddis_validator[0]
            < runtime_contract_validator[0]
            < native_windows_indices[1]
            < native_windows_indices[2]
            < native_windows_indices[3]
            < invalid_import[0]
        )
        or len(parser_functions) != 1
        or len(parser_wrappers) != 1
        or not import_parser_is_confined
        or not parser_reads_image
        or parser_unpack_shapes != expected_unpack_shapes
        or not parser_fields_match
        or not parser_guards_match
        or expected_sections_values
        != [
            {
                ".text": (0, 0x60000020),
                ".rodata": (1, 0x40000040),
                ".data": (2, 0xC0000040),
                ".bss": (3, 0xC0000080),
            }
        ]
        or dos_stub_values != [expected_dos_stub]
        or len(dos_range_indices) != 1
        or len(dos_guard_indices) != 1
        or dos_range_indices[0] >= dos_guard_indices[0]
    ):
        raise AuditError(
        "Cupid Toolchain fixed-point PE32 behavior differs: "
        "the staged bytes or independent parser are not checked; "
        f"validators={len(validators)}, "
        f"native_windows={native_windows_indices!r}, "
        f"helper={windows_helper_matches}, "
        f"guards={native_guards_match}, "
        f"workloads={native_workloads_match}, "
        f"control_flow={native_windows_control_flow_matches}, "
        f"cupiddis_profile={windows_cupiddis_profile_matches}, "
        f"labels={behavior_generation_labels_match}, "
        f"missing={missing_windows_behavior_fragments!r}"
        )
    sentinel_writes = [
        statement.value.func.value.id
        for index, statement in enumerate(behavior_function.body)
        if index < pe32_failure[0]
        and isinstance(statement, ast.Expr)
        and isinstance(statement.value, ast.Call)
        and not statement.value.keywords
        and len(statement.value.args) == 1
        and isinstance(statement.value.args[0], ast.Name)
        and statement.value.args[0].id == "sentinel"
        and isinstance(statement.value.func, ast.Attribute)
        and statement.value.func.attr == "write_bytes"
        and isinstance(statement.value.func.value, ast.Name)
        and statement.value.func.value.id
        in {"stage_two_pe32_failure", "stage_three_pe32_failure"}
    ]
    failure_checks = [
        statement
        for index, statement in enumerate(behavior_function.body)
        if index > pe32_failure_status
        if isinstance(statement, ast.If)
        and any(
            isinstance(node, ast.Name) and node.id == "invalid_pe32_result"
            for node in ast.walk(statement.test)
        )
    ]
    failure_sentinel_checks = (
        {
            read_bytes_receiver(node.left)
            for node in ast.walk(failure_checks[0].test)
            if isinstance(node, ast.Compare)
            and len(node.ops) == 1
            and isinstance(node.ops[0], ast.NotEq)
            and len(node.comparators) == 1
            and isinstance(node.comparators[0], ast.Name)
            and node.comparators[0].id == "sentinel"
            and read_bytes_receiver(node.left) is not None
        }
        if len(failure_checks) == 1
        else set()
    )
    failure_result_attributes = (
        result_attributes(failure_checks[0].test, "invalid_pe32_result")
        if len(failure_checks) == 1
        else set()
    )
    failure_diagnostic_checks = (
        [
            node
            for node in ast.walk(failure_checks[0].test)
            if isinstance(node, ast.Compare)
            and len(node.ops) == 1
            and isinstance(node.ops[0], ast.NotIn)
            and len(node.comparators) == 1
            and isinstance(node.left, ast.Constant)
            and node.left.value
            == "CupidLD PE32 requires text address 0x00401000"
            and isinstance(node.comparators[0], ast.Attribute)
            and node.comparators[0].attr == "stderr"
            and isinstance(node.comparators[0].value, ast.Name)
            and node.comparators[0].value.id == "invalid_pe32_result"
        ]
        if len(failure_checks) == 1
        else []
    )
    if (
        sentinel_writes
        != ["stage_two_pe32_failure", "stage_three_pe32_failure"]
        or failure_sentinel_checks
        != {"stage_two_pe32_failure", "stage_three_pe32_failure"}
        or failure_result_attributes != {"stdout", "stderr"}
        or len(failure_diagnostic_checks) != 1
        or bootstrap_source.count('"cupidld help omits i386pe"') != 1
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point PE32 behavior differs: "
            "the semantic failure does not diagnose and preserve both outputs"
        )

    required_bootstrap_helper_fragments = (
        "def freeze_source_inputs(",
        "destination = snapshot_root / name",
        "if frozen_data != data:",
        "def require_source_closures(",
        "require_frozen_source_snapshot(source_inputs, plan)",
        "live_source_root, plan, source_inputs.inventory",
        "def require_live_seed_inputs(",
        "manifest_bytes != captured.manifest_bytes",
        "live_bytes != expected_bytes",
    )
    missing_bootstrap_fragments = [
        f"helper: {fragment}"
        for fragment in required_bootstrap_helper_fragments
        if bootstrap_source.count(fragment) != 1
    ]

    def bootstrap_assignment(name: str) -> object | None:
        matches = [
            statement.value
            for statement in bootstrap_tree.body
            if isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == name
        ]
        if len(matches) != 1:
            return None
        value = matches[0]
        try:
            if (
                isinstance(value, ast.Call)
                and isinstance(value.func, ast.Name)
                and value.func.id == "frozenset"
                and len(value.args) == 1
                and not value.keywords
            ):
                return frozenset(ast.literal_eval(value.args[0]))
            return ast.literal_eval(value)
        except (TypeError, ValueError):
            return None

    if bootstrap_assignment("BOOTSTRAP_PUBLICATION_NAMES") != (
        "stage-two",
        "stage-three",
        "stage-four",
        "behavior",
        "bootstrap-report.json",
    ):
        missing_bootstrap_fragments.append(
            "publication must carry stages two through four"
        )
    if bootstrap_assignment("WINDOWS_COMPILE_DEFINES") != frozenset(
        {
            "ctool_host",
            "cupidasm_main",
            "cupidc_main",
            "cupiddis_main",
            "cupidld_main",
            "cupidobj_main",
            "publication_runtime",
        }
    ):
        missing_bootstrap_fragments.append(
            "native Windows compile definitions must cover every driver"
        )
    if "WINDOWS_LINKS" in bootstrap_source:
        missing_bootstrap_fragments.append(
            "native Windows links must come from the verified Linux plan"
        )
    fixed_point_functions = {
        name: [
            statement
            for statement in bootstrap_tree.body
            if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
            and statement.name == name
        ]
        for name in (
            "_bootstrap_from_frozen_seed",
            "_bootstrap_windows_from_frozen_seed",
        )
    }
    if any(len(functions) != 1 for functions in fixed_point_functions.values()):
        missing_bootstrap_fragments.append(
            "the Linux and Windows fixed-point drivers must each be unique"
        )
        linux_bootstrap_function = None
        windows_bootstrap_function = None
        linux_bootstrap_source = ""
        windows_bootstrap_source = ""
    else:
        linux_bootstrap_function = fixed_point_functions[
            "_bootstrap_from_frozen_seed"
        ][0]
        windows_bootstrap_function = fixed_point_functions[
            "_bootstrap_windows_from_frozen_seed"
        ][0]
        linux_bootstrap_source = (
            ast.get_source_segment(bootstrap_source, linux_bootstrap_function)
            or ""
        )
        windows_bootstrap_source = (
            ast.get_source_segment(
                bootstrap_source, windows_bootstrap_function
            )
            or ""
        )
    required_linux_bootstrap_fragments = (
        "source_inputs = freeze_source_inputs(",
        "private_source_root = source_inputs.root",
        "runner = ToolRunner(private_source_root)",
        'private_source_root / "stage-two",',
        'private_source_root / "stage-three",',
        'private_source_root / "stage-four",',
        "stage_three_producers = {",
        "stage_four = _build_stage(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            private_source_root / \"stage-four\",\n"
        "            stage_three_producers,\n"
        "            plan,\n"
        "            \"stage four\",\n"
        "        )",
        "comparisons = (\n"
        "            _compare_stages(stage_three, stage_four, source_names)\n"
        "            if compare_fixed_point\n"
        "            else None\n"
        "        )",
        "behavior_evidence: dict[str, object] = {}",
        "behavior = _run_behavior_checks(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            private_source_root,\n"
        "            stage_three,\n"
        "            stage_four,",
        "            behavior_evidence,\n"
        "        )",
        '"behavior_generations": ["stage-three", "stage-four"],',
        '"stage-four": {\n'
        '                    "objects": _artifact_inventory(stage_four.objects),\n'
        '                    "producer_generation": "stage-three",',
        '"status": (\n'
        '                "pass"\n'
        '                if compare_fixed_point\n'
        '                else "pending-fixed-point-author"\n'
        '            ),',
        'windows_runtime = behavior_evidence.get("windows_runtime")',
        'windows_cupiddis = windows_runtime.get("cupiddis")',
        'windows_runtime_contract = windows_runtime.get("runtime_contract")',
        'windows_native_tools = windows_runtime.get("native_tools")',
        ') != {"cupidasm", "cupidc", "cupidld", "cupidobj"}:',
        'for tool_name in ("cupidasm", "cupidc", "cupidld", "cupidobj"):',
        'report_path = private_source_root / "bootstrap-report.json"',
        '"windows_cupiddis": windows_cupiddis["loader"]',
        '"windows_cupidld": windows_native_tools["cupidld"]["loader"]',
        '"windows_cupidasm": windows_native_tools["cupidasm"]',
        '"windows_cupidc": windows_native_tools["cupidc"]["loader"]',
        '"windows_loader": windows_loader,',
        '"windows_cupidobj": windows_native_tools["cupidobj"]',
        '"windows_runtime_contract": (',
        '"windows_runtime": windows_runtime,',
        'publication_root = private_workspace / "publication"',
        "for name in BOOTSTRAP_PUBLICATION_NAMES:",
        "(private_source_root / name).replace(",
        "publish_bootstrap_outputs(publication_root, output_root)",
    )
    missing_bootstrap_fragments.extend(
        f"Linux driver: {fragment}"
        for fragment in required_linux_bootstrap_fragments
        if linux_bootstrap_source.count(fragment) != 1
    )
    required_windows_bootstrap_fragments = (
        "source_inputs = freeze_source_inputs(",
        "private_source_root = source_inputs.root",
        "runner = ToolRunner(private_source_root)",
        'private_source_root / "stage-two",',
        'private_source_root / "stage-three",',
        'private_source_root / "stage-four",',
        "stage_two = _build_windows_stage(",
        "stage_three = _build_windows_stage(",
        "stage_four = _build_windows_stage(",
        "comparisons = _compare_windows_stages(",
        "behavior = _run_native_windows_behavior_checks(",
        '"behavior_generations": ["stage-three", "stage-four"],',
        '"schema": WINDOWS_REPORT_SCHEMA,',
        'report_path = private_source_root / "bootstrap-report.json"',
        'publication_root = private_workspace / "publication"',
        "for name in BOOTSTRAP_PUBLICATION_NAMES:",
        "(private_source_root / name).replace(",
        "publish_bootstrap_outputs(publication_root, output_root)",
        "stage_two = _build_windows_stage(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            private_source_root / \"stage-two\",",
        "stage_three = _build_windows_stage(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            private_source_root / \"stage-three\",",
        "stage_four = _build_windows_stage(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            private_source_root / \"stage-four\",\n"
        "            stage_three_producers,\n"
        "            native_plan,\n"
        "            \"stage four\",\n"
        "        )",
        "comparisons = _compare_windows_stages(\n"
        "            stage_three,\n"
        "            stage_four,",
        "behavior = _run_native_windows_behavior_checks(\n"
        "            runner,\n"
        "            private_source_root,\n"
        "            stage_three,\n"
        "            stage_four,\n"
        "            native_plan,\n"
        "        )",
        '"stage-four": {\n'
        '                    "objects": _artifact_inventory(stage_four.objects),\n'
        '                    "producer_generation": "native-stage-three",',
    )
    missing_bootstrap_fragments.extend(
        f"Windows driver: {fragment}"
        for fragment in required_windows_bootstrap_fragments
        if windows_bootstrap_source.count(fragment) != 1
    )

    def named_assignment_values(
        function: ast.FunctionDef | ast.AsyncFunctionDef | None,
        name: str,
    ) -> list[ast.expr]:
        if function is None:
            return []
        values: list[ast.expr] = []
        for node in ast.walk(function):
            if (
                isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == name
            ):
                values.append(node.value)
            elif (
                isinstance(node, ast.AnnAssign)
                and isinstance(node.target, ast.Name)
                and node.target.id == name
                and node.value is not None
            ):
                values.append(node.value)
        return values

    expected_linux_comparison = ast.parse(
        "_compare_stages(stage_three, stage_four, source_names) "
        "if compare_fixed_point else None",
        mode="eval",
    ).body
    linux_comparison_values = named_assignment_values(
        linux_bootstrap_function, "comparisons"
    )
    linux_keyword_defaults = (
        {
            argument.arg: default
            for argument, default in zip(
                linux_bootstrap_function.args.kwonlyargs,
                linux_bootstrap_function.args.kw_defaults,
            )
        }
        if linux_bootstrap_function is not None
        else {}
    )
    if (
        len(linux_comparison_values) != 1
        or ast.dump(linux_comparison_values[0], include_attributes=False)
        != ast.dump(expected_linux_comparison, include_attributes=False)
        or "compare_fixed_point" not in linux_keyword_defaults
        or linux_keyword_defaults["compare_fixed_point"] is not None
    ):
        missing_bootstrap_fragments.append(
            "Linux driver: fixed-point comparison must remain an "
            "explicit internal policy"
        )

    bootstrap_policy_functions = {
        name: [
            statement
            for statement in bootstrap_tree.body
            if isinstance(
                statement, (ast.FunctionDef, ast.AsyncFunctionDef)
            )
            and statement.name == name
        ]
        for name in (
            "_bootstrap_from_seed_with_policy",
            "bootstrap_from_seed",
            "_bootstrap_for_manifest_author",
        )
    }
    if any(
        len(functions) != 1
        for functions in bootstrap_policy_functions.values()
    ):
        missing_bootstrap_fragments.append(
            "bootstrap policy drivers must each be unique"
        )
    else:
        seed_policy = bootstrap_policy_functions[
            "_bootstrap_from_seed_with_policy"
        ][0]
        public_bootstrap = bootstrap_policy_functions[
            "bootstrap_from_seed"
        ][0]
        author_bootstrap = bootstrap_policy_functions[
            "_bootstrap_for_manifest_author"
        ][0]
        seed_policy_defaults = {
            argument.arg: default
            for argument, default in zip(
                seed_policy.args.kwonlyargs,
                seed_policy.args.kw_defaults,
            )
        }

        def fixed_point_policy_call(
            function: ast.FunctionDef | ast.AsyncFunctionDef,
            callee: str,
        ) -> tuple[bool, ast.expr | None]:
            calls = [
                node
                for node in ast.walk(function)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == callee
            ]
            if len(calls) != 1:
                return False, None
            policy_keywords = [
                keyword.value
                for keyword in calls[0].keywords
                if keyword.arg == "compare_fixed_point"
            ]
            return (
                len(policy_keywords) == 1,
                policy_keywords[0] if len(policy_keywords) == 1 else None,
            )

        forwards_policy, forwarded_value = fixed_point_policy_call(
            seed_policy, "_bootstrap_from_frozen_seed"
        )
        public_policy, public_value = fixed_point_policy_call(
            public_bootstrap, "_bootstrap_from_seed_with_policy"
        )
        author_policy, author_value = fixed_point_policy_call(
            author_bootstrap, "_bootstrap_from_seed_with_policy"
        )
        public_argument_names = {
            argument.arg
            for argument in (
                *public_bootstrap.args.posonlyargs,
                *public_bootstrap.args.args,
                *public_bootstrap.args.kwonlyargs,
            )
        }
        if (
            seed_policy_defaults.get("compare_fixed_point", False)
            is not None
            or not forwards_policy
            or not isinstance(forwarded_value, ast.Name)
            or forwarded_value.id != "compare_fixed_point"
            or "compare_fixed_point" in public_argument_names
            or not public_policy
            or not isinstance(public_value, ast.Constant)
            or public_value.value is not True
            or not author_policy
            or not isinstance(author_value, ast.Constant)
            or author_value.value is not False
        ):
            missing_bootstrap_fragments.append(
                "public bootstrap must finalize while the private author "
                "bootstrap stays pending"
            )

    def source_closure_call_count(
        function: ast.FunctionDef | ast.AsyncFunctionDef | None,
        plan_name: str,
    ) -> int:
        if function is None:
            return 0
        count = 0
        for node in ast.walk(function):
            if not isinstance(node, ast.Call):
                continue
            if not isinstance(node.func, ast.Name):
                continue
            if (
                node.func.id != "require_source_closures"
                or len(node.args) != 3
            ):
                continue
            names = [
                argument.id if isinstance(argument, ast.Name) else None
                for argument in node.args
            ]
            if names == ["source_inputs", "source_root", plan_name]:
                count += 1
        return count

    if source_closure_call_count(linux_bootstrap_function, "plan") != 5:
        missing_bootstrap_fragments.append(
            "Linux driver: five live and frozen closure checks"
        )
    if source_closure_call_count(
        windows_bootstrap_function, "linux_plan"
    ) != 5:
        missing_bootstrap_fragments.append(
            "Windows driver: five live and frozen closure checks"
        )

    def live_seed_call_count(
        function: ast.FunctionDef | ast.AsyncFunctionDef | None,
        expected_names: list[str],
    ) -> int:
        if function is None:
            return 0
        return sum(
            1
            for node in ast.walk(function)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "require_live_seed_inputs"
            and [
                argument.id if isinstance(argument, ast.Name) else None
                for argument in node.args
            ]
            == expected_names
        )

    if live_seed_call_count(linux_bootstrap_function, ["seed_inputs"]) != 5:
        missing_bootstrap_fragments.append(
            "Linux driver: five live seed cohort checks"
        )
    if live_seed_call_count(
        windows_bootstrap_function, ["seed_inputs", "plan_inputs"]
    ) != 5:
        missing_bootstrap_fragments.append(
            "Windows driver: five live execution and plan seed checks"
        )

    native_windows_function_names = (
        "_windows_build_plan",
        "_windows_link_arguments",
        "_build_windows_stage",
        "_compare_windows_stages",
        "_run_native_windows_behavior_checks",
    )
    native_windows_functions = {
        name: [
            statement
            for statement in bootstrap_tree.body
            if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
            and statement.name == name
        ]
        for name in native_windows_function_names
    }
    missing_native_windows_fragments: list[str] = []
    native_windows_sources: dict[str, str] = {}
    for name, functions in native_windows_functions.items():
        if len(functions) != 1:
            missing_native_windows_fragments.append(
                f"{name} must be unique"
            )
            native_windows_sources[name] = ""
        else:
            native_windows_sources[name] = (
                ast.get_source_segment(bootstrap_source, functions[0]) or ""
            )

    native_windows_fragment_contracts = {
        "_windows_build_plan": (
            'path = "/toolchain/hosted/i386-windows/runtime.cc"',
            '"publication_runtime.cc"',
            '"/toolchain/hosted/i386-windows/tool_start.asm"',
            '"publication_start.asm"',
            'raw_links = _require_object(linux_plan.get("links"), '
            '"build_plan.links")',
            'known_linux_objects = {"start", *source_names}',
            "native_order = list(linux_order)",
            'native_order.index("start") + 1',
            'native_order.index("runtime")',
            'linux_plan.get("include_arguments")',
            '"links": links',
        ),
        "_windows_link_arguments": (
            '"i386pe"',
            '"0x00401000"',
            '"_start"',
            "for selector in _windows_import_selectors(tool_name):",
            "objects[name] for name in link_order",
        ),
        "_build_windows_stage": (
            'producers["cupidc"]',
            'producers["cupidasm"]',
            'producers["cupidld"]',
            "_validate_i386_relocatable(object_path)\n"
            "        return name, object_path",
            "_validate_i386_relocatable(object_path)\n"
            "        objects[name] = object_path",
            'native_plan.get("links"), "Windows build plan links"',
            "tool_name, executable, objects, link_order",
            "_validate_static_i386_pe32(",
            "_windows_imports(tool_name)",
        ),
        "_compare_windows_stages": (
            "stage_three.objects[name].read_bytes()",
            "stage_four.objects[name].read_bytes()",
            "stage_three.tools[name].read_bytes()",
            "stage_four.tools[name].read_bytes()",
            '"all_equal": True',
            '"compared_generations": ["stage-three", "stage-four"]',
        ),
        "_run_native_windows_behavior_checks": (
            "for tool_name in TOOL_NAMES:",
            "failure_result = _run_stage_pair(",
            '"--definitely-invalid-option"',
            "stage_two_object.read_bytes()",
            "stage_three_object.read_bytes()",
            'stage_two_binary.read_bytes() != b"\\xb8\\x34\\x12\\xc3"',
            "stage_three_binary.read_bytes()\n"
            "        != stage_two_binary.read_bytes()",
            "stage_two_wrapped.read_bytes()",
            "stage_three_wrapped.read_bytes()",
            'native_plan.get("links"), "Windows build plan links"',
            'raw_links.get("cupidasm")',
            "stage_two.objects,\n            link_order,",
            "stage_three.objects,\n            link_order,",
            "link_result = _run_stage_pair(",
            'stage_two.tools["cupidasm"].read_bytes()',
            'stage_three.tools["cupidasm"].read_bytes()',
            "_validate_static_i386_pe32(",
        ),
    }
    for name, fragments in native_windows_fragment_contracts.items():
        source = native_windows_sources[name]
        missing_native_windows_fragments.extend(
            f"{name}: {fragment}"
            for fragment in fragments
            if source.count(fragment) != 1
        )
    if "EXPECTED_INCLUDE_ARGUMENTS" in native_windows_sources[
        "_windows_build_plan"
    ]:
        missing_native_windows_fragments.append(
            "_windows_build_plan: include arguments bypass the Linux plan"
        )

    behavior_functions = native_windows_functions[
        "_run_native_windows_behavior_checks"
    ]
    if len(behavior_functions) == 1:
        behavior_function = behavior_functions[0]
        if any(
            live_linked_code_policy_call_count(
                behavior_function, helper_name
            )
            != 1
            for helper_name in linked_code_policy_helper_names
        ):
            raise AuditError(
                "Cupid Toolchain fixed-point linked-code policy calls "
                "differ: _run_native_windows_behavior_checks must call all "
                "three helpers once"
            )
        expected_native_windows_behavior = ast.parse(
            "{"
            "'failure_cases': len(TOOL_NAMES) + 4, "
            "'help_cases': len(TOOL_NAMES), "
            "'success_cases': len(TOOL_NAMES) + 3"
            "}",
            mode="eval",
        ).body
        native_windows_behavior_returns = [
            statement.value
            for statement in behavior_function.body
            if isinstance(statement, ast.Return)
            and statement.value is not None
        ]
        if (
            len(native_windows_behavior_returns) != 1
            or ast.dump(
                native_windows_behavior_returns[0], include_attributes=False
            )
            != ast.dump(
                expected_native_windows_behavior, include_attributes=False
            )
        ):
            missing_native_windows_fragments.append(
                "_run_native_windows_behavior_checks: return 9 failure, "
                "5 help, and 8 success cases"
            )
        behavior_parents = {
            child: parent
            for parent in ast.walk(behavior_function)
            for child in ast.iter_child_nodes(parent)
        }
        live_behavior_calls = (
            "help_result",
            "failure_result",
            "compile_result",
            "assembly_result",
            "disassembly_result",
            "wrap_result",
            "link_result",
        )
        for result_name in live_behavior_calls:
            matches = [
                node
                for node in ast.walk(behavior_function)
                if isinstance(node, ast.Assign)
                and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)
                and node.targets[0].id == result_name
                and isinstance(node.value, ast.Call)
                and isinstance(node.value.func, ast.Name)
                and node.value.func.id == "_run_stage_pair"
                and not _ast_node_is_statically_dead(
                    node, behavior_function, behavior_parents
                )
            ]
            if len(matches) != 1:
                missing_native_windows_fragments.append(
                    "_run_native_windows_behavior_checks: one live "
                    f"{result_name} stage-pair call"
                )
    if missing_native_windows_fragments:
        raise AuditError(
            "Cupid Toolchain native Windows fixed-point behavior differs: "
            f"{missing_native_windows_fragments!r}"
        )
    source_input_functions = [
        statement
        for statement in bootstrap_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_source_input_paths"
    ]
    source_input_strings = (
        [
            node.value
            for node in ast.walk(source_input_functions[0])
            if isinstance(node, ast.Constant)
            and isinstance(node.value, str)
        ]
        if len(source_input_functions) == 1
        else []
    )
    required_windows_source_inputs = (
        "toolchain/hosted/i386-windows/cupidbuild_start.asm",
        "toolchain/hosted/i386-windows/publication_runtime.cc",
        "toolchain/hosted/i386-windows/publication_start.asm",
        "toolchain/hosted/i386-windows/runtime.cc",
        "toolchain/hosted/i386-windows/start.asm",
        "toolchain/hosted/i386-windows/tool_start.asm",
        "toolchain/tests/hosted_i386_windows_contract.cc",
        "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    )
    required_windows_publisher_inputs = (
        "toolchain/hosted/i386-linux/include/windows.h",
        *required_windows_source_inputs,
    )
    required_contract_control_inputs = (
        "toolchain/Makefile",
        "toolchain/tests/artifact_size_policy_contract.cc",
        "toolchain/tests/toolchain_manifest_contract.cc",
        "tools/bootstrap_toolchain.py",
        "tools/cupidc_toolchain_contracts.py",
        "tools/user_syscall_abi.py",
    )
    required_user_abi_inputs = (
        "kernel/core/types.h",
        "kernel/core/syscall.h",
        "kernel/core/syscall.cc",
        "kernel/fs/vfs.h",
        "kernel/network/socket.h",
        "user/cupid.h",
    )
    windows_source_inputs_are_exact = all(
        source_input_strings.count(path) == 1
        for path in required_windows_source_inputs
    )
    publisher_control_values: list[object] = []
    publisher_windows_values: list[object] = []
    publisher_user_abi_values: list[object] = []
    publisher_plan_source_values: list[tuple[str, ...] | None] = []
    for statement in contract_publisher_tree.body:
        if (
            isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == "CONTRACT_CONTROL_INPUTS"
        ):
            try:
                publisher_control_values.append(
                    ast.literal_eval(statement.value)
                )
            except (TypeError, ValueError):
                publisher_control_values.append(None)
        if (
            isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == "WINDOWS_RUNTIME_INPUTS"
        ):
            try:
                publisher_windows_values.append(
                    ast.literal_eval(statement.value)
                )
            except (TypeError, ValueError):
                publisher_windows_values.append(None)
        if (
            isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == "USER_SYSCALL_ABI_INPUTS"
        ):
            try:
                publisher_user_abi_values.append(
                    ast.literal_eval(statement.value)
                )
            except (TypeError, ValueError):
                publisher_user_abi_values.append(None)
        if (
            isinstance(statement, ast.Assign)
            and len(statement.targets) == 1
            and isinstance(statement.targets[0], ast.Name)
            and statement.targets[0].id == "CONTRACT_PLANS"
        ):
            plan_sources: list[str] = []
            if isinstance(statement.value, ast.Tuple):
                for plan in statement.value.elts:
                    if (
                        not isinstance(plan, ast.Call)
                        or not isinstance(plan.func, ast.Name)
                        or plan.func.id != "ContractPlan"
                        or len(plan.args) < 2
                        or not isinstance(plan.args[1], ast.Constant)
                        or not isinstance(plan.args[1].value, str)
                    ):
                        plan_sources = []
                        break
                    plan_sources.append(plan.args[1].value)
            publisher_plan_source_values.append(
                tuple(plan_sources) if plan_sources else None
            )
    publisher_input_functions = [
        statement
        for statement in contract_publisher_tree.body
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef))
        and statement.name == "_contract_input_paths"
    ]
    expected_publication_path_source = """
paths = {root / plan.source for plan in CONTRACT_PLANS}
paths.update(root / path for path in CONTRACT_CONTROL_INPUTS)
paths.update(root / path for path in WINDOWS_RUNTIME_INPUTS)
paths.update(root / path for path in USER_SYSCALL_ABI_INPUTS)
paths.add(root / "toolchain/tests/hosted_i386_runtime_contract.cc")
paths.add(root / "kernel/lang/as_elf.cc")
paths.add(root / "kernel/lang/as_elf.h")
paths.add(root / "toolchain/x86.cc")
paths.update((root / "toolchain").glob("*.h"))
paths.update((root / "toolchain/tests").glob("*.inc"))
paths.update((root / "toolchain/tests").glob("*.h"))
paths.update((root / "toolchain/hosted/i386-linux/include").glob("*.h"))
missing = sorted(
    path.relative_to(root).as_posix()
    for path in paths
    if not path.is_file() or path.is_symlink()
)
return tuple(
    sorted(paths, key=lambda path: path.relative_to(root).as_posix())
)
"""
    expected_publication_path_statements = [
        ast.dump(statement, include_attributes=False)
        for statement in ast.parse(expected_publication_path_source).body
    ]

    def statement_references_paths(statement: ast.stmt) -> bool:
        return any(
            isinstance(node, ast.Name) and node.id == "paths"
            for node in ast.walk(statement)
        )

    publisher_path_statements = (
        [
            ast.dump(statement, include_attributes=False)
            for statement in publisher_input_functions[0].body
            if statement_references_paths(statement)
        ]
        if len(publisher_input_functions) == 1
        else []
    )
    publisher_plan_sources = (
        publisher_plan_source_values[0]
        if len(publisher_plan_source_values) == 1
        else None
    )

    publisher_protocol_errors: list[str] = []
    publisher_functions = {
        name: [
            statement
            for statement in contract_publisher_tree.body
            if isinstance(
                statement, (ast.FunctionDef, ast.AsyncFunctionDef)
            )
            and statement.name == name
        ]
        for name in (
            "_capture_stage_pairs",
            "_capture_regular_stage_file",
            "_stage_file_identity",
            "build_contracts",
        )
    }
    if any(len(functions) != 1 for functions in publisher_functions.values()):
        publisher_protocol_errors.append(
            "publisher protocol functions must each be unique"
        )
    else:
        build_function = publisher_functions["build_contracts"][0]
        build_parents = {
            child: parent
            for parent in ast.walk(build_function)
            for child in ast.iter_child_nodes(parent)
        }

        def live_build_calls(name: str) -> list[ast.Call]:
            return [
                node
                for node in ast.walk(build_function)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Name)
                and node.func.id == name
                and not _ast_node_is_statically_dead(
                    node, build_function, build_parents
                )
            ]

        bootstrap_calls = live_build_calls(
            "_bootstrap_for_manifest_author"
        )
        public_bootstrap_calls = live_build_calls("bootstrap_from_seed")
        if len(bootstrap_calls) != 1 or public_bootstrap_calls:
            publisher_protocol_errors.append(
                "publisher must use the private pending bootstrap"
            )

        author_calls = live_build_calls("_checked_manifest_author_bytes")
        comparison_calls = live_build_calls("_compare_stage_files")
        tool_fixed_point_assignments = [
            node
            for node in ast.walk(build_function)
            if isinstance(node, ast.Assign)
            and len(node.targets) == 1
            and isinstance(node.targets[0], ast.Subscript)
            and isinstance(node.targets[0].value, ast.Name)
            and node.targets[0].value.id == "report"
            and isinstance(node.targets[0].slice, ast.Constant)
            and node.targets[0].slice.value == "tool_fixed_point"
            and isinstance(node.value, ast.Call)
            and isinstance(node.value.func, ast.Name)
            and node.value.func.id == "_tool_fixed_point_record"
            and not _ast_node_is_statically_dead(
                node, build_function, build_parents
            )
        ]
        protocol_order_is_exact = (
            len(author_calls) == 1
            and len(comparison_calls) == 4
            and len(tool_fixed_point_assignments) == 1
            and author_calls[0].lineno
            < min(call.lineno for call in comparison_calls)
            and max(call.lineno for call in comparison_calls)
            < tool_fixed_point_assignments[0].lineno
        )
        if not protocol_order_is_exact:
            publisher_protocol_errors.append(
                "Cupid author, four Python comparisons, and fixed-point "
                "summary must remain in that order"
            )

        comparison_guards = []
        for node in ast.walk(build_function):
            if (
                not isinstance(node, ast.Compare)
                or not isinstance(node.left, ast.Constant)
                or node.left.value != "comparisons"
                or len(node.ops) != 1
                or not isinstance(node.ops[0], ast.In)
                or len(node.comparators) != 1
                or not isinstance(node.comparators[0], ast.Name)
                or node.comparators[0].id != "bootstrap_report"
                or _ast_node_is_statically_dead(
                    node, build_function, build_parents
                )
            ):
                continue
            parent = build_parents.get(node)
            while parent is not None and not isinstance(parent, ast.If):
                parent = build_parents.get(parent)
            if parent is not None and any(
                isinstance(descendant, ast.Raise)
                for statement in parent.body
                for descendant in ast.walk(statement)
            ):
                comparison_guards.append(node)
        if (
            len(comparison_guards) != 1
            or len(author_calls) != 1
            or comparison_guards[0].lineno >= author_calls[0].lineno
        ):
            publisher_protocol_errors.append(
                "publisher must reject a precomputed bootstrap comparison"
            )

        pending_status_guards = []
        for node in ast.walk(build_function):
            if (
                not isinstance(node, ast.Compare)
                or not isinstance(node.left, ast.Call)
                or not isinstance(node.left.func, ast.Attribute)
                or not isinstance(node.left.func.value, ast.Name)
                or node.left.func.value.id != "bootstrap_report"
                or node.left.func.attr != "get"
                or len(node.left.args) != 1
                or not isinstance(node.left.args[0], ast.Constant)
                or node.left.args[0].value != "status"
                or len(node.ops) != 1
                or not isinstance(node.ops[0], ast.NotEq)
                or len(node.comparators) != 1
                or not isinstance(node.comparators[0], ast.Constant)
                or node.comparators[0].value
                != "pending-fixed-point-author"
                or _ast_node_is_statically_dead(
                    node, build_function, build_parents
                )
            ):
                continue
            parent = build_parents.get(node)
            while parent is not None and not isinstance(parent, ast.If):
                parent = build_parents.get(parent)
            if parent is not None and any(
                isinstance(descendant, ast.Raise)
                for statement in parent.body
                for descendant in ast.walk(statement)
            ):
                pending_status_guards.append(node)
        if (
            len(pending_status_guards) != 1
            or len(author_calls) != 1
            or pending_status_guards[0].lineno >= author_calls[0].lineno
        ):
            publisher_protocol_errors.append(
                "publisher must require the pending bootstrap status"
            )

        capture_pairs = publisher_functions["_capture_stage_pairs"][0]
        capture_pair_parents = {
            child: parent
            for parent in ast.walk(capture_pairs)
            for child in ast.iter_child_nodes(parent)
        }
        capture_pair_calls = [
            node
            for node in ast.walk(capture_pairs)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_capture_regular_stage_file"
            and not _ast_node_is_statically_dead(
                node, capture_pairs, capture_pair_parents
            )
        ]
        capture_pair_reads_paths = any(
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr == "read_bytes"
            for node in ast.walk(capture_pairs)
        )
        if len(capture_pair_calls) != 2 or capture_pair_reads_paths:
            publisher_protocol_errors.append(
                "each stage pair must use two checked regular-file captures"
            )

        capture_regular = publisher_functions[
            "_capture_regular_stage_file"
        ][0]
        capture_parents = {
            child: parent
            for parent in ast.walk(capture_regular)
            for child in ast.iter_child_nodes(parent)
        }

        def live_capture_call_count(base: str, name: str) -> int:
            return sum(
                1
                for node in ast.walk(capture_regular)
                if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name)
                and node.func.value.id == base
                and node.func.attr == name
                and not _ast_node_is_statically_dead(
                    node, capture_regular, capture_parents
                )
            )

        nofollow_calls = [
            node
            for node in ast.walk(capture_regular)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "getattr"
            and len(node.args) == 3
            and isinstance(node.args[0], ast.Name)
            and node.args[0].id == "os"
            and isinstance(node.args[1], ast.Constant)
            and node.args[1].value == "O_NOFOLLOW"
            and isinstance(node.args[2], ast.Constant)
            and node.args[2].value == 0
            and not _ast_node_is_statically_dead(
                node, capture_regular, capture_parents
            )
        ]
        identity_calls = [
            node
            for node in ast.walk(capture_regular)
            if isinstance(node, ast.Call)
            and isinstance(node.func, ast.Name)
            and node.func.id == "_stage_file_identity"
            and not _ast_node_is_statically_dead(
                node, capture_regular, capture_parents
            )
        ]
        regular_capture_is_pinned = (
            live_capture_call_count("path", "lstat") == 2
            and live_capture_call_count("stat", "S_ISREG") == 3
            and live_capture_call_count("os", "open") == 1
            and live_capture_call_count("os", "fstat") == 2
            and live_capture_call_count("os", "read") == 1
            and live_capture_call_count("os", "close") == 1
            and len(nofollow_calls) == 1
            and len(identity_calls) == 6
        )
        if not regular_capture_is_pinned:
            publisher_protocol_errors.append(
                "stage evidence must retain descriptor-pinned regular-file "
                "identity checks"
            )

        identity_function = publisher_functions["_stage_file_identity"][0]
        identity_source = (
            ast.get_source_segment(
                contract_publisher_source, identity_function
            )
            or ""
        )
        for field in (
            "value.st_dev",
            "value.st_ino",
            "stat.S_IFMT(value.st_mode)",
            "value.st_size",
            "value.st_mtime_ns",
        ):
            if identity_source.count(field) != 1:
                publisher_protocol_errors.append(
                    f"stage-file identity omits {field}"
                )

    if publisher_protocol_errors:
        raise AuditError(
            "Cupid Toolchain manifest author decision order differs: "
            f"{publisher_protocol_errors!r}"
        )

    if (
        missing_bootstrap_fragments
        or not windows_source_inputs_are_exact
        or publisher_control_values != [required_contract_control_inputs]
        or publisher_windows_values != [required_windows_publisher_inputs]
        or publisher_user_abi_values != [required_user_abi_inputs]
        or publisher_plan_sources is None
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point source freeze differs: "
            f"{missing_bootstrap_fragments!r}"
        )
    if publisher_path_statements != expected_publication_path_statements:
        raise AuditError(
            "Cupid Toolchain fixed-point publication input closure differs"
        )

    publication_paths = {
        root / path
        for path in (
            *publisher_plan_sources,
            *required_contract_control_inputs,
            *required_windows_publisher_inputs,
            *required_user_abi_inputs,
            "toolchain/tests/hosted_i386_runtime_contract.cc",
            "kernel/lang/as_elf.cc",
            "kernel/lang/as_elf.h",
            "toolchain/x86.cc",
        )
    }
    publication_paths.update((root / "toolchain").glob("*.h"))
    publication_paths.update((root / "toolchain/tests").glob("*.inc"))
    publication_paths.update((root / "toolchain/tests").glob("*.h"))
    publication_paths.update(
        (root / "toolchain/hosted/i386-linux/include").glob("*.h")
    )
    invalid_publication_paths = sorted(
        path.relative_to(root).as_posix()
        for path in publication_paths
        if not path.is_file() or path.is_symlink()
    )
    publication_inputs = tuple(
        sorted(path.relative_to(root).as_posix() for path in publication_paths)
    )
    if (
        invalid_publication_paths
        or publication_inputs != USER_SYSCALL_ABI_PUBLICATION_INPUTS
    ):
        raise AuditError(
            "Cupid Toolchain fixed-point publication input closure differs"
        )

    return {
        "status": "pass",
        "platform": "i386-linux",
        "tool_c_sources": len(expected_toolchain_sources),
        "compiler_c_sources": len(expected_compiler_sources),
        "strict_c_sources": sum(
            1
            for _name, _path, gnu in expected_toolchain_sources
            if not gnu
        ),
        "gnu_c_sources": sum(
            1
            for _name, _path, gnu in expected_toolchain_sources
            if gnu
        ),
        "include_roots": [
            {
                "path": "/toolchain",
                "forms": ["quoted", "angle"],
            },
            {
                "path": "/toolchain/hosted/i386-linux/include",
                "forms": ["angle"],
            },
        ],
        "link_objects": [
            {"tool": name, "objects": len(objects)}
            for name, objects in expected_toolchain_links
        ],
        "tool_images": len(expected_toolchain_links),
        "producer_tools": ["cupidc", "cupidasm", "cupidld"],
        "executed_tools": [
            name for name, _objects in expected_toolchain_links
        ],
        "compared_c_objects": len(expected_toolchain_sources),
        "compared_startup_objects": 1,
        "compared_tool_images": len(expected_toolchain_links),
        "help_cases": len(expected_toolchain_links),
        "success_behavior_cases": expected_behavior_matrix["success_cases"],
        "failure_behavior_cases": expected_behavior_matrix["failure_cases"],
        "windows_help_cases": 5,
        "windows_success_behavior_cases": 8,
        "windows_failure_behavior_cases": 9,
        "contract_manifest_inputs": len(publication_inputs),
        "source_head_capabilities": [
            "cupid.cupidbuild_guarded_object_transaction",
            "cupiddis.elf32_code_anchors",
            "cupidld.pe32_fixed_image",
            "cupidld.pe32_imports",
            "cupid.windows_cupidasm",
            "cupid.windows_cupidc",
            "cupid.windows_cupiddis",
            "cupid.windows_cupidld",
            "cupid.windows_cupidobj",
            "cupid.windows_runtime_contract",
            "cupid.windows_runtime_probe",
        ],
        "stages": [
            "generation-one",
            "stage-two",
            "stage-three",
            "stage-four",
        ],
        "checked_seed_source_root": "private-captured",
        "checked_seed_source_boundary_checks": 5,
        "checked_seed_publication": "complete-bundle",
    }


def _c_preprocessor_user_wrapper_flags(root: Path) -> str:
    wrapper = root / "tools" / "cupidc_production_compile.py"
    try:
        module = ast.parse(wrapper.read_text(encoding="utf-8"))
    except (OSError, SyntaxError) as error:
        raise AuditError(
            f"CupidC user profile wrapper is unavailable: {error}"
        ) from error
    value = None
    for statement in module.body:
        if (
            isinstance(statement, ast.Assign)
            and any(
                isinstance(target, ast.Name)
                and target.id == "USER_I386_ARGUMENTS"
                for target in statement.targets
            )
        ):
            try:
                value = ast.literal_eval(statement.value)
            except (ValueError, TypeError) as error:
                raise AuditError(
                    "CupidC USER_I386_ARGUMENTS is not a literal tuple"
                ) from error
            break
    expected = ("--freestanding", "-I", "/user")
    if value != expected:
        raise AuditError(
            "CupidC user wrapper profile differs from the checked contract: "
            f"expected={expected!r}, actual={value!r}"
        )
    # CupidC targets i386 directly. Feed that implicit target into the common
    # profile validator beside the wrapper's explicit freestanding and include
    # settings.
    return "-m32 -ffreestanding -I /user"


def _validate_c_preprocessor_make_profiles(root: Path, make: str) -> None:
    include_rows, macro_rows, forced_rows = (
        _c_preprocessor_profile_configuration()
    )
    manifest_roots: dict[str, list[str]] = collections.defaultdict(list)
    for profile, path, _ in include_rows:
        manifest_roots[profile].append(path)
    manifest_forced: dict[str, list[str]] = collections.defaultdict(list)
    for profile, path in forced_rows:
        manifest_forced[profile].append(path)

    root_values = _read_evaluated_make_variables(
        root,
        make,
        ("CFLAGS", "CFLAGS_DOOM", "CFLAGS_DOOM_TREE", "OPT"),
    )
    user_wrapper = root / "tools" / "cupidc_production_compile.py"
    if user_wrapper.is_file():
        user_variable = "USER_I386_ARGUMENTS"
        user_flags = _c_preprocessor_user_wrapper_flags(root)
    else:
        # Some focused audit fixtures model the former user Make profile and
        # intentionally omit the production wrapper. Keep those fixtures useful
        # for profile-drift diagnostics while requiring the wrapper in the real
        # production binding validator.
        user_variable = "CFLAGS"
        user_values = _read_evaluated_make_variables(
            root / "user", make, ("CFLAGS",)
        )
        user_flags = user_values["CFLAGS"]
    toolchain_values = _read_evaluated_make_variables(
        root / "toolchain", make, ("CPPFLAGS", "CFLAGS")
    )
    hosted_flags = (
        f"{toolchain_values['CPPFLAGS']} {toolchain_values['CFLAGS']}"
    )
    specifications = (
        ("KERNEL_I386", ".", "CFLAGS", root_values["CFLAGS"]),
        (
            "DOOM_COMPAT_I386",
            ".",
            "CFLAGS_DOOM",
            root_values["CFLAGS_DOOM"],
        ),
        (
            "DOOM_TREE_I386",
            ".",
            "CFLAGS_DOOM_TREE",
            root_values["CFLAGS_DOOM_TREE"],
        ),
        (
            "USER_I386",
            "user",
            user_variable,
            user_flags,
        ),
        (
            "HOSTED_TOOLCHAIN_64",
            "toolchain",
            "CPPFLAGS+CFLAGS",
            hosted_flags,
        ),
        (
            "HOSTED_KERNEL_BRIDGE_64",
            "toolchain",
            "CPPFLAGS+-I../kernel/lang+CFLAGS",
            f"{toolchain_values['CPPFLAGS']} -I../kernel/lang "
            f"{toolchain_values['CFLAGS']}",
        ),
    )
    make_owned_macro_names = {
        "KERNEL_I386": {"DEBUG"},
        "DOOM_COMPAT_I386": set(),
        "DOOM_TREE_I386": {
            "DEFAULT_SAVEGAMEDIR",
            "DOOM_PORT_CUPIDOS",
        },
        "USER_I386": set(),
        "HOSTED_TOOLCHAIN_64": set(),
        "HOSTED_KERNEL_BRIDGE_64": set(),
    }
    manifest_macros: dict[str, dict[str, str]] = collections.defaultdict(dict)
    for profile, name, replacement in macro_rows:
        manifest_macros[profile][name] = replacement
    for profile, directory, variable, expanded in specifications:
        includes, forced, defines, flags = _make_preprocessor_flags(
            expanded, variable
        )
        logical_includes = [
            _make_flag_logical_path(directory, path) for path in includes
        ]
        logical_forced = [
            _make_flag_logical_path(directory, path) for path in forced
        ]
        if logical_includes != manifest_roots[profile]:
            raise AuditError(
                f"CupidC profile {profile} include-root order differs from "
                f"Make {variable}: expected={manifest_roots[profile]!r}, "
                f"actual={logical_includes!r}"
            )
        if logical_forced != manifest_forced[profile]:
            raise AuditError(
                f"CupidC profile {profile} forced includes differ from Make "
                f"{variable}: expected={manifest_forced[profile]!r}, "
                f"actual={logical_forced!r}"
            )
        missing_manifest_macros = sorted(
            make_owned_macro_names[profile] - set(manifest_macros[profile])
        )
        if missing_manifest_macros:
            raise AuditError(
                f"CupidC profile {profile} omits Make-owned macro action(s): "
                f"{missing_manifest_macros!r}"
            )
        expected_defines = {
            name: manifest_macros[profile][name]
            for name in make_owned_macro_names[profile]
        }
        if defines != expected_defines:
            raise AuditError(
                f"CupidC profile {profile} configured macros differ from Make "
                f"{variable}: expected={expected_defines!r}, "
                f"actual={defines!r}"
            )
        hosted_profile = profile in _C_PP_HOSTED_PROFILES
        if hosted_profile:
            required_flags = {"-std=c11"}
        else:
            required_flags = {"-m32", "-ffreestanding"}
            if profile != "USER_I386":
                required_flags.update(("-msse2", "-nostdinc"))
        missing_flags = sorted(required_flags - flags)
        if missing_flags:
            raise AuditError(
                f"CupidC profile {profile} lost target flag(s) in Make "
                f"{variable}: {missing_flags!r}"
            )
        implicit_function_flag = (
            "-Wno-implicit-function-declaration" in flags
        )
        expects_implicit_functions = next(
            policy.implicit_function_declarations == "CTOOL_TRUE"
            for policy in _C_PP_PROFILE_ROWS
            if policy.name == profile
        )
        if implicit_function_flag != expects_implicit_functions:
            raise AuditError(
                f"CupidC profile {profile} implicit-function policy differs "
                f"from Make {variable}: expected="
                f"{expects_implicit_functions!r}, "
                f"actual={implicit_function_flag!r}"
            )
        profile_flags = (
            {"-std=c11"}
            if hosted_profile
            else _C_PP_I386_MODELED_FLAGS
        )
        unsupported = _c_preprocessor_unmodeled_flags(
            flags, profile_flags
        )
        if unsupported:
            raise AuditError(
                f"CupidC profile {profile} has unmodeled preprocessor flag(s) "
                f"in Make {variable}: {unsupported!r}"
            )

    opt_includes, opt_forced, opt_defines, opt_flags = (
        _make_preprocessor_flags(root_values["OPT"], "OPT")
    )
    opt_unmodeled = _c_preprocessor_unmodeled_flags(opt_flags)
    if opt_includes or opt_forced or opt_defines or opt_unmodeled:
        raise AuditError(
            "CupidC KERNEL_I386 OPT has preprocessor effects: "
            f"includes={opt_includes!r}, forced={opt_forced!r}, "
            f"defines={opt_defines!r}, unsupported={opt_unmodeled!r}"
        )


def _c_preprocessor_deferred_reason(path: str) -> str:
    external_header_units = {
        "/toolchain/ctool_host.cc",
        "/toolchain/cupidbuild.cc",
        "/toolchain/cupidbuild_host.cc",
        "/toolchain/cupidbuild_main.cc",
        "/toolchain/cupidasm_main.cc",
        "/toolchain/cupiddis_main.cc",
        "/toolchain/cupidc_main.cc",
        "/toolchain/cupidld_main.cc",
        "/toolchain/cupidobj_main.cc",
    }
    if path not in external_header_units and not path.startswith(
        "/toolchain/tests/"
    ):
        raise AuditError(
            f"CupidC hosted deferral is not an external runtime unit: {path}"
        )
    return "external system headers/runtime block hosted CupidC preprocessing"


def _c_preprocessor_require_exact_paths(
    label: str, actual: list[str], expected: tuple[str, ...]
) -> None:
    if tuple(sorted(actual)) != tuple(sorted(expected)):
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        raise AuditError(
            f"CupidC active preprocessing {label} changed; "
            f"missing={missing!r}, unexpected={unexpected!r}"
        )


def _validate_user_syscall_abi_transform(
    directory: str,
    transform: dict[str, object],
) -> None:
    expected_inputs = [
        *USER_SYSCALL_ABI_AUDIT_INPUTS,
        "user/Makefile",
    ]
    if (
        directory != "user"
        or transform.get("output") != "user/test-syscall-abi"
        or transform.get("operation") != "verify_user_syscall_abi"
        or transform.get("tools")
        != [
            "cupid_assembler",
            "cupid_c_compiler",
            "cupid_c_contract",
            "cupid_linker",
            "host_python",
        ]
    ):
        raise AuditError(
            "user syscall ABI verifier differs from its checked "
            "target, operation, or tool contract"
        )
    inputs = transform.get("inputs")
    if inputs != expected_inputs:
        actual_inputs = (
            inputs
            if isinstance(inputs, list)
            and all(isinstance(path, str) for path in inputs)
            else []
        )
        missing = sorted(set(expected_inputs) - set(actual_inputs))
        unexpected = sorted(set(actual_inputs) - set(expected_inputs))
        raise AuditError(
            "user syscall ABI verifier inputs changed; "
            f"missing={missing!r}, unexpected={unexpected!r}; "
            f"expected={expected_inputs!r}, actual={inputs!r}"
        )
    recipe = transform.get("recipe")
    if recipe != ["$(USER_SYSCALL_ABI)"]:
        raise AuditError(
            "user syscall ABI verifier recipe changed; "
            f"expected=['$(USER_SYSCALL_ABI)'], actual={recipe!r}"
        )
    markers = _c_preprocessor_recipe_markers(
        transform,
        {"USER_SYSCALL_ABI"},
    )
    if markers != collections.Counter({"USER_SYSCALL_ABI": 1}):
        raise AuditError(
            "user syscall ABI verifier recipe marker changed; "
            f"actual={dict(markers)!r}"
        )


def _toolchain_contract_cupidc_ownership_inputs(
    models: list[BuildModel],
) -> set[str]:
    evidence: set[str] = set()
    allowed = set(TOOLCHAIN_CONTRACT_LINUX_INPUTS)
    for model in models:
        if model.directory != "toolchain":
            continue
        for transform in model.transforms:
            if (
                transform.get("output")
                != "toolchain/build/cupidc-contracts/manifest.json"
                or transform.get("operation")
                != "generate_toolchain_manifest"
                or transform.get("tools")
                != [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ]
            ):
                continue
            inputs = transform.get("inputs")
            if not isinstance(inputs, list):
                continue
            evidence.update(
                path
                for path in inputs
                if isinstance(path, str)
                and path in allowed
                and _language(path) == "cupid_c"
            )
    return evidence


def _toolchain_contract_cupidasm_ownership_inputs(
    models: list[BuildModel],
) -> set[str]:
    evidence: set[str] = set()
    observed: set[str] = set()
    allowed = set(TOOLCHAIN_CONTRACT_CUPIDASM_OWNERSHIP_INPUTS)
    found_manifest = False
    for model in models:
        if model.directory != "toolchain":
            continue
        for transform in model.transforms:
            if (
                transform.get("output")
                != "toolchain/build/cupidc-contracts/manifest.json"
                or transform.get("operation")
                != "generate_toolchain_manifest"
                or transform.get("tools")
                != [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ]
            ):
                continue
            found_manifest = True
            inputs = transform.get("inputs")
            if not isinstance(inputs, list):
                continue
            observed.update(
                path
                for path in inputs
                if isinstance(path, str)
                and path.startswith("toolchain/hosted/")
                and Path(path).suffix.lower() in {".asm", ".s"}
            )
            evidence.update(
                path
                for path in inputs
                if isinstance(path, str) and path in allowed
            )
    if found_manifest and observed != allowed:
        missing = sorted(allowed - observed)
        unexpected = sorted(observed - allowed)
        raise AuditError(
            "Toolchain contract CupidASM startup ownership differs: "
            f"missing={missing!r}, unexpected={unexpected!r}"
        )
    return evidence


def _validate_native_user_tools_transform(
    directory: str,
    transform: dict[str, object],
) -> None:
    expected_recipe = [
        "$(MAKE) -C ../toolchain build/cupidc.exe build/cupidld.exe"
    ]
    if (
        directory != "user"
        or transform.get("output") != "user/native-user-tools"
        or transform.get("operation") != "recursive_make"
        or transform.get("tools") != ["make"]
        or transform.get("inputs") != []
        or transform.get("recipe") != expected_recipe
    ):
        raise AuditError(
            "native Windows user-tool prerequisite differs from its checked "
            "recursive build contract"
        )


def _read_cupidc_kernel_wrapper(root: Path) -> tuple[str, ast.Module]:
    wrapper = root / "tools" / "cupidc_kernel_compile.py"
    try:
        source = wrapper.read_text(encoding="utf-8")
        return source, ast.parse(source, filename=str(wrapper))
    except (OSError, SyntaxError) as error:
        raise AuditError(
            f"CupidObj profile manifest wrapper is unavailable: {error}"
        ) from error


def _cupidobj_profile_argument_sets(
    tree: ast.Module,
) -> dict[str, tuple[str, ...]]:
    profile_names = (
        "DOOM_COMPAT_I386_ARGUMENTS",
        "DOOM_TREE_I386_ARGUMENTS",
    )
    arguments: dict[str, tuple[str, ...]] = {}
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name) or target.id not in profile_names:
            continue
        if target.id in arguments or not isinstance(statement.value, ast.Tuple):
            raise AuditError(
                "CupidObj profile manifest wrapper profile declarations "
                f"changed: {target.id} is not one literal tuple"
            )
        expanded: list[str] = []
        for item in statement.value.elts:
            if isinstance(item, ast.Constant) and isinstance(item.value, str):
                expanded.append(item.value)
                continue
            if (
                isinstance(item, ast.Starred)
                and isinstance(item.value, ast.Name)
                and item.value.id in arguments
            ):
                expanded.extend(arguments[item.value.id])
                continue
            raise AuditError(
                "CupidObj profile manifest wrapper profile declarations "
                f"changed: {target.id} contains a dynamic argument"
            )
        arguments[target.id] = tuple(expanded)
    missing = [name for name in profile_names if name not in arguments]
    if missing:
        raise AuditError(
            "CupidObj profile manifest wrapper profile declarations "
            f"changed: missing {missing!r}"
        )
    return arguments


def _cupidobj_profile_include_roots(
    root: Path,
    tree: ast.Module,
) -> tuple[Path, ...]:
    try:
        resolved_root = root.resolve(strict=True)
    except OSError as error:
        raise AuditError(
            f"CupidObj profile manifest root is unavailable: {error}"
        ) from error
    include_names = set()
    for profile, arguments in _cupidobj_profile_argument_sets(tree).items():
        for index, argument in enumerate(arguments):
            if argument != "-I":
                continue
            if index + 1 == len(arguments):
                raise AuditError(
                    "CupidObj profile manifest wrapper profile declarations "
                    f"changed: {profile} ends with -I"
                )
            include_name = arguments[index + 1]
            normalized = posixpath.normpath(include_name)
            if (
                not include_name.startswith("/")
                or "\\" in include_name
                or normalized != include_name
                or include_name == "/"
            ):
                raise AuditError(
                    "CupidObj profile manifest wrapper has an invalid "
                    f"include root: {include_name!r}"
                )
            include_names.add(include_name[1:])

    include_roots = []
    for include_name in sorted(include_names):
        include_root = resolved_root / include_name
        try:
            resolved_include = include_root.resolve(strict=True)
            resolved_include.relative_to(resolved_root)
        except (OSError, ValueError) as error:
            raise AuditError(
                "CupidObj profile manifest include root is unavailable: "
                f"/{include_name}"
            ) from error
        if not resolved_include.is_dir():
            raise AuditError(
                "CupidObj profile manifest include root is not a directory: "
                f"/{include_name}"
            )
        include_roots.append(resolved_include)
    return tuple(include_roots)


def _path_is_link_or_junction(path: Path) -> bool:
    if path.is_symlink():
        return True
    isjunction = getattr(os.path, "isjunction", None)
    return bool(isjunction is not None and isjunction(path))


def _cupidobj_profile_manifest_expected_inputs(root: Path) -> list[str]:
    _source, tree = _read_cupidc_kernel_wrapper(root)
    resolved_root = root.resolve(strict=True)
    profile_inputs = set()
    try:
        for include_root in _cupidobj_profile_include_roots(root, tree):
            for path in include_root.rglob("*"):
                relative_parts = path.relative_to(include_root).parts
                if any(part.startswith(".") for part in relative_parts):
                    continue
                if _path_is_link_or_junction(path):
                    raise AuditError(
                        "CupidObj profile manifest input may not be a link "
                        f"or junction: {path.relative_to(resolved_root).as_posix()}"
                    )
                if not path.is_file() or path.suffix not in {".h", ".inc"}:
                    continue
                try:
                    relative = path.resolve(strict=True).relative_to(
                        resolved_root
                    )
                except (OSError, ValueError) as error:
                    raise AuditError(
                        "CupidObj profile manifest input escapes the "
                        f"repository: {path}"
                    ) from error
                profile_inputs.add(relative.as_posix())
    except OSError as error:
        raise AuditError(
            f"CupidObj profile manifest inputs are unavailable: {error}"
        ) from error
    return [
        *sorted(profile_inputs),
        *_CUPIDOBJ_PROFILE_MANIFEST_CONTROL_INPUTS,
    ]


def _normalized_make_lines(source: str) -> list[str]:
    logical_source = re.sub(r"\\\r?\n[ \t]*", " ", source)
    return [
        " ".join(line.split())
        for line in logical_source.splitlines()
        if line.strip()
    ]


def _is_cupidobj_profile_manifest_production_root(root: Path) -> bool:
    return (
        all(
            (root / relative).is_file()
            for relative in _CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_FILES
        )
        and all(
            (root / relative).is_dir()
            for relative in _CUPIDOBJ_PROFILE_MANIFEST_PRODUCTION_DIRECTORIES
        )
    )


def _validate_cupidobj_profile_manifest_make_source(source: str) -> None:
    lines = _normalized_make_lines(source)
    required_lines = (
        "DOOM_CUPIDC_INPUT_MANIFEST := "
        "build/bootstrap/doom-cupidc-inputs.json",
        "$(DOOM_CUPIDC_INPUT_MANIFEST): FORCE $(DOOM_CUPIDC_HEADERS) "
        "$(CHECKED_SEED_INPUTS) tools/cupidc_kernel_compile.py",
        "$(PYTHON) tools/cupidc_kernel_compile.py --root . "
        "--manifest $(PRODUCTION_SEED_MANIFEST) "
        "--write-profile-input-manifest $@",
    )
    changed = [line for line in required_lines if lines.count(line) != 1]
    if changed:
        raise AuditError(
            "CupidObj profile manifest Make contract changed; expected one "
            f"copy of each line: {changed!r}"
        )


def _cupidobj_wrapper_structure_error(detail: str) -> None:
    raise AuditError(
        f"CupidObj profile manifest wrapper structure changed: {detail}"
    )


def _single_cupidobj_wrapper_node(
    publisher: ast.AST,
    node_type: type[ast.AST],
    source: str,
    description: str,
) -> ast.AST:
    matches = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, node_type) and ast.unparse(node) == source
    ]
    if len(matches) != 1:
        _cupidobj_wrapper_structure_error(
            f"expected one {description}, found {len(matches)}"
        )
    return matches[0]


def _cupidobj_wrapper_nodes(
    publisher: ast.AST,
    node_type: type[ast.AST],
    source: str,
    description: str,
    count: int,
) -> list[ast.AST]:
    matches = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, node_type) and ast.unparse(node) == source
    ]
    if len(matches) != count:
        _cupidobj_wrapper_structure_error(
            f"expected {count} {description}, found {len(matches)}"
        )
    return sorted(
        matches,
        key=lambda node: (node.lineno, node.col_offset),
    )


def _single_cupidobj_wrapper_if(
    publisher: ast.AST,
    condition: str,
    description: str,
) -> ast.If:
    matches = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, ast.If) and ast.unparse(node.test) == condition
    ]
    if len(matches) != 1:
        _cupidobj_wrapper_structure_error(
            f"expected one {description}, found {len(matches)}"
        )
    return matches[0]


def _ast_contains(parent: ast.AST, child: ast.AST) -> bool:
    return any(node is child for node in ast.walk(parent))


def _ast_statements_contain(
    statements: list[ast.stmt],
    child: ast.AST,
) -> bool:
    return any(_ast_contains(statement, child) for statement in statements)


def _ast_node_is_statically_dead(
    node: ast.AST,
    publisher: ast.AST,
    parents: dict[ast.AST, ast.AST],
) -> bool:
    current = node
    while current is not publisher and current in parents:
        parent = parents[current]
        if isinstance(
            parent,
            (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda),
        ) and parent is not publisher:
            return True
        if isinstance(parent, ast.If) and isinstance(
            parent.test,
            ast.Constant,
        ) and isinstance(parent.test.value, bool):
            if (
                not parent.test.value
                and current in parent.body
                or parent.test.value
                and current in parent.orelse
            ):
                return True
        if (
            isinstance(parent, ast.While)
            and isinstance(parent.test, ast.Constant)
            and parent.test.value is False
            and current in parent.body
        ):
            return True
        current = parent
    return False


def _if_raises_kernel_compile_error(statement: ast.AST) -> bool:
    if not isinstance(statement, ast.If) or len(statement.body) != 1:
        return False
    raised = statement.body[0]
    return (
        isinstance(raised, ast.Raise)
        and isinstance(raised.exc, ast.Call)
        and isinstance(raised.exc.func, ast.Name)
        and raised.exc.func.id == "KernelCompileError"
    )


def _if_returns_false(statement: ast.AST) -> bool:
    if not isinstance(statement, ast.If) or len(statement.body) != 1:
        return False
    returned = statement.body[0]
    return (
        isinstance(returned, ast.Return)
        and isinstance(returned.value, ast.Constant)
        and returned.value.value is False
    )


def _validate_cupidobj_profile_manifest_wrapper(root: Path) -> None:
    _source, tree = _read_cupidc_kernel_wrapper(root)
    functions = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "write_profile_input_manifest"
    ]
    if len(functions) != 1:
        _cupidobj_wrapper_structure_error(
            f"expected one publisher, found {len(functions)}"
        )
    publisher = functions[0]

    output_directory = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "output_directory = _capture_profile_directory(resolved.parent, "
        "'profile output directory')",
        "output-directory capture",
    )
    publication_lock = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "publication_lock = _acquire_profile_manifest_lock(resolved)",
        "publication lock",
    )
    checked_seed = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "checked_seed = verify_seed_inputs(manifest_path)",
        "initial checked-seed verification",
    )
    capture = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "capture = _capture_profile_inputs(root)",
        "profile-input capture",
    )
    document = _single_cupidobj_wrapper_node(
        publisher,
        ast.Call,
        "_profile_input_document(root, capture)",
        "Python oracle document",
    )
    frozen_seed = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "frozen_seed = freeze_seed_inputs(manifest_path, private / "
        "'checked-seed')",
        "private checked-seed freeze",
    )
    frozen_seed_check = _single_cupidobj_wrapper_if(
        publisher,
        "frozen_seed.manifest_sha256 != checked_seed.manifest_sha256",
        "frozen-seed identity gate",
    )
    snapshot = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "snapshot_payload = _profile_snapshot_bytes(root, capture)",
        "profile snapshot",
    )
    snapshot_capture = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "snapshot_capture = _capture_profile_file(snapshot, 'CupidObj "
        "profile snapshot')",
        "profile-snapshot capture",
    )
    arguments = _single_cupidobj_wrapper_node(
        publisher,
        ast.AnnAssign,
        "arguments: tuple[str | Path, ...] = ('profile-manifest', "
        "snapshot.resolve(), '-o', candidate.resolve(strict=False))",
        "CupidObj profile-manifest argument vector",
    )
    seed_run = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "result = run_seed_tool(manifest_path, private, 'cupidobj', "
        "arguments, timeout=60, frozen_seed=frozen_seed)",
        "checked CupidObj invocation",
    )
    snapshot_recheck = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_require_profile_file_unchanged(snapshot_capture, 'CupidObj "
        "profile snapshot')",
        "profile-snapshot recheck",
    )
    checked_output = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "checked_output = _capture_profile_file(candidate, 'checked "
        "CupidObj profile manifest output')",
        "CupidObj candidate capture",
    )
    candidate_capture = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "candidate_capture = checked_output",
        "verified candidate selection",
    )
    parity_gate = _single_cupidobj_wrapper_if(
        publisher,
        "candidate_capture.payload != oracle_payload",
        "CupidObj/Python parity gate",
    )
    live_seed = _single_cupidobj_wrapper_node(
        publisher,
        ast.Assign,
        "live_seed = verify_seed_inputs(manifest_path)",
        "live checked-seed verification",
    )
    live_seed_check = _single_cupidobj_wrapper_if(
        publisher,
        "live_seed.manifest_sha256 != checked_seed.manifest_sha256",
        "live checked-seed identity gate",
    )
    profile_recheck = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_require_profile_inputs_unchanged(root, capture)",
        "live profile-input recheck",
    )
    directory_rechecks = _cupidobj_wrapper_nodes(
        publisher,
        ast.Expr,
        "_require_profile_directory_unchanged(output_directory)",
        "output-directory rechecks",
        3,
    )
    output_recheck = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_require_profile_output_unchanged(resolved, initial_output)",
        "live output recheck",
    )
    unchanged_gate = _single_cupidobj_wrapper_if(
        publisher,
        "initial_output is not None and initial_output.payload == "
        "candidate_capture.payload",
        "unchanged-output gate",
    )
    publication = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_replace_profile_candidate_with_retry(candidate_capture, resolved, "
        "initial_output, output_directory)",
        "checked candidate publication",
    )
    release = _single_cupidobj_wrapper_node(
        publisher,
        ast.Expr,
        "_release_profile_manifest_lock(publication_lock)",
        "publication-lock release",
    )
    checked_branch = _single_cupidobj_wrapper_if(
        publisher,
        "manifest_path is not None",
        "checked-seed branch",
    )
    manifest_branch = _single_cupidobj_wrapper_if(
        publisher,
        "manifest_path is None",
        "checked CupidObj branch",
    )

    parents = {
        child: parent
        for parent in ast.walk(publisher)
        for child in ast.iter_child_nodes(parent)
    }
    ordered_nodes = (
        output_directory,
        publication_lock,
        directory_rechecks[0],
        checked_seed,
        capture,
        document,
        frozen_seed,
        frozen_seed_check,
        snapshot,
        snapshot_capture,
        arguments,
        seed_run,
        directory_rechecks[1],
        snapshot_recheck,
        checked_output,
        candidate_capture,
        parity_gate,
        live_seed,
        live_seed_check,
        profile_recheck,
        directory_rechecks[2],
        output_recheck,
        unchanged_gate,
        publication,
        release,
    )
    if any(
        _ast_node_is_statically_dead(node, publisher, parents)
        for node in ordered_nodes
    ):
        _cupidobj_wrapper_structure_error(
            "a required safety step is in statically dead code"
        )
    locations = [
        (node.lineno, node.col_offset) for node in ordered_nodes
    ]
    if locations != sorted(locations) or len(set(locations)) != len(locations):
        _cupidobj_wrapper_structure_error(
            "the safety steps are not in their checked order"
        )

    outer_tries = [
        statement
        for statement in publisher.body
        if isinstance(statement, ast.Try)
        and _ast_statements_contain(statement.finalbody, release)
    ]
    temporary_scopes = [
        node
        for node in ast.walk(publisher)
        if isinstance(node, ast.With)
        and any(
            ast.unparse(item.context_expr)
            == "tempfile.TemporaryDirectory(prefix=f'.{resolved.name}.profile-', "
            "dir=resolved.parent)"
            for item in node.items
        )
    ]
    if len(outer_tries) != 1 or len(temporary_scopes) != 1:
        _cupidobj_wrapper_structure_error(
            "the publication try/finally or private workspace changed"
        )
    outer_try = outer_tries[0]
    temporary_scope = temporary_scopes[0]
    if (
        output_directory not in publisher.body
        or publication_lock not in publisher.body
        or directory_rechecks[0] not in outer_try.body
        or checked_branch not in outer_try.body
        or temporary_scope not in outer_try.body
        or release not in outer_try.finalbody
        or not _ast_statements_contain(checked_branch.body, checked_seed)
        or manifest_branch not in temporary_scope.body
        or not _ast_statements_contain(
            manifest_branch.orelse,
            seed_run,
        )
        or not all(
            _ast_statements_contain(manifest_branch.orelse, node)
            for node in (
                frozen_seed,
                frozen_seed_check,
                snapshot,
                snapshot_capture,
                arguments,
                directory_rechecks[1],
                snapshot_recheck,
                checked_output,
                candidate_capture,
                parity_gate,
                live_seed,
                live_seed_check,
            )
        )
        or not all(
            node in temporary_scope.body
            for node in (
                profile_recheck,
                directory_rechecks[2],
                output_recheck,
                unchanged_gate,
                publication,
            )
        )
    ):
        _cupidobj_wrapper_structure_error(
            "a safety step moved outside its checked control-flow scope"
        )
    if not all(
        _if_raises_kernel_compile_error(node)
        for node in (frozen_seed_check, parity_gate, live_seed_check)
    ) or not _if_returns_false(unchanged_gate):
        _cupidobj_wrapper_structure_error(
            "a safety gate no longer stops or preserves publication"
        )


def _validate_cupidobj_profile_manifest_delivery(
    root: Path,
    transforms: list[dict[str, object]],
) -> None:
    makefile = root / "Makefile"
    try:
        make_source = makefile.read_text(encoding="utf-8")
    except OSError as error:
        raise AuditError(f"could not read root Makefile: {error}") from error
    deliveries = [
        transform
        for transform in transforms
        if transform.get("output") == _CUPIDOBJ_PROFILE_MANIFEST_OUTPUT
    ]
    if not deliveries and not _is_cupidobj_profile_manifest_production_root(
        root
    ):
        return

    _validate_cupidobj_profile_manifest_make_source(make_source)
    if len(deliveries) != 1:
        raise AuditError(
            "CupidObj profile manifest delivery must appear exactly once; "
            f"found {len(deliveries)}"
        )
    delivery = deliveries[0]
    if (
        delivery.get("operation") != "generate_profile_manifest"
        or delivery.get("tools") != ["cupid_object", "host_python"]
        or delivery.get("recipe") != _CUPIDOBJ_PROFILE_MANIFEST_RECIPE
    ):
        raise AuditError(
            "CupidObj profile manifest delivery differs from its checked "
            "operation, tools, or recipe"
        )
    expected_inputs = _cupidobj_profile_manifest_expected_inputs(root)
    inputs = delivery.get("inputs")
    if inputs != expected_inputs:
        actual_inputs = inputs if isinstance(inputs, list) else []
        missing = [path for path in expected_inputs if path not in actual_inputs]
        unexpected = [
            path for path in actual_inputs if path not in expected_inputs
        ]
        raise AuditError(
            "CupidObj profile manifest inputs changed; "
            f"missing={missing!r}, unexpected={unexpected!r}, "
            f"order_changed={not missing and not unexpected}"
        )
    _validate_cupidobj_profile_manifest_wrapper(root)


_CUPIDOBJ_INSTALL_SOURCE_DELIVERIES = {
    "kernel/util/bin_programs_gen.cc": (
        "$(CUPIDOBJ) install-source bin --bin $(BIN_CC_SRCS) "
        "--headers $(BIN_HDR_SRCS) --browser $(BROWSER_SUB_SRCS) -o $@",
        collections.Counter(
            {
                "CUPIDOBJ": 1,
                "BIN_CC_SRCS": 1,
                "BIN_HDR_SRCS": 1,
                "BROWSER_SUB_SRCS": 1,
            }
        ),
    ),
    "kernel/util/docs_programs_gen.cc": (
        "$(CUPIDOBJ) install-source docs --ctxt $(DOC_CTXT_SRCS) "
        "--doc-assets $(DOC_ASSET_SRCS) --home-assets "
        "$(HOME_ASSET_SRCS) -o $@",
        collections.Counter(
            {
                "CUPIDOBJ": 1,
                "DOC_CTXT_SRCS": 1,
                "DOC_ASSET_SRCS": 1,
                "HOME_ASSET_SRCS": 1,
            }
        ),
    ),
    "kernel/util/demos_programs_gen.cc": (
        "$(CUPIDOBJ) install-source demos --demos $(DEMO_ASM_SRCS) -o $@",
        collections.Counter({"CUPIDOBJ": 1, "DEMO_ASM_SRCS": 1}),
    ),
}


def _cupidobj_install_source_expected_content(
    transforms: list[dict[str, object]],
) -> dict[str, set[str]]:
    wrapped_text: set[str] = set()
    wrapped_binary: set[str] = set()
    for transform in transforms:
        operation = transform.get("operation")
        if operation not in {
            "wrap_text_as_elf32_relocatable",
            "wrap_binary_as_elf32_relocatable",
        }:
            continue
        inputs = transform.get("inputs")
        if not isinstance(inputs, list):
            continue
        destination = (
            wrapped_text
            if operation == "wrap_text_as_elf32_relocatable"
            else wrapped_binary
        )
        destination.update(
            path for path in inputs if isinstance(path, str)
        )

    bin_content = {
        path
        for path in wrapped_text
        if (
            path.startswith("bin/")
            and path.endswith((".cc", ".h"))
        )
    }
    documents = {
        path
        for path in wrapped_text
        if path.startswith("cupidos-txt/") and path.endswith(".CTXT")
    }
    demos = {
        path
        for path in wrapped_text
        if path.startswith("demos/") and path.endswith(".asm")
    }
    assets = {
        "image.bmp",
        "snail.bmp",
        "test.png",
        "file_example_JPG_1MB.jpg",
    }
    missing_assets = sorted(assets - wrapped_binary)
    if missing_assets:
        raise AuditError(
            "CupidObj install-source asset wraps changed; missing="
            f"{missing_assets!r}"
        )
    return {
        "kernel/util/bin_programs_gen.cc": bin_content,
        "kernel/util/docs_programs_gen.cc": documents | assets,
        "kernel/util/demos_programs_gen.cc": demos,
    }


def _validate_cupidobj_install_source_delivery(
    directory: str,
    transform: dict[str, object],
    expected_content: set[str],
) -> None:
    output = str(transform.get("output", "<unknown>"))
    contract = _CUPIDOBJ_INSTALL_SOURCE_DELIVERIES.get(output)
    expected_recipe = [contract[0]] if contract is not None else None
    if (
        directory != "."
        or contract is None
        or transform.get("operation") != "generate_install_source"
        or transform.get("tools") != ["cupid_object", "host_python"]
        or transform.get("recipe") != expected_recipe
    ):
        raise AuditError(
            "CupidObj install-source delivery differs from its checked "
            "target, operation, tool, or recipe contract"
        )
    inputs = transform.get("inputs")
    if not isinstance(inputs, list):
        raise AuditError("CupidObj install-source delivery inputs are absent")
    required_inputs = {
        "Makefile",
        "tools/bootstrap_toolchain.py",
        *WINDOWS_PRODUCTION_SEED_INPUTS,
    }
    missing_inputs = sorted(required_inputs - set(inputs))
    if missing_inputs:
        raise AuditError(
            "CupidObj install-source delivery lost checked inputs: "
            f"{missing_inputs!r}"
        )
    content_inputs = [
        path for path in inputs if path not in required_inputs
    ]
    if len(content_inputs) != len(set(content_inputs)):
        raise AuditError(
            "CupidObj install-source delivery has duplicate content inputs"
        )
    content_set = set(content_inputs)
    if output == "kernel/util/bin_programs_gen.cc":
        programs = {
            path
            for path in content_set
            if path.startswith("bin/")
            and path.count("/") == 1
            and path.endswith(".cc")
        }
        headers = {
            path
            for path in content_set
            if path.startswith("bin/")
            and path.count("/") == 1
            and path.endswith(".h")
        }
        browser = {
            path
            for path in content_set
            if path.startswith("bin/browser/") and path.endswith(".cc")
        }
        expected_headers = {"bin/fat16.h", "bin/shell.h"}
        content_valid = (
            len(programs) == 108
            and headers == expected_headers
            and len(browser) == 22
            and content_set == programs | headers | browser
        )
    elif output == "kernel/util/docs_programs_gen.cc":
        documents = {
            path
            for path in content_set
            if path.startswith("cupidos-txt/") and path.endswith(".CTXT")
        }
        assets = {
            "image.bmp",
            "snail.bmp",
            "test.png",
            "file_example_JPG_1MB.jpg",
        }
        content_valid = (
            len(documents) == 19
            and assets.issubset(content_set)
            and content_set == documents | assets
        )
    else:
        demos = {
            path
            for path in content_set
            if path.startswith("demos/") and path.endswith(".asm")
        }
        content_valid = len(demos) == 22 and content_set == demos
    if not content_valid:
        raise AuditError(
            "CupidObj install-source delivery content inputs changed for "
            f"{output}: {sorted(content_set)!r}"
        )
    if content_set != expected_content:
        missing_content = sorted(expected_content - content_set)
        unexpected_content = sorted(content_set - expected_content)
        raise AuditError(
            "CupidObj install-source delivery content inputs changed for "
            f"{output}: missing={missing_content!r}, "
            f"unexpected={unexpected_content!r}"
        )
    expected_markers = contract[1]
    markers = _c_preprocessor_recipe_markers(
        transform, set(expected_markers)
    )
    if markers != expected_markers:
        raise AuditError(
            "CupidObj install-source delivery recipe markers changed; "
            f"actual={dict(markers)!r}"
        )


def _c_preprocessor_active_cases_manifest(
    audit: dict[str, object],
) -> CPreprocessorActiveCasesManifest:
    sources = audit.get("sources")
    if not isinstance(sources, list):
        raise AuditError("CupidC active preprocessing source inventory is absent")
    source_entries: dict[str, dict[str, object]] = {}
    for entry in sources:
        if not isinstance(entry, dict) or "path" not in entry:
            raise AuditError(
                "CupidC active preprocessing source inventory is malformed"
            )
        path = str(entry["path"])
        if path in source_entries:
            raise AuditError(
                f"CupidC active preprocessing source is duplicated: {path}"
            )
        source_entries[path] = entry

    root_build = audit.get("build")
    supplemental = audit.get("supplemental_builds")
    if not isinstance(root_build, dict) or not isinstance(supplemental, list):
        raise AuditError("CupidC active preprocessing build inventory is absent")
    builds: list[dict[str, object]] = [root_build]
    for build in supplemental:
        if not isinstance(build, dict):
            raise AuditError(
                "CupidC active preprocessing supplemental build is malformed"
            )
        builds.append(build)

    active_by_profile: dict[str, list[str]] = {
        profile.name: [] for profile in _C_PP_PROFILE_ROWS
    }
    generated: list[str] = []
    include_only: list[str] = []
    non_roots: list[str] = []
    deferred_hosted: list[str] = []
    seen_directories: set[str] = set()

    for build in builds:
        directory = str(build.get("directory", ""))
        if directory in seen_directories:
            raise AuditError(
                f"CupidC active preprocessing build is duplicated: {directory!r}"
            )
        seen_directories.add(directory)
        transforms = build.get("transforms")
        if not isinstance(transforms, list):
            raise AuditError(
                f"CupidC active preprocessing transforms are absent for {directory!r}"
            )
        has_install_source_delivery = any(
            isinstance(transform, dict)
            and transform.get("output")
            in _CUPIDOBJ_INSTALL_SOURCE_DELIVERIES
            for transform in transforms
        )
        install_source_content = (
            _cupidobj_install_source_expected_content(transforms)
            if directory == "." and has_install_source_delivery
            else {}
        )
        for transform_value in sorted(
            transforms,
            key=lambda item: str(item.get("output", ""))
            if isinstance(item, dict)
            else "",
        ):
            if not isinstance(transform_value, dict):
                raise AuditError(
                    f"CupidC active preprocessing transform is malformed in "
                    f"{directory!r}"
                )
            transform = transform_value
            operation = str(transform.get("operation", ""))
            tools = transform.get("tools")
            output = str(transform.get("output", ""))
            if (
                directory == "toolchain"
                and output
                == "toolchain/build/cupidc-contracts/manifest.json"
            ):
                expected_tools = [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ]
                inputs = transform.get("inputs")
                if (
                    operation != "generate_toolchain_manifest"
                    or tools != expected_tools
                    or not isinstance(inputs, list)
                    or not all(isinstance(path, str) for path in inputs)
                ):
                    raise AuditError(
                        "CupidC contract cohort transform differs from "
                        "the checked manifest-author build contract"
                    )
                closure_roots = [
                    _c_preprocessor_logical_path(path)
                    for path in inputs
                    if _language(path) in {"c", "cupid_c"}
                    and path not in USER_SYSCALL_ABI_SOURCE_INPUTS
                    and path
                    != "toolchain/tests/artifact_size_policy_contract.cc"
                ]
                expected_closure = (
                    _C_PP_HOSTED_I386_STRICT_CASES
                    + _C_PP_HOSTED_I386_GNU_CASES
                    + _C_PP_TOOLCHAIN_CONTRACT_CASES
                    + tuple(
                        path
                        for path in _C_PP_HOSTED_I386_WINDOWS_CASES
                        if path not in _C_PP_HOSTED_I386_STRICT_CASES
                    )
                )
                _c_preprocessor_require_exact_paths(
                    "toolchain contract closure",
                    closure_roots,
                    expected_closure,
                )
                required_inputs = set(TOOLCHAIN_CONTRACT_LINUX_INPUTS) | {
                    "link.ld",
                    "toolchain/hosted/i386-linux/start.asm",
                    "toolchain/hosted/i386-windows/start.asm",
                    "toolchain/Makefile",
                    "tools/bootstrap_toolchain.py",
                    "tools/cupidc_toolchain_contracts.py",
                }
                missing_inputs = sorted(required_inputs - set(inputs))
                if missing_inputs:
                    raise AuditError(
                        "CupidC contract cohort lost checked inputs: "
                        f"{missing_inputs!r}"
                    )
                recipe = transform.get("recipe")
                normalized_recipe = " ".join(
                    token
                    for token in "\n".join(
                        recipe if isinstance(recipe, list) else []
                    ).split()
                    if token != "\\"
                )
                expected_recipe = (
                    "$(PYTHON) ../tools/cupidc_toolchain_contracts.py build "
                    "--root .. --manifest "
                    "../bootstrap/seeds/i386-linux/manifest.json "
                    "--output $(CONTRACT_DIR)"
                )
                if (
                    not isinstance(recipe, list)
                    or not all(isinstance(line, str) for line in recipe)
                    or normalized_recipe != expected_recipe
                ):
                    raise AuditError(
                        "CupidC contract cohort recipe no longer invokes the "
                        "checked fixed-point builder"
                    )
                for logical in expected_closure:
                    entry = source_entries.get(logical[1:])
                    if entry is None or entry.get("origin") != "tracked":
                        raise AuditError(
                            "CupidC hosted i386 closure source is not tracked: "
                            f"{logical}"
                        )
                active_by_profile["HOSTED_I386_LINUX"].extend(
                    _C_PP_HOSTED_I386_STRICT_CASES
                    + tuple(
                        path
                        for path in _C_PP_TOOLCHAIN_CONTRACT_CASES
                        if path not in _C_PP_HOSTED_BRIDGE_CASES
                        and path
                        != "/toolchain/tests/hosted_i386_windows_contract.cc"
                    )
                )
                active_by_profile["HOSTED_I386_WINDOWS"].extend(
                    _C_PP_HOSTED_I386_WINDOWS_CASES
                )
                active_by_profile["FREESTANDING_I386"].append(
                    "/toolchain/tests/hosted_i386_windows_contract.cc"
                )
                active_by_profile["HOSTED_I386_KERNEL_BRIDGE"].extend(
                    sorted(_C_PP_HOSTED_BRIDGE_CASES)
                )
                active_by_profile["HOSTED_I386_LINUX_GNU"].extend(
                    _C_PP_HOSTED_I386_GNU_CASES
                )
                continue
            if operation == "verify_toolchain_manifest":
                expected_tools = [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ]
                inputs = transform.get("inputs")
                if (
                    directory != "toolchain"
                    or output != "toolchain/all"
                    or tools != expected_tools
                    or transform.get("recipe")
                    != ["$(TOOLCHAIN_MANIFEST_CONTRACT)"]
                    or not isinstance(inputs, list)
                    or not all(isinstance(path, str) for path in inputs)
                    or len(inputs)
                    != len(TOOLCHAIN_MANIFEST_CONTRACT_TRANSFORM_INPUTS)
                    or set(inputs)
                    != TOOLCHAIN_MANIFEST_CONTRACT_TRANSFORM_INPUTS
                ):
                    raise AuditError(
                        "Cupid Toolchain manifest contract transform differs "
                        "from the checked build contract"
                    )
                contract_source = (
                    "toolchain/tests/toolchain_manifest_contract.cc"
                )
                entry = source_entries.get(contract_source)
                if entry is None or entry.get("origin") != "tracked":
                    raise AuditError(
                        "Cupid Toolchain manifest contract source is not tracked"
                    )
                continue
            if operation == "verify_artifact_size_policy":
                expected_tools = [
                    "cupid_assembler",
                    "cupid_c_compiler",
                    "cupid_c_contract",
                    "cupid_linker",
                    "host_python",
                ]
                inputs = transform.get("inputs")
                if (
                    directory != "."
                    or output != "verify-artifact-sizes"
                    or tools != expected_tools
                    or transform.get("recipe")
                    != ARTIFACT_SIZE_CONTRACT_RECIPE
                    or not isinstance(inputs, list)
                    or not all(isinstance(path, str) for path in inputs)
                    or len(inputs) != len(ARTIFACT_SIZE_CONTRACT_TRANSFORM_INPUTS)
                    or set(inputs) != ARTIFACT_SIZE_CONTRACT_TRANSFORM_INPUTS
                ):
                    raise AuditError(
                        "Cupid artifact-size contract transform differs from "
                        "the checked build contract"
                    )
                contract_source = (
                    "toolchain/tests/artifact_size_policy_contract.cc"
                )
                entry = source_entries.get(contract_source)
                if entry is None or entry.get("origin") != "tracked":
                    raise AuditError(
                        "Cupid artifact-size contract source is not tracked"
                    )
                active_by_profile["HOSTED_I386_LINUX"].append(
                    "/" + contract_source
                )
                continue
            if operation == "verify_user_syscall_abi":
                _validate_user_syscall_abi_transform(directory, transform)
                continue
            if (
                directory == "user"
                and output == "user/native-user-tools"
            ):
                _validate_native_user_tools_transform(directory, transform)
                continue
            if (
                directory == "."
                and output in _CUPIDOBJ_INSTALL_SOURCE_DELIVERIES
            ):
                _validate_cupidobj_install_source_delivery(
                    directory,
                    transform,
                    install_source_content[output],
                )
                continue
            if operation in {
                "compile_c_to_elf32_object",
                "compile_c_to_host_object",
            }:
                allowed_compile_tools = (
                    ["host_c_compiler"],
                    ["cupid_c_compiler", "host_python"],
                )
                if tools not in allowed_compile_tools:
                    raise AuditError(
                        f"CupidC active preprocessing compile transform has "
                        f"unexpected tools for {transform.get('output')}: {tools!r}"
                    )
                if (
                    tools == ["cupid_c_compiler", "host_python"]
                    and (
                        directory not in {".", "user"}
                        or operation != "compile_c_to_elf32_object"
                    )
                ):
                    raise AuditError(
                        "CupidC checked compile transform differs from its "
                        f"freestanding build contract for "
                        f"{transform.get('output')}: "
                        f"directory={directory!r}, operation={operation!r}"
                    )
                profile = _c_preprocessor_profile_for_c_transform(
                    directory, transform
                )
                root = _c_preprocessor_one_c_root(transform)
                logical = _c_preprocessor_logical_path(root)
                entry = source_entries.get(root)
                if entry is None:
                    raise AuditError(
                        f"CupidC active preprocessing root is absent from source "
                        f"inventory: {root}"
                    )
                origin = str(entry.get("origin", ""))
                if logical in _C_PP_DEFERRED_HOSTED_CASES:
                    if (
                        directory != "toolchain"
                        or operation != "compile_c_to_host_object"
                        or profile not in _C_PP_HOSTED_PROFILES
                    ):
                        raise AuditError(
                            f"CupidC active preprocessing hosted deferral "
                            f"transform differs for {logical}: "
                            f"directory={directory!r}, operation={operation!r}, "
                            f"profile={profile!r}"
                        )
                    if origin != "tracked":
                        raise AuditError(
                            f"CupidC active preprocessing hosted deferral is "
                            f"not a tracked source ({origin!r}): {logical}"
                        )
                    deferred_hosted.append(logical)
                    continue
                if origin == "generated":
                    if profile != "KERNEL_I386":
                        raise AuditError(
                            f"CupidC generated root has non-kernel profile: {root}"
                        )
                    generated.append(logical)
                elif origin == "tracked":
                    active_by_profile[profile].append(logical)
                else:
                    raise AuditError(
                        f"CupidC active preprocessing root has unknown origin "
                        f"{origin!r}: {root}"
                    )
                continue
            if operation == "recursive_make":
                if tools != ["make"]:
                    raise AuditError(
                        "CupidC recursive Make transform has unexpected tools "
                        f"for {transform.get('output')}: {tools!r}"
                    )
                # A parent Make rule names child-build sources as prerequisites,
                # but it does not deliver those sources into Cupid OS. The
                # supplemental child graph owns their compile classification.
                continue

            inputs = transform.get("inputs")
            if not isinstance(inputs, list):
                if operation in {
                    "wrap_binary_as_elf32_relocatable",
                    "wrap_text_as_elf32_relocatable",
                }:
                    raise AuditError(
                        f"CupidC delivery transform inputs are absent for "
                        f"{transform.get('output')}"
                    )
                continue
            delivered_inputs = [
                path
                for path in inputs
                if isinstance(path, str)
                and (
                    _language(path) == "cupid_c"
                    or (
                        _language(path) == "c_header"
                        and path.startswith("bin/")
                    )
                )
            ]
            if delivered_inputs and operation != "wrap_text_as_elf32_relocatable":
                raise AuditError(
                    f"CupidC active preprocessing found an unclassified Cupid "
                    f"delivery transform: {transform.get('output')} ({operation})"
                )
            if operation != "wrap_text_as_elf32_relocatable":
                continue
            cupid_inputs = [
                path
                for path in inputs
                if isinstance(path, str)
                and _language(path) in {"cupid_c", "c_header"}
            ]
            if not cupid_inputs:
                continue
            if directory != ".":
                raise AuditError(
                    f"CupidC active preprocessing found an unclassified Cupid "
                    f"delivery transform: {transform.get('output')} ({operation})"
                )
            _c_preprocessor_recipe_markers(transform, {"CUPIDOBJ"})
            if tools != ["cupid_object", "host_python"]:
                raise AuditError(
                    f"CupidC delivery transform has unexpected tools for "
                    f"{transform.get('output')}: {tools!r}"
                )
            if len(cupid_inputs) != 1:
                raise AuditError(
                    f"CupidC delivery transform expected exactly one source for "
                    f"{transform.get('output')}; found {len(cupid_inputs)}"
                )
            root = cupid_inputs[0]
            logical = _c_preprocessor_logical_path(root)
            if _language(root) == "c_header":
                non_roots.append(logical)
            elif root.startswith("bin/browser/"):
                include_only.append(logical)
            elif root.startswith("bin/") and root.count("/") == 1:
                active_by_profile["CUPID_RUNTIME"].append(logical)
            else:
                raise AuditError(
                    f"CupidC delivery source has no active-case classification: {root}"
                )

    for profile, expected_count in _C_PP_ACTIVE_COUNTS.items():
        cases = active_by_profile[profile]
        if len(cases) != len(set(cases)):
            raise AuditError(
                f"CupidC active preprocessing profile has duplicate roots: {profile}"
            )
        if len(cases) != expected_count:
            raise AuditError(
                f"CupidC active preprocessing profile {profile} expected "
                f"{expected_count} tracked roots; found {len(cases)}"
            )
    _c_preprocessor_require_exact_paths(
        "generated kernel roots", generated, _C_PP_GENERATED_KERNEL_CASES
    )
    _c_preprocessor_require_exact_paths(
        "delivered non-root headers", non_roots, _C_PP_NON_ROOT_HEADERS
    )
    _c_preprocessor_require_exact_paths(
        "deferred hosted roots", deferred_hosted, _C_PP_DEFERRED_HOSTED_CASES
    )
    if len(include_only) != 22 or len(include_only) != len(set(include_only)):
        raise AuditError(
            f"CupidC active preprocessing expected 22 distinct browser "
            f"include-only fragments; found {len(include_only)}"
        )
    browser = source_entries.get("bin/browser.cc")
    browser_includes = set(browser.get("includes", [])) if browser else set()
    unresolved_fragments = sorted(
        path[1:] for path in include_only if path[1:] not in browser_includes
    )
    if unresolved_fragments:
        raise AuditError(
            f"CupidC browser fragments lost /bin/browser.cc ownership: "
            f"{unresolved_fragments!r}"
        )

    include_roots, macros, forced_includes = (
        _c_preprocessor_profile_configuration()
    )
    active_rows = tuple(
        (profile.name, path)
        for profile in _C_PP_PROFILE_ROWS
        for path in sorted(active_by_profile[profile.name])
    )
    generated_rows = tuple(
        ("KERNEL_I386", path) for path in sorted(generated)
    )
    return CPreprocessorActiveCasesManifest(
        profiles=_C_PP_PROFILE_ROWS,
        include_roots=include_roots,
        macros=macros,
        forced_includes=forced_includes,
        active_cases=active_rows,
        generated_cases=generated_rows,
        include_only=tuple(
            (path, "/bin/browser.cc") for path in sorted(include_only)
        ),
        non_roots=tuple(
            (
                path,
                "delivered header requires a translation-unit owner context",
            )
            for path in sorted(non_roots)
        ),
        deferred_hosted=tuple(
            (path, _c_preprocessor_deferred_reason(path))
            for path in sorted(deferred_hosted)
        ),
    )


def _c_preprocessor_translation_unit_contract(
    manifest: CPreprocessorActiveCasesManifest,
) -> dict[str, object]:
    profiles = []
    for profile_policy in manifest.profiles:
        name = profile_policy.name
        roots = [
            {"path": path, "forms": forms}
            for profile, path, forms in manifest.include_roots
            if profile == name
        ]
        macros = [
            {"name": macro_name, "replacement": replacement}
            for profile, macro_name, replacement in manifest.macros
            if profile == name
        ]
        forced = [
            path
            for profile, path in manifest.forced_includes
            if profile == name
        ]
        profiles.append(
            {
                "name": name,
                "mode": profile_policy.mode,
                "gnu_extensions": (
                    profile_policy.gnu_extensions == "CTOOL_TRUE"
                ),
                "hosted_environment": (
                    profile_policy.hosted_environment == "CTOOL_TRUE"
                ),
                "implicit_function_declarations": (
                    profile_policy.implicit_function_declarations
                    == "CTOOL_TRUE"
                ),
                "compatibility_pointer_conversions": (
                    profile_policy.compatibility_pointer_conversions
                    == "CTOOL_TRUE"
                ),
                "tracked_translation_units": sum(
                    profile == name
                    for profile, _ in manifest.active_cases
                ),
                "generated_translation_units": sum(
                    profile == name
                    for profile, _ in manifest.generated_cases
                ),
                "include_roots": roots,
                "macro_actions": macros,
                "forced_includes": forced,
            }
        )
    external_deferred = sum(
        reason.startswith("external system headers/runtime")
        for _, reason in manifest.deferred_hosted
    )
    hermetic_deferred = sum(
        reason.startswith("hermetic unit")
        for _, reason in manifest.deferred_hosted
    )
    return {
        "status": "pass",
        "tracked_translation_units": len(manifest.active_cases),
        "generated_translation_units": len(manifest.generated_cases),
        "total_translation_units": (
            len(manifest.active_cases) + len(manifest.generated_cases)
        ),
        "include_only_fragments": len(manifest.include_only),
        "delivered_non_root_headers": len(manifest.non_roots),
        "deferred_hosted_translation_units": len(manifest.deferred_hosted),
        "deferred_external_header_units": external_deferred,
        "deferred_hermetic_units": hermetic_deferred,
        "profiles": profiles,
    }


def _c_string_literal(value: str) -> str:
    pieces = ['"']
    for byte in value.encode("utf-8"):
        if byte == 0x22:
            pieces.append('\\"')
        elif byte == 0x5C:
            pieces.append("\\\\")
        elif byte == 0x3F:
            pieces.append("\\?")
        elif 0x20 <= byte <= 0x7E:
            pieces.append(chr(byte))
        else:
            pieces.append(f"\\{byte:03o}")
    pieces.append('"')
    return "".join(pieces)


def _render_c_preprocessor_active_cases(
    manifest: CPreprocessorActiveCasesManifest,
) -> str:
    groups = (
        [
            f"CUPIDC_PP_PROFILE({profile.name}, {profile.mode}, "
            f"{profile.gnu_extensions}, {profile.hosted_environment}, "
            f"{profile.implicit_function_declarations}, "
            f"{profile.compatibility_pointer_conversions})"
            for profile in manifest.profiles
        ],
        [
            f"CUPIDC_PP_INCLUDE_ROOT({name}, {_c_string_literal(path)}, {forms})"
            for name, path, forms in manifest.include_roots
        ],
        [
            f"CUPIDC_PP_MACRO({profile}, {_c_string_literal(name)}, "
            f"{_c_string_literal(replacement)})"
            for profile, name, replacement in manifest.macros
        ],
        [
            f"CUPIDC_PP_FORCED_INCLUDE({profile}, {_c_string_literal(path)})"
            for profile, path in manifest.forced_includes
        ],
        [
            f"CUPIDC_PP_ACTIVE_CASE({profile}, {_c_string_literal(path)})"
            for profile, path in manifest.active_cases
        ],
        [
            f"CUPIDC_PP_GENERATED_CASE({profile}, {_c_string_literal(path)})"
            for profile, path in manifest.generated_cases
        ],
        [
            f"CUPIDC_PP_INCLUDE_ONLY({_c_string_literal(path)}, "
            f"{_c_string_literal(owner)})"
            for path, owner in manifest.include_only
        ],
        [
            f"CUPIDC_PP_NON_ROOT({_c_string_literal(path)}, "
            f"{_c_string_literal(reason)})"
            for path, reason in manifest.non_roots
        ],
        [
            f"CUPIDC_PP_DEFERRED_HOSTED({_c_string_literal(path)}, "
            f"{_c_string_literal(reason)})"
            for path, reason in manifest.deferred_hosted
        ],
    )
    lines = [
        "/* Checked active CupidC preprocessing cases.",
        " * Generated by tools/build_graph_audit.py; do not edit.",
        " * __GNUC__=1 is an active-source definedness compatibility marker,",
        " * not the version of the compiler hosting the bootstrap.",
        " */",
        "",
    ]
    for group in groups:
        lines.extend(group)
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def _json_payload(audit: dict[str, object]) -> str:
    return json.dumps(audit, indent=2, sort_keys=True) + "\n"


def _markdown_cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def _render_markdown(audit: dict[str, object]) -> str:
    source_counts = collections.Counter(
        str(source["language"]) for source in audit["sources"]
    )
    cohort_counts = collections.Counter(
        str(source["cohort"]) for source in audit["sources"]
    )
    cohort_lines: collections.Counter[str] = collections.Counter()
    for source in audit["sources"]:
        if source["lines"] is not None:
            cohort_lines[str(source["cohort"])] += int(source["lines"])
    unreachable_counts = collections.Counter(
        str(source["classification"])
        for source in audit["unreachable_sources"]
    )
    tool_counts: collections.Counter[str] = collections.Counter()
    all_builds = [audit["build"], *audit["supplemental_builds"]]
    all_transforms = [
        transform for build in all_builds for transform in build["transforms"]
    ]
    for transform in all_transforms:
        tool_counts.update(str(tool) for tool in transform["tools"])
    feature_counts: collections.Counter[str] = collections.Counter()
    feature_occurrences: collections.Counter[str] = collections.Counter()
    for feature in audit["features"]:
        parts = str(feature["id"]).split(".")
        group = ".".join(parts[:2]) if len(parts) > 1 else parts[0]
        feature_counts[group] += 1
        feature_occurrences[group] += int(feature["occurrences"])

    lines = [
        "# Active build and source audit",
        "",
        "This file is generated deterministically by "
        "`tools/build_graph_audit.py` from the supported Make graph and source tree.",
        "",
        "## Scope",
        "",
        f"- Root Make target: `{audit['build']['root_target']}`",
        "- Supplemental builds: "
        + (
            ", ".join(
                f"`{build['directory']}:{build['root_target']}`"
                for build in audit["supplemental_builds"]
            )
            or "none"
        ),
        f"- Active source inputs: {audit['summary']['active_sources']}",
        f"- Unreachable source-like files: {audit['summary']['unreachable_sources']}",
        f"- Reachable output transforms: {audit['summary']['transforms']}",
        f"- Distinct feature requirements: {audit['summary']['features']}",
        "- Make conditionals use the canonical `OS=Windows_NT` graph and the "
        "C locale fixes wildcard order on every host. Direct Linux build "
        "tests cover the Linux execution branch.",
        "- The `TempleOS/` reference tree is excluded.",
        "- Source and control-file SHA-256 values use canonical LF text bytes.",
        "",
        "Generated C translation units are recorded as reachable build inputs but have "
        "no source hash or lexical features; their content is owned by the recorded "
        "generator transform.",
        "",
        "## Active language inputs",
        "",
        "| Language | Files |",
        "| --- | ---: |",
    ]
    lines.extend(
        f"| `{_markdown_cell(language)}` | {count} |"
        for language, count in sorted(source_counts.items())
    )
    lines.extend(
        [
            "",
            "## Source cohorts",
            "",
            "| Cohort | Files | Checked-source lines |",
            "| --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(cohort)}` | {count} | {cohort_lines[cohort]} |"
        for cohort, count in sorted(cohort_counts.items())
    )
    lines.extend(
        [
            "",
            "## Supported build roots",
            "",
            "| Directory | Root target | Transforms | Include paths |",
            "| --- | --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{build['directory']}` | `{build['root_target']}` | "
        f"{len(build['transforms'])} | {len(build['include_search_paths'])} |"
        for build in all_builds
    )
    lines.extend(
        [
            "",
            "## Current output ownership",
            "",
            "| Tool interface | Reachable transforms |",
            "| --- | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(tool)}` | {count} |"
        for tool, count in sorted(tool_counts.items())
    )
    lines.extend(
        [
            "",
            "## Feature inventory",
            "",
            "| Feature family | Distinct requirements | Lexical/build occurrences |",
            "| --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(group)}` | {count} | {feature_occurrences[group]} |"
        for group, count in sorted(feature_counts.items())
    )
    lines.extend(
        [
            "",
            "The JSON companion records stable feature IDs, occurrence counts, files, "
            "and representative source locations.",
            "",
        ]
    )
    if audit["abi"] is not None:
        abi = audit["abi"]
        linker = abi["linker_script"]
        lines.extend(
            [
                "## ABI and object contract",
                "",
                "| Property | Required value |",
                "| --- | --- |",
                f"| Architecture | `{abi['architecture']}` |",
                f"| Data model | `{abi['data_model']}` |",
                f"| Endianness | `{abi['endianness']}` |",
                f"| Calling convention | `{abi['calling_convention']}` |",
                f"| Object interchange | `{abi['object_interchange']}` |",
                f"| Required relocations | `{', '.join(abi['required_relocations'])}` |",
                f"| Stack alignment | {abi['stack_alignment_bytes']} bytes |",
                "",
            ]
        )
        if linker is not None:
            lines.extend(
                [
                    f"`{linker['path']}` has SHA-256 `{linker['sha256']}` and uses "
                    f"{', '.join(f'`{feature}`' for feature in linker['features'])}.",
                    "It is referenced by linker flags but is not a declared Make "
                    "prerequisite." if not linker["declared_make_prerequisite"] else
                    "It is also a declared Make prerequisite.",
                    "",
                ]
            )
    lines.extend(
        [
            "## Source-driven capability priority",
            "",
            "| Rank | Capability | Source evidence |",
            "| ---: | --- | ---: |",
        ]
    )
    lines.extend(
        f"| {item['rank']} | `{item['id']}` - {_markdown_cell(item['title'])} | "
        f"{item['source_count']} |"
        for item in audit["roadmap"]["capability_priorities"]
    )
    lines.extend(
        [
            "",
            "## Source-cohort migration order",
            "",
            "| Rank | Cohort step | Files | Rationale |",
            "| ---: | --- | ---: | --- |",
        ]
    )
    lines.extend(
        f"| {item['rank']} | `{item['id']}` | {item['source_count']} | "
        f"{_markdown_cell(item['rationale'])} |"
        for item in audit["roadmap"]["source_cohort_order"]
    )
    lines.extend(
        [
            "",
            "## Unreachable source classification",
            "",
            "| Classification | Files |",
            "| --- | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(classification)}` | {count} |"
        for classification, count in sorted(unreachable_counts.items())
    )
    lines.extend(
        [
            "",
            "An exact content match does not by itself prove semantic duplication; "
            "path-sensitive compatibility headers remain removal-blocked.",
            "",
            "| Path | Language | Classification | Lines | Evidence |",
            "| --- | --- | --- | ---: | --- |",
        ]
    )
    for source in audit["unreachable_sources"]:
        duplicates = source.get("duplicate_of", [])
        semantic_relations = [
            relation
            for relation in source.get("relations", [])
            if relation["kind"] != "exact_content_match"
        ]
        if semantic_relations:
            evidence = ", ".join(
                f"{relation['kind']}: `{relation['path']}`"
                for relation in semantic_relations
            )
        elif duplicates:
            shown = ", ".join(f"`{path}`" for path in duplicates[:3])
            if len(duplicates) > 3:
                shown += f" (+{len(duplicates) - 3} more)"
            evidence = f"content match: {shown}"
        else:
            evidence = str(source["reason"])
        lines.append(
            f"| `{source['path']}` | `{source['language']}` | "
            f"`{source['classification']}` | {source['lines']} | "
            f"{_markdown_cell(evidence)} |"
        )
    lines.extend(
        [
            "",
            "## Audit contracts",
            "",
            "| Contract | Status | Detail |",
            "| --- | --- | --- |",
        ]
    )
    if audit["contracts"]:
        for name, contract in sorted(audit["contracts"].items()):
            if "tool_images" in contract:
                link_counts = ", ".join(
                    f"{entry['tool']}={entry['objects']}"
                    for entry in contract["link_objects"]
                )
                detail = (
                    f"{contract['tool_c_sources']} tool C sources "
                    f"({contract['strict_c_sources']} strict, "
                    f"{contract['gnu_c_sources']} GNU); "
                    f"{contract['tool_images']} tools "
                    f"({link_counts}); "
                    f"{contract['compared_c_objects']} C objects and "
                    f"{contract['compared_startup_objects']} startup object "
                    f"compared across stages; "
                    f"{contract['compared_tool_images']} tool images; "
                    f"{contract['success_behavior_cases']} success and "
                    f"{contract['failure_behavior_cases']} failure cases; "
                    f"{contract['platform']}"
                )
            elif "compiler_c_sources" in contract:
                detail = (
                    f"{contract['compiler_c_sources']} compiler C sources "
                    f"({contract['strict_c_sources']} strict, "
                    f"{contract['gnu_c_sources']} GNU); "
                    f"{contract['link_objects']} linked objects; "
                    f"{contract['compared_c_objects']} C objects and "
                    f"{contract['compared_startup_objects']} startup object "
                    f"compared across stages; "
                    f"{contract['compared_compiler_images']} compiler images; "
                    f"{contract['behavior_cases']} behavior cases; "
                    f"{contract['platform']}"
                )
            elif "tracked_translation_units" in contract:
                profile_counts = ", ".join(
                    f"{profile['name']}={profile['tracked_translation_units']}"
                    for profile in contract["profiles"]
                )
                detail = (
                    f"{contract['tracked_translation_units']} tracked + "
                    f"{contract['generated_translation_units']} generated "
                    f"translation units ({profile_counts}); "
                    f"{contract['include_only_fragments']} include-only, "
                    f"{contract['delivered_non_root_headers']} non-root headers; "
                    f"{contract['deferred_hosted_translation_units']} hosted "
                    f"deferred ({contract['deferred_external_header_units']} "
                    "external, "
                    f"{contract['deferred_hermetic_units']} hermetic)"
                )
            elif "tracked_c_sources" in contract:
                detail = (
                    f"{contract['tracked_c_sources']} tracked .c sources; "
                    f"{contract['active_tracked_c_sources']} active; "
                    f"{contract['cupidc_owned_tracked_c_sources']} owned by "
                    "CupidC; "
                    f"{contract['unreachable_tracked_c_sources']} unreachable; "
                    f"{contract['tracked_cupid_c_sources']} tracked .cc "
                    f"sources; {contract['active_tracked_cupid_c_sources']} "
                    "active with independent CupidC evidence; "
                    f"{contract['unreachable_tracked_cupid_c_sources']} "
                    "unreachable"
                )
            elif "cupidasm_owned_sources" in contract:
                detail = (
                    f"{contract['active_sources']} active assembly sources; "
                    f"{contract['cupidasm_owned_sources']} CupidASM-owned; "
                    f"{contract['toolchain_startup_sources']} Toolchain "
                    "startup; "
                    f"{contract['other_owned_sources']} other-owned; "
                    f"{contract['ownerless_sources']} ownerless; "
                    f"{len(contract['explicit_classifications'])} explicit "
                    "host-only classifications"
                )
            elif "expression_occurrences" in contract:
                detail = (
                    f"{contract['expression_occurrences']} conditional expressions "
                    f"({contract['if_occurrences']} #if, "
                    f"{contract['elif_occurrences']} #elif); "
                    f"{contract['unique_expressions']} normalized expressions; "
                    f"{contract['directive_expression_pairs']} "
                    "directive/expression pairs"
                )
            elif "named_line_occurrences" in contract:
                directive_word = (
                    "directive"
                    if contract["named_line_occurrences"] == 1
                    else "directives"
                )
                source_word = (
                    "file" if contract["source_files"] == 1 else "files"
                )
                detail = (
                    f"{contract['named_line_occurrences']} named #line "
                    f"{directive_word} ({contract['direct_line_occurrences']} "
                    f"direct, {contract['pp_token_line_occurrences']} pp-token; "
                    f"{contract['filename_occurrences']} filename); "
                    f"{contract['numeric_marker_occurrences']} numeric markers; "
                    f"{contract['source_files']} source {source_word}; "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "pp_token_operand_occurrences" in contract:
                detail = (
                    f"{contract['include_occurrences']} C include operands "
                    f"({contract['direct_quoted_occurrences']} quoted, "
                    f"{contract['direct_angle_occurrences']} angle, "
                    f"{contract['pp_token_operand_occurrences']} pp-token); "
                    f"{contract['source_files']} source files; "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "exe_occurrences" in contract:
                detail = (
                    f"{contract['block_occurrences']} Cupid #exe blocks "
                    f"({contract['ordinary_marker_occurrences']} #, "
                    f"{contract['digraph_marker_occurrences']} %:); "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "pragma_occurrences" in contract:
                detail = (
                    f"{contract['pragma_occurrences']} pragmas "
                    f"({contract['once_occurrences']} once, "
                    f"{contract['pack_push_occurrences']} pack pushes, "
                    f"{contract['pack_pop_occurrences']} pack pops); "
                    "pack balanced: "
                    f"{'yes' if contract['pack_balanced'] else 'no'}; "
                    f"max pack depth {contract['max_pack_depth']}"
                )
            else:
                missing = contract.get("missing_link_inputs", [])
                detail = (
                    f"{contract.get('linked_objects', 0)} linked objects; "
                    f"{contract.get('declared_artifacts', 0)} declared artifacts; "
                    f"{len(missing)} missing"
                )
            lines.append(
                f"| `{_markdown_cell(name)}` | `{contract['status']}` | "
                f"{_markdown_cell(detail)} |"
            )
    else:
        lines.append("| _none declared_ | `not_applicable` | n/a |")
    lines.extend(
        [
            "",
            "## Interpretation limits",
            "",
            "- Feature occurrences are comment/string-masked lexical evidence, not a "
            "substitute for a compiler AST or executed semantic tests.",
            "- Include reachability follows checked Make include paths, forced "
            "includes, "
            "quoted/angle C includes, and `%include`; the conditional contract records "
            "normalized source expressions while evaluation remains a "
            "compiler-contract responsibility.",
            "- Named `#line` pp-token operands are classified before macro expansion; "
            "the CupidC corpus harness owns expansion and semantic validation.",
            "- Relocation kinds and ABI values are required interchange contracts; "
            "per-object relocation counts are recorded in the chronological bootstrap log.",
            "- `not_reached` means absent from the supported roots recorded above, not "
            "automatically safe to delete.",
        ]
    )
    return "\n".join(lines) + "\n\n"


def _write_text_atomic(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        newline="\n",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as stream:
        temporary = Path(stream.name)
        stream.write(payload)
    temporary.replace(path)


def _check_text(path: Path, expected: str) -> bool:
    try:
        actual = path.read_text(encoding="utf-8")
    except OSError:
        return False
    return actual == expected


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--make", default="make", help="GNU Make executable")
    parser.add_argument("--target", default="all", help="supported build root")
    parser.add_argument(
        "--supplemental-build",
        action="append",
        default=[],
        metavar="DIRECTORY:TARGET",
        help="additional supported Make root (repeatable)",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--summary", type=Path, help="generated Markdown summary")
    parser.add_argument(
        "--c-preprocessor-active-cases",
        type=Path,
        help="generated checked X-macro manifest for active CupidC jobs",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify checked outputs and passing contracts without writing",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    try:
        supplemental_builds = []
        for specification in args.supplemental_build:
            if ":" not in specification:
                raise AuditError(
                    "supplemental build must use DIRECTORY:TARGET syntax: "
                    f"{specification!r}"
                )
            directory, supplemental_target = specification.rsplit(":", 1)
            if not directory or not supplemental_target:
                raise AuditError(
                    "supplemental build must use DIRECTORY:TARGET syntax: "
                    f"{specification!r}"
                )
            supplemental_builds.append((directory, supplemental_target))
        audit = build_audit(
            args.root,
            args.make,
            args.target,
            supplemental_builds,
        )
        json_payload = _json_payload(audit)
        markdown_payload = _render_markdown(audit) if args.summary else None
        active_cases_payload = (
            _render_c_preprocessor_active_cases(
                _c_preprocessor_active_cases_manifest(audit)
            )
            if args.c_preprocessor_active_cases
            else None
        )
    except AuditError as exc:
        print(f"build graph audit failed: {exc}", file=sys.stderr)
        return 2

    if args.check:
        stale = []
        if not _check_text(args.output, json_payload):
            stale.append(args.output)
        if args.summary and not _check_text(args.summary, markdown_payload or ""):
            stale.append(args.summary)
        if args.c_preprocessor_active_cases and not _check_text(
            args.c_preprocessor_active_cases, active_cases_payload or ""
        ):
            stale.append(args.c_preprocessor_active_cases)
        for path in stale:
            print(f"build graph audit out of date: {path.name}", file=sys.stderr)
        failed_contracts = [
            name
            for name, contract in audit["contracts"].items()
            if contract.get("status") != "pass"
        ]
        for name in failed_contracts:
            print(f"build graph audit contract failed: {name}", file=sys.stderr)
        return 1 if stale or failed_contracts else 0

    _write_text_atomic(args.output, json_payload)
    if args.summary and markdown_payload is not None:
        _write_text_atomic(args.summary, markdown_payload)
    if args.c_preprocessor_active_cases and active_cases_payload is not None:
        _write_text_atomic(
            args.c_preprocessor_active_cases, active_cases_payload
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

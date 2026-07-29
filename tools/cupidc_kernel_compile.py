#!/usr/bin/env python3
"""Compile an approved kernel source with the checked CupidC seed."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        WSL_PRIVATE_RUN_SCRIPT,
        freeze_seed_inputs,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        WSL_PRIVATE_RUN_SCRIPT,
        freeze_seed_inputs,
    )


APPROVED_CRYPTO_SOURCES = (
    "kernel/crypto/aes.cc",
    "kernel/crypto/aes_gcm.cc",
    "kernel/crypto/asn1.cc",
    "kernel/crypto/bigint.cc",
    "kernel/crypto/chacha20.cc",
    "kernel/crypto/chacha20poly1305.cc",
    "kernel/crypto/csprng.cc",
    "kernel/crypto/ct.cc",
    "kernel/crypto/ecdsa.cc",
    "kernel/crypto/ed25519.cc",
    "kernel/crypto/hkdf.cc",
    "kernel/crypto/hmac.cc",
    "kernel/crypto/p256.cc",
    "kernel/crypto/poly1305.cc",
    "kernel/crypto/rsa.cc",
    "kernel/crypto/sha256.cc",
    "kernel/crypto/sha512.cc",
    "kernel/crypto/x25519.cc",
    "kernel/crypto/x509.cc",
    "kernel/crypto/x509_chain.cc",
)
BLOCKED_CRYPTO_SOURCES = ()
KERNEL_CRYPTO_SOURCES = tuple(
    sorted(APPROVED_CRYPTO_SOURCES + BLOCKED_CRYPTO_SOURCES)
)
APPROVED_SMP_SOURCES = (
    "kernel/smp/acpi.cc",
    "kernel/smp/mp_tables.cc",
    "kernel/smp/percpu.cc",
    "kernel/smp/smp.cc",
)
APPROVED_OPERAND_FREE_SOURCES = (
    "drivers/e1000.cc",
    "kernel/gui/desktop.cc",
    "kernel/network/socket.cc",
    "kernel/network/tcp.cc",
)
APPROVED_PORT_IO_SOURCES = (
    "drivers/ata.cc",
    "drivers/keyboard.cc",
    "drivers/mouse.cc",
    "drivers/pci.cc",
    "drivers/pit.cc",
    "drivers/rtc.cc",
    "drivers/rtl8139.cc",
    "drivers/speaker.cc",
    "drivers/vga.cc",
    "kernel/audio/ac97.cc",
    "kernel/core/syscall.cc",
    "kernel/lang/shell.cc",
    "kernel/usb/ehci.cc",
    "kernel/usb/uhci.cc",
)
APPROVED_COMPILER_READY_SOURCES = (
    "kernel/audio/memio.cc",
    "kernel/audio/midiopl.cc",
    "kernel/audio/mixer.cc",
    "kernel/audio/mus2midi.cc",
    "kernel/audio/opl_smoke.cc",
    "kernel/cpu/math.cc",
    "kernel/fs/blockcache.cc",
    "kernel/fs/blockdev.cc",
    "kernel/fs/devfs.cc",
    "kernel/fs/fat16_vfs.cc",
    "kernel/fs/fs.cc",
    "kernel/fs/homefs.cc",
    "kernel/fs/iso9660_vfs.cc",
    "kernel/fs/ramfs.cc",
    "kernel/fs/vfs.cc",
    "kernel/fs/vfs_helpers.cc",
    "kernel/gfx/bmp.cc",
    "kernel/gfx/font_8x8.cc",
    "kernel/gfx/fontsys.cc",
    "kernel/gfx/gfx2d_assets.cc",
    "kernel/gfx/gfx2d_effects.cc",
    "kernel/gfx/gfx2d_icons.cc",
    "kernel/gfx/gfx2d_transform.cc",
    "kernel/gfx/graphics.cc",
    "kernel/gfx/ttf.cc",
    "kernel/gui/ansi.cc",
    "kernel/gui/clipboard.cc",
    "kernel/gui/ctxt_image_worker.cc",
    "kernel/gui/gui.cc",
    "kernel/gui/gui_containers.cc",
    "kernel/gui/gui_events.cc",
    "kernel/gui/gui_menus.cc",
    "kernel/gui/gui_themes.cc",
    "kernel/gui/gui_widgets.cc",
    "kernel/gui/terminal_app.cc",
    "kernel/gui/ui.cc",
    "kernel/lang/as_elf.cc",
    "kernel/lang/ctool_kernel.cc",
    "kernel/lang/cupidc_elf.cc",
    "kernel/lang/cupidscript_arrays.cc",
    "kernel/lang/cupidscript_exec.cc",
    "kernel/lang/cupidscript_jobs.cc",
    "kernel/lang/cupidscript_lex.cc",
    "kernel/lang/cupidscript_parse.cc",
    "kernel/lang/cupidscript_runtime.cc",
    "kernel/lang/cupidscript_streams.cc",
    "kernel/lang/cupidscript_strings.cc",
    "kernel/lang/dis.cc",
    "kernel/lang/exec.cc",
    "kernel/lang/godspeak.cc",
    "kernel/mm/swap.cc",
    "kernel/mm/swap_disk.cc",
    "kernel/network/arp.cc",
    "kernel/network/dhcp.cc",
    "kernel/network/dns.cc",
    "kernel/network/icmp.cc",
    "kernel/network/ip.cc",
    "kernel/network/net_if.cc",
    "kernel/smp/ioapic.cc",
    "kernel/tls/tls_ca_bundle_data.cc",
    "kernel/tls/tls_ctx.cc",
    "kernel/tls/tls_handshake.cc",
    "kernel/tls/tls_kdf.cc",
    "kernel/tls/tls_record.cc",
    "kernel/tls/tls_selftest.cc",
    "kernel/tls/tls12_handshake.cc",
    "kernel/usb/usb.cc",
    "kernel/usb/usb_hid.cc",
    "kernel/usb/usb_hub.cc",
    "kernel/usb/usb_msc.cc",
    "kernel/util/calendar.cc",
)
APPROVED_TOOLCHAIN_KERNEL_SOURCES = (
    "toolchain/ctool.cc",
    "toolchain/cupidasm.cc",
    "toolchain/cupiddis.cc",
    "toolchain/elf32.cc",
    "toolchain/x86.cc",
)
APPROVED_SOURCE_DRIVEN_SOURCES = (
    "drivers/serial.cc",
    "drivers/timer.cc",
    "kernel/audio/nuked_opl3.cc",
    "kernel/core/app_launch.cc",
    "kernel/core/kernel.cc",
    "kernel/core/panic.cc",
    "kernel/core/process.cc",
    "kernel/cpu/fpu.cc",
    "kernel/cpu/idt.cc",
    "kernel/cpu/irq.cc",
    "kernel/cpu/ksyms.cc",
    "kernel/cpu/libm.cc",
    "kernel/cpu/pic.cc",
    "kernel/cpu/simd.cc",
    "kernel/fs/fat16.cc",
    "kernel/fs/iso9660.cc",
    "kernel/fs/loopdev.cc",
    "kernel/gfx/deflate.cc",
    "kernel/gfx/gfx2d.cc",
    "kernel/gfx/glyph_raster.cc",
    "kernel/gfx/jpeg.cc",
    "kernel/gfx/png.cc",
    "kernel/gui/ed.cc",
    "kernel/lang/as.cc",
    "kernel/lang/cupidc.cc",
    "kernel/lang/cupidc_lex.cc",
    "kernel/lang/cupidc_parse.cc",
    "kernel/lang/cupidc_string.cc",
    "kernel/lang/ssh_io.cc",
    "kernel/mm/memory.cc",
    "kernel/mm/paging.cc",
    "kernel/network/sshd.cc",
    "kernel/network/udp.cc",
    "kernel/smp/bkl.cc",
    "kernel/smp/lapic.cc",
    "kernel/tls/tls_ca_bundle.cc",
)
APPROVED_GENERATED_KERNEL_SOURCES = (
    "kernel/cpu/ksyms_data.cc",
)
FROZEN_KERNEL_INPUT_CLOSURES = {
    "kernel/audio/nuked_opl3.cc": (
        "kernel/audio/nuked_opl3.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
    ),
    "kernel/core/kernel.cc": (
        "drivers/ata.h",
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/pci.h",
        "drivers/pit.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/syscall.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.h",
        "kernel/cpu/idt.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/pic.h",
        "kernel/cpu/simd.h",
        "kernel/crypto/csprng.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/devfs.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fat16_vfs.h",
        "kernel/fs/fs.h",
        "kernel/fs/homefs.h",
        "kernel/fs/iso9660_vfs.h",
        "kernel/fs/ramfs.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/fontsys.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/clipboard.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_containers.h",
        "kernel/gui/gui_events.h",
        "kernel/gui/gui_menus.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/gui_widgets.h",
        "kernel/gui/ui.h",
        "kernel/lang/as.h",
        "kernel/lang/ctool_kernel.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h",
        "kernel/smp/bkl.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/lapic.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
        "kernel/tls/tls_selftest.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/util/calendar.h",
        "toolchain/ctool.h",
    ),
    "kernel/cpu/fpu.cc": (
        "drivers/serial.h",
        "kernel/core/panic.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/libm.h",
    ),
    "kernel/cpu/libm.cc": (
        "kernel/core/types.h",
        "kernel/cpu/libm.h",
    ),
    "kernel/cpu/simd.cc": (
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
    ),
    "kernel/gfx/glyph_raster.cc": (
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/gfx/glyph_raster.h",
        "kernel/mm/memory.h",
    ),
    "kernel/gfx/jpeg.cc": (
        "kernel/core/types.h",
        "kernel/cpu/libm.h",
        "kernel/gfx/jpeg.h",
        "kernel/mm/memory.h",
    ),
    "kernel/smp/percpu.cc": (
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/smp/percpu.h",
    ),
    "kernel/smp/smp.cc": (
        "drivers/serial.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/fpu.h",
        "kernel/cpu/idt.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/smp/acpi.h",
        "kernel/smp/bkl.h",
        "kernel/smp/ioapic.h",
        "kernel/smp/lapic.h",
        "kernel/smp/mp_tables.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
    ),
    "kernel/cpu/ksyms_data.cc": (
        "kernel/cpu/ksyms.h",
        "kernel/core/types.h",
    ),
}
APPROVED_KERNEL_SOURCES = tuple(
    sorted(
        APPROVED_CRYPTO_SOURCES
        + APPROVED_SMP_SOURCES
        + APPROVED_OPERAND_FREE_SOURCES
        + APPROVED_PORT_IO_SOURCES
        + APPROVED_COMPILER_READY_SOURCES
        + APPROVED_TOOLCHAIN_KERNEL_SOURCES
        + APPROVED_SOURCE_DRIVEN_SOURCES
    )
)
APPROVED_KERNEL_COMPILE_SOURCES = tuple(
    sorted(APPROVED_KERNEL_SOURCES + APPROVED_GENERATED_KERNEL_SOURCES)
)

KERNEL_I386_ARGUMENTS = (
    "--gnu",
    "--freestanding",
    "-D",
    "__GNUC__=1",
    "-D",
    "__ORDER_LITTLE_ENDIAN__=1234",
    "-D",
    "__ORDER_BIG_ENDIAN__=4321",
    "-D",
    "__ORDER_PDP_ENDIAN__=3412",
    "-D",
    "__BYTE_ORDER__=__ORDER_LITTLE_ENDIAN__",
    "-D",
    "__SSE2__=1",
    "-D",
    "DEBUG=1",
    "-I",
    "/kernel",
    "-I",
    "/kernel/audio",
    "-I",
    "/kernel/core",
    "-I",
    "/kernel/cpu",
    "-I",
    "/kernel/crypto",
    "-I",
    "/kernel/doom",
    "-I",
    "/kernel/fs",
    "-I",
    "/kernel/gfx",
    "-I",
    "/kernel/gui",
    "-I",
    "/kernel/lang",
    "-I",
    "/kernel/mm",
    "-I",
    "/kernel/network",
    "-I",
    "/kernel/smp",
    "-I",
    "/kernel/tls",
    "-I",
    "/kernel/usb",
    "-I",
    "/kernel/util",
    "-I",
    "/drivers",
    "-I",
    "/toolchain",
)

DEFAULT_TIMEOUT_SECONDS = 180
GENERATED_KERNEL_TIMEOUT_SECONDS = 600


class KernelCompileError(RuntimeError):
    """A checked kernel compilation could not publish an object."""


def build_compile_arguments(
    logical_source: str,
    logical_output: str,
    compiler_root: str,
) -> tuple[str, ...]:
    """Build the complete, fixed KERNEL_I386 CupidC argument vector."""
    return (
        "-c",
        logical_source,
        "-o",
        logical_output,
        *KERNEL_I386_ARGUMENTS,
        "--root",
        compiler_root,
    )


def build_wsl_invocation(
    linux_root: str,
    linux_seed: str,
    arguments: Sequence[str],
) -> tuple[str, ...]:
    """Build a WSL command that runs a private copy of the checked seed."""
    return (
        "wsl",
        "-e",
        "sh",
        "-c",
        WSL_PRIVATE_RUN_SCRIPT,
        "sh",
        linux_root,
        linux_seed,
        *arguments,
    )


class SeedExecutor:
    """Run the static Linux seed natively or through private WSL staging."""

    def __init__(self, root: Path):
        self.root = root.resolve()
        self.uses_wsl = os.name == "nt"
        if self.uses_wsl:
            if shutil.which("wsl") is None:
                raise KernelCompileError(
                    "WSL is required to run the checked i386 Linux seed"
                )
            self.compiler_root = self._wsl_path(self.root)
        else:
            self.compiler_root = str(self.root)

    @staticmethod
    def _wsl_path(path: Path) -> str:
        try:
            result = subprocess.run(
                ["wsl", "-e", "wslpath", "-a", str(path.resolve())],
                text=True,
                capture_output=True,
            )
        except OSError as error:
            raise KernelCompileError(
                f"WSL could not translate {path}: {error}"
            ) from error
        if result.returncode != 0 or not result.stdout.strip():
            details = result.stderr.strip() or f"status {result.returncode}"
            raise KernelCompileError(
                f"WSL could not translate {path}: {details}"
            )
        return result.stdout.strip()

    def compiler_root_for(self, path: Path) -> str:
        """Return a CupidC root path for an arbitrary captured source tree."""
        resolved = path.resolve()
        if self.uses_wsl:
            return self._wsl_path(resolved)
        return str(resolved)

    def run(
        self,
        executable: Path,
        arguments: Sequence[str],
        timeout: int,
    ) -> subprocess.CompletedProcess[str]:
        if not self.uses_wsl:
            return subprocess.run(
                [str(executable), *arguments],
                cwd=self.root,
                text=True,
                capture_output=True,
                timeout=timeout,
            )

        command = build_wsl_invocation(
            self.compiler_root,
            self._wsl_path(executable),
            arguments,
        )
        return subprocess.run(
            command,
            text=True,
            capture_output=True,
            timeout=timeout,
        )


def _read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise KernelCompileError(
            f"cannot read emitted object {path}: {error}"
        ) from error


def validate_i386_relocatable_bytes(image: bytes) -> None:
    """Validate the ELF32 structure and relocation contract CupidLD consumes."""
    if len(image) < 52:
        raise KernelCompileError("ELF header is outside the emitted object")
    if image[0:7] != b"\x7fELF\x01\x01\x01":
        raise KernelCompileError(
            "emitted object is not little-endian ELF32 version 1"
        )
    (
        object_type,
        machine,
        version,
        _entry,
        program_offset,
        section_offset,
        _flags,
        header_size,
        program_entry_size,
        program_count,
        section_entry_size,
        section_count,
        section_name_index,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", image, 16)
    if object_type != 1 or machine != 3 or version != 1:
        raise KernelCompileError(
            "emitted object is not an i386 ELF32 relocatable object"
        )
    if header_size != 52 or section_entry_size != 40:
        raise KernelCompileError(
            "emitted object has an invalid ELF or section header size"
        )
    if program_count != 0 or program_offset != 0 or program_entry_size != 0:
        raise KernelCompileError(
            "emitted relocatable object unexpectedly has program headers"
        )
    if section_count == 0 or section_name_index >= section_count:
        raise KernelCompileError(
            "emitted object has an invalid section table"
        )
    section_bytes = section_count * section_entry_size
    if (
        section_offset > len(image)
        or section_bytes > len(image) - section_offset
    ):
        raise KernelCompileError(
            "emitted object has a truncated section header table"
        )

    sections = []
    for index in range(section_count):
        section = struct.unpack_from(
            "<IIIIIIIIII",
            image,
            section_offset + index * section_entry_size,
        )
        (
            _name,
            section_type,
            _section_flags,
            _section_address,
            payload_offset,
            payload_size,
            _link,
            _info,
            alignment,
            _entry_size,
        ) = section
        if (
            section_type != 8
            and (
                payload_offset > len(image)
                or payload_size > len(image) - payload_offset
            )
        ):
            raise KernelCompileError(
                f"emitted object section {index} payload is outside the file"
            )
        if alignment != 0 and alignment & (alignment - 1):
            raise KernelCompileError(
                f"emitted object section {index} alignment is not a power of two"
            )
        if (
            section_type != 8
            and alignment > 1
            and payload_offset % alignment != 0
        ):
            raise KernelCompileError(
                f"emitted object section {index} payload is misaligned"
            )
        sections.append(section)

    name_section = sections[section_name_index]
    if name_section[1] != 3:
        raise KernelCompileError(
            "emitted object section name table is not a string table"
        )
    name_data = image[name_section[4] : name_section[4] + name_section[5]]
    section_names = []
    for index, section in enumerate(sections):
        name_offset = section[0]
        if name_offset >= len(name_data):
            raise KernelCompileError(
                f"emitted object section {index} name is outside the string table"
            )
        terminator = name_data.find(b"\0", name_offset)
        if terminator < 0:
            raise KernelCompileError(
                f"emitted object section {index} name is not terminated"
            )
        try:
            section_names.append(
                name_data[name_offset:terminator].decode("ascii")
            )
        except UnicodeDecodeError as error:
            raise KernelCompileError(
                f"emitted object section {index} name is not ASCII"
            ) from error

    required_sections = {".symtab", ".strtab", ".shstrtab"}
    missing_sections = sorted(required_sections - set(section_names))
    if missing_sections:
        raise KernelCompileError(
            "emitted object is missing required section "
            + ", ".join(missing_sections)
        )

    symbol_counts = {}
    for section_index, section in enumerate(sections):
        if section[1] != 2:
            continue
        payload_offset = section[4]
        payload_size = section[5]
        string_index = section[6]
        first_nonlocal = section[7]
        entry_size = section[9]
        if entry_size != 16 or payload_size % entry_size != 0:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has invalid entries"
            )
        if string_index >= len(sections) or sections[string_index][1] != 3:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has no string table"
            )
        symbol_count = payload_size // entry_size
        symbol_counts[section_index] = symbol_count
        if symbol_count == 0 or first_nonlocal > symbol_count:
            raise KernelCompileError(
                f"emitted object symbol table {section_index} has an invalid boundary"
            )
        string_section = sections[string_index]
        string_data = image[
            string_section[4] : string_section[4] + string_section[5]
        ]
        for symbol_index in range(symbol_count):
            (
                symbol_name,
                symbol_value,
                symbol_size,
                _symbol_info,
                _symbol_other,
                symbol_section,
            ) = struct.unpack_from(
                "<IIIBBH",
                image,
                payload_offset + symbol_index * entry_size,
            )
            if (
                symbol_name >= len(string_data)
                or string_data.find(b"\0", symbol_name) < 0
            ):
                raise KernelCompileError(
                    f"emitted object symbol {symbol_index} has an invalid name"
                )
            if symbol_section < 0xFF00:
                if symbol_section >= len(sections):
                    raise KernelCompileError(
                        f"emitted object symbol {symbol_index} has an invalid section"
                    )
                if symbol_section != 0:
                    target_size = sections[symbol_section][5]
                    if (
                        symbol_value > target_size
                        or symbol_size > target_size - symbol_value
                    ):
                        raise KernelCompileError(
                            f"emitted object symbol {symbol_index} exceeds its section"
                        )
            elif symbol_section not in (0xFFF1, 0xFFF2):
                raise KernelCompileError(
                    f"emitted object symbol {symbol_index} has an unsupported section"
                )
    if not symbol_counts:
        raise KernelCompileError("emitted object has no symbol table")

    for section_index, section in enumerate(sections):
        if section[1] == 4:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} uses RELA"
            )
        if section[1] != 9:
            continue
        payload_offset = section[4]
        payload_size = section[5]
        symbol_table_index = section[6]
        target_index = section[7]
        entry_size = section[9]
        if entry_size != 8 or payload_size % entry_size != 0:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has invalid entries"
            )
        if symbol_table_index not in symbol_counts:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has no symbol table"
            )
        if target_index == 0 or target_index >= len(sections):
            raise KernelCompileError(
                f"emitted object relocation section {section_index} has no target"
            )
        target = sections[target_index]
        if target[1] == 8:
            raise KernelCompileError(
                f"emitted object relocation section {section_index} targets NOBITS"
            )
        relocation_count = payload_size // entry_size
        for relocation_index in range(relocation_count):
            relocation_offset, relocation_info = struct.unpack_from(
                "<II",
                image,
                payload_offset + relocation_index * entry_size,
            )
            if relocation_offset + 4 > target[5]:
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} "
                    "is outside its target"
                )
            symbol_index = relocation_info >> 8
            if symbol_index >= symbol_counts[symbol_table_index]:
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} "
                    "has an invalid symbol"
                )
            relocation_type = relocation_info & 0xFF
            if relocation_type not in (1, 2):
                raise KernelCompileError(
                    f"emitted object relocation {relocation_index} uses "
                    f"unsupported i386 type {relocation_type}"
                )
            addend = struct.unpack_from(
                "<i",
                image,
                target[4] + relocation_offset,
            )[0]
            expected_addend = 0 if relocation_type == 1 else -4
            if addend != expected_addend:
                description = (
                    "absolute"
                    if relocation_type == 1
                    else "PC-relative"
                )
                raise KernelCompileError(
                    f"{description} relocation addend is {addend}, "
                    f"expected {expected_addend}"
                )


def validate_i386_relocatable(path: Path) -> None:
    validate_i386_relocatable_bytes(_read_bytes(path))


def _root_path(root: Path) -> Path:
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise KernelCompileError(
            f"repository root cannot be resolved: {error}"
        ) from error
    if not resolved.is_dir():
        raise KernelCompileError(
            f"repository root is not a directory: {resolved}"
        )
    return resolved


def _source_path(root: Path, source: Path) -> tuple[Path, str]:
    candidate = source if source.is_absolute() else root / source
    if candidate.is_symlink():
        raise KernelCompileError("approved source may not be a symlink")
    try:
        resolved = candidate.resolve(strict=True)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise KernelCompileError(
            f"source must resolve inside repository root: {source}"
        ) from error
    relative_name = relative.as_posix()
    if relative_name not in APPROVED_KERNEL_COMPILE_SOURCES:
        raise KernelCompileError(
            "source is outside the approved CupidC kernel cohort: "
            f"{relative_name}"
        )
    if not resolved.is_file():
        raise KernelCompileError(f"approved source is not a file: {relative_name}")
    return resolved, "/" + relative_name


def _output_path(root: Path, output: Path) -> tuple[Path, str]:
    candidate = output if output.is_absolute() else root / output
    if candidate.is_symlink():
        raise KernelCompileError("output may not be a symlink")
    try:
        parent = candidate.parent.resolve(strict=True)
        resolved = (parent / candidate.name).resolve(strict=False)
        relative = resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise KernelCompileError(
            f"output must stay inside repository root: {output}"
        ) from error
    if not parent.is_dir():
        raise KernelCompileError(f"output parent is not a directory: {parent}")
    if not relative.parts or resolved == root:
        raise KernelCompileError("output must name a file inside repository root")
    if resolved.exists() and not resolved.is_file():
        raise KernelCompileError(f"output is not a regular file: {resolved}")
    if resolved.suffix != ".o":
        raise KernelCompileError("kernel compiler output must use the .o suffix")
    return resolved, "/" + relative.as_posix()


def _kernel_input_paths(
    root: Path,
    source_name: str,
) -> tuple[Path, ...]:
    closure = FROZEN_KERNEL_INPUT_CLOSURES.get(source_name)
    if closure is None:
        return ()
    paths = []
    for relative_name in (source_name, *closure):
        path = root / relative_name
        if path.is_symlink():
            raise KernelCompileError(
                f"kernel input may not be a symlink: {relative_name}"
            )
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(root)
        except (OSError, ValueError) as error:
            raise KernelCompileError(
                f"kernel input is unavailable: {relative_name}"
            ) from error
        if not resolved.is_file():
            raise KernelCompileError(
                f"kernel input is not a file: {relative_name}"
            )
        paths.append(resolved)
    return tuple(paths)


def _capture_kernel_inputs(
    paths: Sequence[Path],
) -> dict[Path, bytes]:
    captured = {}
    for path in paths:
        try:
            captured[path] = path.read_bytes()
        except OSError as error:
            raise KernelCompileError(
                f"cannot read kernel input {path}: {error}"
            ) from error
    return captured


def _write_kernel_inputs(
    root: Path,
    frozen_root: Path,
    captured: dict[Path, bytes],
) -> None:
    for path, payload in captured.items():
        target = frozen_root / path.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)


def _compiler_root_for(executor: SeedExecutor, path: Path) -> str:
    mapper = getattr(executor, "compiler_root_for", None)
    if callable(mapper):
        return str(mapper(path))
    return str(path.resolve())


def compile_kernel_source(
    root: Path,
    source: Path,
    output: Path,
    *,
    manifest: Path | None = None,
    executor: SeedExecutor | None = None,
    timeout: int | None = None,
) -> None:
    """Compile one approved source and atomically publish a checked object."""
    root = _root_path(root)
    source, logical_source = _source_path(root, source)
    output, _logical_output = _output_path(root, output)
    if source == output:
        raise KernelCompileError("output may not replace an approved source")
    source_name = logical_source.lstrip("/")
    if timeout is None:
        timeout = (
            GENERATED_KERNEL_TIMEOUT_SECONDS
            if source_name in APPROVED_GENERATED_KERNEL_SOURCES
            else DEFAULT_TIMEOUT_SECONDS
        )
    if timeout <= 0:
        raise KernelCompileError("compiler timeout must be positive")

    input_paths = _kernel_input_paths(root, source_name)
    captured_inputs = _capture_kernel_inputs(input_paths)
    manifest_path = (
        manifest.resolve()
        if manifest is not None
        else root
        / "bootstrap"
        / "seeds"
        / "i386-linux"
        / "manifest.json"
    )
    active_executor = executor if executor is not None else SeedExecutor(root)
    try:
        with tempfile.TemporaryDirectory(
            prefix="cupidc-kernel-seed-"
        ) as seed_temporary:
            try:
                seed_inputs = freeze_seed_inputs(
                    manifest_path, Path(seed_temporary)
                )
            except BootstrapError as error:
                raise KernelCompileError(
                    f"checked seed verification failed: {error}"
                ) from error
            seed = seed_inputs.tools.get("cupidc")
            if seed is None:
                raise KernelCompileError(
                    "checked seed verification did not return CupidC"
                )

            with tempfile.TemporaryDirectory(
                prefix=f".{output.name}.cupidc-",
                dir=output.parent,
            ) as temporary:
                temporary_root = Path(temporary)
                if captured_inputs:
                    _write_kernel_inputs(
                        root,
                        temporary_root,
                        captured_inputs,
                    )
                    temporary_output = (
                        temporary_root / ".output" / output.name
                    )
                    temporary_output.parent.mkdir()
                    logical_temporary = f"/.output/{output.name}"
                    compiler_root = _compiler_root_for(
                        active_executor,
                        temporary_root,
                    )
                else:
                    temporary_output = temporary_root / output.name
                    logical_temporary = (
                        "/" + temporary_output.relative_to(root).as_posix()
                    )
                    compiler_root = active_executor.compiler_root
                arguments = build_compile_arguments(
                    logical_source,
                    logical_temporary,
                    compiler_root,
                )
                try:
                    result = active_executor.run(seed, arguments, timeout)
                except subprocess.TimeoutExpired as error:
                    raise KernelCompileError(
                        f"CupidC timed out after {timeout} seconds for "
                        f"{logical_source.lstrip('/')}"
                    ) from error
                except OSError as error:
                    raise KernelCompileError(
                        f"CupidC could not run for "
                        f"{logical_source.lstrip('/')}: {error}"
                    ) from error
                if result.returncode != 0:
                    details = (result.stderr or "").strip()
                    if not details:
                        details = (result.stdout or "").strip()
                    suffix = f": {details}" if details else ""
                    raise KernelCompileError(
                        f"CupidC failed for {logical_source.lstrip('/')} "
                        f"with status {result.returncode}{suffix}"
                    )
                if (
                    temporary_output.is_symlink()
                    or not temporary_output.is_file()
                ):
                    raise KernelCompileError(
                        f"CupidC did not publish an object for "
                        f"{logical_source.lstrip('/')}"
                    )
                try:
                    validate_i386_relocatable(temporary_output)
                except KernelCompileError as error:
                    raise KernelCompileError(
                        f"emitted object is invalid for "
                        f"{logical_source.lstrip('/')}: {error}"
                    ) from error
                if (
                    captured_inputs
                    and _capture_kernel_inputs(input_paths)
                    != captured_inputs
                ):
                    raise KernelCompileError(
                        f"kernel inputs changed while compiling "
                        f"{source_name}"
                    )
                os.replace(temporary_output, output)
    except OSError as error:
        raise KernelCompileError(
            f"could not publish kernel object {output}: {error}"
        ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compile one approved kernel source with the checked CupidC seed."
        )
    )
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument(
        "--timeout",
        type=int,
        help=(
            "compiler time limit in seconds; defaults to 180 for checked-in "
            "sources and 600 for generated kernel symbols"
        ),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        compile_kernel_source(
            arguments.root,
            arguments.source,
            arguments.output,
            manifest=arguments.manifest,
            timeout=arguments.timeout,
        )
    except KernelCompileError as error:
        print(f"CupidC kernel compile failed: {error}", file=sys.stderr)
        return 1
    print(f"CupidC kernel object: {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

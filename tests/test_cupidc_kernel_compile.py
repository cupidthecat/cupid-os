import contextlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import cupidc_kernel_compile as kernel_compile


REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-linux"
    / "manifest.json"
)

CRYPTO_SOURCES = (
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
SMP_SOURCES = (
    "kernel/smp/acpi.cc",
    "kernel/smp/mp_tables.cc",
)
OPERAND_FREE_SOURCES = (
    "drivers/e1000.cc",
    "kernel/gui/desktop.cc",
    "kernel/network/socket.cc",
    "kernel/network/tcp.cc",
)
PORT_IO_SOURCES = (
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
COMPILER_READY_SOURCES = (
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
TOOLCHAIN_KERNEL_SOURCES = (
    "toolchain/ctool.c",
    "toolchain/cupidasm.c",
    "toolchain/cupiddis.c",
    "toolchain/elf32.c",
    "toolchain/x86.c",
)
SOURCE_DRIVEN_SOURCES = (
    "drivers/serial.cc",
    "drivers/timer.cc",
    "kernel/core/app_launch.cc",
    "kernel/core/panic.cc",
    "kernel/core/process.cc",
    "kernel/cpu/idt.cc",
    "kernel/cpu/irq.cc",
    "kernel/cpu/ksyms.cc",
    "kernel/cpu/pic.cc",
    "kernel/fs/fat16.cc",
    "kernel/fs/iso9660.cc",
    "kernel/fs/loopdev.cc",
    "kernel/gfx/deflate.cc",
    "kernel/gfx/gfx2d.cc",
    "kernel/gfx/png.cc",
    "kernel/gui/ed.cc",
    "kernel/lang/as.cc",
    "kernel/lang/cupidc.cc",
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
GENERATED_KERNEL_SOURCES = (
    "kernel/cpu/ksyms_data.cc",
)
GENERATED_KERNEL_INPUT_CLOSURES = {
    "kernel/cpu/ksyms_data.cc": (
        "kernel/cpu/ksyms.h",
        "kernel/core/types.h",
    ),
}
NEW_PRODUCTION_SOURCES = (
    COMPILER_READY_SOURCES + TOOLCHAIN_KERNEL_SOURCES
)
KERNEL_SOURCES = tuple(
    sorted(
        CRYPTO_SOURCES
        + SMP_SOURCES
        + OPERAND_FREE_SOURCES
        + PORT_IO_SOURCES
        + COMPILER_READY_SOURCES
        + TOOLCHAIN_KERNEL_SOURCES
        + SOURCE_DRIVEN_SOURCES
    )
)

OPERAND_FREE_DEPENDENCIES = {
    "drivers/e1000.cc": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h",
    ),
    "kernel/gui/desktop.cc": (
        "drivers/keyboard.h",
        "drivers/mouse.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/app_launch.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/bmp.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gfx/graphics.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/gui_widgets.h",
        "kernel/gui/terminal_app.h",
        "kernel/gui/ui.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/dis.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/util/calendar.h",
    ),
    "kernel/network/socket.cc": (
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/crypto/sha256.h",
        "kernel/crypto/x509.h",
        "kernel/crypto/x509_chain.h",
        "kernel/mm/memory.h",
        "kernel/network/socket.h",
        "kernel/network/tcp.h",
        "kernel/network/udp.h",
        "kernel/smp/bkl.h",
        "kernel/tls/tls_ctx.h",
        "kernel/tls/tls_record.h",
    ),
    "kernel/network/tcp.cc": (
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/cpu.h",
        "kernel/cpu/isr.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/tcp.h",
        "kernel/smp/bkl.h",
    ),
}

PORT_IO_DEPENDENCIES = {
    "drivers/ata.cc": (
        "drivers/ata.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.h",
    ),
    "drivers/keyboard.cc": (
        "drivers/keyboard.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/vga.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/lang/shell.h",
        "kernel/util/calendar.h",
    ),
    "drivers/mouse.cc": (
        "drivers/mouse.h",
        "drivers/serial.h",
        "drivers/vga.h",
        "kernel/core/ports.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/pic.h",
        "kernel/gfx/graphics.h",
    ),
    "drivers/pci.cc": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
    ),
    "drivers/pit.cc": (
        "drivers/pit.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
    ),
    "drivers/rtc.cc": (
        "drivers/rtc.h",
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
    ),
    "drivers/rtl8139.cc": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h",
    ),
    "drivers/speaker.cc": (
        "drivers/pit.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
    ),
    "drivers/vga.cc": (
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/simd.h",
        "kernel/mm/memory.h",
    ),
    "kernel/audio/ac97.cc": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/audio/ac97.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
    ),
    "kernel/core/syscall.cc": (
        "drivers/ata.h",
        "drivers/pci.h",
        "drivers/pit.h",
        "drivers/serial.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/syscall.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/vfs.h",
        "kernel/fs/vfs_helpers.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/network/arp.h",
        "kernel/network/dns.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/udp.h",
        "kernel/smp/bkl.h",
        "kernel/smp/lapic.h",
    ),
    "kernel/lang/shell.cc": (
        "drivers/keyboard.h",
        "drivers/pci.h",
        "drivers/rtc.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "drivers/vga.h",
        "kernel/core/app_launch.h",
        "kernel/core/assert.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/process.h",
        "kernel/core/string.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/cpu/math.h",
        "kernel/fs/blockcache.h",
        "kernel/fs/blockdev.h",
        "kernel/fs/fat16.h",
        "kernel/fs/fs.h",
        "kernel/fs/vfs.h",
        "kernel/gfx/gfx2d.h",
        "kernel/gui/ansi.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/terminal_app.h",
        "kernel/lang/as.h",
        "kernel/lang/cupidc.h",
        "kernel/lang/cupidscript.h",
        "kernel/lang/cupidscript_arrays.h",
        "kernel/lang/cupidscript_jobs.h",
        "kernel/lang/cupidscript_streams.h",
        "kernel/lang/dis.h",
        "kernel/lang/exec.h",
        "kernel/lang/shell.h",
        "kernel/mm/memory.h",
        "kernel/mm/swap.h",
        "kernel/network/arp.h",
        "kernel/network/dns.h",
        "kernel/network/icmp.h",
        "kernel/network/ip.h",
        "kernel/network/net_if.h",
        "kernel/network/socket.h",
        "kernel/network/sshd.h",
        "kernel/smp/bkl.h",
        "kernel/smp/percpu.h",
        "kernel/smp/smp.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
        "kernel/util/calendar.h",
    ),
    "kernel/usb/ehci.cc": (
        "drivers/pci.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
    ),
    "kernel/usb/uhci.cc": (
        "drivers/pci.h",
        "drivers/serial.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/panic.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/usb/usb.h",
        "kernel/usb/usb_hc.h",
    ),
}

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


def _align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def _valid_elf32_object():
    text = struct.pack("<Ii", 0, -4)
    relocations = struct.pack("<IIII", 0, (2 << 8) | 1, 4, (2 << 8) | 2)
    strings = b"\0entry\0external\0"
    section_strings = b"\0.text\0.rel.text\0.symtab\0.strtab\0.shstrtab\0"

    text_offset = 52
    relocation_offset = text_offset + len(text)
    symbol_offset = relocation_offset + len(relocations)
    symbols = bytearray(3 * 16)
    struct.pack_into("<IIIBBH", symbols, 16, 1, 0, len(text), 0x12, 0, 1)
    struct.pack_into("<IIIBBH", symbols, 32, 7, 0, 0, 0x10, 0, 0)
    string_offset = symbol_offset + len(symbols)
    section_string_offset = string_offset + len(strings)
    section_offset = _align(section_string_offset + len(section_strings), 4)
    image = bytearray(section_offset + 6 * 40)
    image[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        1,
        3,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        6,
        5,
    )
    image[text_offset:relocation_offset] = text
    image[relocation_offset:symbol_offset] = relocations
    image[symbol_offset:string_offset] = symbols
    image[string_offset:section_string_offset] = strings
    image[section_string_offset : section_string_offset + len(section_strings)] = (
        section_strings
    )

    sections = (
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 6, 0, text_offset, len(text), 0, 0, 4, 0),
        (
            7,
            9,
            0,
            0,
            relocation_offset,
            len(relocations),
            3,
            1,
            4,
            8,
        ),
        (17, 2, 0, 0, symbol_offset, len(symbols), 4, 1, 4, 16),
        (25, 3, 0, 0, string_offset, len(strings), 0, 0, 1, 0),
        (
            33,
            3,
            0,
            0,
            section_string_offset,
            len(section_strings),
            0,
            0,
            1,
            0,
        ),
    )
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


def _data_only_elf32_object(symbol_size=4):
    data = b"\x10\x20\x30\x40"
    strings = b"\0font_table\0"
    section_strings = b"\0.rodata\0.symtab\0.strtab\0.shstrtab\0"

    data_offset = 52
    symbol_offset = _align(data_offset + len(data), 4)
    symbols = bytearray(2 * 16)
    struct.pack_into(
        "<IIIBBH",
        symbols,
        16,
        1,
        0,
        symbol_size,
        0x11,
        0,
        1,
    )
    string_offset = symbol_offset + len(symbols)
    section_string_offset = string_offset + len(strings)
    section_offset = _align(section_string_offset + len(section_strings), 4)
    image = bytearray(section_offset + 5 * 40)
    image[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        1,
        3,
        1,
        0,
        0,
        section_offset,
        0,
        52,
        0,
        0,
        40,
        5,
        4,
    )
    image[data_offset : data_offset + len(data)] = data
    image[symbol_offset:string_offset] = symbols
    image[string_offset:section_string_offset] = strings
    image[section_string_offset : section_string_offset + len(section_strings)] = (
        section_strings
    )

    sections = (
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 2, 0, data_offset, len(data), 0, 0, 4, 0),
        (9, 2, 0, 0, symbol_offset, len(symbols), 3, 1, 4, 16),
        (17, 3, 0, 0, string_offset, len(strings), 0, 0, 1, 0),
        (
            25,
            3,
            0,
            0,
            section_string_offset,
            len(section_strings),
            0,
            0,
            1,
            0,
        ),
    )
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


class FakeExecutor:
    def __init__(self, root, result=None, payload=None, events=None):
        self.root = root
        self.compiler_root = "/native/repository"
        self.result = result or subprocess.CompletedProcess([], 0, "", "")
        self.payload = payload
        self.events = events if events is not None else []
        self.calls = []

    def compiler_root_for(self, path):
        return str(path)

    def run(self, executable, arguments, timeout):
        self.events.append("run")
        self.calls.append((executable, tuple(arguments), timeout))
        if self.payload is not None:
            logical_output = arguments[arguments.index("-o") + 1]
            requested_root = Path(
                arguments[arguments.index("--root") + 1]
            )
            compiler_root = (
                requested_root
                if requested_root.is_dir()
                else self.root
            )
            destination = compiler_root / logical_output.lstrip("/")
            destination.write_bytes(self.payload)
        return self.result


class KernelCompileCommandTests(unittest.TestCase):
    def test_approved_sources_and_profile_are_exact(self):
        self.assertEqual(kernel_compile.APPROVED_CRYPTO_SOURCES, CRYPTO_SOURCES)
        self.assertEqual(kernel_compile.APPROVED_SMP_SOURCES, SMP_SOURCES)
        self.assertEqual(
            kernel_compile.APPROVED_OPERAND_FREE_SOURCES,
            OPERAND_FREE_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_PORT_IO_SOURCES,
            PORT_IO_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_COMPILER_READY_SOURCES,
            COMPILER_READY_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_TOOLCHAIN_KERNEL_SOURCES,
            TOOLCHAIN_KERNEL_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_SOURCE_DRIVEN_SOURCES,
            SOURCE_DRIVEN_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_GENERATED_KERNEL_SOURCES,
            GENERATED_KERNEL_SOURCES,
        )
        self.assertEqual(
            kernel_compile.GENERATED_KERNEL_INPUT_CLOSURES,
            GENERATED_KERNEL_INPUT_CLOSURES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_KERNEL_SOURCES,
            KERNEL_SOURCES,
        )
        self.assertEqual(
            kernel_compile.APPROVED_KERNEL_COMPILE_SOURCES,
            tuple(sorted(KERNEL_SOURCES + GENERATED_KERNEL_SOURCES)),
        )
        self.assertEqual(len(KERNEL_SOURCES), 144)
        self.assertEqual(len(set(KERNEL_SOURCES)), 144)
        self.assertEqual(kernel_compile.KERNEL_I386_ARGUMENTS, KERNEL_I386_ARGUMENTS)

        command = kernel_compile.build_compile_arguments(
            "/kernel/crypto/ct.cc",
            "/build/cupid/ct.o",
            "/native/repository",
        )
        self.assertEqual(
            command,
            (
                "-c",
                "/kernel/crypto/ct.cc",
                "-o",
                "/build/cupid/ct.o",
                *KERNEL_I386_ARGUMENTS,
                "--root",
                "/native/repository",
            ),
        )

    def test_production_owned_roots_use_the_cupidc_extension(self):
        renamed_sources = (
            CRYPTO_SOURCES
            + SMP_SOURCES
            + OPERAND_FREE_SOURCES
            + PORT_IO_SOURCES
            + COMPILER_READY_SOURCES
        )
        self.assertEqual(len(renamed_sources), 111)
        self.assertEqual(len(set(renamed_sources)), 111)
        self.assertEqual(
            tuple(
                source
                for source in KERNEL_SOURCES
                if Path(source).suffix == ".c"
            ),
            TOOLCHAIN_KERNEL_SOURCES,
        )
        self.assertEqual(
            sum(Path(source).suffix == ".cc" for source in KERNEL_SOURCES),
            139,
        )

        for source in renamed_sources:
            with self.subTest(source=source):
                self.assertEqual(Path(source).suffix, ".cc")
                self.assertTrue((REPO_ROOT / source).is_file())
                self.assertFalse(
                    (REPO_ROOT / Path(source).with_suffix(".c")).exists()
                )

    def test_wsl_invocation_uses_a_private_staged_seed(self):
        command = kernel_compile.build_wsl_invocation(
            "/mnt/c/repository",
            "/mnt/c/repository/bootstrap/seeds/i386-linux/cupidc.elf",
            ("-c", "/kernel/crypto/ct.cc"),
        )
        self.assertEqual(command[:4], ("wsl", "-e", "sh", "-c"))
        self.assertIn("umask 077", command[4])
        self.assertIn("mktemp -d", command[4])
        self.assertIn('chmod 700 "$private"', command[4])
        self.assertIn('trap \'rm -rf -- "$private"\'', command[4])
        self.assertNotIn("$$", command[4])
        self.assertEqual(command[6], "/mnt/c/repository")
        self.assertEqual(
            command[7],
            "/mnt/c/repository/bootstrap/seeds/i386-linux/cupidc.elf",
        )
        self.assertEqual(command[-2:], ("-c", "/kernel/crypto/ct.cc"))

    def test_native_executor_runs_the_checked_seed_directly(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            seed = root / "cupidc.elf"
            executor = kernel_compile.SeedExecutor.__new__(
                kernel_compile.SeedExecutor
            )
            executor.root = root
            executor.uses_wsl = False
            executor.compiler_root = str(root)
            completed = subprocess.CompletedProcess([], 0, "", "")

            with mock.patch.object(
                kernel_compile.subprocess,
                "run",
                return_value=completed,
            ) as run:
                result = executor.run(
                    seed,
                    ("-c", "/kernel/crypto/ct.cc"),
                    17,
                )

            self.assertIs(result, completed)
            run.assert_called_once_with(
                [
                    str(seed),
                    "-c",
                    "/kernel/crypto/ct.cc",
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=17,
            )


class KernelCompileMakefileTests(unittest.TestCase):
    def test_exact_approved_cohort_uses_the_checked_cupidc_wrapper(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn(
            "CUPIDC_KERNEL_COMPILE := $(PYTHON) "
            "tools/cupidc_kernel_compile.py --root .",
            makefile,
        )
        expected_compile_inputs = {
            "Makefile",
            "tools/cupidc_kernel_compile.py",
            "tools/kernel_cupidc_frontier.py",
            "tools/bootstrap_toolchain.py",
            "bootstrap/seeds/i386-linux/manifest.json",
            "bootstrap/seeds/i386-linux/cupidasm.elf",
            "bootstrap/seeds/i386-linux/cupidc.elf",
            "bootstrap/seeds/i386-linux/cupiddis.elf",
            "bootstrap/seeds/i386-linux/cupidld.elf",
            "bootstrap/seeds/i386-linux/cupidobj.elf",
        }
        compile_inputs_match = re.search(
            r"(?ms)^CUPIDC_KERNEL_COMPILE_INPUTS := (.+?)\n"
            r"(?=[A-Z][A-Z0-9_]*\s*[:?]?=)",
            makefile,
        )
        self.assertIsNotNone(compile_inputs_match)
        actual_compile_inputs = set(
            compile_inputs_match.group(1).replace("\\\n", " ").split()
        )
        self.assertEqual(actual_compile_inputs, expected_compile_inputs)
        recipe_pattern = re.compile(
            r"^\t\$\(CUPIDC_KERNEL_COMPILE\) --source (\S+) "
            r"--output (\S+)$",
            re.MULTILINE,
        )
        actual = {
            (source, output)
            for source, output in recipe_pattern.findall(makefile)
        }
        expected = {
            (source, str(Path(source).with_suffix(".o")).replace("\\", "/"))
            for source in KERNEL_SOURCES + GENERATED_KERNEL_SOURCES
        }
        self.assertEqual(actual, expected)

        logical_makefile = makefile.replace("\\\n", " ")
        for source, output in sorted(expected):
            rule_pattern = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf"[ \t]+\$\(CUPIDC_KERNEL_COMPILE_INPUTS\)"
                rf"\n\t\$\(CUPIDC_KERNEL_COMPILE\) "
                rf"--source {re.escape(source)} "
                rf"--output {re.escape(output)}$",
                re.MULTILINE,
            )
            self.assertRegex(logical_makefile, rule_pattern)

        self.assertIn(
            "kernel/crypto/ecdsa.o: kernel/crypto/ecdsa.cc "
            "kernel/crypto/ecdsa.h kernel/crypto/p256.h "
            "kernel/crypto/hmac.h kernel/crypto/sha256.h "
            "kernel/core/string.h kernel/core/types.h "
            "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
            makefile,
        )
        self.assertIn(
            "kernel/smp/mp_tables.o: kernel/smp/mp_tables.cc "
            "kernel/smp/mp_tables.h kernel/smp/ioapic.h "
            "kernel/smp/percpu.h kernel/core/process.h "
            "kernel/core/types.h drivers/serial.h "
            "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
            makefile,
        )
        self.assertIn(
            "kernel/smp/acpi.o: kernel/smp/acpi.cc kernel/smp/acpi.h "
            "kernel/smp/mp_tables.h kernel/smp/ioapic.h "
            "kernel/smp/percpu.h kernel/core/process.h "
            "kernel/core/types.h drivers/serial.h "
            "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
            makefile,
        )
        self.assertRegex(
            makefile,
            r"kernel/smp/bkl\.o \\\n"
            r"\s+kernel/smp/mp_tables\.o \\\n"
            r"\s+kernel/smp/acpi\.o \\\n"
            r"\s+kernel/smp/smp\.o",
        )

        for source, headers in OPERAND_FREE_DEPENDENCIES.items():
            output = Path(source).with_suffix(".o").as_posix()
            match = re.search(
                rf"^{re.escape(output)}: ([^\n]+)$",
                makefile,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, source)
            self.assertEqual(
                set(match.group(1).split()),
                {
                    source,
                    *headers,
                    "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
                },
            )

        for source, headers in PORT_IO_DEPENDENCIES.items():
            output = Path(source).with_suffix(".o").as_posix()
            match = re.search(
                rf"^{re.escape(output)}: ([^\n]+)$",
                logical_makefile,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, source)
            self.assertEqual(
                set(match.group(1).split()),
                {
                    source,
                    *headers,
                    "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
                },
            )

        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        audited_sources = {
            entry["path"]: entry for entry in audit["sources"]
        }

        def recursive_includes(source):
            closure = set()
            pending = list(audited_sources[source]["includes"])
            while pending:
                dependency = pending.pop()
                if dependency in closure:
                    continue
                closure.add(dependency)
                entry = audited_sources.get(dependency)
                if entry is not None:
                    pending.extend(entry["includes"])
            return closure

        for source in NEW_PRODUCTION_SOURCES + SOURCE_DRIVEN_SOURCES:
            output = Path(source).with_suffix(".o").as_posix()
            match = re.search(
                rf"^{re.escape(output)}: ([^\n]+)$",
                logical_makefile,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, source)
            self.assertEqual(
                set(match.group(1).split()),
                {
                    source,
                    *recursive_includes(source),
                    "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
                },
            )
        self.assertIn(
            "kernel/usb/usb_hc.h",
            recursive_includes("kernel/usb/usb.cc"),
        )

        for source in KERNEL_SOURCES + GENERATED_KERNEL_SOURCES:
            output = str(Path(source).with_suffix(".o")).replace("\\", "/")
            host_rule = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf"\n\t\$\(CC\) ",
                re.MULTILINE,
            )
            self.assertNotRegex(makefile, host_rule)

    def test_new_production_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in NEW_PRODUCTION_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.splitlines()
            if "tools/cupidc_kernel_compile.py" in line
        ]
        self.assertEqual(len(commands), len(NEW_PRODUCTION_SOURCES))
        for source in NEW_PRODUCTION_SOURCES:
            self.assertTrue(
                any(f"--source {source}" in command for command in commands),
                source,
            )
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

    def test_source_driven_targets_do_not_expand_to_host_tools(self):
        poison = "__host_tool_must_not_run__"
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in SOURCE_DRIVEN_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                *[
                    f"{variable}={poison}"
                    for variable in (
                        "CC",
                        "CXX",
                        "CPP",
                        "HOSTCC",
                        "HOSTCXX",
                        "ASM",
                        "LD",
                        "AR",
                        "NM",
                        "OBJCOPY",
                    )
                ],
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.splitlines()
            if "tools/cupidc_kernel_compile.py" in line
        ]
        self.assertEqual(len(commands), len(SOURCE_DRIVEN_SOURCES))
        for source in SOURCE_DRIVEN_SOURCES:
            self.assertEqual(
                sum(f"--source {source}" in command for command in commands),
                1,
                source,
            )
        self.assertNotIn(poison, result.stdout + result.stderr)

    def test_smp_targets_do_not_expand_to_the_host_compiler(self):
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                "kernel/smp/acpi.o",
                "kernel/smp/mp_tables.o",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.splitlines()
            if "tools/cupidc_kernel_compile.py" in line
        ]
        self.assertEqual(len(commands), 2)
        self.assertIn("--source kernel/smp/acpi.cc", commands[0])
        self.assertIn("--source kernel/smp/mp_tables.cc", commands[1])
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

    def test_operand_free_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in OPERAND_FREE_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.splitlines()
            if "tools/cupidc_kernel_compile.py" in line
        ]
        self.assertEqual(len(commands), len(OPERAND_FREE_SOURCES))
        for source in OPERAND_FREE_SOURCES:
            self.assertTrue(
                any(f"--source {source}" in command for command in commands),
                source,
            )
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

    def test_port_io_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in PORT_IO_SOURCES
        ]
        result = subprocess.run(
            [
                "make",
                "-B",
                "-n",
                "CC=__host_c_compiler_must_not_run__",
                *targets,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = [
            line
            for line in result.stdout.splitlines()
            if "tools/cupidc_kernel_compile.py" in line
        ]
        self.assertEqual(len(commands), len(PORT_IO_SOURCES))
        for source in PORT_IO_SOURCES:
            self.assertTrue(
                any(f"--source {source}" in command for command in commands),
                source,
            )
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )


class KernelCompileOperationTests(unittest.TestCase):
    def _root_fixture(self):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name).resolve()
        source = root / "kernel" / "crypto" / "ct.cc"
        source.parent.mkdir(parents=True)
        source.write_text("int ct_fixture;\n", encoding="utf-8")
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = root / "build" / "ct.o"
        output.parent.mkdir()
        return temporary, root, source, seed, manifest, output

    def test_manifest_and_seed_are_frozen_before_successful_execution(self):
        temporary, root, source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        events = []
        executor = FakeExecutor(
            root,
            payload=_valid_elf32_object(),
            events=events,
        )

        def freeze(path, snapshot_directory):
            self.assertEqual(path, manifest)
            frozen_seed = snapshot_directory / "cupidc.elf"
            frozen_seed.write_bytes(seed.read_bytes())
            events.append("freeze")
            return mock.Mock(tools={"cupidc": frozen_seed})

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=freeze,
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=executor,
            )

        self.assertEqual(events, ["freeze", "run"])
        self.assertEqual(output.read_bytes(), _valid_elf32_object())
        self.assertEqual(
            executor.calls[0][2],
            kernel_compile.DEFAULT_TIMEOUT_SECONDS,
        )
        self.assertNotEqual(executor.calls[0][0], seed)
        self.assertEqual(executor.calls[0][0].name, "cupidc.elf")
        arguments = executor.calls[0][1]
        self.assertEqual(arguments[0:2], ("-c", "/kernel/crypto/ct.cc"))
        self.assertEqual(
            arguments[arguments.index("--root") + 1],
            "/native/repository",
        )

    def test_unapproved_source_is_rejected_without_execution(self):
        temporary, root, _source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        executor = FakeExecutor(root)

        for relative in (
            "kernel/core/panic.c",
            "kernel/crypto/ct.c",
            "kernel/audio/nuked_opl3.c",
            "kernel/core/string.c",
            "kernel/crypto/new_cipher.c",
            "kernel/gfx/jpeg.c",
            "kernel/gui/terminal_ansi.c",
            "kernel/lang/as.c",
            "kernel/mm/paging.c",
            "kernel/smp/percpu.c",
        ):
            source = root / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("int unapproved;\n", encoding="utf-8")
            with self.subTest(source=relative), self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "source is outside the approved CupidC kernel cohort",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(executor.calls, [])

    def test_generated_kernel_symbol_inputs_are_compiled_from_one_frozen_closure(
        self,
    ):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        source = root / "kernel" / "cpu" / "ksyms_data.cc"
        source.parent.mkdir(parents=True)
        source.write_text(
            '#include "ksyms.h"\nconst int generated_symbol = 1;\n',
            encoding="utf-8",
        )
        header = source.parent / "ksyms.h"
        header.write_text('#include "types.h"\n', encoding="utf-8")
        types = root / "kernel" / "core" / "types.h"
        types.parent.mkdir(parents=True)
        types.write_text("typedef unsigned int uint32_t;\n", encoding="utf-8")
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.parent / "ksyms_data.o"
        captured = {}

        class ClosureExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                compiler_root = Path(
                    arguments[arguments.index("--root") + 1]
                )
                for relative in (
                    "kernel/cpu/ksyms_data.cc",
                    "kernel/cpu/ksyms.h",
                    "kernel/core/types.h",
                ):
                    captured[relative] = (
                        compiler_root / relative
                    ).read_bytes()
                return super().run(executable, arguments, timeout)

        executor = ClosureExecutor(root, payload=_data_only_elf32_object())

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=executor,
            )

        self.assertEqual(
            captured,
            {
                "kernel/cpu/ksyms_data.cc": source.read_bytes(),
                "kernel/cpu/ksyms.h": header.read_bytes(),
                "kernel/core/types.h": types.read_bytes(),
            },
        )
        self.assertEqual(output.read_bytes(), _data_only_elf32_object())
        self.assertEqual(
            executor.calls[0][2],
            kernel_compile.GENERATED_KERNEL_TIMEOUT_SECONDS,
        )
        compiler_root = Path(
            executor.calls[0][1][
                executor.calls[0][1].index("--root") + 1
            ]
        )
        self.assertNotEqual(compiler_root, root)

    def test_generated_kernel_symbol_drift_preserves_the_existing_object(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        source = root / "kernel" / "cpu" / "ksyms_data.cc"
        source.parent.mkdir(parents=True)
        source.write_text(
            '#include "ksyms.h"\nconst int generated_symbol = 1;\n',
            encoding="utf-8",
        )
        header = source.parent / "ksyms.h"
        header.write_text('#include "types.h"\n', encoding="utf-8")
        types = root / "kernel" / "core" / "types.h"
        types.parent.mkdir(parents=True)
        types.write_text("typedef unsigned int uint32_t;\n", encoding="utf-8")
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.parent / "ksyms_data.o"
        output.write_bytes(b"existing object")

        class DriftingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                header.write_text(
                    '#include "types.h"\nint changed;\n',
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = DriftingExecutor(
            root,
            payload=_data_only_elf32_object(),
        )
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "generated kernel inputs changed while compiling "
                "kernel/cpu/ksyms_data.cc",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )

        self.assertEqual(output.read_bytes(), b"existing object")

    def test_output_outside_root_is_rejected(self):
        temporary, root, source, _seed, manifest, _output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        outside = root.parent / "outside.o"

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "output must stay inside repository root",
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                outside,
                manifest=manifest,
                executor=FakeExecutor(root),
            )

    def test_compiler_failure_preserves_existing_output(self):
        temporary, root, source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        executor = FakeExecutor(
            root,
            result=subprocess.CompletedProcess(
                [],
                1,
                "",
                "/kernel/crypto/ct.cc:9: error CTD000006: unsupported",
            ),
        )

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "CupidC failed for kernel/crypto/ct.cc with status 1.*CTD000006",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_manifest_failure_preserves_output_without_running_compiler(self):
        temporary, root, source, _seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        executor = FakeExecutor(root)

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=kernel_compile.BootstrapError(
                "SHA-256 differs for cupidc.elf"
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "checked seed verification failed.*SHA-256 differs",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(executor.calls, [])
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_invalid_object_preserves_existing_output(self):
        temporary, root, source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        output.write_bytes(b"existing object")
        executor = FakeExecutor(root, payload=b"not an object")

        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=lambda _manifest, snapshot: mock.Mock(
                tools={"cupidc": shutil.copyfile(seed, snapshot / seed.name)}
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "emitted object is invalid",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_public_validator_rejects_a_bad_pc_relative_addend(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "bad.o"
        image = bytearray(_valid_elf32_object())
        struct.pack_into("<i", image, 56, 0)
        path.write_bytes(image)
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "PC-relative relocation addend is 0, expected -4",
        ):
            kernel_compile.validate_i386_relocatable(path)

    def test_public_validator_accepts_a_data_only_relocatable_object(self):
        kernel_compile.validate_i386_relocatable_bytes(
            _data_only_elf32_object()
        )

    def test_public_validator_rejects_a_data_symbol_outside_its_section(self):
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "symbol 1 exceeds its section",
        ):
            kernel_compile.validate_i386_relocatable_bytes(
                _data_only_elf32_object(symbol_size=5)
            )


class KernelCompileCliTests(unittest.TestCase):
    def test_cli_reports_a_clear_failure(self):
        error = io.StringIO()
        with mock.patch.object(
            kernel_compile,
            "compile_kernel_source",
            side_effect=kernel_compile.KernelCompileError("fixture failure"),
        ):
            with contextlib.redirect_stderr(error):
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        "kernel/crypto/ct.cc",
                        "--output",
                        "build/ct.o",
                    ]
                )
        self.assertEqual(status, 1)
        self.assertEqual(
            error.getvalue(),
            "CupidC kernel compile failed: fixture failure\n",
        )

    def test_real_checked_seed_compiles_hmac_with_relocations_when_available(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-compile-test-",
            dir=REPO_ROOT,
        ) as temporary:
            output = Path(temporary) / "hmac.o"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        "kernel/crypto/hmac.cc",
                        "--output",
                        str(output),
                    ]
                )
            self.assertEqual(status, 0)
            self.assertIn("CupidC kernel object:", stdout.getvalue())
            kernel_compile.validate_i386_relocatable(output)

    def test_real_checked_seed_compiles_the_approved_smp_sources_when_available(
        self,
    ):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-smp-compile-test-",
            dir=REPO_ROOT,
        ) as temporary:
            for source in SMP_SOURCES:
                output = Path(temporary) / (Path(source).stem + ".o")
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        source,
                        "--output",
                        str(output),
                    ]
                )
                self.assertEqual(status, 0)
                kernel_compile.validate_i386_relocatable(output)

    def test_real_checked_seed_compiles_operand_free_sources_when_available(
        self,
    ):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-operand-free-test-",
            dir=REPO_ROOT,
        ) as temporary:
            for source in OPERAND_FREE_SOURCES:
                output = Path(temporary) / (Path(source).stem + ".o")
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        source,
                        "--output",
                        str(output),
                    ]
                )
                self.assertEqual(status, 0)
                kernel_compile.validate_i386_relocatable(output)

    def test_real_checked_seed_compiles_port_io_sources_when_available(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".cupidc-kernel-port-io-test-",
            dir=REPO_ROOT,
        ) as temporary:
            for source in PORT_IO_SOURCES:
                output_name = (
                    Path(source)
                    .with_suffix(".o")
                    .as_posix()
                    .replace("/", "-")
                )
                output = Path(temporary) / output_name
                status = kernel_compile.main(
                    [
                        "--root",
                        str(REPO_ROOT),
                        "--source",
                        source,
                        "--output",
                        str(output),
                    ]
                )
                self.assertEqual(status, 0, source)
                kernel_compile.validate_i386_relocatable(output)


if __name__ == "__main__":
    unittest.main()

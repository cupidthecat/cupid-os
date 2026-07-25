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
    "kernel/crypto/aes.c",
    "kernel/crypto/aes_gcm.c",
    "kernel/crypto/asn1.c",
    "kernel/crypto/bigint.c",
    "kernel/crypto/chacha20.c",
    "kernel/crypto/chacha20poly1305.c",
    "kernel/crypto/csprng.c",
    "kernel/crypto/ct.c",
    "kernel/crypto/ecdsa.c",
    "kernel/crypto/ed25519.c",
    "kernel/crypto/hkdf.c",
    "kernel/crypto/hmac.c",
    "kernel/crypto/p256.c",
    "kernel/crypto/poly1305.c",
    "kernel/crypto/rsa.c",
    "kernel/crypto/sha256.c",
    "kernel/crypto/sha512.c",
    "kernel/crypto/x25519.c",
    "kernel/crypto/x509.c",
    "kernel/crypto/x509_chain.c",
)
SMP_SOURCES = (
    "kernel/smp/acpi.c",
    "kernel/smp/mp_tables.c",
)
OPERAND_FREE_SOURCES = (
    "drivers/e1000.c",
    "kernel/gui/desktop.c",
    "kernel/network/socket.c",
    "kernel/network/tcp.c",
)
PORT_IO_SOURCES = (
    "drivers/ata.c",
    "drivers/keyboard.c",
    "drivers/mouse.c",
    "drivers/pci.c",
    "drivers/pit.c",
    "drivers/rtc.c",
    "drivers/rtl8139.c",
    "drivers/speaker.c",
    "drivers/vga.c",
    "kernel/audio/ac97.c",
    "kernel/core/syscall.c",
    "kernel/lang/shell.c",
    "kernel/usb/ehci.c",
    "kernel/usb/uhci.c",
)
COMPILER_READY_SOURCES = (
    "kernel/audio/memio.c",
    "kernel/audio/midiopl.c",
    "kernel/audio/mixer.c",
    "kernel/audio/mus2midi.c",
    "kernel/audio/opl_smoke.c",
    "kernel/cpu/math.c",
    "kernel/fs/blockcache.c",
    "kernel/fs/blockdev.c",
    "kernel/fs/devfs.c",
    "kernel/fs/fat16_vfs.c",
    "kernel/fs/fs.c",
    "kernel/fs/homefs.c",
    "kernel/fs/iso9660_vfs.c",
    "kernel/fs/ramfs.c",
    "kernel/fs/vfs.c",
    "kernel/fs/vfs_helpers.c",
    "kernel/gfx/bmp.c",
    "kernel/gfx/font_8x8.c",
    "kernel/gfx/fontsys.c",
    "kernel/gfx/gfx2d_assets.c",
    "kernel/gfx/gfx2d_effects.c",
    "kernel/gfx/gfx2d_icons.c",
    "kernel/gfx/gfx2d_transform.c",
    "kernel/gfx/graphics.c",
    "kernel/gfx/ttf.c",
    "kernel/gui/ansi.c",
    "kernel/gui/clipboard.c",
    "kernel/gui/ctxt_image_worker.c",
    "kernel/gui/gui.c",
    "kernel/gui/gui_containers.c",
    "kernel/gui/gui_events.c",
    "kernel/gui/gui_menus.c",
    "kernel/gui/gui_themes.c",
    "kernel/gui/gui_widgets.c",
    "kernel/gui/terminal_app.c",
    "kernel/gui/ui.c",
    "kernel/lang/as_elf.c",
    "kernel/lang/ctool_kernel.c",
    "kernel/lang/cupidc_elf.c",
    "kernel/lang/cupidscript_arrays.c",
    "kernel/lang/cupidscript_exec.c",
    "kernel/lang/cupidscript_jobs.c",
    "kernel/lang/cupidscript_lex.c",
    "kernel/lang/cupidscript_parse.c",
    "kernel/lang/cupidscript_runtime.c",
    "kernel/lang/cupidscript_streams.c",
    "kernel/lang/cupidscript_strings.c",
    "kernel/lang/dis.c",
    "kernel/lang/exec.c",
    "kernel/lang/godspeak.c",
    "kernel/mm/swap.c",
    "kernel/mm/swap_disk.c",
    "kernel/network/arp.c",
    "kernel/network/dhcp.c",
    "kernel/network/dns.c",
    "kernel/network/icmp.c",
    "kernel/network/ip.c",
    "kernel/network/net_if.c",
    "kernel/smp/ioapic.c",
    "kernel/tls/tls_ca_bundle_data.c",
    "kernel/tls/tls_ctx.c",
    "kernel/tls/tls_handshake.c",
    "kernel/tls/tls_kdf.c",
    "kernel/tls/tls_record.c",
    "kernel/tls/tls_selftest.c",
    "kernel/tls/tls12_handshake.c",
    "kernel/usb/usb.c",
    "kernel/usb/usb_hid.c",
    "kernel/usb/usb_hub.c",
    "kernel/usb/usb_msc.c",
    "kernel/util/calendar.c",
)
TOOLCHAIN_KERNEL_SOURCES = (
    "toolchain/ctool.c",
    "toolchain/cupidasm.c",
    "toolchain/cupiddis.c",
    "toolchain/elf32.c",
    "toolchain/x86.c",
)
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
    )
)

OPERAND_FREE_DEPENDENCIES = {
    "drivers/e1000.c": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h",
    ),
    "kernel/gui/desktop.c": (
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
    "kernel/network/socket.c": (
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
    "kernel/network/tcp.c": (
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
    "drivers/ata.c": (
        "drivers/ata.h",
        "kernel/core/debug.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
        "kernel/fs/blockdev.h",
    ),
    "drivers/keyboard.c": (
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
    "drivers/mouse.c": (
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
    "drivers/pci.c": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
    ),
    "drivers/pit.c": (
        "drivers/pit.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
    ),
    "drivers/rtc.c": (
        "drivers/rtc.h",
        "drivers/serial.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
    ),
    "drivers/rtl8139.c": (
        "drivers/pci.h",
        "drivers/serial.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/irq.h",
        "kernel/cpu/isr.h",
        "kernel/mm/memory.h",
        "kernel/network/net_if.h",
    ),
    "drivers/speaker.c": (
        "drivers/pit.h",
        "drivers/speaker.h",
        "drivers/timer.h",
        "kernel/core/kernel.h",
        "kernel/core/ports.h",
        "kernel/core/types.h",
        "kernel/cpu/isr.h",
    ),
    "drivers/vga.c": (
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
    "kernel/audio/ac97.c": (
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
    "kernel/core/syscall.c": (
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
    "kernel/lang/shell.c": (
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
    "kernel/usb/ehci.c": (
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
    "kernel/usb/uhci.c": (
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

    def run(self, executable, arguments, timeout):
        self.events.append("run")
        self.calls.append((executable, tuple(arguments), timeout))
        if self.payload is not None:
            logical_output = arguments[arguments.index("-o") + 1]
            destination = self.root / logical_output.lstrip("/")
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
            kernel_compile.APPROVED_KERNEL_SOURCES,
            KERNEL_SOURCES,
        )
        self.assertEqual(len(KERNEL_SOURCES), 116)
        self.assertEqual(len(set(KERNEL_SOURCES)), 116)
        self.assertEqual(kernel_compile.KERNEL_I386_ARGUMENTS, KERNEL_I386_ARGUMENTS)

        command = kernel_compile.build_compile_arguments(
            "/kernel/crypto/ct.c",
            "/build/cupid/ct.o",
            "/native/repository",
        )
        self.assertEqual(
            command,
            (
                "-c",
                "/kernel/crypto/ct.c",
                "-o",
                "/build/cupid/ct.o",
                *KERNEL_I386_ARGUMENTS,
                "--root",
                "/native/repository",
            ),
        )

    def test_wsl_invocation_uses_a_private_staged_seed(self):
        command = kernel_compile.build_wsl_invocation(
            "/mnt/c/repository",
            "/mnt/c/repository/bootstrap/seeds/i386-linux/cupidc.elf",
            ("-c", "/kernel/crypto/ct.c"),
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
        self.assertEqual(command[-2:], ("-c", "/kernel/crypto/ct.c"))

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
                    ("-c", "/kernel/crypto/ct.c"),
                    17,
                )

            self.assertIs(result, completed)
            run.assert_called_once_with(
                [
                    str(seed),
                    "-c",
                    "/kernel/crypto/ct.c",
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
            for source in KERNEL_SOURCES
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
            "kernel/crypto/ecdsa.o: kernel/crypto/ecdsa.c "
            "kernel/crypto/ecdsa.h kernel/crypto/p256.h "
            "kernel/crypto/hmac.h kernel/crypto/sha256.h "
            "kernel/core/string.h kernel/core/types.h "
            "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
            makefile,
        )
        self.assertIn(
            "kernel/smp/mp_tables.o: kernel/smp/mp_tables.c "
            "kernel/smp/mp_tables.h kernel/smp/ioapic.h "
            "kernel/smp/percpu.h kernel/core/process.h "
            "kernel/core/types.h drivers/serial.h "
            "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
            makefile,
        )
        self.assertIn(
            "kernel/smp/acpi.o: kernel/smp/acpi.c kernel/smp/acpi.h "
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
            output = source.removesuffix(".c") + ".o"
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
            output = source.removesuffix(".c") + ".o"
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

        for source in NEW_PRODUCTION_SOURCES:
            output = source.removesuffix(".c") + ".o"
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
            recursive_includes("kernel/usb/usb.c"),
        )

        for source in KERNEL_SOURCES:
            output = str(Path(source).with_suffix(".o")).replace("\\", "/")
            host_rule = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf"\n\t\$\(CC\) ",
                re.MULTILINE,
            )
            self.assertNotRegex(makefile, host_rule)

    def test_new_production_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            source.removesuffix(".c") + ".o"
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
        self.assertIn("--source kernel/smp/acpi.c", commands[0])
        self.assertIn("--source kernel/smp/mp_tables.c", commands[1])
        self.assertNotIn(
            "__host_c_compiler_must_not_run__",
            result.stdout + result.stderr,
        )

    def test_operand_free_targets_do_not_expand_to_the_host_compiler(self):
        targets = [
            source.removesuffix(".c") + ".o"
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
            source.removesuffix(".c") + ".o"
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
        source = root / "kernel" / "crypto" / "ct.c"
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
        self.assertNotEqual(executor.calls[0][0], seed)
        self.assertEqual(executor.calls[0][0].name, "cupidc.elf")
        arguments = executor.calls[0][1]
        self.assertEqual(arguments[0:2], ("-c", "/kernel/crypto/ct.c"))
        self.assertEqual(
            arguments[arguments.index("--root") + 1],
            "/native/repository",
        )

    def test_unapproved_source_is_rejected_without_execution(self):
        temporary, root, _source, seed, manifest, output = self._root_fixture()
        self.addCleanup(temporary.cleanup)
        executor = FakeExecutor(root)

        for relative in (
            "drivers/serial.c",
            "kernel/audio/nuked_opl3.c",
            "kernel/core/string.c",
            "kernel/crypto/new_cipher.c",
            "kernel/gfx/png.c",
            "kernel/gui/ed.c",
            "kernel/lang/as.c",
            "kernel/network/udp.c",
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
                "/kernel/crypto/ct.c:9: error CTD000006: unsupported",
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
                "CupidC failed for kernel/crypto/ct.c with status 1.*CTD000006",
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
                        "kernel/crypto/ct.c",
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
                        "kernel/crypto/hmac.c",
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
                output_name = source.removesuffix(".c").replace("/", "-") + ".o"
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

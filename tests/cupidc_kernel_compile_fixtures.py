## @file
#  @brief Share fixture objects and expected profiles across kernel compiler tests.
import struct
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-linux"
    / "manifest.json"
)


def _run_captured_test_tool(
    _manifest,
    _working_directory,
    tool_name,
    arguments,
    *,
    timeout,
    frozen_seed,
    runner,
):
    if runner is None:
        raise AssertionError("test fixture requires an injected runner")
    return runner.run(frozen_seed.tools[tool_name], arguments, timeout)

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
    "kernel/smp/percpu.cc",
    "kernel/smp/smp.cc",
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
    "toolchain/ctool.cc",
    "toolchain/cupidasm.cc",
    "toolchain/cupiddis.cc",
    "toolchain/cupidld.cc",
    "toolchain/elf32.cc",
    "toolchain/x86.cc",
)
SOURCE_DRIVEN_SOURCES = (
    "drivers/serial.cc",
    "drivers/timer.cc",
    "kernel/audio/nuked_opl3.cc",
    "kernel/core/app_launch.cc",
    "kernel/core/kernel.cc",
    "kernel/core/panic.cc",
    "kernel/core/process.cc",
    "kernel/core/string.cc",
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
GENERATED_KERNEL_SOURCES = (
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
        "kernel/gfx/gfx2d_assets.h",
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
    "kernel/core/string.cc": (
        "kernel/core/string.h",
        "kernel/core/types.h",
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
        "toolchain/ctool.h",
        "toolchain/cupiddis.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h",
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
        "kernel/gfx/gfx2d_icons.h",
        "kernel/gui/ansi.h",
        "kernel/gui/desktop.h",
        "kernel/gui/gui.h",
        "kernel/gui/gui_themes.h",
        "kernel/gui/terminal_app.h",
        "kernel/lang/as.h",
        "kernel/lang/as_elf.h",
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
        "toolchain/ctool.h",
        "toolchain/cupidasm.h",
        "toolchain/cupiddis.h",
        "toolchain/cupidld.h",
        "toolchain/elf32.h",
        "toolchain/pe32.h",
        "toolchain/x86.h",
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
        self.result = result or subprocess.CompletedProcess([], 0, "", "")
        self.payload = payload
        self.events = events if events is not None else []
        self.calls = []

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

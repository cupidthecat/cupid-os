#!/usr/bin/env python3
"""Compile an approved production source with the checked CupidC seed."""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
import stat
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

try:
    from tools.bootstrap_toolchain import (
        BootstrapError,
        ToolRunner,
        freeze_seed_inputs,
        run_seed_tool,
        verify_seed_inputs,
    )
except ModuleNotFoundError:
    from bootstrap_toolchain import (
        BootstrapError,
        ToolRunner,
        freeze_seed_inputs,
        run_seed_tool,
        verify_seed_inputs,
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
    "toolchain/cupidld.cc",
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
APPROVED_GENERATED_KERNEL_SOURCES = (
    "kernel/cpu/ksyms_data.cc",
)
APPROVED_DOOM_COMPAT_SOURCES = (
    "kernel/doom/dglibc.cc",
    "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/doomgeneric_cupidos.cc",
)
APPROVED_DOOM_TREE_SOURCES = (
    "kernel/doom/i_sound_cupidos.cc",
    "kernel/doom/src/am_map.cc",
    "kernel/doom/src/d_event.cc",
    "kernel/doom/src/d_items.cc",
    "kernel/doom/src/d_iwad.cc",
    "kernel/doom/src/d_loop.cc",
    "kernel/doom/src/d_main.cc",
    "kernel/doom/src/d_mode.cc",
    "kernel/doom/src/d_net.cc",
    "kernel/doom/src/doomdef.cc",
    "kernel/doom/src/doomgeneric.cc",
    "kernel/doom/src/doomstat.cc",
    "kernel/doom/src/dstrings.cc",
    "kernel/doom/src/dummy.cc",
    "kernel/doom/src/f_finale.cc",
    "kernel/doom/src/f_wipe.cc",
    "kernel/doom/src/g_game.cc",
    "kernel/doom/src/gusconf.cc",
    "kernel/doom/src/hu_lib.cc",
    "kernel/doom/src/hu_stuff.cc",
    "kernel/doom/src/i_endoom.cc",
    "kernel/doom/src/i_input.cc",
    "kernel/doom/src/i_joystick.cc",
    "kernel/doom/src/i_scale.cc",
    "kernel/doom/src/i_system.cc",
    "kernel/doom/src/i_timer.cc",
    "kernel/doom/src/i_video.cc",
    "kernel/doom/src/icon.cc",
    "kernel/doom/src/info.cc",
    "kernel/doom/src/m_argv.cc",
    "kernel/doom/src/m_bbox.cc",
    "kernel/doom/src/m_cheat.cc",
    "kernel/doom/src/m_config.cc",
    "kernel/doom/src/m_controls.cc",
    "kernel/doom/src/m_fixed.cc",
    "kernel/doom/src/m_menu.cc",
    "kernel/doom/src/m_misc.cc",
    "kernel/doom/src/m_random.cc",
    "kernel/doom/src/p_ceilng.cc",
    "kernel/doom/src/p_doors.cc",
    "kernel/doom/src/p_enemy.cc",
    "kernel/doom/src/p_floor.cc",
    "kernel/doom/src/p_inter.cc",
    "kernel/doom/src/p_lights.cc",
    "kernel/doom/src/p_map.cc",
    "kernel/doom/src/p_maputl.cc",
    "kernel/doom/src/p_mobj.cc",
    "kernel/doom/src/p_plats.cc",
    "kernel/doom/src/p_pspr.cc",
    "kernel/doom/src/p_saveg.cc",
    "kernel/doom/src/p_setup.cc",
    "kernel/doom/src/p_sight.cc",
    "kernel/doom/src/p_spec.cc",
    "kernel/doom/src/p_switch.cc",
    "kernel/doom/src/p_telept.cc",
    "kernel/doom/src/p_tick.cc",
    "kernel/doom/src/p_user.cc",
    "kernel/doom/src/r_bsp.cc",
    "kernel/doom/src/r_data.cc",
    "kernel/doom/src/r_draw.cc",
    "kernel/doom/src/r_main.cc",
    "kernel/doom/src/r_plane.cc",
    "kernel/doom/src/r_segs.cc",
    "kernel/doom/src/r_sky.cc",
    "kernel/doom/src/r_things.cc",
    "kernel/doom/src/s_sound.cc",
    "kernel/doom/src/sha1.cc",
    "kernel/doom/src/sounds.cc",
    "kernel/doom/src/st_lib.cc",
    "kernel/doom/src/st_stuff.cc",
    "kernel/doom/src/statdump.cc",
    "kernel/doom/src/tables.cc",
    "kernel/doom/src/v_video.cc",
    "kernel/doom/src/w_checksum.cc",
    "kernel/doom/src/w_file.cc",
    "kernel/doom/src/w_file_stdc.cc",
    "kernel/doom/src/w_main.cc",
    "kernel/doom/src/w_wad.cc",
    "kernel/doom/src/wi_stuff.cc",
    "kernel/doom/src/z_zone.cc",
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
DOOM_COMPAT_I386_ARGUMENTS = (
    "--gnu",
    "--doom-compat",
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
    "-I",
    "/kernel/doom/src",
    "-I",
    "/kernel/doom/src/include_stubs",
)
DOOM_TREE_I386_ARGUMENTS = (
    *DOOM_COMPAT_I386_ARGUMENTS,
    "-D",
    'DEFAULT_SAVEGAMEDIR="/home/doom/"',
    "-D",
    "DOOM_PORT_CUPIDOS=1",
    "-include",
    "/kernel/doom/dglibc_compat.h",
)

COMPILER_PROFILE_SOURCES = {
    "kernel": APPROVED_KERNEL_COMPILE_SOURCES,
    "doom-compat": APPROVED_DOOM_COMPAT_SOURCES,
    "doom-tree": APPROVED_DOOM_TREE_SOURCES,
}
COMPILER_PROFILE_ARGUMENTS = {
    "kernel": KERNEL_I386_ARGUMENTS,
    "doom-compat": DOOM_COMPAT_I386_ARGUMENTS,
    "doom-tree": DOOM_TREE_I386_ARGUMENTS,
}

DEFAULT_TIMEOUT_SECONDS = 180
GENERATED_KERNEL_TIMEOUT_SECONDS = 600
PUBLISH_RETRY_DELAYS_SECONDS = (0.05, 0.1, 0.2, 0.4, 0.8)
PROFILE_MANIFEST_SCHEMA = "cupid.doom-profile-inputs.v1"
PROFILE_SNAPSHOT_MAGIC = b"CUPROF1\0"


@dataclass(frozen=True)
class _ProfileInputCapture:
    source_membership: tuple[Path, ...]
    profiles: tuple[
        tuple[str, tuple[Path, ...], tuple[Path, ...]],
        ...,
    ]
    inputs: tuple[tuple[Path, bytes], ...]


@dataclass(frozen=True)
class _ProfileFileCapture:
    path: Path
    resolved: Path
    device: int
    inode: int
    links: int
    size: int
    modified_ns: int
    payload: bytes


@dataclass(frozen=True)
class _ProfileDirectoryCapture:
    path: Path
    resolved: Path
    device: int
    inode: int


class KernelCompileError(RuntimeError):
    """A checked CupidC production operation could not publish its output."""


def _retryable_publish_error(error: OSError) -> bool:
    return (
        isinstance(error, PermissionError)
        or error.errno in (errno.EACCES, errno.EPERM)
        or getattr(error, "winerror", None) in (5, 32)
    )


def _replace_with_retry(source: Path, destination: Path) -> None:
    """Atomically publish a file despite a brief Windows sharing lock."""
    publish_error = None
    for attempt in range(len(PUBLISH_RETRY_DELAYS_SECONDS) + 1):
        try:
            os.replace(source, destination)
            return
        except OSError as error:
            publish_error = error
            if (
                not _retryable_publish_error(error)
                or attempt == len(PUBLISH_RETRY_DELAYS_SECONDS)
            ):
                break
            time.sleep(PUBLISH_RETRY_DELAYS_SECONDS[attempt])
    raise publish_error


def build_compile_arguments(
    logical_source: str,
    logical_output: str,
    compiler_root: str | Path,
    *,
    profile: str = "kernel",
) -> tuple[str | Path, ...]:
    """Build one complete, fixed production CupidC argument vector."""
    try:
        profile_arguments = COMPILER_PROFILE_ARGUMENTS[profile]
    except KeyError as error:
        raise KernelCompileError(
            f"unknown CupidC production profile: {profile}"
        ) from error
    return (
        "-c",
        logical_source,
        "-o",
        logical_output,
        *profile_arguments,
        "--root",
        compiler_root,
    )


def _read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise KernelCompileError(
            f"cannot read emitted object {path}: {error}"
        ) from error


def validate_i386_relocatable_bytes(
    image: bytes,
    *,
    require_executable: bool = False,
) -> None:
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
    executable_bytes = 0
    for index in range(section_count):
        section = struct.unpack_from(
            "<IIIIIIIIII",
            image,
            section_offset + index * section_entry_size,
        )
        (
            _name,
            section_type,
            section_flags,
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
        if section_type == 1 and section_flags & 0x4:
            executable_bytes += payload_size
        sections.append(section)

    if require_executable and executable_bytes == 0:
        raise KernelCompileError(
            "emitted object has no executable section bytes"
        )

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
            # An absolute relocation may select a static subobject through
            # its signed addend. Direct PC-relative calls still use -4.
            if relocation_type == 2 and addend != -4:
                raise KernelCompileError(
                    f"PC-relative relocation addend is {addend}, expected -4"
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


def _source_path(
    root: Path,
    source: Path,
    profile: str,
) -> tuple[Path, str]:
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
    try:
        approved_sources = COMPILER_PROFILE_SOURCES[profile]
    except KeyError as error:
        raise KernelCompileError(
            f"unknown CupidC production profile: {profile}"
        ) from error
    if relative_name not in approved_sources:
        cohort = (
            "CupidC kernel"
            if profile == "kernel"
            else f"{profile} CupidC"
        )
        raise KernelCompileError(
            f"source is outside the approved {cohort} cohort: "
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


def _is_link_like(path: Path) -> bool:
    if path.is_symlink():
        return True
    is_junction = getattr(path, "is_junction", None)
    return bool(is_junction is not None and is_junction())


def _profile_header_paths(
    root: Path,
    profile: str,
) -> tuple[Path, ...]:
    try:
        arguments = COMPILER_PROFILE_ARGUMENTS[profile]
    except KeyError as error:
        raise KernelCompileError(
            f"unknown CupidC production profile: {profile}"
        ) from error
    if profile == "kernel":
        raise KernelCompileError(
            "the kernel profile uses source-specific frozen closures"
        )

    paths = set()
    for index, argument in enumerate(arguments):
        if argument != "-I":
            continue
        include_root = root / arguments[index + 1].lstrip("/")
        if _is_link_like(include_root):
            raise KernelCompileError(
                "CupidC profile include root may not be a link or junction: "
                f"{include_root.relative_to(root).as_posix()}"
            )
        try:
            resolved_root = include_root.resolve(strict=True)
            resolved_root.relative_to(root)
        except (OSError, ValueError) as error:
            raise KernelCompileError(
                "CupidC profile include root is unavailable: "
                f"{arguments[index + 1]}"
            ) from error
        if not resolved_root.is_dir():
            raise KernelCompileError(
                "CupidC profile include root is not a directory: "
                f"{arguments[index + 1]}"
            )
        for path in resolved_root.rglob("*"):
            relative_parts = path.relative_to(resolved_root).parts
            if any(part.startswith(".") for part in relative_parts):
                continue
            if _is_link_like(path):
                relative_name = path.relative_to(root).as_posix()
                raise KernelCompileError(
                    "CupidC profile input may not be a link or junction: "
                    f"{relative_name}"
                )
            if path.is_file() and path.suffix in {".h", ".inc"}:
                paths.add(path)
    return tuple(sorted(paths))


def _profile_source_paths(
    root: Path,
    profile: str,
) -> tuple[Path, ...]:
    try:
        approved_sources = COMPILER_PROFILE_SOURCES[profile]
    except KeyError as error:
        raise KernelCompileError(
            f"unknown CupidC production profile: {profile}"
        ) from error

    paths = []
    for relative_name in approved_sources:
        path = root / relative_name
        current = root
        for part in Path(relative_name).parts:
            current /= part
            if _is_link_like(current):
                raise KernelCompileError(
                    "CupidC profile source may not be a link or junction: "
                    f"{relative_name}"
                )
        try:
            resolved = path.resolve(strict=True)
            resolved.relative_to(root)
        except (OSError, ValueError) as error:
            raise KernelCompileError(
                f"CupidC profile source is unavailable: {relative_name}"
            ) from error
        if not resolved.is_file():
            raise KernelCompileError(
                f"CupidC profile source is not a file: {relative_name}"
            )
        paths.append(resolved)
    return tuple(paths)


def _doom_source_membership(root: Path) -> tuple[Path, ...]:
    approved = (
        *_profile_source_paths(root, "doom-compat"),
        *_profile_source_paths(root, "doom-tree"),
    )
    approved_names = {
        path.relative_to(root).as_posix() for path in approved
    }
    discovered = set()
    doom_root = root / "kernel" / "doom"
    if _is_link_like(doom_root):
        raise KernelCompileError(
            "CupidC profile source directory may not be a link or junction: "
            "kernel/doom"
        )
    for path in doom_root.rglob("*"):
        relative_parts = path.relative_to(doom_root).parts
        if any(part.startswith(".") for part in relative_parts[:-1]):
            continue
        relative_name = path.relative_to(root).as_posix()
        if _is_link_like(path):
            raise KernelCompileError(
                "CupidC profile source tree may not contain a link or "
                f"junction: {relative_name}"
            )
        if path.suffix not in {".c", ".cc"}:
            continue
        if not path.is_file():
            raise KernelCompileError(
                f"CupidC profile source is not a file: {relative_name}"
            )
        discovered.add(relative_name)
    if discovered != approved_names:
        details = []
        missing = sorted(approved_names - discovered)
        unlisted = sorted(discovered - approved_names)
        if missing:
            details.append("missing " + ", ".join(missing))
        if unlisted:
            details.append("unlisted " + ", ".join(unlisted))
        raise KernelCompileError(
            "Doom profile source membership differs from the approved "
            f"cohort: {'; '.join(details)}"
        )
    return tuple(sorted(approved))


def _kernel_input_paths(
    root: Path,
    source_name: str,
    profile: str,
) -> tuple[Path, ...]:
    if profile != "kernel":
        paths = {
            root / source_name,
            *_profile_header_paths(root, profile),
        }
        ordered = []
        for path in sorted(paths):
            relative_name = path.relative_to(root).as_posix()
            if path.is_symlink():
                raise KernelCompileError(
                    f"CupidC profile input may not be a symlink: {relative_name}"
                )
            try:
                resolved = path.resolve(strict=True)
                resolved.relative_to(root)
            except (OSError, ValueError) as error:
                raise KernelCompileError(
                    f"CupidC profile input is unavailable: {relative_name}"
                ) from error
            if not resolved.is_file():
                raise KernelCompileError(
                    f"CupidC profile input is not a file: {relative_name}"
                )
            ordered.append(resolved)
        return tuple(ordered)

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


def _capture_profile_inputs(root: Path) -> _ProfileInputCapture:
    source_membership = _doom_source_membership(root)
    profiles = tuple(
        (
            profile,
            _profile_header_paths(root, profile),
            _profile_source_paths(root, profile),
        )
        for profile in ("doom-compat", "doom-tree")
    )
    input_paths = tuple(
        sorted(
            {
                path
                for _profile, headers, _sources in profiles
                for path in headers
            }
        )
    )
    captured = _capture_kernel_inputs(input_paths)
    capture = _ProfileInputCapture(
        source_membership=source_membership,
        profiles=profiles,
        inputs=tuple((path, captured[path]) for path in input_paths),
    )
    _require_profile_inputs_unchanged(root, capture)
    return capture


def _require_profile_inputs_unchanged(
    root: Path,
    capture: _ProfileInputCapture,
) -> None:
    repeated_profiles = tuple(
        (
            profile,
            _profile_header_paths(root, profile),
            _profile_source_paths(root, profile),
        )
        for profile, _headers, _sources in capture.profiles
    )
    if (
        _doom_source_membership(root) != capture.source_membership
        or repeated_profiles != capture.profiles
    ):
        raise KernelCompileError(
            "Doom profile input membership changed while writing its manifest"
        )
    paths = tuple(path for path, _payload in capture.inputs)
    if _capture_kernel_inputs(paths) != dict(capture.inputs):
        raise KernelCompileError(
            "Doom profile input bytes changed while writing its manifest"
        )


def _profile_input_document(
    root: Path,
    capture: _ProfileInputCapture,
) -> dict[str, object]:
    relative_inputs = {
        path: path.relative_to(root).as_posix()
        for path, _payload in capture.inputs
    }
    return {
        "schema": PROFILE_MANIFEST_SCHEMA,
        "profiles": {
            profile: [
                path.relative_to(root).as_posix() for path in headers
            ]
            for profile, headers, _sources in capture.profiles
        },
        "sources": {
            profile: [
                path.relative_to(root).as_posix() for path in sources
            ]
            for profile, _headers, sources in capture.profiles
        },
        "inputs": [
            {
                "path": relative_inputs[path],
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            for path, payload in capture.inputs
        ],
    }


def _profile_snapshot_bytes(
    root: Path,
    capture: _ProfileInputCapture,
) -> bytes:
    snapshot = bytearray(PROFILE_SNAPSHOT_MAGIC)

    def append_u32(value: int, label: str) -> None:
        if value < 0 or value > 0xFFFFFFFF:
            raise KernelCompileError(
                f"CupidObj profile snapshot {label} exceeds u32"
            )
        snapshot.extend(struct.pack("<I", value))

    def append_bytes(value: bytes, label: str) -> None:
        append_u32(len(value), f"{label} length")
        snapshot.extend(value)

    def append_text(value: str, label: str) -> None:
        try:
            encoded = value.encode("ascii")
        except UnicodeEncodeError as error:
            raise KernelCompileError(
                f"CupidObj profile snapshot {label} is not portable ASCII"
            ) from error
        append_bytes(encoded, label)

    append_text(PROFILE_MANIFEST_SCHEMA, "schema")
    append_u32(len(capture.profiles), "profile count")
    for profile, headers, sources in capture.profiles:
        append_text(profile, "profile name")
        append_u32(len(headers), "header count")
        for path in headers:
            append_text(
                path.relative_to(root).as_posix(),
                "header path",
            )
        append_u32(len(sources), "source count")
        for path in sources:
            append_text(
                path.relative_to(root).as_posix(),
                "source path",
            )
    append_u32(len(capture.inputs), "input count")
    for path, payload in capture.inputs:
        append_text(path.relative_to(root).as_posix(), "input path")
        append_bytes(payload, "input bytes")
    return bytes(snapshot)


def _profile_input_manifest(root: Path) -> dict[str, object]:
    """Return the Python oracle for one stable Doom profile capture."""
    capture = _capture_profile_inputs(root)
    return _profile_input_document(root, capture)


def _profile_manifest_output_path(root: Path, output: Path) -> Path:
    candidate = output if output.is_absolute() else root / output
    requested = Path(os.path.abspath(os.fspath(candidate)))
    try:
        relative = requested.relative_to(root)
    except ValueError as error:
        raise KernelCompileError(
            f"profile input manifest must stay inside repository root: {output}"
        ) from error
    if not relative.parts or requested == root:
        raise KernelCompileError(
            "profile input manifest must name a file inside the repository"
        )
    if requested.suffix != ".json":
        raise KernelCompileError(
            "profile input manifest must use the .json suffix"
        )

    current = root
    for part in relative.parts[:-1]:
        current /= part
        if _is_link_like(current):
            raise KernelCompileError(
                "profile input manifest directory may not be a link or "
                f"junction: {current}"
            )
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError:
            break
        except OSError as error:
            raise KernelCompileError(
                f"profile input manifest directory cannot be inspected: "
                f"{current}: {error}"
            ) from error
        if not stat.S_ISDIR(mode):
            raise KernelCompileError(
                f"profile input manifest directory is not a directory: "
                f"{current}"
            )
    try:
        requested.parent.mkdir(parents=True, exist_ok=True)
        current = root
        for part in relative.parts[:-1]:
            current /= part
            if _is_link_like(current):
                raise KernelCompileError(
                    "profile input manifest directory may not be a link or "
                    f"junction: {current}"
                )
            if not stat.S_ISDIR(current.lstat().st_mode):
                raise KernelCompileError(
                    "profile input manifest directory is not a directory: "
                    f"{current}"
                )
        parent = requested.parent.resolve(strict=True)
        parent.relative_to(root)
    except KernelCompileError:
        raise
    except (OSError, ValueError) as error:
        raise KernelCompileError(
            f"could not prepare profile input manifest directory "
            f"{requested.parent}: {error}"
        ) from error
    resolved = parent / requested.name
    if _is_link_like(resolved):
        raise KernelCompileError(
            "profile input manifest may not be a link or junction"
        )
    try:
        mode = resolved.lstat().st_mode
    except FileNotFoundError:
        return resolved
    except OSError as error:
        raise KernelCompileError(
            f"profile input manifest cannot be inspected: {resolved}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise KernelCompileError(
            f"profile input manifest is not a regular file: {resolved}"
        )
    return resolved


def _profile_seed_manifest_path(manifest: Path) -> Path:
    requested = Path(os.path.abspath(os.fspath(manifest)))
    if _is_link_like(requested):
        raise KernelCompileError(
            "checked seed manifest may not be a link or junction"
        )
    try:
        resolved = requested.resolve(strict=True)
        mode = resolved.lstat().st_mode
    except OSError as error:
        raise KernelCompileError(
            f"checked seed manifest cannot be resolved: {requested}: {error}"
        ) from error
    if not stat.S_ISREG(mode):
        raise KernelCompileError(
            f"checked seed manifest is not a regular file: {resolved}"
        )
    return resolved


def _profile_paths_alias(first: Path, second: Path) -> bool:
    if os.path.normcase(str(first)) == os.path.normcase(str(second)):
        return True
    if not first.exists() or not second.exists():
        return False
    try:
        return os.path.samefile(first, second)
    except FileNotFoundError:
        return False
    except OSError as error:
        raise KernelCompileError(
            f"profile manifest path identity cannot be checked: "
            f"{first}, {second}: {error}"
        ) from error


def _capture_profile_file(
    path: Path,
    label: str,
    *,
    allow_missing: bool = False,
) -> _ProfileFileCapture | None:
    if _is_link_like(path):
        raise KernelCompileError(
            f"{label} may not be a link or junction: {path}"
        )
    try:
        status = path.lstat()
    except FileNotFoundError:
        if allow_missing:
            return None
        raise KernelCompileError(f"{label} is missing: {path}") from None
    except OSError as error:
        raise KernelCompileError(
            f"{label} cannot be inspected: {path}: {error}"
        ) from error
    if not stat.S_ISREG(status.st_mode):
        raise KernelCompileError(
            f"{label} is not a regular file: {path}"
        )
    if status.st_nlink != 1:
        raise KernelCompileError(
            f"{label} may not be a hard link: {path}"
        )
    try:
        with path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            payload = stream.read()
            after = os.fstat(stream.fileno())
        resolved = path.resolve(strict=True)
        final_status = path.lstat()
    except OSError as error:
        raise KernelCompileError(
            f"{label} cannot be read: {path}: {error}"
        ) from error
    stable_fields = (
        "st_dev",
        "st_ino",
        "st_nlink",
        "st_size",
        "st_mtime_ns",
    )
    if (
        status.st_dev != before.st_dev
        or status.st_ino != before.st_ino
        or any(
            getattr(before, field) != getattr(after, field)
            for field in stable_fields
        )
        or any(
            getattr(after, field) != getattr(final_status, field)
            for field in stable_fields
        )
        or not stat.S_ISREG(final_status.st_mode)
        or len(payload) != after.st_size
        or after.st_nlink != 1
        or _is_link_like(path)
    ):
        raise KernelCompileError(f"{label} changed while it was being read")
    return _ProfileFileCapture(
        path=path,
        resolved=resolved,
        device=after.st_dev,
        inode=after.st_ino,
        links=after.st_nlink,
        size=after.st_size,
        modified_ns=after.st_mtime_ns,
        payload=payload,
    )


def _capture_profile_directory(
    path: Path,
    label: str,
) -> _ProfileDirectoryCapture:
    if _is_link_like(path):
        raise KernelCompileError(
            f"{label} may not be a link or junction: {path}"
        )
    try:
        before = path.lstat()
        resolved = path.resolve(strict=True)
        after = path.lstat()
    except OSError as error:
        raise KernelCompileError(
            f"{label} cannot be inspected: {path}: {error}"
        ) from error
    if not stat.S_ISDIR(before.st_mode) or not stat.S_ISDIR(after.st_mode):
        raise KernelCompileError(f"{label} is not a directory: {path}")
    if (
        before.st_dev != after.st_dev
        or before.st_ino != after.st_ino
        or _is_link_like(path)
    ):
        raise KernelCompileError(f"{label} changed while it was inspected")
    return _ProfileDirectoryCapture(
        path=path,
        resolved=resolved,
        device=after.st_dev,
        inode=after.st_ino,
    )


def _require_profile_directory_unchanged(
    initial: _ProfileDirectoryCapture,
) -> None:
    current = _capture_profile_directory(initial.path, "profile output directory")
    if (
        current.resolved != initial.resolved
        or current.device != initial.device
        or current.inode != initial.inode
    ):
        raise KernelCompileError(
            "profile input manifest directory changed while authoring the "
            "profile manifest"
        )


def _require_profile_output_unchanged(
    output: Path,
    initial: _ProfileFileCapture | None,
) -> None:
    current = _capture_profile_file(
        output,
        "profile input manifest output",
        allow_missing=True,
    )
    if initial is None:
        if current is not None:
            raise KernelCompileError(
                "profile input manifest output appeared while authoring the "
                "profile manifest"
            )
        return
    if current is None:
        raise KernelCompileError(
            "profile input manifest output disappeared while authoring the "
            "profile manifest"
        )
    if (
        current.resolved != initial.resolved
        or current.device != initial.device
        or current.inode != initial.inode
        or current.links != initial.links
        or current.size != initial.size
        or current.modified_ns != initial.modified_ns
        or current.payload != initial.payload
    ):
        raise KernelCompileError(
            "profile input manifest output changed while authoring the "
            "profile manifest"
        )


def _require_profile_file_unchanged(
    initial: _ProfileFileCapture,
    label: str,
) -> None:
    current = _capture_profile_file(initial.path, label)
    if current != initial:
        raise KernelCompileError(f"{label} changed after validation")


def _write_private_profile_file(path: Path, payload: bytes) -> None:
    with path.open("xb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def _acquire_profile_manifest_lock(output: Path) -> tuple[object, str]:
    lock_name = hashlib.sha256(
        str(output).casefold().encode("utf-8")
    ).hexdigest()
    lock_path = output.parent / f".cupid-profile-{lock_name}.lock"
    descriptor = None
    stream = None
    try:
        if _is_link_like(lock_path):
            raise OSError(errno.ELOOP, "publication lock is link-like")
        flags = os.O_RDWR | os.O_CREAT
        flags |= getattr(os, "O_BINARY", 0)
        flags |= getattr(os, "O_CLOEXEC", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(lock_path, flags, 0o600)
        stream = os.fdopen(descriptor, "r+b", buffering=0)
        descriptor = None
        path_status = lock_path.lstat()
        file_status = os.fstat(stream.fileno())
        if (
            not stat.S_ISREG(path_status.st_mode)
            or not stat.S_ISREG(file_status.st_mode)
            or path_status.st_dev != file_status.st_dev
            or path_status.st_ino != file_status.st_ino
            or file_status.st_nlink != 1
            or _is_link_like(lock_path)
        ):
            raise OSError(
                errno.EPERM,
                "publication lock is not one private regular file",
            )
        effective_user = getattr(os, "geteuid", None)
        if (
            effective_user is not None
            and file_status.st_uid != effective_user()
        ):
            raise OSError(
                errno.EACCES,
                "publication lock belongs to another user",
            )
        change_mode = getattr(os, "fchmod", None)
        if change_mode is not None:
            change_mode(stream.fileno(), 0o600)
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"\0")
            stream.flush()
            os.fsync(stream.fileno())
        stream.seek(0)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            platform = "windows"
        else:
            import fcntl

            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            platform = "posix"
    except (ImportError, OSError) as error:
        if descriptor is not None:
            try:
                os.close(descriptor)
            except OSError:
                pass
        if stream is not None:
            try:
                stream.close()
            except OSError:
                pass
        raise KernelCompileError(
            f"another profile manifest publisher is active for {output}, "
            f"or its publication lock is unavailable: {error}"
        ) from error
    return stream, platform


def _release_profile_manifest_lock(lock: tuple[object, str]) -> None:
    stream, platform = lock
    try:
        stream.seek(0)
        if platform == "windows":
            import msvcrt

            msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
    except (ImportError, OSError):
        pass
    finally:
        try:
            stream.close()
        except OSError:
            pass


def _replace_profile_candidate_with_retry(
    candidate: _ProfileFileCapture,
    output: Path,
    initial_output: _ProfileFileCapture | None,
    output_directory: _ProfileDirectoryCapture,
) -> None:
    publish_error = None
    for attempt in range(len(PUBLISH_RETRY_DELAYS_SECONDS) + 1):
        _require_profile_directory_unchanged(output_directory)
        _require_profile_file_unchanged(
            candidate,
            "checked CupidObj profile manifest output",
        )
        _require_profile_output_unchanged(output, initial_output)
        try:
            os.replace(candidate.path, output)
        except OSError as error:
            publish_error = error
            if (
                not _retryable_publish_error(error)
                or attempt == len(PUBLISH_RETRY_DELAYS_SECONDS)
            ):
                break
            time.sleep(PUBLISH_RETRY_DELAYS_SECONDS[attempt])
            continue
        _require_profile_directory_unchanged(output_directory)
        published = _capture_profile_file(
            output,
            "published profile input manifest",
        )
        if published is None or published.payload != candidate.payload:
            raise KernelCompileError(
                "published profile input manifest differs from its checked "
                "candidate"
            )
        return
    raise publish_error


def _profile_capture_paths(
    capture: _ProfileInputCapture,
) -> tuple[Path, ...]:
    paths = set(capture.source_membership)
    paths.update(path for path, _payload in capture.inputs)
    for _profile, headers, sources in capture.profiles:
        paths.update(headers)
        paths.update(sources)
    return tuple(sorted(paths))


def write_profile_input_manifest(
    root: Path,
    output: Path,
    *,
    manifest: Path | None = None,
) -> bool:
    """Publish one stable Doom profile manifest when its bytes change."""
    root = _root_path(root)
    resolved = _profile_manifest_output_path(root, output)
    output_directory = _capture_profile_directory(
        resolved.parent,
        "profile output directory",
    )
    publication_lock = _acquire_profile_manifest_lock(resolved)
    try:
        _require_profile_directory_unchanged(output_directory)
        initial_output = _capture_profile_file(
            resolved,
            "profile input manifest output",
            allow_missing=True,
        )
        manifest_path = (
            _profile_seed_manifest_path(manifest)
            if manifest is not None
            else None
        )
        checked_seed = None
        if manifest_path is not None:
            if _profile_paths_alias(resolved, manifest_path):
                raise KernelCompileError(
                    "profile input manifest output may not replace the "
                    "checked seed manifest"
                )
            try:
                checked_seed = verify_seed_inputs(manifest_path)
            except BootstrapError as error:
                raise KernelCompileError(
                    f"checked seed verification failed: {error}"
                ) from error
            for name, path in checked_seed.tools.items():
                if _profile_paths_alias(resolved, path):
                    raise KernelCompileError(
                        "profile input manifest output may not replace "
                        f"checked seed tool {name}"
                    )

        capture = _capture_profile_inputs(root)
        for path in _profile_capture_paths(capture):
            if _profile_paths_alias(resolved, path):
                raise KernelCompileError(
                    "profile input manifest output may not replace a Doom "
                    f"profile input: {path.relative_to(root).as_posix()}"
                )
        oracle_payload = (
            json.dumps(
                _profile_input_document(root, capture),
                indent=2,
                sort_keys=True,
            )
            + "\n"
        ).encode("utf-8")

        with tempfile.TemporaryDirectory(
            prefix=f".{resolved.name}.profile-",
            dir=resolved.parent,
        ) as temporary:
            private = Path(temporary).resolve()
            candidate = private / "profile-manifest.json"
            if manifest_path is None:
                _write_private_profile_file(candidate, oracle_payload)
                candidate_capture = _capture_profile_file(
                    candidate,
                    "Python profile manifest candidate",
                )
                if candidate_capture is None:
                    raise KernelCompileError(
                        "Python profile manifest candidate is missing"
                    )
            else:
                try:
                    frozen_seed = freeze_seed_inputs(
                        manifest_path,
                        private / "checked-seed",
                    )
                except BootstrapError as error:
                    raise KernelCompileError(
                        f"checked seed could not be frozen: {error}"
                    ) from error
                if (
                    frozen_seed.manifest_sha256
                    != checked_seed.manifest_sha256
                ):
                    raise KernelCompileError(
                        "checked seed changed before CupidObj execution"
                    )
                snapshot = private / "profile-inputs.cuprof"
                snapshot_payload = _profile_snapshot_bytes(root, capture)
                _write_private_profile_file(snapshot, snapshot_payload)
                snapshot_capture = _capture_profile_file(
                    snapshot,
                    "CupidObj profile snapshot",
                )
                if snapshot_capture is None:
                    raise KernelCompileError(
                        "CupidObj profile snapshot is missing"
                    )
                arguments: tuple[str | Path, ...] = (
                    "profile-manifest",
                    snapshot.resolve(),
                    "-o",
                    candidate.resolve(strict=False),
                )
                try:
                    result = run_seed_tool(
                        manifest_path,
                        private,
                        "cupidobj",
                        arguments,
                        timeout=60,
                        frozen_seed=frozen_seed,
                    )
                except BootstrapError as error:
                    raise KernelCompileError(
                        f"checked CupidObj could not run: {error}"
                    ) from error
                if result.returncode != 0:
                    details = (result.stderr or result.stdout or "").strip()
                    suffix = f": {details}" if details else ""
                    raise KernelCompileError(
                        "checked CupidObj failed with status "
                        f"{result.returncode}{suffix}"
                    )
                _require_profile_directory_unchanged(output_directory)
                _require_profile_file_unchanged(
                    snapshot_capture,
                    "CupidObj profile snapshot",
                )
                checked_output = _capture_profile_file(
                    candidate,
                    "checked CupidObj profile manifest output",
                )
                if checked_output is None:
                    raise KernelCompileError(
                        "checked CupidObj profile manifest output is missing"
                    )
                for path in (
                    snapshot,
                    manifest_path,
                    resolved,
                    *frozen_seed.tools.values(),
                ):
                    if _profile_paths_alias(candidate, path):
                        raise KernelCompileError(
                            "checked CupidObj profile manifest output aliases "
                            f"an input: {path}"
                        )
                candidate_capture = checked_output
                if candidate_capture.payload != oracle_payload:
                    raise KernelCompileError(
                        "checked CupidObj profile manifest differs from the "
                        "Python oracle"
                    )
                try:
                    live_seed = verify_seed_inputs(manifest_path)
                except BootstrapError as error:
                    raise KernelCompileError(
                        "checked seed inputs changed while authoring the "
                        f"profile manifest: {error}"
                    ) from error
                if (
                    live_seed.manifest_sha256
                    != checked_seed.manifest_sha256
                ):
                    raise KernelCompileError(
                        "checked seed manifest changed while authoring the "
                        "profile manifest"
                    )

            _require_profile_inputs_unchanged(root, capture)
            _require_profile_directory_unchanged(output_directory)
            _require_profile_output_unchanged(resolved, initial_output)
            if (
                initial_output is not None
                and initial_output.payload == candidate_capture.payload
            ):
                return False
            _replace_profile_candidate_with_retry(
                candidate_capture,
                resolved,
                initial_output,
                output_directory,
            )
    except KernelCompileError:
        raise
    except OSError as error:
        raise KernelCompileError(
            f"could not publish profile input manifest {resolved}: {error}"
        ) from error
    finally:
        _release_profile_manifest_lock(publication_lock)
    return True


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


def compile_kernel_source(
    root: Path,
    source: Path,
    output: Path,
    *,
    manifest: Path | None = None,
    executor: ToolRunner | None = None,
    timeout: int | None = None,
    profile: str = "kernel",
) -> None:
    """Compile one approved source and atomically publish a checked object."""
    root = _root_path(root)
    source, logical_source = _source_path(root, source, profile)
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

    input_paths = _kernel_input_paths(root, source_name, profile)
    captured_inputs = _capture_kernel_inputs(input_paths)
    profile_source_membership = (
        _doom_source_membership(root)
        if profile != "kernel"
        else ()
    )
    manifest_path = (
        manifest.resolve()
        if manifest is not None
        else root
        / "bootstrap"
        / "seeds"
        / "i386-linux"
        / "manifest.json"
    )
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
                    compiler_root = temporary_root.resolve()
                else:
                    temporary_output = temporary_root / output.name
                    logical_temporary = (
                        "/" + temporary_output.relative_to(root).as_posix()
                    )
                    compiler_root = root
                arguments = build_compile_arguments(
                    logical_source,
                    logical_temporary,
                    compiler_root,
                    profile=profile,
                )
                try:
                    result = run_seed_tool(
                        manifest_path,
                        root,
                        "cupidc",
                        arguments,
                        timeout=timeout,
                        frozen_seed=seed_inputs,
                        runner=executor,
                    )
                except BootstrapError as error:
                    if isinstance(error.__cause__, subprocess.TimeoutExpired):
                        raise KernelCompileError(
                            f"CupidC timed out after {timeout} seconds for "
                            f"{logical_source.lstrip('/')}"
                        ) from error
                    if isinstance(error.__cause__, OSError):
                        raise KernelCompileError(
                            f"CupidC could not run for "
                            f"{logical_source.lstrip('/')}: "
                            f"{error.__cause__}"
                        ) from error
                    raise KernelCompileError(str(error)) from error
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
                    and (
                        _kernel_input_paths(root, source_name, profile)
                        != input_paths
                        or (
                            profile != "kernel"
                            and _doom_source_membership(root)
                            != profile_source_membership
                        )
                        or _capture_kernel_inputs(input_paths)
                        != captured_inputs
                    )
                ):
                    input_label = (
                        "kernel"
                        if profile == "kernel"
                        else f"{profile} profile"
                    )
                    raise KernelCompileError(
                        f"{input_label} inputs changed while compiling "
                        f"{source_name}"
                    )
                _replace_with_retry(temporary_output, output)
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
    parser.add_argument("--source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument(
        "--write-profile-input-manifest",
        type=Path,
        help=(
            "write the content-addressed Doom profile input manifest and "
            "leave its timestamp unchanged when the input space is unchanged"
        ),
    )
    parser.add_argument(
        "--profile",
        choices=tuple(COMPILER_PROFILE_SOURCES),
        default="kernel",
        help="fixed production compiler profile",
    )
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
    parser = _build_parser()
    arguments = parser.parse_args(argv)
    if arguments.write_profile_input_manifest is not None:
        if arguments.source is not None or arguments.output is not None:
            parser.error(
                "--write-profile-input-manifest cannot be combined with "
                "--source or --output"
            )
        try:
            changed = write_profile_input_manifest(
                arguments.root,
                arguments.write_profile_input_manifest,
                manifest=arguments.manifest,
            )
        except KernelCompileError as error:
            print(
                f"Doom profile input manifest failed: {error}",
                file=sys.stderr,
            )
            return 1
        status = "updated" if changed else "unchanged"
        print(
            "Doom profile input manifest "
            f"{status}: {arguments.write_profile_input_manifest}"
        )
        return 0
    if arguments.source is None or arguments.output is None:
        parser.error(
            "--source and --output are required for a compile operation"
        )
    try:
        compile_kernel_source(
            arguments.root,
            arguments.source,
            arguments.output,
            manifest=arguments.manifest,
            timeout=arguments.timeout,
            profile=arguments.profile,
        )
    except KernelCompileError as error:
        print(f"CupidC kernel compile failed: {error}", file=sys.stderr)
        return 1
    print(f"CupidC kernel object: {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Compile an approved production source with the checked CupidC seed."""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
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


class KernelCompileError(RuntimeError):
    """A checked kernel compilation could not publish an object."""


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
    compiler_root: str,
    *,
    profile: str = "kernel",
) -> tuple[str, ...]:
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


def _profile_input_manifest(root: Path) -> dict[str, object]:
    source_membership = _doom_source_membership(root)
    profile_header_paths = {
        profile: _profile_header_paths(root, profile)
        for profile in ("doom-compat", "doom-tree")
    }
    profile_source_paths = {
        profile: _profile_source_paths(root, profile)
        for profile in profile_header_paths
    }
    paths = tuple(
        sorted(
            {
                path
                for members in profile_header_paths.values()
                for path in members
            }
        )
    )
    captured = _capture_kernel_inputs(paths)
    repeated_header_paths = {
        profile: _profile_header_paths(root, profile)
        for profile in profile_header_paths
    }
    repeated_source_paths = {
        profile: _profile_source_paths(root, profile)
        for profile in profile_source_paths
    }
    if (
        _doom_source_membership(root) != source_membership
        or repeated_header_paths != profile_header_paths
        or repeated_source_paths != profile_source_paths
    ):
        raise KernelCompileError(
            "Doom profile input membership changed while writing its manifest"
        )
    if _capture_kernel_inputs(paths) != captured:
        raise KernelCompileError(
            "Doom profile input bytes changed while writing its manifest"
        )

    relative_names = {
        path: path.relative_to(root).as_posix() for path in paths
    }
    return {
        "schema": "cupid.doom-profile-inputs.v1",
        "profiles": {
            profile: [
                relative_names[path] for path in members
            ]
            for profile, members in profile_header_paths.items()
        },
        "sources": {
            profile: [
                path.relative_to(root).as_posix() for path in members
            ]
            for profile, members in profile_source_paths.items()
        },
        "inputs": [
            {
                "path": relative_names[path],
                "bytes": len(captured[path]),
                "sha256": hashlib.sha256(captured[path]).hexdigest(),
            }
            for path in paths
        ],
    }


def write_profile_input_manifest(root: Path, output: Path) -> bool:
    """Write the Doom profile input manifest when its content has changed."""
    root = _root_path(root)
    candidate = output if output.is_absolute() else root / output
    if candidate.is_symlink():
        raise KernelCompileError("profile input manifest may not be a symlink")
    try:
        parent = candidate.parent.resolve(strict=False)
        resolved = (parent / candidate.name).resolve(strict=False)
        resolved.relative_to(root)
    except (OSError, ValueError) as error:
        raise KernelCompileError(
            f"profile input manifest must stay inside repository root: {output}"
        ) from error
    if resolved.suffix != ".json":
        raise KernelCompileError(
            "profile input manifest must use the .json suffix"
        )
    try:
        parent.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise KernelCompileError(
            f"could not create profile input manifest directory {parent}: "
            f"{error}"
        ) from error
    payload = (
        json.dumps(
            _profile_input_manifest(root),
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")
    try:
        if resolved.is_file() and resolved.read_bytes() == payload:
            return False
        with tempfile.NamedTemporaryFile(
            prefix=f".{resolved.name}.",
            suffix=".tmp",
            dir=parent,
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(payload)
        try:
            _replace_with_retry(temporary_path, resolved)
        finally:
            if temporary_path.exists():
                temporary_path.unlink()
    except OSError as error:
        raise KernelCompileError(
            f"could not publish profile input manifest {resolved}: {error}"
        ) from error
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
                    profile=profile,
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
            )
        except KernelCompileError as error:
            print(
                f"CupidC profile input manifest failed: {error}",
                file=sys.stderr,
            )
            return 1
        status = "updated" if changed else "unchanged"
        print(
            "CupidC profile input manifest "
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

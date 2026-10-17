import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

from tools import kernel_cupidc_frontier as frontier


REPO_ROOT = Path(__file__).resolve().parents[1]
FRONTIER_TOOL = REPO_ROOT / "tools" / "kernel_cupidc_frontier.py"
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / ("i386-windows" if os.name == "nt" else "i386-linux")
    / "manifest.json"
)

CRYPTO_SOURCES = [
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
]
SMP_SOURCES = [
    "kernel/smp/acpi.cc",
    "kernel/smp/mp_tables.cc",
    "kernel/smp/percpu.cc",
    "kernel/smp/smp.cc",
]
OPERAND_FREE_SOURCES = [
    "drivers/e1000.cc",
    "kernel/gui/desktop.cc",
    "kernel/network/socket.cc",
    "kernel/network/tcp.cc",
]
PORT_IO_SOURCES = [
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
]
COMPILER_READY_SOURCES = [
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
]
TOOLCHAIN_KERNEL_SOURCES = [
    "toolchain/ctool.cc",
    "toolchain/cupidasm.cc",
    "toolchain/cupiddis.cc",
    "toolchain/cupidld.cc",
    "toolchain/elf32.cc",
    "toolchain/x86.cc",
]
SOURCE_DRIVEN_SOURCES = [
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
]
KERNEL_SOURCES = sorted(
    CRYPTO_SOURCES
    + SMP_SOURCES
    + OPERAND_FREE_SOURCES
    + PORT_IO_SOURCES
    + COMPILER_READY_SOURCES
    + TOOLCHAIN_KERNEL_SOURCES
    + SOURCE_DRIVEN_SOURCES
)
FRONTIER_TIMEOUT_SECONDS = max(1200, 15 * len(KERNEL_SOURCES))

BOUNDARY_DIAGNOSTICS = {}

KERNEL_I386_PROFILE = [
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
]


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
    section_offset = _align(
        section_string_offset + len(section_strings),
        4,
    )
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

    sections = [
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
    ]
    for index, section in enumerate(sections):
        struct.pack_into(
            "<IIIIIIIIII",
            image,
            section_offset + index * 40,
            *section,
        )
    return bytes(image)


def _write_fake_compiler(path):
    path.write_text(
        textwrap.dedent(
            """
            import shutil
            import sys
            from pathlib import Path

            BOUNDARIES = {}

            arguments = sys.argv[1:]
            source = arguments[arguments.index("-c") + 1]
            output = arguments[arguments.index("-o") + 1]
            root = Path(arguments[arguments.index("--root") + 1])

            if source in BOUNDARIES:
                line, code, message = BOUNDARIES[source]
                sys.stderr.write(
                    f"{source}:{line}:1: error {code}: {message}\\n"
                )
                raise SystemExit(1)

            destination = root / output.lstrip("/")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / "fixture.o", destination)
            """
        ).lstrip(),
        encoding="utf-8",
    )


def _write_portable_fake_seed(path):
    path.write_text(
        textwrap.dedent(
            """\
            #!/bin/sh
            source=
            output=
            root=
            while [ "$#" -gt 0 ]; do
                case "$1" in
                    -c)
                        source=$2
                        shift 2
                        ;;
                    -o)
                        output=$2
                        shift 2
                        ;;
                    --root)
                        root=$2
                        shift 2
                        ;;
                    *)
                        shift
                        ;;
                esac
            done

            destination="$root/${output#/}"
            mkdir -p "$(dirname "$destination")"
            cp "$root/fixture.o" "$destination"
            """
        ),
        encoding="utf-8",
        newline="\n",
    )


class WslPrivateDirectoryTests(unittest.TestCase):
    def test_exact_frontier_directory_is_accepted(self):
        self.assertEqual(
            frontier._validated_wsl_private_directory(
                "/tmp/cupid-kernel-frontier.ABC123\n"
            ),
            "/tmp/cupid-kernel-frontier.ABC123",
        )

    def test_broad_or_malformed_cleanup_targets_are_rejected(self):
        invalid = (
            "/",
            "/tmp",
            "/tmp/cupid-kernel-frontier.ABC123/../other",
            "/tmp/cupid-kernel-frontier.ABC12!",
            "/tmp/cupid-kernel-frontier.ABC123 extra",
            "/tmp/other-frontier.ABC123",
            (
                "/tmp/cupid-kernel-frontier.ABC123\n"
                "/tmp/cupid-kernel-frontier.DEF456\n"
            ),
        )
        for value in invalid:
            with self.subTest(value=value), self.assertRaisesRegex(
                frontier.FrontierError,
                "invalid private seed directory",
            ):
                frontier._validated_wsl_private_directory(value)


class FrontierInputSnapshotTests(unittest.TestCase):
    def test_source_hash_canonicalizes_crlf(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lf = root / "lf.h"
            crlf = root / "crlf.h"
            lf.write_bytes(b"first\nsecond\n")
            crlf.write_bytes(b"first\r\nsecond\r\n")

            self.assertEqual(
                frontier._source_sha256(lf),
                frontier._source_sha256(crlf),
            )
            self.assertEqual(
                frontier._source_sha256(lf),
                hashlib.sha256(b"first\nsecond\n").hexdigest(),
            )

    def test_source_hash_canonicalizes_lone_carriage_returns(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lf = root / "lf.h"
            cr = root / "cr.h"
            lf.write_bytes(b"first\nsecond\n")
            cr.write_bytes(b"first\rsecond\n")

            self.assertEqual(
                frontier._source_sha256(lf),
                frontier._source_sha256(cr),
            )
            self.assertEqual(
                frontier._source_sha256(cr),
                hashlib.sha256(b"first\nsecond\n").hexdigest(),
            )

    def test_input_inventory_ignores_private_compiler_staging_headers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tracked = root / "kernel" / "core" / "types.h"
            tracked.parent.mkdir(parents=True)
            tracked.write_text("typedef unsigned int uint32_t;\n")
            private = (
                root
                / "kernel"
                / "cpu"
                / ".ksyms_data.o.cupidc-private"
                / "kernel"
                / "core"
                / "types.h"
            )
            private.parent.mkdir(parents=True)
            private.write_text("typedef unsigned int private_uint32_t;\n")

            inputs = frontier._input_paths(root)

            self.assertIn(tracked, inputs)
            self.assertNotIn(private, inputs)


class FrontierPublicationTests(unittest.TestCase):
    def test_transient_permission_error_retries_atomic_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            output = parent / "frontier"
            real_replace = frontier.os.replace
            attempts = []

            def replace(source, destination):
                attempts.append((Path(source), Path(destination)))
                if len(attempts) < 3:
                    raise PermissionError(13, "frontier still busy")
                real_replace(source, destination)

            with (
                mock.patch.object(
                    frontier.os,
                    "replace",
                    side_effect=replace,
                ),
                mock.patch.object(frontier.time, "sleep") as sleep,
            ):
                with frontier._staged_output(output) as staging:
                    (staging / "manifest.json").write_text(
                        "{}\n",
                        encoding="utf-8",
                    )

            self.assertEqual(len(attempts), 3)
            self.assertEqual(
                [call.args[0] for call in sleep.call_args_list],
                list(frontier.PUBLISH_RETRY_DELAYS_SECONDS[:2]),
            )
            self.assertEqual(
                (output / "manifest.json").read_text(encoding="utf-8"),
                "{}\n",
            )

    def test_persistent_permission_error_fails_without_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "frontier"
            denied = PermissionError(13, "frontier remains busy")

            with (
                mock.patch.object(
                    frontier.os,
                    "replace",
                    side_effect=denied,
                ) as replace,
                mock.patch.object(frontier.time, "sleep") as sleep,
                self.assertRaisesRegex(
                    frontier.FrontierError,
                    "could not publish frontier directory",
                ),
            ):
                with frontier._staged_output(output):
                    pass

            self.assertEqual(
                replace.call_count,
                len(frontier.PUBLISH_RETRY_DELAYS_SECONDS) + 1,
            )
            self.assertEqual(
                [call.args[0] for call in sleep.call_args_list],
                list(frontier.PUBLISH_RETRY_DELAYS_SECONDS),
            )
            self.assertFalse(output.exists())


class FrontierElfValidationTests(unittest.TestCase):
    def test_program_headers_are_rejected(self):
        malformed = bytearray(_valid_elf32_object())
        struct.pack_into("<I", malformed, 28, 52)
        struct.pack_into("<H", malformed, 42, 32)
        struct.pack_into("<H", malformed, 44, 1)

        with self.assertRaisesRegex(
            frontier.FrontierError,
            "unexpectedly has program headers",
        ):
            frontier._validate_elf32_header(malformed)

    def test_missing_required_string_table_section_is_rejected(self):
        malformed = bytearray(_valid_elf32_object())
        section_offset = struct.unpack_from("<I", malformed, 32)[0]
        struct.pack_into("<I", malformed, section_offset + 4 * 40, 0)

        with self.assertRaisesRegex(
            frontier.FrontierError,
            r"missing required section \.strtab",
        ):
            frontier._validate_elf32_header(malformed)

    def test_missing_symbol_table_is_rejected(self):
        malformed = bytearray(_valid_elf32_object())
        section_offset = struct.unpack_from("<I", malformed, 32)[0]
        struct.pack_into("<I", malformed, section_offset + 3 * 40 + 4, 1)

        with self.assertRaisesRegex(
            frontier.FrontierError,
            "has no symbol table",
        ):
            frontier._validate_elf32_header(malformed)


class DefaultSeedExecutionTests(unittest.TestCase):
    def test_windows_default_uses_a_frozen_native_seed_without_wsl(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            manifest = root / "bootstrap/seeds/i386-windows/manifest.json"
            captured = []

            def freeze(selected, snapshot):
                self.assertEqual(selected, manifest)
                frozen = snapshot / "cupidc.exe"
                frozen.write_bytes(b"native checked seed")
                captured.append(frozen)
                return mock.Mock(tools={"cupidc": frozen}, manifest_sha256="c" * 64)

            with (
                mock.patch.object(frontier.os, "name", "nt"),
                mock.patch.object(frontier, "freeze_seed_inputs", side_effect=freeze),
                mock.patch.object(frontier.shutil, "which", side_effect=AssertionError("WSL selected")),
                mock.patch.object(frontier.subprocess, "run", side_effect=AssertionError("bridge selected")),
            ):
                with frontier._default_seed_execution(root) as execution:
                    prefix, compiler_root, provenance = execution
                    self.assertEqual(prefix, [str(captured[0])])
                    self.assertEqual(compiler_root, str(root))
                    self.assertEqual(captured[0].read_bytes(), b"native checked seed")
                    self.assertEqual(provenance["compiler"]["seed_manifest_sha256"], "c" * 64)
            self.assertFalse(captured[0].exists())

    def test_explicit_windows_seed_is_rejected_on_a_posix_host(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()

            def freeze(_manifest, snapshot):
                frozen = snapshot / "cupidc.exe"
                frozen.write_bytes(b"native checked seed")
                return mock.Mock(tools={"cupidc": frozen}, manifest_sha256="c" * 64)

            with (
                mock.patch.object(frontier.os, "name", "posix"),
                mock.patch.object(frontier, "freeze_seed_inputs", side_effect=freeze),
                mock.patch.object(frontier.subprocess, "run", side_effect=AssertionError("foreign image executed")),
            ):
                with self.assertRaisesRegex(frontier.FrontierError, "Windows seed requires a Windows host"):
                    with frontier._default_seed_execution(root, root / "windows/manifest.json"):
                        self.fail("foreign seed was accepted")

    def test_invalid_native_seed_does_not_fall_back_to_wsl(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            with (
                mock.patch.object(frontier.os, "name", "nt"),
                mock.patch.object(frontier, "freeze_seed_inputs", side_effect=frontier.BootstrapError("invalid seed")),
                mock.patch.object(frontier.shutil, "which") as which,
            ):
                with self.assertRaisesRegex(frontier.FrontierError, "checked seed verification failed: invalid seed"):
                    with frontier._default_seed_execution(root):
                        self.fail("invalid seed was accepted")
                which.assert_not_called()

    def test_seed_manifest_cannot_be_combined_with_unchecked_execution(self):
        arguments = frontier._parse_arguments([
            "--seed-manifest", "checked/manifest.json", "--output-dir", "output"])
        self.assertEqual(arguments.seed_manifest, Path("checked/manifest.json"))
        for selection in (
            ["--compiler", "compiler"],
            ["--compiler-host-path", "compiler"],
            ["--runner", "runner"],
            ["--runner-arg", "argument"],
        ):
            with self.subTest(selection=selection), mock.patch.object(frontier.sys, "stderr"):
                with self.assertRaises(SystemExit) as error:
                    frontier._parse_arguments([
                        "--seed-manifest", "checked/manifest.json", "--output-dir", "output", *selection])
                self.assertEqual(error.exception.code, 2)

    def test_compiler_host_path_alone_selects_explicit_execution(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            compiler = root / "explicit-cupidc"
            compiler.write_bytes(b"explicit compiler fixture")
            arguments = frontier._parse_arguments(
                [
                    "--root",
                    str(root),
                    "--compiler-host-path",
                    str(compiler),
                    "--output-dir",
                    str(root / "frontier"),
                ]
            )

            with (
                mock.patch.object(
                    frontier,
                    "_default_seed_execution",
                    side_effect=AssertionError("default seed selected"),
                ),
                mock.patch.object(frontier, "_execute_frontier") as execute,
            ):
                frontier._run_frontier(arguments)

            execute.assert_called_once()
            self.assertEqual(
                execute.call_args.args[4]["compiler"]["mode"],
                "explicit",
            )

    def test_cli_freezes_an_explicit_portable_compiler(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object())
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=60,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "kernel CupidC frontier: ok "
                f"({len(KERNEL_SOURCES)} sources, 0 boundaries)\n",
            )
            self.assertEqual(result.stderr, "")
            manifest = json.loads(
                (root / "frontier" / "manifest.json").read_text(encoding="utf-8")
            )
            profile_encoding = json.dumps(
                KERNEL_I386_PROFILE,
                ensure_ascii=True,
                separators=(",", ":"),
            ).encode("ascii")
            self.assertEqual(
                manifest["provenance"],
                {
                    "compiler": {
                        "mode": "explicit",
                        "sha256": hashlib.sha256(
                            compiler.read_bytes()
                        ).hexdigest(),
                        "size": compiler.stat().st_size,
                    },
                    "profile": {
                        "arguments": KERNEL_I386_PROFILE,
                        "name": "KERNEL_I386",
                        "sha256": hashlib.sha256(profile_encoding).hexdigest(),
                    },
                },
            )

    def test_linux_seed_is_staged_executable_and_removed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            seed = root / "bootstrap" / "seeds" / "i386-linux" / "cupidc.elf"
            seed.parent.mkdir(parents=True)
            seed.write_bytes(b"checked seed")

            def freeze(_manifest, snapshot):
                frozen = snapshot / "cupidc.elf"
                frozen.write_bytes(seed.read_bytes())
                frozen.chmod(0o700)
                return mock.Mock(
                    tools={"cupidc": frozen},
                    manifest_sha256="a" * 64,
                )

            with (
                mock.patch.object(frontier.os, "name", "posix"),
                mock.patch.object(
                    frontier,
                    "freeze_seed_inputs",
                    side_effect=freeze,
                ),
                mock.patch.object(
                    type(root),
                    "chmod",
                    autospec=True,
                ) as chmod,
            ):
                with frontier._default_seed_execution(root) as execution:
                    command_prefix, compiler_root, provenance = execution
                    staged_seed = type(root)(command_prefix[0])
                    self.assertEqual(compiler_root, str(root))
                    self.assertEqual(staged_seed.read_bytes(), b"checked seed")
                    self.assertEqual(
                        provenance["compiler"]["seed_manifest_sha256"],
                        "a" * 64,
                    )
                    chmod.assert_called_once_with(staged_seed, 0o700)

            self.assertFalse(staged_seed.exists())

    def test_explicit_linux_seed_is_staged_in_wsl_tmp_and_removed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            seed = root / "bootstrap" / "seeds" / "i386-linux" / "cupidc.elf"
            seed.parent.mkdir(parents=True)
            seed.write_bytes(b"checked seed")
            calls = []

            def freeze(_manifest, snapshot):
                frozen = snapshot / "cupidc.elf"
                frozen.write_bytes(seed.read_bytes())
                return mock.Mock(
                    tools={"cupidc": frozen},
                    manifest_sha256="b" * 64,
                )

            def fake_run(command, **_kwargs):
                calls.append(command)
                if "wslpath" in command:
                    translated = (
                        "/mnt/repository/cupidc.elf"
                        if str(command[-1]).endswith("cupidc.elf")
                        else "/mnt/repository"
                    )
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout=translated + "\n",
                        stderr="",
                    )
                if "mktemp -d" in " ".join(command):
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout="/tmp/cupid-kernel-frontier.ABC123\n",
                        stderr="",
                    )
                return subprocess.CompletedProcess(
                    command,
                    0,
                    stdout="",
                    stderr="",
                )

            with (
                mock.patch.object(frontier.os, "name", "nt"),
                mock.patch.object(frontier.shutil, "which", return_value="wsl"),
                mock.patch.object(
                    frontier,
                    "freeze_seed_inputs",
                    side_effect=freeze,
                ),
                mock.patch.object(
                    frontier.subprocess,
                    "run",
                    side_effect=fake_run,
                ),
            ):
                with frontier._default_seed_execution(
                    root, seed.parent / "manifest.json"
                ) as execution:
                    command_prefix, compiler_root, provenance = execution
                    self.assertEqual(command_prefix[:2], ["wsl", "-e"])
                    staged_seed = command_prefix[2]
                    self.assertEqual(
                        staged_seed,
                        "/tmp/cupid-kernel-frontier.ABC123/tool",
                    )
                    self.assertEqual(compiler_root, "/mnt/repository")
                    self.assertEqual(
                        provenance["compiler"]["seed_manifest_sha256"],
                        "b" * 64,
                    )

            self.assertEqual(calls[2][:3], ["wsl", "-e", "sh"])
            self.assertIn("/mnt/repository/cupidc.elf", calls[2])
            self.assertIn("mktemp -d", calls[2][4])
            self.assertIn(
                "/tmp/cupid-kernel-frontier.XXXXXX",
                calls[2][4],
            )
            self.assertNotIn("TMPDIR", calls[2][4])
            self.assertEqual(
                calls[-1],
                [
                    "wsl",
                    "-e",
                    "rm",
                    "-rf",
                    "--",
                    "/tmp/cupid-kernel-frontier.ABC123",
                ],
            )


class KernelCupidCFrontierCliTests(unittest.TestCase):
    def test_duplicate_object_stems_are_rejected_case_insensitively(self):
        with self.assertRaisesRegex(
            frontier.FrontierError,
            (
                "frontier object name collision: "
                "drivers/Shared.c and kernel/shared.c both use shared.o"
            ),
        ):
            frontier._require_unique_object_names(
                ("drivers/Shared.c", "kernel/shared.c")
            )

    def test_output_path_must_be_a_directory(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            output = root / "frontier"
            output.write_text("not a directory\n", encoding="utf-8")

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn(
                f"output path is not a directory: {output}",
                result.stderr,
            )

    def test_unexpected_crypto_source_is_rejected_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            unexpected = root / "kernel" / "crypto" / "new_cipher.c"
            unexpected.write_text("int new_cipher;\n", encoding="utf-8")
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel crypto source inventory differs",
                result.stderr,
            )
            self.assertIn(
                "kernel/crypto/new_cipher.c",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_exact_approved_cohort_compiles_twice_with_matching_objects(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            fixture = _valid_elf32_object()
            (root / "fixture.o").write_bytes(fixture)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "kernel CupidC frontier: ok "
                f"({len(KERNEL_SOURCES)} sources, 0 boundaries)\n",
            )
            self.assertEqual(result.stderr, "")

            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                manifest["schema"],
                "cupid.kernel-cupidc-frontier.v1",
            )
            self.assertEqual(
                [entry["source"] for entry in manifest["sources"]],
                KERNEL_SOURCES,
            )
            self.assertEqual(
                manifest["boundaries"],
                [
                    {
                        "source": source,
                        "line": line,
                        "code": code,
                        "message": message,
                        "source_sha256": frontier._source_sha256(
                            root / source
                        ),
                    }
                    for source, (line, code, message) in (BOUNDARY_DIAGNOSTICS.items())
                ],
            )
            self.assertEqual(list((output / "negative").iterdir()), [])
            self.assertEqual(
                manifest["input_snapshot"]["count"],
                len(KERNEL_SOURCES),
            )
            self.assertEqual(
                len(manifest["input_snapshot"]["files"]),
                len(KERNEL_SOURCES),
            )
            self.assertEqual(
                len(manifest["input_snapshot"]["sha256"]),
                64,
            )
            profile_encoding = json.dumps(
                KERNEL_I386_PROFILE,
                ensure_ascii=True,
                separators=(",", ":"),
            ).encode("ascii")
            self.assertEqual(
                manifest["provenance"],
                {
                    "compiler": {
                        "mode": "explicit",
                        "sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
                        "size": compiler.stat().st_size,
                    },
                    "profile": {
                        "arguments": KERNEL_I386_PROFILE,
                        "name": "KERNEL_I386",
                        "sha256": hashlib.sha256(profile_encoding).hexdigest(),
                    },
                },
            )
            expected_hash = hashlib.sha256(fixture).hexdigest()
            for source in KERNEL_SOURCES:
                name = Path(source).stem + ".o"
                first = output / "first" / name
                second = output / "second" / name
                self.assertEqual(first.read_bytes(), fixture)
                self.assertEqual(second.read_bytes(), fixture)
                entry = next(
                    item for item in manifest["sources"] if item["source"] == source
                )
                self.assertEqual(entry["size"], len(fixture))
                self.assertEqual(entry["object_sha256"], expected_hash)
                self.assertEqual(
                    entry["source_sha256"],
                    frontier._source_sha256(root / source),
                )

    def test_missing_approved_smp_source_is_rejected_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "kernel" / "smp" / "acpi.cc").unlink()
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "approved kernel source is not a file: "
                "kernel/smp/acpi.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_missing_approved_port_io_source_is_rejected_before_publication(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "drivers" / "ata.cc").unlink()
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "approved kernel source is not a file: drivers/ata.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_truncated_elf32_section_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object()[:-1])
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "emitted object has a truncated section header table",
                result.stderr,
            )

    def test_section_payload_outside_object_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            struct.pack_into(
                "<II",
                malformed,
                section_offset + 40 + 16,
                len(malformed) - 2,
                8,
            )
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "emitted object section 1 payload is outside the file",
                result.stderr,
            )

    def test_symbol_name_outside_string_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            symbol_offset = struct.unpack_from(
                "<I",
                malformed,
                section_offset + 3 * 40 + 16,
            )[0]
            struct.pack_into("<I", malformed, symbol_offset + 16, 0xFFFFFFFF)
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "emitted object symbol 1 has an invalid name",
                result.stderr,
            )

    def test_relocation_symbol_outside_symbol_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            relocation_offset = struct.unpack_from(
                "<I",
                malformed,
                section_offset + 2 * 40 + 16,
            )[0]
            struct.pack_into(
                "<I",
                malformed,
                relocation_offset + 4,
                (99 << 8) | 1,
            )
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "emitted object relocation 0 has an invalid symbol",
                result.stderr,
            )

    def test_relocation_type_outside_cupidc_contract_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            relocation_offset = struct.unpack_from(
                "<I",
                malformed,
                section_offset + 2 * 40 + 16,
            )[0]
            struct.pack_into(
                "<I",
                malformed,
                relocation_offset + 4,
                (2 << 8) | 42,
            )
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "emitted object relocation 0 uses unsupported i386 type 42",
                result.stderr,
            )

    def test_explicit_addend_relocation_section_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            struct.pack_into("<I", malformed, section_offset + 2 * 40 + 4, 4)
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "emitted object relocation section 2 uses RELA",
                result.stderr,
            )

    def test_absolute_relocation_addend_selects_a_static_subobject(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            text_offset = struct.unpack_from(
                "<I",
                malformed,
                section_offset + 40 + 16,
            )[0]
            struct.pack_into("<i", malformed, text_offset, 4)
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root / "frontier" / "manifest.json").is_file())

    def test_pc_relative_relocation_addend_outside_contract_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            malformed = bytearray(_valid_elf32_object())
            section_offset = struct.unpack_from("<I", malformed, 32)[0]
            text_offset = struct.unpack_from(
                "<I",
                malformed,
                section_offset + 40 + 16,
            )[0]
            struct.pack_into("<i", malformed, text_offset + 4, 4)
            (root / "fixture.o").write_bytes(malformed)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.cc produced invalid ELF32: "
                "PC-relative relocation addend is 4, expected -4",
                result.stderr,
            )

    def test_failed_approved_compile_cannot_publish_the_frontier(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object())
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    "if source in BOUNDARIES:\n",
                    (
                        'if source == "/kernel/smp/acpi.cc":\n'
                        '    destination = root / output.lstrip("/")\n'
                        "    destination.parent.mkdir("
                        "parents=True, exist_ok=True)\n"
                        '    destination.write_bytes(b"partial")\n'
                        '    sys.stderr.write("forced compile failure\\n")\n'
                        "    raise SystemExit(1)\n"
                        "\n"
                        "if source in BOUNDARIES:\n"
                    ),
                    1,
                ),
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel/smp/acpi.cc did not compile: "
                "forced compile failure",
                result.stderr,
            )
            self.assertFalse((root / "frontier").exists())

    def test_late_port_io_compile_failure_cannot_publish_the_frontier(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object())
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    "if source in BOUNDARIES:\n",
                    (
                        'if source == "/kernel/usb/uhci.cc":\n'
                        '    destination = root / output.lstrip("/")\n'
                        "    destination.parent.mkdir("
                        "parents=True, exist_ok=True)\n"
                        '    destination.write_bytes(b"partial")\n'
                        '    sys.stderr.write("forced late failure\\n")\n'
                        "    raise SystemExit(1)\n"
                        "\n"
                        "if source in BOUNDARIES:\n"
                    ),
                    1,
                ),
                encoding="utf-8",
            )
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel/usb/uhci.cc did not compile: forced late failure",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_nondeterministic_smp_object_cannot_publish_the_frontier(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            first = _valid_elf32_object()
            second = bytearray(first)
            symbol_name = second.find(b"entry")
            self.assertGreaterEqual(symbol_name, 0)
            second[symbol_name] = ord("E")
            (root / "fixture.o").write_bytes(first)
            (root / "fixture-second.o").write_bytes(second)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    'shutil.copyfile(root / "fixture.o", destination)\n',
                    (
                        'if source == "/kernel/smp/acpi.cc":\n'
                        '    marker = root / "acpi-first.done"\n'
                        "    fixture = (\n"
                        '        root / "fixture-second.o"\n'
                        "        if marker.exists()\n"
                        '        else root / "fixture.o"\n'
                        "    )\n"
                        '    marker.write_text("seen\\n", encoding="utf-8")\n'
                        "else:\n"
                        '    fixture = root / "fixture.o"\n'
                        "shutil.copyfile(fixture, destination)\n"
                    ),
                    1,
                ),
                encoding="utf-8",
            )
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel/smp/acpi.cc object output is not deterministic",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_nondeterministic_port_io_object_cannot_publish_the_frontier(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            first = _valid_elf32_object()
            second = bytearray(first)
            symbol_name = second.find(b"entry")
            self.assertGreaterEqual(symbol_name, 0)
            second[symbol_name] = ord("E")
            (root / "fixture.o").write_bytes(first)
            (root / "fixture-second.o").write_bytes(second)
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    'shutil.copyfile(root / "fixture.o", destination)\n',
                    (
                        'if source == "/kernel/lang/shell.cc":\n'
                        '    marker = root / "shell-first.done"\n'
                        "    fixture = (\n"
                        '        root / "fixture-second.o"\n'
                        "        if marker.exists()\n"
                        '        else root / "fixture.o"\n'
                        "    )\n"
                        '    marker.write_text("seen\\n", encoding="utf-8")\n'
                        "else:\n"
                        '    fixture = root / "fixture.o"\n'
                        "shutil.copyfile(fixture, destination)\n"
                    ),
                    1,
                ),
                encoding="utf-8",
            )
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel/lang/shell.cc object output is not deterministic",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_success_without_an_object_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object())
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    'shutil.copyfile(root / "fixture.o", destination)\n',
                    "raise SystemExit(0)\n",
                    1,
                ),
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(root / "frontier"),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn(
                "drivers/ata.cc did not publish an object",
                result.stderr,
            )
            self.assertFalse((root / "frontier").exists())

    def test_source_drift_stops_without_publishing_a_partial_frontier(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES + list(BOUNDARY_DIAGNOSTICS):
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object())
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    'shutil.copyfile(root / "fixture.o", destination)\n',
                    (
                        'shutil.copyfile(root / "fixture.o", destination)\n'
                        '(root / "kernel/crypto/aes.cc").write_text('
                        '"int changed;\\n", encoding="utf-8")\n'
                    ),
                    1,
                ),
                encoding="utf-8",
            )
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel CupidC inputs changed during frontier run: "
                "kernel/crypto/aes.cc",
                result.stderr,
            )
            self.assertFalse(output.exists())

    def test_port_io_header_drift_stops_without_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            header = root / "kernel" / "core" / "ports.h"
            header.write_text("int ports_fixture;\n", encoding="utf-8")
            (root / "fixture.o").write_bytes(_valid_elf32_object())
            compiler = root / "fake_cupidc.py"
            _write_fake_compiler(compiler)
            compiler.write_text(
                compiler.read_text(encoding="utf-8").replace(
                    'shutil.copyfile(root / "fixture.o", destination)\n',
                    (
                        'shutil.copyfile(root / "fixture.o", destination)\n'
                        '(root / "kernel/core/ports.h").write_text('
                        '"int changed;\\n", encoding="utf-8")\n'
                    ),
                    1,
                ),
                encoding="utf-8",
            )
            output = root / "frontier"

            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(root),
                    "--compiler",
                    str(compiler),
                    "--runner",
                    sys.executable,
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel CupidC inputs changed during frontier run: "
                "kernel/core/ports.h",
                result.stderr,
            )
            self.assertFalse(output.exists())


class RealKernelCupidCFrontierTests(unittest.TestCase):
    def test_checked_seed_compiles_the_complete_approved_cohort(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        seed = SEED_MANIFEST.parent / ("cupidc.exe" if os.name == "nt" else "cupidc.elf")
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".kernel-cupidc-frontier-test-",
            dir=REPO_ROOT,
        ) as temporary:
            output = Path(temporary) / "result"
            result = subprocess.run(
                [
                    sys.executable,
                    str(FRONTIER_TOOL),
                    "--root",
                    str(REPO_ROOT),
                    "--output-dir",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=FRONTIER_TIMEOUT_SECONDS,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "kernel CupidC frontier: ok "
                f"({len(KERNEL_SOURCES)} sources, 0 boundaries)\n",
            )
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                [entry["source"] for entry in manifest["sources"]],
                KERNEL_SOURCES,
            )
            self.assertEqual(manifest["boundaries"], [])
            self.assertEqual(
                sum(entry["size"] for entry in manifest["sources"]),
                4210824,
            )
            object_records = {
                entry["source"]: (entry["size"], entry["object_sha256"])
                for entry in manifest["sources"]
            }
            self.assertEqual(
                object_records["kernel/smp/acpi.cc"],
                (
                    5360,
                    "446ff25ac8e3ecf0f7cf1292b989487d"
                    "cef4c48bb7e263bf7cef4743cdbd0955",
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/mp_tables.cc"],
                (
                    3960,
                    "3d3b5d3cfc539fb82dab1b780de69580"
                    "4a8f6f1d26b3be770986b176ee2d30f5",
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/percpu.cc"],
                (
                    6632,
                    "c9ccaa9a23a5e1f9b3076ca81c2971a9"
                    "02fa85b7ae78a409ec37e7b93253f956",
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/smp.cc"],
                (
                    8032,
                    "c21b0097d625494a1a9a536a0a491b8b"
                    "6bdde2ea5d81f0eb46c4f23558c25e75",
                ),
            )
            self.assertEqual(
                object_records["drivers/e1000.cc"],
                (
                    8160,
                    "9f105fa046b21e0a1ee19b54e63a5b5f"
                    "19249820b85f439793e91aa8e30fdb96",
                ),
            )
            self.assertEqual(
                object_records["kernel/gui/desktop.cc"],
                (
                    111152,
                    "b211f54bddf262907c19739d7c1750f9"
                    "5ea7bfc34209263a3f373b618c52be99",
                ),
            )
            self.assertEqual(
                object_records["kernel/network/socket.cc"],
                (
                    11552,
                    "3d79638290fbe28af2efe83d7de802bd"
                    "15f4a6668b54baa51bb47e545b5547eb",
                ),
            )
            self.assertEqual(
                object_records["kernel/network/tcp.cc"],
                (
                    18356,
                    "7868defe8a9f6f5cb29edfd642153e74"
                    "ad4cd842f439a372b2deb7bbedf39e35",
                ),
            )
            self.assertEqual(
                object_records["toolchain/x86.cc"],
                (
                    136108,
                    "c791b14905e4f609fb833395a3a64989"
                    "479f08608d6944df0e2ffc0dbb17c5d2",
                ),
            )
            port_io_object_records = {
                "drivers/ata.cc": (
                                      10536,
                                      "e64a799ea615023a0e03be84892ec358"
                                      "a03b4d7213f849127840fbba869c9f0a",
                                  ),
                "drivers/keyboard.cc": (
                                           11480,
                                           "50ba7bae23f16b91e714d71cbf1fc3cf"
                                           "1c1b5d7f8761b8bcb36407350fb8ff61",
                                       ),
                "drivers/mouse.cc": (
                                        12580,
                                        "a087b17123ee4f6f085f02e8fb50912c"
                                        "d213afe95a75bf0bf39dd337d953ec16",
                                    ),
                "drivers/pci.cc": (
                                      6880,
                                      "15201d88616602fc37bd18edf6c9faf3"
                                      "b23db8f245afecf22e81eeea537ef1df",
                                  ),
                "drivers/pit.cc": (
                                      1768,
                                      "b81285f9221049d49879832c77492295"
                                      "935eef5fa73708a8dacab84cae0b953a",
                                  ),
                "drivers/rtc.cc": (
                                      7356,
                                      "3ba79a5ea4e0990a8d45006ca2677855"
                                      "dd4f535f8b461e21cf8486e6e09f6ac7",
                                  ),
                "drivers/rtl8139.cc": (
                                          7944,
                                          "ffd058c6ec49c962d78553627c66f6ef"
                                          "6384a465d7cccd5bbc7e4de4a595ef55",
                                      ),
                "drivers/speaker.cc": (
                                          1560,
                                          "ba736f568bad56a6d2372de9031964aa"
                                          "c329441fd5f8afe3e051765b6dc9cd7a",
                                      ),
                "drivers/vga.cc": (
                                      4572,
                                      "8bbda29c6b7d4f38a4215a6aef1f1f6d"
                                      "bafc2bb995afcd4fbc47939c11667786",
                                  ),
                "kernel/audio/ac97.cc": (
                                            13716,
                                            "f64eba1824a413aa51819c1d5e2b68b7"
                                            "d71cd3f16b31170b40bbe14c3c2a5ec5",
                                        ),
                "kernel/core/syscall.cc": (
                                              12212,
                                              "c670f12eec319dc0b67afef7204b684a"
                                              "3f852f6672f5ee81febcc85369255adc",
                                          ),
                "kernel/lang/shell.cc": (
                                            166276,
                                            "4a2beced940133d29cc1c9a093ffd194"
                                            "fa4a10d0904a4de1f59c7e4d87c37d1c",
                                        ),
                "kernel/usb/ehci.cc": (
                                          21084,
                                          "1ff10a1ae85f01016acd59c6c565201f"
                                          "be29f51818ed7b9292b54c6ef4e259b3",
                                      ),
                "kernel/usb/uhci.cc": (
                                          17452,
                                          "3d88d59b87304f8b456f91e041051784"
                                          "610fe1ca14fe8af83c7b1a235b30a9c2",
                                      ),
            }
            self.assertEqual(
                {
                    source: object_records[source]
                    for source in PORT_IO_SOURCES
                },
                port_io_object_records,
            )
            source_driven_object_records = {
                "drivers/serial.cc": (
                                         20464,
                                         "ec33482a7678328a72d1cf2040025bcd"
                                         "803824ad42db4a7f659d44162c047887",
                                     ),
                "drivers/timer.cc": (
                                        6260,
                                        "9ef289e24b3eeb134d2e483e302f6cf5"
                                        "d31b47f64f1489ed105aa67a4d0e632f",
                                    ),
                "kernel/audio/nuked_opl3.cc": (
                                                  37984,
                                                  "33e89e9027f58883caadaa4ff4ae5f0d"
                                                  "eaa924df5ebed88d2a0da7d194ec4b01",
                                              ),
                "kernel/core/app_launch.cc": (
                                                 5312,
                                                 "a9e65976fdd78677b2f5d2cd3b5327ec"
                                                 "9f6ffc63da7e25ccee9c1133edba2bf8",
                                             ),
                "kernel/core/kernel.cc": (
                                             25384,
                                             "dbc62ff2103e9afcdf92a421820b22c0"
                                             "d4e7b301aa7aee8b052c6154e84b3663",
                                         ),
                "kernel/core/panic.cc": (
                                            9884,
                                            "e384fc5f4fded11fba10d2a1b2b87b70"
                                            "938033e02d980aaaa34fc98ec9450d51",
                                        ),
                "kernel/core/process.cc": (
                                              31904,
                                              "5020e3b4fb2d8a365372c0130e274f1f"
                                              "aef68ff84ce2986bc204a95096c6be09",
                                          ),
                "kernel/core/string.cc": (
                                             13820,
                                             "b748de8c4a5f1f5ab4a15c567686d989"
                                             "86bf62ad6014eba8c703d5053f579a7d",
                                         ),
                "kernel/cpu/fpu.cc": (
                                         6484,
                                         "d732a346a4db31320379bfc7f48d1056"
                                         "bd5a37e164d33588b3c3b7ca9490a622",
                                     ),
                "kernel/cpu/idt.cc": (
                                         8640,
                                         "84629994ef0de9dde25e9476cf9f2cab"
                                         "bbe60ff9a956a2b72697cd0a026e1718",
                                     ),
                "kernel/cpu/irq.cc": (
                                         4112,
                                         "107781a293f7663a2a367fcd66d9cf13"
                                         "620f9e4f35438ae5fcd711d87ebd4310",
                                     ),
                "kernel/cpu/ksyms.cc": (
                                           2368,
                                           "fdfb7d2b1307f2e6019c508ab157bb35"
                                           "61a0907a69774126a7e66f56a7e41371",
                                       ),
                "kernel/cpu/libm.cc": (
                                          16004,
                                          "91cb378447c48e9aac0d94ea858257b2"
                                          "d5c44540ea77a78ed8abfb1ae83bcf36",
                                      ),
                "kernel/cpu/pic.cc": (
                                         2408,
                                         "9e2714a677bb42f159ab6c11e292b688"
                                         "aad4214b65de21863c2968a854ce8ab4",
                                     ),
                "kernel/cpu/simd.cc": (
                                          8008,
                                          "7f63adc1b925f8cf8e0f8c3e202a1a5b"
                                          "e0bbd53fc378e086a849da3495870378",
                                      ),
                "kernel/fs/fat16.cc": (
                                          62120,
                                          "2534959aa756e313aef582b54d2f810c"
                                          "79557d3c14eb17a8b1282ca41d9cb269",
                                      ),
                "kernel/fs/iso9660.cc": (
                                            12284,
                                            "0096dcb2106f8ef91fead6276762b268"
                                            "996297e924aaded6c40db7898ecbc9eb",
                                        ),
                "kernel/fs/loopdev.cc": (
                                            3172,
                                            "52e1cd89fba510352c2dc1bd2b8b92e6"
                                            "b78a4cf0131410a0ef41c7aa087d68c6",
                                        ),
                "kernel/gfx/deflate.cc": (
                                             9808,
                                             "6ab5a09d0316ef7c506995d9106cb3af"
                                             "a9ca075843a3f4e79233e888f187158e",
                                         ),
                "kernel/gfx/gfx2d.cc": (
                                           166388,
                                           "3362e5c410cda9c6bf4b0ad002beb87b"
                                           "6c77f670fb70b7d7336b69db2fe5f6b6",
                                       ),
                "kernel/gfx/glyph_raster.cc": (
                                                  10500,
                                                  "0d774afbdb3cadec549b8bec39baa1f0"
                                                  "3092dbe21e0e6940abcd97a37de25dc2",
                                              ),
                "kernel/gfx/jpeg.cc": (
                                          19368,
                                          "0161427ed5ec067c6d1bbf9ca71a8ddf"
                                          "ec58c2d24a18c4563b4f0f0a87fedfdc",
                                      ),
                "kernel/gfx/png.cc": (
                                         21836,
                                         "733b2e911260d29f55a0fcba743fbd23"
                                         "8078b133ded6173101aadc25bdd56153",
                                     ),
                "kernel/gui/ed.cc": (
                                        52172,
                                        "f8e1864ac3fa6c60ea7f67b3512433aa"
                                        "f9371ea789408f03b35a3bf74f6f755f",
                                    ),
                "kernel/lang/as.cc": (
                                         160204,
                                         "57134084ef66459357c08f9a6c2f4389"
                                         "8024aad2c380f3648e55e701e9e5f6ea",
                                     ),
                "kernel/lang/cupidc.cc": (
                                             307048,
                                             '9ebd46a503fe3db8a58fdbaa0f4d0448'
                                             '2368705a473d773d29f32591357f104f',
                                         ),
                "kernel/lang/cupidc_lex.cc": (
                                                 48324,
                                                 "ff6916ae15c2b9f710273c0d67269557"
                                                 "3238220187b68fe14077f9df6df136d4",
                                             ),
                "kernel/lang/cupidc_parse.cc": (
                                                   487140,
                                                   '70615a3ff05fe34ad1f5340d0f09b4c8'
                                                   '403970735ad39e5854caee721a6a694c',
                                               ),
                "kernel/lang/cupidc_string.cc": (
                                                    6824,
                                                    "e10f067a4bf7e818a29a32a7e46b22a5"
                                                    "29416df24cb1995909f9ca187b863334",
                                                ),
                "kernel/lang/ssh_io.cc": (
                                             11452,
                                             "f45e0f2e8ba05194c456f14d215400b6"
                                             "a5eab7228b5979821d9c32d578d23e6f",
                                         ),
                "kernel/mm/memory.cc": (
                                           17380,
                                           "3e6b77ec20a004b2cdb23ddbd76f8bb9"
                                           "4c8ebec3007b3f159e624caa3519dae7",
                                       ),
                "kernel/mm/paging.cc": (
                                           2180,
                                           "9cae0d7bbbef42caaf41381ef03f206d"
                                           "fd55f58411b4afd3b3a4fe3b71da8238",
                                       ),
                "kernel/network/sshd.cc": (
                                              45960,
                                              "4606e34fe4b954d5de10ba571ce41c3b"
                                              "bb2699ce14c655044795d7cba173b0b9",
                                          ),
                "kernel/network/udp.cc": (
                                             2972,
                                             "4809550519e130cc932233e5e9703a97"
                                             "60b8dc6ef1593f4cd83ab2678d1e9a30",
                                         ),
                "kernel/smp/bkl.cc": (
                                         3024,
                                         "df27603bbf7764d84460f8b575ae15ce"
                                         "9cab461692b426ccd2b7cb947b54bd17",
                                     ),
                "kernel/smp/lapic.cc": (
                                           4128,
                                           "989ee78669365521b35f238cf850c715"
                                           "93e48b245a021c483726709c30e9df79",
                                       ),
                "kernel/tls/tls_ca_bundle.cc": (
                    388,
                    'f94fe7c44ba8fbb94df7ef97f8e37c6ddb0155eba143c07d154803a2c9171ec2',
                ),
            }
            self.assertEqual(
                {
                    source: object_records[source]
                    for source in SOURCE_DRIVEN_SOURCES
                },
                source_driven_object_records,
            )
            self.assertEqual(manifest["input_snapshot"]["count"], 478)
            self.assertEqual(
                manifest["input_snapshot"]["sha256"],
                'e800a1c182dc676724cab4ceb4bd629c'
                '2e6124a8e98270625b3e2498e78b1e11',
            )
            self.assertEqual(
                manifest["provenance"]["compiler"],
                {
                    "mode": "checked-seed",
                    "sha256": hashlib.sha256(seed.read_bytes()).hexdigest(),
                    "size": seed.stat().st_size,
                    "seed_manifest_sha256": hashlib.sha256(
                        SEED_MANIFEST.read_bytes()
                    ).hexdigest(),
                },
            )


if __name__ == "__main__":
    unittest.main()

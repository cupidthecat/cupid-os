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
    / "i386-linux"
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

    def test_windows_seed_is_staged_in_wsl_tmp_and_removed(self):
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
                with frontier._default_seed_execution(root) as execution:
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
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
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
                4162872,
            )
            object_records = {
                entry["source"]: (entry["size"], entry["object_sha256"])
                for entry in manifest["sources"]
            }
            self.assertEqual(
                object_records["kernel/smp/acpi.cc"],
                (
                    5360,
                    'eb0212ae698355c1fe095d211e0ec5dfd71732af14ecff3355c683a341427a33',
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/mp_tables.cc"],
                (
                    3960,
                    '5f3b28380424d152a670eca91542dba97325c55b12f7ae596d0d3d58fb2fba24',
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/percpu.cc"],
                (
                    6632,
                    '4d7f63e37ecb48fe526f5c61505e9db17bb83fd19ba47946368e8272ca4af716',
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/smp.cc"],
                (
                    8032,
                    'b40925edb03f01f7cc03226ea4594f6f85b938319de2bfff9a304e93c706279a',
                ),
            )
            self.assertEqual(
                object_records["drivers/e1000.cc"],
                (
                    8160,
                    'd217206b962485df43ee05247f1d0563ef6b3124374af9bea8a3c2815f569c8a',
                ),
            )
            self.assertEqual(
                object_records["kernel/gui/desktop.cc"],
                (
                    111152,
                    'bd9ddaaa0820b98b119ce7dc026ba84f8a785db8ab9901801e6de294d06bb083',
                ),
            )
            self.assertEqual(
                object_records["kernel/network/socket.cc"],
                (
                    11552,
                    '2983188467cdf757f61dfac61269fa879a9fbe52b06aa405b567528830efe91b',
                ),
            )
            self.assertEqual(
                object_records["kernel/network/tcp.cc"],
                (
                    18356,
                    '084475f4d09525d91c737d855819e2d30d7b101ef2f04a4027644234e4ced448',
                ),
            )
            self.assertEqual(
                object_records["toolchain/x86.cc"],
                (
                    136108,
                    'dff96083877c54ac939b240e372dd12fcd1e58e49fd1332290e81ebd827998a1',
                ),
            )
            port_io_object_records = {
                "drivers/ata.cc": (
                    10536,
                    '9062a30bdf6e23d7f335b4fb2df374821a52e2f9e068e1ec297bb1788d946461',
                ),
                "drivers/keyboard.cc": (
                    11480,
                    'e5e0ced46569e51fd83004e66a4547605efa0da1679ef3a0fe1534c424e45ffe',
                ),
                "drivers/mouse.cc": (
                    12580,
                    'f54981f2ff5bfc76524e428dc95316782e204a13bab9db52d78ccf46662d777e',
                ),
                "drivers/pci.cc": (
                    6880,
                    '3b1bc8084d162651adb943d69dd8dcde8dda1499cc535c96ae188ab57d00656c',
                ),
                "drivers/pit.cc": (
                    1768,
                    'affa6d8150e05e0cedeb93a0312f268b5f4de86a42e1d09ef72bfc7491cb6cf7',
                ),
                "drivers/rtc.cc": (
                    7356,
                    '056e63838c5562473a43386872a47b98ba87c83d67e6ac33d7ef79ebe8c6b49a',
                ),
                "drivers/rtl8139.cc": (
                    7944,
                    'e23a582d1bba3de464ec2b470048d0ee714eed6220c971eaae0f454196b11af7',
                ),
                "drivers/speaker.cc": (
                    1560,
                    '278d8be88e17ae2300dcc17277ea2202a9a4699ce7881b9752b7a19e8fde281f',
                ),
                "drivers/vga.cc": (
                    4572,
                    '082bf6721f997f378c6fe8630fdd77602b9a8f15324b7c5e839fc466489a8716',
                ),
                "kernel/audio/ac97.cc": (
                    13716,
                    '90f5d376a6eab5480685e2454ad1e40737f27aaa87d6e05df1fe6b1779df3cbc',
                ),
                "kernel/core/syscall.cc": (
                    12212,
                    'e6a527342f185be8a5a76cce8cd8776b187b1191ab30992ba079daf611e3e4ea',
                ),
                "kernel/lang/shell.cc": (
                    166276,
                    '7d597199c9c0cae758a1a8c800abd3da6bfa74e61074db2d03c1cc38ccdcfe44',
                ),
                "kernel/usb/ehci.cc": (
                    21084,
                    '094d54f42bd3166c1d2ea4d4e28035d3387877e8e7fb17323eff92c187df4047',
                ),
                "kernel/usb/uhci.cc": (
                    17452,
                    '57dd3b97447d0d4ce1acb2da685c0101dc547cc9f3156dd55681d66671def12d',
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
                    19980,
                    'c23179bbd79178fb8350d722ba24266037e74a8b78e65645bee18e71fa989cd0',
                ),
                "drivers/timer.cc": (
                    6260,
                    'b436edb83d1869f7d618d706bb9328c7f559bac91b39436536a49cb710df052f',
                ),
                "kernel/audio/nuked_opl3.cc": (
                    37984,
                    'e2cdeba4b807596d5dacfd0c783fe797e035c64516e82e4d416f34775c116dc5',
                ),
                "kernel/core/app_launch.cc": (
                    5312,
                    '56bc4f681a7e479ea9c26bcd854c95a6024cc240048669b1272b04a7acecce5f',
                ),
                "kernel/core/kernel.cc": (
                    25384,
                    '867a199a12dce7743db38aebee1cf748bc59615735e7a37ea80900e4df8a6c35',
                ),
                "kernel/core/panic.cc": (
                    9900,
                    'c19fedd77e8c986eb37755f0c5be8cb31571a7d05f5aeeec36c1bda7614a5ba7',
                ),
                "kernel/core/process.cc": (
                    31904,
                    'bec2e8f5eea28e7612e0c28f4d5ca76474c47d83086db9fa7290c5db04e6252d',
                ),
                "kernel/core/string.cc": (
                    13820,
                    '34e1143fbde2aae8eed602aa1b69fcce608ec35d6f56e226600663c514ffc280',
                ),
                "kernel/cpu/fpu.cc": (
                    6484,
                    'd365c3b6201266cbdafcb3872e4adbf453f1778458754047076f11917f2b3afb',
                ),
                "kernel/cpu/idt.cc": (
                    8640,
                    '43c5dff872298c574b11979ab422c11c9136ecd83796ba48bc0df95deec005eb',
                ),
                "kernel/cpu/irq.cc": (
                    4112,
                    '47355288dcef1cd0a8f4d07303a5d7bd23ca12b3bb133999685c4f8d15ae2d93',
                ),
                "kernel/cpu/ksyms.cc": (
                    2368,
                    '67a34475226fc81680583d7e359c6e7e27893977b784c133a808e7db374de583',
                ),
                "kernel/cpu/libm.cc": (
                    16004,
                    '82655ff4fba8ed73413d316fb959af403627f76b0f25810a3c5b326957718dc8',
                ),
                "kernel/cpu/pic.cc": (
                    2408,
                    'c1855a19e0cd285953996344493dcefe916f06d89fed706219718920b4d2ea5d',
                ),
                "kernel/cpu/simd.cc": (
                    8008,
                    '42ce37cb0ba0c5b3e52262139497e285385c626f741adf0f67e73501899ad230',
                ),
                "kernel/fs/fat16.cc": (
                    62120,
                    'aeb20c926671367f5bb403294d1b12fd52909eb6d0b54da85bff28d8320657ce',
                ),
                "kernel/fs/iso9660.cc": (
                    12284,
                    'cb9aa916f133ed396bd242b0e5e88158560862f50177a69daaaf1a6312bf592c',
                ),
                "kernel/fs/loopdev.cc": (
                    3172,
                    '0d80399a7a3396822e004dbd8ad6a27aa976bbeee410218afcada663949c8be7',
                ),
                "kernel/gfx/deflate.cc": (
                    9808,
                    '026a44cf8df0c431d16b486a5e2e1afee063965e20e9fccede1901ff4fd533e5',
                ),
                "kernel/gfx/gfx2d.cc": (
                    166388,
                    '841e23a7f7473efbfdb9204f505ed4f514943fddc18542ddcb398f5624f1a07b',
                ),
                "kernel/gfx/glyph_raster.cc": (
                    10500,
                    '93290146189a0bf0ef2b082e5b4f64384ef73c4e6096bf1941624a0e37d3b969',
                ),
                "kernel/gfx/jpeg.cc": (
                    19368,
                    '480e085c8d9b587525069a5cf58acdc1637f6bc04aef4aa87b481a31a5f937a8',
                ),
                "kernel/gfx/png.cc": (
                    21836,
                    '91e66593bc1e3d151cf0b7ca54440ef3f49e448a5405bab89529e06d7fdd05b9',
                ),
                "kernel/gui/ed.cc": (
                    52172,
                    '8334ca1c1924317b2efb961e3151360129fa0e0a65ea3eaafafd3a62273ff577',
                ),
                "kernel/lang/as.cc": (
                    160204,
                    '501c06b85dee4a74d0eeafa8dca66790598d0f0f8f012e938084895ab7b3a8c2',
                ),
                "kernel/lang/cupidc.cc": (
                    272652,
                    '8456d83d3093fd8251379dd77dabef2ff73811b141c4708c49fa4612881c5cbd',
                ),
                "kernel/lang/cupidc_lex.cc": (
                    48324,
                    '98b8b4040214afa3b0f2ba6ac6a86c4a6167ae2fbb6dcf621b6dbb148057cbb7',
                ),
                "kernel/lang/cupidc_parse.cc": (
                    474052,
                    '3cf105b8bd98a1f77fbfbd520eb88410d25bccd36541f7a1cab72ee3ca6b4cb4',
                ),
                "kernel/lang/cupidc_string.cc": (
                    6824,
                    'b14eec63b6a9f1ae0710a0b613e51fbff168431b8bab22424202ea1aa392c5ae',
                ),
                "kernel/lang/ssh_io.cc": (
                    11452,
                    '0f5de2a005763516bebe5a16b782494ab5b6337d393711c0c992d4fb5efd4e81',
                ),
                "kernel/mm/memory.cc": (
                    17380,
                    '88ca033bb7dcc521f4a3797d03f87d1bb041d0356573063382409539a7cfaa46',
                ),
                "kernel/mm/paging.cc": (
                    2180,
                    '266fbe5367bd9a114b4cfdb8a6ee4ac6fdf5265281b625b710dbe1ba3b7baa8a',
                ),
                "kernel/network/sshd.cc": (
                    45960,
                    '73c33acc4a4852b7fd8815256ae696f3cd1d9f03094437fcb2d01317a4a60418',
                ),
                "kernel/network/udp.cc": (
                    2972,
                    '280b70b728ece9b0dcf12c2ef07f9057354173724a39a34a8e6b8632a8ef3023',
                ),
                "kernel/smp/bkl.cc": (
                    3024,
                    '0b9943852ccebb12753528e84e61bef5e4b60b2f4fe565d98a4e018b07046e4c',
                ),
                "kernel/smp/lapic.cc": (
                    4128,
                    '4c76e7cb0382d2474299f13ce85307dc258d3f12a001c6e1e596fc6a679048b2',
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
                "d3b96db6b47e6618d4faeaae621149f0"
                "f30a64695451fe62be2f3b500b599608",
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

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
]
SMP_SOURCES = [
    "kernel/smp/acpi.c",
    "kernel/smp/mp_tables.c",
]
OPERAND_FREE_SOURCES = [
    "drivers/e1000.c",
    "kernel/gui/desktop.c",
    "kernel/network/socket.c",
    "kernel/network/tcp.c",
]
PORT_IO_SOURCES = [
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
]
COMPILER_READY_SOURCES = [
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
]
TOOLCHAIN_KERNEL_SOURCES = [
    "toolchain/ctool.c",
    "toolchain/cupidasm.c",
    "toolchain/cupiddis.c",
    "toolchain/elf32.c",
    "toolchain/x86.c",
]
SOURCE_DRIVEN_SOURCES = [
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
                        "source_sha256": hashlib.sha256(
                            (root / source).read_bytes()
                        ).hexdigest(),
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
                    hashlib.sha256((root / source).read_bytes()).hexdigest(),
                )

    def test_missing_approved_smp_source_is_rejected_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in KERNEL_SOURCES:
                path = root / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("int source_fixture;\n", encoding="utf-8")
            (root / "kernel" / "smp" / "acpi.c").unlink()
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
                "kernel/smp/acpi.c",
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
            (root / "drivers" / "ata.c").unlink()
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
                "approved kernel source is not a file: drivers/ata.c",
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
                "drivers/ata.c produced invalid ELF32: "
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
                "drivers/ata.c produced invalid ELF32: "
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
                "drivers/ata.c produced invalid ELF32: "
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
                "drivers/ata.c produced invalid ELF32: "
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
                "drivers/ata.c produced invalid ELF32: "
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
                "drivers/ata.c produced invalid ELF32: "
                "emitted object relocation section 2 uses RELA",
                result.stderr,
            )

    def test_relocation_addend_outside_cupidc_contract_is_rejected(self):
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

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "drivers/ata.c produced invalid ELF32: "
                "absolute relocation addend is 4, expected 0",
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
                        'if source == "/kernel/smp/acpi.c":\n'
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
                "kernel/smp/acpi.c did not compile: "
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
                        'if source == "/kernel/usb/uhci.c":\n'
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
                "kernel/usb/uhci.c did not compile: forced late failure",
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
                        'if source == "/kernel/smp/acpi.c":\n'
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
                "kernel/smp/acpi.c object output is not deterministic",
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
                        'if source == "/kernel/lang/shell.c":\n'
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
                "kernel/lang/shell.c object output is not deterministic",
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
                "drivers/ata.c did not publish an object",
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
                        '(root / "kernel/crypto/aes.c").write_text('
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
                "kernel/crypto/aes.c",
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
                3514456,
            )
            object_records = {
                entry["source"]: (entry["size"], entry["object_sha256"])
                for entry in manifest["sources"]
            }
            self.assertEqual(
                object_records["kernel/smp/acpi.c"],
                (
                    5708,
                    "0e32026db8af4d22ad9007c1900df16bee2bca342187a797dc12f154f340b1d5",
                ),
            )
            self.assertEqual(
                object_records["kernel/smp/mp_tables.c"],
                (
                    4156,
                    "37791cc5ab28b93e92553735a2c8380d539f9473529e3f8d5731859c37358960",
                ),
            )
            self.assertEqual(
                object_records["drivers/e1000.c"],
                (
                    8780,
                    "38e896c6b1d0359c858a7601d6c0b692"
                    "786b9ff439d78c933fdde7af2d07d875",
                ),
            )
            self.assertEqual(
                object_records["kernel/gui/desktop.c"],
                (
                    111196,
                    "f6f0edc79419ebd8ecfaf9254a17dfb8"
                    "fe8b6cc7139bf16f872c0ce0a8fba340",
                ),
            )
            self.assertEqual(
                object_records["kernel/network/socket.c"],
                (
                    12416,
                    "dff17d1b2e668f577aab6d45ef341a22"
                    "6ebaf7ae7278c5c8a2d0aafcd0346ee5",
                ),
            )
            self.assertEqual(
                object_records["kernel/network/tcp.c"],
                (
                    20204,
                    "831f2a82687ab327f4b48b28fef69104"
                    "cc94af0770dc6caf7b8a8df5b87a7368",
                ),
            )
            port_io_object_records = {
                "drivers/ata.c": (
                    10748,
                    "7675b2eaf6aca4ae022b53943887a6fc"
                    "5d419a41a6dd2af3300f6265fd501575",
                ),
                "drivers/keyboard.c": (
                    11740,
                    "0703723bd6aecb968fd011d8921cf8595"
                    "eff10d2e8d30b9dd5c68c74f85e6daa",
                ),
                "drivers/mouse.c": (
                    12936,
                    "0fc5292e291cd8ff0403cda1948029cb8"
                    "f1e92051e04f882e8935dc371f330d8",
                ),
                "drivers/pci.c": (
                    7136,
                    "7d006772700b8b0192daa7690417bc687"
                    "2b8324588cd67e50126cf318858a68e",
                ),
                "drivers/pit.c": (
                    1816,
                    "988d4678c3ca72ee706192c22138dbe3"
                    "a899d70d7ce059eaed5c613e2ca77b53",
                ),
                "drivers/rtc.c": (
                    7520,
                    "e4e81e276d1fc15c04f3b56ace981647"
                    "9af6e8c4c3a4f3b1a38b2a137766ef4a",
                ),
                "drivers/rtl8139.c": (
                    8416,
                    "0244bebe07cbaf334725e28a4b23963cb"
                    "bd7cff409b53f6be209557b9561157a",
                ),
                "drivers/speaker.c": (
                    1576,
                    "f880fc8db95090e040596589725c0935"
                    "384da2387ba45661cb25657337bc55fa",
                ),
                "drivers/vga.c": (
                    4764,
                    "ab0ffd587b4e4ea473f161957d53255c"
                    "bb755a401c176cf205eee571d8969840",
                ),
                "kernel/audio/ac97.c": (
                    14100,
                    "35b1cb43e884a581f5560419b640b5dc"
                    "811dd0c91ff7d70b19d4020db403aa07",
                ),
                "kernel/core/syscall.c": (
                    12572,
                    "c2e30823de92cdd54dd849763dc37d81"
                    "fdd72e64326cef807bf825725096a5aa",
                ),
                "kernel/lang/shell.c": (
                    175056,
                    "6f608e0ab1abefa467949f18b3731e31"
                    "a9f46fbdda73f691dcfddf941c1c1559",
                ),
                "kernel/usb/ehci.c": (
                    22820,
                    "f56b0adb33a676b28d16317a16fb3725"
                    "44ab6db0ee0c0c63b23e10b54534d610",
                ),
                "kernel/usb/uhci.c": (
                    18576,
                    "bfdade6cbc6210796e7b579cda617fb1b"
                    "00eca2c41df6a9ef9b4a5200bb6940f",
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
                    20776,
                    "7b049fdc9ef79fee075e5903fa9846635"
                    "6d089135665591249194b5b81690bbd",
                ),
                "drivers/timer.cc": (
                    6404,
                    "af2c13c68060bfa71a2e001722837ba1"
                    "64c803de8864acfa6d468d7cedc2e3da",
                ),
                "kernel/core/app_launch.cc": (
                    5488,
                    "242aa3d0d14d70f6096fd64d3cff4a52"
                    "a148b3caa660190c1024da0b0f6b1e9f",
                ),
                "kernel/core/panic.cc": (
                    10212,
                    "84daa51a65d6970ae7a7918b05fe64b7"
                    "676c39d3309264375e349cf0ae20d428",
                ),
                "kernel/core/process.cc": (
                    30216,
                    "819e6e712cdb08d3b1b112fcc42122a1"
                    "aa5802b19c0cac8c1a3edbc0bca620d4",
                ),
                "kernel/cpu/idt.cc": (
                    8756,
                    "0ad16fd3250bc09ced7c928cb287123db"
                    "245980de73c15f0249db71a2f2f6ea3",
                ),
                "kernel/cpu/irq.cc": (
                    4308,
                    "96356f3cfa63bbef6acf5a352e0ce89e"
                    "d0f9b6a92174b66206f0eef7ae684ade",
                ),
                "kernel/cpu/ksyms.cc": (
                    2620,
                    "5d64f392df38b6730ff30c3099c3bdfd"
                    "96bcc84d90740e20fd263c1ad7c94389",
                ),
                "kernel/cpu/pic.cc": (
                    2408,
                    "c1855a19e0cd285953996344493dcefe9"
                    "16f06d89fed706219718920b4d2ea5d",
                ),
                "kernel/fs/fat16.cc": (
                    52084,
                    "bee3f404245e61d92e8d6de500d0d1ef"
                    "0b94a488b18dc7fba5b9b40ee21448c8",
                ),
                "kernel/fs/iso9660.cc": (
                    13444,
                    "e55e4612db707b47b5312685bffa4ac4"
                    "573b96f905c7887957551bf7495b679e",
                ),
                "kernel/fs/loopdev.cc": (
                    3456,
                    "cc53a4983a96d3b4a65931beec83c092"
                    "66fe46e596f6b84f14a0205a40d3809d",
                ),
                "kernel/gfx/deflate.cc": (
                    10640,
                    "74d24784f3ea32e0523de68b3c6fc0d3"
                    "bd63e80df05402d7b6fa4a85f891fee8",
                ),
                "kernel/gfx/gfx2d.cc": (
                    169580,
                    "b7f700414da5687ca40935a1d1ebf01e"
                    "acfad0237e555800ad9a1fbe392fbcb6",
                ),
                "kernel/gfx/png.cc": (
                    24128,
                    "1823778061d7eb76c9f6ef03aadd7862"
                    "d4207a08fa5ab8731e58a84cd0dd84be",
                ),
                "kernel/gui/ed.cc": (
                    54992,
                    "7bc80e9e9371d8827ce71502df104d67"
                    "9e47b20300c1a45969845aefdaabedf6",
                ),
                "kernel/lang/as.cc": (
                    148056,
                    "f05ffb741a81403f3bfb86358b3f9601"
                    "1b2ddef65c87e291f582c1d77b0cedfd",
                ),
                "kernel/lang/cupidc.cc": (
                    288180,
                    "4e8501e628a770b346bbe16e23d9549c"
                    "4320f1f01f0ddcb9309b907a8c898046",
                ),
                "kernel/lang/cupidc_parse.cc": (
                    290084,
                    "f5856f1ae536c7be8daadf913bd54a94"
                    "9dd2b976e62fe1779e8e832453210375",
                ),
                "kernel/lang/cupidc_string.cc": (
                    7332,
                    "24533c70791b3c9b8f1f0d47bc5c7639"
                    "af67e300c59eac4abbe1c9fa747598f2",
                ),
                "kernel/lang/ssh_io.cc": (
                    12152,
                    "f1bd3163beed6a1dca210ca228a12e1a"
                    "f37223487f22ab2154701263654b464d",
                ),
                "kernel/mm/memory.cc": (
                    18364,
                    "7e0dc352c315dba8250bf7ae9b126c27"
                    "2e0b98f2cf0bf7429a6d7675d82636c1",
                ),
                "kernel/mm/paging.cc": (
                    2336,
                    "fc9b757a35cf474f90436333ba732be2"
                    "52253feeea531cad851215e17f793e2d",
                ),
                "kernel/network/sshd.cc": (
                    48936,
                    "319919ad47d1346aa2a2450f75dd4092"
                    "f25165b4cfe569408442a4ad981509d3",
                ),
                "kernel/network/udp.cc": (
                    3188,
                    "9ec0a805ed7bdc271a49dce88f335aa1"
                    "80588b910da075ef4cf8cf95d1efa726",
                ),
                "kernel/smp/bkl.cc": (
                    3128,
                    "254793a6970f466cf4b3d55a98e907d1"
                    "a68649a9b6dc736edadc5697bd316fd3",
                ),
                "kernel/smp/lapic.cc": (
                    4184,
                    "6ce344d265ad3fb6b221a9159d860954"
                    "c5f5512a7eac526838e69bc181a4c045",
                ),
                "kernel/tls/tls_ca_bundle.cc": (
                    388,
                    "f94fe7c44ba8fbb94df7ef97f8e37c6d"
                    "db0155eba143c07d154803a2c9171ec2",
                ),
            }
            self.assertEqual(
                {
                    source: object_records[source]
                    for source in SOURCE_DRIVEN_SOURCES
                },
                source_driven_object_records,
            )
            self.assertEqual(manifest["input_snapshot"]["count"], 432)
            self.assertEqual(
                manifest["input_snapshot"]["sha256"],
                "7670679039ca8f2b9b7816a68cb9b391"
                "d8a2e65f6b03a7a043d35005b75283bf",
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

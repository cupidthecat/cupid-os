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

from tools import kernel_crypto_frontier as frontier


REPO_ROOT = Path(__file__).resolve().parents[1]
FRONTIER_TOOL = REPO_ROOT / "tools" / "kernel_crypto_frontier.py"
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
    "kernel/crypto/bigint.c",
    "kernel/crypto/chacha20.c",
    "kernel/crypto/chacha20poly1305.c",
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
]

BOUNDARY_DIAGNOSTICS = {
    "kernel/crypto/csprng.c": (
        29,
        "CTB00000F",
        "GNU inline assembly is outside this function-body slice",
    ),
    "kernel/crypto/asn1.c": (
        102,
        "CTD000006",
        "CupidC IR lowering does not yet support this conversion",
    ),
    "kernel/crypto/x509.c": (
        120,
        "CTD000006",
        "CupidC IR lowering does not yet support this conversion",
    ),
    "kernel/crypto/x509_chain.c": (
        96,
        "CTD000006",
        "CupidC IR lowering does not yet support this conversion",
    ),
}

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

            BOUNDARIES = {
                "/kernel/crypto/csprng.c": (
                    29,
                    "CTB00000F",
                    "GNU inline assembly is outside this function-body slice",
                ),
                "/kernel/crypto/asn1.c": (
                    102,
                    "CTD000006",
                    "CupidC IR lowering does not yet support this conversion",
                ),
                "/kernel/crypto/x509.c": (
                    120,
                    "CTD000006",
                    "CupidC IR lowering does not yet support this conversion",
                ),
                "/kernel/crypto/x509_chain.c": (
                    96,
                    "CTD000006",
                    "CupidC IR lowering does not yet support this conversion",
                ),
            }

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

            case "$source" in
                /kernel/crypto/csprng.c)
                    printf '%s\\n' "$source:29:1: error CTB00000F: GNU inline assembly is outside this function-body slice" >&2
                    exit 1
                    ;;
                /kernel/crypto/asn1.c)
                    printf '%s\\n' "$source:102:1: error CTD000006: CupidC IR lowering does not yet support this conversion" >&2
                    exit 1
                    ;;
                /kernel/crypto/x509.c)
                    printf '%s\\n' "$source:120:1: error CTD000006: CupidC IR lowering does not yet support this conversion" >&2
                    exit 1
                    ;;
                /kernel/crypto/x509_chain.c)
                    printf '%s\\n' "$source:96:1: error CTD000006: CupidC IR lowering does not yet support this conversion" >&2
                    exit 1
                    ;;
            esac

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

    def test_missing_required_section_is_rejected(self):
        malformed = bytearray(_valid_elf32_object())
        section_offset = struct.unpack_from("<I", malformed, 32)[0]
        struct.pack_into("<I", malformed, section_offset + 40, 0)

        with self.assertRaisesRegex(
            frontier.FrontierError,
            r"missing required section \.text",
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
    def test_cli_freezes_an_explicit_portable_compiler(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel crypto frontier: ok (16 sources, 4 boundaries)\n",
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


class KernelCryptoFrontierCliTests(unittest.TestCase):
    def test_output_path_must_be_a_directory(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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

    def test_exact_crypto_cohort_compiles_twice_with_matching_objects(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel crypto frontier: ok (16 sources, 4 boundaries)\n",
            )
            self.assertEqual(result.stderr, "")

            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                [entry["source"] for entry in manifest["sources"]],
                CRYPTO_SOURCES,
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
            self.assertEqual(manifest["input_snapshot"]["count"], 20)
            self.assertEqual(
                len(manifest["input_snapshot"]["files"]),
                20,
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
            for source in CRYPTO_SOURCES:
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

    def test_truncated_elf32_section_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "emitted object has a truncated section header table",
                result.stderr,
            )

    def test_section_payload_outside_object_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "emitted object section 1 payload is outside the file",
                result.stderr,
            )

    def test_symbol_name_outside_string_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "emitted object symbol 1 has an invalid name",
                result.stderr,
            )

    def test_relocation_symbol_outside_symbol_table_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "emitted object relocation 0 has an invalid symbol",
                result.stderr,
            )

    def test_relocation_type_outside_cupidc_contract_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "emitted object relocation 0 uses unsupported i386 type 42",
                result.stderr,
            )

    def test_explicit_addend_relocation_section_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "emitted object relocation section 2 uses RELA",
                result.stderr,
            )

    def test_relocation_addend_outside_cupidc_contract_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c produced invalid ELF32: "
                "absolute relocation addend is 4, expected 0",
                result.stderr,
            )

    def test_failed_boundary_compile_cannot_publish_an_object(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                        "if source in BOUNDARIES:\n"
                        '    destination = root / output.lstrip("/")\n'
                        "    destination.parent.mkdir("
                        "parents=True, exist_ok=True)\n"
                        '    destination.write_bytes(b"partial")\n'
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
                "kernel/crypto/csprng.c published an object after failure",
                result.stderr,
            )
            self.assertFalse((root / "frontier").exists())

    def test_success_without_an_object_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel/crypto/aes.c did not publish an object",
                result.stderr,
            )
            self.assertFalse((root / "frontier").exists())

    def test_source_drift_stops_without_publishing_a_partial_frontier(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for source in CRYPTO_SOURCES + list(BOUNDARY_DIAGNOSTICS):
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
                "kernel crypto inputs changed during frontier run: "
                "kernel/crypto/aes.c",
                result.stderr,
            )
            self.assertFalse(output.exists())


class RealKernelCryptoFrontierTests(unittest.TestCase):
    def test_checked_seed_compiles_the_complete_approved_cohort(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".kernel-crypto-frontier-test-",
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
                timeout=300,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "kernel crypto frontier: ok (16 sources, 4 boundaries)\n",
            )
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(
                [entry["source"] for entry in manifest["sources"]],
                CRYPTO_SOURCES,
            )
            self.assertEqual(len(manifest["boundaries"]), 4)
            self.assertEqual(
                sum(entry["size"] for entry in manifest["sources"]),
                165112,
            )
            self.assertGreater(manifest["input_snapshot"]["count"], 20)
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

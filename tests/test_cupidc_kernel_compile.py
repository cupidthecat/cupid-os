import contextlib
import io
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
            "tools/kernel_crypto_frontier.py",
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
            for source in CRYPTO_SOURCES
        }
        self.assertEqual(actual, expected)

        for source, output in sorted(expected):
            rule_pattern = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf" \$\(CUPIDC_KERNEL_COMPILE_INPUTS\)"
                rf"\n\t\$\(CUPIDC_KERNEL_COMPILE\) "
                rf"--source {re.escape(source)} "
                rf"--output {re.escape(output)}$",
                re.MULTILINE,
            )
            self.assertRegex(makefile, rule_pattern)

        self.assertIn(
            "kernel/crypto/ecdsa.o: kernel/crypto/ecdsa.c "
            "kernel/crypto/ecdsa.h kernel/crypto/p256.h "
            "kernel/crypto/hmac.h kernel/crypto/sha256.h "
            "kernel/core/string.h kernel/core/types.h "
            "$(CUPIDC_KERNEL_COMPILE_INPUTS)",
            makefile,
        )

        for source in CRYPTO_SOURCES:
            output = str(Path(source).with_suffix(".o")).replace("\\", "/")
            host_rule = re.compile(
                rf"^{re.escape(output)}: [^\n]*"
                rf"\n\t\$\(CC\) ",
                re.MULTILINE,
            )
            self.assertNotRegex(makefile, host_rule)


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
            kernel_compile.compile_kernel_crypto(
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
        source = root / "kernel" / "crypto" / "new_cipher.c"
        source.write_text("int unapproved;\n", encoding="utf-8")
        executor = FakeExecutor(root)

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "source is outside the approved kernel crypto cohort",
        ):
            kernel_compile.compile_kernel_crypto(
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
            kernel_compile.compile_kernel_crypto(
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
                kernel_compile.compile_kernel_crypto(
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
                kernel_compile.compile_kernel_crypto(
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
                kernel_compile.compile_kernel_crypto(
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


class KernelCompileCliTests(unittest.TestCase):
    def test_cli_reports_a_clear_failure(self):
        error = io.StringIO()
        with mock.patch.object(
            kernel_compile,
            "compile_kernel_crypto",
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


if __name__ == "__main__":
    unittest.main()

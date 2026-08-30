import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

from tools.bootstrap_toolchain import (
    _CUPIDBUILD_BOOTLOADER_BEHAVIOR_SOURCE,
    _CUPIDBUILD_SMP_BEHAVIOR_SOURCE,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"
BASELINE_JPEG = (
    b"\xff\xd8"
    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    b"\xff\xd9"
)


class CupidBuildCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidbuild-cli-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli_path = build_path / ("cupidbuild" + suffix)
        cli_target = relative_build.as_posix() + "/cupidbuild" + suffix
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build.as_posix()}",
                cli_target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidBuild hosted CLI build failed\n"
                + result.stdout
                + result.stderr
            )
    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_help_names_every_guarded_assembly_command(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("cupidbuild assemble-cupidasm-object", result.stdout)
        self.assertIn("cupidbuild assemble-bootloader", result.stdout)
        self.assertIn("cupidbuild assemble-smp-trampoline", result.stdout)

    def test_help_names_the_typed_jpeg_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild embed-jpeg --seed-manifest MANIFEST\s+"
            r"--root ROOT --source SOURCE --output OUTPUT",
        )

    def test_help_names_the_typed_kernel_symbol_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild generate-ksyms --seed-manifest MANIFEST\s+"
            r"--root ROOT --source SOURCE --output OUTPUT",
        )

    def test_help_names_the_typed_kernel_flatten_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild flatten-kernel --seed-manifest MANIFEST\s+"
            r"--root ROOT --input-manifest MANIFEST --output OUTPUT",
        )

    def test_typed_kernel_flatten_transaction_matches_checked_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-kernel-flat-success-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            kernel = root / "kernel"
            kernel.mkdir()
            seed_manifest = self._copy_checked_assembly_seed(root / "seed")
            built = self._build_ksyms_elf(
                root,
                "BITS 32\n"
                "global _start:function\n"
                "section .text\n"
                "_start:\n"
                "    mov eax, 0x12345678\n"
                "    ret\n",
            )
            pass_one = kernel / "kernel.elf.pass1"
            linked = kernel / "kernel.elf"
            shutil.copy2(built, pass_one)
            shutil.copy2(built, linked)
            cohort = []
            for index in range(25):
                member = kernel / f"cohort-{index:02d}.elf"
                shutil.copy2(built, member)
                cohort.append(f"kernel/{member.name}")
            manifest = root / "code-inputs.txt"
            manifest.write_text(
                "\n".join(
                    [
                        "kernel/kernel.elf.pass1",
                        "kernel/kernel.elf",
                        *cohort,
                        "",
                    ]
                ),
                encoding="utf-8",
                newline="\n",
            )
            expected = kernel / "expected.bin"
            flattened = subprocess.run(
                [
                    str(self._production_tool("cupidobj")),
                    "flat",
                    str(linked),
                    "-o",
                    str(expected),
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(flattened.returncode, 0, flattened.stderr)
            output = kernel / "kernel.bin"
            before = self._private_roots()

            result = self._run_flatten_kernel(
                root, manifest, output, seed_manifest
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((result.stdout, result.stderr), ("", ""))
            self.assertEqual(output.read_bytes(), expected.read_bytes())
            self.assertEqual(self._private_roots(), before)

    def test_typed_kernel_flatten_rejects_a_malformed_input_manifest(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-kernel-flat-manifest-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            kernel = root / "kernel"
            kernel.mkdir()
            seed_manifest = self._copy_checked_assembly_seed(root / "seed")
            manifest = root / "code-inputs.txt"
            manifest.write_bytes(b"kernel/kernel.elf")
            output = kernel / "kernel.bin"
            output.write_bytes(b"last known good flat kernel")

            result = self._run_flatten_kernel(
                root, manifest, output, seed_manifest
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must end with a newline", result.stderr)
            self.assertEqual(
                output.read_bytes(), b"last known good flat kernel"
            )

    def test_typed_kernel_flatten_rejects_unsafe_manifest_forms(self):
        cases = (
            ("empty", b"", "may not be empty"),
            ("crlf", b"kernel/kernel.elf\r\n", "must use LF newlines"),
            ("blank", b"kernel/a.o\n\n", "line is blank"),
            ("comment", b"#comment\n", "may not be a comment"),
            ("backslash", b"kernel\\a.o\n", "must use forward slashes"),
            ("whitespace", b"kernel/a file.o\n", "may not contain whitespace"),
            ("absolute", b"/kernel/a.o\n", "not canonical and relative"),
            ("traversal", b"kernel/../a.o\n", "not canonical and relative"),
            ("dot", b"kernel/./a.o\n", "not canonical and relative"),
            (
                "case-collision",
                b"kernel/A.o\nkernel/a.o\n",
                "listed more than once",
            ),
            (
                "too-many",
                b"".join(
                    f"kernel/member-{index:03d}.o\n".encode("ascii")
                    for index in range(501)
                ),
                "exceeds the 500-input limit",
            ),
        )
        for name, payload, message in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f".cupidbuild-kernel-flat-{name}-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                kernel = root / "kernel"
                kernel.mkdir()
                seed_manifest = self._copy_checked_assembly_seed(root / "seed")
                manifest = root / "code-inputs.txt"
                manifest.write_bytes(payload)
                output = kernel / "kernel.bin"
                sentinel = b"last known good flat kernel"
                output.write_bytes(sentinel)

                result = self._run_flatten_kernel(
                    root, manifest, output, seed_manifest
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)
                self.assertEqual(output.read_bytes(), sentinel)

    def test_typed_kernel_flatten_requires_both_linked_kernel_inputs(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-kernel-flat-linked-pair-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            kernel = root / "kernel"
            kernel.mkdir()
            seed_manifest = self._copy_checked_assembly_seed(root / "seed")
            (kernel / "kernel.elf.pass1").write_bytes(b"frozen pass one\n")
            manifest = root / "code-inputs.txt"
            manifest.write_text(
                "kernel/kernel.elf.pass1\n", encoding="ascii", newline="\n"
            )
            output = kernel / "kernel.bin"
            sentinel = b"last known good flat kernel"
            output.write_bytes(sentinel)

            result = self._run_flatten_kernel(
                root, manifest, output, seed_manifest
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must include both linked kernels", result.stderr)
            self.assertEqual(output.read_bytes(), sentinel)

    def test_typed_kernel_flatten_rejects_an_invalid_linked_kernel(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-kernel-flat-elf-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            kernel = root / "kernel"
            kernel.mkdir()
            seed_manifest = self._copy_checked_assembly_seed(root / "seed")
            built = self._build_ksyms_elf(
                root,
                "BITS 32\n"
                "global _start:function\n"
                "section .text\n"
                "_start:\n"
                "    ret\n",
            )
            shutil.copy2(built, kernel / "kernel.elf.pass1")
            (kernel / "kernel.elf").write_bytes(b"not an ELF image\n")
            manifest = root / "code-inputs.txt"
            manifest.write_text(
                "kernel/kernel.elf.pass1\nkernel/kernel.elf\n",
                encoding="utf-8",
                newline="\n",
            )
            output = kernel / "kernel.bin"
            output.write_bytes(b"last known good flat kernel")

            result = self._run_flatten_kernel(
                root, manifest, output, seed_manifest
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked CupidDis failed", result.stderr)
            self.assertEqual(
                output.read_bytes(), b"last known good flat kernel"
            )

    def test_typed_kernel_flatten_keeps_an_independent_byte_parity_gate(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild.cc").read_text(encoding="utf-8")
        operation = source.split(
            "int cupidbuild_flatten_kernel(", 1
        )[1].split("int cupidbuild_run_checked_tool(", 1)[0]

        independent_render = operation.index("cupidbuild_render_flat_image(")
        object_transform = operation.index(
            "status = cupidbuild_host_run_in_private(\n      transaction, seed.frozen_tools[4]"
        )
        parity = operation.index("memcmp(candidate, expected, expected_size)")

        self.assertLess(independent_render, object_transform)
        self.assertLess(object_transform, parity)
        self.assertEqual(operation.count("cupidbuild_render_flat_image("), 1)
        self.assertEqual(operation.count("cupidbuild_host_run_in_private("), 3)
        self.assertIn(
            "disassembler_arguments[input_count + 1u] = (const char *)0;",
            operation,
        )
        self.assertIn("disassembler_arguments, 300000u", operation)
        self.assertIn("linked_arguments, 600000u", operation)
        self.assertNotIn("CUPIDBUILD_CODE_BATCH", source)

    def test_typed_kernel_symbol_transaction_matches_checked_tools(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-ksyms-success-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            elf = self._build_ksyms_elf(
                root,
                "BITS 32\n"
                "global _start:function\n"
                "global same_address:function\n"
                "global helper:function\n"
                "section .text\n"
                "_start:\n"
                "same_address:\n"
                "    call helper\n"
                "    ret\n"
                "helper:\n"
                "    ret\n",
            )
            symbols = root / "kernel.symbols"
            expected = root / "expected.cc"
            output = root / "ksyms_data.cc"
            inspected = subprocess.run(
                [str(self._production_tool("cupiddis")), "-n", elf.name],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(inspected.returncode, 0, inspected.stderr)
            symbols.write_text(inspected.stdout, encoding="utf-8")
            generated = subprocess.run(
                [
                    str(self._production_tool("cupidobj")),
                    "ksyms-source",
                    symbols.name,
                    "-o",
                    expected.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)

            result = self._run_generate_ksyms(elf, output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((result.stdout, result.stderr), ("", ""))
            self.assertEqual(output.read_bytes(), expected.read_bytes())
            blob = self._ksyms_blob_from_source(output.read_bytes())
            magic, count, string_offset, total_size = struct.unpack_from(
                "<IIII", blob
            )
            self.assertEqual(magic, 0x4D59534B)
            self.assertEqual(count, 2)
            self.assertEqual(string_offset, 32)
            self.assertEqual(total_size, len(blob))
            rows = [
                struct.unpack_from("<II", blob, 16 + index * 8)
                for index in range(count)
            ]
            self.assertEqual(
                [address for address, _ in rows],
                [0x01C00000, 0x01C00006],
            )
            names = []
            for _, name_offset in rows:
                name_start = string_offset + name_offset
                name_end = blob.index(b"\0", name_start)
                names.append(blob[name_start:name_end].decode("ascii"))
            self.assertEqual(names, ["same_address", "helper"])
            self.assertNotIn("_start", names)

    def test_typed_kernel_symbol_transaction_rejects_malformed_symbol_rows(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-ksyms-rows-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            elf = self._build_ksyms_elf(
                root,
                "BITS 32\n"
                "global _start:function\n"
                "global bad_name:function\n"
                "section .text\n"
                "_start:\n"
                "    ret\n"
                "bad_name:\n"
                "    ret\n",
            )
            payload = elf.read_bytes()
            self.assertEqual(payload.count(b"bad_name\0"), 1)
            elf.write_bytes(payload.replace(b"bad_name\0", b"bad name\0"))
            inspected = subprocess.run(
                [str(self._production_tool("cupiddis")), "-n", elf.name],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(inspected.returncode, 0, inspected.stderr)
            self.assertIn(" bad name", inspected.stdout)
            output = root / "ksyms_data.cc"
            output.write_bytes(b"last known good symbol source")
            before = self._private_roots()

            result = self._run_generate_ksyms(elf, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "independent kernel symbol validation failed", result.stderr
            )
            self.assertIn("malformed row", result.stderr)
            self.assertEqual(
                output.read_bytes(), b"last known good symbol source"
            )
            self.assertEqual(self._private_roots(), before)

    def test_typed_kernel_symbol_parity_failure_preserves_previous_output(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-ksyms-parity-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            document = json.loads(manifest.read_text(encoding="utf-8"))
            artifact = next(
                item
                for item in document["artifacts"]
                if item["name"] == "cupidobj"
            )
            cupidobj = seed / artifact["file"]
            payload = cupidobj.read_bytes()
            banner = b"Auto-generated by tools/hostbuild.py"
            mutated_banner = b"Auto-generated by tools/hostbuilE.py"
            self.assertEqual(len(banner), len(mutated_banner))
            self.assertEqual(payload.count(banner), 1)
            self._replace_seed_tool_bytes(
                manifest,
                "cupidobj",
                payload.replace(banner, mutated_banner),
            )
            elf = self._build_ksyms_elf(
                root,
                "BITS 32\n"
                "global _start:function\n"
                "section .text\n"
                "_start:\n"
                "    ret\n",
            )
            output = root / "ksyms_data.cc"
            output.write_bytes(b"last known good symbol source")
            before = self._private_roots()

            result = self._run_generate_ksyms(elf, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "differs from the independent renderer", result.stderr
            )
            self.assertEqual(
                output.read_bytes(), b"last known good symbol source"
            )
            self.assertEqual(self._private_roots(), before)

    def test_typed_kernel_symbol_transaction_rejects_malformed_elf(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-ksyms-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            source.write_bytes(b"not an ELF image\n")
            output.write_bytes(b"last known good symbol source")
            before = self._private_roots()

            result = self._run_generate_ksyms(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked CupidDis failed", result.stderr)
            self.assertEqual(
                output.read_bytes(), b"last known good symbol source"
            )
            self.assertEqual(self._private_roots(), before)

    def test_typed_kernel_symbol_transaction_keeps_independent_parity(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild.cc").read_text(encoding="utf-8")
        operation = source.split(
            "int cupidbuild_generate_ksyms(", 1
        )[1].split("int cupidbuild_run_checked_tool(", 1)[0]

        disassemble = operation.index(
            "cupidbuild_host_run_to_private_output("
        )
        independent_render = operation.index(
            "cupidbuild_render_ksyms_source("
        )
        object_transform = operation.index("cupidbuild_host_run(")
        parity = operation.index("memcmp(candidate, expected, expected_size)")

        self.assertLess(disassemble, independent_render)
        self.assertLess(independent_render, object_transform)
        self.assertLess(object_transform, parity)
        self.assertEqual(operation.count("cupidbuild_render_ksyms_source("), 1)

    def test_typed_kernel_symbol_live_lock_preserves_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-ksyms-lock-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "kernel.elf.pass1"
            output = root / "ksyms_data.cc"
            lock = Path(str(output) + ".cupidbuild.lock")
            source.write_bytes(b"not reached while the lock is held")
            output.write_bytes(b"last known good symbol source")
            lock.write_bytes(f"{os.getpid()}\n".encode("ascii"))

            result = self._run_generate_ksyms(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("live process", result.stderr)
            self.assertEqual(
                output.read_bytes(), b"last known good symbol source"
            )
            self.assertTrue(lock.is_file())

    def test_normal_kernel_symbol_recipe_uses_the_typed_checked_transaction(
        self,
    ):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        start = makefile.index(
            "kernel/cpu/ksyms_data.cc: kernel/kernel.elf.pass1"
        )
        end = makefile.index("\n\n", start)
        rule = makefile[start:end]
        logical_rule = " ".join(rule.replace("\\\n", " ").split())

        self.assertEqual(
            logical_rule,
            "kernel/cpu/ksyms_data.cc: kernel/kernel.elf.pass1 Makefile "
            "$(PRODUCTION_SEED_INPUTS) "
            "$(PRODUCTION_SEED_DIRECTORY)"
            "cupidbuild.$(PRODUCTION_SEED_SUFFIX) generate-ksyms "
            "--seed-manifest $(PRODUCTION_SEED_MANIFEST) "
            '--root "$(CURDIR)" --source $< --output $@',
        )
        self.assertNotIn("$(PYTHON)", rule)
        self.assertNotIn("tools/hostbuild.py", rule.lower())
        self.assertNotIn("$(CUPIDDIS)", rule)
        self.assertNotIn("$(CUPIDOBJ)", rule)
        self.assertNotIn(">", rule)

    def test_normal_jpeg_recipes_use_the_typed_checked_transaction(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        for suffix in ("jpg", "jpeg"):
            with self.subTest(suffix=suffix):
                start = makefile.index(f"%.{suffix}.o: %.{suffix}")
                end = makefile.index("\n\n", start)
                rule = makefile[start:end]
                logical_rule = " ".join(rule.replace("\\\n", " ").split())
                self.assertEqual(
                    logical_rule,
                    f"%.{suffix}.o: %.{suffix} Makefile "
                    "$(PRODUCTION_SEED_INPUTS) "
                    "$(PRODUCTION_SEED_DIRECTORY)"
                    "cupidbuild.$(PRODUCTION_SEED_SUFFIX) embed-jpeg "
                    "--seed-manifest $(PRODUCTION_SEED_MANIFEST) "
                    '--root "$(CURDIR)" --source $< --output $@',
                )
                self.assertNotIn("$(PYTHON)", rule)
                self.assertNotIn("tools/hostbuild.py", rule)
                self.assertNotIn("$(CHECKED_SEED_INPUTS)", rule)

    def test_both_jpeg_suffixes_ignore_host_runner_overrides(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")
        fixture = tempfile.NamedTemporaryFile(
            prefix=".cupidbuild-make-",
            suffix=".jpeg",
            dir=REPO_ROOT,
            delete=False,
        )
        fixture_path = Path(fixture.name)
        try:
            fixture.write(BASELINE_JPEG)
            fixture.close()
            for target in (
                "file_example_JPG_1MB.jpg.o",
                fixture_path.name + ".o",
            ):
                with self.subTest(target=target):
                    result = subprocess.run(
                        [
                            make,
                            "--no-print-directory",
                            "-n",
                            "-B",
                            "PYTHON=python-that-must-not-run",
                            "CUPIDOBJ=poison-cupidobj",
                            target,
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    logical_output = " ".join(result.stdout.split())
                    self.assertRegex(
                        logical_output,
                        r"bootstrap/seeds/i386-(?:linux|windows)/"
                        r"cupidbuild\.(?:elf|exe) embed-jpeg ",
                    )
                    self.assertRegex(
                        logical_output,
                        r"--seed-manifest bootstrap/seeds/i386-"
                        r"(?:linux|windows)/manifest\.json",
                    )
                    self.assertIn(f"--source {target[:-2]}", logical_output)
                    self.assertIn(f"--output {target}", logical_output)
                    self.assertNotIn(
                        "python-that-must-not-run",
                        logical_output,
                    )
                    self.assertNotIn("poison-cupidobj", logical_output)
        finally:
            fixture.close()
            fixture_path.unlink(missing_ok=True)
            fixture_path.with_name(fixture_path.name + ".o").unlink(
                missing_ok=True
            )

    def test_invalid_command_returns_usage_status_without_output(self):
        result = subprocess.run(
            [str(self.cli_path), "unknown"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage: cupidbuild", result.stderr)

    def test_help_names_the_checked_tool_runner(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild run --seed-manifest MANIFEST\s+"
            r"--root ROOT --tool \{cupidobj\|cupidld\} "
            r"\[--timeout SECONDS\]\s+"
            r"-- TOOL_ARGS\.\.\.",
        )

    def test_checked_tool_runner_matches_admitted_tools_success_and_failure(self):
        cases = (
            ("help", ["--help"], 30),
            ("invalid option", ["--not-a-tool-option"], None),
        )
        for tool in ("cupidobj", "cupidld"):
            executable = self._production_tool(tool)
            for name, arguments, timeout in cases:
                with self.subTest(tool=tool, name=name):
                    direct = subprocess.run(
                        [str(executable), *arguments],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=90,
                    )
                    checked = self._run_checked_tool(
                        tool, arguments, timeout=timeout
                    )

                    self.assertEqual(
                        (
                            checked.returncode,
                            checked.stdout,
                            checked.stderr,
                        ),
                        (
                            direct.returncode,
                            direct.stdout,
                            direct.stderr,
                        ),
                    )

    def test_checked_cupidld_runner_links_one_exact_fixed_elf(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-cupidld-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "entry.asm"
            object_path = root / "entry.o"
            direct_output = root / "direct.elf"
            checked_output = root / "checked.elf"
            source.write_text(
                "BITS 32\n"
                "global _start:function\n"
                "section .text\n"
                "_start:\n"
                "    ret\n",
                encoding="ascii",
            )
            assembled = subprocess.run(
                [
                    str(self._production_tool("cupidasm")),
                    "-f",
                    "elf32",
                    source.name,
                    "-o",
                    object_path.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(assembled.returncode, 0, assembled.stderr)
            self.assertEqual((assembled.stdout, assembled.stderr), ("", ""))
            common = [
                "-m",
                "elf_i386",
                "--text-address",
                "0x01C00000",
                "--entry",
                "_start",
            ]
            direct = subprocess.run(
                [
                    str(self._production_tool("cupidld")),
                    *common,
                    "-o",
                    direct_output.name,
                    object_path.name,
                ],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidld",
                [*common, "-o", checked_output.name, object_path.name],
                root=root,
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            direct_image = direct_output.read_bytes()
            checked_image = checked_output.read_bytes()
            self.assertEqual(checked_image, direct_image)
            self.assertGreater(len(checked_image), 52)
            self.assertEqual(checked_image[:4], b"\x7fELF")
            self.assertEqual(
                struct.unpack_from("<HHII", checked_image, 16),
                (2, 3, 1, 0x01C00000),
            )
            self.assertEqual(list(root.glob("*.cupid-tmp-*")), [])

    def test_checked_tool_runner_forwards_more_than_thirty_one_arguments(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-many-arguments-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            demos = root / "demos"
            demos.mkdir()
            demo_paths = []
            for index in range(40):
                source = demos / f"demo{index:02d}.asm"
                source.write_text("bits 32\nret\n", encoding="ascii")
                demo_paths.append(source.relative_to(root).as_posix())
            direct_output = root / "direct.cc"
            checked_output = root / "checked.cc"
            common = ["install-source", "demos", "--demos", *demo_paths]
            direct_arguments = [*common, "-o", direct_output.name]
            checked_arguments = [*common, "-o", checked_output.name]
            self.assertGreater(len(checked_arguments), 31)

            direct = subprocess.run(
                [str(self._production_tool("cupidobj")), *direct_arguments],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidobj", checked_arguments, root=root
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            self.assertEqual(checked_output.read_bytes(), direct_output.read_bytes())

    def test_checked_tool_runner_preserves_quoted_arguments(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-quoted-arguments-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            spaced = root / "path with spaces"
            spaced.mkdir()
            source = spaced / "source name.txt"
            source.write_bytes(b"checked quoting input\n")
            direct_output = spaced / "direct output.o"
            checked_output = spaced / "checked output.o"
            identity = 'quoted"identity\\'
            source_argument = source.relative_to(root).as_posix()
            direct_arguments = [
                "wrap-text", source_argument, "-o",
                direct_output.relative_to(root).as_posix(),
                "--identity", identity,
            ]
            checked_arguments = [
                "wrap-text", source_argument, "-o",
                checked_output.relative_to(root).as_posix(),
                "--identity", identity,
            ]

            direct = subprocess.run(
                [str(self._production_tool("cupidobj")), *direct_arguments],
                cwd=root,
                text=True,
                capture_output=True,
                timeout=90,
            )
            checked = self._run_checked_tool(
                "cupidobj", checked_arguments, root=root
            )

            self.assertEqual(direct.returncode, 0, direct.stderr)
            self.assertEqual(
                (checked.returncode, checked.stdout, checked.stderr),
                (direct.returncode, direct.stdout, direct.stderr),
            )
            self.assertEqual(checked_output.read_bytes(), direct_output.read_bytes())

    def test_checked_tool_runner_rejects_missing_or_unsupported_tool(self):
        manifest = self._production_manifest()
        prefix = [
            str(self.cli_path),
            "run",
            "--seed-manifest",
            str(manifest),
            "--root",
            str(REPO_ROOT),
        ]
        cases = (
            ("missing", [*prefix, "--", "--help"]),
            (
                "unsupported",
                [*prefix, "--tool", "cupidc", "--", "--help"],
            ),
        )
        for name, command in cases:
            with self.subTest(name=name):
                result = subprocess.run(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=90,
                )

                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("usage: cupidbuild run", result.stderr)

    def test_checked_tool_runner_requires_the_argument_separator(self):
        result = subprocess.run(
            [
                str(self.cli_path),
                "run",
                "--seed-manifest",
                str(self._production_manifest()),
                "--root",
                str(REPO_ROOT),
                "--tool",
                "cupidobj",
                "--help",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=90,
        )

        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage: cupidbuild run", result.stderr)

    def test_checked_tool_runner_rejects_duplicate_options_and_bad_timeouts(self):
        manifest = str(self._production_manifest())
        root = str(REPO_ROOT)
        required = [
            "--seed-manifest", manifest,
            "--root", root,
            "--tool", "cupidobj",
        ]
        cases = {
            "duplicate manifest": ["--seed-manifest", manifest],
            "duplicate root": ["--root", root],
            "duplicate tool": ["--tool", "cupidobj"],
            "duplicate timeout": ["--timeout", "1", "--timeout", "2"],
            "zero timeout": ["--timeout", "0"],
            "negative timeout": ["--timeout", "-1"],
            "too large timeout": ["--timeout", "86401"],
            "nonnumeric timeout": ["--timeout", "soon"],
        }
        for name, extra in cases.items():
            with self.subTest(name=name):
                result = subprocess.run(
                    [
                        str(self.cli_path), "run", *required, *extra,
                        "--", "--help",
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=90,
                )

                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("usage: cupidbuild run", result.stderr)

    def test_checked_tool_runner_removes_private_roots_after_tool_exit(self):
        cases = (
            ("success", ["--help"], 0),
            ("tool failure", ["--not-a-cupidobj-option"], 2),
        )
        for name, arguments, expected_status in cases:
            with self.subTest(name=name):
                before = self._run_private_roots()
                result = self._run_checked_tool("cupidobj", arguments)
                after = self._run_private_roots()

                self.assertEqual(after, before)
                self.assertEqual(
                    result.returncode,
                    expected_status,
                    result.stderr,
                )

    def test_checked_tool_runner_times_out_and_cleans_up(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-timeout-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked timeout input\n" * 1_500_000)

            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                timeout=1,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked CupidObj timed out", result.stderr)
            self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])

    def test_checked_tool_runner_reports_seed_drift_after_timeout(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-timeout-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked timeout drift input\n" * 1_500_000)
            command = self._checked_tool_command(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                timeout=1,
                manifest=manifest,
            )
            process = subprocess.Popen(
                command,
                cwd=root,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            deadline = time.monotonic() + 20
            launched = False
            while time.monotonic() < deadline and process.poll() is None:
                if os.name == "nt":
                    launched = any(
                        private.joinpath("tool.stdout").is_file()
                        for private in root.glob(".cupidbuild-run-*")
                    )
                else:
                    children = Path(
                        f"/proc/{process.pid}/task/{process.pid}/children"
                    )
                    try:
                        launched = bool(children.read_text(encoding="ascii").strip())
                    except (FileNotFoundError, PermissionError, ProcessLookupError):
                        launched = False
                if launched:
                    break
                time.sleep(0.001)
            self.assertTrue(launched, "checked tool launch was not observed")
            manifest.write_bytes(manifest.read_bytes() + b" \n")
            stdout, stderr = process.communicate(timeout=90)

            self.assertEqual(process.returncode, 1)
            self.assertEqual(stdout, "")
            self.assertIn("checked seed inputs changed", stderr)
            self.assertNotIn("checked CupidObj timed out", stderr)
            self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])

    @unittest.skipUnless(os.name == "nt", "Windows uses private runner roots")
    def test_checked_tool_runner_fails_when_private_cleanup_is_incomplete(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-cleanup-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked cleanup input\n" * 1_000_000)
            planted = threading.Event()

            def plant_unowned_file_after_tool_starts():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        if private.name.startswith(
                            ".cupidbuild-run-cleanup-failure-"
                        ):
                            continue
                        if (private.joinpath("tool.stdout").is_file()):
                            private.joinpath("unowned.txt").write_text(
                                "leave me alone\n", encoding="ascii"
                            )
                            planted.set()
                            return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=plant_unowned_file_after_tool_starts, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(
                    planted.is_set(), "checked tool launch was not observed"
                )
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "private checked-tool cleanup failed", result.stderr
                )
                self.assertEqual(len(private_roots), 1)
                self.assertEqual(
                    private_roots[0].joinpath("unowned.txt").read_text(
                        encoding="ascii"
                    ),
                    "leave me alone\n",
                )
            finally:
                for private in private_roots:
                    shutil.rmtree(private)

    @unittest.skipUnless(os.name == "nt", "Windows uses named stream files")
    def test_checked_tool_runner_does_not_delete_a_planted_stream_path(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-planted-stream-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            planted = threading.Event()

            def plant_stdout_after_private_root_appears():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        stream = private / "tool.stdout"
                        if not stream.exists():
                            try:
                                stream.write_text(
                                    "unowned stream\n", encoding="ascii"
                                )
                            except OSError:
                                continue
                            planted.set()
                            return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=plant_stdout_after_private_root_appears, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool("cupidobj", ["--help"], root=root)
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(planted.is_set(), "private root was not observed")
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "private checked-tool cleanup failed", result.stderr
                )
                planted_path = next(
                    private / "tool.stdout"
                    for private in private_roots
                    if private.joinpath("tool.stdout").is_file()
                )
                self.assertEqual(
                    planted_path.read_text(encoding="ascii"),
                    "unowned stream\n",
                )
            finally:
                for private in private_roots:
                    shutil.rmtree(private)

    @unittest.skipUnless(os.name == "nt", "Windows uses named frozen inputs")
    def test_checked_tool_runner_rejects_private_seed_drift(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-private-seed-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"private seed drift input\n" * 1_000_000)
            changed = threading.Event()

            def change_frozen_tool_after_launch():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        if not private.joinpath("tool.stdout").is_file():
                            continue
                        frozen = next(private.glob("cupidc.*"), None)
                        if frozen is None:
                            continue
                        with frozen.open("ab") as stream:
                            stream.write(b"drift")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_frozen_tool_after_launch, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(changed.is_set(), "checked tool launch was not observed")
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn(
                    "private checked seed changed while checked tool ran",
                    result.stderr,
                )
                self.assertNotIn(
                    "private checked-tool cleanup failed", result.stderr
                )
                self.assertEqual(private_roots, [])
            finally:
                for private in private_roots:
                    shutil.rmtree(private)

    @unittest.skipIf(os.name == "nt", "Linux uses anonymous runner files")
    def test_checked_tool_runner_uses_sealed_anonymous_inputs(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-anonymous-inputs-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"anonymous checked input\n" * 1_500_000)
            command = self._checked_tool_command(
                "cupidobj", ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            process = subprocess.Popen(
                command, cwd=root, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            deadline = time.monotonic() + 20
            sealed = []
            while time.monotonic() < deadline and process.poll() is None:
                self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])
                descriptors = Path(f"/proc/{process.pid}/fd")
                try:
                    links = {
                        path: os.readlink(path)
                        for path in descriptors.iterdir()
                    }
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    time.sleep(0.001)
                    continue
                frozen = [
                    path for path, link in links.items()
                    if "/memfd:" in link
                    and "cupidbuild-stdout" not in link
                    and "cupidbuild-stderr" not in link
                ]
                if len(frozen) >= 7:
                    import fcntl

                    for path in frozen:
                        descriptor = os.open(path, os.O_RDWR)
                        try:
                            sealed.append(fcntl.fcntl(descriptor, 1034))
                            with self.assertRaises(OSError):
                                os.write(descriptor, b"drift")
                            with self.assertRaises(OSError):
                                os.ftruncate(descriptor, 0)
                        finally:
                            os.close(descriptor)
                    break
                time.sleep(0.001)
            stdout, stderr = process.communicate(timeout=90)

            self.assertEqual(process.returncode, 0, stderr)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "")
            self.assertGreaterEqual(len(sealed), 7)
            self.assertTrue(all(value == 15 for value in sealed), sealed)
            self.assertEqual(list(root.glob(".cupidbuild-run-*")), [])

    @unittest.skipUnless(os.name == "nt", "Windows uses private runner roots")
    def test_checked_tool_runner_does_not_delete_a_replacement_private_root(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-directory-replacement-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "large.txt"
            output = root / "large.o"
            stolen = root / "stolen-private-root"
            source.write_bytes(b"checked replacement input\n" * 1_000_000)
            attempted = threading.Event()
            replaced = threading.Event()

            def replace_private_root_after_tool_starts():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    for private in root.glob(".cupidbuild-run-*"):
                        if private.name.startswith(
                            ".cupidbuild-run-directory-replacement-"
                        ):
                            continue
                        if not private.joinpath("tool.stdout").is_file():
                            continue
                        attempted.set()
                        try:
                            private.rename(stolen)
                        except OSError:
                            return
                        private.mkdir()
                        private.joinpath("replacement.txt").write_text(
                            "replacement stays\n", encoding="ascii"
                        )
                        replaced.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_private_root_after_tool_starts, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
            )
            mutator.join(timeout=20)
            private_roots = list(root.glob(".cupidbuild-run-*"))

            try:
                self.assertTrue(
                    attempted.is_set(), "checked tool launch was not observed"
                )
                self.assertFalse(
                    replaced.is_set(),
                    "the pinned Windows directory was renamed",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(private_roots, [])
            finally:
                for private in private_roots:
                    shutil.rmtree(private)
                if stolen.exists():
                    shutil.rmtree(stolen)

    def test_checked_tool_runner_suppresses_output_after_live_seed_drift(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-run-seed-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "large.txt"
            output = root / "large.o"
            source.write_bytes(b"checked runner input\n" * 1_500_000)
            changed = threading.Event()

            def change_manifest_after_tool_starts():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if output.exists():
                        manifest.write_bytes(manifest.read_bytes() + b" \n")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_manifest_after_tool_starts, daemon=True
            )
            mutator.start()
            result = self._run_checked_tool(
                "cupidobj",
                ["wrap-text", source.name, "-o", output.name],
                root=root,
                manifest=manifest,
            )
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "checked tool launch was not observed")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertEqual(set(root.glob(".cupidbuild-run-*")), set())

    def _production_manifest(self):
        platform = "i386-windows" if os.name == "nt" else "i386-linux"
        return REPO_ROOT / "bootstrap" / "seeds" / platform / "manifest.json"

    def _production_tool(self, name):
        manifest = self._production_manifest()
        document = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = next(
            artifact
            for artifact in document["artifacts"]
            if artifact["name"] == name
        )
        return manifest.parent / artifact["file"]

    def _run_checked_tool(
        self,
        tool,
        arguments,
        *,
        root=REPO_ROOT,
        timeout=None,
        manifest=None,
    ):
        command = self._checked_tool_command(
            tool, arguments, root=root, timeout=timeout, manifest=manifest
        )
        return subprocess.run(
            command,
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )

    def _checked_tool_command(
        self,
        tool,
        arguments,
        *,
        root=REPO_ROOT,
        timeout=None,
        manifest=None,
    ):
        command = [
            str(self.cli_path),
            "run",
            "--seed-manifest",
            str(manifest or self._production_manifest()),
            "--root",
            str(root),
            "--tool",
            tool,
        ]
        if timeout is not None:
            command.extend(["--timeout", str(timeout)])
        command.extend(["--", *arguments])
        return command

    def _run_private_roots(self):
        return set(REPO_ROOT.glob(".cupidbuild-run-*"))

    def _run_assembly(self, operation, source, output, manifest=None):
        return subprocess.run(
            [
                str(self.cli_path),
                operation,
                "--seed-manifest",
                str(manifest or self._production_manifest()),
                "--root",
                str(REPO_ROOT),
                "--source",
                source.relative_to(REPO_ROOT).as_posix(),
                "--output",
                output.relative_to(REPO_ROOT).as_posix(),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=90,
        )

    def _run_object(self, source, output, manifest=None):
        return self._run_assembly(
            "assemble-cupidasm-object", source, output, manifest
        )

    def _run_embed_jpeg(self, source, output, manifest=None):
        return self._run_assembly("embed-jpeg", source, output, manifest)

    def _run_generate_ksyms(self, source, output, manifest=None):
        return self._run_assembly("generate-ksyms", source, output, manifest)

    def _run_flatten_kernel(self, root, input_manifest, output, seed_manifest):
        return subprocess.run(
            [
                str(self.cli_path),
                "flatten-kernel",
                "--seed-manifest",
                str(seed_manifest),
                "--root",
                str(root),
                "--input-manifest",
                input_manifest.relative_to(root).as_posix(),
                "--output",
                output.relative_to(root).as_posix(),
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )

    def _build_ksyms_elf(self, root, source_text):
        source = root / "entry.asm"
        object_path = root / "entry.o"
        elf = root / "kernel.elf.pass1"
        source.write_text(source_text, encoding="ascii")
        assembled = subprocess.run(
            [
                str(self._production_tool("cupidasm")),
                "-f",
                "elf32",
                source.name,
                "-o",
                object_path.name,
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )
        self.assertEqual(assembled.returncode, 0, assembled.stderr)
        linked = subprocess.run(
            [
                str(self._production_tool("cupidld")),
                "-m",
                "elf_i386",
                "--text-address",
                "0x01C00000",
                "--entry",
                "_start",
                "-o",
                elf.name,
                object_path.name,
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )
        self.assertEqual(linked.returncode, 0, linked.stderr)
        return elf

    def _ksyms_blob_from_source(self, source):
        words = [
            int(match, 16)
            for match in re.findall(rb"0x([0-9a-f]{8})u,", source)
        ]
        size_match = re.search(rb"ksym_blob_size = ([0-9]+)u;", source)
        self.assertIsNotNone(size_match)
        self.assertTrue(words)
        packed = b"".join(struct.pack("<I", word) for word in words)
        size = int(size_match.group(1))
        self.assertGreaterEqual(len(packed), size)
        self.assertLess(len(packed) - size, 4)
        return packed[:size]

    @staticmethod
    def _large_baseline_jpeg(entropy_size=8 * 1024 * 1024):
        return BASELINE_JPEG[:-2] + b"\x12" * entropy_size + BASELINE_JPEG[-2:]

    def _private_roots(self):
        return {
            path
            for path in REPO_ROOT.glob(".cupidbuild-object-*")
            if re.fullmatch(r"\.cupidbuild-object-[0-9a-f]{8}", path.name)
        }

    def _copy_checked_assembly_seed(self, destination):
        destination.mkdir()
        production_seed = self._production_manifest().parent
        shutil.copy2(production_seed / "manifest.json", destination / "manifest.json")
        suffix = ".exe" if os.name == "nt" else ".elf"
        for tool in (
            "cupidasm",
            "cupidc",
            "cupiddis",
            "cupidld",
            "cupidobj",
            "cupidbuild",
        ):
            shutil.copy2(
                production_seed / f"{tool}{suffix}",
                destination / f"{tool}{suffix}",
            )
        return destination / "manifest.json"

    def _replace_seed_tool(self, manifest, name, executable):
        document = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = next(
            artifact
            for artifact in document["artifacts"]
            if artifact["name"] == name
        )
        destination = manifest.parent / artifact["file"]
        shutil.copy2(executable, destination)
        payload = destination.read_bytes()
        artifact["size"] = len(payload)
        artifact["sha256"] = hashlib.sha256(payload).hexdigest()
        manifest.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def _replace_seed_tool_bytes(self, manifest, name, payload):
        document = json.loads(manifest.read_text(encoding="utf-8"))
        artifact = next(
            artifact
            for artifact in document["artifacts"]
            if artifact["name"] == name
        )
        destination = manifest.parent / artifact["file"]
        destination.write_bytes(payload)
        artifact["size"] = len(payload)
        artifact["sha256"] = hashlib.sha256(payload).hexdigest()
        manifest.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def _rewrite_manifest(self, manifest, change, *, compact=False):
        document = json.loads(manifest.read_text(encoding="utf-8"))
        changed = change(document)
        if changed is not None:
            document = changed
        if compact:
            encoded = json.dumps(document, separators=(",", ":")) + "\n"
        else:
            encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
        manifest.write_text(encoded, encoding="utf-8")

    def _promote_seed_contract(self, manifest):
        document = json.loads(manifest.read_text(encoding="utf-8"))
        suffix = ".exe" if os.name == "nt" else ".elf"
        stand_in = manifest.parent / f"cupidbuild{suffix}"
        shutil.copy2(manifest.parent / f"cupidobj{suffix}", stand_in)
        payload = stand_in.read_bytes()
        build_artifact = next(
            (
                artifact
                for artifact in document["artifacts"]
                if artifact["name"] == "cupidbuild"
            ),
            None,
        )
        if build_artifact is None:
            build_artifact = {
                "file": stand_in.name,
                "name": "cupidbuild",
                "producer": False,
                "sha256": "",
                "size": 0,
            }
            document["artifacts"].append(build_artifact)
        build_artifact["sha256"] = hashlib.sha256(payload).hexdigest()
        build_artifact["size"] = len(payload)
        revision = "abcdef0123456789abcdef0123456789abcdef01"
        snapshot = "2" * 64
        if os.name == "nt":
            document["schema"] = "cupid.execution-seed.v2"
            document["provenance"] = {
                "artifact_generation": (
                    "paired-stage-four-six-tool-native-windows"
                ),
                "fixed_point_command": "make bootstrap-windows-from-seed",
                "fixed_point_result": "pass",
                "linux_candidate_build_plan_sha256": (
                    "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
                ),
                "native_build_plan_sha256": (
                    "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14"
                ),
                "plan_seed_manifest_sha256": "3" * 64,
                "parent_execution_seed_manifest_sha256": (
                    "751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef"
                ),
                "parent_execution_seed_source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "parent_plan_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_plan_seed_source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "producer_lineage": document["provenance"][
                    "producer_lineage"
                ],
                "source_input_count": 58,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        else:
            document["schema"] = "cupid.bootstrap-seed.v2"
            plan = document["build_plan"]
            if "cupidbuild" not in plan["links"]:
                plan["sources"].extend(
                    [
                    {
                        "gnu_extensions": False,
                        "name": "cupidbuild",
                        "path": "/toolchain/cupidbuild.cc",
                    },
                    {
                        "gnu_extensions": False,
                        "name": "cupidbuild_host",
                        "path": "/toolchain/cupidbuild_host.cc",
                    },
                    {
                        "gnu_extensions": False,
                        "name": "cupidbuild_main",
                        "path": "/toolchain/cupidbuild_main.cc",
                    },
                    ]
                )
                plan["links"]["cupidbuild"] = [
                    "start",
                    "cupidbuild_main",
                    "cupidbuild",
                    "cupidbuild_host",
                    "ctool_host",
                    "ctool",
                    "elf32",
                    "runtime",
                ]
            encoded_plan = json.dumps(
                plan,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=True,
            ).encode("ascii")
            document["build_plan_sha256"] = hashlib.sha256(
                encoded_plan
            ).hexdigest()
            document["provenance"] = {
                "artifact_generation": "paired-stage-four-six-tool",
                "fixed_point_command": "make bootstrap-from-seed",
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_seed_source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
                "producer_lineage": document["provenance"][
                    "producer_lineage"
                ],
                "seed_generation": "stage-four",
                "source_input_count": 58,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        manifest.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return document

    @staticmethod
    def _legacy_seed_contract(document):
        legacy = json.loads(json.dumps(document))
        legacy["artifacts"] = [
            artifact
            for artifact in legacy["artifacts"]
            if artifact["name"] != "cupidbuild"
        ]
        revision = "a17c9465911da41d59b7ada71733d36c39faa5ea"
        snapshot = (
            "46c5335c80d822dd5085ee22077486ea"
            "647e5396482d42454847c87e4222aa67"
        )
        lineage = legacy["provenance"]["producer_lineage"]
        if os.name == "nt":
            legacy["schema"] = "cupid.execution-seed.v1"
            legacy["provenance"] = {
                "artifact_generation": "paired-stage-four-native-windows",
                "fixed_point_command": "make bootstrap-windows-from-seed",
                "fixed_point_result": "pass",
                "parent_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde"
                    "39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_seed_source_revision": revision,
                "producer_lineage": lineage,
                "source_input_count": 50,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        else:
            legacy["schema"] = "cupid.bootstrap-seed.v1"
            plan = legacy["build_plan"]
            candidate_sources = {
                "cupidbuild",
                "cupidbuild_host",
                "cupidbuild_main",
            }
            plan["sources"] = [
                source
                for source in plan["sources"]
                if source["name"] not in candidate_sources
            ]
            del plan["links"]["cupidbuild"]
            encoded_plan = json.dumps(
                plan,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=True,
            ).encode("ascii")
            legacy["build_plan_sha256"] = hashlib.sha256(
                encoded_plan
            ).hexdigest()
            legacy["provenance"] = {
                "fixed_point_command": "make bootstrap-from-seed",
                "fixed_point_result": "pass",
                "producer_lineage": lineage,
                "seed_generation": "stage-four",
                "source_input_count": 50,
                "source_revision": revision,
                "source_snapshot_sha256": snapshot,
            }
        return legacy

    @staticmethod
    def _reverse_object_fields(value):
        if isinstance(value, dict):
            return {
                key: CupidBuildCliTests._reverse_object_fields(value[key])
                for key in reversed(value)
            }
        if isinstance(value, list):
            return [
                CupidBuildCliTests._reverse_object_fields(item)
                for item in value
            ]
        return value

    def test_checked_tools_publish_a_guarded_relocatable_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-success-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\nglobal entry\nentry: nop\nret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_embed_jpeg_preserves_the_original_source_identity(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-success-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "photo-with-original-name.jpg"
            output = root / "output.o"
            expected = root / "expected.o"
            source.write_bytes(BASELINE_JPEG)
            output.write_bytes(b"last known good object")
            identity = source.relative_to(REPO_ROOT).as_posix()
            oracle = subprocess.run(
                [
                    str(self._production_tool("cupidobj")),
                    "wrap-jpeg",
                    identity,
                    "-o",
                    expected.relative_to(REPO_ROOT).as_posix(),
                    "--identity",
                    identity,
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=90,
            )
            self.assertEqual(oracle.returncode, 0, oracle.stderr)

            result = self._run_embed_jpeg(source, output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            self.assertEqual(output.read_bytes(), expected.read_bytes())
        self.assertEqual(self._private_roots(), before)

    def test_embed_jpeg_rejects_malformed_input_without_clobbering(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-malformed-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "malformed.jpg"
            output = root / "output.o"
            source.write_bytes(b"not a JPEG frame")
            output.write_bytes(b"last known good object")

            result = self._run_embed_jpeg(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked CupidObj failed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")
        self.assertEqual(self._private_roots(), before)

    def test_embed_jpeg_keeps_the_independent_parser_after_cupidobj(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild.cc").read_text(encoding="utf-8")
        operation = source.split(
            "int cupidbuild_embed_jpeg(", 1
        )[1].split("int cupidbuild_run_checked_tool(", 1)[0]

        tool_call = operation.index("cupidbuild_host_run(")
        object_check = operation.index(
            "cupidbuild_validate_jpeg_object_bytes("
        )
        parser_check = operation.index("cupidbuild_validate_jpeg_bytes(")

        self.assertLess(tool_call, object_check)
        self.assertLess(object_check, parser_check)
        self.assertEqual(operation.count("cupidbuild_validate_jpeg_bytes("), 1)

    def test_embed_jpeg_rejects_output_aliasing_the_seed_manifest(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-manifest-alias-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.jpg"
            source.write_bytes(BASELINE_JPEG)
            original_manifest = manifest.read_bytes()

            result = self._run_embed_jpeg(source, manifest, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("output may not replace an input", result.stderr)
            self.assertNotIn("code output", result.stderr)
            self.assertEqual(manifest.read_bytes(), original_manifest)
        self.assertEqual(self._private_roots(), before)

    def test_embed_jpeg_live_lock_preserves_the_previous_output(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-lock-contention-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.jpg"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            source.write_bytes(BASELINE_JPEG)
            output.write_bytes(b"last known good object")
            lock.write_bytes(f"{os.getpid()}\n".encode("ascii"))

            result = self._run_embed_jpeg(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("live process", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")
            self.assertTrue(lock.is_file())
        self.assertEqual(self._private_roots(), before)

    def test_embed_jpeg_source_drift_preserves_the_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-source-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.jpg"
            output = root / "output.o"
            original_source = self._large_baseline_jpeg()
            source.write_bytes(original_source)
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def replace_source_when_transaction_opens():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / "source.asm").is_file() for path in new_roots):
                        source.write_bytes(original_source + b"\x00")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_source_when_transaction_opens, daemon=True
            )
            mutator.start()
            result = self._run_embed_jpeg(source, output)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "JPEG transaction was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("source changed while checked tools ran", result.stderr)
            self.assertNotIn("CupidASM source", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")
            self.assertEqual(self._private_roots(), before)

    def test_embed_jpeg_seed_drift_preserves_the_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-seed-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            suffix = ".exe" if os.name == "nt" else ".elf"
            source = root / "input.jpg"
            output = root / "output.o"
            source.write_bytes(self._large_baseline_jpeg())
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def change_manifest_after_freeze():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any(
                        (path / f"cupidobj{suffix}").is_file()
                        for path in new_roots
                    ):
                        with manifest.open("ab") as stream:
                            stream.write(b"\n")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_manifest_after_freeze, daemon=True
            )
            mutator.start()
            result = self._run_embed_jpeg(source, output, manifest)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "frozen JPEG seed was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")
            self.assertEqual(self._private_roots(), before)

    def test_embed_jpeg_output_drift_preserves_competing_bytes(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-jpeg-output-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.jpg"
            output = root / "output.o"
            source.write_bytes(self._large_baseline_jpeg())
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def replace_output_after_wrapping():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / "candidate.o").is_file() for path in new_roots):
                        output.write_bytes(b"competing publisher")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_output_after_wrapping, daemon=True
            )
            mutator.start()
            result = self._run_embed_jpeg(source, output)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "checked JPEG output was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output changed while checked tools ran", result.stderr)
            self.assertNotIn("code output", result.stderr)
            self.assertEqual(output.read_bytes(), b"competing publisher")
            self.assertEqual(self._private_roots(), before)

    def test_checked_tools_publish_the_active_raw_boot_artifacts(self):
        cases = (
            (
                "assemble-bootloader",
                REPO_ROOT / "boot" / "boot.asm",
                2560,
                "46cc9778da2b5cc5e8f04d7cc4b07243"
                "c3e07d466626ad84fb813dc6fef3a0d3",
            ),
            (
                "assemble-smp-trampoline",
                REPO_ROOT / "kernel" / "smp" / "smp_trampoline.S",
                4096,
                "b738ebb68f28b9b07e330761f4e9a789"
                "8f0424ab0a3835cd6079ae7d4a189e90",
            ),
        )
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-raw-success-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            for operation, source, size, digest in cases:
                with self.subTest(operation=operation):
                    output = root / f"{operation}.bin"
                    output.write_bytes(b"last known good raw image")

                    result = self._run_assembly(operation, source, output)

                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    payload = output.read_bytes()
                    self.assertEqual(len(payload), size)
                    self.assertEqual(hashlib.sha256(payload).hexdigest(), digest)
        self.assertEqual(self._private_roots(), before)

    def test_raw_commands_reject_incomplete_or_stray_options(self):
        source = REPO_ROOT / "boot" / "boot.asm"
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-raw-cli-failure-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "output.bin"
            base = [
                str(self.cli_path),
                "assemble-bootloader",
                "--seed-manifest",
                str(self._production_manifest()),
                "--root",
                str(REPO_ROOT),
                "--source",
                source.relative_to(REPO_ROOT).as_posix(),
            ]
            invocations = (
                base,
                base
                + [
                    "--root",
                    str(REPO_ROOT),
                    "--output",
                    output.relative_to(REPO_ROOT).as_posix(),
                ],
                base
                + [
                    "--output",
                    output.relative_to(REPO_ROOT).as_posix(),
                    "--unexpected",
                ],
            )
            for arguments in invocations:
                with self.subTest(arguments=arguments):
                    result = subprocess.run(
                        arguments,
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=90,
                    )

                    self.assertEqual(result.returncode, 2)
                    self.assertEqual(result.stdout, "")
                    self.assertIn("usage: cupidbuild", result.stderr)
                    self.assertFalse(output.exists())

    def test_raw_operations_reject_wrong_sizes_and_preserve_outputs(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-raw-size-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            source.write_text(
                "bits 16\norg 0x7c00\nsection .text\nnop\n",
                encoding="utf-8",
            )
            for operation in (
                "assemble-bootloader",
                "assemble-smp-trampoline",
            ):
                with self.subTest(operation=operation):
                    output = root / f"{operation}.bin"
                    output.write_bytes(b"last known good raw image")

                    result = self._run_assembly(operation, source, output)

                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("raw output validation failed", result.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"last known good raw image"
                    )

    def test_smp_operation_rejects_a_wrong_exact_size_layout(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-smp-layout-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.bin"
            source.write_text(
                "bits 16\norg 0x8000\nsection .text\n"
                "times 4096 db 0\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good raw image")

            result = self._run_assembly(
                "assemble-smp-trampoline", source, output
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("range map does not match", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good raw image")

    def test_bootloader_operation_rejects_a_nonlocal_raw_target(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-boot-unknown-code-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.bin"
            source.write_text(
                "bits 16\norg 0x7c00\nsection .text\n"
                "jmp 0x7000\n"
                "times 2560 - ($ - $$) db 0\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good raw image")

            result = self._run_assembly("assemble-bootloader", source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked CupidDis failed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good raw image")

    def test_six_tool_v2_contract_reaches_checked_execution_profiles(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-v2-contract-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            self._promote_seed_contract(manifest)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\nglobal entry:function\nentry: ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            if os.name == "nt":
                self.assertEqual(result.returncode, 1)
                self.assertIn("execution profile mismatch", result.stderr)
                self.assertEqual(
                    output.read_bytes(), b"last known good object"
                )
            else:
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertEqual(
                    output.read_bytes()[:7], b"\x7fELF\x01\x01\x01"
                )

    def test_six_tool_v2_contract_accepts_58_and_59_source_inputs(self):
        for source_input_count in (58, 59):
            with self.subTest(
                source_input_count=source_input_count
            ), tempfile.TemporaryDirectory(
                prefix=".cupidbuild-object-v2-source-count-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                manifest = self._copy_checked_assembly_seed(root / "seed")
                document = self._promote_seed_contract(manifest)
                document["provenance"][
                    "source_input_count"
                ] = source_input_count
                manifest.write_text(
                    json.dumps(document, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                source = root / "input.asm"
                output = root / "output.o"
                source.write_text(
                    "bits 32\nsection .text\nglobal entry:function\nentry: ret\n",
                    encoding="utf-8",
                )
                output.write_bytes(b"last known good object")

                result = self._run_object(source, output, manifest)

                self.assertNotIn(
                    "fixed-point provenance differs", result.stderr
                )
                if os.name == "nt":
                    self.assertEqual(result.returncode, 1)
                    self.assertIn("execution profile mismatch", result.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"last known good object"
                    )
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(
                        output.read_bytes()[:7], b"\x7fELF\x01\x01\x01"
                    )

    def test_six_tool_v2_contract_rejects_parent_and_revision_drift(self):
        def uppercase_revision(document):
            revision = document["provenance"]["source_revision"]
            document["provenance"]["source_revision"] = revision.upper()

        def change_parent(document):
            parent_field = (
                "parent_execution_seed_manifest_sha256"
                if os.name == "nt"
                else "parent_seed_manifest_sha256"
            )
            document["provenance"][parent_field] = "0" * 64

        cases = (
            ("uppercase source revision", uppercase_revision),
            ("wrong parent manifest", change_parent),
            (
                "preceding source input count",
                lambda document: document["provenance"].update(
                    {"source_input_count": 57}
                ),
            ),
            (
                "unpublished source input count",
                lambda document: document["provenance"].update(
                    {"source_input_count": 60}
                ),
            ),
            (
                "non-numeric source input count",
                lambda document: document["provenance"].update(
                    {"source_input_count": "59"}
                ),
            ),
        )
        if os.name == "nt":
            cases += (
                (
                    "malformed plan seed manifest",
                    lambda document: document["provenance"].update(
                        {"plan_seed_manifest_sha256": "A" * 64}
                    ),
                ),
            )
        for label, mutate in cases:
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix=".cupidbuild-object-v2-provenance-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                manifest = self._copy_checked_assembly_seed(root / "seed")
                document = self._promote_seed_contract(manifest)
                source = root / "input.asm"
                output = root / "output.o"
                source.write_text(
                    "bits 32\nsection .text\nret\n", encoding="utf-8"
                )
                output.write_bytes(b"last known good object")
                mutate(document)
                manifest.write_text(
                    json.dumps(document, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )

                result = self._run_object(source, output, manifest)

                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "fixed-point provenance differs", result.stderr
                )
                self.assertEqual(
                    output.read_bytes(), b"last known good object"
                )

    def test_unlisted_executable_seed_peer_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-seed-membership-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            suffix = ".EXE" if os.name == "nt" else ".ELF"
            (manifest.parent / f"unlisted{suffix}").write_bytes(b"not trusted")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unlisted executable file", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_unlisted_executable_shaped_directory_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-seed-directory-peer-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            suffix = ".EXE" if os.name == "nt" else ".ELF"
            (manifest.parent / f"unlisted{suffix}").mkdir()
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unlisted executable file", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_seed_execution_profile_drift_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-seed-execution-profile-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            suffix = ".exe" if os.name == "nt" else ".elf"
            compiler = manifest.parent / f"cupidc{suffix}"
            payload = bytearray(compiler.read_bytes())
            entry_offset = 152 + 16 if os.name == "nt" else 24
            payload[entry_offset] ^= 0x01
            self._replace_seed_tool_bytes(manifest, "cupidc", payload)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("execution profile mismatch", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_reordered_compact_seed_manifest_keeps_the_same_contract(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-compact-manifest-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\nglobal entry:function\nentry: ret\n",
                encoding="utf-8",
            )
            self._rewrite_manifest(
                manifest, self._reverse_object_fields, compact=True
            )

            result = self._run_object(source, output, manifest)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_unsupported_seed_schema_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-schema-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            original = output.read_bytes()
            self._rewrite_manifest(
                manifest,
                lambda document: document.__setitem__("schema", "cupid.seed.v0"),
            )

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed manifest is invalid", result.stderr)
            self.assertIn("schema differs", result.stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_seed_target_drift_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-target-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            original = output.read_bytes()
            self._rewrite_manifest(
                manifest,
                lambda document: document["target"].__setitem__(
                    "architecture", "x86_64"
                ),
            )

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed manifest is invalid", result.stderr)
            self.assertIn("target contract differs", result.stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_seed_provenance_failure_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-provenance-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            original = output.read_bytes()
            self._rewrite_manifest(
                manifest,
                lambda document: document["provenance"].__setitem__(
                    "fixed_point_result", "fail"
                ),
            )

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed manifest is invalid", result.stderr)
            self.assertIn("fixed-point provenance differs", result.stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_seed_producer_role_drift_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-producer-role-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            original = output.read_bytes()

            def change_role(document):
                artifact = next(
                    item for item in document["artifacts"]
                    if item["name"] == "cupiddis"
                )
                artifact["producer"] = True

            self._rewrite_manifest(manifest, change_role)

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed manifest is invalid", result.stderr)
            self.assertIn("artifact inventory differs", result.stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_malformed_or_incomplete_seed_contract_is_rejected(self):
        def v2_artifact_in_v1_manifest(document):
            build_artifact = next(
                artifact
                for artifact in document["artifacts"]
                if artifact["name"] == "cupidbuild"
            )
            legacy = self._legacy_seed_contract(document)
            legacy["artifacts"].append(build_artifact)
            return json.dumps(legacy, indent=2, sort_keys=True) + "\n"

        mutations = {
            "trailing document": lambda document: (
                json.dumps(document, indent=2, sort_keys=True) + "\n{}\n"
            ),
            "duplicate schema": lambda document: (
                json.dumps(document, indent=2, sort_keys=True).replace(
                    '  "schema":',
                    f'  "schema": {json.dumps(document["schema"])},\n'
                    '  "schema":',
                    1,
                )
                + "\n"
            ),
            "escaped schema": lambda document: (
                json.dumps(document, indent=2, sort_keys=True).replace(
                    document["schema"],
                    r"\u0063" + document["schema"][1:],
                    1,
                )
                + "\n"
            ),
            "missing artifact": lambda document: (
                json.dumps(
                    {**document, "artifacts": document["artifacts"][:-1]},
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ),
            "unknown artifact": lambda document: (
                json.dumps(
                    {
                        **document,
                        "artifacts": [
                            (
                                {**artifact, "name": "unknown"}
                                if index == 0
                                else artifact
                            )
                            for index, artifact in enumerate(document["artifacts"])
                        ],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ),
            "v2 artifact in a v1 manifest": v2_artifact_in_v1_manifest,
            "extra artifact field": lambda document: (
                json.dumps(
                    {
                        **document,
                        "artifacts": [
                            (
                                {**artifact, "note": "unchecked"}
                                if index == 0
                                else artifact
                            )
                            for index, artifact in enumerate(document["artifacts"])
                        ],
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ),
            "extra top-level field": lambda document: (
                json.dumps(
                    {**document, "unchecked": True}, indent=2, sort_keys=True
                )
                + "\n"
            ),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix=".cupidbuild-object-manifest-contract-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                manifest = self._copy_checked_assembly_seed(root / "seed")
                source = root / "input.asm"
                output = root / "output.o"
                source.write_text(
                    "bits 32\nsection .text\nret\n", encoding="utf-8"
                )
                output.write_bytes(b"last known good object")
                original = output.read_bytes()
                document = json.loads(manifest.read_text(encoding="utf-8"))
                manifest.write_text(mutate(document), encoding="utf-8")

                result = self._run_object(source, output, manifest)

                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertIn("checked seed manifest is invalid", result.stderr)
                self.assertEqual(output.read_bytes(), original)

    @unittest.skipIf(os.name == "nt", "Linux build-plan contract")
    def test_linux_build_plan_drift_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-build-plan-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            original = output.read_bytes()
            self._rewrite_manifest(
                manifest,
                lambda document: document["build_plan"].__setitem__(
                    "workers", 1
                ),
            )

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked seed manifest is invalid", result.stderr)
            self.assertIn("build plan differs", result.stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_function_anchor_inside_an_instruction_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-code-anchor-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\n"
                "section .text\n"
                "global entry:function\n"
                "global inside:function\n"
                "entry: db 0xb8\n"
                "inside: dd 0\n"
                "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            original = output.read_bytes()

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("checked CupidDis failed", result.stderr)
            self.assertEqual(output.read_bytes(), original)

    def test_success_and_failure_remove_every_new_private_candidate(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-cleanup-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")

            success = self._run_object(source, output)
            source.write_text("bits 32\nnot assembly\n", encoding="utf-8")
            failure = self._run_object(source, output)

            self.assertEqual(success.returncode, 0, success.stderr)
            self.assertNotEqual(failure.returncode, 0)
            self.assertEqual(self._private_roots(), before)

    def test_fixed_point_raw_fixtures_satisfy_the_public_operations(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-fixed-point-raw-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            cases = (
                (
                    "assemble-bootloader",
                    "guarded-bootloader.asm",
                    _CUPIDBUILD_BOOTLOADER_BEHAVIOR_SOURCE,
                    2560,
                ),
                (
                    "assemble-smp-trampoline",
                    "guarded-smp-trampoline.S",
                    _CUPIDBUILD_SMP_BEHAVIOR_SOURCE,
                    4096,
                ),
            )
            for operation, source_name, contents, expected_size in cases:
                source = root / source_name
                output = root / f"{operation}.bin"
                source.write_text(contents, encoding="ascii", newline="\n")

                result = self._run_assembly(operation, source, output)

                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(result.stderr, "")
                self.assertEqual(output.stat().st_size, expected_size)

    def test_assembler_failure_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-assembler-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nthis is not assembly\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CupidASM", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_unrelated_seed_file_keeps_the_checked_contract(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-unrelated-seed-file-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            (seed / "README.txt").write_text("not executable\n", encoding="utf-8")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_inspector_failure_is_reported_and_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-inspector-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            suffix = ".exe" if os.name == "nt" else ".elf"
            self._replace_seed_tool(
                manifest,
                "cupiddis",
                self._production_manifest().parent / f"cupidobj{suffix}",
            )
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertEqual(result.returncode, 1)
            self.assertIn("checked CupidDis failed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_checked_tool_digest_mismatch_fails_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-seed-mismatch-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            self._copy_checked_assembly_seed(seed)
            suffix = ".exe" if os.name == "nt" else ".elf"
            assembler = seed / f"cupidasm{suffix}"
            payload = bytearray(assembler.read_bytes())
            payload[-1] ^= 0x01
            assembler.write_bytes(payload)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, seed / "manifest.json")

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked CupidASM digest mismatch", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_unknown_opcode_is_rejected_by_the_checked_inspector(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-unknown-opcode-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\ndb 0x0f, 0xff\n", encoding="utf-8"
            )
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CupidDis", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_nonlocal_direct_target_is_rejected_by_the_checked_inspector(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-nonlocal-target-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\ndb 0xe8, 0xff, 0xff, 0xff, 0x7f\nret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CupidDis", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_source_link_is_rejected_without_touching_the_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-source-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            target = root / "target.asm"
            source = root / "input.asm"
            output = root / "output.o"
            target.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            try:
                source.symlink_to(target.name)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_output_hardlink_to_source_is_rejected_as_an_alias(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-output-alias-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            os.link(source, output)
            original = source.read_bytes()

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("may not replace an input", result.stderr)
            self.assertEqual(source.read_bytes(), original)

    def test_output_link_is_rejected_without_touching_its_target(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-output-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            target = root / "target.o"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            target.write_bytes(b"last known good object")
            try:
                output.symlink_to(target.name)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_bytes(), b"last known good object")

    def test_checked_tool_link_is_rejected_before_execution(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-tool-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            suffix = ".exe" if os.name == "nt" else ".elf"
            assembler = seed / f"cupidasm{suffix}"
            linked = seed / f"linked-cupidasm{suffix}"
            assembler.rename(linked)
            try:
                assembler.symlink_to(linked.name)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_live_publication_lock_rejects_a_competing_publisher(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-lock-contention-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            lock.write_bytes(f"{os.getpid()}\n".encode("ascii"))

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("live process", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")
            self.assertTrue(lock.is_file())

    def test_stale_publication_lock_is_reclaimed_and_removed(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-stale-lock-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            lock.write_bytes(b"4294967294\n")

            result = self._run_object(source, output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(lock.exists())
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_occupied_stale_recovery_path_is_preserved(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-occupied-recovery-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            recovery = Path(str(lock) + ".reclaim-fffffffe")
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            lock.write_bytes(b"4294967294\n")
            recovery.write_bytes(b"belongs to another recovery")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("could not enter stale recovery", result.stderr)
            self.assertEqual(lock.read_bytes(), b"4294967294\n")
            self.assertEqual(recovery.read_bytes(), b"belongs to another recovery")
            self.assertEqual(output.read_bytes(), b"last known good object")

    @unittest.skipIf(os.name == "nt", "Linux hard-link recovery protocol")
    def test_occupied_stale_commit_path_is_preserved(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-occupied-commit-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            recovery = Path(str(lock) + ".reclaim-fffffffe")
            committed = Path(str(recovery) + ".commit")
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            lock.write_bytes(b"4294967294\n")
            committed.write_bytes(b"belongs to another recovery commit")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("changed during stale recovery", result.stderr)
            self.assertEqual(lock.read_bytes(), b"4294967294\n")
            self.assertFalse(recovery.exists())
            self.assertEqual(
                committed.read_bytes(), b"belongs to another recovery commit"
            )
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_malformed_publication_lock_is_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-malformed-lock-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            lock.write_bytes(b"not an owner\n")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not a regular owner file", result.stderr)
            self.assertEqual(lock.read_bytes(), b"not an owner\n")
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_replaced_publication_lock_preserves_the_new_owner_and_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-replaced-lock-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            replacement = root / "replacement.lock"
            source.write_text(
                "bits 32\nsection .text\n" + "nop\n" * 10000 + "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            replacement_owner = f"{os.getpid()}\n".encode("ascii")
            replacement.write_bytes(replacement_owner)
            before = self._private_roots()
            changed = threading.Event()

            def replace_lock_after_transaction_opens():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if lock.is_file() and any(
                        (path / "source.asm").is_file() for path in new_roots
                    ):
                        os.replace(replacement, lock)
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_lock_after_transaction_opens, daemon=True
            )
            mutator.start()
            result = self._run_object(source, output)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "publication lock was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("publication lock changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")
            self.assertEqual(lock.read_bytes(), replacement_owner)

    def test_occupied_private_candidate_is_left_untouched(self):
        occupied = None
        for attempt in range(4096):
            candidate = REPO_ROOT / f".cupidbuild-object-{attempt:08x}"
            try:
                candidate.mkdir()
            except FileExistsError:
                continue
            occupied = candidate
            break
        self.assertIsNotNone(occupied, "no private candidate slot was available")
        sentinel = occupied / "belongs-to-another-publisher"
        sentinel.write_bytes(b"keep")
        try:
            with tempfile.TemporaryDirectory(
                prefix=".cupidbuild-object-occupied-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                source = root / "input.asm"
                output = root / "output.o"
                source.write_text(
                    "bits 32\nsection .text\nret\n", encoding="utf-8"
                )

                result = self._run_object(source, output)

                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(sentinel.read_bytes(), b"keep")
        finally:
            shutil.rmtree(occupied)

    def test_source_drift_after_freeze_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-source-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            original_source = (
                "bits 32\nsection .text\nglobal entry\nentry:\n"
                + "nop\n" * 10000
                + "ret\n"
            )
            source.write_text(original_source, encoding="utf-8")
            output.write_bytes(b"last known good object")
            changed = threading.Event()
            existing_private_roots = set(
                REPO_ROOT.glob(".cupidbuild-object-[0-9a-f]*")
            )

            def replace_source_when_transaction_opens():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    private_sources = [
                        path / "source.asm"
                        for path in REPO_ROOT.glob(".cupidbuild-object-[0-9a-f]*")
                        if path not in existing_private_roots
                    ]
                    if any(path.is_file() for path in private_sources):
                        source.write_text(
                            original_source + "; live source changed\n",
                            encoding="utf-8",
                        )
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_source_when_transaction_opens, daemon=True
            )
            mutator.start()
            result = self._run_object(source, output)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "transaction was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("source changed while checked tools ran", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_raw_source_drift_reports_the_image_transaction(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-raw-source-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.bin"
            original_source = (
                "bits 16\norg 0x7c00\n"
                + "nop\ndb 0\n" * 1279
                + "nop\nret\n"
            )
            source.write_text(original_source, encoding="ascii")
            output.write_bytes(b"last known good raw image")
            before = self._private_roots()
            changed = threading.Event()

            def replace_source_when_transaction_opens():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / "source.asm").is_file() for path in new_roots):
                        source.write_text(
                            original_source + "; live source changed\n",
                            encoding="ascii",
                        )
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_source_when_transaction_opens, daemon=True
            )
            mutator.start()
            result = self._run_assembly(
                "assemble-bootloader", source, output
            )
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "transaction was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "source changed while checked tools ran", result.stderr
            )
            self.assertNotIn("object source", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good raw image")

    def test_raw_map_drift_during_inspection_preserves_the_previous_image(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-raw-map-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.bin"
            source.write_text(
                "bits 16\norg 0x7c00\n"
                + "nop\ndb 0\n" * 1279
                + "nop\nret\n",
                encoding="ascii",
            )
            output.write_bytes(b"last known good raw image")
            before = self._private_roots()
            changed = threading.Event()

            def touch_map_after_inspector_starts():
                deadline = time.monotonic() + 20
                private_root = None
                map_path = None
                stdout_path = None
                initial_stdout = None
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    for candidate_root in new_roots:
                        candidate_map = candidate_root / "candidate.map"
                        candidate_stdout = candidate_root / "tool.stdout"
                        if not candidate_map.is_file():
                            continue
                        private_root = candidate_root
                        map_path = candidate_map
                        stdout_path = candidate_stdout
                        try:
                            status = stdout_path.stat()
                            initial_stdout = (
                                status.st_ino,
                                status.st_ctime_ns,
                                status.st_mtime_ns,
                            )
                        except FileNotFoundError:
                            initial_stdout = None
                        break
                    if private_root is not None:
                        break
                    time.sleep(0.001)
                while time.monotonic() < deadline and map_path is not None:
                    try:
                        status = stdout_path.stat()
                        current_stdout = (
                            status.st_ino,
                            status.st_ctime_ns,
                            status.st_mtime_ns,
                        )
                    except FileNotFoundError:
                        time.sleep(0.001)
                        continue
                    if initial_stdout is not None and current_stdout == initial_stdout:
                        time.sleep(0.001)
                        continue
                    map_status = map_path.stat()
                    os.utime(
                        map_path,
                        ns=(
                            map_status.st_atime_ns,
                            map_status.st_mtime_ns + 2_000_000_000,
                        ),
                    )
                    changed.set()
                    return

            mutator = threading.Thread(
                target=touch_map_after_inspector_starts,
                daemon=True,
            )
            mutator.start()
            result = self._run_assembly(
                "assemble-bootloader", source, output
            )
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "CupidDis map read was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "private output changed while validation ran",
                result.stderr,
            )
            self.assertEqual(output.read_bytes(), b"last known good raw image")
            self.assertEqual(self._private_roots(), before)

    def test_checked_tool_drift_after_freeze_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-tool-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            suffix = ".exe" if os.name == "nt" else ".elf"
            assembler = seed / f"cupidasm{suffix}"
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\n" + "nop\n" * 10000 + "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def change_tool_after_freeze():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / assembler.name).is_file() for path in new_roots):
                        with assembler.open("ab") as stream:
                            stream.write(b"drift")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(target=change_tool_after_freeze, daemon=True)
            mutator.start()
            result = self._run_object(source, output, manifest)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "frozen checked tool was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_seed_membership_drift_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-seed-membership-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            suffix = ".exe" if os.name == "nt" else ".elf"
            assembler = seed / f"cupidasm{suffix}"
            unlisted = seed / f"unlisted{suffix}"
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\n" + "nop\n" * 10000 + "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def add_peer_after_freeze():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / assembler.name).is_file() for path in new_roots):
                        unlisted.write_bytes(b"not trusted")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(target=add_peer_after_freeze, daemon=True)
            mutator.start()
            result = self._run_object(source, output, manifest)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "frozen checked tool was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("directory membership changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_failed_tool_still_rechecks_seed_membership(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-failed-tool-membership-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            suffix = ".exe" if os.name == "nt" else ".elf"
            assembler = seed / f"cupidasm{suffix}"
            unlisted = seed / f"unlisted{suffix}"
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\n"
                + "nop\n" * 10000
                + "not_an_instruction\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def add_peer_after_freeze():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / assembler.name).is_file() for path in new_roots):
                        unlisted.write_bytes(b"not trusted")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(target=add_peer_after_freeze, daemon=True)
            mutator.start()
            result = self._run_object(source, output, manifest)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "frozen checked tool was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("directory membership changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_seed_manifest_drift_after_freeze_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-manifest-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\n" + "nop\n" * 10000 + "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def change_manifest_after_freeze():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / "manifest.json").is_file() for path in new_roots):
                        with manifest.open("ab") as stream:
                            stream.write(b"\n")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_manifest_after_freeze, daemon=True
            )
            mutator.start()
            result = self._run_object(source, output, manifest)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "frozen seed manifest was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_destination_drift_preserves_the_competing_bytes(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-destination-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text(
                "bits 32\nsection .text\n" + "nop\n" * 10000 + "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def replace_destination_after_assembly():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / "candidate.o").is_file() for path in new_roots):
                        output.write_bytes(b"competing publisher")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_destination_after_assembly, daemon=True
            )
            mutator.start()
            result = self._run_object(source, output)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "private object was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"competing publisher")

    def test_output_parent_drift_preserves_the_original_parent(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-parent-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            parent = root / "objects"
            displaced = root / "objects-before-drift"
            parent.mkdir()
            output = parent / "output.o"
            source.write_text(
                "bits 32\nsection .text\n" + "nop\n" * 10000 + "ret\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()

            def replace_parent_after_assembly():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any((path / "candidate.o").is_file() for path in new_roots):
                        parent.rename(displaced)
                        parent.mkdir()
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(target=replace_parent_after_assembly, daemon=True)
            mutator.start()
            result = self._run_object(source, output)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "private object was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output parent changed", result.stderr)
            self.assertEqual(
                (displaced / "output.o").read_bytes(), b"last known good object"
            )
            self.assertFalse(output.exists())

    @unittest.skipUnless(os.name == "nt", "Windows replacement semantics")
    def test_replacement_failure_rolls_back_and_the_next_run_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-replacement-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            output.chmod(stat.S_IREAD)
            try:
                failed = self._run_object(source, output)

                self.assertNotEqual(failed.returncode, 0)
                self.assertIn("could not be published", failed.stderr)
                self.assertEqual(output.read_bytes(), b"last known good object")
            finally:
                output.chmod(stat.S_IREAD | stat.S_IWRITE)

            recovered = self._run_object(source, output)

            self.assertEqual(recovered.returncode, 0, recovered.stderr)
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")


if __name__ == "__main__":
    unittest.main()

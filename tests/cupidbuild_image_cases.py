## @file
#  @brief Check JPEG wrapping, symbol generation, and kernel flattening.
import json
import os
import shutil
import struct
import subprocess
import tempfile
import threading
import time
from tests.c_source_spelling import _c_spelling_ranges
from pathlib import Path

from tests.cupidbuild_cli_fixtures import BASELINE_JPEG, REPO_ROOT, TOOLCHAIN_ROOT


class ImagePublicationCases:
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
            ("colon", b"kernel/a:b.o\n", "code input path is unsafe"),
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

    def test_typed_kernel_flatten_rejects_an_oversized_initialized_span(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-kernel-flat-span-", dir=REPO_ROOT
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
            image = bytearray(built.read_bytes())
            program_offset = struct.unpack_from("<I", image, 28)[0]
            program_size = struct.unpack_from("<H", image, 42)[0]
            program_count = struct.unpack_from("<H", image, 44)[0]
            self.assertGreaterEqual(program_count, 2)
            self.assertEqual(
                struct.unpack_from("<I", image, program_offset)[0], 1
            )
            first_program = image[
                program_offset : program_offset + program_size
            ]
            second_program = program_offset + program_size
            image[
                second_program : second_program + program_size
            ] = first_program
            first_address = struct.unpack_from(
                "<I", image, program_offset + 12
            )[0]
            second_address = first_address + 64 * 1024 * 1024
            struct.pack_into("<I", image, second_program + 8, second_address)
            struct.pack_into("<I", image, second_program + 12, second_address)
            struct.pack_into("<I", image, second_program + 24, 4)
            struct.pack_into("<I", image, second_program + 28, 1)
            (kernel / "kernel.elf.pass1").write_bytes(image)
            (kernel / "kernel.elf").write_bytes(image)
            manifest = root / "code-inputs.txt"
            manifest.write_text(
                "kernel/kernel.elf.pass1\nkernel/kernel.elf\n",
                encoding="ascii",
                newline="\n",
            )
            output = kernel / "kernel.bin"
            sentinel = b"last known good flat kernel"
            output.write_bytes(sentinel)

            result = self._run_flatten_kernel(
                root, manifest, output, seed_manifest
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "independent flat kernel validation failed: "
                "flat kernel exceeds the 64 MiB transaction limit",
                result.stderr,
            )
            self.assertEqual(output.read_bytes(), sentinel)

    def test_typed_kernel_flatten_keeps_an_independent_byte_parity_gate(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild_kernel_image.inc").read_text(encoding="utf-8")
        operation = source.split(
            "int cupidbuild_flatten_kernel(", 1
        )[1]

        independent_render = operation.index("cupidbuild_render_flat_image(")
        object_transforms = _c_spelling_ranges(
            operation,
            "status = cupidbuild_host_run_in_private(transaction, seed.frozen_tools[4]",
        )
        self.assertEqual(len(object_transforms), 1)
        object_transform = object_transforms[0][0]
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
        self.assertTrue(_c_spelling_ranges(operation, "linked_arguments, 600000u"))
        self.assertNotIn("CUPIDBUILD_CODE_BATCH", source)

    def test_typed_kernel_flatten_names_its_manifest_in_the_public_request(self):
        header = (TOOLCHAIN_ROOT / "cupidbuild.h").read_text(encoding="utf-8")
        main = (TOOLCHAIN_ROOT / "cupidbuild_main.cc").read_text(
            encoding="utf-8"
        )
        source = (TOOLCHAIN_ROOT / "cupidbuild_kernel_image.inc").read_text(encoding="utf-8")

        self.assertIn("const char *input_manifest;", header)
        self.assertNotIn(
            "typedef cupidbuild_assembly_request_t cupidbuild_kernel_request_t;",
            header,
        )
        self.assertIn("&kernel_request.input_manifest", main)
        self.assertIn("request->input_manifest", source)

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
        source = (TOOLCHAIN_ROOT / "cupidbuild_kernel_image.inc").read_text(encoding="utf-8")
        operation = source.split(
            "int cupidbuild_generate_ksyms(", 1
        )[1].split("int cupidbuild_flatten_kernel(", 1)[0]

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
            '--root "$(CURDIR)" --seed-release $(PRODUCTION_SEED_RELEASE) '
            '--source $< --output $@',
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
                    '--root "$(CURDIR)" --seed-release $(PRODUCTION_SEED_RELEASE) '
                    '--source $< --output $@',
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
                    if any(
                        self._private_entry(token, "candidate.o").is_file()
                        for token in new_roots
                    ):
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
                        self._private_candidate_has_bytes(token)
                        for token in new_roots
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
            blocked = threading.Event()

            def replace_output_after_wrapping():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if any(
                        self._private_entry(token, "candidate.o").is_file()
                        for token in new_roots
                    ):
                        try:
                            output.write_bytes(b"competing publisher")
                        except PermissionError:
                            blocked.set()
                            return
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_output_after_wrapping, daemon=True
            )
            mutator.start()
            result = self._run_embed_jpeg(source, output)
            mutator.join(timeout=20)

            self.assertTrue(
                changed.is_set() or blocked.is_set(),
                "checked JPEG output was not observed",
            )
            if os.name == "nt":
                self.assertTrue(blocked.is_set())
                if result.returncode == 0:
                    self.assertNotEqual(
                        output.read_bytes(), b"last known good object"
                    )
                else:
                    self.assertIn("cupidobj: cannot write", result.stderr)
                    self.assertIn("checked CupidObj failed", result.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"last known good object"
                    )
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "output changed while checked tools ran", result.stderr
                )
                self.assertNotIn("code output", result.stderr)
                self.assertEqual(output.read_bytes(), b"competing publisher")
            self.assertEqual(self._private_roots(), before)

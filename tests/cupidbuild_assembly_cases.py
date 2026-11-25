## @file
#  @brief Check guarded assembly and raw image publication.
import hashlib
import os
import shutil
import stat
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from tools.bootstrap_toolchain import (
    _CUPIDBUILD_BOOTLOADER_BEHAVIOR_SOURCE,
    _CUPIDBUILD_ISO_PATTERN_BEHAVIOR_SOURCE,
    _CUPIDBUILD_SMP_BEHAVIOR_SOURCE,
)

from tests.cupidbuild_cli_fixtures import REPO_ROOT


class AssemblyPublicationCases:
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
        self.assertIn("cupidbuild assemble-iso-pattern", result.stdout)

    def test_iso_pattern_matches_active_source_and_preserves_timestamp(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-iso-pattern-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "big.bin"
            source = REPO_ROOT / "test_iso" / "big_pattern.asm"
            expected = bytes(range(256)) * 16
            for previous in (None, b"previous ISO pattern", expected):
                with self.subTest(
                    existing_size=None if previous is None else len(previous)
                ):
                    if previous is not None:
                        output.write_bytes(previous)
                        os.utime(output, ns=(1_600_000_000_000_000_000,) * 2)
                        previous_mtime = output.stat().st_mtime_ns
                    result = self._run_assembly(
                        "assemble-iso-pattern", source, output
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "")
                    self.assertEqual(output.read_bytes(), expected)
                    if previous == expected:
                        self.assertEqual(output.stat().st_mtime_ns, previous_mtime)
        self.assertEqual(self._private_roots(), before)

    def test_iso_pattern_rejects_size_bytes_layout_and_assembly_failures(self):
        cases = (
            ("bits 32\norg 0\ntimes 4095 db $\n", "raw output validation failed"),
            ("bits 32\norg 0\ntimes 4097 db $\n", "raw output validation failed"),
            ("bits 32\norg 0\ntimes 4096 db 0\n", "ISO pattern differs"),
            ("bits 32\norg 256\ntimes 4096 db $\n", "ISO pattern layout policy"),
            (
                "bits 32\norg 0\nadd byte [ecx], al\ntimes 4094 db $\n",
                "ISO pattern layout policy",
            ),
            ("bits 32\ninvalid_instruction\n", "checked CupidASM failed"),
        )
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-iso-pattern-invalid-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "pattern.asm"
            output = root / "big.bin"
            for contents, diagnostic in cases:
                with self.subTest(diagnostic=diagnostic, contents=contents):
                    source.write_text(contents, encoding="ascii")
                    output.write_bytes(b"previous ISO pattern")
                    previous_mtime = output.stat().st_mtime_ns
                    result = self._run_assembly(
                        "assemble-iso-pattern", source, output
                    )
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn(diagnostic, result.stderr)
                    self.assertEqual(output.read_bytes(), b"previous ISO pattern")
                    self.assertEqual(output.stat().st_mtime_ns, previous_mtime)
        self.assertEqual(self._private_roots(), before)

    def test_iso_pattern_rejects_a_live_lock_and_source_alias(self):
        before = self._private_roots()
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-iso-pattern-guard-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "pattern.asm"
            output = root / "big.bin"
            contents = b"bits 32\norg 0\ntimes 4096 db $\n"
            source.write_bytes(contents)
            output.write_bytes(b"previous ISO pattern")
            lock = Path(str(output) + ".cupidbuild.lock")
            lock.write_text(str(os.getpid()) + "\n", encoding="ascii")
            result = self._run_assembly("assemble-iso-pattern", source, output)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("lock", result.stderr)
            self.assertEqual(output.read_bytes(), b"previous ISO pattern")
            self.assertTrue(lock.exists())
            result = self._run_assembly("assemble-iso-pattern", source, source)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn("output may not replace an input", result.stderr)
            self.assertEqual(source.read_bytes(), contents)
        self.assertEqual(self._private_roots(), before)

    @unittest.skipIf(os.name == "nt", "Windows retains the output parent against rename")
    def test_iso_pattern_preserves_recovery_when_the_output_parent_is_replaced(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-iso-post-install-parent-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            stable_parent_lookup = self._retained_directory_lookup_survives_rename(root)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "pattern.asm"
            source.write_text("bits 32\norg 0\ntimes 4096 db $\n", encoding="ascii")
            output_parent = root / "artifacts"
            output_parent.mkdir()
            output = output_parent / "pattern.bin"
            previous = b"previous ISO pattern"
            output.write_bytes(previous)
            os.utime(output, ns=(1_600_000_000_000_000_000,) * 2)
            previous_stat = output.stat()
            displaced = root / "displaced-artifacts"
            ready = root / "publication-ready"
            resume = root / "publication-resume"
            marker = output_parent / "foreign-successor.txt"
            changed = threading.Event()
            mutation_errors = []
            foreign_parent_stat = []

            def replace_parent_after_candidate_install():
                try:
                    deadline = time.monotonic() + 30
                    while not ready.is_file():
                        if time.monotonic() >= deadline:
                            raise AssertionError("candidate installation was not observed")
                        time.sleep(0.001)
                    output_parent.rename(displaced)
                    output_parent.mkdir()
                    marker.write_bytes(b"foreign successor")
                    foreign_parent_stat.append(output_parent.stat())
                    changed.set()
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-install"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            mutator = threading.Thread(target=replace_parent_after_candidate_install, daemon=True)
            mutator.start()
            command = self._assembly_command(
                "assemble-iso-pattern", source, output,
                manifest=manifest, root=root,
            )
            command[0] = str(self.race_cli_path)
            result = subprocess.run(
                command, cwd=root, text=True, capture_output=True,
                timeout=90, env=environment,
            )
            mutator.join(timeout=30)

            self.assertFalse(mutator.is_alive(), "the parent mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertTrue(changed.is_set())
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertRegex(result.stderr, "could not be published|output parent changed")
            self.assertIn("transaction cleanup failed", result.stderr)
            self.assertEqual(marker.read_bytes(), b"foreign successor")
            self._assert_replaced_parent_recovery(
                root, output, displaced, previous, previous_stat,
                stable_parent_lookup, foreign_parent_stat[0],
            )
            if not stable_parent_lookup:
                self.assertEqual((displaced / output.name).read_bytes(), bytes(range(256)) * 16)
            marker.unlink()
            output_parent.rmdir()
            displaced.rename(output_parent)

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

    @unittest.skipIf(os.name == "nt", "DrvFS fallback is POSIX-only")
    def test_ambiguous_noreplace_fallback_preserves_recovery_evidence(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-noreplace-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            ready = root / "noreplace-ready"
            resume = root / "noreplace-resume"
            replacement = b"foreign replacement\n"
            mutation_errors = []
            source.write_text(
                "bits 32\nsection .text\nglobal entry\nentry: nop\nret\n",
                encoding="ascii",
            )
            environment = os.environ.copy()
            environment["CUPIDBUILD_NOREPLACE_TEST_DESTINATION"] = output.name
            environment["CUPIDBUILD_NOREPLACE_TEST_FORCE_FALLBACK"] = "1"
            environment["CUPIDBUILD_NOREPLACE_TEST_FAIL_SOURCE_UNLINK"] = "1"
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = (
                "after-noreplace-link"
            )
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            command = self._assembly_command(
                "assemble-cupidasm-object",
                source,
                output,
                manifest=manifest,
                root=root,
            )
            command[0] = str(self.race_cli_path)

            def replace_linked_candidate():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            output.unlink()
                            output.write_bytes(replacement)
                            return
                        time.sleep(0.001)
                    mutation_errors.append(
                        "no-replace checkpoint was not observed"
                    )
                except Exception as error:  # pragma: no cover - surfaced below
                    mutation_errors.append(repr(error))
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:  # pragma: no cover - surfaced below
                        mutation_errors.append(repr(error))

            mutator = threading.Thread(
                target=replace_linked_candidate, daemon=True
            )
            mutator.start()

            result = subprocess.run(
                command,
                cwd=root,
                env=environment,
                text=True,
                capture_output=True,
                timeout=90,
            )
            mutator.join(timeout=30)

            self.assertFalse(
                mutator.is_alive(), "the no-replace mutator did not stop"
            )
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "checked output could not claim the missing output",
                result.stderr,
            )
            self.assertTrue(output.is_file())
            self.assertEqual(output.read_bytes(), replacement)
            self.assertNotEqual(self._private_roots(root), set())
            self.assertTrue(Path(str(output) + ".cupidbuild.lock").is_file())

    @unittest.skipIf(os.name == "nt", "POSIX uses anonymous private artifacts")
    def test_posix_flat_transaction_names_only_publication_artifacts(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-anonymous-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            ready = root / "publication-ready"
            resume = root / "publication-resume"
            lock = Path(str(output) + ".cupidbuild.lock")
            source.write_text(
                "bits 32\nsection .text\nglobal entry\nentry: nop\nret\n",
                encoding="ascii",
            )
            output.write_bytes(b"last known good object")
            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "before-mutation"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            command = self._assembly_command(
                "assemble-cupidasm-object",
                source,
                output,
                manifest=manifest,
                root=root,
            )
            command[0] = str(self.race_cli_path)
            process = subprocess.Popen(
                command,
                cwd=root,
                env=environment,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            checkpoint_seen = False
            token_count = 0
            flat_token_seen = False
            private_entries = set()
            lock_seen = False
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline and process.poll() is None:
                if ready.is_file():
                    checkpoint_seen = True
                    tokens = list(self._private_roots(root))
                    token_count = len(tokens)
                    if token_count == 1:
                        token = tokens[0]
                        flat_token_seen = token.name.endswith(".reserve")
                        if flat_token_seen:
                            prefix = token.name[: -len(".reserve")]
                            private_entries = {
                                path.name[len(prefix) + 1 :]
                                for path in root.iterdir()
                                if path.name.startswith(prefix + ".")
                            }
                    lock_seen = lock.is_file()
                    break
                time.sleep(0.001)
            if process.poll() is None:
                resume.write_bytes(b"continue")
            stdout, stderr = process.communicate(timeout=90)

            self.assertTrue(
                checkpoint_seen,
                "the pre-publication checkpoint was not reached\n" + stderr,
            )
            self.assertEqual(token_count, 1)
            self.assertTrue(flat_token_seen, "the flat reservation was not retained")
            self.assertEqual(
                private_entries,
                {"reserve", "candidate.o", "candidate.publish"},
            )
            self.assertTrue(lock_seen, "the publication lock was not retained")
            self.assertEqual(process.returncode, 0, stderr)
            self.assertEqual((stdout, stderr), ("", ""))
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(self._private_roots(root), set())
            self.assertFalse(lock.exists())

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
            raw_invocations = (
                [arguments[0], operation, *arguments[2:]]
                for operation in (
                    "assemble-bootloader",
                    "assemble-smp-trampoline",
                    "assemble-iso-pattern",
                )
                for arguments in invocations
            )
            for arguments in raw_invocations:
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

    def test_success_and_failure_remove_every_transaction_entry(self):
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
                (
                    "assemble-iso-pattern",
                    "guarded-iso-pattern.asm",
                    _CUPIDBUILD_ISO_PATTERN_BEHAVIOR_SOURCE,
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

    @unittest.skipIf(os.name == "nt", "Windows reclaims locks by retained handle")
    def test_occupied_stale_recovery_path_is_preserved(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-occupied-recovery-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            recovery = Path(str(lock) + ".reclaim-link-fffffffe")
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
    def test_occupied_stale_recovery_move_path_is_preserved(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-occupied-commit-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.o"
            lock = Path(str(output) + ".cupidbuild.lock")
            recovery = Path(str(lock) + ".reclaim-link-fffffffe")
            moved = Path(str(lock) + ".reclaim-move-fffffffe")
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")
            lock.write_bytes(b"4294967294\n")
            moved.write_bytes(b"belongs to another recovery move")

            result = self._run_object(source, output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("could not enter stale recovery", result.stderr)
            self.assertEqual(lock.read_bytes(), b"4294967294\n")
            self.assertFalse(recovery.exists())
            self.assertEqual(
                moved.read_bytes(), b"belongs to another recovery move"
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
            blocked = threading.Event()

            def replace_lock_after_transaction_opens():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    if lock.is_file() and any(
                        self._private_entry(token, "candidate.o").is_file()
                        for token in new_roots
                    ):
                        try:
                            os.replace(replacement, lock)
                        except PermissionError:
                            blocked.set()
                            return
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=replace_lock_after_transaction_opens, daemon=True
            )
            mutator.start()
            result = self._run_object(source, output)
            mutator.join(timeout=20)

            self.assertTrue(
                changed.is_set() or blocked.is_set(),
                "publication lock was not observed",
            )
            if os.name == "nt":
                self.assertTrue(blocked.is_set())
                if result.returncode == 0:
                    self.assertNotEqual(
                        output.read_bytes(), b"last known good object"
                    )
                else:
                    self.assertIn("usage: cupidasm", result.stderr)
                    self.assertIn("checked CupidASM failed", result.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"last known good object"
                    )
                self.assertFalse(lock.exists())
                self.assertEqual(replacement.read_bytes(), replacement_owner)
            else:
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
            existing_private_roots = self._private_roots()

            def replace_source_when_transaction_opens():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    private_sources = [
                        self._private_entry(token, "candidate.o")
                        for token in self._private_roots()
                        if token not in existing_private_roots
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
                    if any(
                        self._private_entry(token, "candidate.o").is_file()
                        for token in new_roots
                    ):
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

    @unittest.skipUnless(os.name == "nt", "POSIX keeps the private map anonymous")
    def test_windows_raw_map_drift_during_inspection_preserves_the_previous_image(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-raw-map-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            source = root / "input.asm"
            output = root / "output.bin"
            source.write_text(
                "bits 16\norg 0x7c00\n"
                + "; wait\n" * 500_000
                + "nop\ndb 0\n" * 1279
                + "nop\nret\n",
                encoding="ascii",
            )
            output.write_bytes(b"last known good raw image")
            before = self._private_roots()
            changed = threading.Event()

            def touch_map_after_inspector_starts():
                deadline = time.monotonic() + 20
                map_path = None
                stdout_path = None
                initial_stdout = None
                while time.monotonic() < deadline:
                    new_roots = self._private_roots() - before
                    for candidate_root in new_roots:
                        candidate_map = self._private_entry(
                            candidate_root, "candidate.map"
                        )
                        candidate_stdout = self._private_entry(
                            candidate_root, "tool.stdout"
                        )
                        if not candidate_map.is_file():
                            continue
                        try:
                            status = candidate_stdout.stat()
                        except FileNotFoundError:
                            continue
                        map_path = candidate_map
                        stdout_path = candidate_stdout
                        initial_stdout = (
                            status.st_ino,
                            status.st_ctime_ns,
                            status.st_mtime_ns,
                        )
                        break
                    if initial_stdout is not None:
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
                    if current_stdout == initial_stdout:
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
                    if any(
                        self._private_candidate_has_bytes(token)
                        for token in new_roots
                    ):
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
                    if any(
                        self._private_candidate_has_bytes(token)
                        for token in new_roots
                    ):
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
                + "nop\n" * 250_000
                + "not_an_instruction\n",
                encoding="utf-8",
            )
            output.write_bytes(b"last known good object")
            before = self._private_roots()
            changed = threading.Event()
            process = None
            children_path = None

            if os.name != "nt":
                command = self._assembly_command(
                    "assemble-cupidasm-object",
                    source,
                    output,
                    manifest=manifest,
                )
                process = subprocess.Popen(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                children_path = Path(
                    f"/proc/{process.pid}/task/{process.pid}/children"
                )

            def add_peer_after_freeze():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    tool_started = False
                    if os.name == "nt":
                        new_roots = self._private_roots() - before
                        tool_started = any(
                            self._private_entry(token, assembler.name).is_file()
                            for token in new_roots
                        )
                    else:
                        try:
                            tool_started = bool(
                                children_path.read_text(encoding="ascii").split()
                            )
                        except (FileNotFoundError, PermissionError):
                            tool_started = False
                    if tool_started:
                        unlisted.write_bytes(b"not trusted")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(target=add_peer_after_freeze, daemon=True)
            mutator.start()
            if os.name == "nt":
                result = self._run_object(source, output, manifest)
            else:
                stdout, stderr = process.communicate(timeout=90)
                result = subprocess.CompletedProcess(
                    command,
                    process.returncode,
                    stdout,
                    stderr,
                )
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
            blocked = threading.Event()

            def replace_destination_after_assembly():
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
                target=replace_destination_after_assembly, daemon=True
            )
            mutator.start()
            result = self._run_object(source, output)
            mutator.join(timeout=20)

            self.assertTrue(
                changed.is_set() or blocked.is_set(),
                "private object was not observed",
            )
            if os.name == "nt":
                self.assertTrue(blocked.is_set())
                if result.returncode == 0:
                    self.assertNotEqual(
                        output.read_bytes(), b"last known good object"
                    )
                else:
                    self.assertIn("usage: cupidasm", result.stderr)
                    self.assertIn("checked CupidASM failed", result.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"last known good object"
                    )
            else:
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
            ready = root / "publication-ready"
            resume = root / "publication-resume"
            changed = threading.Event()
            blocked = threading.Event()

            def replace_parent_after_launch():
                try:
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            try:
                                parent.rename(displaced)
                            except PermissionError:
                                blocked.set()
                                return
                            parent.mkdir()
                            changed.set()
                            return
                        time.sleep(0.001)
                finally:
                    resume.write_bytes(b"continue")

            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-tool-launch"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            command = self._assembly_command("assemble-cupidasm-object", source, output)
            command[0] = str(self.race_cli_path)
            mutator = threading.Thread(target=replace_parent_after_launch, daemon=True)
            mutator.start()
            result = subprocess.run(
                command, cwd=REPO_ROOT, env=environment, text=True,
                capture_output=True, timeout=90,
            )
            mutator.join(timeout=20)
            self.assertTrue(ready.is_file(), "publication checkpoint was not observed")

            self.assertTrue(
                changed.is_set() or blocked.is_set(),
                "private object was not observed",
            )
            if os.name == "nt":
                self.assertTrue(blocked.is_set())
            if blocked.is_set():
                # DrvFS can retain Windows directory sharing restrictions too.
                self.assertFalse(displaced.exists())
                self.assertTrue(output.is_file())
                if result.returncode == 0:
                    self.assertNotEqual(
                        output.read_bytes(), b"last known good object"
                    )
                else:
                    self.assertIn("usage: cupidasm", result.stderr)
                    self.assertIn("checked CupidASM failed", result.stderr)
                    self.assertEqual(
                        output.read_bytes(), b"last known good object"
                    )
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("output parent changed", result.stderr)
                self.assertEqual(
                    (displaced / "output.o").read_bytes(),
                    b"last known good object",
                )
                self.assertFalse(output.exists())

    @unittest.skipUnless(os.name == "nt", "Windows replacement semantics")
    def test_read_only_backup_is_preserved_after_successful_replacement(self):
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
                published = self._run_object(source, output)

                self.assertEqual(published.returncode, 0, published.stderr)
                self.assertIn("was published, but transaction cleanup was incomplete",
                              published.stderr)
                self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
                backups = list(root.glob(".cupidbuild-old-*"))
                self.assertEqual(len(backups), 1)
                self.assertEqual(backups[0].read_bytes(), b"last known good object")
            finally:
                output.chmod(stat.S_IREAD | stat.S_IWRITE)
                for backup in root.glob(".cupidbuild-old-*"):
                    backup.chmod(stat.S_IREAD | stat.S_IWRITE)

            recovered = self._run_object(source, output)

            self.assertEqual(recovered.returncode, 0, recovered.stderr)
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")
            self.assertEqual(backups[0].read_bytes(), b"last known good object")

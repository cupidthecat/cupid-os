import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


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
        cls.fixture_path = build_path / ("cupidbuild-tool-fixture" + suffix)
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
        fixture_source = build_path / "cupidbuild_tool_fixture.cc"
        fixture_source.write_text(
            "#include <stdio.h>\n"
            "#include <string.h>\n"
            "int main(int argc, char **argv) {\n"
            "  if (argc > 1 && strcmp(argv[1], \"--require-known\") == 0) {\n"
            "    (void)fputs(\"fixture inspector failure\\n\", stderr);\n"
            "    return 43;\n"
            "  }\n"
            "  (void)puts(\"fixture unexpected output\");\n"
            "  return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        fixture_build = subprocess.run(
            [
                "clang" if os.name == "nt" else "cc",
                "-std=c11",
                "-Werror",
                "-x",
                "c",
                str(fixture_source),
                "-o",
                str(cls.fixture_path),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if fixture_build.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidBuild process fixture build failed\n"
                + fixture_build.stdout
                + fixture_build.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_help_names_the_guarded_object_command(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("cupidbuild assemble-cupidasm-object", result.stdout)

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

    def _production_manifest(self):
        platform = "i386-windows" if os.name == "nt" else "i386-linux"
        return REPO_ROOT / "bootstrap" / "seeds" / platform / "manifest.json"

    def _run_object(self, source, output, manifest=None):
        return subprocess.run(
            [
                str(self.cli_path),
                "assemble-cupidasm-object",
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
        for tool in ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj"):
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

    def test_successful_assembler_output_is_private_and_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-noisy-assembler-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            self._replace_seed_tool(manifest, "cupidasm", self.fixture_path)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("fixture unexpected output", result.stdout)
            self.assertEqual(output.read_bytes(), b"last known good object")

    def test_inspector_failure_is_reported_and_preserves_the_previous_object(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-inspector-failure-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed = root / "seed"
            manifest = self._copy_checked_assembly_seed(seed)
            self._replace_seed_tool(manifest, "cupiddis", self.fixture_path)
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertEqual(result.returncode, 1)
            self.assertIn("fixture inspector failure", result.stderr)
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
            self.assertIn("code output changed", result.stderr)
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

## @file
#  @brief Check seed metadata, membership, and historical contracts.
import json
import os
import tempfile
import unittest
from pathlib import Path

from tests.cupidbuild_cli_fixtures import REPO_ROOT


class SeedContractCases:
    def _assert_historical_seed_execution(self, result, output):
        if os.name == "nt":
            # Metadata acceptance reaches the profile check. Borrowed UTF-8
            # executables do not satisfy this historical ANSI profile.
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr,
                             "cupidbuild: checked seed execution profile mismatch\n")
            self.assertEqual(output.read_bytes(), b"last known good object")
        else:
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((result.stdout, result.stderr), ("", ""))
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_historical_v2_metadata_retains_its_execution_profile(self):
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

            self._assert_historical_seed_execution(result, output)

    def test_historical_v2_metadata_accepts_59_and_61_source_inputs(self):
        for source_input_count in (59, 61):
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
                self._assert_historical_seed_execution(result, output)

    def test_historical_v2_metadata_accepts_the_reviewed_parent(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-v2-active-parent-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            document = self._promote_seed_contract(manifest)
            revision = "142a9737f618ab8500308576a1c222501d639e5f"
            if os.name == "nt":
                document["provenance"].update(
                    {
                        "parent_execution_seed_manifest_sha256": (
                            "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd"
                        ),
                        "parent_execution_seed_source_revision": revision,
                        "parent_plan_seed_manifest_sha256": (
                            "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52"
                        ),
                        "parent_plan_seed_source_revision": revision,
                    }
                )
            else:
                document["provenance"].update(
                    {
                        "parent_seed_manifest_sha256": (
                            "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52"
                        ),
                        "parent_seed_source_revision": revision,
                    }
                )
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

            self.assertNotIn("fixed-point provenance differs", result.stderr)
            self._assert_historical_seed_execution(result, output)

    @unittest.skipUnless(os.name == "nt", "native Windows seed contract")
    def test_six_tool_v2_contract_accepts_the_current_windows_plan(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-v2-current-plan-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            source = root / "input.asm"
            output = root / "output.o"
            source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
            output.write_bytes(b"last known good object")

            result = self._run_object(source, output, manifest)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((result.stdout, result.stderr), ("", ""))
            self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_six_tool_v2_contract_rejects_retired_16a_parent(self):
        modes = ("execution", "plan", "both") if os.name == "nt" else ("both",)
        for mode in modes:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(
                prefix=".cupidbuild-retired-16a-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                manifest = self._copy_checked_assembly_seed(root / "seed")
                document = self._promote_seed_contract(manifest)
                provenance = document["provenance"]
                retired = "16a86f5b1693e017c36c6d902df9946c5d674b17"
                linux = "d16626ec2dc1fde37114b080e8e855022a4d5ac768eddb3777862ce24ad3ac9d"
                windows = "bd4d5435301972fba4ba55e0edfe7451a876fd56b3dbe73fc60a4deca61e43dc"
                if os.name == "nt":
                    if mode in ("execution", "both"):
                        provenance.update(parent_execution_seed_source_revision=retired,
                                          parent_execution_seed_manifest_sha256=windows)
                    if mode in ("plan", "both"):
                        provenance.update(parent_plan_seed_source_revision=retired,
                                          parent_plan_seed_manifest_sha256=linux)
                else:
                    provenance.update(parent_seed_source_revision=retired,
                                      parent_seed_manifest_sha256=linux)
                manifest.write_text(json.dumps(document), encoding="utf-8")
                source = root / "input.asm"
                source.write_text("bits 32\nsection .text\nret\n", encoding="ascii")
                output = root / "output.o"
                output.write_bytes(b"retained object")
                old_time = output.stat().st_mtime_ns
                result = self._run_object(source, output, manifest)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("fixed-point provenance differs", result.stderr)
                self.assertEqual(output.read_bytes(), b"retained object")
                self.assertEqual(output.stat().st_mtime_ns, old_time)

    def test_historical_v1_contract_survives_v2_parent_rotation(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-historical-v1-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            document = json.loads(manifest.read_text(encoding="utf-8"))
            legacy = self._legacy_seed_contract(document)
            self._select_fixture_release(manifest, None)
            for artifact in document["artifacts"]:
                if artifact["name"] == "cupidbuild":
                    (manifest.parent / artifact["file"]).unlink()
            manifest.write_text(json.dumps(legacy), encoding="utf-8")
            source = root / "input.asm"
            source.write_text("bits 32\nsection .text\nret\n", encoding="ascii")
            output = root / "output.o"
            output.write_bytes(b"retained historical object")
            result = self._run_object(source, output, manifest)
            # This synthetic manifest probes the historical parser, not an
            # authentic v1 tool cohort. Windows also checks its old PE profile.
            if os.name == "nt":
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stderr,
                                 "cupidbuild: checked seed execution profile mismatch\n")
                self.assertEqual(output.read_bytes(), b"retained historical object")
            else:
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

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

        def mix_parent_generations(document):
            if os.name == "nt":
                document["provenance"][
                    "parent_execution_seed_manifest_sha256"
                ] = (
                    "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd"
                )
            else:
                document["provenance"]["parent_seed_manifest_sha256"] = (
                    "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52"
                )

        def use_retired_v1_execution_parent(document):
            if os.name == "nt":
                document["provenance"].update(
                    {
                        "parent_execution_seed_manifest_sha256": (
                            "751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef"
                        ),
                        "parent_execution_seed_source_revision": (
                            "a17c9465911da41d59b7ada71733d36c39faa5ea"
                        ),
                    }
                )
            else:
                document["provenance"].update(
                    {
                        "parent_seed_manifest_sha256": (
                            "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b"
                        ),
                        "parent_seed_source_revision": (
                            "a17c9465911da41d59b7ada71733d36c39faa5ea"
                        ),
                    }
                )

        def use_retired_v2_execution_parent(document):
            if os.name == "nt":
                document["provenance"].update(
                    {
                        "parent_execution_seed_manifest_sha256": (
                            "e7e65908eb03eec43e44e2946b395723b164f5701d980aae8ffaaf1006c3d7e4"
                        ),
                        "parent_execution_seed_source_revision": (
                            "0232cb57aad5d6bdfd7bd77499762514b2f0ebfd"
                        ),
                    }
                )
            else:
                document["provenance"].update(
                    {
                        "parent_seed_manifest_sha256": (
                            "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
                        ),
                        "parent_seed_source_revision": (
                            "0232cb57aad5d6bdfd7bd77499762514b2f0ebfd"
                        ),
                    }
                )

        def use_retired_v2_parent_generation(document):
            use_retired_v2_execution_parent(document)
            if os.name == "nt":
                document["provenance"].update(
                    {
                        "parent_plan_seed_manifest_sha256": (
                            "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
                        ),
                        "parent_plan_seed_source_revision": (
                            "0232cb57aad5d6bdfd7bd77499762514b2f0ebfd"
                        ),
                    }
                )

        def use_retired_9d2529_execution_parent(document):
            if os.name == "nt":
                document["provenance"].update(
                    {
                        "parent_execution_seed_manifest_sha256": (
                            "6960e4cb8bd26c3711db85aede44655f0f9b83a0a2fc3612053bb5d91674ff6a"
                        ),
                        "parent_execution_seed_source_revision": (
                            "9d2529a718672edcd970f24960535db6ddbde5e4"
                        ),
                    }
                )
            else:
                document["provenance"].update(
                    {
                        "parent_seed_manifest_sha256": (
                            "30adaac167ee6cdde136ce5d957e6b4ca0b0a2bdc23ff376436634a6a0db027e"
                        ),
                        "parent_seed_source_revision": (
                            "9d2529a718672edcd970f24960535db6ddbde5e4"
                        ),
                    }
                )

        def use_retired_9d2529_parent_generation(document):
            use_retired_9d2529_execution_parent(document)
            if os.name == "nt":
                document["provenance"].update(
                    {
                        "parent_plan_seed_manifest_sha256": (
                            "30adaac167ee6cdde136ce5d957e6b4ca0b0a2bdc23ff376436634a6a0db027e"
                        ),
                        "parent_plan_seed_source_revision": (
                            "9d2529a718672edcd970f24960535db6ddbde5e4"
                        ),
                    }
                )

        cases = (
            ("uppercase source revision", uppercase_revision),
            ("retired 9d2529 execution parent", use_retired_9d2529_execution_parent),
            ("retired 9d2529 parent generation", use_retired_9d2529_parent_generation),
            ("retired input count", lambda document: document["provenance"].update({"source_input_count": 58})),
            ("future input count", lambda document: document["provenance"].update({"source_input_count": 62})),
            ("wrong parent manifest", change_parent),
            ("mixed parent generations", mix_parent_generations),
            ("retired v1 parent generation", use_retired_v1_execution_parent),
            ("retired v2 execution parent", use_retired_v2_execution_parent),
            ("retired v2 parent generation", use_retired_v2_parent_generation),
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
                    "unknown native build plan",
                    lambda document: document["provenance"].update(
                        {"native_build_plan_sha256": "0" * 64}
                    ),
                ),
                (
                    "retired source-current native build plan",
                    lambda document: document["provenance"].update(
                        {
                            "native_build_plan_sha256": (
                                "c27481d2c532486648a1170a8a44b3b0020cea1460408f5606f340fb86976ed3"
                            )
                        }
                    ),
                ),
                (
                    "malformed native build plan",
                    lambda document: document["provenance"].update(
                        {"native_build_plan_sha256": "C" * 64}
                    ),
                ),
                (
                    "matched parents from different generations",
                    lambda document: document["provenance"].update(
                        {
                            "parent_execution_seed_manifest_sha256": (
                                "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd"
                            ),
                            "parent_execution_seed_source_revision": (
                                "142a9737f618ab8500308576a1c222501d639e5f"
                            ),
                        }
                    ),
                ),
                (
                    "retired v1 plan parent generation",
                    lambda document: document["provenance"].update(
                        {
                            "parent_plan_seed_manifest_sha256": (
                                "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b"
                            ),
                            "parent_plan_seed_source_revision": (
                                "a17c9465911da41d59b7ada71733d36c39faa5ea"
                            ),
                        }
                    ),
                ),
                (
                    "retired v2 plan parent generation",
                    lambda document: document["provenance"].update(
                        {
                            "parent_plan_seed_manifest_sha256": (
                                "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
                            ),
                            "parent_plan_seed_source_revision": (
                                "0232cb57aad5d6bdfd7bd77499762514b2f0ebfd"
                            ),
                        }
                    ),
                ),
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

    def test_semantically_escaped_seed_manifest_executes(self):
        for escaped_key in (False, True):
            with self.subTest(escaped_key=escaped_key), tempfile.TemporaryDirectory(
                prefix=".cupidbuild-escaped-seed-", dir=REPO_ROOT
            ) as temporary:
                root = Path(temporary)
                manifest = self._copy_checked_assembly_seed(root / "seed")
                document = json.loads(manifest.read_text(encoding="utf-8"))
                encoded = json.dumps(document)
                if escaped_key:
                    encoded = encoded.replace('"schema"', r'"sch\u0065ma"')
                else:
                    encoded = encoded.replace(
                        document["schema"], r"\u0063" + document["schema"][1:]
                    )
                manifest.write_text(encoded, encoding="utf-8")
                source = root / "input.asm"
                output = root / "output.o"
                source.write_text("bits 32\nsection .text\nret\n", encoding="utf-8")
                result = self._run_object(source, output, manifest)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(result.stdout + result.stderr, "")
                self.assertEqual(output.read_bytes()[:7], b"\x7fELF\x01\x01\x01")

    def test_historical_unsupported_seed_schema_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-schema-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            self._promote_seed_contract(manifest)
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

    def test_historical_seed_provenance_failure_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-provenance-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            self._promote_seed_contract(manifest)
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

    def test_historical_seed_producer_role_drift_is_rejected_before_assembly(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-object-producer-role-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            self._promote_seed_contract(manifest)
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
            "wrong escaped schema": lambda document: (
                json.dumps(document, indent=2, sort_keys=True).replace(
                    document["schema"],
                    r"\u0078" + document["schema"][1:],
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

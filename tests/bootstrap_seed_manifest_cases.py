## @file
#  @brief Check promoted manifests, import profiles, and source inventories.
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock

from tests.c_source_spelling import _c_spelling_ranges
from tests.bootstrap_seed_fixture_profiles import (
    OWNER_SOURCE_NAMES,
    historical_disk_manifest,
    historical_disk_snapshot,
    observer_workers_for_seed,
)
from tools.bootstrap_toolchain import (
    BootstrapError,
    CANDIDATE_TOOL_NAMES,
    EXPECTED_LINKS,
    PROMOTED_SOURCE_INPUT_COUNT,
    ISO_PUBLICATION_CUPIDBUILD_LINK,
    PROMOTED_SEED_SCHEMA,
    PRODUCER_NAMES,
    PROMOTED_WINDOWS_PLAN_SHA256,
    PROMOTED_WINDOWS_SEED_SCHEMA,
    SEED_SCHEMA,
    SeedInputs,
    Stage,
    TOOL_NAMES,
    WINDOWS_CUPIDBUILD_IMPORT_PROFILES,
    WINDOWS_CUPIDBUILD_IMPORTS,
    WINDOWS_CUPIDBUILD_SEED_IMPORTS,
    WINDOWS_LINKER_IMPORTS,
    WINDOWS_LINKER_SEED_IMPORTS,
    WINDOWS_TOOL_IMPORTS,
    WINDOWS_TOOL_SEED_IMPORTS,
    _build_plan_sha256,
    _candidate_build_plan,
    _materialize_behavior_seed,
    _materialize_behavior_seed_pair,
    _require_seed_pair_identity,
    _retarget_native_windows_behavior_seed,
    _run_stage_pair,
    _windows_build_plan,
    _windows_imports,
    _windows_utf8_imports,
    _windows_profile_imports,
    _promoted_windows_imports,
    _validate_static_i386_pe32,
    _validate_build_plan,
    capture_source_snapshot,
    freeze_seed_inputs,
    verify_seed_inputs,
)

from tests.bootstrap_seed_suite_fixtures import (
    BOOTSTRAP_TOOL,
    CURRENT_SOURCE_HEAD_INPUT_COUNT,
    CURRENT_SOURCE_HEAD_SNAPSHOT_SHA256,
    REPO_ROOT,
    SEED_MANIFEST,
    WINDOWS_SEED_MANIFEST,
    _promoted_windows_profile_flags,
)


class BootstrapSeedManifestCases:
    def test_source_head_inventory_matches_exact_snapshot_before_bootstrap(self):
        plan = _candidate_build_plan(json.loads(SEED_MANIFEST.read_bytes())["build_plan"])
        inventory = capture_source_snapshot(
            REPO_ROOT, plan, windows_utf8=True, windows_long_paths=True,
            windows_user_link_aliases=True, windows_observer_workers=True,
        )
        self.assertEqual(len(inventory), CURRENT_SOURCE_HEAD_INPUT_COUNT)
        digest = hashlib.sha256(json.dumps(
            inventory, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
        ).encode("ascii")).hexdigest()
        self.assertEqual(digest, CURRENT_SOURCE_HEAD_SNAPSHOT_SHA256)

    def test_source_head_inventory_rejects_source_content_drift(self):
        read = Path.read_bytes
        source = REPO_ROOT / "toolchain/cupidbuild_disk_input_discovery.cc"

        def changed(path):
            data = read(path)
            return data + b"\n" if path == source else data

        with mock.patch.object(Path, "read_bytes", changed), self.assertRaises(AssertionError):
            self.test_source_head_inventory_matches_exact_snapshot_before_bootstrap()

    def test_promoted_complete_disk_plan_retains_the_closed_source_and_link_contract(self):
        manifest = historical_disk_manifest(json.loads(SEED_MANIFEST.read_bytes()))
        _validate_build_plan(manifest, promoted=True)
        plan = manifest["build_plan"]
        self.assertEqual(len(plan["sources"]), 37)
        self.assertEqual(len(plan["links"]["cupidbuild"]), 23)
        self.assertIn("iso_fixture_bundle", plan["links"]["cupidobj"])
        new_roles = ("iso_fixture_bundle", "cupidbuild_iso", "cupidbuild_iso_capture",
                     "cupidbuild_iso_image", "cupidbuild_iso_publication",
                     "fat16_stage", "fat16_names", "disk_image")
        for role in new_roles:
            for edge in ("source", "cupidbuild"):
                altered = json.loads(json.dumps(manifest))
                if edge == "source":
                    altered["build_plan"]["sources"] = [row for row in plan["sources"]
                                                         if row["name"] != role]
                else:
                    altered["build_plan"]["links"][edge].remove(role)
                with self.subTest(role=role, edge=edge):
                    with self.assertRaises(BootstrapError):
                        _validate_build_plan(altered, promoted=True)
        altered = json.loads(json.dumps(manifest))
        altered["build_plan"]["links"]["cupidobj"].remove("iso_fixture_bundle")
        with self.assertRaisesRegex(BootstrapError, "link order differs: cupidobj"):
            _validate_build_plan(altered, promoted=True)
        _validate_build_plan(self._legacy_linux_manifest_fixture(manifest))

    def test_complete_iso_promoted_profiles_bind_each_plan_count_and_import_set(self):
        plan = historical_disk_manifest(json.loads(SEED_MANIFEST.read_bytes()))["build_plan"]
        plan['sources'] = [row for row in plan['sources']
                           if row['name'] not in {'fat16_stage', 'fat16_names', 'disk_image'}]
        plan['links']['cupidbuild'] = list(ISO_PUBLICATION_CUPIDBUILD_LINK)
        profiles = (
            ("0787562d0768485fa614c941fc79a7c6e329c64b6956261cca945a95c2ef9f56", 85, False, False, False),
            ("e3bb4c45bb7633d95b205dcbab6405bb569b4cc71965a2eb52b8dfc78e370e18", 90, True, False, False),
            ("5f6a59e696fb7edafdc5dda0b0cc67aa06550556a39816f27081b5a41a81adfc", 91, True, True, False),
            ("d04c045db6492070389894c81364d5a6eada0ee135373f9d2ea1954386aaeb88", 91, True, False, True),
            ("0dfd1982dc1cd7c9d625c4c0546fc20f13fe3c9ae4dc8cbcf8835ff2e6b4e12d", 92, True, True, True),
        )
        for digest, count, utf8, long_paths, aliases in profiles:
            with self.subTest(plan=digest, count=count):
                actual_plan = _windows_build_plan(plan, utf8=utf8, long_paths=long_paths, user_link_aliases=aliases)
                self.assertEqual(_build_plan_sha256(actual_plan), digest)
                snapshot = capture_source_snapshot(REPO_ROOT, plan, windows_utf8=utf8,
                    windows_long_paths=long_paths, windows_user_link_aliases=aliases)
                # The ISO profile predates the disk modules.
                disk_headers = {"toolchain/" + name + ".h"
                                for name in ("fat16_stage", "fat16_names", "disk_image")}
                snapshot = {name: row for name, row in historical_disk_snapshot(snapshot).items()
                            if name not in disk_headers}
                self.assertEqual(len(snapshot), count)
                for role in CANDIDATE_TOOL_NAMES:
                    expected = (_windows_utf8_imports(role, long_paths=long_paths, user_link_aliases=aliases)
                                if utf8 else _windows_imports(role))
                    self.assertEqual(_promoted_windows_imports(role, digest, count), expected)

    def test_complete_iso_promoted_profiles_reject_crossed_counts_and_unknown_members(self):
        profiles = (
            ("0787562d0768485fa614c941fc79a7c6e329c64b6956261cca945a95c2ef9f56", 85),
            ("e3bb4c45bb7633d95b205dcbab6405bb569b4cc71965a2eb52b8dfc78e370e18", 90),
            ("5f6a59e696fb7edafdc5dda0b0cc67aa06550556a39816f27081b5a41a81adfc", 91),
            ("d04c045db6492070389894c81364d5a6eada0ee135373f9d2ea1954386aaeb88", 91),
            ("0dfd1982dc1cd7c9d625c4c0546fc20f13fe3c9ae4dc8cbcf8835ff2e6b4e12d", 92),
        )
        for digest, selected_count in profiles:
            for count in (75, 78, 80, 81, 82, 85, 90, 91, 92, True, float(selected_count)):
                if type(count) is int and count == selected_count:
                    continue
                with self.subTest(plan=digest, count=count):
                    with self.assertRaises(BootstrapError):
                        _promoted_windows_imports("cupidbuild", digest, count)
        for role, digest, count in (("unknown", profiles[-1][0], 92), ("cupidbuild", "0" * 64, 92)):
            with self.subTest(role=role, digest=digest):
                with self.assertRaises(BootstrapError):
                    _promoted_windows_imports(role, digest, count)

    def test_promoted_profile_selection_preserves_same_count_distinct_members(self):
        plan = historical_disk_manifest(json.loads(SEED_MANIFEST.read_text(encoding="utf-8")))["build_plan"]
        profiles = (
            ("e9efefe58f1d105bbc30874cc8d627a96b2acc2de85a5205fddad99ab159ab11", 81, 98, True, False),
            ("f3132b33a0945a6d15484a39c853716468dec96b4d158c4cc550161286092a16", 81, 98, False, True),
            ("1a348ed367f047ac4a67dfc4e819155c0c96e66be19ff216b06d19db3efbe188", 82, 99, True, True),
        )
        for digest, count, current_count, long_paths, aliases in profiles:
            with self.subTest(plan=digest, count=count):
                flags = _promoted_windows_profile_flags(digest, count)
                self.assertEqual(flags, {"long_paths": long_paths, "user_link_aliases": aliases})
                snapshot = capture_source_snapshot(REPO_ROOT, plan, windows_utf8=True,
                    windows_long_paths=flags["long_paths"], windows_user_link_aliases=flags["user_link_aliases"])
                self.assertEqual(len(historical_disk_snapshot(snapshot)), current_count)
                for name in ("cupidbuild_iso.h", "cupidbuild_iso_image.h",
                             "cupidbuild_iso_capture.h", "iso_fixture_bundle.cc",
                             "iso_fixture_bundle.h"):
                    self.assertIn("toolchain/" + name, snapshot)
                self.assertEqual("toolchain/hosted/i386-windows/utf8_long_path_start.asm" in snapshot, long_paths)
                self.assertEqual("toolchain/hosted/i386-windows/final_path_start.asm" in snapshot, aliases)

    def test_promoted_profile_selection_rejects_unmatched_plan_count_pairs(self):
        aliases = "79241fcdd8784952cf9e1e74907ac817dc83e24429c5625d3424a889c2753d70"
        long_aliases = "2dc92702e1e6e823b0c43fd48427d66bd021563925fe2b8b418451206768f8ff"
        for digest, count in ((aliases, 78), (long_aliases, 77), (aliases, True),
                              (long_aliases, 78.0), ("0" * 64, 77)):
            with self.subTest(plan=digest, count=count):
                with self.assertRaises(BootstrapError):
                    _promoted_windows_profile_flags(digest, count)

    def test_artifact_report_identity_checks_bytes_size_and_exact_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "artifact.exe"
            payload = b"captured artifact bytes"
            path.write_bytes(payload)
            expected = {"sha256": hashlib.sha256(payload).hexdigest(),
                        "size": len(payload)}
            self._assert_file_artifact_identity(expected, path)
            for changed in (
                {**expected, "sha256": "0" * 64},
                {**expected, "size": expected["size"] + 1},
                {**expected, "extra": 1},
                {"sha256": expected["sha256"]},
            ):
                with self.subTest(reported=changed):
                    with self.assertRaises(AssertionError):
                        self._assert_file_artifact_identity(changed, path)
                    self._assert_file_artifact_identity(expected, path)
            path.write_bytes(payload[:-1] + b"!")
            with self.assertRaises(AssertionError):
                self._assert_file_artifact_identity(expected, path)
            path.write_bytes(payload)
            self._assert_file_artifact_identity(expected, path)

    def test_native_windows_behavior_seed_tracks_the_executed_plan(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-windows-behavior-plan-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            frozen = freeze_seed_inputs(
                WINDOWS_SEED_MANIFEST, root / "windows-seed"
            )
            candidate_plan = _candidate_build_plan(
                json.loads(SEED_MANIFEST.read_text())["build_plan"]
            )
            snapshot = capture_source_snapshot(REPO_ROOT, candidate_plan)
            current_plan_sha256 = _build_plan_sha256(_windows_build_plan(candidate_plan))

            retargeted = _retarget_native_windows_behavior_seed(
                frozen, current_plan_sha256, candidate_plan, snapshot
            )

            self.assertEqual(
                retargeted.manifest["provenance"][
                    "native_build_plan_sha256"
                ],
                current_plan_sha256,
            )
            self.assertEqual(
                json.loads(retargeted.manifest_bytes)["provenance"][
                    "native_build_plan_sha256"
                ],
                current_plan_sha256,
            )
            self.assertEqual(
                retargeted.manifest_sha256,
                hashlib.sha256(retargeted.manifest_bytes).hexdigest(),
            )
            self.assertIn("toolchain/hosted/i386-linux/include/cupid_runtime.h", snapshot)
            self.assertIn("toolchain/cupidbuild_jpeg.h", snapshot)
            self.assertEqual(retargeted.manifest["provenance"]["source_input_count"], 112)
            self.assertEqual(retargeted.manifest["provenance"]["linux_candidate_build_plan_sha256"],
                             _build_plan_sha256(candidate_plan))
            self.assertEqual(retargeted.manifest["provenance"]["source_snapshot_sha256"],
                             hashlib.sha256(json.dumps(snapshot, sort_keys=True,
                                 separators=(",", ":"), ensure_ascii=True).encode("ascii")).hexdigest())
            with self.assertRaisesRegex(BootstrapError, "behavior build plan differs"):
                _retarget_native_windows_behavior_seed(frozen, "1" * 64, candidate_plan, snapshot)
            with self.assertRaisesRegex(BootstrapError, "source snapshot is empty"):
                _retarget_native_windows_behavior_seed(frozen, current_plan_sha256, candidate_plan, {})
            self.assertEqual(retargeted.artifact_bytes, frozen.artifact_bytes)
            self.assertEqual(retargeted.tools, frozen.tools)
            self.assertEqual(
                frozen.manifest["provenance"][
                    "native_build_plan_sha256"
                ],
                PROMOTED_WINDOWS_PLAN_SHA256,
            )
            with self.assertRaisesRegex(
                BootstrapError,
                "native Windows behavior build plan SHA-256 is invalid",
            ):
                _retarget_native_windows_behavior_seed(frozen, "not-a-digest", candidate_plan, snapshot)

            missing_plan_document = json.loads(frozen.manifest_bytes)
            del missing_plan_document["provenance"][
                "native_build_plan_sha256"
            ]
            missing_plan_bytes = (
                json.dumps(
                    missing_plan_document,
                    indent=2,
                    sort_keys=True,
                    ensure_ascii=True,
                )
                + "\n"
            ).encode("ascii")
            missing_plan = SeedInputs(
                manifest=missing_plan_document,
                manifest_bytes=missing_plan_bytes,
                manifest_sha256=hashlib.sha256(
                    missing_plan_bytes
                ).hexdigest(),
                live_manifest_path=frozen.live_manifest_path,
                artifact_bytes=frozen.artifact_bytes,
                tools=frozen.tools,
            )
            with self.assertRaisesRegex(
                BootstrapError,
                "native Windows behavior seed build plan is unavailable",
            ):
                _retarget_native_windows_behavior_seed(
                    missing_plan, current_plan_sha256, candidate_plan, snapshot
                )

            linux = freeze_seed_inputs(
                SEED_MANIFEST, root / "linux-seed"
            )
            self.assertIs(
                _retarget_native_windows_behavior_seed(
                    linux, current_plan_sha256, candidate_plan, snapshot
                ),
                linux,
            )

    def test_long_behavior_seed_preserves_exact_reviewed_lineage(self):
        windows = verify_seed_inputs(WINDOWS_SEED_MANIFEST)
        linux = verify_seed_inputs(SEED_MANIFEST)
        plan = _candidate_build_plan(linux.manifest["build_plan"])
        snapshot = capture_source_snapshot(
            REPO_ROOT, plan, windows_utf8=True, windows_long_paths=True
        )
        native_digest = _build_plan_sha256(
            _windows_build_plan(plan, utf8=True, long_paths=True)
        )
        snapshot_digest = hashlib.sha256(json.dumps(
            snapshot, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii")).hexdigest()

        def fixture(original, document):
            payload = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode()
            return SeedInputs(
                manifest=document,
                manifest_bytes=payload,
                manifest_sha256=hashlib.sha256(payload).hexdigest(),
                live_manifest_path=original.live_manifest_path,
                artifact_bytes=original.artifact_bytes,
                tools=original.tools,
            )

        plan_document = json.loads(linux.manifest_bytes)
        plan_document["provenance"].update(
            source_input_count=len(snapshot),
            source_snapshot_sha256=snapshot_digest,
        )
        plan_seed = fixture(linux, plan_document)
        document = json.loads(windows.manifest_bytes)
        document["provenance"].update(
            source_input_count=len(snapshot),
            source_snapshot_sha256=snapshot_digest,
            native_build_plan_sha256=native_digest,
            linux_candidate_build_plan_sha256=_build_plan_sha256(plan),
            plan_seed_manifest_sha256=plan_seed.manifest_sha256,
        )
        reviewed = fixture(windows, document)

        def retarget(seed, source_snapshot=snapshot, parent=plan_seed):
            return _retarget_native_windows_behavior_seed(
                seed, native_digest, plan, source_snapshot,
                utf8=True, long_paths=True, parent_plan_seed=parent,
            )

        self.assertIs(retarget(reviewed), reviewed)
        with self.assertRaisesRegex(BootstrapError, "parent plan seed is unavailable"):
            retarget(reviewed, parent=None)
        mismatched_plan = json.loads(plan_seed.manifest_bytes)
        mismatched_plan["provenance"]["source_snapshot_sha256"] = "0" * 64
        with self.assertRaisesRegex(BootstrapError, "source snapshot differs"):
            retarget(reviewed, parent=fixture(linux, mismatched_plan))
        for field, changed in (
            ("source_input_count", len(snapshot) - 1),
            ("source_input_count", float(len(snapshot))),
            ("native_build_plan_sha256", "0" * 64),
            ("linux_candidate_build_plan_sha256", "0" * 64),
        ):
            with self.subTest(field=field, changed=changed):
                altered = json.loads(reviewed.manifest_bytes)
                altered["provenance"][field] = changed
                candidate = fixture(windows, altered)
                result = retarget(candidate)
                self.assertIsNot(result, candidate)
                self.assertEqual(
                    result.manifest["provenance"]["parent_execution_seed_manifest_sha256"],
                    candidate.manifest_sha256,
                )
        changed_snapshot = json.loads(json.dumps(snapshot))
        first = next(iter(changed_snapshot))
        changed_snapshot[first]["sha256"] = "0" * 64
        result = retarget(reviewed, source_snapshot=changed_snapshot)
        self.assertIsNot(result, reviewed)
        self.assertEqual(
            result.manifest["provenance"]["parent_execution_seed_manifest_sha256"],
            reviewed.manifest_sha256,
        )
        self.assertIs(retarget(reviewed), reviewed)

    def test_behavior_seed_can_bind_one_fixed_point_stage(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-stage-behavior-seed-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            seed_root = root / "checked-seed"
            seed_root.mkdir()
            artifacts = []
            old_bytes = []
            old_tools = {}
            stage_tools = {}
            next_stage_tools = {}
            for index, name in enumerate(CANDIDATE_TOOL_NAMES):
                filename = f"{name}.elf"
                old_payload = f"old {name}\n".encode("ascii")
                stage_payload = f"stage {index} {name}\n".encode("ascii")
                old_path = seed_root / filename
                old_path.write_bytes(old_payload)
                stage_path = root / f"stage-{filename}"
                stage_path.write_bytes(stage_payload)
                next_stage_path = root / f"next-stage-{filename}"
                next_stage_path.write_bytes(
                    f"next stage {index} {name}\n".encode("ascii")
                )
                artifacts.append(
                    {
                        "file": filename,
                        "name": name,
                        "producer": name in PRODUCER_NAMES,
                        "sha256": hashlib.sha256(old_payload).hexdigest(),
                        "size": len(old_payload),
                    }
                )
                old_bytes.append((name, old_payload))
                old_tools[name] = old_path
                stage_tools[name] = stage_path
                next_stage_tools[name] = next_stage_path
            document = {
                "artifacts": artifacts,
                "schema": PROMOTED_SEED_SCHEMA,
            }
            manifest_bytes = (
                json.dumps(document, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            live_manifest = seed_root / "manifest.json"
            live_manifest.write_bytes(manifest_bytes)
            seed_inputs = SeedInputs(
                manifest=document,
                manifest_bytes=manifest_bytes,
                manifest_sha256=hashlib.sha256(manifest_bytes).hexdigest(),
                live_manifest_path=live_manifest,
                artifact_bytes=tuple(old_bytes),
                tools=old_tools,
            )

            materialized = _materialize_behavior_seed(
                seed_inputs,
                root,
                "stage-three-seed",
                Stage(objects={}, tools=stage_tools),
            )

            result = json.loads(materialized.read_text(encoding="ascii"))
            result_artifacts = {
                artifact["name"]: artifact
                for artifact in result["artifacts"]
            }
            for index, name in enumerate(CANDIDATE_TOOL_NAMES):
                payload = f"stage {index} {name}\n".encode("ascii")
                artifact = result_artifacts[name]
                self.assertEqual(artifact["size"], len(payload))
                self.assertEqual(
                    artifact["sha256"], hashlib.sha256(payload).hexdigest()
                )
                self.assertEqual(
                    (materialized.parent / artifact["file"]).read_bytes(),
                    payload,
                )
            self.assertEqual(seed_inputs.manifest_bytes, manifest_bytes)
            for name, payload in old_bytes:
                self.assertEqual(old_tools[name].read_bytes(), payload)

            incomplete_tools = dict(stage_tools)
            del incomplete_tools[CANDIDATE_TOOL_NAMES[-1]]
            with self.assertRaisesRegex(
                BootstrapError,
                "behavior seed stage tool inventory differs",
            ):
                _materialize_behavior_seed(
                    seed_inputs,
                    root,
                    "incomplete-stage-seed",
                    Stage(objects={}, tools=incomplete_tools),
                )

            pair_root = root / "pair"
            pair_root.mkdir()
            stage_three_manifest, stage_four_manifest = (
                _materialize_behavior_seed_pair(
                    seed_inputs,
                    pair_root,
                    Stage(objects={}, tools=stage_tools),
                    Stage(objects={}, tools=next_stage_tools),
                )
            )
            self.assertEqual(
                stage_three_manifest.parent.name, "stage-three-seed"
            )
            self.assertEqual(
                stage_four_manifest.parent.name, "stage-four-seed"
            )
            stage_three_records = {
                artifact["name"]: artifact
                for artifact in json.loads(
                    stage_three_manifest.read_text(encoding="ascii")
                )["artifacts"]
            }
            stage_four_records = {
                artifact["name"]: artifact
                for artifact in json.loads(
                    stage_four_manifest.read_text(encoding="ascii")
                )["artifacts"]
            }
            for index, name in enumerate(CANDIDATE_TOOL_NAMES):
                filename = f"{name}.elf"
                stage_payload = f"stage {index} {name}\n".encode("ascii")
                next_stage_payload = (
                    f"next stage {index} {name}\n".encode("ascii")
                )
                self.assertEqual(
                    (stage_three_manifest.parent / filename).read_bytes(),
                    stage_payload,
                )
                self.assertEqual(
                    (stage_four_manifest.parent / filename).read_bytes(),
                    next_stage_payload,
                )
                self.assertEqual(
                    stage_three_records[name]["sha256"],
                    hashlib.sha256(stage_payload).hexdigest(),
                )
                self.assertEqual(
                    stage_four_records[name]["sha256"],
                    hashlib.sha256(next_stage_payload).hexdigest(),
                )

            def seed_with_manifest(
                candidate_document: dict[str, object],
            ) -> SeedInputs:
                candidate_bytes = (
                    json.dumps(candidate_document, indent=2, sort_keys=True)
                    + "\n"
                ).encode("ascii")
                return SeedInputs(
                    manifest=candidate_document,
                    manifest_bytes=candidate_bytes,
                    manifest_sha256=hashlib.sha256(
                        candidate_bytes
                    ).hexdigest(),
                    live_manifest_path=live_manifest,
                    artifact_bytes=tuple(old_bytes),
                    tools=old_tools,
                )

            duplicate_document = json.loads(json.dumps(document))
            duplicate_document["artifacts"].append(
                dict(duplicate_document["artifacts"][0])
            )
            with self.assertRaisesRegex(
                BootstrapError,
                "behavior seed artifact inventory has duplicates",
            ):
                _materialize_behavior_seed(
                    seed_with_manifest(duplicate_document),
                    root,
                    "duplicate-artifact-seed",
                    Stage(objects={}, tools=stage_tools),
                )

            missing_document = json.loads(json.dumps(document))
            missing_document["artifacts"].pop()
            with self.assertRaisesRegex(
                BootstrapError, "behavior seed artifact inventory differs"
            ):
                _materialize_behavior_seed(
                    seed_with_manifest(missing_document),
                    root,
                    "missing-artifact-seed",
                    Stage(objects={}, tools=stage_tools),
                )

            invalid_name_document = json.loads(json.dumps(document))
            invalid_name_document["artifacts"][0]["file"] = "../tool.elf"
            with self.assertRaisesRegex(
                BootstrapError, "behavior seed artifact name is invalid"
            ):
                _materialize_behavior_seed(
                    seed_with_manifest(invalid_name_document),
                    root,
                    "invalid-name-seed",
                    Stage(objects={}, tools=stage_tools),
                )

    def test_stage_pair_mismatch_reports_each_observed_result(self):
        runner = mock.Mock()
        runner.run.side_effect = [
            subprocess.CompletedProcess(
                ["stage-three"], 1, "first out", "first error"
            ),
            subprocess.CompletedProcess(
                ["stage-four"], 2, "second out", "second error"
            ),
        ]
        stage_three = Stage(objects={}, tools={"cupidld": Path("three")})
        stage_four = Stage(objects={}, tools={"cupidld": Path("four")})

        with self.assertRaisesRegex(
            BootstrapError,
            "^cupidld behavior differs across stages: "
            "stage-three status 1, stdout 'first out', stderr "
            "'first error'; stage-four status 2, stdout 'second out', "
            "stderr 'second error'$",
        ):
            _run_stage_pair(
                runner,
                stage_three,
                stage_four,
                "cupidld",
                ["--help"],
            )

    def test_candidate_plan_adds_cupidbuild_without_changing_the_v1_plan(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        candidate_names = {
            "cupidbuild", "cupidbuild_host", "cupidbuild_main",
            "seed_manifest", "seed_release", "contract_parse_internal",
            "cupidbuild_artifacts", "artifact_size_policy",
            "user_syscall_abi", "cupidbuild_user_abi",
            "iso_fixture_bundle",
            "cupidbuild_iso", "cupidbuild_iso_capture", "cupidbuild_iso_image",
            "cupidbuild_iso_publication",
            "fat16_stage", "fat16_names", "disk_image",
        }
        candidate_names.update(OWNER_SOURCE_NAMES)
        checked_plan["sources"] = [
            source for source in checked_plan["sources"]
            if source["name"] not in candidate_names
        ]
        del checked_plan["links"]["cupidbuild"]
        checked_plan["links"]["cupidobj"] = list(EXPECTED_LINKS["cupidobj"])
        original_plan = json.loads(json.dumps(checked_plan))

        candidate_plan = _candidate_build_plan(checked_plan)

        self.assertEqual(
            TOOL_NAMES,
            ("cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc"),
        )
        self.assertEqual(
            CANDIDATE_TOOL_NAMES,
            (
                "cupidasm",
                "cupiddis",
                "cupidld",
                "cupidobj",
                "cupidc",
                "cupidbuild",
            ),
        )
        self.assertEqual(checked_plan, original_plan)
        self.assertEqual(
            candidate_plan["sources"][19:37],
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
                {"gnu_extensions": False, "name": "seed_manifest",
                 "path": "/toolchain/seed_manifest.cc"},
                {"gnu_extensions": False, "name": "seed_release",
                 "path": "/toolchain/seed_release.cc"},
                {"gnu_extensions": False, "name": "contract_parse_internal",
                 "path": "/toolchain/contract_parse_internal.cc"},
                {"gnu_extensions": False, "name": "cupidbuild_artifacts",
                 "path": "/toolchain/cupidbuild_artifacts.cc"},
                {"gnu_extensions": False, "name": "artifact_size_policy",
                 "path": "/toolchain/artifact_size_policy.cc"},
                {"gnu_extensions": False, "name": "user_syscall_abi",
                 "path": "/toolchain/user_syscall_abi.cc"},
                {"gnu_extensions": False, "name": "cupidbuild_user_abi",
                 "path": "/toolchain/cupidbuild_user_abi.cc"},
                {"gnu_extensions": False, "name": "iso_fixture_bundle",
                 "path": "/toolchain/iso_fixture_bundle.cc"},
                {"gnu_extensions": False, "name": "cupidbuild_iso",
                 "path": "/toolchain/cupidbuild_iso.cc"},
                {"gnu_extensions": False, "name": "cupidbuild_iso_capture",
                 "path": "/toolchain/cupidbuild_iso_capture.cc"},
                {"gnu_extensions": False, "name": "cupidbuild_iso_image",
                 "path": "/toolchain/cupidbuild_iso_image.cc"},
                {"gnu_extensions": False, "name": "cupidbuild_iso_publication",
                 "path": "/toolchain/cupidbuild_iso_publication.cc"},
                {"gnu_extensions": False, "name": "fat16_stage", "path": "/toolchain/fat16_stage.cc"},
                {"gnu_extensions": False, "name": "fat16_names", "path": "/toolchain/fat16_names.cc"},
                {"gnu_extensions": False, "name": "disk_image", "path": "/toolchain/disk_image.cc"},
            ],
        )
        self.assertEqual(
            candidate_plan["links"]["cupidbuild"],
            [
                "start",
                "cupidbuild_main",
                "cupidbuild",
                "cupidbuild_host",
                "ctool_host",
                "ctool",
                "elf32",
                "seed_manifest",
                "seed_release",
                "contract_parse_internal",
                "cupidbuild_artifacts",
                "artifact_size_policy",
                "user_syscall_abi",
                "cupidbuild_user_abi",
                "iso_fixture_bundle",
                "cupidbuild_iso",
                "cupidbuild_iso_capture",
                "cupidbuild_iso_image",
                "cupidbuild_iso_publication",
                "fat16_stage", "fat16_names", "disk_image",
                *OWNER_SOURCE_NAMES,
                "runtime",
            ],
        )
        self.assertNotEqual(
            _build_plan_sha256(candidate_plan),
            _build_plan_sha256(checked_plan),
        )
        source_inventory = capture_source_snapshot(
            REPO_ROOT, candidate_plan, windows_utf8=True
        )
        self.assertEqual(len(source_inventory), 124)
        for path in (
            "toolchain/hosted/i386-linux/include/cupid_runtime.h",
            "toolchain/cupidbuild.cc",
            "toolchain/cupidbuild_host.cc",
            "toolchain/cupidbuild_host_candidate_store.inc",
            "toolchain/cupidbuild_host_external_inputs.inc",
            "toolchain/cupidbuild_host_frozen_inputs.inc",
            "toolchain/cupidbuild_host_source_paths.inc",
            "toolchain/cupidbuild_disk_image.inc",
            "toolchain/cupidbuild_jpeg.h",
            "toolchain/cupidbuild_main.cc",
            "toolchain/cupidobj_install_sources.inc",
            "toolchain/cupidobj_iso.inc",
            "toolchain/cupidobj_profile_manifest.inc",
        ):
            self.assertIn(path, source_inventory)

        self.assertEqual(tuple(row["name"] for row in candidate_plan["sources"][37:]), OWNER_SOURCE_NAMES)

    def test_candidate_plan_accepts_an_exact_six_tool_plan(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        candidate_plan = _candidate_build_plan(checked_plan)

        self.assertEqual(
            _candidate_build_plan(candidate_plan), candidate_plan
        )
        self.assertEqual(
            _build_plan_sha256(candidate_plan),
            "e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa"
        )

    def test_promoted_linux_seed_verifies_all_six_artifacts(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-promoted-linux-seed-"
        ) as temporary:
            seed = Path(temporary) / "seed"
            shutil.copytree(SEED_MANIFEST.parent, seed)
            manifest = self._promote_manifest_fixture(
                json.loads(
                    (seed / "manifest.json").read_text(encoding="utf-8")
                ),
                False,
            )
            (seed / "manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            verified = verify_seed_inputs(seed / "manifest.json")

        self.assertEqual(tuple(verified.tools), CANDIDATE_TOOL_NAMES)
        self.assertEqual(
            tuple(name for name, _data in verified.artifact_bytes),
            CANDIDATE_TOOL_NAMES,
        )

    def test_promoted_windows_seed_selects_publication_profiles(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-promoted-windows-seed-"
        ) as temporary:
            seed = Path(temporary) / "seed"
            shutil.copytree(WINDOWS_SEED_MANIFEST.parent, seed)
            manifest = self._promote_manifest_fixture(
                json.loads(
                    (seed / "manifest.json").read_text(encoding="utf-8")
                ),
                True,
            )
            (seed / "manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            with mock.patch(
                "tools.bootstrap_toolchain._validate_static_i386_pe32_bytes"
            ) as validate:
                verified = verify_seed_inputs(seed / "manifest.json")

        self.assertEqual(tuple(verified.tools), CANDIDATE_TOOL_NAMES)
        profiles = {
            call.args[1]: call.args[3] for call in validate.call_args_list
        }
        self.assertEqual(
            profiles,
            {
                name + ".exe": _windows_profile_imports(
                    name, utf8=True, observer_workers=observer_workers_for_seed(
                        PROMOTED_WINDOWS_PLAN_SHA256, PROMOTED_SOURCE_INPUT_COUNT),
                    **_promoted_windows_profile_flags(
                        PROMOTED_WINDOWS_PLAN_SHA256, PROMOTED_SOURCE_INPUT_COUNT
                    )
                )
                for name in CANDIDATE_TOOL_NAMES
            },
        )

    def test_windows_cupidbuild_import_profiles_are_exact_pairs(self):
        current_kernel = WINDOWS_CUPIDBUILD_IMPORTS[0][1]
        self.assertEqual(
            current_kernel,
            (
                "CloseHandle",
                "CreateDirectoryA",
                "CreateFileA",
                "CreateProcessA",
                "DeleteFileA",
                "DeleteProcThreadAttributeList",
                "ExitProcess",
                "FindClose",
                "FindFirstFileA",
                "FindNextFileA",
                "FlushFileBuffers",
                "GetCommandLineA",
                "GetCurrentDirectoryA",
                "GetCurrentProcessId",
                "GetExitCodeProcess",
                "GetFileAttributesA",
                "GetFileInformationByHandle",
                "GetFullPathNameA",
                "GetLastError",
                "GetStdHandle",
                "InitializeProcThreadAttributeList",
                "MoveFileExA",
                "OpenProcess",
                "ReadFile",
                "RemoveDirectoryA",
                "SetFilePointer",
                "SetHandleInformation",
                "TerminateProcess",
                "UpdateProcThreadAttribute",
                "VirtualAlloc",
                "VirtualFree",
                "WaitForSingleObject",
                "WriteFile",
            ),
        )
        self.assertEqual(
            WINDOWS_CUPIDBUILD_SEED_IMPORTS[1],
            ("NTDLL.dll", ("NtSetInformationFile",)),
        )
        self.assertEqual(
            WINDOWS_CUPIDBUILD_IMPORTS[1],
            (
                "NTDLL.dll",
                (
                    "NtCreateFile",
                    "NtQueryDirectoryFile",
                    "NtSetInformationFile",
                ),
            ),
        )
        self.assertEqual(
            WINDOWS_CUPIDBUILD_IMPORT_PROFILES,
            (
                WINDOWS_CUPIDBUILD_SEED_IMPORTS,
                WINDOWS_CUPIDBUILD_IMPORTS,
            ),
        )
        exactly_two_ntdll = (
            "NTDLL.dll",
            WINDOWS_CUPIDBUILD_IMPORTS[1][1][:2],
        )
        rejected_profiles = (
            (
                WINDOWS_CUPIDBUILD_SEED_IMPORTS[0],
                WINDOWS_CUPIDBUILD_IMPORTS[1],
            ),
            (
                WINDOWS_CUPIDBUILD_IMPORTS[0],
                WINDOWS_CUPIDBUILD_SEED_IMPORTS[1],
            ),
            (WINDOWS_CUPIDBUILD_SEED_IMPORTS[0], exactly_two_ntdll),
            (WINDOWS_CUPIDBUILD_IMPORTS[0], exactly_two_ntdll),
        )
        for profile in rejected_profiles:
            self.assertNotIn(profile, WINDOWS_CUPIDBUILD_IMPORT_PROFILES)

    def test_promoted_windows_images_reject_legacy_import_profiles(self):
        for name in CANDIDATE_TOOL_NAMES:
            legacy_profile = (
                WINDOWS_CUPIDBUILD_SEED_IMPORTS
                if name == "cupidbuild"
                else (
                    WINDOWS_LINKER_SEED_IMPORTS
                    if name in ("cupidasm", "cupidld")
                    else WINDOWS_TOOL_SEED_IMPORTS
                )
            )
            with self.subTest(tool=name), self.assertRaisesRegex(
                BootstrapError, "PE32 import"
            ):
                _validate_static_i386_pe32(
                    WINDOWS_SEED_MANIFEST.parent / f"{name}.exe",
                    0x00401000,
                    legacy_profile,
                )

    def test_windows_tool_import_profiles_lock_the_runtime_transition(self):
        self.assertNotIn(
            "GetFileInformationByHandle", WINDOWS_TOOL_SEED_IMPORTS[0][1]
        )
        self.assertIn(
            "GetFileInformationByHandle", WINDOWS_TOOL_IMPORTS[0][1]
        )
        self.assertNotIn(
            "GetFileInformationByHandle", WINDOWS_LINKER_SEED_IMPORTS[0][1]
        )
        self.assertIn(
            "GetFileInformationByHandle", WINDOWS_LINKER_IMPORTS[0][1]
        )

        startup = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-windows"
            / "tool_start.asm"
        ).read_text(encoding="ascii")
        for required in (
            "extern __imp_GetFileInformationByHandle",
            "global cupid_windows_get_file_information:function",
            "cupid_windows_get_file_information:",
            "call dword [__imp_GetFileInformationByHandle]",
        ):
            self.assertIn(required, startup)

    def test_cupidbuild_source_locks_the_paired_import_profiles(self):
        source = (REPO_ROOT / "toolchain/cupidbuild_validation.inc").read_text(
            encoding="utf-8"
        )
        for required in (
            "ordinary_seed_imports",
            "ordinary_current_imports",
            "linker_seed_imports",
            "linker_current_imports",
            "cupidbuild_legacy_imports",
            "cupidbuild_current_imports",
            "DeleteProcThreadAttributeList",
            "InitializeProcThreadAttributeList",
            "UpdateProcThreadAttribute",
            "cupidbuild_legacy_ntdll_imports",
            "cupidbuild_current_ntdll_imports",
            "current_windows_plan",
            "expected_count + expected_ntdll_count",
            "expected_ntdll_imports[index]",
        ):
            self.assertIn(required, source)
        self.assertTrue(_c_spelling_ranges(
            source, '"SetFilePointer", "SetHandleInformation",'
        ))
        self.assertNotIn("expected_total_minimum", source)
        self.assertNotIn("expected_total_maximum", source)

        startup = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-windows"
            / "cupidbuild_start.asm"
        ).read_text(encoding="utf-8")
        for required in (
            "extern __imp_DeleteProcThreadAttributeList",
            "extern __imp_InitializeProcThreadAttributeList",
            "extern __imp_SetHandleInformation",
            "extern __imp_UpdateProcThreadAttribute",
            "extern __imp_NtQueryDirectoryFile",
            "global cupid_windows_delete_proc_thread_attribute_list:function",
            "global cupid_windows_initialize_proc_thread_attribute_list:function",
            "global cupid_windows_set_handle_information:function",
            "global cupid_windows_update_proc_thread_attribute:function",
            "global cupid_windows_nt_query_directory_file:function",
            "cupid_windows_delete_proc_thread_attribute_list:",
            "cupid_windows_initialize_proc_thread_attribute_list:",
            "cupid_windows_set_handle_information:",
            "cupid_windows_update_proc_thread_attribute:",
            "cupid_windows_nt_query_directory_file:",
            "call dword [__imp_DeleteProcThreadAttributeList]",
            "call dword [__imp_InitializeProcThreadAttributeList]",
            "call dword [__imp_SetHandleInformation]",
            "call dword [__imp_UpdateProcThreadAttribute]",
            "call dword [__imp_NtQueryDirectoryFile]",
        ):
            self.assertIn(required, startup)
        expected_exports = {
            "cupid_windows_create_directory",
            "cupid_windows_create_process",
            "cupid_windows_delete_proc_thread_attribute_list",
            "cupid_windows_find_close",
            "cupid_windows_find_first_file",
            "cupid_windows_find_next_file",
            "cupid_windows_get_current_process_id",
            "cupid_windows_get_exit_code_process",
            "cupid_windows_get_file_attributes",
            "cupid_windows_initialize_proc_thread_attribute_list",
            "cupid_windows_open_process",
            "cupid_windows_remove_directory",
            "cupid_windows_set_handle_information",
            "cupid_windows_terminate_process",
            "cupid_windows_update_proc_thread_attribute",
            "cupid_windows_wait_for_single_object",
            "cupid_windows_nt_create_file",
            "cupid_windows_nt_query_directory_file",
            "cupid_windows_nt_set_information_file",
        }
        self.assertEqual(
            {
                line.removeprefix("global ").removesuffix(":function")
                for line in startup.splitlines()
                if line.startswith("global ")
            },
            expected_exports,
        )
        self.assertNotIn("cupid_windows_get_file_information", startup)
        self.assertNotIn("__imp_GetFileInformationByHandle", startup)
        self.assertNotIn("cupid_windows_set_file_pointer", startup)

    def test_windows_cupidbuild_handle_list_has_an_i386_hosted_abi(self):
        header = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-linux"
            / "include"
            / "windows.h"
        ).read_text(encoding="utf-8")
        startup = (
            REPO_ROOT
            / "toolchain"
            / "hosted"
            / "i386-windows"
            / "cupidbuild_start.asm"
        ).read_text(encoding="utf-8")
        for required in (
            "typedef unsigned int SIZE_T;",
            "typedef void *LPPROC_THREAD_ATTRIBUTE_LIST;",
            "STARTUPINFOEXA",
            "EXTENDED_STARTUPINFO_PRESENT 0x00080000u",
            "PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002u",
            "DeleteProcThreadAttributeList",
            "InitializeProcThreadAttributeList",
            "UpdateProcThreadAttribute",
        ):
            self.assertIn(required, header)

        def wrapper_body(label):
            lines = startup.splitlines()
            start = lines.index(label + ":") + 1
            end = start
            while end < len(lines) and lines[end].startswith(" "):
                end += 1
            return lines[start:end]

        self.assertEqual(
            wrapper_body("cupid_windows_delete_proc_thread_attribute_list"),
            [
                " push ebp",
                " mov ebp, esp",
                " push dword [ebp + 8]",
                " call dword [__imp_DeleteProcThreadAttributeList]",
                " mov esp, ebp",
                " pop ebp",
                " ret",
            ],
        )
        self.assertEqual(
            wrapper_body(
                "cupid_windows_initialize_proc_thread_attribute_list"
            ),
            [
                " push ebp",
                " mov ebp, esp",
                " push dword [ebp + 20]",
                " push dword [ebp + 16]",
                " push dword [ebp + 12]",
                " push dword [ebp + 8]",
                " call dword [__imp_InitializeProcThreadAttributeList]",
                " mov esp, ebp",
                " pop ebp",
                " ret",
            ],
        )
        self.assertEqual(
            wrapper_body("cupid_windows_update_proc_thread_attribute"),
            [
                " push ebp",
                " mov ebp, esp",
                " push dword [ebp + 32]",
                " push dword [ebp + 28]",
                " push dword [ebp + 24]",
                " push dword [ebp + 20]",
                " push dword [ebp + 16]",
                " push dword [ebp + 12]",
                " push dword [ebp + 8]",
                " call dword [__imp_UpdateProcThreadAttribute]",
                " mov esp, ebp",
                " pop ebp",
                " ret",
            ],
        )

        compiler = shutil.which("clang")
        if compiler is None:
            self.skipTest("clang is required for the i386 layout check")
        layout_source = (
            "#include <windows.h>\n"
            '_Static_assert(sizeof(SIZE_T) == 4u, "SIZE_T");\n'
            '_Static_assert(sizeof(STARTUPINFOA) == 68u, "STARTUPINFOA");\n'
            "_Static_assert(__builtin_offsetof(STARTUPINFOEXA, "
            'lpAttributeList) == 68u, "attribute offset");\n'
            '_Static_assert(sizeof(STARTUPINFOEXA) == 72u, "STARTUPINFOEXA");\n'
        )
        layout = subprocess.run(
            [
                compiler,
                "--target=i386-pc-linux-gnu",
                "-nostdinc",
                "-I",
                str(
                    REPO_ROOT
                    / "toolchain"
                    / "hosted"
                    / "i386-linux"
                    / "include"
                ),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-x",
                "c",
                "-fsyntax-only",
                "-",
            ],
            input=layout_source,
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(layout.returncode, 0, layout.stdout + layout.stderr)

    def test_native_windows_cupidbuild_enforces_paired_import_profiles(self):
        if os.name != "nt":
            self.skipTest("native CupidBuild profile validation requires Windows")

        legacy_kernel = WINDOWS_CUPIDBUILD_SEED_IMPORTS[0]
        legacy_ntdll = WINDOWS_CUPIDBUILD_SEED_IMPORTS[1]
        current_kernel = WINDOWS_CUPIDBUILD_IMPORTS[0]
        current_ntdll = WINDOWS_CUPIDBUILD_IMPORTS[1]
        exactly_two_ntdll = (
            "NTDLL.dll",
            ("NtCreateFile", "NtSetInformationFile"),
        )
        seed_profiles = {
            "cupidasm": WINDOWS_LINKER_SEED_IMPORTS,
            "cupidc": WINDOWS_TOOL_SEED_IMPORTS,
            "cupiddis": WINDOWS_TOOL_SEED_IMPORTS,
            "cupidld": WINDOWS_LINKER_SEED_IMPORTS,
            "cupidobj": WINDOWS_TOOL_SEED_IMPORTS,
            "cupidbuild": WINDOWS_CUPIDBUILD_SEED_IMPORTS,
        }
        current_profiles = {
            "cupidasm": WINDOWS_LINKER_IMPORTS,
            "cupidc": WINDOWS_TOOL_IMPORTS,
            "cupiddis": WINDOWS_TOOL_IMPORTS,
            "cupidld": WINDOWS_LINKER_IMPORTS,
            "cupidobj": WINDOWS_TOOL_IMPORTS,
            "cupidbuild": WINDOWS_CUPIDBUILD_IMPORTS,
        }
        current_plan = (
            "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669"
        )
        legacy_plan = (
            "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14"
        )

        def with_profile(profiles, tool, profile):
            changed = dict(profiles)
            changed[tool] = profile
            return changed

        promoted_profiles = {
            name: _windows_profile_imports(
                name, utf8=True, observer_workers=observer_workers_for_seed(
                    PROMOTED_WINDOWS_PLAN_SHA256, PROMOTED_SOURCE_INPUT_COUNT),
                **_promoted_windows_profile_flags(
                    PROMOTED_WINDOWS_PLAN_SHA256, PROMOTED_SOURCE_INPUT_COUNT
                )
            )
            for name in CANDIDATE_TOOL_NAMES
        }
        missing_resolver_cases = (
            tuple(
                (
                    "promoted-without-resolver-" + name,
                    PROMOTED_WINDOWS_PLAN_SHA256,
                    with_profile(
                        promoted_profiles, name, _windows_utf8_imports(name)
                    ),
                    False,
                )
                for name in ("cupidc", "cupiddis", "cupidobj")
            )
            if _promoted_windows_profile_flags(
                PROMOTED_WINDOWS_PLAN_SHA256, PROMOTED_SOURCE_INPUT_COUNT
            )["long_paths"]
            else ()
        )
        cases = (
            ("promoted", PROMOTED_WINDOWS_PLAN_SHA256, promoted_profiles, True),
            *missing_resolver_cases,
            ("promoted-seed-ordinary", PROMOTED_WINDOWS_PLAN_SHA256,
             with_profile(promoted_profiles, "cupidc", WINDOWS_TOOL_SEED_IMPORTS), False),
            ("promoted-seed-linker", PROMOTED_WINDOWS_PLAN_SHA256,
             with_profile(promoted_profiles, "cupidasm", WINDOWS_LINKER_SEED_IMPORTS), False),
            ("promoted-seed-cupidbuild", PROMOTED_WINDOWS_PLAN_SHA256,
             with_profile(promoted_profiles, "cupidbuild", WINDOWS_CUPIDBUILD_SEED_IMPORTS), False),
            ("promoted-ansi-ordinary", PROMOTED_WINDOWS_PLAN_SHA256,
             with_profile(promoted_profiles, "cupidc", WINDOWS_TOOL_IMPORTS), False),
            ("promoted-ansi-linker", PROMOTED_WINDOWS_PLAN_SHA256,
             with_profile(promoted_profiles, "cupidasm", WINDOWS_LINKER_IMPORTS), False),
            ("promoted-ansi-cupidbuild", PROMOTED_WINDOWS_PLAN_SHA256,
             with_profile(promoted_profiles, "cupidbuild", WINDOWS_CUPIDBUILD_IMPORTS), False),
            ("seed", legacy_plan, seed_profiles, True),
            ("current", current_plan, current_profiles, True),
            (
                "current-plan-seed-ordinary",
                current_plan,
                with_profile(
                    current_profiles, "cupidc", WINDOWS_TOOL_SEED_IMPORTS
                ),
                False,
            ),
            (
                "current-plan-seed-linker",
                current_plan,
                with_profile(
                    current_profiles, "cupidasm", WINDOWS_LINKER_SEED_IMPORTS
                ),
                False,
            ),
            (
                "seed-plan-current-ordinary",
                legacy_plan,
                with_profile(seed_profiles, "cupiddis", WINDOWS_TOOL_IMPORTS),
                False,
            ),
            (
                "seed-plan-current-linker",
                legacy_plan,
                with_profile(seed_profiles, "cupidld", WINDOWS_LINKER_IMPORTS),
                False,
            ),
            (
                "current-plan-seed-cupidbuild",
                current_plan,
                with_profile(
                    current_profiles,
                    "cupidbuild",
                    WINDOWS_CUPIDBUILD_SEED_IMPORTS,
                ),
                False,
            ),
            (
                "seed-plan-current-cupidbuild",
                legacy_plan,
                with_profile(
                    seed_profiles, "cupidbuild", WINDOWS_CUPIDBUILD_IMPORTS
                ),
                False,
            ),
            (
                "current-mixed-cupidbuild",
                current_plan,
                with_profile(
                    current_profiles,
                    "cupidbuild",
                    (current_kernel, legacy_ntdll),
                ),
                False,
            ),
            (
                "current-short-ntdll",
                current_plan,
                with_profile(
                    current_profiles,
                    "cupidbuild",
                    (current_kernel, exactly_two_ntdll),
                ),
                False,
            ),
            (
                "seed-mixed-cupidbuild",
                legacy_plan,
                with_profile(
                    seed_profiles,
                    "cupidbuild",
                    (legacy_kernel, current_ntdll),
                ),
                False,
            ),
        )

        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-cli-", dir=REPO_ROOT / "toolchain"
        ) as cli_temporary, tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-pairs-", dir=REPO_ROOT
        ) as case_temporary:
            cli_root = Path(cli_temporary)
            cli_relative = cli_root.relative_to(REPO_ROOT / "toolchain")
            cli = cli_root / "cupidbuild.exe"
            built = subprocess.run(
                [
                    "make",
                    "-C",
                    str(REPO_ROOT / "toolchain"),
                    f"BUILD_DIR={cli_relative.as_posix()}",
                    f"{cli_relative.as_posix()}/cupidbuild.exe",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=180,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)

            case_root = Path(case_temporary)
            source = case_root / "input.asm"
            source.write_text(
                "bits 32\n"
                "section .text\n"
                "global entry:function\n"
                "entry: ret\n",
                encoding="utf-8",
                newline="\n",
            )
            for name, plan, profiles, accepted in cases:
                with self.subTest(profile=name):
                    profile_root = case_root / name
                    seed = profile_root / "seed"
                    shutil.copytree(WINDOWS_SEED_MANIFEST.parent, seed)
                    manifest_path = seed / "manifest.json"
                    manifest = json.loads(
                        manifest_path.read_text(encoding="utf-8")
                    )
                    manifest["provenance"]["native_build_plan_sha256"] = plan
                    if plan != PROMOTED_WINDOWS_PLAN_SHA256:
                        manifest["provenance"]["source_input_count"] = 59
                        manifest["provenance"]["linux_candidate_build_plan_sha256"] = (
                            "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
                        )
                        manifest["provenance"].update(
                            parent_execution_seed_manifest_sha256=(
                                "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd"
                            ),
                            parent_plan_seed_manifest_sha256=(
                                "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52"
                            ),
                            parent_execution_seed_source_revision=(
                                "142a9737f618ab8500308576a1c222501d639e5f"
                            ),
                            parent_plan_seed_source_revision=(
                                "142a9737f618ab8500308576a1c222501d639e5f"
                            ),
                        )
                    for artifact in manifest["artifacts"]:
                        profile = profiles[artifact["name"]]
                        tool = seed / artifact["file"]
                        payload = self._minimal_cupidbuild_profile_pe32(profile)
                        tool.write_bytes(payload)
                        artifact["size"] = len(payload)
                        artifact["sha256"] = hashlib.sha256(payload).hexdigest()
                        _validate_static_i386_pe32(
                            tool, 0x00401000, profile
                        )
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                        newline="\n",
                    )
                    output = profile_root / "output.o"
                    output.write_bytes(b"last known good object")
                    release_arguments = []
                    if plan == PROMOTED_WINDOWS_PLAN_SHA256:
                        from tools import seed_release_identity

                        record = seed_release_identity.current_release_identity()
                        images = {row["name"]: row for row in manifest["artifacts"]}
                        for row in record["artifacts"]:
                            if row["format"] == "pe32":
                                row["size"] = images[row["name"]]["size"]
                                row["sha256"] = images[row["name"]]["sha256"]
                        release_path = profile_root / "profile-release.json"
                        release_path.write_text(json.dumps(record, sort_keys=True) + "\n",
                                                encoding="utf-8", newline="\n")
                        release_arguments = ["--seed-release", str(release_path)]
                    result = subprocess.run(
                        [
                            str(cli),
                            "assemble-cupidasm-object",
                            "--seed-manifest",
                            str(manifest_path),
                            "--root",
                            str(REPO_ROOT),
                            "--source",
                            source.relative_to(REPO_ROOT).as_posix(),
                            "--output",
                            output.relative_to(REPO_ROOT).as_posix(),
                            *release_arguments,
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                        timeout=90,
                    )
                    if accepted:
                        self.assertNotIn("checked seed manifest is invalid", result.stderr)
                        self.assertNotIn(
                            "checked seed execution profile mismatch",
                            result.stderr,
                        )
                        if result.returncode == 0:
                            self.assertEqual(
                                (result.stdout, result.stderr), ("", "")
                            )
                            self.assertEqual(output.read_bytes()[:4], b"\x7fELF")
                        else:
                            self.assertEqual(result.returncode, 1)
                            self.assertEqual(result.stdout, "")
                            self.assertEqual(
                                output.read_bytes(), b"last known good object"
                            )
                    else:
                        self.assertEqual(result.returncode, 1)
                        self.assertEqual(result.stdout, "")
                        self.assertIn(
                            "checked seed execution profile mismatch",
                            result.stderr,
                        )
                        self.assertEqual(
                            output.read_bytes(), b"last known good object"
                        )

    def test_promoted_seed_rejects_provenance_and_artifact_drift(self):
        source = json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))

        def change_plan(manifest):
            manifest["build_plan"]["workers"] = 1
            manifest["build_plan_sha256"] = _build_plan_sha256(
                manifest["build_plan"]
            )

        cases = {
            "uppercase revision": (
                lambda manifest: manifest["provenance"].update(
                    {
                        "source_revision": (
                            "ABCDEF0123456789ABCDEF0123456789ABCDEF01"
                        )
                    }
                ),
                "source revision is invalid",
            ),
            "wrong parent": (
                lambda manifest: manifest["provenance"].update(
                    {"parent_seed_manifest_sha256": "0" * 64}
                ),
                "parent seed manifest differs",
            ),
            "wrong producer": (
                lambda manifest: manifest["artifacts"][-1].update(
                    {"producer": True}
                ),
                "artifact producer role differs: cupidbuild",
            ),
            "wrong artifact size": (
                lambda manifest: manifest["artifacts"][-1].update(
                    {"size": 1}
                ),
                "promoted artifact size differs: cupidbuild",
            ),
            "wrong artifact digest": (
                lambda manifest: manifest["artifacts"][-1].update(
                    {"sha256": "0" * 64}
                ),
                "promoted artifact SHA-256 differs: cupidbuild",
            ),
            "missing artifact": (
                lambda manifest: manifest["artifacts"].pop(),
                "manifest must contain six tool artifacts",
            ),
            "wrong plan": (
                change_plan,
                "promoted build plan SHA-256 differs",
            ),
        }
        for label, (mutate, expected) in cases.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="cupid-promoted-seed-drift-"
            ) as temporary:
                seed = Path(temporary) / "seed"
                shutil.copytree(SEED_MANIFEST.parent, seed)
                manifest = self._promote_manifest_fixture(source, False)
                mutate(manifest)
                (seed / "manifest.json").write_text(
                    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )

                with self.assertRaisesRegex(
                    BootstrapError, f"^{re.escape(expected)}$"
                ):
                    verify_seed_inputs(seed / "manifest.json")

    def test_manifest_reader_rejects_json_string_escapes(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-seed-json-escape-"
        ) as temporary:
            manifest = Path(temporary) / "manifest.json"
            encoded = SEED_MANIFEST.read_text(encoding="utf-8").replace(
                PROMOTED_SEED_SCHEMA,
                r"\u0063upid.bootstrap-seed.v2",
                1,
            )
            manifest.write_text(encoded, encoding="utf-8", newline="\n")

            with self.assertRaisesRegex(
                BootstrapError,
                "^manifest strings may not use escapes$",
            ):
                verify_seed_inputs(manifest)

    def test_windows_bootstrap_requires_one_seed_identity(self):
        revision = "abcdef0123456789abcdef0123456789abcdef01"
        snapshot = "2" * 64

        def seed(
            schema,
            current_revision=revision,
            current_snapshot=snapshot,
            manifest_digest="0" * 64,
        ):
            provenance = {
                "source_revision": current_revision,
                "source_snapshot_sha256": current_snapshot,
            }
            if schema == PROMOTED_WINDOWS_SEED_SCHEMA:
                provenance["plan_seed_manifest_sha256"] = "0" * 64
            return SeedInputs(
                manifest={
                    "provenance": provenance,
                    "schema": schema,
                },
                manifest_bytes=b"{}",
                manifest_sha256=manifest_digest,
                live_manifest_path=Path("manifest.json"),
                artifact_bytes=(),
                tools={},
            )

        execution = seed(PROMOTED_WINDOWS_SEED_SCHEMA)
        plan = seed(PROMOTED_SEED_SCHEMA)
        _require_seed_pair_identity(execution, plan)

        cases = {
            "generation": (
                seed(SEED_SCHEMA),
                "native Windows bootstrap seed generation differs",
            ),
            "revision": (
                seed(
                    PROMOTED_SEED_SCHEMA,
                    "0123456789abcdef0123456789abcdef01234567",
                ),
                "native Windows bootstrap seed source revision differs",
            ),
            "snapshot": (
                seed(PROMOTED_SEED_SCHEMA, current_snapshot="3" * 64),
                "native Windows bootstrap seed source snapshot differs",
            ),
            "plan manifest": (
                seed(PROMOTED_SEED_SCHEMA, manifest_digest="1" * 64),
                "native Windows bootstrap plan seed manifest differs",
            ),
        }
        for label, (changed_plan, expected) in cases.items():
            with self.subTest(label=label), self.assertRaisesRegex(
                BootstrapError,
                f"^{expected}$",
            ):
                _require_seed_pair_identity(execution, changed_plan)

    def test_candidate_plan_rejects_a_reserved_cupidbuild_source(self):
        checked_plan = json.loads(
            SEED_MANIFEST.read_text(encoding="utf-8")
        )["build_plan"]
        checked_plan["sources"].append(
            {
                "gnu_extensions": False,
                "name": "cupidbuild",
                "path": "/toolchain/untrusted-cupidbuild.cc",
            }
        )

        with self.assertRaisesRegex(
            BootstrapError,
            "^Linux build plan uses the reserved candidate source name: "
            "cupidbuild$",
        ):
            _candidate_build_plan(checked_plan)

    def test_recomputed_digest_cannot_change_the_source_plan(self):
        original = self._legacy_linux_manifest_fixture(
            json.loads(SEED_MANIFEST.read_text(encoding="utf-8"))
        )
        mutations = {
            "substituted source": lambda plan: plan["sources"][1].update(
                {"path": "/toolchain/elf32.cc"}
            ),
            "traversal source": lambda plan: plan["sources"][1].update(
                {"path": "/toolchain/../toolchain/ctool.cc"}
            ),
            "duplicate source path": lambda plan: plan["sources"][1].update(
                {"path": plan["sources"][3]["path"]}
            ),
            "unknown source key": lambda plan: plan["sources"][1].update(
                {"unrecorded": True}
            ),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory(
                prefix="cupid-bootstrap-plan-"
            ) as temporary:
                manifest = json.loads(json.dumps(original))
                mutate(manifest["build_plan"])
                encoded_plan = json.dumps(
                    manifest["build_plan"],
                    sort_keys=True,
                    separators=(",", ":"),
                    ensure_ascii=True,
                ).encode("ascii")
                manifest["build_plan_sha256"] = hashlib.sha256(
                    encoded_plan
                ).hexdigest()
                manifest_path = Path(temporary) / "manifest.json"
                manifest_path.write_text(
                    json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                    newline="\n",
                )

                result = subprocess.run(
                    [
                        sys.executable,
                        str(BOOTSTRAP_TOOL),
                        "verify",
                        "--manifest",
                        str(manifest_path),
                    ],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertRegex(
                result.stderr,
                "^bootstrap seed verification failed: "
                "(build plan sources differ|build source ctool keys differ)"
                "\n$",
            )

    def test_manifest_schema_rejects_unknown_duplicate_or_wrong_types(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-bootstrap-schema-"
        ) as temporary:
            original_text = SEED_MANIFEST.read_text(encoding="utf-8")
            cases = []

            manifest = json.loads(original_text)
            manifest["unrecorded"] = True
            cases.append(("unknown key", manifest, "manifest keys differ"))

            manifest = json.loads(original_text)
            manifest["build_plan"]["workers"] = 2.0
            cases.append(
                (
                    "floating worker count",
                    manifest,
                    "build plan workers type differs",
                )
            )

            manifest = json.loads(original_text)
            manifest["target"]["elf_class"] = 32.0
            cases.append(
                (
                    "floating target integer",
                    manifest,
                    "manifest target field type differs: elf_class",
                )
            )

            manifest = json.loads(original_text)
            manifest["artifacts"][0]["producer"] = 1
            cases.append(
                (
                    "integer producer flag",
                    manifest,
                    "artifact producer role type is invalid: cupidasm",
                )
            )

            manifest_path = Path(temporary) / "manifest.json"
            for label, manifest, expected in cases:
                with self.subTest(label=label):
                    manifest_path.write_text(
                        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                        newline="\n",
                    )
                    result = subprocess.run(
                        [
                            sys.executable,
                            str(BOOTSTRAP_TOOL),
                            "verify",
                            "--manifest",
                            str(manifest_path),
                        ],
                        cwd=REPO_ROOT,
                        text=True,
                        capture_output=True,
                    )
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(
                        result.stderr,
                        f"bootstrap seed verification failed: {expected}\n",
                    )

            manifest_path.write_text(
                '{"schema":"wrong",' + original_text.lstrip()[1:],
                encoding="utf-8",
                newline="\n",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(BOOTSTRAP_TOOL),
                    "verify",
                    "--manifest",
                    str(manifest_path),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                result.stderr,
                "bootstrap seed verification failed: "
                "manifest contains duplicate JSON key: schema\n",
            )

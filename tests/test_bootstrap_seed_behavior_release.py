"""Reuse reviewed tool bytes without replacing the SDK's producer observations."""

import copy
from dataclasses import replace
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from tools import bootstrap_stage_release as release
from tools import bootstrap_toolchain as seed
from tools import seed_release_identity as pins


ROOT = Path(__file__).resolve().parents[1]


class SeedBehaviorReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cupid-seed-behavior-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        seed_root = self.root / "seed"
        shutil.copytree(ROOT / "bootstrap/seeds/i386-linux", seed_root)
        self.parent = seed.verify_seed_inputs(seed_root / "manifest.json")
        self.path = self.root / "reviewed-release.json"
        self.payload = pins.encode_release_identity()
        self.path.write_bytes(self.payload)
        self.plan = copy.deepcopy(self.parent.manifest["build_plan"])
        self.stages = []
        for name in ("stage-three", "stage-four"):
            directory = self.root / name
            directory.mkdir()
            tools = {}
            for role, contents in self.parent.artifact_bytes:
                path = directory / (role + ".elf")
                path.write_bytes(contents)
                tools[role] = path
            # The native publication author owns object comparisons. A reviewed
            # tool cohort does not turn these independent observations into proof.
            obj = directory / "independent.o"
            obj.write_bytes(name.encode("ascii"))
            self.stages.append(seed.Stage({"independent": obj}, tools))
        self.source = seed.SourceInputs(self.root, {"sdk.cc": {"size": 1, "sha256": "a" * 64}})

    def request(self):
        return release.capture_seed_behavior_release(self.path, self.parent)

    def authorize(self, request, **changes):
        arguments = dict(source_inputs=self.source, source_root=self.root,
                         linux_plan=self.plan, windows_plan={}, linux_seed=self.parent,
                         windows_seed=None, stage_three=self.stages[0],
                         stage_four=self.stages[1], format_name="elf32")
        arguments.update(changes)
        return request.authorize(**arguments)

    def test_exact_tools_reuse_reviewed_fixture_without_claiming_sdk_source(self):
        source_before = copy.deepcopy(self.source.inventory)
        object_before = [stage.objects["independent"].read_bytes() for stage in self.stages]
        request = self.request()
        authority = self.authorize(request)
        manifest = seed._materialize_behavior_seed(replace(self.parent, behavior_release=authority),
                                                  self.root, "behavior", self.stages[1])
        document = json.loads(manifest.read_bytes())
        self.assertEqual(document["provenance"]["source_input_count"], seed.PROMOTED_SOURCE_INPUT_COUNT)
        self.assertNotEqual(document["provenance"]["source_input_count"], len(self.source.inventory))
        self.assertEqual((manifest.parent / "seed-release.json").read_bytes(), self.payload)
        self.assertEqual(self.source.inventory, source_before)
        self.assertEqual([stage.objects["independent"].read_bytes() for stage in self.stages], object_before)
        self.assertEqual(request.source_revision, seed.PROMOTED_SOURCE_REVISION)
        self.assertEqual(request.snapshot_sha256, seed.PROMOTED_SOURCE_SNAPSHOT_SHA256)
        authority.identity["source_revision"] = "0" * 40
        self.assertEqual(self.authorize(request).identity["source_revision"], seed.PROMOTED_SOURCE_REVISION)
        request.require_live()

    def test_every_unreviewed_release_claim_is_rejected(self):
        original = json.loads(self.payload)
        for name in original.keys() - {"artifacts"}:
            with self.subTest(field=name):
                changed = copy.deepcopy(original)
                changed[name] = original[name] + 1 if type(original[name]) is int else "changed"
                self.path.write_bytes(release._encode(changed))
                with self.assertRaisesRegex(seed.BootstrapError, "behavior seed release is invalid"):
                    self.request()
        self.path.write_bytes(self.payload)
        self.authorize(self.request())

    def test_duplicate_keys_and_artifact_identity_changes_are_rejected(self):
        original = json.loads(self.payload)
        for mutation in ("duplicate-key", "duplicate-role", "digest", "boolean-size", "missing-role"):
            changed = copy.deepcopy(original)
            if mutation == "duplicate-role":
                changed["artifacts"][1] = changed["artifacts"][0]
            elif mutation == "digest":
                changed["artifacts"][0]["sha256"] = "0" * 64
            elif mutation == "boolean-size":
                changed["artifacts"][0]["size"] = True
            elif mutation == "missing-role":
                changed["artifacts"].pop()
            payload = (b'{"schema":"a","schema":"b"}' if mutation == "duplicate-key"
                       else release._encode(changed))
            self.path.write_bytes(payload)
            with self.subTest(mutation=mutation), self.assertRaises(seed.BootstrapError):
                self.request()

    def test_both_stages_require_exact_tool_membership_and_bytes(self):
        request = self.request()
        for index in range(2):
            stage = self.stages[index]
            original = stage.tools["cupidobj"].read_bytes()
            stage.tools["cupidobj"].write_bytes(b"different actual tool")
            with self.subTest(stage=index, mutation="bytes"), self.assertRaisesRegex(
                    seed.BootstrapError, "stage tools differ"):
                self.authorize(request)
            stage.tools["cupidobj"].write_bytes(original)
            for tools in ({key: value for key, value in stage.tools.items() if key != "cupidobj"},
                          {**stage.tools, "extra": stage.tools["cupidobj"]}):
                with self.subTest(stage=index, mutation="membership"), self.assertRaisesRegex(
                        seed.BootstrapError, "stage tools differ"):
                    self.authorize(request, **{("stage_three" if index == 0 else "stage_four"):
                                               seed.Stage(stage.objects, tools)})
        self.authorize(request)

    def test_seed_selection_format_and_plan_cannot_change(self):
        request = self.request()
        for changes in ({"format_name": "pe32"}, {"windows_seed": self.parent},
                        {"linux_seed": replace(self.parent, manifest_bytes=b"{}")},
                        {"linux_seed": replace(self.parent, artifact_bytes=())}):
            with self.subTest(changes=list(changes)), self.assertRaisesRegex(
                    seed.BootstrapError, "selection differs"):
                self.authorize(request, **changes)
        changed_plan = copy.deepcopy(self.plan)
        changed_plan["workers"] += 1
        with self.assertRaisesRegex(seed.BootstrapError, "plan differs"):
            self.authorize(request, linux_plan=changed_plan)
        with self.assertRaisesRegex(seed.BootstrapError, "plan differs"):
            self.authorize(replace(request, linux_plan_bytes=release._encode(changed_plan)))
        self.authorize(request)

    def test_release_and_selected_seed_drift_fail_before_materialization(self):
        request = self.request()
        for path, changed, diagnostic in (
                (self.path, self.payload + b" \n", "caller seed release changed"),
                (self.parent.live_manifest_path, b"{}", "seed manifest changed"),
                (self.parent.tools["cupidobj"], b"different seed", "seed artifact changed")):
            original = path.read_bytes()
            path.write_bytes(changed)
            with self.subTest(path=path.name), self.assertRaisesRegex(seed.BootstrapError, diagnostic):
                self.authorize(request)
            self.assertFalse((self.root / "behavior").exists())
            path.write_bytes(original)
        self.authorize(request)

    def test_capture_rejects_mismatched_owned_selection(self):
        with self.assertRaisesRegex(seed.BootstrapError, "selection differs"):
            release.capture_seed_behavior_release(self.path, replace(self.parent, manifest_bytes=b"{}"))
        with self.assertRaisesRegex(seed.BootstrapError, "selection differs"):
            release.capture_seed_behavior_release(self.path, replace(self.parent, artifact_bytes=()))

    def test_release_size_and_regular_kind_are_bounded(self):
        for payload in (b"", b" " * (release.MAX_RELEASE_BYTES + 1)):
            self.path.write_bytes(payload)
            with self.subTest(size=len(payload)), self.assertRaises(seed.BootstrapError):
                self.request()
        self.path.unlink()
        self.path.mkdir()
        with self.assertRaisesRegex(seed.BootstrapError, "regular, unlinked"):
            self.request()

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_linked_release_and_staged_tools_are_rejected(self):
        target = self.root / "release-target.json"
        self.path.rename(target)
        self.path.symlink_to(target)
        with self.assertRaisesRegex(seed.BootstrapError, "regular, unlinked"):
            self.request()
        self.path.unlink()
        target.rename(self.path)
        request = self.request()
        tool = self.stages[0].tools["cupidobj"]
        target = tool.with_name("actual-cupidobj.elf")
        tool.rename(target)
        tool.symlink_to(target)
        with self.assertRaisesRegex(seed.BootstrapError, "regular, unlinked"):
            self.authorize(request)


if __name__ == "__main__":
    unittest.main()

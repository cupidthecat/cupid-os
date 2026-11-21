"""The current producer may consume an already captured owner-plan seed."""
import copy
import json
import unittest

from tests import test_toolchain_manifest_contract as fixture
from tools import bootstrap_toolchain as bootstrap


OWNER_PLAN = "e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa"


def owner_seed_request(mode, *, profile=None, context_options=None, raw_drift=False):
    """Synthetic policy facts; this does not qualify an executable seed cohort."""
    manifest, observations, old_seed, seed_observations = fixture._owner_context_fixture(
        **(profile or {}))
    document = json.loads(old_seed)
    document["build_plan"] = bootstrap._candidate_build_plan(document["build_plan"])
    document["build_plan_sha256"] = bootstrap._build_plan_sha256(document["build_plan"])
    assert document["build_plan_sha256"] == OWNER_PLAN
    bootstrap._validate_build_plan(document, promoted=True)
    seed_bytes = fixture._json_bytes(document)
    manifest["bootstrap"]["seed_manifest"]["sha256"] = fixture._digest(seed_bytes)
    context = fixture._seed_context_bytes(seed_bytes, **(context_options or {}))
    builder = fixture._author_request if mode == "author" else fixture._request
    options = {}
    if mode == "author":
        options["bootstrap_object_pairs"] = fixture._matching_bootstrap_pairs(
            (*fixture.BOOTSTRAP_OBJECT_NAMES, *fixture.COMPLETE_ISO_OBJECTS,
             *fixture.DISK_FOUNDATION_OBJECTS, *fixture.OWNER_OBJECTS), "bootstrap-object")
    payload = builder(manifest=manifest, observations=observations,
                      seed_manifest_bytes=seed_bytes + (b" " if raw_drift else b""),
                      seed_observations=seed_observations, **options)
    magic = b"CUPMAN5\0" if mode == "author" else b"CUPMAN6\0"
    return magic + context + payload[8:], manifest, document


class ToolchainOwnerSeedContextTests(unittest.TestCase):
    setUpClass = fixture.ToolchainManifestContractTests.__dict__["setUpClass"]
    tearDownClass = fixture.ToolchainManifestContractTests.__dict__["tearDownClass"]
    setUp = fixture.ToolchainManifestContractTests.setUp
    tearDown = fixture.ToolchainManifestContractTests.tearDown
    run_request = fixture.ToolchainManifestContractTests.run_request
    run_author_request = fixture.ToolchainManifestContractTests.run_author_request

    def invoke(self, mode, payload):
        return self.run_author_request(payload) if mode == "author" else self.run_request(payload)

    def test_current_owner_seed_context_accepts_all_publication_profiles(self):
        for long_paths, aliases in ((False, False), (True, False),
                                    (False, True), (True, True)):
            for mode in ("author", "check"):
                with self.subTest(long_paths=long_paths, aliases=aliases, mode=mode):
                    payload, expected, _seed = owner_seed_request(mode, profile={
                        "long_paths": long_paths, "user_link_aliases": aliases})
                    result = self.invoke(mode, payload)
                    self.assertEqual((result.returncode, result.stderr), (0, ""))
                    actual = json.loads(result.stdout)
                    if mode == "author":
                        self.assertEqual(actual, expected)
                    else:
                        self.assertEqual(actual["artifact_count"], 22)
                        self.assertEqual(actual["bootstrap_source_input_count"],
                                         expected["bootstrap"]["source_inputs"]["count"])

    def test_current_owner_seed_context_rejects_unknown_plans_and_changed_facts(self):
        _payload, _manifest, document = owner_seed_request("check")
        facts = [(row["file"], row["size"], row["sha256"])
                 for row in sorted(document["artifacts"], key=lambda row: row["file"])]
        changed_size = copy.deepcopy(facts)
        changed_size[0] = (facts[0][0], 0, facts[0][2])
        changed_digest = copy.deepcopy(facts)
        changed_digest[0] = (facts[0][0], facts[0][1], "0" * 64)
        variants = [{"plan_digest": "0" * 64}, {"manifest_digest": "0" * 64},
                    *({"facts": rows} for rows in (changed_size, changed_digest,
                       facts[::-1], facts[:-1], facts + [facts[0]],
                       [facts[1], *facts[1:]]))]
        for mode in ("author", "check"):
            for options in variants:
                with self.subTest(mode=mode, options=options):
                    payload, _manifest, _seed = owner_seed_request(mode, context_options=options)
                    result = self.invoke(mode, payload)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertTrue(result.stderr.startswith("Cupid Toolchain manifest contract failed:"))

    def test_current_owner_seed_context_hashes_the_original_manifest_bytes(self):
        for mode in ("author", "check"):
            with self.subTest(mode=mode):
                payload, _manifest, _seed = owner_seed_request(mode, raw_drift=True)
                result = self.invoke(mode, payload)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()

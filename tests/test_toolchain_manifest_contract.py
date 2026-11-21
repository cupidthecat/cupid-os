## @file
#  @brief Verify the checked toolchain manifest reader and author.
import copy
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.toolchain_manifest_fixtures import (
    ARTIFACT_NAMES,
    BOOTSTRAP_OBJECT_NAMES,
    BOOTSTRAP_PATHS,
    BOOTSTRAP_TOOL_NAMES,
    BUILD_PLAN_SHA256,
    COMPLETE_ISO_OBJECTS,
    DISK_FOUNDATION_OBJECTS,
    INPUT_PATHS,
    OBJECT_COMPARISON_NAMES,
    OWNER_BOOTSTRAP_INPUTS,
    OWNER_HEADERS,
    OWNER_OBJECTS,
    OWNER_SHARED_INPUTS,
    REPO_ROOT,
    _append_bytes,
    _author_request,
    _build_contract,
    _captured_seed_fixture,
    _complete_context_fixture,
    _complete_context_request,
    _context_request,
    _digest,
    _digest_size,
    _disk_context_fixture,
    _disk_context_request,
    _fixture,
    _json_bytes,
    _matching_bootstrap_pairs,
    _matching_executable_pairs,
    _matching_object_pairs,
    _owner_context_fixture,
    _owner_context_request,
    _profile_fixture,
    _request,
    _seed_context_bytes,
    _seed_fixture,
)
from tools import cupidc_toolchain_contracts


class ToolchainManifestContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract_build = tempfile.TemporaryDirectory(
            prefix=".toolchain-manifest-contract-"
        )
        cls.contract = _build_contract(Path(cls.contract_build.name))

    @classmethod
    def tearDownClass(cls):
        cls.contract_build.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.request_path = Path(self.temporary.name) / "request.bin"

    def tearDown(self):
        self.temporary.cleanup()

    def run_request(self, payload):
        self.request_path.write_bytes(payload)
        return subprocess.run(
            [self.contract, "check", self.request_path],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=10,
        )

    def run_author_request(self, payload):
        self.request_path.write_bytes(payload)
        return subprocess.run(
            [self.contract, "author", self.request_path],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=10,
        )

    def assert_contract_failure(self, payload):
        result = self.run_request(payload)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertTrue(
            result.stderr.startswith(
                "Cupid Toolchain manifest contract failed:"
            ),
            result.stderr,
        )

    def assert_author_failure(self, payload):
        result = self.run_author_request(payload)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertTrue(
            result.stderr.startswith(
                "Cupid Toolchain manifest contract failed:"
            ),
            result.stderr,
        )

    def test_valid_snapshot_emits_canonical_report(self):
        manifest, observations = _fixture()
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            '{"artifact_count":22,"artifact_total_bytes":682,'
            '"bootstrap_source_input_count":80,"input_count":92,'
            '"schema":"cupid.toolchain-manifest-verification.v1"}\n',
        )
        self.assertEqual(result.stderr, "")

    def test_author_emits_the_exact_canonical_manifest_from_facts(self):
        manifest, observations = _fixture()

        result = self.run_author_request(
            _author_request(manifest=manifest, observations=observations)
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout.encode("ascii"),
            (
                json.dumps(manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii"),
        )
        self.assertEqual(result.stderr, "")

    def test_captured_seed_author_accepts_candidate_plan_without_manifest_pins(self):
        seed_bytes, observations = _captured_seed_fixture()
        result = self.run_author_request(_context_request("author", seed_bytes=seed_bytes, observations=observations))
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        manifest = json.loads(result.stdout)
        self.assertEqual(manifest["bootstrap"]["seed_manifest"]["sha256"], _digest(seed_bytes))
        self.assertEqual(manifest["tool_fixed_point"]["c_objects"], 29)

    def test_captured_seed_verifier_accepts_candidate_plan_without_manifest_pins(self):
        result = self.run_request(_context_request("check"))
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        self.assertEqual(json.loads(result.stdout)["artifact_count"], 22)

    def test_complete_iso_producer_author_and_verifier_bind_every_profile(self):
        for long_paths, aliases, count in ((False, False, 90), (True, False, 91),
                                          (False, True, 91), (True, True, 92)):
            with self.subTest(long_paths=long_paths, aliases=aliases):
                manifest, _observations, _seed, _facts = _complete_context_fixture(
                    long_paths=long_paths, user_link_aliases=aliases)
                result = self.run_author_request(_complete_context_request(
                    "author", long_paths=long_paths, user_link_aliases=aliases))
                self.assertEqual((result.returncode, result.stderr), (0, ""))
                self.assertEqual(result.stdout.encode("ascii"),
                    (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("ascii"))
                reversed_inputs = [(path, 1, row["size"], row["sha256"])
                    for path, row in reversed(sorted(manifest["inputs"].items()))]
                shuffled = self.run_author_request(_complete_context_request("author",
                    long_paths=long_paths, user_link_aliases=aliases, input_observations=reversed_inputs))
                self.assertEqual((shuffled.returncode, shuffled.stdout, shuffled.stderr), (0, result.stdout, ""))
                result = self.run_request(_complete_context_request(
                    "check", long_paths=long_paths, user_link_aliases=aliases))
                self.assertEqual((result.returncode, result.stderr), (0, ""))
                self.assertEqual(json.loads(result.stdout)["bootstrap_source_input_count"], count)
                reversed_manifest = dict(reversed(list(manifest.items())))
                result = self.run_request(_complete_context_request("check", long_paths=long_paths,
                    user_link_aliases=aliases, manifest_bytes=json.dumps(reversed_manifest).encode("ascii")))
                self.assertEqual((result.returncode, result.stderr), (0, ""))

    def test_complete_iso_producer_rejects_each_missing_or_replaced_input(self):
        original, _observations, _seed, _facts = _complete_context_fixture()
        for name in COMPLETE_ISO_OBJECTS:
            for suffix in (".cc", ".h"):
                path = "toolchain/" + name + suffix
                for replacement in (None, "toolchain/unreviewed" + suffix):
                    manifest = copy.deepcopy(original)
                    inputs = manifest["bootstrap"]["source_inputs"]
                    row = inputs["files"].pop(path)
                    if replacement is not None:
                        inputs["files"][replacement] = row
                    inputs["count"] = len(inputs["files"])
                    inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
                    for mode in ("author", "check"):
                        with self.subTest(path=path, replacement=replacement, mode=mode):
                            run = self.run_author_request if mode == "author" else self.run_request
                            result = run(_complete_context_request(mode, manifest=manifest))
                            self.assertEqual(result.returncode, 1, result.stderr)
                            self.assertEqual(result.stdout, "")

    def test_complete_iso_producer_rejects_each_absent_or_changed_raw_object_pair(self):
        original = _matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS), "bootstrap-object")
        for name in COMPLETE_ISO_OBJECTS:
            position = next(index for index, row in enumerate(original) if row[0] == name)
            row = original[position]
            missing = original[:position] + original[position + 1:]
            changed = list(original)
            changed[position] = (*row[:4], row[4] + b"drift")
            duplicate = list(original)
            duplicate[position] = original[0]
            for pairs in (missing, changed, duplicate):
                with self.subTest(object=name, pairs=len(pairs), last=pairs[position - 1][0]):
                    self.assert_author_failure(_complete_context_request("author", bootstrap_object_pairs=pairs))
        self.assertEqual(self.run_author_request(_complete_context_request("author")).returncode, 0)

    def test_complete_iso_publication_retains_each_new_header_input(self):
        original, _observations, _seed, _facts = _complete_context_fixture()
        for name in COMPLETE_ISO_OBJECTS:
            path = "toolchain/" + name + ".h"
            for replacement in (None, "toolchain/unreviewed.h"):
                manifest = copy.deepcopy(original)
                row = manifest["inputs"].pop(path)
                if replacement is not None:
                    manifest["inputs"][replacement] = row
                manifest["input_count"] = len(manifest["inputs"])
                for mode in ("author", "check"):
                    with self.subTest(path=path, replacement=replacement, mode=mode):
                        run = self.run_author_request if mode == "author" else self.run_request
                        result = run(_complete_context_request(mode, manifest=manifest))
                        self.assertEqual(result.returncode, 1, result.stderr)
                        self.assertEqual(result.stdout, "")

    def test_disk_producer_author_and_verifier_bind_all_profiles_and_inventories(self):
        for long_paths, aliases, count in ((False, False, 97), (True, False, 98),
                                          (False, True, 98), (True, True, 99)):
            for release_inputs in (False, True):
                profile = dict(long_paths=long_paths, user_link_aliases=aliases,
                               release_inputs=release_inputs)
                with self.subTest(**profile):
                    manifest, _observations, _seed, _facts = _disk_context_fixture(**profile)
                    self.assertEqual(manifest["input_count"], 104 if release_inputs else 100)
                    self.assertEqual(manifest["bootstrap"]["source_inputs"]["count"], count)
                    authored = self.run_author_request(_disk_context_request("author", **profile))
                    self.assertEqual((authored.returncode, authored.stderr), (0, ""))
                    self.assertEqual(authored.stdout, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                    reversed_inputs = [(path, 1, row["size"], row["sha256"])
                        for path, row in reversed(sorted(manifest["inputs"].items()))]
                    shuffled = self.run_author_request(_disk_context_request("author", **profile,
                        input_observations=reversed_inputs))
                    self.assertEqual((shuffled.returncode, shuffled.stdout, shuffled.stderr), (0, authored.stdout, ""))
                    verified = self.run_request(_disk_context_request("check", **profile))
                    self.assertEqual((verified.returncode, verified.stderr), (0, ""))
                    report = json.loads(verified.stdout)
                    self.assertEqual((report["input_count"], report["bootstrap_source_input_count"]),
                        (manifest["input_count"], count))

    def test_owner_producer_author_and_verifier_bind_all_current_profiles(self):
        for long_paths, aliases, count in ((False, False, 112), (True, False, 113),
                                          (False, True, 113), (True, True, 114)):
            for release_inputs in (False, True):
                profile = dict(long_paths=long_paths, user_link_aliases=aliases, release_inputs=release_inputs)
                with self.subTest(**profile):
                    manifest, _observations, _seed, _facts = _owner_context_fixture(**profile)
                    self.assertEqual(manifest['input_count'], 112 if release_inputs else 108)
                    self.assertEqual(manifest['bootstrap']['source_inputs']['count'], count)
                    authored = self.run_author_request(_owner_context_request('author', **profile))
                    self.assertEqual(authored.returncode, 0, authored.stderr)
                    self.assertEqual(json.loads(authored.stdout), manifest)
                    verified = self.run_request(_owner_context_request('check', **profile))
                    self.assertEqual(verified.returncode, 0, verified.stderr)

    def test_owner_release_keeps_the_spelling_fixture_in_each_profile(self):
        for long_paths, aliases in ((False, False), (True, False), (False, True), (True, True)):
            profile = dict(source_spelling=True, long_paths=long_paths, user_link_aliases=aliases)
            with self.subTest(**profile):
                manifest, *_ = _owner_context_fixture(**profile)
                self.assertEqual(manifest['input_count'], 113)
                authored = self.run_author_request(_owner_context_request('author', **profile))
                self.assertEqual(authored.returncode, 0, authored.stderr)
                self.assertEqual(authored.stdout, json.dumps(manifest, indent=2, sort_keys=True) + '\n')
                checked = self.run_request(_owner_context_request('check', **profile))
                self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_owner_release_rejects_missing_replaced_or_crossed_spelling_inputs(self):
        original, *_ = _owner_context_fixture(source_spelling=True)
        path = 'toolchain/tests/cupidc_source_spelling_fixture.h'
        mutations = []
        changed = copy.deepcopy(original)
        del changed['inputs'][path]
        mutations.append((changed, ('check',)))
        changed = copy.deepcopy(original)
        changed['inputs']['toolchain/tests/unknown_spelling_fixture.h'] = changed['inputs'].pop(path)
        mutations.append((changed, ('author', 'check')))
        changed = copy.deepcopy(original)
        changed['input_count'] = 112
        mutations.append((changed, ('check',)))
        for index, (changed, modes) in enumerate(mutations):
            for mode in modes:
                with self.subTest(index=index, mode=mode):
                    run = self.run_author_request if mode == 'author' else self.run_request
                    result = run(_owner_context_request(mode, manifest=changed, source_spelling=True))
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stdout, '')

        observations = [(name, 0 if name == path else 1,
                         0 if name == path else row['size'],
                         '0' * 64 if name == path else row['sha256'])
                        for name, row in sorted(original['inputs'].items())]
        for mode in ('author', 'check'):
            with self.subTest(missing_observation=True, mode=mode):
                run = self.run_author_request if mode == 'author' else self.run_request
                result = run(_owner_context_request(mode, source_spelling=True,
                             input_observations=observations))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, '')

    def test_current_release_requires_the_jpeg_and_pe32_headers(self):
        manifest, *_ = _owner_context_fixture(source_spelling=True)
        jpeg = 'toolchain/cupidbuild_jpeg.h'
        pe32 = 'toolchain/tests/cupidld_pe32_cases.inc'
        for path in (jpeg, pe32):
            data = (REPO_ROOT / path).read_bytes()
            manifest['inputs'][path] = {'size': len(data), 'sha256': _digest(data)}
        manifest['input_count'] = 115
        sources = manifest['bootstrap']['source_inputs']
        sources['files'][jpeg] = manifest['inputs'][jpeg].copy()
        sources['count'] = len(sources['files'])
        sources['sha256'] = _digest(_json_bytes(sources['files']))
        missing_jpeg = copy.deepcopy(manifest)
        missing_sources = missing_jpeg['bootstrap']['source_inputs']
        del missing_sources['files'][jpeg]
        missing_sources['count'] -= 1
        missing_sources['sha256'] = _digest(_json_bytes(missing_sources['files']))
        replaced_pe32 = copy.deepcopy(manifest)
        replaced_pe32['inputs']['toolchain/tests/unknown.inc'] = replaced_pe32['inputs'].pop(pe32)
        for mode in ('author', 'check'):
            run = self.run_author_request if mode == 'author' else self.run_request
            good = run(_owner_context_request(mode, manifest=manifest))
            self.assertEqual(good.returncode, 0, good.stderr)
            for mutation, changed in (('missing JPEG', missing_jpeg), ('replaced PE32', replaced_pe32)):
                with self.subTest(mode=mode, mutation=mutation):
                    result = run(_owner_context_request(mode, manifest=changed))
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stdout, '')

    def test_split_x86_inventory_requires_both_includes(self):
        manifest, *_ = _owner_context_fixture(source_spelling=True)
        jpeg = "toolchain/cupidbuild_jpeg.h"
        x86_paths = ("toolchain/tests/x86_integer_contract.inc",
                     "toolchain/tests/x86_operand_contract.inc")
        for name in (jpeg, "toolchain/tests/cupidld_pe32_cases.inc", *x86_paths):
            data = (REPO_ROOT / name).read_bytes()
            manifest["inputs"][name] = {"size": len(data), "sha256": _digest(data)}
        manifest["input_count"] = len(manifest["inputs"])
        sources = manifest["bootstrap"]["source_inputs"]
        sources["files"][jpeg] = manifest["inputs"][jpeg].copy()
        sources["count"] = len(sources["files"])
        sources["sha256"] = _digest(_json_bytes(sources["files"]))
        for mode in ("author", "check"):
            run = self.run_author_request if mode == "author" else self.run_request
            good = run(_owner_context_request(mode, manifest=manifest))
            self.assertEqual(good.returncode, 0, good.stderr)
            for name in x86_paths:
                with self.subTest(mode=mode, path=name):
                    changed = copy.deepcopy(manifest)
                    changed["inputs"]["toolchain/tests/unreviewed.inc"] = changed["inputs"].pop(name)
                    bad = run(_owner_context_request(mode, manifest=changed))
                    self.assertEqual(bad.returncode, 1, bad.stderr)
                    self.assertEqual(bad.stdout, "")
                    del changed["inputs"]["toolchain/tests/unreviewed.inc"]
                    changed["input_count"] -= 1
                    bad = run(_owner_context_request(mode, manifest=changed))
                    self.assertEqual(bad.returncode, 1, bad.stderr)
                    self.assertEqual(bad.stdout, "")

    def test_historical_owner_rejects_a_spelling_header_in_place_of_an_old_member(self):
        original, *_ = _owner_context_fixture()
        path = 'toolchain/tests/cupidc_source_spelling_fixture.h'
        data = (REPO_ROOT / path).read_bytes()
        del original['inputs']['toolchain/cupidbuild_source_path.h']
        original['inputs'][path] = {'size': len(data), 'sha256': _digest(data)}
        for mode in ('author', 'check'):
            with self.subTest(mode=mode):
                run = self.run_author_request if mode == 'author' else self.run_request
                result = run(_owner_context_request(mode, manifest=original))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, '')

    def test_owner_producer_rejects_missing_or_replaced_new_inputs(self):
        original, _observations, _seed, _facts = _owner_context_fixture()
        for inventory, paths in (('bootstrap', OWNER_BOOTSTRAP_INPUTS),
                                 ('inputs', (*OWNER_HEADERS, *OWNER_SHARED_INPUTS))):
            for path in paths:
                for replacement in (None, 'toolchain/unexpected-owner-input.cc'):
                    changed = copy.deepcopy(original)
                    rows = changed['bootstrap']['source_inputs']['files'] if inventory == 'bootstrap' else changed['inputs']
                    row = rows.pop(path)
                    if replacement:
                        rows[replacement] = row
                    if inventory == 'bootstrap':
                        inputs = changed['bootstrap']['source_inputs']
                        inputs['count'] = len(rows)
                        inputs['sha256'] = _digest(_json_bytes(rows))
                    else:
                        changed['input_count'] = len(rows)
                    for mode in ('author', 'check'):
                        with self.subTest(inventory=inventory, path=path, replacement=replacement, mode=mode):
                            run = self.run_author_request if mode == 'author' else self.run_request
                            result = run(_owner_context_request(mode, manifest=changed))
                            self.assertEqual(result.returncode, 1, result.stderr)
                            self.assertEqual(result.stdout, '')

    def test_owner_producer_compares_every_new_raw_object_pair(self):
        original = _matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS,
                                              *DISK_FOUNDATION_OBJECTS, *OWNER_OBJECTS), 'bootstrap-object')
        for name in OWNER_OBJECTS:
            position = next(index for index, row in enumerate(original) if row[0] == name)
            row = original[position]
            changed = list(original)
            changed[position] = (*row[:4], row[4] + b'drift')
            duplicate = list(original)
            duplicate[position] = original[0]
            for pairs in (original[:position] + original[position + 1:], changed, duplicate):
                with self.subTest(name=name, count=len(pairs)):
                    self.assert_author_failure(_owner_context_request('author', bootstrap_object_pairs=pairs))

    def test_owner_producer_rejects_crossed_historical_plan_and_object_profiles(self):
        original, _observations, _seed, _facts = _owner_context_fixture()
        for target, field, values in (('bootstrap', 'build_plan_sha256',
                ('808d9a566c3dd200252cb6ca974dfa19992a923867dc60f500797efd5169c73e', BUILD_PLAN_SHA256)),
                ('tool_fixed_point', 'c_objects', (29, 34, 37, 43, 45))):
            for value in values:
                changed = copy.deepcopy(original)
                changed[target][field] = value
                self.assert_contract_failure(_owner_context_request('check', manifest=changed))

    def test_disk_producer_requires_every_source_header_and_unicode_table(self):
        original, _observations, _seed, _facts = _disk_context_fixture()
        bootstrap_paths = ["toolchain/" + name + suffix for name in DISK_FOUNDATION_OBJECTS
                           for suffix in (".cc", ".h")] + ["toolchain/fat16_name_profiles.inc"]
        for inventory, paths in (("bootstrap", bootstrap_paths),
                                 ("inputs", [path for path in bootstrap_paths if path.endswith(".h")])):
            for path in paths:
                for replacement in (None, "toolchain/unreviewed" + Path(path).suffix):
                    changed = copy.deepcopy(original)
                    rows = changed["bootstrap"]["source_inputs"]["files"] if inventory == "bootstrap" else changed["inputs"]
                    row = rows.pop(path)
                    if replacement is not None:
                        rows[replacement] = row
                    if inventory == "bootstrap":
                        inputs = changed["bootstrap"]["source_inputs"]
                        inputs["count"] = len(rows)
                        inputs["sha256"] = _digest(_json_bytes(rows))
                    else:
                        changed["input_count"] = len(rows)
                    for mode in ("author", "check"):
                        with self.subTest(inventory=inventory, path=path, replacement=replacement, mode=mode):
                            run = self.run_author_request if mode == "author" else self.run_request
                            result = run(_disk_context_request(mode, manifest=changed))
                            self.assertEqual(result.returncode, 1, result.stderr)
                            self.assertEqual(result.stdout, "")

    def test_disk_producer_compares_every_new_raw_object_pair(self):
        original = _matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS,
                                              *DISK_FOUNDATION_OBJECTS), "bootstrap-object")
        for name in DISK_FOUNDATION_OBJECTS:
            position = next(index for index, row in enumerate(original) if row[0] == name)
            row = original[position]
            absent = original[:position] + original[position + 1:]
            changed = list(original)
            changed[position] = (*row[:4], row[4] + b"drift")
            duplicate = list(original)
            duplicate[position] = original[0]
            for pairs in (absent, changed, duplicate):
                with self.subTest(name=name, count=len(pairs)):
                    self.assert_author_failure(_disk_context_request("author", bootstrap_object_pairs=pairs))
        self.assertEqual(self.run_author_request(_disk_context_request("author")).returncode, 0)

    def test_disk_producer_rejects_mixed_plan_counts_and_classic_envelopes(self):
        original, _observations, _seed, _facts = _disk_context_fixture()
        for target, field, values in (("bootstrap", "build_plan_sha256", (BUILD_PLAN_SHA256,
                  "ac8edd3ceb4e253439858bbe77c2674933517ec7939bcbe81f1b65ada0d921e3")),
                ("tool_fixed_point", "c_objects", (29, 34, 36, 38))):
            for value in values:
                changed = copy.deepcopy(original)
                changed[target][field] = value
                self.assert_contract_failure(_disk_context_request("check", manifest=changed))
        for mode in ("author", "check"):
            run = self.run_author_request if mode == "author" else self.run_request
            self.assertEqual(run(_disk_context_request(mode, captured=False)).returncode, 1)
            for profile in ({"release_inputs": False}, {"release_inputs": True}):
                fixture, _observations, _seed, _facts = _disk_context_fixture(**profile)
                fixture["bootstrap"]["source_inputs"] = _complete_context_fixture()[0]["bootstrap"]["source_inputs"]
                self.assertEqual(run(_disk_context_request(mode, manifest=fixture, **profile)).returncode, 1)

    def test_disk_verifier_binds_changed_header_and_table_observations(self):
        original, observations, seed_bytes, seed_observations = _disk_context_fixture()
        for inventory, paths in (("input_observations", ["toolchain/" + name + ".h" for name in DISK_FOUNDATION_OBJECTS]),
                                ("bootstrap_observations", ["toolchain/fat16_name_profiles.inc"])):
            rows = original["inputs"] if inventory == "input_observations" else original["bootstrap"]["source_inputs"]["files"]
            for path in paths:
                for field in ("size", "sha256"):
                    observed = [(name, 1, row["size"] + int(name == path and field == "size"),
                                 "0" * 64 if name == path and field == "sha256" else row["sha256"])
                                for name, row in sorted(rows.items())]
                    with self.subTest(inventory=inventory, path=path, field=field):
                        self.assert_contract_failure(_disk_context_request("check", **{inventory: observed}))

    def test_complete_iso_producer_rejects_mixed_plan_count_and_classic_envelopes(self):
        original, observations, seed_bytes, seed_observations = _complete_context_fixture()
        for field, value in (("plan", BUILD_PLAN_SHA256), ("count", 29), ("count", 33), ("count", 35)):
            manifest = copy.deepcopy(original)
            if field == "plan":
                manifest["bootstrap"]["build_plan_sha256"] = value
            else:
                manifest["tool_fixed_point"]["c_objects"] = value
            with self.subTest(field=field, value=value):
                self.assert_contract_failure(_complete_context_request("check", manifest=manifest))
        self.assert_author_failure(_author_request(manifest=original, observations=observations,
            seed_manifest_bytes=seed_bytes, seed_observations=seed_observations,
            bootstrap_object_pairs=_matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS), "bootstrap-object")))
        self.assert_contract_failure(_request(manifest=original, observations=observations,
            seed_manifest_bytes=seed_bytes, seed_observations=seed_observations))

    def test_captured_seed_context_still_binds_raw_manifest_and_each_tool_fact(self):
        seed_bytes, observations = _captured_seed_fixture()
        contexts = [_seed_context_bytes(seed_bytes, manifest_digest="0" * 64),
                    _seed_context_bytes(seed_bytes, plan_digest="1" * 64)]
        facts = [(name, size, digest) for name, _kind, size, digest in observations]
        contexts.extend(_seed_context_bytes(seed_bytes, facts=changed) for changed in (
            facts[:-1], [facts[0], *facts[:-1]], facts[::-1],
            [(facts[0][0], 0, facts[0][2]), *facts[1:]],
            [(facts[0][0], facts[0][1] + 1, facts[0][2]), *facts[1:]],
            [(facts[0][0], facts[0][1], "0" * 64), *facts[1:]],
        ))
        for mode in ("author", "check"):
            run = self.run_author_request if mode == "author" else self.run_request
            for context in contexts:
                with self.subTest(mode=mode, context=context[:80]):
                    result = run(_context_request(mode, seed_bytes=seed_bytes, observations=observations, context=context))
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stdout, "")
            self.assertEqual(run(_context_request(mode)).returncode, 0)

    def test_captured_seed_requests_reject_prefix_truncation_and_trailing_data(self):
        for mode in ("author", "check"):
            run = self.run_author_request if mode == "author" else self.run_request
            payload = _context_request(mode)
            for changed in (*[payload[:length] for length in (0, 7, 12, 78, 144, 200, 650, len(payload) - 1)], payload + b"\0"):
                with self.subTest(mode=mode, length=len(changed)):
                    result = run(changed)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual(result.stdout, "")
            self.assertEqual(run(payload).returncode, 0)

    def test_captured_seed_author_still_compares_every_raw_pair_lane(self):
        seed_bytes, seed_observations = _captured_seed_fixture()
        for lane in ("object_pairs", "executable_pairs", "bootstrap_object_pairs",
                     "bootstrap_tool_pairs"):
            with self.subTest(lane=lane):
                manifest, observations = _fixture()
                manifest["bootstrap"]["seed_manifest"]["sha256"] = _digest(seed_bytes)
                pairs = {
                    "object_pairs": _matching_object_pairs(manifest),
                    "executable_pairs": _matching_executable_pairs(manifest),
                    "bootstrap_object_pairs": _matching_bootstrap_pairs(
                        BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"),
                    "bootstrap_tool_pairs": _matching_bootstrap_pairs(
                        BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"),
                }
                name, kind, payload, other_kind, _other = pairs[lane][0]
                pairs[lane][0] = (name, kind, payload, other_kind, payload + b"drift")
                classic = _author_request(
                    manifest=manifest, observations=observations,
                    seed_manifest_bytes=seed_bytes, seed_observations=seed_observations,
                    **pairs)
                request = b"CUPMAN5\0" + _seed_context_bytes(seed_bytes) + classic[8:]
                self.assert_author_failure(request)
        self.assertEqual(self.run_author_request(_context_request("author")).returncode, 0)

    def test_captured_seed_verifier_still_rejects_publication_artifact_drift(self):
        seed_bytes, seed_observations = _captured_seed_fixture()
        manifest, observations = _fixture()
        manifest["bootstrap"]["seed_manifest"]["sha256"] = _digest(seed_bytes)
        manifest["artifacts"][0]["sha256"] = "0" * 64
        classic = _request(
            manifest=manifest, observations=observations,
            seed_manifest_bytes=seed_bytes, seed_observations=seed_observations)
        result = self.run_request(b"CUPMAN6\0" + _seed_context_bytes(seed_bytes) + classic[8:])
        self.assertEqual((result.returncode, result.stdout), (1, ""))
        self.assertEqual(self.run_request(_context_request("check")).returncode, 0)

    def test_author_hashes_matching_stage_object_pairs(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        self.assertEqual(result.stderr, "")

    def test_author_rejects_mismatched_stage_object_bytes(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        name, first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        object_pairs[0] = (
            name,
            first_kind,
            first_bytes,
            second_kind,
            bytes([second_bytes[0] + 1]),
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    def test_author_rejects_truncated_stage_object_bytes(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        payload = _author_request(
            manifest=manifest,
            observations=observations,
            object_pairs=object_pairs,
        )
        name, first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        record = bytearray()
        _append_bytes(record, name.encode("ascii"))
        record.extend(struct.pack("<I", first_kind))
        _append_bytes(record, first_bytes)
        record.extend(struct.pack("<I", second_kind))
        _append_bytes(record, second_bytes)
        record_offset = payload.index(bytes(record))
        truncated = payload[: record_offset + len(record) - 1]

        self.assert_author_failure(truncated)

    def test_author_rejects_nonregular_stage_object_kind(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        name, _first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        object_pairs[0] = (
            name,
            2,
            first_bytes,
            second_kind,
            second_bytes,
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    def test_author_rejects_duplicate_stage_object_pair(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        object_pairs[-1] = object_pairs[0]

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    def test_author_recovers_after_rejected_stage_object_pair(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        rejected_pairs = list(object_pairs)
        name, first_kind, first_bytes, second_kind, _second_bytes = (
            rejected_pairs[0]
        )
        rejected_pairs[0] = (
            name,
            first_kind,
            first_bytes,
            second_kind,
            b"different",
        )
        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=rejected_pairs,
            )
        )

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        self.assertEqual(result.stderr, "")

    def test_author_decides_all_sixty_nine_stage_pairs(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        executable_pairs = _matching_executable_pairs(manifest)
        bootstrap_object_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
        )
        bootstrap_tool_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
        )

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
                executable_pairs=executable_pairs,
                bootstrap_object_pairs=bootstrap_object_pairs,
                bootstrap_tool_pairs=bootstrap_tool_pairs,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        self.assertEqual(
            sum(
                len(pairs)
                for pairs in (
                    object_pairs,
                    executable_pairs,
                    bootstrap_object_pairs,
                    bootstrap_tool_pairs,
                )
            ),
            69,
        )

    def test_author_rejects_mismatch_in_each_remaining_pair_lane(self):
        for lane in (
            "executable_pairs",
            "bootstrap_object_pairs",
            "bootstrap_tool_pairs",
        ):
            with self.subTest(lane=lane):
                manifest, observations = _fixture()
                pairs_by_lane = {
                    "executable_pairs": _matching_executable_pairs(
                        manifest
                    ),
                    "bootstrap_object_pairs": _matching_bootstrap_pairs(
                        BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
                    ),
                    "bootstrap_tool_pairs": _matching_bootstrap_pairs(
                        BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
                    ),
                }
                changed = list(pairs_by_lane[lane])
                (
                    name,
                    first_kind,
                    first_bytes,
                    second_kind,
                    second_bytes,
                ) = changed[0]
                changed[0] = (
                    name,
                    first_kind,
                    first_bytes,
                    second_kind,
                    second_bytes[:-1] + bytes([second_bytes[-1] ^ 1]),
                )
                pairs_by_lane[lane] = changed

                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        **pairs_by_lane,
                    )
                )

    def test_author_rejects_matching_executable_pair_with_wrong_artifact_fact(
        self,
    ):
        manifest, observations = _fixture()
        executable_pairs = _matching_executable_pairs(manifest)
        name, first_kind, _first_bytes, second_kind, _second_bytes = (
            executable_pairs[0]
        )
        changed_bytes = b"equal but not the published artifact"
        executable_pairs[0] = (
            name,
            first_kind,
            changed_bytes,
            second_kind,
            changed_bytes,
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                executable_pairs=executable_pairs,
            )
        )

    def test_author_rejects_mismatched_stage_object_sizes(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        name, first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        object_pairs[0] = (
            name,
            first_kind,
            first_bytes,
            second_kind,
            second_bytes + b"x",
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    @unittest.skipIf(
        os.name == "nt" and shutil.which("wsl") is None,
        "WSL is required to execute the checked Linux stage-four seed",
    )
    def test_checked_stage_four_author_builds_and_emits_oracle_bytes(self):
        seed_manifest = (
            REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupman4-checked-stage-four-", dir=REPO_ROOT
        ) as temporary:
            workspace = Path(temporary)
            stage_four = workspace / "stage-four"
            seed = cupidc_toolchain_contracts.freeze_seed_inputs(
                seed_manifest, stage_four
            )
            self.assertEqual(
                seed.manifest["provenance"]["seed_generation"],
                "stage-four",
            )
            runner = cupidc_toolchain_contracts.ToolRunner(REPO_ROOT)
            for name, logical_source, gnu_extensions in (
                ("ctool", "toolchain/ctool.cc", False),
                ("ctool_host", "toolchain/ctool_host.cc", False),
                (
                    "runtime",
                    "toolchain/hosted/i386-linux/runtime.cc",
                    True,
                ),
            ):
                output = stage_four / f"{name}.o"
                arguments = ["--root", REPO_ROOT]
                if gnu_extensions:
                    arguments.append("--gnu")
                arguments.extend(
                    (
                        "-c",
                        f"/{logical_source}",
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    )
                )
                result = runner.run(
                    seed.tools["cupidc"], tuple(arguments), 300
                )
                self.assertEqual(
                    (result.returncode, result.stdout, result.stderr),
                    (0, "", ""),
                    f"checked stage-four CupidC failed for {logical_source}",
                )
                cupidc_toolchain_contracts._validate_i386_relocatable(
                    output
                )

            executable = cupidc_toolchain_contracts._build_manifest_author(
                REPO_ROOT,
                stage_four,
                workspace / "author-build",
            )
            if os.name == "nt":
                self.assertEqual(executable.suffix, ".exe")
                cupidc_toolchain_contracts._validate_static_i386_pe32(
                    executable,
                    int(
                        cupidc_toolchain_contracts.EXPECTED_WINDOWS_TARGET[
                            "entry"
                        ]
                    ),
                    cupidc_toolchain_contracts.WINDOWS_TOOL_IMPORTS,
                )
            manifest, observations = _fixture()
            request = workspace / "request.bin"
            request.write_bytes(
                _author_request(
                    manifest=manifest,
                    observations=observations,
                )
            )
            native_commands: list[tuple[str, ...]] = []
            real_subprocess_run = subprocess.run

            def run_without_wsl(command, *arguments, **keywords):
                executable_name = Path(str(command[0])).name.lower()
                if executable_name in {"wsl", "wsl.exe"}:
                    raise AssertionError(
                        "the native PE author crossed the WSL boundary"
                    )
                native_commands.append(tuple(str(item) for item in command))
                return real_subprocess_run(command, *arguments, **keywords)

            with mock.patch.object(
                cupidc_toolchain_contracts.subprocess,
                "run",
                side_effect=run_without_wsl,
            ):
                result = runner.run(executable, ("author", request), 120)
            cupidc_toolchain_contracts.require_live_seed_inputs(seed)

            self.assertEqual(len(native_commands), 1)
            self.assertEqual(
                Path(native_commands[0][0]).suffix,
                ".exe" if os.name == "nt" else ".elf",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            self.assertEqual(
                result.stdout.encode("ascii"),
                (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
                    "ascii"
                ),
            )

            for aliases, long_paths in ((False, True), (True, False), (True, True)):
                with self.subTest(aliases=aliases, long_paths=long_paths):
                    manifest, observations = _profile_fixture(
                        user_link_aliases=aliases, long_paths=long_paths
                    )

                    def execute(mode, payload):
                        request.write_bytes(payload)
                        with mock.patch.object(
                            cupidc_toolchain_contracts.subprocess,
                            "run",
                            side_effect=run_without_wsl,
                        ):
                            return runner.run(executable, (mode, request), 120)

                    author_payload = _author_request(
                        manifest=manifest, observations=observations
                    )
                    canonical = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
                    authored = execute("author", author_payload)
                    self.assertEqual(authored.returncode, 0, authored.stderr)
                    self.assertEqual(authored.stdout, canonical)
                    checked = execute(
                        "check", _request(manifest=manifest, observations=observations)
                    )
                    self.assertEqual(checked.returncode, 0, checked.stderr)
                    self.assertEqual(
                        json.loads(checked.stdout)["bootstrap_source_input_count"],
                        manifest["bootstrap"]["source_inputs"]["count"],
                    )
                    if aliases:
                        for removed in (
                            "toolchain/hosted/i386-windows/final_path_start.asm",
                            BOOTSTRAP_PATHS[0],
                        ):
                            bad = copy.deepcopy(manifest)
                            inputs = bad["bootstrap"]["source_inputs"]
                            inputs["files"]["toolchain/unexpected.asm"] = inputs["files"].pop(removed)
                            inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
                            for mode, framing in (("author", _author_request), ("check", _request)):
                                rejected = execute(mode, framing(manifest=bad, observations=observations))
                                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                                self.assertEqual(rejected.stdout, "")
                        sources = manifest["bootstrap"]["source_inputs"]["files"]
                        duplicate = [
                            (path, 1, row["size"], row["sha256"])
                            for path, row in sorted(sources.items())
                        ]
                        duplicate[0] = duplicate[-1]
                        for mode, framing in (("author", _author_request), ("check", _request)):
                            rejected = execute(
                                mode, framing(manifest=manifest, observations=observations,
                                              bootstrap_observations=duplicate)
                            )
                            self.assertEqual(rejected.returncode, 1, rejected.stderr)
                            self.assertEqual(rejected.stdout, "")
                        recovered = execute(
                            "author", _author_request(manifest=manifest, observations=observations)
                        )
                        self.assertEqual(recovered.returncode, 0, recovered.stderr)
                        self.assertEqual(recovered.stdout, canonical)
            cupidc_toolchain_contracts.require_live_seed_inputs(seed)
            self.assertTrue(all(
                Path(command[0]).suffix == (".exe" if os.name == "nt" else ".elf")
                for command in native_commands
            ))

    def test_author_output_is_independent_of_fact_order(self):
        manifest, observations = _fixture()
        input_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        bootstrap_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(
                manifest["bootstrap"]["source_inputs"]["files"].items()
            )
        ]
        seed_bytes, seed_observations = _seed_fixture(manifest)
        object_pairs = _matching_object_pairs(manifest)
        executable_pairs = _matching_executable_pairs(manifest)
        bootstrap_object_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
        )
        bootstrap_tool_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
        )

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=list(reversed(observations)),
                input_observations=list(reversed(input_observations)),
                bootstrap_observations=list(
                    reversed(bootstrap_observations)
                ),
                seed_manifest_bytes=seed_bytes,
                seed_observations=list(reversed(seed_observations)),
                object_pairs=list(reversed(object_pairs)),
                executable_pairs=list(reversed(executable_pairs)),
                bootstrap_object_pairs=list(
                    reversed(bootstrap_object_pairs)
                ),
                bootstrap_tool_pairs=list(reversed(bootstrap_tool_pairs)),
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )

    def test_publisher_framing_drives_the_standalone_author(self):
        manifest, artifact_observations = _fixture()
        seed_bytes, seed_observations = _seed_fixture(manifest)
        request = cupidc_toolchain_contracts._manifest_author_request(
            artifact_observations,
            [
                (path, 1, record["size"], record["sha256"])
                for path, record in sorted(manifest["inputs"].items())
            ],
            [
                (path, 1, record["size"], record["sha256"])
                for path, record in sorted(
                    manifest["bootstrap"]["source_inputs"]["files"].items()
                )
            ],
            manifest["bootstrap"]["source_inputs"]["sha256"],
            manifest["bootstrap"]["seed_manifest"]["path"],
            seed_bytes,
            seed_observations,
            _matching_object_pairs(manifest),
            _matching_executable_pairs(manifest),
            _matching_bootstrap_pairs(
                BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
            ),
            _matching_bootstrap_pairs(
                BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
            ),
        )

        result = self.run_author_request(request)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )

    def test_author_framing_rejects_truncation_and_trailing_bytes(self):
        payload = _author_request()
        self.assert_author_failure(payload[:-1])
        self.assert_author_failure(payload + b"x")

    def test_author_protocol_has_no_caller_all_equal_field(self):
        payload = _author_request()

        result = self.run_author_request(payload)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_author_failure(payload + struct.pack("<I", 1))
        self.assert_author_failure(b"CUPMAN2\0" + payload[8:])

    def test_author_rejects_substituted_and_duplicate_fact_paths(self):
        manifest, observations = _fixture()
        input_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        substituted = list(input_observations)
        _path, kind, size, digest = substituted[0]
        substituted[0] = (
            "toolchain/tests/not-the-author.cc",
            kind,
            size,
            digest,
        )
        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=substituted,
            )
        )

        duplicated = list(input_observations)
        duplicated[-1] = duplicated[0]
        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=duplicated,
            )
        )

    def test_author_artifact_reader_rejects_wrong_missing_and_extra_facts(self):
        manifest, observations = _fixture()
        wrong = list(observations)
        _name, kind, size, digest = wrong[0]
        wrong[0] = ("not-an-artifact.elf", kind, size, digest)
        cases = {
            "wrong": wrong,
            "missing": observations[:-1],
            "extra": [*observations, observations[0]],
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=changed,
                    )
                )

    def test_author_bootstrap_reader_rejects_each_corrupt_fact_class(self):
        manifest, observations = _fixture()
        bootstrap = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(
                manifest["bootstrap"]["source_inputs"]["files"].items()
            )
        ]
        wrong_path = list(bootstrap)
        _path, kind, size, digest = wrong_path[0]
        wrong_path[0] = ("toolchain/not-a-bootstrap-source.cc", kind, size, digest)
        wrong_size = list(bootstrap)
        path, kind, size, digest = wrong_size[0]
        wrong_size[0] = (path, kind, size + 1, digest)
        wrong_digest = list(bootstrap)
        path, kind, size, _digest_value = wrong_digest[0]
        wrong_digest[0] = (path, kind, size, "0" * 64)
        cases = {
            "count": bootstrap[:-1],
            "path": wrong_path,
            "size": wrong_size,
            "digest": wrong_digest,
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        bootstrap_observations=changed,
                    )
                )

    def test_author_seed_reader_rejects_each_corrupt_fact_class(self):
        manifest, observations = _fixture()
        seed_bytes, seed_observations = _seed_fixture(manifest)
        wrong_size = list(seed_observations)
        name, kind, size, digest = wrong_size[0]
        wrong_size[0] = (name, kind, size + 1, digest)
        wrong_digest = list(seed_observations)
        name, kind, size, _digest_value = wrong_digest[0]
        wrong_digest[0] = (name, kind, size, "0" * 64)
        cases = {
            "path": {
                "seed_manifest_path": "bootstrap/seeds/not-linux/manifest.json",
                "seed_manifest_bytes": seed_bytes,
                "seed_observations": seed_observations,
            },
            "bytes": {
                "seed_manifest_bytes": seed_bytes + b"x",
                "seed_observations": seed_observations,
            },
            "artifact size": {
                "seed_manifest_bytes": seed_bytes,
                "seed_observations": wrong_size,
            },
            "artifact digest": {
                "seed_manifest_bytes": seed_bytes,
                "seed_observations": wrong_digest,
            },
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        **changed,
                    )
                )

    def test_author_object_reader_rejects_missing_duplicate_and_empty_pairs(self):
        manifest, observations = _fixture()
        objects = _matching_object_pairs(manifest)
        duplicated = list(objects)
        duplicated[-1] = duplicated[0]
        empty = list(objects)
        name, first_kind, _first_bytes, second_kind, _second_bytes = empty[0]
        empty[0] = (name, first_kind, b"", second_kind, b"")
        cases = {
            "missing": objects[:-1],
            "duplicate": duplicated,
            "empty": empty,
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        object_pairs=changed,
                    )
                )

    def test_author_output_binds_input_and_object_sizes(self):
        manifest, observations = _fixture()
        inputs = [
            (path, 1, record["size"] + 101, record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        objects = _matching_object_pairs(manifest)

        baseline = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=inputs,
                object_pairs=objects,
            )
        )
        self.assertEqual(baseline.returncode, 0, baseline.stderr)

        changed_inputs = list(inputs)
        path, kind, size, digest = changed_inputs[0]
        changed_inputs[0] = (path, kind, size + 1, digest)
        changed_input = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=changed_inputs,
                object_pairs=objects,
            )
        )
        self.assertEqual(changed_input.returncode, 0, changed_input.stderr)
        self.assertNotEqual(changed_input.stdout, baseline.stdout)

        changed_objects = list(objects)
        name, first_kind, first_bytes, second_kind, second_bytes = (
            changed_objects[0]
        )
        changed_bytes = first_bytes + b"x"
        changed_objects[0] = (
            name,
            first_kind,
            changed_bytes,
            second_kind,
            changed_bytes,
        )
        changed_object = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=inputs,
                object_pairs=changed_objects,
            )
        )
        self.assertEqual(changed_object.returncode, 0, changed_object.stderr)
        self.assertNotEqual(changed_object.stdout, baseline.stdout)

    def test_author_derives_fixed_point_summary_from_pair_inventories(self):
        manifest, observations = _fixture()
        manifest["tool_fixed_point"] = {
            "all_equal": False,
            "c_objects": 18,
            "compared_generations": ["stage-two"],
            "startup_objects": 2,
            "tool_images": 4,
        }

        result = self.run_author_request(
            _author_request(manifest=manifest, observations=observations)
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout)["tool_fixed_point"],
            {
                "all_equal": True,
                "c_objects": 29,
                "compared_generations": ["stage-three", "stage-four"],
                "startup_objects": 1,
                "tool_images": 6,
            },
        )

    def test_manifest_object_order_does_not_change_the_result(self):
        manifest, observations = _fixture()
        reordered = {"comparisons": manifest["comparisons"]}
        reordered.update(
            (key, value)
            for key, value in manifest.items()
            if key != "comparisons"
        )
        manifest_bytes = json.dumps(
            reordered,
            ensure_ascii=True,
            separators=(",", ":"),
        ).encode("ascii")
        result = self.run_request(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                observations=observations,
            )
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_request_framing_rejects_truncation_and_trailing_bytes(self):
        payload = _request()
        self.assert_contract_failure(payload[:-1])
        self.assert_contract_failure(payload + b"x")
        self.assert_contract_failure(b"CUPMAN1\0" + payload[8:])

    def test_live_input_observations_are_independent_evidence(self):
        manifest, observations = _fixture()
        live = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        for field, replacement in (
            (1, 2),
            (3, "0" * 64),
            (0, "toolchain/not-the-captured-input.h"),
        ):
            with self.subTest(field=field):
                changed = list(live)
                record = list(changed[0])
                record[field] = replacement
                changed[0] = tuple(record)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        observations=observations,
                        input_observations=changed,
                    )
                )
        changed_size = list(live)
        path, kind, size, digest = changed_size[1]
        changed_size[1] = (path, kind, size + 1, digest)
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                input_observations=changed_size,
            )
        )
        duplicate = list(live)
        duplicate[-1] = duplicate[0]
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                input_observations=duplicate,
            )
        )

    def test_bootstrap_observations_are_independent_evidence(self):
        manifest, observations = _fixture()
        files = manifest["bootstrap"]["source_inputs"]["files"]
        live = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(files.items())
        ]
        for field, replacement in (
            (1, 2),
            (2, live[0][2] + 1),
            (3, "0" * 64),
            (0, "toolchain/not-the-captured-source.cc"),
        ):
            with self.subTest(field=field):
                changed = list(live)
                record = list(changed[0])
                record[field] = replacement
                changed[0] = tuple(record)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        observations=observations,
                        bootstrap_observations=changed,
                    )
                )

    def test_seed_observations_are_independent_evidence(self):
        manifest, observations = _fixture()
        seed_bytes, live = _seed_fixture(manifest)
        for field, replacement in (
            (1, 2),
            (2, live[0][2] + 1),
            (3, "0" * 64),
            (0, "not-the-captured-seed.elf"),
        ):
            with self.subTest(field=field):
                changed = list(live)
                record = list(changed[0])
                record[field] = replacement
                changed[0] = tuple(record)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        observations=observations,
                        seed_manifest_bytes=seed_bytes,
                        seed_observations=changed,
                    )
                )
        duplicate = list(live)
        duplicate[-1] = duplicate[0]
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                seed_manifest_bytes=seed_bytes,
                seed_observations=duplicate,
            )
        )
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                seed_manifest_path="bootstrap/seeds/other/manifest.json",
                seed_manifest_bytes=seed_bytes,
                seed_observations=live,
            )
        )
        changed_seed = bytearray(seed_bytes)
        changed_seed[-2] ^= 1
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                seed_manifest_bytes=bytes(changed_seed),
                seed_observations=live,
            )
        )

    def test_duplicate_manifest_keys_are_rejected(self):
        manifest, observations = _fixture()
        manifest_bytes = _json_bytes(manifest).replace(
            b'"status":"pass"',
            b'"status":"pass","status":"pass"',
            1,
        )
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                observations=observations,
            )
        )

    def test_malformed_object_comparison_record_is_rejected(self):
        manifest, observations = _fixture()
        manifest_bytes = _json_bytes(manifest).replace(
            b'"object_comparisons":{"as_elf":{',
            b'"object_comparisons":{"as_elf"{',
            1,
        )
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                observations=observations,
            )
        )

    def test_object_comparison_hashes_remain_producer_evidence(self):
        manifest, observations = _fixture()
        manifest["object_comparisons"] = {
            name: {"sha256": "0" * 64, "size": index + 1}
            for index, name in enumerate(OBJECT_COMPARISON_NAMES)
        }
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_long_path_inventory_is_authored_and_verified_with_the_resolver_shim(self):
        manifest, observations = _fixture()
        inputs = manifest["bootstrap"]["source_inputs"]
        shim = "toolchain/hosted/i386-windows/utf8_long_path_start.asm"
        inputs["files"][shim] = _digest_size("long path resolver", 31)
        inputs["count"] = 81
        inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
        request = _author_request(manifest=manifest, observations=observations)
        result = self.run_author_request(request)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), manifest)
        self.assertEqual(result.stdout, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        verified = self.run_request(_request(manifest=manifest, observations=observations))
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertEqual(json.loads(verified.stdout)["bootstrap_source_input_count"], 81)
        bad = copy.deepcopy(manifest)
        bad_inputs = bad["bootstrap"]["source_inputs"]
        bad_inputs["files"]["toolchain/unexpected.asm"] = bad_inputs["files"].pop(shim)
        bad_inputs["sha256"] = _digest(_json_bytes(bad_inputs["files"]))
        self.assert_contract_failure(_request(manifest=bad, observations=observations))
        rejected = self.run_author_request(_author_request(manifest=bad, observations=observations))
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual(rejected.stdout, "")
        recovered = self.run_author_request(request)
        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        self.assertEqual(recovered.stdout, result.stdout)

    def _assert_profile_authored_and_verified(self, *, long_paths):
        manifest, observations = _profile_fixture(
            user_link_aliases=True, long_paths=long_paths
        )
        authored = self.run_author_request(
            _author_request(manifest=manifest, observations=observations)
        )
        self.assertEqual(authored.returncode, 0, authored.stderr)
        self.assertEqual(
            authored.stdout, json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        )
        verified = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertEqual(
            json.loads(verified.stdout)["bootstrap_source_input_count"],
            82 if long_paths else 81,
        )

    def test_alias_inventory_is_authored_and_verified(self):
        self._assert_profile_authored_and_verified(long_paths=False)

    def test_alias_long_path_inventory_is_authored_and_verified(self):
        self._assert_profile_authored_and_verified(long_paths=True)

    def test_alias_profiles_reject_same_count_source_substitution(self):
        for long_paths in (False, True):
            for removed in (
                "toolchain/hosted/i386-windows/final_path_start.asm",
                BOOTSTRAP_PATHS[0],
            ):
                with self.subTest(long_paths=long_paths, removed=removed):
                    manifest, observations = _profile_fixture(
                        user_link_aliases=True, long_paths=long_paths
                    )
                    inputs = manifest["bootstrap"]["source_inputs"]
                    inputs["files"]["toolchain/unexpected.asm"] = (
                        inputs["files"].pop(removed)
                    )
                    inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
                    self.assert_contract_failure(
                        _request(manifest=manifest, observations=observations)
                    )
                    self.assert_author_failure(
                        _author_request(manifest=manifest, observations=observations)
                    )

    def _release_inventory_fixture(self):
        manifest, observations = _fixture()
        for name in ("tools/__init__.py", "tools/bootstrap_user_abi.py",
                     "tools/bootstrap_stage_release.py", "tools/seed_release_identity.py"):
            manifest["inputs"][name] = _digest_size("input:" + name, 37)
        manifest["input_count"] = len(manifest["inputs"])
        return manifest, observations


    def test_captured_author_and_verifier_bind_complete_release_inventory(self):
        manifest, observations = self._release_inventory_fixture()
        authored = self.run_author_request(_context_request("author", manifest=manifest,
                                                             artifact_observations=observations))
        self.assertEqual(authored.returncode, 0, authored.stderr)
        self.assertEqual(authored.stdout, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        verified = self.run_request(_context_request("check", manifest=manifest,
                                                     artifact_observations=observations))
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertEqual(json.loads(verified.stdout)["input_count"], 96)
        seed_bytes, seed_observations = _captured_seed_fixture()
        for field in ("size", "sha256"):
            changed = copy.deepcopy(manifest)
            row = changed["inputs"]["tools/bootstrap_stage_release.py"]
            row[field] = row[field] + 1 if field == "size" else "0" * 64
            # Independent live observations must describe the original input.
            classic = _request(manifest=changed, observations=observations,
                input_observations=[(name, 1, record["size"], record["sha256"])
                                    for name, record in sorted(manifest["inputs"].items())],
                seed_manifest_bytes=seed_bytes, seed_observations=seed_observations)
            payload = b"CUPMAN6\0" + _seed_context_bytes(seed_bytes) + classic[8:]
            with self.subTest(field=field):
                self.assert_contract_failure(payload)


    def test_release_inventory_requires_captured_context_and_exact_membership(self):
        manifest, observations = self._release_inventory_fixture()
        self.assert_author_failure(_author_request(manifest=manifest, observations=observations))
        self.assert_contract_failure(_request(manifest=manifest, observations=observations))
        for mutation in ("missing", "extra", "substitute"):
            changed = copy.deepcopy(manifest)
            if mutation != "extra":
                changed["inputs"].pop("tools/bootstrap_stage_release.py")
            if mutation != "missing":
                changed["inputs"]["tools/unreviewed_behavior.py"] = _digest_size("unknown", 7)
            changed["input_count"] = len(changed["inputs"])
            with self.subTest(mutation=mutation):
                self.assert_author_failure(_context_request("author", manifest=changed,
                                                            artifact_observations=observations))
                self.assert_contract_failure(_context_request("check", manifest=changed,
                                                               artifact_observations=observations))
        good = self.run_author_request(_context_request("author", manifest=manifest,
                                                         artifact_observations=observations))
        self.assertEqual(good.returncode, 0, good.stderr)


    def test_publication_inventory_matches_real_contract_inputs(self):
        actual = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in cupidc_toolchain_contracts._contract_input_paths(REPO_ROOT)
        }
        self.assertEqual({*INPUT_PATHS, *["toolchain/" + name + ".h" for name in COMPLETE_ISO_OBJECTS],
                          *["toolchain/" + name + ".h" for name in DISK_FOUNDATION_OBJECTS],
                          *OWNER_HEADERS, *OWNER_SHARED_INPUTS,
                          "toolchain/cupidbuild_jpeg.h",
                          "toolchain/tests/cupidld_pe32_cases.inc",
                          "toolchain/tests/x86_integer_contract.inc",
                          "toolchain/tests/x86_operand_contract.inc",
                          "toolchain/tests/cupidc_source_spelling_fixture.h",
                          "tools/__init__.py", "tools/bootstrap_user_abi.py",
                          "tools/bootstrap_stage_release.py", "tools/seed_release_identity.py"}, actual)
        self.assertEqual(len(actual), 117)

    def _complete_release_inventory_fixture(self, **profile):
        manifest, observations, seed_bytes, seed_observations = _complete_context_fixture(**profile)
        for name in ("tools/__init__.py", "tools/bootstrap_user_abi.py",
                     "tools/bootstrap_stage_release.py", "tools/seed_release_identity.py"):
            payload = (REPO_ROOT / name).read_bytes()
            manifest["inputs"][name] = {"size": len(payload), "sha256": _digest(payload)}
        manifest["input_count"] = len(manifest["inputs"])
        return manifest, observations, seed_bytes, seed_observations

    def test_complete_iso_authority_inventory_binds_every_profile(self):
        for long_paths, aliases in ((False, False), (True, False), (False, True), (True, True)):
            profile = {"long_paths": long_paths, "user_link_aliases": aliases}
            manifest, _observations, _seed, _facts = self._complete_release_inventory_fixture(**profile)
            with self.subTest(**profile):
                self.assertEqual(manifest["input_count"], 101)
                authored = self.run_author_request(_complete_context_request("author", manifest=manifest, **profile))
                self.assertEqual(authored.returncode, 0, authored.stderr)
                self.assertEqual(authored.stdout, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
                reordered = dict(reversed(list(manifest.items())))
                verified = self.run_request(_complete_context_request("check", manifest=reordered, **profile))
                self.assertEqual(verified.returncode, 0, verified.stderr)
                self.assertEqual(json.loads(verified.stdout)["input_count"], 101)

    def test_complete_iso_authority_inventory_rejects_each_missing_replaced_or_changed_dependency(self):
        original, observations, seed_bytes, seed_observations = self._complete_release_inventory_fixture()
        paths = ("tools/__init__.py", "tools/bootstrap_user_abi.py",
                 "tools/bootstrap_stage_release.py", "tools/seed_release_identity.py")
        for name in paths:
            for replacement in (None, "tools/unreviewed_authority.py"):
                changed = copy.deepcopy(original)
                row = changed["inputs"].pop(name)
                if replacement is not None:
                    changed["inputs"][replacement] = row
                changed["input_count"] = len(changed["inputs"])
                for mode in ("author", "check"):
                    with self.subTest(path=name, replacement=replacement, mode=mode):
                        run = self.run_author_request if mode == "author" else self.run_request
                        result = run(_complete_context_request(mode, manifest=changed))
                        self.assertEqual(result.returncode, 1, result.stderr)
                        self.assertEqual(result.stdout, "")
            for field in ("size", "sha256"):
                changed = copy.deepcopy(original)
                row = changed["inputs"][name]
                row[field] = row[field] + 1 if field == "size" else "0" * 64
                classic = _request(manifest=changed, observations=observations,
                    input_observations=[(path, 1, record["size"], record["sha256"])
                                        for path, record in sorted(original["inputs"].items())],
                    seed_manifest_bytes=seed_bytes, seed_observations=seed_observations)
                with self.subTest(path=name, field=field):
                    self.assert_contract_failure(b"CUPMAN6\0" + _seed_context_bytes(seed_bytes) + classic[8:])
        self.assertEqual(self.run_author_request(_complete_context_request("author", manifest=original)).returncode, 0)

    def test_user_link_bridge_cannot_be_substituted(self):
        manifest, observations = _fixture()
        bridge = "toolchain/hosted/i386-windows/final_path_start.asm"
        manifest["inputs"]["toolchain/hosted/i386-windows/substituted.asm"] = (
            manifest["inputs"].pop(bridge)
        )
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("manifest input path inventory differs", result.stderr)

    def test_current_publication_inventory_counts_are_exact(self):
        self.assertEqual(len(ARTIFACT_NAMES), 22)
        self.assertEqual(len(INPUT_PATHS), 92)
        self.assertEqual(len(BOOTSTRAP_PATHS), 80)
        self.assertEqual(len(OBJECT_COMPARISON_NAMES), 17)
        self.assertEqual(len(BOOTSTRAP_OBJECT_NAMES), 30)
        self.assertEqual(len(BOOTSTRAP_TOOL_NAMES), 6)
        for input_count in (75, 77, 80, 82, 86, 87, 88, 89, 91):
            with self.subTest(input_count=input_count):
                self.assertNotEqual(input_count, len(INPUT_PATHS))
                manifest, observations = _fixture()
                manifest["inputs"] = {
                    path: _digest_size(f"input:{path}", index)
                    for index, path in enumerate(INPUT_PATHS[:input_count], 1)
                }
                if input_count > len(INPUT_PATHS):
                    manifest["inputs"]["toolchain/unexpected.h"] = (
                        _digest_size("unexpected", input_count)
                    )
                manifest["input_count"] = input_count
                self.assert_contract_failure(
                    _request(manifest=manifest, observations=observations)
                )

        for source_count in (58, 60, 73, 75, 77):
            with self.subTest(source_count=source_count):
                self.assertNotEqual(source_count, len(BOOTSTRAP_PATHS))
                manifest, observations = _fixture()
                bootstrap_files = {
                    path: {
                        "sha256": _digest(f"source:{path}"),
                        "size": index,
                    }
                    for index, path in enumerate(
                        BOOTSTRAP_PATHS[:source_count], 1
                    )
                }
                if source_count > len(BOOTSTRAP_PATHS):
                    bootstrap_files["toolchain/unexpected.cc"] = {
                        "sha256": _digest("unexpected"),
                        "size": source_count - 1,
                    }
                manifest["bootstrap"]["source_inputs"] = {
                    "count": source_count,
                    "files": bootstrap_files,
                    "sha256": _digest(_json_bytes(bootstrap_files)),
                }
                self.assert_contract_failure(
                    _request(manifest=manifest, observations=observations)
                )

    def test_manifest_schema_is_exact(self):
        manifest, observations = _fixture()
        manifest["schema"] = "cupid.toolchain-contracts.v1"
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_manifest_metadata_requires_the_exact_shape(self):
        manifest, observations = _fixture()
        manifest["status"] = "unchecked"
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["target"]["entry"] = 0x08049000
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["unexpected"] = None
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        del manifest["target"]
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_tool_fixed_point_record_is_exact(self):
        manifest, observations = _fixture()
        manifest["tool_fixed_point"]["all_equal"] = False
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["tool_fixed_point"]["c_objects"] = 18
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["tool_fixed_point"]["compared_generations"].reverse()
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_artifact_records_match_the_exact_observed_cohort(self):
        manifest, observations = _fixture()
        manifest["artifacts"].pop()
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        name, kind, size, digest = observations[0]
        observations[0] = (name, kind, size + 1, digest)
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        name, kind, size, _digest_value = observations[0]
        observations[0] = (name, kind, size, "0" * 64)
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_comparison_maps_bind_the_exact_names_and_digests(self):
        manifest, observations = _fixture()
        manifest["comparisons"]["runtime"] = "0" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        del manifest["object_comparisons"]["as_elf"]
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["object_comparisons"]["as_elf"]["size"] = 0
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["object_comparisons"]["unknown"] = {
            "sha256": "1" * 64,
            "size": 1,
        }
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_input_inventory_count_paths_and_digests_are_checked(self):
        manifest, observations = _fixture()
        manifest["input_count"] += 1
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        first_path = INPUT_PATHS[0]
        manifest["inputs"][first_path]["sha256"] = "A" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["inputs"]["../outside"] = manifest["inputs"].pop(
            first_path
        )
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["inputs"]["toolchain/substituted-input.h"] = (
            manifest["inputs"].pop(first_path)
        )
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_bootstrap_record_binds_its_canonical_source_snapshot(self):
        manifest, observations = _fixture()
        empty_snapshot = _json_bytes({})
        manifest["bootstrap"]["source_inputs"] = {
            "count": 0,
            "files": {},
            "sha256": _digest(empty_snapshot),
        }
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["source_inputs"]["count"] += 1
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["source_inputs"]["sha256"] = "0" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["seed_manifest"]["path"] = "../seed.json"
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        files = manifest["bootstrap"]["source_inputs"]["files"]
        files["toolchain/substituted-source.cc"] = files.pop(
            BOOTSTRAP_PATHS[0]
        )
        manifest["bootstrap"]["source_inputs"]["sha256"] = _digest(
            _json_bytes(files)
        )
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["build_plan_sha256"] = "0" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )


if __name__ == "__main__":
    unittest.main()

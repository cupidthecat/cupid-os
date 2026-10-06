import concurrent.futures
import copy
import ctypes
import json
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from tests.test_seed_release import DRAFT, ROOT, encode, _host_compiler, release
from tests.test_seed_release_match import manifest
from tools import bootstrap_toolchain as seed



def pre_artifact_plan():
    plan = copy.deepcopy(manifest(1)["build_plan"])
    removed = {"artifact_size_policy", "cupidbuild_artifacts"}
    plan["sources"] = [row for row in plan["sources"] if row["name"] not in removed]
    plan["links"]["cupidbuild"] = [name for name in plan["links"]["cupidbuild"]
                                  if name not in removed]
    assert seed._build_plan_sha256(plan) == "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe"
    return plan


def historical_parents(value):
    provenance = value["provenance"]
    for field in provenance:
        if field.startswith("parent_"):
            provenance[field] = (
                "83d00ce70e5607dc5c011bb97c6478121f24a21c" if field.endswith("source_revision") else
                "f5124cbddbeb55a61ce2f8ae93923daae512d6fec6732a532b1e8f0d15bed590" if "execution" in field else
                "a11c8af08eb1170d040dc6b361c30df321c088fcb4ae5becd6c2864995380622")
    return value


def historical_manifest(fmt):
    """Construct the earlier structural contract without changing installed seeds."""
    value = historical_parents(manifest(fmt))
    value["provenance"]["source_input_count"] = 59
    if fmt == 1:
        removed = {"seed_manifest", "seed_release", "contract_parse_internal"}
        plan = pre_artifact_plan()
        value["build_plan"] = plan
        plan["sources"] = [row for row in plan["sources"] if row["name"] not in removed]
        plan["links"]["cupidbuild"] = [name for name in plan["links"]["cupidbuild"]
                                      if name not in removed]
        value["build_plan_sha256"] = seed._build_plan_sha256(plan)
        assert value["build_plan_sha256"] == "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
    else:
        value["provenance"]["linux_candidate_build_plan_sha256"] = "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
        value["provenance"]["native_build_plan_sha256"] = "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669"
    return value


def candidate_manifest(fmt):
    value = historical_parents(manifest(fmt))
    value["provenance"]["source_input_count"] = 66
    plan = pre_artifact_plan()
    linux_digest = seed._build_plan_sha256(plan)
    windows_digest = seed._build_plan_sha256(seed._windows_build_plan(plan))
    assert linux_digest == "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe"
    assert windows_digest == "70158fd9780990ec0cd0ed1c4da1af9f22f8acbcb483324693fd46c2362177b9"
    if fmt == 1:
        value["build_plan"] = plan
        value["build_plan_sha256"] = linux_digest
    else:
        value["provenance"]["linux_candidate_build_plan_sha256"] = linux_digest
        value["provenance"]["native_build_plan_sha256"] = windows_digest
    return value


def artifact_manifest(fmt):
    value = manifest(fmt)
    provenance = value["provenance"]
    provenance["source_input_count"] = 76
    plan = copy.deepcopy(manifest(1)["build_plan"])
    if fmt == 1:
        value["build_plan"] = plan
        value["build_plan_sha256"] = seed._build_plan_sha256(plan)
        provenance["parent_seed_source_revision"] = "72170b06d54ae59f222e5773a96ba3f33503b495"
        provenance["parent_seed_manifest_sha256"] = "9db461d2bc423e6a235496dc43135fcb023b0bdd7da4881c3c306aba9b872905"
    else:
        provenance["linux_candidate_build_plan_sha256"] = seed._build_plan_sha256(plan)
        provenance["native_build_plan_sha256"] = seed._build_plan_sha256(
            seed._windows_build_plan(plan, utf8=True))
        provenance["parent_execution_seed_source_revision"] = "72170b06d54ae59f222e5773a96ba3f33503b495"
        provenance["parent_plan_seed_source_revision"] = "72170b06d54ae59f222e5773a96ba3f33503b495"
        provenance["parent_execution_seed_manifest_sha256"] = "4ac54f8369c85f975411178852d9d05b107c312b6a470fde2a5eea8dc513ce27"
        provenance["parent_plan_seed_manifest_sha256"] = "9db461d2bc423e6a235496dc43135fcb023b0bdd7da4881c3c306aba9b872905"
    return value


def selection_manifest(fmt):
    value = artifact_manifest(fmt)
    provenance = value["provenance"]
    for key in provenance:
        if key.startswith("parent_"):
            provenance[key] = (
                "ec896462586597893dd197697ee3b68ce2c8e69b" if key.endswith("source_revision") else
                "25290a99f9de273890cd98130e8ad7df5e9ffcd89010be8e285a06a380e1eaf2" if "execution" in key else
                "dabdc048ce54c7434fd9edd602f0531ead60f425db39bc2550332d7c69d30608")
    return value


def long_path_manifest(fmt):
    value = selection_manifest(fmt)
    provenance = value["provenance"]
    provenance["source_input_count"] = 77
    if fmt == 1:
        provenance["parent_seed_source_revision"] = "5ba6ea24fdef3b23c505551ab537688e681c9593"
        provenance["parent_seed_manifest_sha256"] = "1a8a91581562751cca6c51c5cd3de1259a73e92a0c161d1425155724d80ba7a8"
    else:
        plan = value["provenance"]["linux_candidate_build_plan_sha256"]
        linux = copy.deepcopy(manifest(1)["build_plan"])
        assert plan == seed._build_plan_sha256(linux)
        provenance["native_build_plan_sha256"] = seed._build_plan_sha256(
            seed._windows_build_plan(linux, utf8=True, long_paths=True))
        provenance["parent_execution_seed_source_revision"] = "5ba6ea24fdef3b23c505551ab537688e681c9593"
        provenance["parent_plan_seed_source_revision"] = "5ba6ea24fdef3b23c505551ab537688e681c9593"
        provenance["parent_execution_seed_manifest_sha256"] = "c8c2780000575ff255b6f72287f85d4b47f85b420b3fa0f29eeacfe74207175b"
        provenance["parent_plan_seed_manifest_sha256"] = "1a8a91581562751cca6c51c5cd3de1259a73e92a0c161d1425155724d80ba7a8"
    return value


def default_parent_manifest(fmt):
    value = long_path_manifest(fmt)
    value["provenance"]["source_input_count"] = 76
    if fmt == 2:
        value["provenance"]["native_build_plan_sha256"] = (
            "6aba99be40f915aa2adcb92ecb8341bef6f4a8a290e275fe47823ad380bd3748"
        )
    return value


def next_parent_manifest(fmt, *, long_paths):
    value = (long_path_manifest if long_paths else default_parent_manifest)(fmt)
    for field in value["provenance"]:
        if field.startswith("parent_"):
            value["provenance"][field] = (
                "8403b0a82b5693409d2242fdbff32688c8f2cac5" if field.endswith("source_revision") else
                "5d129b2575450dac756d75a4dc859501fdcd9bacf53190ed360ec66f68e21297" if "execution" in field else
                "84b8bef11969bac58d69e97baacd86d8f1b4aa030ecd25359bb1dcdb8f679cbc")
    return value


def user_compile_parent_manifest(fmt, *, long_paths):
    value = next_parent_manifest(fmt, long_paths=long_paths)
    for field in value["provenance"]:
        if field.startswith("parent_"):
            value["provenance"][field] = (
                "78e71bd6137042720c378d2c596aa40b153dad11" if field.endswith("source_revision") else
                "1d40ec6e03bdd736e5993f8a204588f0e376541f4019b83bd00650469b9531bd" if "execution" in field else
                "b6f247af2034d7432333eed74230452fede2198ba744c30a5c410ce19c4b79b4")
    return value


def user_link_alias_manifest(fmt, *, long_paths):
    value = user_compile_parent_manifest(fmt, long_paths=long_paths)
    value["provenance"]["source_input_count"] = 78 if long_paths else 77
    if fmt == 2:
        value["provenance"]["native_build_plan_sha256"] = (
            "2dc92702e1e6e823b0c43fd48427d66bd021563925fe2b8b418451206768f8ff" if long_paths else
            "79241fcdd8784952cf9e1e74907ac817dc83e24429c5625d3424a889c2753d70")
    return value


def user_abi_plan():
    """Keep the earlier ABI-only profile independent of current plan upgrades."""
    plan = seed._candidate_build_plan(manifest(1)["build_plan"])
    removed = {"iso_fixture_bundle", "cupidbuild_iso", "cupidbuild_iso_capture",
               "cupidbuild_iso_image", "cupidbuild_iso_publication"}
    plan["sources"] = [row for row in plan["sources"] if row["name"] not in removed]
    plan["links"]["cupidobj"] = [name for name in plan["links"]["cupidobj"] if name != "iso_fixture_bundle"]
    plan["links"]["cupidbuild"] = [name for name in plan["links"]["cupidbuild"] if name not in removed]
    assert seed._build_plan_sha256(plan) == "48d6cc38b7a7362a83a911d2d3aaae8e79537c3f1744f3f5e7aac997728ed7f4"
    return plan


def user_abi_manifest(fmt, *, long_paths=False, aliases=False):
    value = user_compile_parent_manifest(fmt, long_paths=long_paths)
    plan = user_abi_plan()
    value["provenance"]["source_input_count"] = 80 + int(long_paths) + int(aliases)
    if fmt == 1:
        value["build_plan"] = plan
        value["build_plan_sha256"] = seed._build_plan_sha256(plan)
    else:
        value["provenance"]["linux_candidate_build_plan_sha256"] = seed._build_plan_sha256(plan)
        value["provenance"]["native_build_plan_sha256"] = seed._build_plan_sha256(
            seed._windows_build_plan(plan, utf8=True, long_paths=long_paths,
                                     user_link_aliases=aliases))
    return value


class Artifact(ctypes.Structure):
    _fields_ = [("file", ctypes.c_char * 32), ("sha256", ctypes.c_char * 65), ("size", ctypes.c_uint32)]


class Result(ctypes.Structure):
    _fields_ = [("artifact_count", ctypes.c_uint32), ("current_windows_plan", ctypes.c_uint32),
               ("artifacts", Artifact * 6)]


def leaves(value, path=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from leaves(child, path + (key,))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from leaves(child, path + (index,))
    else:
        yield path, value


def changed(value):
    if isinstance(value, bool): return not value
    if isinstance(value, int): return value + 1
    return "wrong"


class ManifestTests(unittest.TestCase):
    records = []
    pair_records = []
    release_records = []

    def check_release(self, record, value, fmt, accepted=True, capacity=128, expected=None):
        payloads = [encode(item) if isinstance(item, dict) else item for item in (record, value)]
        buffers = [ctypes.create_string_buffer(item) for item in payloads]
        before = [bytes(item) for item in buffers]
        result = Result()
        ctypes.memset(ctypes.byref(result), 0xa5, ctypes.sizeof(result))
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        status = self.release_api(buffers[0], len(payloads[0]), buffers[1], len(payloads[1]),
                                  fmt, ctypes.byref(result), error, capacity)
        self.assertEqual(status, int(accepted), error.value)
        self.assertEqual([bytes(item) for item in buffers], before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        if accepted:
            document = json.loads(payloads[1])
            self.assertEqual(result.artifact_count, 6)
            self.assertEqual(result.current_windows_plan,
                             expected[1] if expected is not None else
                             5 if fmt == 2 and document["provenance"]["source_input_count"] == 78
                             else 4 if fmt == 2 else 0)
            for index, role in enumerate(("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")):
                artifact = next(item for item in document["artifacts"] if item["name"] == role)
                actual = result.artifacts[index]
                self.assertEqual((actual.file.decode(), actual.sha256.decode(), actual.size),
                                 (artifact["file"], artifact["sha256"], artifact["size"]))
            if capacity:
                self.assertEqual(error.value, b"")
        else:
            self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
            if capacity > 1:
                self.assertTrue(error.value)
        if os.environ.get("CUPID_MANIFEST_RELEASE_EXPORT"):
            summary = "0\n"
            if accepted:
                fields = ["1", str(result.artifact_count), str(result.current_windows_plan)]
                for artifact in result.artifacts:
                    fields.extend((artifact.file.decode(), str(artifact.size), artifact.sha256.decode()))
                summary = " ".join(fields) + "\n"
            self.release_records.append((*payloads, fmt, summary))

    def test_installed_long_default_retarget_records_actual_parents_and_is_strictly_rejected(self):
        windows = seed.verify_seed_inputs(ROOT / "bootstrap/seeds/i386-windows/manifest.json")
        linux = seed.verify_seed_inputs(ROOT / "bootstrap/seeds/i386-linux/manifest.json")
        self.assertEqual(windows.manifest["provenance"]["source_input_count"], 78)
        plan = copy.deepcopy(linux.manifest["build_plan"])
        snapshot = seed.capture_source_snapshot(ROOT, plan, windows_utf8=True, windows_user_link_aliases=True)
        snapshot = {path: row for path, row in snapshot.items()
                    if path not in ("toolchain/user_syscall_abi.h", "toolchain/cupidbuild_user_abi.h",
                                    "toolchain/cupidbuild_iso_publication.h")}
        self.assertEqual(len(snapshot), 81)
        digest = seed._build_plan_sha256(seed._windows_build_plan(plan, utf8=True, user_link_aliases=True))
        changed = seed._retarget_native_windows_behavior_seed(windows, digest, plan, snapshot,
            utf8=True, user_link_aliases=True, parent_plan_seed=linux)
        self.assertIsNot(changed, windows)
        provenance = changed.manifest["provenance"]
        self.assertEqual(provenance["source_input_count"], 81)
        self.assertEqual(provenance["native_build_plan_sha256"], digest)
        self.assertEqual(provenance["parent_execution_seed_manifest_sha256"], windows.manifest_sha256)
        self.assertEqual(provenance["parent_plan_seed_manifest_sha256"], linux.manifest_sha256)
        self.assertEqual(provenance["parent_execution_seed_source_revision"], windows.manifest["provenance"]["source_revision"])
        self.assertEqual(provenance["parent_plan_seed_source_revision"], linux.manifest["provenance"]["source_revision"])
        self.check(windows.manifest, 2)
        self.check(changed.manifest, 2, False)
        data = encode(changed.manifest)
        incoming = ctypes.create_string_buffer(data)
        error = ctypes.create_string_buffer(128)
        result = Result()
        self.assertEqual(self.api(incoming, len(data), 2, ctypes.byref(result), error, len(error)), 0)
        self.assertIn(b"fixed-point provenance differs", error.value)
        self.check(windows.manifest, 2)

    def test_explicit_release_retains_strict_historical_reader(self):
        from tests.test_seed_pair import released_alias_pair
        for long_paths in (False, True):
            record, linux, windows = released_alias_pair(long_paths)
            for fmt, document in ((1, linux), (2, windows)):
                self.check(document, fmt, False)
                self.check_release(record, document, fmt)
                self.check_release(release(), document, fmt, False)
                # A release match cannot replace a complete supported target.
                altered = copy.deepcopy(document)
                altered["target"]["entry"] += 1
                self.check_release(record, altered, fmt, False)
                self.check(document, fmt, False)

    def test_release_aware_reader_clears_failures_and_bounds_diagnostics(self):
        from tests.test_seed_pair import released_alias_pair
        record, linux, windows = released_alias_pair()
        for fmt, document in ((1, linux), (2, windows)):
            for capacity in (0, 1, 2, 8, 128):
                self.check_release(record, document, fmt, capacity=capacity)
                self.check_release(b"bad", document, fmt, False, capacity)
                self.check_release(record, b"bad", fmt, False, capacity)
                altered = copy.deepcopy(document)
                altered["provenance"]["fixed_point_result"] = "fail"
                self.check_release(record, altered, fmt, False, capacity)
            for invalid in (b"", b"x" * 1048577, encode(document) + b"x"):
                self.check_release(record, invalid, fmt, False)
        incoming = ctypes.create_string_buffer(encode(record))
        manifest_input = ctypes.create_string_buffer(encode(windows))
        args = [incoming, len(encode(record)), manifest_input, len(encode(windows)), 2, ctypes.pointer(Result()), None, 0]
        for index in (0, 2, 5):
            invalid = list(args)
            invalid[index] = None
            self.assertEqual(self.release_api(*invalid), 0)
        for fmt in (0, 3, 0xffffffff):
            invalid = list(args)
            invalid[4] = fmt
            self.assertEqual(self.release_api(*invalid), 0)
        invalid = list(args)
        invalid[-1] = 1
        self.assertEqual(self.release_api(*invalid), 0)

    def test_release_aware_reader_checks_every_release_identity(self):
        from tests.test_seed_pair import released_alias_pair
        record, linux, windows = released_alias_pair(True)
        for fmt, document in ((1, linux), (2, windows)):
            for key in ("source_revision", "source_snapshot_sha256", "source_input_count",
                        "parent_source_revision", "parent_linux_manifest_sha256" if fmt == 1 else "parent_windows_manifest_sha256",
                        "linux_plan_sha256", "windows_plan_sha256"):
                altered = copy.deepcopy(record)
                altered[key] = altered[key] + 1 if isinstance(altered[key], int) else "a" * len(altered[key])
                if fmt == 1 and key == "windows_plan_sha256":
                    continue  # The Linux manifest does not declare the Windows plan.
                self.check_release(altered, document, fmt, False)
            for index in range(6):
                altered = copy.deepcopy(record)
                row = next(item for item in altered["artifacts"] if item["format"] == ("elf32" if fmt == 1 else "pe32")
                           and item["name"] == document["artifacts"][index]["name"])
                row["size"] += 1
                self.check_release(altered, document, fmt, False)

    def test_user_link_alias_profiles_require_exact_plan_count_and_parent(self):
        for long_paths in (False, True):
            for fmt in (1, 2):
                value = user_link_alias_manifest(fmt, long_paths=long_paths)
                expected = (6, (5 if long_paths else 4) if fmt == 2 else 0)
                self.check(value, fmt, expected=expected)
                for count in (75, 79, True, 77.0, 78.0):
                    altered = copy.deepcopy(value)
                    altered["provenance"]["source_input_count"] = count
                    self.check(altered, fmt, False)
                if fmt == 2:
                    for other in (user_link_alias_manifest(2, long_paths=not long_paths),
                                  user_compile_parent_manifest(2, long_paths=long_paths)):
                        altered = copy.deepcopy(value)
                        altered["provenance"]["native_build_plan_sha256"] = other["provenance"]["native_build_plan_sha256"]
                        self.check(altered, fmt, False)
                # Linux count 77 also describes the historical long profile.
                if fmt == 2 or long_paths:
                    previous = next_parent_manifest(fmt, long_paths=long_paths)
                    altered = copy.deepcopy(value)
                    for field, original in previous["provenance"].items():
                        if field.startswith("parent_"):
                            altered["provenance"][field] = original
                    self.check(altered, fmt, False)
                self.check(value, fmt)

    def test_user_abi_candidate_retains_all_four_windows_import_profiles(self):
        for long_paths in (False, True):
            for aliases in (False, True):
                for fmt in (1, 2):
                    value = user_abi_manifest(fmt, long_paths=long_paths, aliases=aliases)
                    profile = (4 if aliases else 2) + int(long_paths)
                    self.check(value, fmt, expected=(6, profile if fmt == 2 else 0))
                    for count in (79, 83, True, float(value["provenance"]["source_input_count"])):
                        altered = copy.deepcopy(value)
                        altered["provenance"]["source_input_count"] = count
                        self.check(altered, fmt, False)
                    fields = ("build_plan_sha256",) if fmt == 1 else (
                        "linux_candidate_build_plan_sha256", "native_build_plan_sha256")
                    for field in fields:
                        altered = copy.deepcopy(value)
                        owner = altered if fmt == 1 else altered["provenance"]
                        previous = manifest(fmt) if fmt == 1 else manifest(fmt)["provenance"]
                        owner[field] = previous[field]
                        self.check(altered, fmt, False)
                    self.check(value, fmt, expected=(6, profile if fmt == 2 else 0))

    def test_user_abi_candidate_requires_both_complete_source_and_link_rows(self):
        value = user_abi_manifest(1)
        plan = value["build_plan"]
        self.assertEqual(len(plan["sources"]), 29)
        self.assertEqual(plan["links"]["cupidbuild"][-3:],
                         ["user_syscall_abi", "cupidbuild_user_abi", "runtime"])
        for name in ("user_syscall_abi", "cupidbuild_user_abi"):
            index = next(index for index, row in enumerate(plan["sources"]) if row["name"] == name)
            for key, original in plan["sources"][index].items():
                altered = copy.deepcopy(value)
                altered["build_plan"]["sources"][index][key] = changed(original)
                self.check(altered, 1, False)
            for section in ("sources", "link"):
                altered = copy.deepcopy(value)
                if section == "sources":
                    altered["build_plan"]["sources"].pop(index)
                else:
                    altered["build_plan"]["links"]["cupidbuild"].remove(name)
                altered["build_plan_sha256"] = seed._build_plan_sha256(altered["build_plan"])
                self.check(altered, 1, False)
        altered = copy.deepcopy(value)
        order = altered["build_plan"]["links"]["cupidbuild"]
        order[-3], order[-2] = order[-2], order[-3]
        altered["build_plan_sha256"] = seed._build_plan_sha256(altered["build_plan"])
        self.check(altered, 1, False)
        self.check(value, 1, expected=(6, 0))

    def test_user_abi_windows_profile_cannot_borrow_another_inventory_or_plan(self):
        profiles = [user_abi_manifest(2, long_paths=long_paths, aliases=aliases)
                    for long_paths in (False, True) for aliases in (False, True)]
        for index, value in enumerate(profiles):
            for other_index, other in enumerate(profiles):
                if other_index == index:
                    continue
                altered = copy.deepcopy(value)
                altered["provenance"]["native_build_plan_sha256"] = other["provenance"]["native_build_plan_sha256"]
                same_count = value["provenance"]["source_input_count"] == other["provenance"]["source_input_count"]
                # Count 81 supports both long paths and default user-link aliases.
                self.check(altered, 2, same_count,
                           expected=(6, (3 if other_index == 2 else 4)) if same_count else None)

    def test_user_compile_parent_profiles_require_complete_release_tuple(self):
        for long_paths in (False, True):
            for fmt in (1, 2):
                expected = (6, (3 if long_paths else 2) if fmt == 2 else 0)
                value = user_compile_parent_manifest(fmt, long_paths=long_paths)
                self.check(value, fmt, expected=expected)
                previous = next_parent_manifest(fmt, long_paths=long_paths)
                for field, original in value["provenance"].items():
                    if not field.startswith("parent_"):
                        continue
                    for replacement in ("0" * len(original), previous["provenance"][field]):
                        altered = copy.deepcopy(value)
                        altered["provenance"][field] = replacement
                        self.check(altered, fmt, False)
                for count in (75, 79, True, 76.0, 77.0):
                    altered = copy.deepcopy(value)
                    altered["provenance"]["source_input_count"] = count
                    self.check(altered, fmt, False)
                if fmt == 2:
                    altered = copy.deepcopy(value)
                    altered["provenance"]["source_input_count"] = 76 if long_paths else 77
                    self.check(altered, fmt, False)
                self.check(value, fmt, expected=expected)

    def test_next_parent_profiles_accept_complete_release_tuple(self):
        for long_paths in (False, True):
            for fmt in (1, 2):
                expected = (6, (3 if long_paths else 2) if fmt == 2 else 0)
                value = next_parent_manifest(fmt, long_paths=long_paths)
                self.check(value, fmt, expected=expected)
                previous = (long_path_manifest if long_paths else default_parent_manifest)(fmt)
                for field, original in value["provenance"].items():
                    if not field.startswith("parent_"):
                        continue
                    for changed in ("0" * len(original), previous["provenance"][field]):
                        altered = copy.deepcopy(value)
                        altered["provenance"][field] = changed
                        self.check(altered, fmt, False)
                for count in (75, 78, True, 76.0, 77.0):
                    altered = copy.deepcopy(value)
                    altered["provenance"]["source_input_count"] = count
                    self.check(altered, fmt, False)
                if fmt == 2:
                    altered = copy.deepcopy(value)
                    altered["provenance"]["source_input_count"] = 76 if long_paths else 77
                    self.check(altered, fmt, False)
                self.check(value, fmt, expected=expected)

    def test_default_profile_accepts_complete_current_parent_tuple(self):
        for fmt in (1, 2):
            value = default_parent_manifest(fmt)
            expected = (6, 2 if fmt == 2 else 0)
            self.check(value, fmt, expected=expected)
            previous = selection_manifest(fmt)["provenance"]
            for field, original in value["provenance"].items():
                if not field.startswith("parent_"):
                    continue
                for changed in ("0" * len(original), previous[field]):
                    altered = copy.deepcopy(value)
                    altered["provenance"][field] = changed
                    self.check(altered, fmt, False)
                    self.check(value, fmt, expected=expected)
            for count in (59, 61, 66, 73, 75, 78, True, 76.0):
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = count
                self.check(altered, fmt, False)
            if fmt == 2:
                altered = copy.deepcopy(value)
                altered["provenance"]["native_build_plan_sha256"] = (
                    long_path_manifest(2)["provenance"]["native_build_plan_sha256"]
                )
                self.check(altered, fmt, False)
            self.check(value, fmt, expected=expected)

    def test_long_path_pair_binds_release_and_actual_linux_manifest_bytes(self):
        linux, windows = long_path_manifest(1), long_path_manifest(2)
        windows["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256(encode(linux)).hexdigest()
        reviewed = release()
        reviewed.update(source_input_count=77,
            parent_source_revision="5ba6ea24fdef3b23c505551ab537688e681c9593",
            parent_linux_manifest_sha256="1a8a91581562751cca6c51c5cd3de1259a73e92a0c161d1425155724d80ba7a8",
            parent_windows_manifest_sha256="c8c2780000575ff255b6f72287f85d4b47f85b420b3fa0f29eeacfe74207175b",
            windows_plan_sha256=windows["provenance"]["native_build_plan_sha256"])
        api = self.library.test_pair
        api.argtypes = [ctypes.c_void_p, ctypes.c_size_t] * 3 + [ctypes.c_void_p, ctypes.c_size_t]
        api.restype = ctypes.c_int

        def check(record, first, second, accepted, capacity=128):
            raw = tuple(encode(value) for value in (record, first, second))
            incoming = tuple(ctypes.create_string_buffer(value) for value in raw)
            before = tuple(bytes(value) for value in incoming)
            error = ctypes.create_string_buffer(b"!" * (capacity + 8))
            self.assertEqual(api(incoming[0], len(raw[0]), incoming[1], len(raw[1]),
                incoming[2], len(raw[2]), error, capacity), int(accepted), error.value)
            self.assertEqual(tuple(bytes(value) for value in incoming), before)
            self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
            if capacity:
                self.assertEqual(error.value == b"", accepted or capacity == 1)
            if os.environ.get("CUPID_MANIFEST_PAIR_EXPORT"):
                self.pair_records.append((*raw, accepted))

        for capacity in (0, 1, 128):
            check(reviewed, linux, windows, True, capacity)
        for field in ("source_input_count", "windows_plan_sha256", "parent_source_revision",
                      "parent_linux_manifest_sha256", "parent_windows_manifest_sha256"):
            bad = copy.deepcopy(reviewed)
            bad[field] = 76 if field == "source_input_count" else "0" * len(bad[field])
            check(bad, linux, windows, False)
        for field in ("source_input_count", "native_build_plan_sha256", "plan_seed_manifest_sha256"):
            bad = copy.deepcopy(windows)
            bad["provenance"][field] = 76 if field == "source_input_count" else "0" * 64
            check(reviewed, linux, bad, False)
        default_linux = copy.deepcopy(linux)
        default_linux["provenance"]["source_input_count"] = 76
        self.check(default_linux, 1, expected=(6, 0))
        rebound_windows = copy.deepcopy(windows)
        rebound_windows["provenance"]["plan_seed_manifest_sha256"] = (
            hashlib.sha256(encode(default_linux)).hexdigest()
        )
        check(reviewed, default_linux, rebound_windows, False)
        # Harmless JSON whitespace still changes the bytes referenced by Windows.
        changed = dict(reversed(list(linux.items())))
        check(reviewed, changed, windows, False)
        recovered = copy.deepcopy(windows)
        recovered["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256(encode(changed)).hexdigest()
        check(reviewed, changed, recovered, True)
        check(reviewed, linux, windows, True)

    def test_long_path_generation_accepts_exact_plan_count_and_parent_tuple(self):
        for fmt in (1, 2):
            value = long_path_manifest(fmt)
            self.check(value, fmt, expected=(6, 3 if fmt == 2 else 0))
            for field in value["provenance"]:
                if field.startswith("parent_"):
                    altered = copy.deepcopy(value)
                    altered["provenance"][field] = selection_manifest(fmt)["provenance"][field]
                    self.check(altered, fmt, False)
                    self.check(value, fmt, expected=(6, 3 if fmt == 2 else 0))
            for count in (76, 78, True, 77.0):
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = count
                # The Linux plan and parent are shared. The reviewed release
                # and captured inventory bind its selected count and digest.
                self.check(altered, fmt, fmt == 1 and count == 76,
                           expected=(6, 0) if fmt == 1 and count == 76 else None)
            if fmt == 2:
                for field in ("native_build_plan_sha256", "linux_candidate_build_plan_sha256"):
                    altered = copy.deepcopy(value)
                    altered["provenance"][field] = "0" * 64
                    self.check(altered, fmt, False)
                altered = copy.deepcopy(value)
                altered["provenance"]["native_build_plan_sha256"] = selection_manifest(2)["provenance"]["native_build_plan_sha256"]
                self.check(altered, fmt, False)

    def test_artifact_generation_accepts_complete_plan_and_utf8_import_profile(self):
        for fmt in (1, 2):
            value = artifact_manifest(fmt)
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
            for count in (59, 61, 66, 73, 75, 77):
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = count
                self.check(altered, fmt, False)
                self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))

    def test_artifact_generation_rejects_missing_changed_and_duplicate_modules(self):
        value = artifact_manifest(1)
        for name in ("cupidbuild_artifacts", "artifact_size_policy"):
            for mutation in ("missing", "path", "extensions", "duplicate"):
                altered = copy.deepcopy(value)
                sources = altered["build_plan"]["sources"]
                row = next(row for row in sources if row["name"] == name)
                if mutation == "missing":
                    sources.remove(row)
                elif mutation == "path":
                    row["path"] = "/toolchain/other.cc"
                elif mutation == "extensions":
                    row["gnu_extensions"] = True
                else:
                    sources.append(copy.deepcopy(row))
                self.check(altered, 1, False)
            altered = copy.deepcopy(value)
            altered["build_plan"]["links"]["cupidbuild"].remove(name)
            self.check(altered, 1, False)
        self.check(value, 1)

    def test_artifact_generation_rejects_mixed_plan_profiles(self):
        value = artifact_manifest(2)
        utf8 = candidate_manifest(2)
        utf8["provenance"]["native_build_plan_sha256"] = "a31575236059b77a47bb58c79072754258c4762d30105319c451e407b7353f99"
        for source in (utf8, candidate_manifest(2), historical_manifest(2)):
            for field in ("linux_candidate_build_plan_sha256", "native_build_plan_sha256"):
                altered = copy.deepcopy(value)
                self.assertNotEqual(value["provenance"][field], source["provenance"][field])
                altered["provenance"][field] = source["provenance"][field]
                self.check(altered, 2, False)
        self.check(value, 2, expected=(6, 2))

    def test_artifact_generation_binds_complete_utf8_parent_tuple(self):
        for fmt in (1, 2):
            value = artifact_manifest(fmt)
            installed = manifest(fmt)
            for key in installed["provenance"]:
                if key.startswith("parent_"):
                    installed["provenance"][key] = (
                        "e4f2ed652e756b1abb375ec061923f5259799e01" if key.endswith("source_revision") else
                        "c715ce354c28b97c6b9c4e5702c98d368d07dff9e52bc2f0deb71ab3194d2395" if "execution" in key else
                        "da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed")
            parents = {key: item for key, item in value["provenance"].items()
                       if key.startswith("parent_")}
            for field, expected in parents.items():
                for replacement in ("0" * len(expected), installed["provenance"][field]):
                    self.assertNotEqual(expected, replacement)
                    altered = copy.deepcopy(value)
                    altered["provenance"][field] = replacement
                    self.check(altered, fmt, False)
                    self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))

    def test_selection_generation_requires_complete_artifact_parent_tuple(self):
        for fmt in (1, 2):
            value = selection_manifest(fmt)
            previous = artifact_manifest(fmt)["provenance"]
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
            for key, expected in value["provenance"].items():
                if not key.startswith("parent_"):
                    continue
                for replacement in ("0" * len(expected), previous[key]):
                    self.assertNotEqual(expected, replacement)
                    altered = copy.deepcopy(value)
                    altered["provenance"][key] = replacement
                    self.check(altered, fmt, False)
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
        for role in ("execution", "plan"):
            value = selection_manifest(2)
            for suffix in ("source_revision", "manifest_sha256"):
                key = "parent_" + role + "_seed_" + suffix
                value["provenance"][key] = artifact_manifest(2)["provenance"][key]
            self.check(value, 2, False)

    def test_utf8_promotion_accepts_exact_conditional_assembly_parent(self):
        revision = "e4f2ed652e756b1abb375ec061923f5259799e01"
        linux = "da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed"
        windows = "c715ce354c28b97c6b9c4e5702c98d368d07dff9e52bc2f0deb71ab3194d2395"
        for fmt in (1, 2):
            value = candidate_manifest(fmt)
            value["provenance"]["source_input_count"] = 73
            if fmt == 1:
                parents = {"parent_seed_manifest_sha256": linux,
                           "parent_seed_source_revision": revision}
            else:
                value["provenance"]["native_build_plan_sha256"] = seed._build_plan_sha256(
                    seed._windows_build_plan(candidate_manifest(1)["build_plan"], utf8=True))
                parents = {"parent_execution_seed_manifest_sha256": windows,
                           "parent_execution_seed_source_revision": revision,
                           "parent_plan_seed_manifest_sha256": linux,
                           "parent_plan_seed_source_revision": revision}
            previous = {
                key: ("142a9737f618ab8500308576a1c222501d639e5f" if key.endswith("revision")
                      else "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd"
                      if "execution" in key else
                      "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52")
                for key in parents
            }
            value["provenance"].update(parents)
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
            for key, expected in parents.items():
                for replacement in ("0" * len(expected), previous[key]):
                    self.assertNotEqual(replacement, expected)
                    altered = copy.deepcopy(value)
                    altered["provenance"][key] = replacement
                    self.check(altered, fmt, False)
                    self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))

    def test_windows_behavior_seed_has_consistent_candidate_provenance(self):
        with tempfile.TemporaryDirectory(prefix="cupid-behavior-manifest-") as temporary:
            frozen = seed.freeze_seed_inputs(
                ROOT / "bootstrap/seeds/i386-windows/manifest.json",
                Path(temporary) / "seed",
            )
            plan = seed._candidate_build_plan(manifest(1)["build_plan"])
            digest = seed._build_plan_sha256(seed._windows_build_plan(plan, utf8=True))
            snapshot = seed.capture_source_snapshot(ROOT, plan, windows_utf8=True)
            retargeted = seed._retarget_native_windows_behavior_seed(
                frozen, digest, plan, snapshot, utf8=True)
            self.check(retargeted.manifest, 2, False)
            provenance = retargeted.manifest['provenance']
            record = release()
            record.update(source_revision=provenance['source_revision'],
                source_snapshot_sha256=provenance['source_snapshot_sha256'],
                source_input_count=provenance['source_input_count'],
                parent_source_revision=provenance['parent_execution_seed_source_revision'],
                parent_linux_manifest_sha256=provenance['parent_plan_seed_manifest_sha256'],
                parent_windows_manifest_sha256=provenance['parent_execution_seed_manifest_sha256'],
                linux_plan_sha256=provenance['linux_candidate_build_plan_sha256'],
                windows_plan_sha256=provenance['native_build_plan_sha256'])
            self.check_release(record, retargeted.manifest, 2, expected=(6, 2))
            for field in ("source_input_count", "linux_candidate_build_plan_sha256"):
                altered = copy.deepcopy(retargeted.manifest)
                altered["provenance"][field] = historical_manifest(2)["provenance"][field]
                self.check_release(record, altered, 2, False)

    def test_candidate_plan_keeps_its_source_count_and_complete_closure(self):
        for fmt in (1, 2):
            self.check(candidate_manifest(fmt), fmt, expected=(6, int(fmt == 2)))
            for count in (50, 59, 61, 65, 67):
                value = candidate_manifest(fmt)
                value["provenance"]["source_input_count"] = count
                self.check(value, fmt, False)
            value = historical_manifest(fmt)
            value["provenance"]["source_input_count"] = 66
            self.check(value, fmt, False)

    def test_utf8_generation_binds_source_count_and_exact_plan_pair(self):
        for fmt in (1, 2):
            value = candidate_manifest(fmt)
            value["provenance"]["source_input_count"] = 73
            if fmt == 2:
                linux = candidate_manifest(1)["build_plan"]
                value["provenance"]["native_build_plan_sha256"] = seed._build_plan_sha256(
                    seed._windows_build_plan(linux, utf8=True))
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
            for count in (68, 69, 70, 71, 72, 74, True, 73.0):
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = count
                self.check(altered, fmt, False)
            if fmt == 2:
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = 66
                self.check(altered, fmt, False)
                for key in ("native_build_plan_sha256", "linux_candidate_build_plan_sha256"):
                    altered = copy.deepcopy(value)
                    altered["provenance"][key] = historical_manifest(2)["provenance"][key]
                    self.check(altered, fmt, False)
                altered = candidate_manifest(2)
                altered["provenance"]["source_input_count"] = 73
                self.check(altered, fmt, False)

    def test_candidate_plan_rejects_each_changed_source_and_link(self):
        for section in ("sources", "links"):
            value = candidate_manifest(1)
            for path, old in leaves(value["build_plan"][section]):
                altered = copy.deepcopy(value)
                target = altered["build_plan"][section]
                for part in path[:-1]:
                    target = target[part]
                target[path[-1]] = changed(old)
                self.check(altered, 1, False)
        for key in ("linux_candidate_build_plan_sha256", "native_build_plan_sha256"):
            value = candidate_manifest(2)
            value["provenance"][key] = historical_manifest(2)["provenance"][key]
            self.check(value, 2, False)

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="cupid-manifest-api-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        shim = tmp / "shim.c"
        shim.write_text('#include "seed_manifest.h"\n'
                        '#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                        'int test_manifest(const unsigned char *p, size_t n, unsigned int f, '
                        'cupid_seed_manifest_result_t *r, char *e, size_t c) { '
                        'return cupid_seed_manifest_validate(p,n,f,r,e,c); }\n'
                        '#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                        'int test_pair(const unsigned char *r, size_t rn, '
                        'const unsigned char *l, size_t ln, const unsigned char *w, size_t wn, '
                        'char *e, size_t c) { return cupid_seed_pair_validate(r,rn,l,ln,w,wn,e,c); }\n')
        with shim.open('a', encoding='utf-8') as stream:
            stream.write('#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                         'int test_manifest_release(const unsigned char *r, size_t rn, '
                         'const unsigned char *m, size_t mn, unsigned int f, '
                         'cupid_seed_manifest_result_t *o, char *e, size_t c) { '
                         'return cupid_seed_manifest_validate_release(r,rn,m,mn,f,o,e,c); }\n')
        library = tmp / ("manifest.dll" if os.name == "nt" else "manifest.so")
        command = [_host_compiler(), "-std=c11", "-O2", "-pedantic", "-Wall", "-Wextra", "-Werror",
                   "-shared", "-I", str(DRAFT), "-I", str(ROOT / "toolchain"), "-x", "c", str(shim),
                   str(DRAFT / "seed_manifest.cc"), str(ROOT / "toolchain/contract_parse_internal.cc"),
                   str(DRAFT / "seed_release.cc"), str(ROOT / "toolchain/cupidbuild_host.cc"),
                   str(ROOT / "toolchain/path_encoding.cc"),
                   "-o", str(library)]
        if os.name != "nt": command.append("-fPIC")
        else: command.extend(["-D_CRT_SECURE_NO_WARNINGS", "-lntdll"])
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if built.returncode: raise AssertionError(built.stdout + built.stderr)
        cls.library = ctypes.CDLL(str(library))
        if os.name == "nt": cls.addClassCleanup(cls.unload)
        cls.api = cls.library.test_manifest
        cls.api.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.POINTER(Result),
                           ctypes.c_void_p, ctypes.c_size_t]
        cls.api.restype = ctypes.c_int
        cls.release_api = cls.library.test_manifest_release
        cls.release_api.argtypes = [ctypes.c_void_p, ctypes.c_size_t] * 2 + [ctypes.c_uint,
            ctypes.POINTER(Result), ctypes.c_void_p, ctypes.c_size_t]
        cls.release_api.restype = ctypes.c_int

    @classmethod
    def unload(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def check(self, value, fmt, accepted=True, capacity=128, expected=None):
        data = encode(value) if isinstance(value, dict) else value
        incoming = ctypes.create_string_buffer(data)
        before = bytes(incoming)
        result = Result(99, 99)
        ctypes.memset(ctypes.byref(result), 0xa5, ctypes.sizeof(result))
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        status = self.api(incoming, len(data), fmt, ctypes.byref(result), error, capacity)
        self.assertEqual(status, int(accepted), (fmt, error.value, data[:80]))
        self.assertEqual(bytes(incoming), before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        if accepted:
            decoded = json.loads(data)
            if expected is None:
                profile = 0
                if fmt == 2:
                    provenance = decoded["provenance"]
                    digest, count = provenance["native_build_plan_sha256"], provenance["source_input_count"]
                    compiler = {name for _, names in seed._promoted_windows_imports("cupidc", digest, count) for name in names}
                    coordinator = {name for _, names in seed._promoted_windows_imports("cupidbuild", digest, count) for name in names}
                    profile = 2 + int("GetFullPathNameW" in compiler) + 2 * int("GetFinalPathNameByHandleW" in coordinator)
                expected = (6, profile)
            self.assertEqual((result.artifact_count, result.current_windows_plan), expected)
            roles = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")
            summary = ["1", str(expected[0]), str(expected[1])]
            for index, role in enumerate(roles):
                actual = result.artifacts[index]
                if index >= result.artifact_count:
                    self.assertEqual(bytes(actual), bytes(ctypes.sizeof(actual)))
                    continue
                row = next(row for row in decoded["artifacts"] if row["name"] == role)
                self.assertEqual((actual.file.decode(), actual.size, actual.sha256.decode()),
                                 (row["file"], row["size"], row["sha256"]))
                summary.extend((actual.file.decode(), str(actual.size), actual.sha256.decode()))
            if capacity: self.assertEqual(error.value, b"")
        else:
            self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
            summary = ["0"]
            if capacity > 1: self.assertTrue(error.value)
        if os.environ.get("CUPID_MANIFEST_EXPORT"):
            self.records.append((data, fmt, " ".join(summary) + "\n"))

    def test_legacy_five_tool_manifests_and_previous_windows_plan(self):
        from tools import bootstrap_toolchain as seed
        for fmt in (1, 2):
            value = historical_manifest(fmt)
            value["schema"] = value["schema"].replace(".v2", ".v1")
            value["artifacts"] = [row for row in value["artifacts"] if row["name"] != "cupidbuild"]
            previous = value["provenance"]
            provenance = {key: previous[key] for key in ("fixed_point_command", "fixed_point_result", "producer_lineage")}
            provenance.update(source_revision=seed.SEED_SOURCE_REVISION,
                              source_snapshot_sha256=seed.SEED_SOURCE_SNAPSHOT_SHA256,
                              source_input_count=50)
            if fmt == 1:
                provenance["seed_generation"] = "stage-four"
                value["build_plan"]["sources"] = value["build_plan"]["sources"][:-3]
                del value["build_plan"]["links"]["cupidbuild"]
                value["build_plan_sha256"] = "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc"
            else:
                provenance.update(artifact_generation="paired-stage-four-native-windows",
                    parent_seed_manifest_sha256="b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b",
                    parent_seed_source_revision=seed.SEED_SOURCE_REVISION)
            value["provenance"] = provenance
            self.check(value, fmt, expected=(5, 0))
            self.check(value, 3 - fmt, False)
            value["provenance"]["source_input_count"] = 59
            self.check(value, fmt, False)
        value = historical_manifest(2)
        value["provenance"]["native_build_plan_sha256"] = "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14"
        self.check(value, 2, expected=(6, 0))

    def test_artifact_size_bound_is_independent_of_host_word_size(self):
        for fmt in (1, 2):
            for size in (1, 67108864, 67108865, 2**32 - 1, 2**32, 2**64 - 1):
                value = manifest(fmt)
                value["artifacts"][0]["size"] = size
                self.check(value, fmt, size <= 67108864)

    def test_null_arguments_clear_result_and_allow_optional_diagnostics(self):
        result = Result(99, 99)
        raw = encode(manifest(1))
        data = ctypes.create_string_buffer(raw)
        self.assertEqual(self.api(None, 1, 1, ctypes.byref(result), None, 0), 0)
        self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
        self.assertEqual(self.api(data, len(raw), 1, None, None, 0), 0)
        self.assertEqual(self.api(data, len(raw), 1, ctypes.byref(result), None, 1), 0)
        self.assertEqual(self.api(data, len(raw), 1, ctypes.byref(result), None, 0), 1)

    def test_both_formats_and_reordering(self):
        for fmt in (1, 2):
            value = manifest(fmt)
            self.check(value, fmt)
            self.check(value, 3 - fmt, False)
            value = dict(reversed(list(value.items())))
            value["artifacts"].reverse()
            self.check(value, fmt)
            self.check(json.dumps(value, indent=3).encode(), fmt)

    def test_escaped_keys_and_every_string_value(self):
        for fmt in (1, 2):
            data = encode(manifest(fmt))
            escaped = []
            # All current manifest strings are ASCII and contain no escaped quotes.
            parts = data.split(b'"')
            for index, part in enumerate(parts):
                if index % 2:
                    part = b"".join(f"\\u{byte:04x}".encode() for byte in part)
                escaped.append(part)
            self.check(b'"'.join(escaped), fmt)
            for old, new in ((b'"schema"', b'"sch\\u0065ma"'),
                             (b"cupidasm", b"cupid\\u0061sm"),
                             (b"/toolchain", b"\\/toolchain")):
                self.check(data.replace(old, new), fmt)

    def test_every_fixed_target_lineage_and_plan_leaf(self):
        for fmt in (1, 2):
            candidate = manifest(fmt)
            locations = [("target",), ("provenance", "producer_lineage")]
            if fmt == 1: locations.append(("build_plan",))
            for base in locations:
                subtree = candidate
                for key in base: subtree = subtree[key]
                for path, value in leaves(subtree):
                    altered = copy.deepcopy(candidate)
                    destination = altered
                    full = base + path
                    for key in full[:-1]: destination = destination[key]
                    destination[full[-1]] = changed(value)
                    with self.subTest(fmt=fmt, path=full): self.check(altered, fmt, False)
            altered = copy.deepcopy(candidate)
            altered["provenance"]["fixed_point_result"] = "fail"
            self.check(altered, fmt, False)

    def test_plan_array_membership_and_order(self):
        for key in ("sources", "include_arguments", "producer_tools"):
            for operation in ("drop", "extra", "reverse"):
                value = manifest(1)
                rows = value["build_plan"][key]
                if operation == "drop": rows.pop()
                elif operation == "extra": rows.append(copy.deepcopy(rows[0]))
                else: rows.reverse()
                self.check(value, 1, False)
        for tool in manifest(1)["build_plan"]["links"]:
            value = manifest(1)
            value["build_plan"]["links"][tool].reverse()
            self.check(value, 1, False)

    def test_missing_extra_duplicate_object_fields(self):
        def objects(value, path=()):
            if isinstance(value, dict):
                yield path, value
                for key, child in value.items(): yield from objects(child, path + (key,))
            elif isinstance(value, list):
                for index, child in enumerate(value): yield from objects(child, path + (index,))
        for fmt in (1, 2):
            baseline = manifest(fmt)
            for location, target in objects(baseline):
                for key in target:
                    candidate = copy.deepcopy(baseline)
                    altered = candidate
                    for part in location: altered = altered[part]
                    del altered[key]
                    self.check(candidate, fmt, False)
                raw = encode(baseline)
                obj = encode(target)
                key = next(iter(target))
                duplicate = encode({key: target[key]})[1:-1]
                self.check(raw.replace(obj, obj[:-1] + b"," + duplicate + b"}", 1), fmt, False)
                self.check(raw.replace(obj, obj[:-1] + b',"unknown":0}', 1), fmt, False)
            raw = encode(baseline)
            self.check(raw[:-1] + b',"sch\\u0065ma":"' + baseline["schema"].encode() + b'"}', fmt, False)

    def test_source_admission_and_parent_binding(self):
        for fmt in (1, 2):
            for count in (58, 60, 62, True, 59.0):
                value = manifest(fmt)
                value["provenance"]["source_input_count"] = count
                self.check(value, fmt, False)
            value = historical_manifest(fmt)
            value["provenance"]["source_input_count"] = 61
            value["provenance"]["source_revision"] = "f" * 40
            self.check(value, fmt, expected=(6, int(fmt == 2)))
            for key, val in value["provenance"].items():
                if key.startswith("parent_"):
                    bad = copy.deepcopy(value)
                    bad["provenance"][key] = "1" * len(val)
                    self.check(bad, fmt, False)

    def test_artifact_role_boolean_digest_and_exact_inventory(self):
        for fmt in (1, 2):
            for index in range(6):
                for key, invalid in (("file", "../escape"), ("name", "wrong"),
                                     ("producer", 1), ("sha256", "G" * 64), ("size", 0)):
                    value = manifest(fmt)
                    value["artifacts"][index][key] = invalid
                    self.check(value, fmt, False)
            value = manifest(fmt)
            value["artifacts"][5] = copy.deepcopy(value["artifacts"][0])
            self.check(value, fmt, False)

    def test_truncation_syntax_limits_and_diagnostics(self):
        for fmt in (1, 2):
            raw = encode(manifest(fmt))
            for end in sorted({0, 1, len(raw) - 1, *range(0, len(raw), 41)}):
                self.check(raw[:end], fmt, False, 0)
            for extra in (b"x", b"{}", b"\0", b"\xff"):
                self.check(raw + extra, fmt, False)
            for replacement in (b"\\ud800", b"\\udfff", b"\\u0000", b"\xff"):
                self.check(raw.replace(b"cupidasm", replacement, 1), fmt, False)
            for capacity in (0, 1, 2, 8, 128):
                self.check(b"bad", fmt, False, capacity)
                self.check(raw, fmt, True, capacity)
            self.check(raw + b" " * (1048576 - len(raw)), fmt)
            self.check(raw + b" " * (1048577 - len(raw)), fmt, False)
        self.check(manifest(1), 0, False)
        self.check(manifest(1), 3, False)

    def test_concurrent_formats_and_results(self):
        def run(index):
            fmt = index % 2 + 1
            self.check(manifest(fmt), fmt)
            self.check(manifest(fmt), 3 - fmt, False)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(run, range(64)))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ManifestTests)
    outcome = unittest.TextTestRunner().run(suite)
    if outcome.wasSuccessful() and os.environ.get("CUPID_MANIFEST_EXPORT"):
        destination = Path(os.environ["CUPID_MANIFEST_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for data, fmt, _ in ManifestTests.records:
                stream.write(struct.pack("<II", len(data), fmt))
                stream.write(data)
        with destination.with_suffix(".txt").open("x") as stream:
            stream.write("".join(summary for _, _, summary in ManifestTests.records))
        print(f"Exported {len(ManifestTests.records)} structural manifest cases")
    if outcome.wasSuccessful() and os.environ.get("CUPID_MANIFEST_PAIR_EXPORT"):
        destination = Path(os.environ["CUPID_MANIFEST_PAIR_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for record, linux, windows, _ in ManifestTests.pair_records:
                stream.write(struct.pack("<III", len(record), len(linux), len(windows)))
                stream.write(record + linux + windows)
        destination.with_suffix(".txt").write_text(
            "".join("1\n" if row[3] else "0\n" for row in ManifestTests.pair_records), encoding="ascii")
        print(f"Exported {len(ManifestTests.pair_records)} long-profile pair cases")
    if outcome.wasSuccessful() and os.environ.get("CUPID_MANIFEST_RELEASE_EXPORT"):
        destination = Path(os.environ["CUPID_MANIFEST_RELEASE_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for record, manifest_bytes, fmt, _ in ManifestTests.release_records:
                stream.write(struct.pack("<III", len(record), len(manifest_bytes), fmt))
                stream.write(record + manifest_bytes)
        destination.with_suffix(".txt").write_text(
            "".join(row[3] for row in ManifestTests.release_records), encoding="ascii")
        print(f"Exported {len(ManifestTests.release_records)} release-aware manifest cases")
    sys.exit(not outcome.wasSuccessful())

"""Keep source-owner consumer shapes separate from installed seed authority."""
import copy
import json
from pathlib import Path
import unittest

from tools import bootstrap_toolchain as bootstrap


ROOT = Path(__file__).resolve().parents[1]
LINUX_PLAN = "e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa"
PROFILES = (
    (False, False, False, 108),
    (True, False, False, 112),
    (True, True, False, 113),
    (True, False, True, 113),
    (True, True, True, 114),
)


class SourceOwnerConsumerTests(unittest.TestCase):
    def setUp(self):
        installed = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())
        self.plan = bootstrap._candidate_build_plan(installed["build_plan"])
        self.manifest = {"build_plan": self.plan, "build_plan_sha256": LINUX_PLAN}

    def test_complete_owner_plan_is_accepted(self):
        self.assertEqual(bootstrap._build_plan_sha256(self.plan), LINUX_PLAN)
        bootstrap._validate_build_plan(self.manifest, promoted=True)
        self.assertEqual(len(self.plan["sources"]), 44)
        self.assertEqual(len(self.plan["links"]["cupidbuild"]), 30)

    def test_every_owner_source_and_link_member_is_required(self):
        for name, _, _ in bootstrap.SOURCE_OWNER_SOURCES:
            for field in ("name", "path", "gnu_extensions"):
                altered = copy.deepcopy(self.manifest)
                row = next(row for row in altered["build_plan"]["sources"] if row["name"] == name)
                row[field] = not row[field] if field == "gnu_extensions" else "wrong"
                with self.subTest(name=name, field=field), self.assertRaises(bootstrap.BootstrapError):
                    bootstrap._validate_build_plan(altered, promoted=True)
            for group in ("sources", "links"):
                for operation in ("remove", "duplicate", "reorder"):
                    altered = copy.deepcopy(self.manifest)
                    plan = altered["build_plan"]
                    rows = plan["sources"] if group == "sources" else plan["links"]["cupidbuild"]
                    index = next(i for i, row in enumerate(rows)
                                 if (row["name"] if group == "sources" else row) == name)
                    if operation == "remove":
                        rows.pop(index)
                    elif operation == "duplicate":
                        rows.append(copy.deepcopy(rows[index]))
                    else:
                        rows[index], rows[index - 1] = rows[index - 1], rows[index]
                    with self.subTest(name=name, group=group, operation=operation):
                        with self.assertRaises(bootstrap.BootstrapError):
                            bootstrap._validate_build_plan(altered, promoted=True)

    def test_owner_plan_requires_its_exact_profile(self):
        for digest, promoted in (("0" * 64, True), (LINUX_PLAN, False)):
            altered = copy.deepcopy(self.manifest)
            altered["build_plan_sha256"] = digest
            with self.subTest(digest=digest, promoted=promoted):
                with self.assertRaises(bootstrap.BootstrapError):
                    bootstrap._validate_build_plan(altered, promoted=promoted)

    def test_historical_disk_plan_remains_accepted(self):
        historical = copy.deepcopy(self.manifest)
        plan = historical["build_plan"]
        roles = {row[0] for row in (*bootstrap.EXPECTED_SOURCES, *bootstrap.PROMOTED_SOURCES)}
        plan["sources"] = [row for row in plan["sources"] if row["name"] in roles]
        plan["links"]["cupidbuild"] = list(bootstrap.PROMOTED_CUPIDBUILD_LINK)
        historical["build_plan_sha256"] = bootstrap._build_plan_sha256(plan)
        self.assertEqual(len(plan["sources"]), 37)
        bootstrap._validate_build_plan(historical, promoted=True)

    def test_all_worker_import_profiles_are_exact_and_role_specific(self):
        for utf8, long_paths, aliases, count in PROFILES:
            plan = bootstrap._windows_build_plan(self.plan, utf8=utf8, long_paths=long_paths,
                                                user_link_aliases=aliases, observer_workers=True)
            digest = bootstrap._build_plan_sha256(plan)
            for role in bootstrap.CANDIDATE_TOOL_NAMES:
                with self.subTest(count=count, long_paths=long_paths, aliases=aliases, role=role):
                    actual = bootstrap._promoted_windows_imports(role, digest, count)
                    expected = bootstrap._windows_profile_imports(role, utf8=utf8,
                        long_paths=long_paths, user_link_aliases=aliases, observer_workers=True)
                    self.assertEqual(actual, expected)
                    self.assertEqual("CreateThread" in dict(actual)["KERNEL32.dll"], role == "cupidbuild")

    def test_worker_import_profiles_reject_changed_counts_and_unknown_roles(self):
        for utf8, long_paths, aliases, selected in PROFILES:
            plan = bootstrap._windows_build_plan(self.plan, utf8=utf8, long_paths=long_paths,
                                                user_link_aliases=aliases, observer_workers=True)
            digest = bootstrap._build_plan_sha256(plan)
            for count in (0, 99, 108, 112, 113, 114, True, float(selected)):
                if type(count) is int and count == selected:
                    continue
                with self.subTest(digest=digest, count=count), self.assertRaises(bootstrap.BootstrapError):
                    bootstrap._promoted_windows_imports("cupidbuild", digest, count)
            with self.assertRaises(bootstrap.BootstrapError):
                bootstrap._promoted_windows_imports("unknown", digest, selected)
        with self.assertRaises(bootstrap.BootstrapError):
            bootstrap._promoted_windows_imports("cupidbuild", "0" * 64, 114)

    def test_installed_seed_pair_retains_full_provenance_and_image_checks(self):
        linux = bootstrap.verify_seed_inputs(ROOT / "bootstrap/seeds/i386-linux/manifest.json")
        windows = bootstrap.verify_seed_inputs(ROOT / "bootstrap/seeds/i386-windows/manifest.json")
        bootstrap._require_seed_pair_identity(windows, linux)


if __name__ == "__main__":
    unittest.main()

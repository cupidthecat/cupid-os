"""Current plans carry the codec while installed seed plans retain their identity."""
import copy
import json
from pathlib import Path
import unittest

from tools import bootstrap_toolchain as bootstrap
from tools import cupidc_toolchain_contracts as contracts


ROOT = Path(__file__).resolve().parents[1]


class IsoFixtureBundlePlanTests(unittest.TestCase):
    def setUp(self):
        self.installed = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes())

    def test_upgrade_is_idempotent_and_preserves_installed_manifest_plan(self):
        before = copy.deepcopy(self.installed)
        plan = bootstrap._candidate_build_plan(self.installed["build_plan"])
        self.assertEqual(self.installed, before)
        self.assertEqual(bootstrap._candidate_build_plan(plan), plan)
        row = {"name": "iso_fixture_bundle", "path": "/toolchain/iso_fixture_bundle.cc",
               "gnu_extensions": False}
        self.assertEqual([source for source in plan["sources"] if source["name"] == row["name"]], [row])
        self.assertEqual(plan["links"]["cupidobj"], list(bootstrap.CANDIDATE_CUPIDOBJ_LINK))
        self.assertIn("iso_fixture_bundle", contracts.BOOTSTRAP_OBJECT_NAMES)

    def test_windows_plan_links_the_same_portable_codec_and_captures_its_header(self):
        linux = bootstrap._candidate_build_plan(self.installed["build_plan"])
        windows = bootstrap._windows_build_plan(linux, utf8=True, user_link_aliases=True)
        row = next(source for source in windows["sources"] if source["name"] == "iso_fixture_bundle")
        self.assertEqual(row["definitions"], [])
        self.assertEqual(windows["links"]["cupidobj"].count("iso_fixture_bundle"), 1)
        captured = bootstrap._source_input_paths(ROOT, linux, windows_utf8=True,
                                                 windows_user_link_aliases=True)
        self.assertIn("toolchain/iso_fixture_bundle.cc", captured)
        self.assertIn("toolchain/iso_fixture_bundle.h", captured)

    def test_conflicting_reserved_codec_sources_and_links_are_rejected(self):
        for patch in ({"path": "/toolchain/other.cc"}, {"gnu_extensions": True}):
            plan = copy.deepcopy(self.installed["build_plan"])
            plan["sources"].append({"name": "iso_fixture_bundle", "path": "/toolchain/iso_fixture_bundle.cc",
                                     "gnu_extensions": False, **patch})
            with self.assertRaisesRegex(bootstrap.BootstrapError, "reserved candidate source.*iso_fixture_bundle"):
                bootstrap._candidate_build_plan(plan)
        plan = bootstrap._candidate_build_plan(self.installed["build_plan"])
        plan["links"]["cupidobj"].append("iso_fixture_bundle")
        with self.assertRaisesRegex(bootstrap.BootstrapError, "candidate link differs: cupidobj"):
            bootstrap._candidate_build_plan(plan)

    def test_historical_linux_and_windows_seed_identities_still_verify(self):
        for host in ("linux", "windows"):
            directory = ROOT / "bootstrap/seeds" / ("i386-" + host)
            seed = bootstrap.verify_seed_inputs(directory / "manifest.json")
            bootstrap.require_live_seed_inputs(seed)
            self.assertEqual(tuple(seed.tools), bootstrap.CANDIDATE_TOOL_NAMES)

    def test_guarded_publication_modules_reach_both_build_links(self):
        plan = bootstrap._candidate_build_plan(self.installed['build_plan'])
        self.assertEqual(len(plan['sources']), 34)
        self.assertEqual(plan['links']['cupidbuild'], list(bootstrap.CANDIDATE_CUPIDBUILD_LINK))
        windows = bootstrap._windows_build_plan(plan, utf8=True, long_paths=True, user_link_aliases=True)
        for name in ('iso_fixture_bundle', 'cupidbuild_iso', 'cupidbuild_iso_capture',
                     'cupidbuild_iso_image', 'cupidbuild_iso_publication'):
            self.assertEqual(windows['links']['cupidbuild'].count(name), 1)
            self.assertIn(name, contracts.BOOTSTRAP_OBJECT_NAMES)
        before = copy.deepcopy(plan)
        bootstrap._windows_build_plan(plan, utf8=True, long_paths=True, user_link_aliases=True)
        self.assertEqual(plan, before)
        previous = copy.deepcopy(plan)
        removed = {'cupidbuild_iso', 'cupidbuild_iso_capture', 'cupidbuild_iso_image', 'cupidbuild_iso_publication'}
        previous['sources'] = [r for r in previous['sources'] if r['name'] not in removed]
        previous['links']['cupidbuild'] = list(bootstrap.ISO_BUNDLE_CUPIDBUILD_LINK)
        self.assertEqual(bootstrap._candidate_build_plan(previous), plan)

    def test_guarded_reserved_sources_and_build_link_corruption_fail(self):
        for name in ('cupidbuild_iso', 'cupidbuild_iso_capture', 'cupidbuild_iso_image', 'cupidbuild_iso_publication'):
            plan = copy.deepcopy(self.installed['build_plan'])
            plan['sources'].append({'name': name, 'path': '/toolchain/wrong.cc', 'gnu_extensions': False})
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'reserved candidate source'):
                bootstrap._candidate_build_plan(plan)
        for mutation in ('missing', 'duplicate', 'order'):
            plan = bootstrap._candidate_build_plan(self.installed['build_plan'])
            link = plan['links']['cupidbuild']
            if mutation == 'missing': link.remove('cupidbuild_iso_publication')
            elif mutation == 'duplicate': link.append('cupidbuild_iso_publication')
            else: link.reverse()
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'candidate link differs: cupidbuild'):
                bootstrap._candidate_build_plan(plan)

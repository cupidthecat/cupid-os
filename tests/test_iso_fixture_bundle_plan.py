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

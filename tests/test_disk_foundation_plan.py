"""Bind disk core sources, Unicode data and historical parents into candidate plans."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tools import artifact_size_policy as policy
from tools import bootstrap_toolchain as bootstrap
from tools import toolchain_manifest_contract as sdk
from tools import cupidc_toolchain_contracts as contracts

ROOT = Path(__file__).resolve().parents[1]
MODULES = ('fat16_stage', 'fat16_names', 'disk_image')
TABLE = 'toolchain/fat16_name_profiles.inc'


class DiskFoundationPlanTests(unittest.TestCase):
    def setUp(self):
        self.installed = json.loads((ROOT / 'bootstrap/seeds/i386-linux/manifest.json').read_bytes())
        self.plan = bootstrap._candidate_build_plan(self.installed['build_plan'])

    def test_complete_plan_adds_exact_portable_cores_and_keeps_parent_immutable(self):
        parent = copy.deepcopy(self.installed['build_plan'])
        self.assertEqual(len(self.plan['sources']), 37)
        self.assertEqual(len(self.plan['links']['cupidbuild']), 23)
        self.assertEqual(self.plan['links']['cupidbuild'][-4:], [*MODULES, 'runtime'])
        for name in MODULES:
            self.assertEqual([row for row in self.plan['sources'] if row['name'] == name],
                             [{'name': name, 'path': '/toolchain/' + name + '.cc', 'gnu_extensions': False}])
        self.assertEqual(bootstrap._candidate_build_plan(self.plan), self.plan)
        self.assertEqual(self.installed['build_plan'], parent)
        self.assertEqual(len(parent['sources']), 37)
        self.assertEqual(tuple(parent['links']['cupidbuild']), bootstrap.CANDIDATE_CUPIDBUILD_LINK)
        bootstrap._validate_build_plan(self.installed, promoted=True)

    def test_historical_iso_parent_upgrades_without_mutating_its_exact_plan(self):
        parent = copy.deepcopy(self.plan)
        parent['sources'] = [row for row in parent['sources'] if row['name'] not in MODULES]
        parent['links']['cupidbuild'] = list(bootstrap.ISO_PUBLICATION_CUPIDBUILD_LINK)
        before = copy.deepcopy(parent)
        self.assertEqual(len(parent['sources']), 34)
        self.assertEqual(bootstrap._build_plan_sha256(parent),
                         'ac8edd3ceb4e253439858bbe77c2674933517ec7939bcbe81f1b65ada0d921e3')
        self.assertEqual(bootstrap._candidate_build_plan(parent), self.plan)
        self.assertEqual(parent, before)

    def test_all_five_windows_profiles_match_measured_source_and_plan_closures(self):
        profiles = (
            (False, False, False, 92, 38, 3, '5633a265a4076d8a544621735795dae2baf9b28653c6a5e4b6fa6d8ec37c3dc7'),
            (True, False, False, 97, 42, 3, 'b84cf24ca21c5024c0ccda5bc470a4c6f97f2bb1073b846381dceec6413239ba'),
            (True, True, False, 98, 42, 4, '4e05c478b4628fc71aadc9ab2d2ac564abdc537402f4b713fe6d02c746ed18d5'),
            (True, False, True, 98, 42, 4, '6023235b95ec568a107b163b2e107dc5979d9dc0d8a811f2bf605baa26d79bf5'),
            (True, True, True, 99, 42, 5, '754895566b00e6e53b045a1414e7b734872f04d4f0c84d62e3dfcf8dd9bc57ab'),
        )
        self.assertEqual(bootstrap._build_plan_sha256(self.plan),
                         '808d9a566c3dd200252cb6ca974dfa19992a923867dc60f500797efd5169c73e')
        for utf8, long_paths, aliases, count, sources, assembly, digest in profiles:
            with self.subTest(utf8=utf8, long_paths=long_paths, aliases=aliases):
                plan = bootstrap._windows_build_plan(self.plan, utf8=utf8, long_paths=long_paths,
                                                     user_link_aliases=aliases)
                self.assertEqual((len(plan['sources']), len(plan['assembly_sources'])), (sources, assembly))
                self.assertEqual(bootstrap._build_plan_sha256(plan), digest)
                captured = bootstrap._source_input_paths(ROOT, self.plan, windows_utf8=utf8,
                    windows_long_paths=long_paths, windows_user_link_aliases=aliases)
                self.assertEqual(len(captured), count)
                self.assertEqual(captured[TABLE], ROOT / TABLE)
                for name in MODULES:
                    self.assertIn('toolchain/' + name + '.cc', captured)
                    self.assertIn('toolchain/' + name + '.h', captured)

    def test_sdk_bootstrap_capture_retains_the_same_explicit_unicode_table(self):
        with policy._PinnedRepository(ROOT) as reader:
            self.assertEqual(len(sdk._contract_input_logical_paths(reader)), 104)
            for long_paths, aliases, expected in ((False, False, 97), (True, False, 98),
                                                 (False, True, 98), (True, True, 99)):
                paths = sdk._bootstrap_input_logical_paths(reader, self.plan,
                    windows_long_paths=long_paths, windows_user_link_aliases=aliases)
                self.assertIn(TABLE, paths)
                self.assertEqual(len(paths), expected)

    def test_sdk_compares_the_complete_producer_object_inventory(self):
        self.assertEqual(len(contracts.BOOTSTRAP_OBJECT_NAMES), 38)
        self.assertEqual(contracts.BOOTSTRAP_OBJECT_NAMES[-4:], (*MODULES, 'start'))
        self.assertEqual(set(contracts.BOOTSTRAP_OBJECT_NAMES),
                         {'start', *[row['name'] for row in self.plan['sources']]})
        self.assertEqual(contracts._tool_fixed_point_record()['c_objects'], 37)

    def test_disk_windows_import_profiles_bind_exact_plan_and_count(self):
        for utf8, long_paths, aliases, count in ((False, False, False, 92),
                (True, False, False, 97), (True, True, False, 98),
                (True, False, True, 98), (True, True, True, 99)):
            native = bootstrap._windows_build_plan(self.plan, utf8=utf8,
                long_paths=long_paths, user_link_aliases=aliases)
            digest = bootstrap._build_plan_sha256(native)
            for name in bootstrap.CANDIDATE_TOOL_NAMES:
                expected = bootstrap._windows_utf8_imports(name, long_paths=long_paths,
                    user_link_aliases=aliases) if utf8 else bootstrap._windows_imports(name)
                self.assertEqual(bootstrap._promoted_windows_imports(name, digest, count), expected)
                for bad in (count - 1, count + 1, True, float(count), str(count)):
                    with self.subTest(name=name, count=bad), self.assertRaises(bootstrap.BootstrapError):
                        bootstrap._promoted_windows_imports(name, digest, bad)
                with self.assertRaises(bootstrap.BootstrapError):
                    bootstrap._promoted_windows_imports(name, '0' * 64, count)

    def test_conflicting_reserved_disk_sources_are_rejected(self):
        for name in MODULES:
            for mutation in ({'path': '/toolchain/wrong.cc'}, {'gnu_extensions': True}):
                with self.subTest(name=name, mutation=mutation):
                    plan = copy.deepcopy(self.plan)
                    next(row for row in plan['sources'] if row['name'] == name).update(mutation)
                    with self.assertRaisesRegex(bootstrap.BootstrapError, 'reserved candidate source.*' + name):
                        bootstrap._candidate_build_plan(plan)

    def test_missing_duplicated_and_reordered_disk_links_are_rejected(self):
        for mutation in ('missing', 'duplicate', 'order'):
            with self.subTest(mutation=mutation):
                plan = copy.deepcopy(self.plan)
                link = plan['links']['cupidbuild']
                if mutation == 'missing':
                    link.remove('fat16_names')
                elif mutation == 'duplicate':
                    link.insert(-1, 'fat16_names')
                else:
                    link[-4], link[-3] = link[-3], link[-4]
                with self.assertRaisesRegex(bootstrap.BootstrapError, 'candidate link differs: cupidbuild'):
                    bootstrap._candidate_build_plan(plan)

    def test_missing_and_nonfile_unicode_tables_fail_source_capture(self):
        captured = bootstrap._source_input_paths(ROOT, self.plan, windows_utf8=True,
            windows_long_paths=True, windows_user_link_aliases=True)
        with tempfile.TemporaryDirectory(prefix='cupid-disk-plan-') as temporary:
            root = Path(temporary)
            for name, path in captured.items():
                if name == TABLE:
                    continue
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'cannot resolve source input'):
                bootstrap._source_input_paths(root, self.plan, windows_utf8=True,
                    windows_long_paths=True, windows_user_link_aliases=True)
            (root / TABLE).mkdir()
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'source input is not a file'):
                bootstrap._source_input_paths(root, self.plan, windows_utf8=True,
                    windows_long_paths=True, windows_user_link_aliases=True)

    def test_installed_paired_parent_cohort_still_verifies(self):
        for host in ('linux', 'windows'):
            checked = bootstrap.verify_seed_inputs(ROOT / 'bootstrap/seeds' / ('i386-' + host) / 'manifest.json')
            self.assertEqual(checked.manifest['provenance']['source_input_count'], 99)
            bootstrap.require_live_seed_inputs(checked)

"""Carry the user-link directory resolver in an exact Windows profile."""

from dataclasses import replace
import copy
import json
from pathlib import Path
import tempfile
import unittest

from tools import bootstrap_toolchain as bootstrap

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = 'toolchain/hosted/i386-windows/final_path_start.asm'
API = 'GetFinalPathNameByHandleW'


class WindowsUserLinkProfileTests(unittest.TestCase):
    def setUp(self):
        self.linux = bootstrap._candidate_build_plan(json.loads(
            (ROOT / 'bootstrap/seeds/i386-linux/manifest.json').read_bytes())['build_plan'])

    def plan(self, long_paths=False):
        return bootstrap._windows_build_plan(self.linux, utf8=True,
            long_paths=long_paths, user_link_aliases=True)

    def test_selected_plans_bind_one_bridge_and_one_exact_role_import(self):
        for long_paths, historical in ((False,
                'b84cf24ca21c5024c0ccda5bc470a4c6f97f2bb1073b846381dceec6413239ba'),
                (True, '4e05c478b4628fc71aadc9ab2d2ac564abdc537402f4b713fe6d02c746ed18d5')):
            with self.subTest(long_paths=long_paths):
                old = bootstrap._windows_build_plan(self.linux, utf8=True, long_paths=long_paths)
                self.assertEqual(bootstrap._build_plan_sha256(old), historical)
                self.assertEqual(bootstrap._windows_plan_profile(old), (True, long_paths, False))
                new = self.plan(long_paths)
                self.assertNotEqual(bootstrap._build_plan_sha256(new), historical)
                self.assertEqual(bootstrap._windows_plan_profile(new), (True, long_paths, True))
                self.assertEqual([row for row in new['assembly_sources']
                    if row['name'] == 'final_path_start'],
                    [{'name': 'final_path_start', 'path': '/' + BRIDGE}])
                for name in bootstrap.CANDIDATE_TOOL_NAMES:
                    old_imports = old['imports'][name]
                    imports = new['imports'][name]
                    added = [row for row in imports if row not in old_imports]
                    self.assertEqual(added, [{'library': 'KERNEL32.dll', 'procedure': API,
                        'slot': '__imp_' + API}] if name == 'cupidbuild' else [])
                    self.assertEqual(new['links'][name].count('final_path_start'),
                                     int(name == 'cupidbuild'))

    def test_invalid_selection_fails_before_source_freeze_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / 'frozen'
            for value in (None, 0, 1, 'yes', [], {}):
                with self.subTest(value=value):
                    with self.assertRaisesRegex(bootstrap.BootstrapError, 'Boolean'):
                        bootstrap._windows_build_plan(self.linux, utf8=True,
                                                       user_link_aliases=value)
                    with self.assertRaisesRegex(bootstrap.BootstrapError, 'Boolean'):
                        bootstrap._windows_utf8_imports('cupidbuild', user_link_aliases=value)
                    with self.assertRaisesRegex(bootstrap.BootstrapError, 'Boolean'):
                        bootstrap.freeze_source_inputs(ROOT, self.linux, destination,
                            windows_utf8=True, windows_user_link_aliases=value)
                    self.assertFalse(destination.exists())
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'UTF-8'):
                bootstrap._windows_build_plan(self.linux, user_link_aliases=True)
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'UTF-8'):
                bootstrap.freeze_source_inputs(ROOT, self.linux, destination,
                                                windows_user_link_aliases=True)
            self.assertFalse(destination.exists())

    def test_link_arguments_select_only_the_complete_requested_imports(self):
        plan = self.plan(True)
        objects = {name: Path(name + '.o') for name in plan['links']['cupidbuild']}
        arguments = bootstrap._windows_link_arguments('cupidbuild', Path('out.exe'),
            objects, plan['links']['cupidbuild'], utf8=True, long_paths=True,
            user_link_aliases=True)
        self.assertEqual(arguments.count('__imp_' + API + '=KERNEL32.dll:' + API), 1)
        self.assertEqual(arguments.count(objects['final_path_start']), 1)
        for name in bootstrap.CANDIDATE_TOOL_NAMES:
            selectors = bootstrap._windows_import_selectors(name, utf8=True,
                long_paths=True, user_link_aliases=True)
            self.assertEqual(sum(API in row for row in selectors), int(name == 'cupidbuild'))

    def test_source_inventory_captures_the_selected_bridge_and_exact_count(self):
        for long_paths, count in ((False, 98), (True, 99)):
            with self.subTest(long_paths=long_paths):
                old = bootstrap.capture_source_snapshot(ROOT, self.linux,
                    windows_utf8=True, windows_long_paths=long_paths)
                current = bootstrap.capture_source_snapshot(ROOT, self.linux,
                    windows_utf8=True, windows_long_paths=long_paths,
                    windows_user_link_aliases=True)
                self.assertEqual(set(current) - set(old), {BRIDGE})
                self.assertEqual(len(current), count)
                for row in self.plan(long_paths)['sources'] + self.plan(long_paths)['assembly_sources']:
                    self.assertIn(row['path'].lstrip('/'), current)
                bootstrap.require_source_snapshot(ROOT, self.linux, current,
                    windows_utf8=True, windows_long_paths=long_paths,
                    windows_user_link_aliases=True)

    def test_frozen_and_live_bridge_mutations_fail_and_recovery_revalidates(self):
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            live = bootstrap.freeze_source_inputs(ROOT, self.linux, top / 'live',
                windows_utf8=True, windows_long_paths=True, windows_user_link_aliases=True)
            frozen = bootstrap.freeze_source_inputs(live.root, self.linux, top / 'frozen',
                windows_utf8=True, windows_long_paths=True, windows_user_link_aliases=True)
            self.assertTrue(frozen.windows_user_link_aliases)
            bootstrap.require_source_closures(frozen, live.root, self.linux)
            for directory, label in ((frozen.root, 'frozen source inputs'), (live.root, 'source inputs')):
                path = directory / BRIDGE
                saved = path.read_bytes()
                path.write_bytes(saved + b'\n; changed alias bridge\n')
                with self.assertRaisesRegex(bootstrap.BootstrapError, label + ' changed'):
                    bootstrap.require_source_closures(frozen, live.root, self.linux)
                path.write_bytes(saved)
                bootstrap.require_source_closures(frozen, live.root, self.linux)
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'frozen source inputs changed'):
                bootstrap.require_frozen_source_snapshot(
                    replace(frozen, windows_user_link_aliases=False), self.linux)

    def test_missing_bridge_rejects_capture_before_namespace_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            top = Path(temporary)
            live = bootstrap.freeze_source_inputs(ROOT, self.linux, top / 'live',
                windows_utf8=True, windows_user_link_aliases=True)
            (live.root / BRIDGE).unlink()
            with self.assertRaisesRegex(bootstrap.BootstrapError, 'cannot resolve source input'):
                bootstrap.freeze_source_inputs(live.root, self.linux, top / 'missing',
                    windows_utf8=True, windows_user_link_aliases=True)
            self.assertFalse((top / 'missing').exists())

    def test_mixed_bridge_import_and_role_bindings_are_rejected(self):
        original = self.plan(True)
        mutations = []
        missing = copy.deepcopy(original)
        missing['assembly_sources'] = [row for row in missing['assembly_sources']
                                      if row['name'] != 'final_path_start']
        mutations.append(missing)
        repeated = copy.deepcopy(original)
        repeated['links']['cupidbuild'].append('final_path_start')
        mutations.append(repeated)
        foreign = copy.deepcopy(original)
        foreign['links']['cupidc'].append('final_path_start')
        mutations.append(foreign)
        imports = copy.deepcopy(original)
        imports['imports']['cupidc'].append({'library': 'KERNEL32.dll', 'procedure': API,
                                           'slot': '__imp_' + API})
        mutations.append(imports)
        for mutated in mutations:
            with self.subTest(mutated=mutated), self.assertRaises(bootstrap.BootstrapError):
                bootstrap._windows_plan_profile(mutated)

    def test_promoted_profiles_pin_digest_count_and_exact_imports(self):
        for long_paths, count, digest in ((False, 98,
                '6023235b95ec568a107b163b2e107dc5979d9dc0d8a811f2bf605baa26d79bf5'),
                (True, 99, '754895566b00e6e53b045a1414e7b734872f04d4f0c84d62e3dfcf8dd9bc57ab')):
            self.assertEqual(bootstrap._build_plan_sha256(self.plan(long_paths)), digest)
            for name in bootstrap.CANDIDATE_TOOL_NAMES:
                expected = bootstrap._windows_utf8_imports(name, long_paths=long_paths,
                                                           user_link_aliases=True)
                self.assertEqual(bootstrap._promoted_windows_imports(name, digest, count), expected)
                for wrong in (count - 1, count + 1, True, float(count), str(count)):
                    with self.subTest(name=name, wrong=wrong), self.assertRaisesRegex(
                            bootstrap.BootstrapError, 'import profile differs'):
                        bootstrap._promoted_windows_imports(name, digest, wrong)

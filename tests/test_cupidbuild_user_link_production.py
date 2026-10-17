"""Check the real Make handoff and its publication input boundaries."""
import copy
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock

from tools import build_graph_audit as audit

ROOT = Path(__file__).resolve().parents[1]
PROGRAMS = ('cat', 'hello', 'ls')


class CupidBuildUserLinkProductionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.user = audit._collect_build_model(ROOT, 'make', 'all', 'user')
        cls.seed_inputs = audit._validate_cupidbuild_user_make_binding(ROOT, 'make')
        cls.links = [item for item in cls.user.transforms
                     if item['output'] in ('user/build/' + name for name in PROGRAMS)]

    def test_all_three_default_links_have_native_ownership(self):
        self.assertEqual(len(self.links), 3)
        audit._validate_cupidbuild_user_link_delivery(
            self.user.transforms, seed_inputs=self.seed_inputs)
        for link in self.links:
            self.assertNotIn('host_python', link['tools'])
            self.assertIn('bootstrap/seeds/release.json', link['inputs'])

    def test_link_delivery_rejects_missing_or_extra_publication_inputs(self):
        for link in self.links:
            others = [row for row in self.links if row is not link]
            for removed in link['inputs']:
                with self.subTest(output=link['output'], removed=removed):
                    changed = copy.deepcopy(link)
                    changed['inputs'].remove(removed)
                    with self.assertRaises(audit.AuditError):
                        audit._validate_cupidbuild_user_link_delivery(
                            [*others, changed], seed_inputs=self.seed_inputs)
            for extra in ('tools/cupidld_user_link.py', link['inputs'][0]):
                with self.subTest(output=link['output'], extra=extra):
                    changed = copy.deepcopy(link)
                    changed['inputs'].append(extra)
                    with self.assertRaises(audit.AuditError):
                        audit._validate_cupidbuild_user_link_delivery(
                            [*others, changed], seed_inputs=self.seed_inputs)

    def test_link_delivery_rejects_recipe_owner_and_scheduling_drift(self):
        link = self.links[0]
        others = self.links[1:]
        for change in (
            {'tools': ['cupid_builder', 'cupid_linker', 'host_python']},
            {'operation': 'transform_object'},
            {'order_only_inputs': ['bootstrap/seeds/release.json']},
            {'recipe': [line for line in link['recipe'] if '--seed-release' not in line]},
            {'recipe': [line.replace('$(CUPIDBUILD_USER_SEED_RELEASE)', 'unchecked.json')
                        for line in link['recipe']]},
            {'recipe': [*link['recipe'], 'echo unchecked']},
        ):
            with self.subTest(change=change), self.assertRaises(audit.AuditError):
                audit._validate_cupidbuild_user_link_delivery(
                    [*others, dict(link, **change)], seed_inputs=self.seed_inputs)
        for rows in (self.links[:-1], [*self.links, link],
                     [*self.links, dict(link, output='user/build/unapproved')]):
            with self.subTest(outputs=[row['output'] for row in rows]):
                with self.assertRaises(audit.AuditError):
                    audit._validate_cupidbuild_user_link_delivery(rows, seed_inputs=self.seed_inputs)

    def test_both_make_host_branches_keep_custom_output_paths_and_native_links(self):
        poison = '__forbidden_user_link__'
        for host, suffix, platform in (('Windows_NT', 'exe', 'windows'), ('Linux', 'elf', 'linux')):
            for build in ('build', 'custom/deep', 'existing/../custom', 'caf\u00e9/deep'):
                with self.subTest(host=host, build=build):
                    command = ['make', '-C', 'user', '-B', '-n', 'OS=' + host, 'BUILD=' + build]
                    command.extend(name + '=' + poison for name in (
                        'PYTHON', 'CUPIDLD_USER_LINK', 'CUPIDLD_USER_LINK_INPUTS',
                        'CHECKED_SEED_INPUTS', 'PRODUCTION_SEED_DIRECTORY',
                        'PRODUCTION_SEED_SUFFIX', 'CUPIDBUILD_USER_SEED_MANIFEST',
                        'CUPIDBUILD_USER_SEED_RELEASE', 'CC', 'CXX', 'CPP', 'HOSTCC',
                        'HOSTCXX', 'ASM', 'AS', 'LD', 'AR', 'NM', 'OBJCOPY'))
                    command.extend(build + '/' + name for name in PROGRAMS)
                    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                        encoding='mbcs' if os.name == 'nt' else 'utf-8', timeout=60)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    rows = [line for line in result.stdout.replace('\\\n', ' ').splitlines()
                            if ' link-user ' in line]
                    self.assertEqual(len(rows), 3, result.stdout)
                    for name in PROGRAMS:
                        row = next(line for line in rows if '--source user/' + build + '/' + name + '.o' in line)
                        self.assertNotIn(poison, row)
                        self.assertIn('cupidbuild.' + suffix + ' link-user', row)
                        self.assertIn('--seed-manifest bootstrap/seeds/i386-' + platform + '/manifest.json', row)
                        self.assertIn('--seed-release bootstrap/seeds/release.json', row)
                        self.assertIn('--output user/' + build + '/' + name, row)

    def test_numeric_address_spellings_keep_the_existing_wrapper(self):
        for spelling in ('0x1c00000', '29360128', '0x1000'):
            with self.subTest(spelling=spelling):
                result = subprocess.run(['make', '-C', 'user', '-B', '-n',
                    'USER_TEXT_ADDRESS=' + spelling, 'build/hello'], cwd=ROOT,
                    capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(' link-user ', result.stdout)
                self.assertIn('tools/cupidld_user_link.py', result.stdout)
                self.assertIn('--text-address ' + spelling + ' --entry _start',
                              result.stdout.replace('\\\n', ' '))

    def test_release_is_distinct_from_the_manifest_in_both_make_bindings(self):
        for directory, names, validate, manifest_name in (
            (ROOT, ('PRODUCTION_SEED_MANIFEST', 'PRODUCTION_SEED_RELEASE',
                    'PRODUCTION_SEED_DIRECTORY', 'PRODUCTION_SEED_SUFFIX',
                    'PRODUCTION_SEED_INPUTS', 'CUPIDOBJ', 'CUPIDLD'),
             audit._validate_cupidbuild_root_seed_make_binding, 'PRODUCTION_SEED_INPUTS'),
            (ROOT / 'user', ('PRODUCTION_SEED_MANIFEST', 'PRODUCTION_SEED_RELEASE',
                    'PRODUCTION_SEED_DIRECTORY', 'PRODUCTION_SEED_SUFFIX',
                    'CHECKED_SEED_INPUTS', 'CUPIDBUILD_USER_COMPILE_INPUTS',
                    'CUPIDBUILD_USER_SEED_MANIFEST', 'CUPIDBUILD_USER_SEED_RELEASE'),
             audit._validate_cupidbuild_user_make_binding, 'CHECKED_SEED_INPUTS'),
        ):
            values = audit._read_evaluated_make_variables(directory, 'make', names)
            changed = dict(values, PRODUCTION_SEED_RELEASE=values['PRODUCTION_SEED_MANIFEST'])
            with self.subTest(directory=directory), mock.patch.object(
                    audit, '_read_evaluated_make_variables', return_value=changed):
                with self.assertRaises(audit.AuditError):
                    validate(ROOT, 'make')
            self.assertEqual(len(values[manifest_name].split()), 8)


class CupidBuildRootReleaseProductionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = audit._collect_build_model(ROOT, 'make', 'all', '.')
        cls.seed_inputs = audit._validate_cupidbuild_root_seed_make_binding(ROOT, 'make')

    def test_real_root_operations_capture_the_selected_release(self):
        audit._validate_cupidbuild_root_release_context(
            self.model.transforms, seed_inputs=self.seed_inputs)
        self.assertIn('bootstrap/seeds/release.json', self.seed_inputs)

    def test_root_rejects_release_options_after_child_separator(self):
        row = next(item for item in self.model.transforms
                   if any(' compile-kernel ' in line for line in item['recipe']))
        changed = copy.deepcopy(row)
        changed['recipe'] = [line.replace('--seed-release', '-- --seed-release')
                             for line in changed['recipe']]
        with self.assertRaises(audit.AuditError):
            audit._validate_cupidbuild_root_release_context([changed], seed_inputs=self.seed_inputs)

    def test_root_rejects_release_edges_moved_to_order_only_or_duplicated(self):
        row = next(item for item in self.model.transforms
                   if any(' compile-kernel ' in line for line in item['recipe']))
        for duplicate in (False, True):
            changed = copy.deepcopy(row)
            if duplicate:
                changed['inputs'].append('bootstrap/seeds/release.json')
            else:
                changed['inputs'].remove('bootstrap/seeds/release.json')
                changed['order_only_inputs'].append('bootstrap/seeds/release.json')
            with self.subTest(duplicate=duplicate), self.assertRaises(audit.AuditError):
                audit._validate_cupidbuild_root_release_context([changed], seed_inputs=self.seed_inputs)


if __name__ == '__main__':
    unittest.main()

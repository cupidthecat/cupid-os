import copy
import os
from pathlib import Path
import subprocess
import unittest
from unittest import mock

from tools import build_graph_audit as audit

ROOT = Path(__file__).resolve().parents[1]


class CupidBuildUserProductionTests(unittest.TestCase):
    def test_three_real_deliveries_and_mutations(self):
        model = audit._collect_build_model(ROOT, 'make', 'all', 'user')
        seed_inputs = audit._validate_cupidbuild_user_make_binding(ROOT, 'make')
        objects = [item for item in model.transforms
                   if item['output'] in ('user/build/cat.o', 'user/build/hello.o', 'user/build/ls.o')]
        self.assertEqual(len(objects), 3)
        audit._validate_cupidbuild_user_compile_delivery(model.transforms, seed_inputs=seed_inputs)
        for item in objects:
            self.assertEqual(audit._c_preprocessor_profile_for_c_transform('user', item), 'USER_I386')
            def check(value):
                audit._validate_cupidbuild_user_compile_delivery([value], seed_inputs=seed_inputs)
            for removed in item['inputs']:
                with self.subTest(output=item['output'], removed=removed):
                    changed = copy.deepcopy(item)
                    changed['inputs'].remove(removed)
                    with self.assertRaises(audit.AuditError):
                        check(changed)
            for change in (
                {'tools': ['cupid_c_compiler', 'host_python']},
                {'operation': 'compile_c_to_host_object'},
                {'inputs': item['inputs'] + [item['inputs'][0]]},
                {'inputs': item['inputs'] + ['unapproved.h']},
                {'order_only_inputs': []},
                {'order_only_inputs': ['user/cupid.h']},
                {'recipe': item['recipe'] + ['echo unchecked']},
                {'recipe': item['recipe'][:-1] + ['--source user/examples/other.cc --output user/build/other.o']},
                {'recipe': item['recipe'][:-1] + [item['recipe'][-1] + ' --timeout 1']},
                {'recipe': item['recipe'][:-1] + [item['recipe'][-1] + ' -DDEBUG=0']},
            ):
                with self.subTest(output=item['output'], change=change), self.assertRaises(audit.AuditError):
                    check(dict(item, **change))
            with self.assertRaises(audit.AuditError):
                audit._validate_cupidbuild_user_compile_delivery([item, item], seed_inputs=seed_inputs)
            for directory in ('.', 'toolchain'):
                with self.assertRaises(audit.AuditError):
                    audit._c_preprocessor_profile_for_c_transform(directory, item)

    def test_closed_compile_recipes_for_both_hosts_and_custom_paths(self):
        poison = '__forbidden_user_compile__'
        for host, suffix in (('Windows_NT', 'exe'), ('Linux', 'elf')):
            for build in ('build', 'custom/deep', 'existing/../custom', 'caf\u00e9/deep'):
                with self.subTest(host=host, build=build):
                    result = subprocess.run(['make', '-C', 'user', '-B', '-n', f'OS={host}',
                        f'BUILD={build}', *[f'{name}={poison}' for name in (
                            'PYTHON', 'CUPIDC_PRODUCTION_COMPILE', 'CUPIDC_PRODUCTION_COMPILE_INPUTS',
                            'CHECKED_SEED_RUN', 'CHECKED_SEED_INPUTS', 'CUPIDBUILD_USER_COMPILE_INPUTS',
                            'CC', 'CXX', 'CPP', 'HOSTCC', 'HOSTCXX', 'ASM', 'AS', 'LD', 'AR', 'NM',
                            'OBJCOPY', 'CFLAGS', 'EXTRA_CFLAGS', 'PRODUCTION_SEED_DIRECTORY',
                            'PRODUCTION_SEED_SUFFIX', 'CUPIDBUILD_USER_SEED_MANIFEST',
                            'CUPIDBUILD_USER_SEED_RELEASE')], *[build + '/' + name + '.o'
                            for name in ('hello', 'ls', 'cat')]], cwd=ROOT,
                            capture_output=True, text=True,
                            encoding='mbcs' if os.name == 'nt' else 'utf-8', timeout=60)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    commands = [line for line in result.stdout.replace('\\\n', ' ').splitlines()
                                if ' compile-user ' in line]
                    self.assertEqual(len(commands), 3)
                    for name in ('hello', 'ls', 'cat'):
                        command = next(line for line in commands if '--source user/examples/' + name + '.cc' in line)
                        self.assertNotIn(poison, command)
                        self.assertIn('cupidbuild.' + suffix + ' compile-user', command)
                        self.assertIn('--seed-manifest bootstrap/seeds/i386-' +
                                      ('windows' if host == 'Windows_NT' else 'linux') + '/manifest.json', command)
                        self.assertIn('--seed-release bootstrap/seeds/release.json', command)
                        self.assertIn('--output user/' + build + '/' + name + '.o', command)

    def test_make_binding_rejects_cohort_drift(self):
        names = ('PRODUCTION_SEED_MANIFEST', 'PRODUCTION_SEED_RELEASE', 'PRODUCTION_SEED_DIRECTORY',
                 'PRODUCTION_SEED_SUFFIX', 'CHECKED_SEED_INPUTS', 'CUPIDBUILD_USER_COMPILE_INPUTS',
                 'CUPIDBUILD_USER_SEED_MANIFEST', 'CUPIDBUILD_USER_SEED_RELEASE')
        values = audit._read_evaluated_make_variables(ROOT / 'user', 'make', names)
        for name in names[1:]:
            changed = dict(values, **{name: values[name] + ' unapproved'})
            with self.subTest(name=name), mock.patch.object(audit, '_read_evaluated_make_variables', return_value=changed):
                with self.assertRaises(audit.AuditError):
                    audit._validate_cupidbuild_user_make_binding(ROOT, 'make')


if __name__ == '__main__':
    unittest.main()

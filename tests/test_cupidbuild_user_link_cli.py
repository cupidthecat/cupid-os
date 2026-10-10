"""Exercise link-user through the actual command parser and retained alias boundary."""
import os
from pathlib import Path
import subprocess
from tests import test_cupidbuild_user_link as physical
from tests import test_cupidbuild_user_link_alias_operation as alias
from tools.cupidld_user_link import validate_user_executable_bytes

_NATIVE_PREFIX = physical.CALLER.split('  cupidbuild_user_link_request_t request;')[0]
_NATIVE_CALLER = '#define main cupidbuild_cli_entry\n#include "cupidbuild_main.cc"\n#undef main\n' + _NATIVE_PREFIX + r'''
#if defined(NATIVE_USER_LINK_WINDOWS)
  result = cupidbuild_cli_entry(argc, argv);
  for (index = 0; index < argc; index++) free(argv[index]);
  free(argv);
  return result;
#else
  return cupidbuild_cli_entry(argc, argv);
#endif
}
'''

class CupidBuildUserLinkCliTests(alias.CupidBuildUserLinkAliasOperationTests):
    native_modules_extra = ('artifact_size_policy', 'cupidbuild_artifacts',
                            'user_syscall_abi', 'cupidbuild_user_abi')
    caller_source = ('#include "cupidbuild_main.cc"\n'
                    if os.environ.get('CUPIDBUILD_USER_LINK_CHECKED') == '1' else _NATIVE_CALLER)

    def command(self, source='user/build/hello.o', output='user/build/hello', root=None):
        return list(map(str, [self.program, 'link-user', '--root', root or self.root,
            '--seed-manifest', 'seed/manifest.json', '--source', source, '--output', output]))

    def run_link(self, source='user/build/hello.o', output='user/build/hello', env=None):
        return subprocess.run(self.command(source, output), cwd=physical.ROOT, env=env,
                              capture_output=True, text=True, timeout=180)

    def test_relative_repository_root_is_rejected_before_publication(self):
        result = subprocess.run(self.command(root=self.root.name), cwd=self.root.parent,
                                capture_output=True, text=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stderr)
        validate_user_executable_bytes(self.output.read_bytes())
        self.assert_clean()

    def test_repository_and_ancestor_aliases_keep_the_same_publication(self):
        alias = self.root.parent / (self.root.name + '-alias')
        self.alias(alias, self.root)
        self.addCleanup(alias.rmdir if os.name == 'nt' else alias.unlink)
        for root in (alias, alias.parent / '..' / alias.parent.name / alias.name):
            result = subprocess.run(self.command(root=root), capture_output=True,
                                    text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stderr)
            validate_user_executable_bytes(self.output.read_bytes())
        self.assert_clean()

    def test_cli_help_describes_the_object_input(self):
        result = subprocess.run([str(self.program), '--help'], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('cupidbuild link-user --seed-manifest MANIFEST --root ROOT --source OBJECT --output OUTPUT', result.stdout)
        self.assertEqual(result.stderr, '')
        self.assertFalse(self.output.exists())
        self.assert_clean()

    def test_malformed_options_preserve_output_and_create_no_directories(self):
        before = self.previous()
        valid = self.command()
        variants = [valid + ['--unknown'], valid + ['--root', str(self.root)],
                    valid + ['--source', 'user/build/hello.o'],
                    valid + ['--output', 'user/missing/hello'],
                    valid + ['--seed-manifest', 'seed/manifest.json']]
        for flag in ('--root', '--source', '--output', '--seed-manifest'):
            index = valid.index(flag)
            variants += [valid[:index] + valid[index+2:], valid[:index+1]]
            empty = list(valid);empty[index+1] = '';variants.append(empty)
        for arguments in variants:
            with self.subTest(arguments=arguments):
                result = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn('usage: cupidbuild', result.stderr)
                self.assert_preserved(before)
                self.assertFalse((self.root/'user/missing').exists())

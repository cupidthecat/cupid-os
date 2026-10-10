"""Keep the resolved physical chain through guarded user-link publication."""

import os
from pathlib import Path
import subprocess

from tests import test_cupidbuild_user_link as physical
from tools.cupidld_user_link import validate_user_executable_bytes


class CupidBuildUserLinkAliasOperationTests(physical.CupidBuildUserLinkTests):
    caller_source = physical.CALLER.replace('cupidbuild_link_user_object(',
                                           'cupidbuild_link_user(')

    def alias(self, path, destination):
        if os.name == 'nt':
            result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(path), str(destination)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            path.symlink_to(destination, target_is_directory=True)

    def test_relative_repository_root_is_rejected_before_publication(self):
        result = subprocess.run(list(map(str, [self.program, self.root.name,
            'seed/manifest.json', 'user/build/hello.o', 'user/build/hello'])),
            cwd=self.root.parent, capture_output=True, text=True, timeout=150)
        self.assertEqual(result.returncode, 0, result.stderr)
        validate_user_executable_bytes(self.output.read_bytes())
        self.assert_clean()

    def test_invalid_pair_and_missing_parent_do_not_create_directories(self):
        before = self.previous()
        for source, output in (('user/build/hello.o', 'user/hello'),
                ('user/build/hello.o', 'user/build/ls'),
                ('user/build/hello.o', 'user/build/hello.o'),
                ('user/missing/hello.o', 'user/missing/hello'),
                ('other/build/hello.o', 'other/build/hello'),
                ('user/build/unknown.o', 'user/build/unknown')):
            with self.subTest(source=source, output=output):
                result = self.run_link(source, output)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
                self.assert_preserved(before)
                self.assertFalse((self.root / 'user/missing').exists())

    def test_linked_object_output_and_physical_parent_are_rejected(self):
        saved = self.parent / 'saved.o'
        self.object.rename(saved)
        self.object.symlink_to(saved)
        before = self.previous()
        self.assertNotEqual(self.run_link().returncode, 0)
        self.assert_preserved(before)
        self.object.unlink()
        saved.rename(self.object)
        foreign = self.root / 'foreign'
        foreign.write_bytes(b'foreign output')
        self.output.unlink()
        self.output.symlink_to(foreign)
        self.assertNotEqual(self.run_link().returncode, 0)
        self.assertEqual(foreign.read_bytes(), b'foreign output')
        self.output.unlink()
        self.assert_clean()

    def test_independent_internal_aliases_publish_the_physical_executable(self):
        self.alias(self.root / 'user/input-alias', self.parent)
        self.alias(self.root / 'user/output-alias', self.parent)
        result = self.run_link('user/input-alias/hello.o', 'user/output-alias/hello')
        self.assertEqual(result.returncode, 0, result.stderr)
        validate_user_executable_bytes(self.output.read_bytes())
        timestamp = self.output.stat().st_mtime_ns
        result = self.run_link('user/input-alias/hello.o', 'user/output-alias/hello')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)
        self.assert_clean()

    def test_repository_and_ancestor_aliases_keep_the_same_publication(self):
        alias = self.root.parent / (self.root.name + '-alias')
        self.alias(alias, self.root)
        self.addCleanup(alias.rmdir if os.name == 'nt' else alias.unlink)
        for root in (alias, alias.parent / '..' / alias.parent.name / alias.name):
            result = subprocess.run(list(map(str, [self.program, root, 'seed/manifest.json',
                'user/build/hello.o', 'user/build/hello'])), capture_output=True,
                text=True, timeout=150)
            self.assertEqual(result.returncode, 0, result.stderr)
            validate_user_executable_bytes(self.output.read_bytes())
        self.assert_clean()

    def test_absolute_and_lexical_leaf_paths_keep_the_approved_pair(self):
        for source, output in ((self.object, self.output),
                ('user/build/../build/hello.o', 'user/./build/hello'),
                ('user//build/hello.o', 'user//build/hello')):
            result = self.run_link(source, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            validate_user_executable_bytes(self.output.read_bytes())
        self.assert_clean()

    def test_external_aliases_and_distinct_physical_parents_preserve_output(self):
        other = self.root / 'other'
        other.mkdir()
        (other / 'hello.o').write_bytes(self.objects['hello'])
        self.alias(self.root / 'user/escape', other)
        before = self.previous()
        for source, output in (('user/escape/hello.o', 'user/escape/hello'),
                ('user/build/hello.o', 'user/escape/hello')):
            result = self.run_link(source, output)
            self.assertNotEqual(result.returncode, 0)
            self.assert_preserved(before)
            self.assertFalse((other / 'hello').exists())

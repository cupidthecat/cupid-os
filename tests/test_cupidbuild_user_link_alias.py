"""Resolve user-link filesystem aliases before retaining the physical chain."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tests.test_cupidbuild_compile_kernel import ROOT

CALLER = r'''
#include "cupidbuild_host.h"
#include "path_encoding.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(NATIVE_ALIAS_WINDOWS)
#include <wchar.h>
#endif
static int invoke(int argc, char **argv) {
  cupidbuild_host_output_parent_t *parent = (cupidbuild_host_output_parent_t *)0;
  char resume = 0;
  int result;
  if (argc != 4 && argc != 5) return 2;
  result = cupidbuild_host_output_parent_resolve_existing(argv[1], argv[2], argv[3], &parent);
  if (result) {
    printf("%s\n%s\n%s\n", cupidbuild_host_output_parent_resolved_root(parent),
        cupidbuild_host_output_parent_resolved_source(parent),
        cupidbuild_host_output_parent_resolved_output(parent));
    if (argc == 5) {
      puts("READY");
      fflush(stdout);
      if (fread(&resume, 1u, 1u, stdin) != 1u || resume != 'R') result = 0;
    }
  }
  if (result) {
    result = cupidbuild_host_output_parent_require_current(parent);
    if (!result && cupidbuild_host_output_parent_require_current(parent)) return 125;
  }
  if (!result) fprintf(stderr, "%s\n", cupidbuild_host_output_parent_error(parent));
  if (!cupidbuild_host_output_parent_close(parent)) return 1;
  return result ? 0 : 1;
}
#if defined(NATIVE_ALIAS_WINDOWS)
int wmain(int argc, wchar_t **wide) {
  char **argv = (char **)calloc((size_t)argc + 1u, sizeof(char *));
  int index, result;
  if (argv == (char **)0) return 126;
  for (index = 0; index < argc; index++) {
    size_t bytes = 0u;
    if (!cupidbuild_path_to_utf8((const unsigned short *)wide[index], wcslen(wide[index]),
                                (char *)0, 0u, &bytes)) return 126;
    argv[index] = (char *)malloc(bytes + 1u);
    if (!cupidbuild_path_to_utf8((const unsigned short *)wide[index], wcslen(wide[index]),
                                argv[index], bytes + 1u, &bytes)) return 126;
  }
  result = invoke(argc, argv);
  for (index = 0; index < argc; index++) free(argv[index]);
  free(argv);
  return result;
}
#else
int main(int argc, char **argv) { return invoke(argc, argv); }
#endif
'''


class CupidBuildUserLinkAliasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get('CUPIDBUILD_ALIAS_PROGRAM')
        if configured:
            cls.program = Path(configured).resolve(strict=True)
            return
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-alias-build-')
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        if os.environ.get('CUPIDBUILD_ALIAS_CHECKED') == '1':
            cls.checked_build = tempfile.TemporaryDirectory(prefix='.alias-checked-',
                                                            dir=ROOT / 'toolchain')
            cls.addClassCleanup(cls.checked_build.cleanup)
            directory = Path(cls.checked_build.name)
        source = directory / 'caller.cc'
        source.write_text(CALLER, encoding='ascii')
        cls.program = directory / ('caller.exe' if os.name == 'nt' else 'caller')
        if os.environ.get('CUPIDBUILD_ALIAS_CHECKED') == '1':
            from tests.test_cupidbuild_user_link import CupidBuildUserLinkTests
            CupidBuildUserLinkTests.build_checked_tool(directory, 'cupidbuild',
                cls.program, 'alias', source)
            return
        command = [shutil.which('clang') or 'clang', '-std=c11', '-O2', '-Wall',
                   '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS',
                   '-I', ROOT / 'toolchain', '-x', 'c', source,
                   ROOT / 'toolchain/cupidbuild_host.cc', ROOT / 'toolchain/path_encoding.cc']
        if os.name == 'nt':
            command += ['-DNATIVE_ALIAS_WINDOWS', '-DCUPID_NATIVE_UTF8_ENABLE',
                        ROOT / 'toolchain/native_utf8.cc', '-lntdll']
        result = subprocess.run(list(map(str, [*command, '-o', cls.program])),
                                capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cupid-alias-case-')
        self.addCleanup(self.temporary.cleanup)
        self.top = Path(self.temporary.name)
        self.root = self.top / 'repo'
        self.parent = self.root / 'user/build'
        self.parent.mkdir(parents=True)
        self.source = self.parent / 'hello.o'
        self.source.write_bytes(b'input object')
        self.output = self.parent / 'hello'
        self.output.write_bytes(b'previous output')

    def alias(self, path, destination):
        if os.name == 'nt':
            result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(path), str(destination)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            path.symlink_to(destination, target_is_directory=True)

    def namespace(self):
        return sorted(str(path.relative_to(self.top)) for path in self.top.rglob('*'))

    def run_resolve(self, source='user/build/hello.o', output='user/build/hello', root=None):
        before = self.namespace(), self.source.read_bytes(), self.output.read_bytes(), self.output.stat().st_mtime_ns
        result = subprocess.run(list(map(str, [self.program, root or self.root, source, output])),
                                cwd=ROOT, capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assertEqual((self.namespace(), self.source.read_bytes(), self.output.read_bytes(),
                          self.output.stat().st_mtime_ns), before)
        return result

    def assert_resolved(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         [self.root.resolve().as_posix(), 'user/build/hello.o', 'user/build/hello'])

    def test_plain_relative_absolute_and_lexical_paths(self):
        for source, output in (
            ('user/build/hello.o', 'user/build/hello'),
            (self.source, self.output),
            ('user/build/../build/hello.o', 'user/./build/hello'),
        ):
            with self.subTest(source=source):
                self.assert_resolved(self.run_resolve(source, output))

    def test_internal_parent_aliases_resolve_to_one_physical_pair(self):
        self.alias(self.root / 'user/input-alias', self.parent)
        self.alias(self.root / 'user/output-alias', self.parent)
        self.assert_resolved(self.run_resolve('user/input-alias/hello.o', 'user/output-alias/hello'))

    def test_repository_and_ancestor_aliases_are_resolved(self):
        self.alias(self.top / 'repo-alias', self.root)
        self.assert_resolved(self.run_resolve(root=self.top / 'repo-alias'))
        self.alias(self.top / 'ancestor-alias', self.top)
        self.assert_resolved(self.run_resolve(root=self.top / 'ancestor-alias/repo'))

    def test_missing_parent_is_rejected_without_creation(self):
        result = self.run_resolve(output='user/missing/nested/hello')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(result.stderr)

    def test_external_parent_aliases_and_prefix_siblings_are_rejected(self):
        outside = self.top / 'repo-sibling'
        outside.mkdir()
        (outside / 'hello.o').write_bytes(b'outside')
        self.alias(self.root / 'user/escape', outside)
        for source, output in (('user/escape/hello.o', 'user/escape/hello'),
                               (outside / 'hello.o', outside / 'hello')):
            with self.subTest(source=source):
                self.assertNotEqual(self.run_resolve(source, output).returncode, 0)

    def test_distinct_physical_parents_are_rejected(self):
        other = self.root / 'user/other'
        other.mkdir()
        self.assertNotEqual(self.run_resolve(output='user/other/hello').returncode, 0)

    def test_relative_repository_and_utf8_long_paths(self):
        self.assert_resolved(self.run_resolve(root=os.path.relpath(self.root, ROOT)))
        destination = self.top
        for index in range(12):
            destination /= 'caf\u00e9-\U0001f680-' + str(index) + '-' + 'x' * 40
        destination.mkdir(parents=True)
        relocated = destination / 'repo'
        self.root.rename(relocated)
        self.root = relocated
        self.parent = relocated / 'user/build'
        self.source = self.parent / 'hello.o'
        self.output = self.parent / 'hello'
        self.assertGreater(len(str(self.root)), 600)
        self.assert_resolved(self.run_resolve())

    def retained(self, root=None):
        process = subprocess.Popen(list(map(str, [self.program, root or self.root,
            'user/build/hello.o', 'user/build/hello', 'wait'])), stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        lines = [process.stdout.readline().rstrip('\n') for _ in range(4)]
        self.assertEqual(lines, [self.root.resolve().as_posix(), 'user/build/hello.o',
                                 'user/build/hello', 'READY'])
        return process

    def test_alias_retarget_does_not_retarget_the_retained_chain(self):
        alias = self.top / 'repo-alias'
        self.alias(alias, self.root)
        process = self.retained(alias)
        outside = self.top / 'outside'
        outside.mkdir()
        if os.name == 'nt':
            alias.rmdir()
        else:
            alias.unlink()
        self.alias(alias, outside)
        stdout, stderr = process.communicate('R', timeout=30)
        self.assertEqual(process.returncode, 0, stderr + stdout)
        self.assertEqual(self.output.read_bytes(), b'previous output')

    def test_physical_parent_replacement_is_blocked_or_detected(self):
        process = self.retained()
        moved = self.parent.with_name('moved')
        try:
            self.parent.rename(moved)
        except PermissionError:
            self.assertEqual(os.name, 'nt')
            stdout, stderr = process.communicate('R', timeout=30)
            self.assertEqual(process.returncode, 0, stderr + stdout)
            return
        try:
            self.parent.mkdir()
            stdout, stderr = process.communicate('R', timeout=30)
            self.assertNotEqual(process.returncode, 0, stdout)
            self.assertTrue(stderr)
            self.assertEqual((moved / 'hello').read_bytes(), b'previous output')
            self.assertEqual(list(self.parent.iterdir()), [])
        finally:
            self.parent.rmdir()
            moved.rename(self.parent)

    def test_missing_source_and_nonregular_leaves_are_rejected(self):
        self.assertNotEqual(self.run_resolve(source='user/build/missing.o').returncode, 0)
        self.assertNotEqual(self.run_resolve(source='user/build').returncode, 0)
        self.assertNotEqual(self.run_resolve(output='user/build').returncode, 0)
        if os.name != 'nt':
            fifo = self.parent / 'pipe'
            os.mkfifo(fifo)
            self.assertNotEqual(self.run_resolve(source='user/build/pipe').returncode, 0)
            self.assertNotEqual(self.run_resolve(output='user/build/pipe').returncode, 0)

    def test_linked_leaves_and_directory_leaves_are_rejected(self):
        for label in ('source', 'output'):
            with self.subTest(label=label):
                leaf = self.source if label == 'source' else self.output
                payload = leaf.read_bytes()
                leaf.unlink()
                try:
                    leaf.symlink_to(self.parent / 'target')
                except OSError as error:
                    leaf.write_bytes(payload)
                    if os.name == 'nt':
                        self.skipTest('file symlink privilege is unavailable: ' + str(error))
                    raise
                try:
                    self.assertNotEqual(subprocess.run(list(map(str, [self.program, self.root,
                        'user/build/hello.o', 'user/build/hello'])), capture_output=True).returncode, 0)
                finally:
                    leaf.unlink()
                    leaf.write_bytes(payload)
        self.assertNotEqual(self.run_resolve(source='user/build').returncode, 0)

    def test_absent_output_is_accepted_without_creating_it(self):
        self.output.unlink()
        before = self.namespace()
        result = subprocess.run(list(map(str, [self.program, self.root,
                                'user/build/hello.o', 'user/build/hello'])),
                                capture_output=True, text=True, encoding='utf-8', timeout=30)
        self.assert_resolved(result)
        self.assertEqual(self.namespace(), before)
        self.assertFalse(self.output.exists())

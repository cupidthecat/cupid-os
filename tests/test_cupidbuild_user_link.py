"""Retained physical user-link boundary, before the alias/CLI handoff."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
import unittest

from tests.test_cupidbuild_compile_kernel import ROOT, SEED, SUFFIX, checked_run
from tools import bootstrap_toolchain as bootstrap
from tools.cupidc_production_compile import USER_I386_ARGUMENTS, USER_SOURCES
from tools.cupidld_user_link import link_user_program, validate_user_executable_bytes


CALLER = r'''
#include "cupidbuild.h"
#if defined(NATIVE_USER_LINK_WINDOWS)
#include "path_encoding.h"
#include <stdlib.h>
#include <wchar.h>
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
#else
int main(int argc, char **argv) {
#endif
  cupidbuild_user_link_request_t request;
  if (argc != 5) return 2;
  request.repository_root = argv[1];
  request.seed_manifest = argv[2];
  request.source = argv[3];
  request.output = argv[4];
#if defined(NATIVE_USER_LINK_WINDOWS)
  result = cupidbuild_link_user_object(&request);
  for (index = 0; index < argc; index++) free(argv[index]);
  free(argv);
  return result;
#else
  return cupidbuild_link_user_object(&request);
#endif
}
'''


class CupidBuildUserLinkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='.user-link-', dir=ROOT / 'toolchain')
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        caller = directory / 'caller.cc'
        caller.write_text(getattr(cls, 'caller_source', CALLER), encoding='ascii')
        cls.program = directory / ('caller.exe' if os.name == 'nt' else 'caller')
        if os.environ.get('CUPIDBUILD_USER_LINK_CHECKED') == '1':
            cls.build_checked_tool(directory, 'cupidbuild', cls.program, 'target', caller)
        else:
            names = ('seed_manifest', 'seed_release', 'contract_parse_internal',
                     'ctool', 'ctool_host', 'elf32', 'cupidbuild_host', 'cupidbuild', 'path_encoding') + getattr(cls, 'native_modules_extra', ())
            command = [shutil.which('clang') or 'clang', '-std=c11', '-O2',
                       '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS',
                       '-DCUPIDBUILD_PUBLICATION_RACE_TEST', '-I', ROOT / 'toolchain',
                       '-x', 'c', caller,
                       *[ROOT / ('toolchain/' + name + '.cc') for name in names]]
            if os.name == 'nt':
                command += ['-DNATIVE_USER_LINK_WINDOWS', '-DCUPID_NATIVE_UTF8_ENABLE',
                            ROOT / 'toolchain/native_utf8.cc', '-lntdll']
            checked_run([*command, '-o', cls.program])
        cls.linker = directory / ('current-cupidld' + SUFFIX)
        cls.build_checked_tool(directory, 'cupidld', cls.linker, 'linker')
        cls.objects = {}
        for source in USER_SOURCES:
            name = Path(source).stem
            path = directory / (name + '.o')
            checked_run([SEED / ('cupidc' + SUFFIX), '--root', ROOT, '-c', '/' + source,
                         '-o', '/' + path.relative_to(ROOT).as_posix(), *USER_I386_ARGUMENTS])
            cls.objects[name] = path.read_bytes()

    @classmethod
    def build_checked_tool(cls, directory, role, program, object_tag, caller=None,
                           extra_assembly=(), extra_imports=()):
        plan = bootstrap._candidate_build_plan(json.loads(
            (ROOT / 'bootstrap/seeds/i386-linux/manifest.json').read_bytes())['build_plan'])
        if os.name == 'nt':
            plan = bootstrap._windows_build_plan(plan, utf8=True, long_paths=True, user_link_aliases=True)
        order = list(plan['links'][role])
        for name, _ in extra_assembly:
            if name in order:
                raise AssertionError('extra assembly duplicates a planned object: ' + name)
            order.append(name)
        objects = {name: directory / (name + '.' + object_tag + '.o') for name in order}
        for row in plan['sources']:
            if row['name'] not in objects:
                continue
            source = ('/' + caller.relative_to(ROOT).as_posix()
                      if caller is not None and row['name'] == 'cupidbuild_main' else row['path'])
            arguments = ['--root', ROOT, '-c', source, '-o',
                         '/' + objects[row['name']].relative_to(ROOT).as_posix(), *plan['include_arguments']]
            for definition in row.get('definitions', []):
                arguments += ['-D', definition]
            if row['gnu_extensions']:
                arguments.append('--gnu')
            checked_run([SEED / ('cupidc' + SUFFIX), *arguments])
        assembly = plan.get('assembly_sources', []) or [
                {'name': 'start', 'path': '/toolchain/hosted/i386-linux/start.asm'}]
        for row in [*assembly, *[{'name': name, 'path': path} for name, path in extra_assembly]]:
            if row['name'] in objects:
                checked_run([SEED / ('cupidasm' + SUFFIX), '-f', 'elf32',
                             ROOT / row['path'].lstrip('/'), '-o', objects[row['name']]])
        arguments = (bootstrap._windows_link_arguments(role, program, objects, order,
                                                       utf8=True, long_paths=True, user_link_aliases=True)
                     if os.name == 'nt' else ['-m', 'elf_i386', '--text-address', '0x08048000',
                                             '--entry', '_start', '-o', program,
                                             *[objects[name] for name in order]])
        checked_run([SEED / ('cupidld' + SUFFIX), *arguments, *extra_imports])
        if os.name != 'nt':
            program.chmod(0o700)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cupid-user-link-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copytree(SEED, self.root / 'seed')
        target = self.root / 'seed' / ('cupidld' + SUFFIX)
        shutil.copyfile(self.linker, target)
        manifest = self.root / 'seed/manifest.json'
        document = json.loads(manifest.read_bytes())
        for row in document['artifacts']:
            if row['name'] == 'cupidld':
                row.update(size=target.stat().st_size, sha256=hashlib.sha256(target.read_bytes()).hexdigest())
        manifest.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')
        self.parent = self.root / 'user/build'
        self.parent.mkdir(parents=True)
        self.object = self.parent / 'hello.o'
        self.object.write_bytes(self.objects['hello'])
        self.output = self.parent / 'hello'

    def run_link(self, source='user/build/hello.o', output='user/build/hello', env=None):
        return subprocess.run(list(map(str, [self.program, self.root, 'seed/manifest.json',
                                            source, output])), cwd=ROOT, env=env,
                              capture_output=True, text=True, timeout=150)

    def previous(self):
        self.output.write_bytes(b'previous executable')
        return self.output.stat().st_mtime_ns

    def assert_clean(self):
        self.assertEqual([p for p in self.root.rglob('*') if p.name.startswith('.cupidbuild-')
                          or p.name.endswith('.cupidbuild.lock')], [])

    def assert_preserved(self, timestamp):
        self.assertEqual(self.output.read_bytes(), b'previous executable')
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)
        self.assert_clean()

    def race(self, phase, mutate):
        if os.environ.get('CUPIDBUILD_USER_LINK_CHECKED') == '1':
            self.skipTest('race hooks belong to the native test caller')
        ready, resume = self.root / 'ready', self.root / 'resume'
        env = dict(os.environ, CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                   CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                   CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        errors = []
        def worker():
            try:
                deadline = time.monotonic() + 40
                while not ready.exists():
                    if time.monotonic() > deadline:
                        raise AssertionError('checkpoint was not reached: ' + phase)
                    time.sleep(.002)
                mutate()
            except BaseException as error:
                errors.append(error)
            finally:
                resume.write_bytes(b'continue')
        thread = threading.Thread(target=worker)
        thread.start()
        try:
            result = self.run_link(env=env)
        finally:
            thread.join(45)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        return result

    def test_three_real_programs_match_checked_python_link(self):
        for name, payload in self.objects.items():
            with self.subTest(program=name):
                source, output = self.parent / (name + '.o'), self.parent / name
                source.write_bytes(payload)
                link_user_program(self.root, source, output,
                                  manifest=SEED / 'manifest.json', tool_mode='checked-seed')
                reference = output.read_bytes()
                output.unlink()
                result = self.run_link('user/build/' + name + '.o', 'user/build/' + name)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), reference)
                validate_user_executable_bytes(reference)
        self.assert_clean()

    def test_equal_executable_preserves_timestamp(self):
        result = self.run_link()
        self.assertEqual(result.returncode, 0, result.stderr)
        before = self.output.stat().st_mtime_ns
        result = self.run_link()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_checked_linker_duplicate_caller_owned_option_preserves_output(self):
        before = self.previous()
        result = subprocess.run(list(map(str, [self.linker, '--caller-owned-output',
            '--caller-owned-output', '-m', 'elf_i386', '--text-address', '0x01C00000',
            '--entry', '_start', '-o', self.output, self.object])),
            cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn('usage:', result.stderr)
        self.assert_preserved(before)

    def test_linked_object_output_and_physical_parent_are_rejected(self):
        saved = self.parent / 'saved.o'
        self.object.rename(saved)
        try:
            self.object.symlink_to(saved)
        except OSError as error:
            saved.rename(self.object)
            self.skipTest('this host cannot create the required symlink: ' + str(error))
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
        alias = self.root / 'user/alias'
        alias.symlink_to(self.parent, target_is_directory=True)
        result = self.run_link('user/alias/hello.o', 'user/alias/hello')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())
        alias.unlink()
        self.assert_clean()

    def test_nested_accented_and_long_existing_parents(self):
        parent = self.root / 'user' / ('r' * 180) / ('s' * 90) / 'caf\u00e9 space'
        parent.mkdir(parents=True)
        (parent / 'hello.o').write_bytes(self.objects['hello'])
        source = (parent / 'hello.o').relative_to(self.root).as_posix()
        output = (parent / 'hello').relative_to(self.root).as_posix()
        result = self.run_link(source, output)
        self.assertEqual(result.returncode, 0, result.stderr)
        validate_user_executable_bytes((parent / 'hello').read_bytes())
        self.assert_clean()

    def test_invalid_pair_and_missing_parent_do_not_create_directories(self):
        for source, output in (('user/build/hello.o', 'user/hello'),
                ('user/build/hello.o', 'user/build/ls'), ('user/build/hello.o', 'user/build/hello.o'),
                ('user/missing/hello.o', 'user/missing/hello'),
                ('user/build/../build/hello.o', 'user/build/hello'),
                ('user//build/hello.o', 'user//build/hello'),
                ('other/build/hello.o', 'other/build/hello'),
                ('user/build/unknown.o', 'user/build/unknown')):
            with self.subTest(source=source, output=output):
                result = self.run_link(source, output)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
                self.assertFalse((self.root / 'user/missing').exists())
        self.assert_clean()

    def test_relative_repository_root_is_rejected_before_publication(self):
        before = self.previous()
        roots = [self.root.name]
        if os.name == 'nt':
            roots.append(self.root.drive + self.root.name)
        for root in roots:
            with self.subTest(root=root):
                result = subprocess.run(list(map(str, [self.program, root,
                    'seed/manifest.json', 'user/build/hello.o', 'user/build/hello'])),
                    cwd=self.root.parent, capture_output=True, text=True, timeout=150)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
                self.assert_preserved(before)

    def test_missing_and_malformed_objects_preserve_executable(self):
        before = self.previous()
        for payload in (None, b'', b'not ELF', self.objects['hello'][:51],
                        self.objects['hello'][:16] + b'\x02\0' + self.objects['hello'][18:]):
            if payload is None:
                self.object.unlink(missing_ok=True)
            else:
                self.object.write_bytes(payload)
            result = self.run_link()
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(result.stderr)
            self.assert_preserved(before)

    def test_missing_entry_and_excessive_arena_preserve_executable(self):
        before = self.previous()
        for payload in (b'void another_entry(void) {}\n',
                        b'char arena[0x200000]; void _start(void) {arena[0] = 1;}\n'):
            source = self.root / 'fixture.cc'
            source.write_bytes(payload)
            checked_run([SEED / ('cupidc' + SUFFIX), '--root', self.root,
                         '-c', '/fixture.cc', '-o', '/user/build/hello.o', *USER_I386_ARGUMENTS])
            result = self.run_link()
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assert_preserved(before)

    def test_digest_or_seed_membership_drift_preserves_executable(self):
        before = self.previous()
        tool = self.root / 'seed' / ('cupidld' + SUFFIX)
        original = tool.read_bytes()
        tool.write_bytes(original + b'drift')
        self.assertNotEqual(self.run_link().returncode, 0)
        self.assert_preserved(before)
        tool.write_bytes(original)
        extra = self.root / 'seed' / ('foreign' + SUFFIX)
        extra.write_bytes(original)
        self.assertNotEqual(self.run_link().returncode, 0)
        self.assert_preserved(before)

    def test_unknown_instruction_and_nonlocal_branch_preserve_executable(self):
        before = self.previous()
        for instructions in ('db 0x0f, 0x3f\nret\n', 'db 0xe9\ndd 0x00100000\n'):
            with self.subTest(instructions=instructions):
                source = self.root / 'unknown.asm'
                source.write_text('bits 32\nsection .text\nglobal _start\n_start:\n' + instructions,
                                  encoding='ascii')
                checked_run([SEED / ('cupidasm' + SUFFIX), '-f', 'elf32', source, '-o', self.object])
                result = self.run_link()
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assertIn('CupidDis', result.stderr)
                self.assert_preserved(before)
    def test_lock_and_output_input_hardlink_preserve_foreign_entries(self):
        before = self.previous()
        lock = self.output.with_name('hello.cupidbuild.lock')
        lock.write_text(str(os.getpid()) + '\n')
        self.assertNotEqual(self.run_link().returncode, 0)
        lock.unlink()
        self.assert_preserved(before)
        self.output.unlink()
        os.link(self.object, self.output)
        self.assertNotEqual(self.run_link().returncode, 0)
        self.assertEqual(self.output.read_bytes(), self.objects['hello'])
        self.assert_clean()

    def test_object_and_manifest_mutations_before_launch_preserve_executable(self):
        for logical in ('user/build/hello.o', 'seed/manifest.json'):
            with self.subTest(input=logical):
                before = self.previous()
                path = self.root / logical
                original = path.read_bytes()
                result = self.race('before-tool-launch', lambda: path.write_bytes(original + b'drift'))
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assert_preserved(before)
                path.write_bytes(original)
                (self.root / 'ready').unlink()
                (self.root / 'resume').unlink()

    def test_input_changed_after_install_is_rolled_back(self):
        before = self.previous()
        result = self.race('after-install', lambda: self.object.write_bytes(self.objects['hello'] + b'drift'))
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assert_preserved(before)


if __name__ == '__main__':
    unittest.main()

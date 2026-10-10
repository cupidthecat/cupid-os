"""User compilation through the retained-parent and closed-bundle boundary."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import threading
import time
import unittest

from tests.test_cupidbuild_compile_kernel import ROOT, SEED, SUFFIX, checked_run
from tools.bootstrap_toolchain import (
    _candidate_build_plan, _windows_build_plan, _windows_link_arguments,
    _check_cupidbuild_compile_user_behavior, CANDIDATE_TOOL_NAMES,
    freeze_seed_inputs, Stage, ToolRunner,
)
from tools.cupidc_kernel_compile import validate_i386_relocatable_bytes
from tools.cupidc_production_compile import (
    USER_I386_ARGUMENTS, USER_SOURCES, compile_production_source,
)

SOURCE = 'user/examples/hello.cc'


class CupidBuildCompileUserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='.compile-user-', dir=ROOT / 'toolchain')
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        native_suffix = '.exe' if os.name == 'nt' else ''
        cls.cli = directory / ('cupidbuild' + native_suffix)
        checked_run(['make', '-C', ROOT / 'toolchain', f'BUILD_DIR={directory.name}',
                     'CPPFLAGS=-DCUPIDBUILD_PUBLICATION_RACE_TEST',
                     f'{directory.name}/{cls.cli.name}'])
        plan = _candidate_build_plan(json.loads(
            (ROOT / 'bootstrap/seeds/i386-linux/manifest.json').read_bytes())['build_plan'])
        if os.name == 'nt':
            plan = _windows_build_plan(plan, utf8=True, long_paths=True, user_link_aliases=True)
        order = plan['links']['cupidbuild']
        objects = {name: directory / (name + '.target.o') for name in order}
        for source in plan['sources']:
            name = source['name']
            if name not in objects:
                continue
            arguments = ['--root', ROOT, '-c', source['path'], '-o',
                         '/' + objects[name].relative_to(ROOT).as_posix(),
                         *plan['include_arguments']]
            for definition in source.get('definitions', []):
                arguments += ['-D', definition]
            if source['gnu_extensions']:
                arguments += ['--gnu']
            checked_run([SEED / ('cupidc' + SUFFIX), *arguments])
            validate_i386_relocatable_bytes(objects[name].read_bytes())
        assembly = plan.get('assembly_sources', []) or [{
            'name': 'start', 'path': '/toolchain/hosted/i386-linux/start.asm'}]
        for source in assembly:
            if source['name'] in objects:
                checked_run([SEED / ('cupidasm' + SUFFIX), '-f', 'elf32',
                             ROOT / source['path'].lstrip('/'), '-o', objects[source['name']]])
        cls.checked = directory / ('checked-cupidbuild' + SUFFIX)
        arguments = (_windows_link_arguments('cupidbuild', cls.checked, objects, order,
                                             utf8=True, long_paths=True, user_link_aliases=True)
                     if os.name == 'nt' else ['-m', 'elf_i386', '--text-address', '0x08048000',
                                             '--entry', '_start', '-o', cls.checked,
                                             *[objects[name] for name in order]])
        checked_run([SEED / ('cupidld' + SUFFIX), *arguments])
        if os.name != 'nt':
            cls.checked.chmod(0o755)
        cls.callers = (cls.cli, cls.checked)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cupid-user-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copytree(SEED, self.root / 'seed')
        for name in (*USER_SOURCES, 'user/cupid.h'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / name).read_bytes())
        self.output = self.root / 'user/build/hello.o'

    def run_compile(self, cli=None, source=SOURCE, output='user/build/hello.o',
                    extra=(), env=None, root=None, cwd=ROOT):
        return subprocess.run(list(map(str, [cli or self.cli, 'compile-user',
            '--root', root or self.root, '--seed-manifest', 'seed/manifest.json',
            '--source', source, '--output', output, *extra])), cwd=cwd, env=env,
            capture_output=True, text=True, timeout=190)

    def previous(self):
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_bytes(b'previous object')
        return self.output.stat().st_mtime_ns

    def assert_clean(self, diagnostic=''):
        self.assertEqual([str(path) for path in self.root.rglob('*')
            if path.name.startswith('.cupidbuild-') or path.name.endswith('.cupidbuild.lock')], [], diagnostic)

    def assert_preserved(self, timestamp, diagnostic=''):
        self.assertEqual(self.output.read_bytes(), b'previous object')
        self.assertEqual(self.output.stat().st_mtime_ns, timestamp)
        self.assert_clean(diagnostic)

    def race(self, cli, phase, mutate, output='user/build/hello.o'):
        ready, resume = self.root / 'ready', self.root / 'resume'
        ready.unlink(missing_ok=True)
        resume.unlink(missing_ok=True)
        env = os.environ.copy()
        env.update(CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                   CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                   CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        errors = []
        def worker():
            try:
                deadline = time.monotonic() + 40
                while not ready.exists():
                    if time.monotonic() >= deadline:
                        raise AssertionError('checkpoint was not reached: ' + phase)
                    time.sleep(.002)
                mutate()
            except Exception as error:
                errors.append(error)
            finally:
                resume.write_bytes(b'continue')
        thread = threading.Thread(target=worker)
        thread.start()
        try:
            result = self.run_compile(cli=cli, env=env, output=output)
        finally:
            thread.join(timeout=45)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        return result

    def test_real_three_source_parity_with_checked_compiler_and_python_wrapper(self):
        for source in USER_SOURCES:
            with self.subTest(source=source):
                checked_run([self.root / 'seed' / ('cupidc' + SUFFIX), '--root', self.root,
                             '-c', '/' + source, '-o', '/reference.o', *USER_I386_ARGUMENTS])
                expected = (self.root / 'reference.o').read_bytes()
                output = self.root / 'user/custom/nested' / Path(source).with_suffix('.o').name
                compile_production_source(self.root, 'user', Path(source), output,
                    manifest=self.root / 'seed/manifest.json', tool_mode='checked-seed')
                self.assertEqual(output.read_bytes(), expected)
                for cli in self.callers:
                    output.write_bytes(b'previous')
                    result = self.run_compile(cli, source, output)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(output.read_bytes(), expected)
                    validate_i386_relocatable_bytes(output.read_bytes())
        self.assert_clean()

    def test_staged_helper_runs_the_real_checked_coordinator(self):
        seed_inputs = freeze_seed_inputs(SEED / 'manifest.json', self.root / 'staged-parent')
        tools = {name: SEED / (name + SUFFIX) for name in CANDIDATE_TOOL_NAMES}
        tools['cupidbuild'] = self.checked
        stage = Stage({}, tools)
        if os.name == 'nt':
            from tests.windows_checked_cohort import build_behavior_cohort
            stage, seed_inputs = build_behavior_cohort(self, ROOT,
                self.root / 'staged-cohort', self.checked, long_paths=True)
        behavior = self.root / 'staged-behavior'
        behavior.mkdir()
        _check_cupidbuild_compile_user_behavior(ToolRunner(ROOT), behavior, stage, stage,
                                               seed_inputs, 'checked caller ')
        self.assert_clean()

    def test_exact_user_closures_and_profile_match_the_wrapper(self):
        text = (ROOT / 'toolchain/cupidbuild.cc').read_text()
        block = text.split('cupidbuild_compile_user_closures[] = {', 1)[1].split('\n};', 1)[0]
        rows = re.findall(r'\{"([^"]+)", \{(.*?)\}, (\d+)u\}', block)
        self.assertEqual({name: tuple(re.findall(r'"([^"]+)"', inputs))
                          for name, inputs, _ in rows},
                         {name: ('user/cupid.h', name) for name in USER_SOURCES})
        self.assertTrue(all(count == '2' for _, _, count in rows))
        profile = text.split('cupidbuild_compile_user_profile[] = {', 1)[1].split('\n};', 1)[0]
        self.assertEqual(tuple(re.findall(r'"([^"]+)"', profile)), USER_I386_ARGUMENTS)

    def test_lexical_aliases_absolute_outputs_unicode_spaces_and_long_components(self):
        directories = ('build', '.hidden/nested', 'temp/../custom', 'custom/./nested',
                       'caf\u00e9 \u65e5\u672c \U0001f431', 'long-' + 'x' * 180)
        expected = None
        for cli in self.callers:
            for directory in directories:
                with self.subTest(cli=cli.name, directory=directory):
                    output = self.root / 'user' / directory / 'hello.o'
                    result = self.run_compile(cli, source=self.root / 'user/examples/./hello.cc', output=output)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    data = Path(os.path.abspath(output)).read_bytes()
                    if expected is None:
                        expected = data
                    self.assertEqual(data, expected)
        self.assert_clean()

    def test_relative_root_and_output_alias_match_absolute_binding(self):
        for cli in self.callers:
            result = self.run_compile(cli, root='..', cwd=self.root / 'user',
                                      source='user/examples/../examples/hello.cc',
                                      output='user/missing/../build/hello.o')
            self.assertEqual(result.returncode, 0, result.stderr)
            validate_i386_relocatable_bytes(self.output.read_bytes())
        self.assert_clean()

    def test_long_repository_argument_from_short_launch_directory(self):
        long_root = self.root / ('r' * 180) / ('s' * 90)
        long_root.mkdir(parents=True)
        shutil.copytree(self.root / 'seed', long_root / 'seed')
        shutil.copytree(self.root / 'user', long_root / 'user')
        self.assertGreater(len(str(long_root)), 260)
        for cli in self.callers:
            result = self.run_compile(cli, root=long_root, cwd=ROOT,
                                      output='user/nested/output/hello.o')
            self.assertEqual(result.returncode, 0, result.stderr)
            validate_i386_relocatable_bytes((long_root / 'user/nested/output/hello.o').read_bytes())
        self.assert_clean()

    def test_invalid_binding_precedes_directory_creation(self):
        for cli in self.callers:
            for source, output, extra in (
                    ('user/examples/unknown.cc', 'user/missing/unknown.o', ()),
                    (SOURCE, 'user/hello.o', ()), (SOURCE, 'user/examples/new/hello.o', ()),
                    (SOURCE, 'user/missing/other.o', ()), (SOURCE, 'user/missing/hello.cc', ()),
                    (SOURCE, '../outside/hello.o', ()), (SOURCE, '/outside/hello.o', ()),
                    (SOURCE, 'user/missing/hello.o', ('--gnu',)),
                    (SOURCE, 'user/missing/hello.o', ('--timeout', '1'))):
                with self.subTest(cli=cli.name, source=source, output=output, extra=extra):
                    result = self.run_compile(cli, source, output, extra=extra)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(result.stderr)
                    self.assertFalse((self.root / 'user/missing').exists())
        self.assert_clean()

    def test_complete_user_profile_and_original_logical_filename(self):
        (self.root / SOURCE).write_bytes(b'#include "../cupid.h"\n'
            b'#if defined(DEBUG) || defined(__GNUC__)\n#error leaked kernel or GNU profile\n#endif\n'
            b'const char *logical = __FILE__;\nvoid _start(cupid_syscall_table_t *sys) {(void)sys;}\n')
        checked_run([self.root / 'seed' / ('cupidc' + SUFFIX), '--root', self.root,
                     '-c', '/' + SOURCE, '-o', '/reference.o', *USER_I386_ARGUMENTS])
        for cli in self.callers:
            result = self.run_compile(cli)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.output.read_bytes(), (self.root / 'reference.o').read_bytes())
            self.assertIn(('/' + SOURCE).encode(), self.output.read_bytes())
        self.assert_clean()

    def test_missing_source_header_invalid_c_and_live_only_include_preserve_output(self):
        source, header = self.root / SOURCE, self.root / 'user/cupid.h'
        original_source, original_header = source.read_bytes(), header.read_bytes()
        for cli in self.callers:
            for mode in ('missing-source', 'missing-header', 'invalid-c', 'live-include', 'gnu'):
                with self.subTest(cli=cli.name, mode=mode):
                    before = self.previous()
                    source.write_bytes(original_source)
                    header.write_bytes(original_header)
                    if mode == 'missing-source':
                        source.unlink()
                    elif mode == 'missing-header':
                        header.unlink()
                    elif mode == 'invalid-c':
                        source.write_bytes(b'int broken = ;\n')
                    elif mode == 'live-include':
                        source.write_bytes(b'#include "live-only.h"\nint value;\n')
                        (source.parent / 'live-only.h').write_bytes(b'int live;\n')
                    else:
                        source.write_bytes(b'int value __attribute__((used));\n')
                    result = self.run_compile(cli)
                    self.assertNotEqual(result.returncode, 0)
                    self.assert_preserved(before, result.stderr)
        source.write_bytes(original_source)
        header.write_bytes(original_header)
        self.assertEqual(self.run_compile().returncode, 0)
        self.assert_clean()

    def test_equal_output_keeps_timestamp_after_complete_checks(self):
        for cli in self.callers:
            self.assertEqual(self.run_compile(cli).returncode, 0)
            data = self.output.read_bytes()
            os.utime(self.output, ns=(1_600_000_000_000_000_000,) * 2)
            before = self.output.stat().st_mtime_ns
            result = self.run_compile(cli)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.output.read_bytes(), data)
            self.assertEqual(self.output.stat().st_mtime_ns, before)
        self.assert_clean()

    def test_source_header_and_manifest_drift_before_launch_and_after_install(self):
        for cli in (self.cli,):
            for phase in ('before-tool-launch', 'after-install'):
                for logical in (SOURCE, 'user/cupid.h', 'seed/manifest.json'):
                    with self.subTest(cli=cli.name, phase=phase, logical=logical):
                        path = self.root / logical
                        original = path.read_bytes()
                        before = self.previous()
                        result = self.race(cli, phase, lambda: path.write_bytes(original + b'\n'))
                        self.assertNotEqual(result.returncode, 0, result.stderr)
                        self.assert_preserved(before)
                        path.write_bytes(original)

    def test_live_lock_hardlink_alias_and_parent_file_collision(self):
        for cli in self.callers:
            before = self.previous()
            lock = self.output.with_name('hello.o.cupidbuild.lock')
            lock.write_text(f'{os.getpid()}\n')
            result = self.run_compile(cli)
            self.assertNotEqual(result.returncode, 0)
            lock.unlink()
            self.assert_preserved(before)
            self.output.unlink()
            original = (self.root / SOURCE).read_bytes()
            os.link(self.root / SOURCE, self.output)
            result = self.run_compile(cli)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(self.output.read_bytes(), original)
            self.output.unlink()
            collision = self.root / 'user/collision'
            collision.write_bytes(b'foreign file')
            result = self.run_compile(cli, output='user/collision/hello.o')
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(collision.read_bytes(), b'foreign file')
            collision.unlink()
        self.assert_clean()

    def test_distinct_outputs_can_publish_during_retained_parent_checks(self):
        for cli in (self.cli,):
            results = []
            result = self.race(cli, 'before-mutation', lambda: results.append(
                self.run_compile(cli, USER_SOURCES[0], 'user/build/cat.o')))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(results), 1)
            self.assertEqual(results[0].returncode, 0, results[0].stderr)
        self.assert_clean()

    def test_linked_header_output_leaf_and_parent_are_rejected(self):
        header = self.root / 'user/cupid.h'
        saved_header = self.root / 'saved-header.h'
        header.rename(saved_header)
        try:
            header.symlink_to(saved_header)
        except OSError as error:
            saved_header.rename(header)
            self.skipTest('this host cannot create the required symlink: ' + str(error))
        for cli in self.callers:
            before = self.previous()
            self.assertNotEqual(self.run_compile(cli).returncode, 0)
            self.assert_preserved(before)
        header.unlink()
        saved_header.rename(header)
        source = self.root / SOURCE
        saved_source = self.root / 'saved-source.cc'
        source.rename(saved_source)
        source.symlink_to(saved_source)
        for cli in self.callers:
            before = self.previous()
            self.assertNotEqual(self.run_compile(cli).returncode, 0)
            self.assert_preserved(before)
        source.unlink()
        saved_source.rename(source)
        foreign = self.root / 'foreign.o'
        foreign.write_bytes(b'foreign output')
        self.output.unlink()
        self.output.symlink_to(foreign)
        for cli in self.callers:
            self.assertNotEqual(self.run_compile(cli).returncode, 0)
            self.assertEqual(foreign.read_bytes(), b'foreign output')
        self.output.unlink()
        outside = self.root / 'outside'
        outside.mkdir()
        linked = self.root / 'user/linked'
        linked.symlink_to(outside, target_is_directory=True)
        for cli in self.callers:
            result = self.run_compile(cli, output='user/linked/hello.o')
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(list(outside.iterdir()), [])
        linked.unlink()
        self.assert_clean()

    def test_created_directories_persist_after_failed_compilation(self):
        (self.root / SOURCE).write_bytes(b'int broken = ;\n')
        for cli in self.callers:
            parent = self.root / 'user' / cli.stem / 'created/nested'
            result = self.run_compile(cli, output=parent / 'hello.o')
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(parent.is_dir())
            self.assertEqual(list(parent.iterdir()), [])
        self.assert_clean()

    def test_all_ancestors_remain_bound_when_original_leaf_is_transplanted(self):
        for phase in ('before-tool-launch', 'before-mutation'):
            with self.subTest(phase=phase):
                parent = self.root / 'user/build'
                nested = parent / 'nested'
                nested.mkdir(parents=True, exist_ok=True)
                self.output = nested / 'hello.o'
                before = self.previous()
                displaced = self.root / ('saved-build-' + phase)
                replacements = []
                def replace():
                    try:
                        parent.rename(displaced)
                    except PermissionError:
                        replacements.append(False)
                        return
                    replacements.append(True)
                    parent.mkdir()
                    (displaced / 'nested').rename(nested)
                result = self.race(self.cli, phase, replace,
                                   output='user/build/nested/hello.o')
                self.assertEqual(len(replacements), 1)
                if replacements[0]:
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(self.output.read_bytes(), b'previous object')
                    self.assertEqual(self.output.stat().st_mtime_ns, before)
                    retained = [path for path in self.root.rglob('*')
                                if path.name.startswith('.cupidbuild-') or
                                path.name.endswith('.cupidbuild.lock')]
                    if phase == 'before-mutation':
                        self.assertIn('cleanup failed', result.stderr)
                        self.assertEqual(len(retained), 4, result.stderr)
                        self.assertEqual({path.name.rsplit('.', 2)[-1] for path in retained},
                                         {'publish', 'o', 'reserve', 'lock'})
                        candidates = [path for path in retained if path.suffix in ('.o', '.publish')]
                        self.assertEqual(len(candidates), 2)
                        self.assertEqual(candidates[0].read_bytes(), candidates[1].read_bytes())
                        validate_i386_relocatable_bytes(candidates[0].read_bytes())
                    else:
                        self.assertEqual(retained, [], result.stderr)
                    nested.rename(displaced / 'nested')
                    parent.rmdir()
                    displaced.rename(parent)
                    for path in retained:
                        path.unlink()
                else:
                    self.assertEqual(os.name, 'nt')
                    self.assertEqual(result.returncode, 0, result.stderr)
                    validate_i386_relocatable_bytes(self.output.read_bytes())
                self.assert_clean()


if __name__ == '__main__':
    unittest.main()

"""Caller-authorized releases across the real typed seeded transactions."""

import copy
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

from tests import test_cupidbuild_seed_release as _release
from tests import test_toolchain_cupidbuild as _build
from tests import test_cupidbuild_user_link as _user_link
from tests.test_cupidc_source_bundle import active_input_bytes
from tools.cupidc_kernel_compile import (
    FROZEN_KERNEL_INPUT_CLOSURES, _profile_input_manifest,
    validate_i386_relocatable_bytes,
)
from tools.cupidc_production_compile import GENERATED_INCLUDE_CLOSURE, USER_SOURCES
from tools.cupidld_user_link import validate_user_executable_bytes

ROOT = _release.ROOT
PHYSICAL_CALLER = _user_link.CALLER.replace('#include "cupidbuild.h"',
    '#include "cupidbuild.h"\n#include <string.h>').replace('argc != 5', 'argc != 6').replace(
    'cupidbuild_link_user_object(&request)',
    '(strcmp(argv[5], "--strict") == 0 ? cupidbuild_link_user_object(&request) : '
    'cupidbuild_link_user_object_with_release(&request, '
    'strcmp(argv[5], "--null") == 0 ? (const char *)0 : argv[5]))')


class CupidBuildSeedReleaseOperationsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _release.CupidBuildSeedReleaseTests.setUpClass.__func__(cls)
        directory = Path(cls.directory.name)
        cls.linker = directory / ('current-cupidld.exe' if os.name == 'nt' else 'current-cupidld.elf')
        _user_link.CupidBuildUserLinkTests.build_checked_tool.__func__(
            cls, directory, 'cupidld', cls.linker, 'release-linker')
        caller = directory / 'physical-release.cc'
        caller.write_text(PHYSICAL_CALLER, encoding='ascii')
        cls.physical = directory / ('physical-release.exe' if os.name == 'nt' else 'physical-release')
        modules = ('seed_manifest', 'seed_release', 'contract_parse_internal', 'ctool',
                   'ctool_host', 'elf32', 'cupidbuild_host', 'cupidbuild', 'path_encoding')
        command = [shutil.which('clang') or 'clang', '-std=c11', '-O2', '-Wall', '-Wextra',
                   '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-I', str(ROOT / 'toolchain'),
                   '-x', 'c', str(caller), *[str(ROOT / ('toolchain/' + name + '.cc')) for name in modules]]
        if os.name == 'nt':
            command += ['-DNATIVE_USER_LINK_WINDOWS', '-DCUPID_NATIVE_UTF8_ENABLE',
                        str(ROOT / 'toolchain/native_utf8.cc'), '-lntdll']
        result = subprocess.run([*command, '-o', str(cls.physical)], cwd=ROOT,
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix='.release-operations-', dir=ROOT if os.name == 'nt' else None)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest, self.release, self.record = (
            _release.CupidBuildSeedReleaseTests.fixture(self, self.root))
        self.historical = self.root / 'seed/historical.json'
        original = ROOT / ('bootstrap/seeds/i386-' + _release.HOST + '/manifest.json')
        # Modern plans require release context. Keep the original no-context
        # baseline on its recognized pre-ISO plan and parent contract.
        historical = json.loads((ROOT / ('tests/fixtures/'
            'seed-manifest-pre-iso-' + _release.HOST + '.json')).read_bytes())
        historical['artifacts'] = json.loads(original.read_bytes())['artifacts']
        self.historical.write_text(json.dumps(historical) + '\n', encoding='utf-8')
        # The installed LD predates caller-owned publication. Build its actual
        # active source and bind the resulting tool bytes in both fixture records.
        payload = self.linker.read_bytes()
        self.tool('cupidld').write_bytes(payload)
        digest = hashlib.sha256(payload).hexdigest()
        for manifest in (self.historical, self.manifest):
            document = json.loads(manifest.read_bytes())
            row = next(row for row in document['artifacts'] if row['name'] == 'cupidld')
            row.update(size=len(payload), sha256=digest)
            manifest.write_text(json.dumps(document) + '\n', encoding='utf-8')
        row = next(row for row in self.record['artifacts']
                   if row['name'] == 'cupidld' and row['format'] == ('pe32' if os.name == 'nt' else 'elf32'))
        row.update(size=len(payload), sha256=digest)
        self.release.write_text(json.dumps(self.record) + '\n', encoding='utf-8')

    def tool(self, name):
        suffix = '.exe' if os.name == 'nt' else '.elf'
        return self.root / 'seed' / (name + suffix)

    def copy_inputs(self, paths):
        for logical in sorted(set(paths)):
            target = self.root / logical
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(active_input_bytes(logical))

    def copy_profile(self):
        document = _profile_input_manifest(ROOT)
        paths = {item['path'] for item in document['inputs']}
        for sources in document['sources'].values():
            paths.update(sources)
        self.copy_inputs(paths)

    def command(self, operation, source, output, *, manifest=None, options=(), cli=None):
        command = [str(cli or self.cli), operation, '--root', str(self.root),
                   '--seed-manifest', str(manifest or self.manifest)]
        if source is not None:
            command += ['--input-manifest' if operation == 'flatten-kernel' else '--source', source]
        return [*command, '--output', output, *map(str, options)]

    def run_operation(self, operation, source, output, **kwargs):
        return subprocess.run(self.command(operation, source, output, **kwargs),
                              cwd=self.root, capture_output=True, text=True, timeout=240)

    def assert_clean(self):
        self.assertEqual(_build.CupidBuildCliTests._private_roots(self, self.root), set())
        self.assertEqual(list(self.root.rglob('*.cupidbuild.lock')), [])

    def check_context(self, operation, source, output, validator):
        """Compare historical bytes, release replay, and rejection preservation."""
        path = self.root / output
        result = self.run_operation(operation, source, output, manifest=self.historical)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''))
        expected = path.read_bytes()
        validator(expected)
        os.utime(path, ns=(1_600_000_000_000_000_000,) * 2)
        timestamp = path.stat().st_mtime_ns
        original = {item: item.read_bytes() for item in (self.manifest, self.release)}
        result = self.run_operation(operation, source, output,
                                    options=('--seed-release', self.release))
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''))
        self.assertEqual(path.read_bytes(), expected)
        # Operations that replace an unchanged output keep their existing policy.
        timestamp = path.stat().st_mtime_ns
        for item, contents in original.items():
            self.assertEqual(item.read_bytes(), contents)
        # The original entry point remains strict after an explicit release call.
        result = self.run_operation(operation, source, output)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('fixed-point provenance differs', result.stderr)
        self.assertEqual(path.read_bytes(), expected)
        self.assertEqual(path.stat().st_mtime_ns, timestamp)
        invalid = copy.deepcopy(self.record)
        invalid['parent_linux_manifest_sha256'] = 'a' * 64
        self.release.write_text(json.dumps(invalid) + '\n', encoding='utf-8')
        result = self.run_operation(operation, source, output,
                                    options=('--seed-release', self.release))
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertTrue(result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertEqual(path.read_bytes(), expected)
        self.assertEqual(path.stat().st_mtime_ns, timestamp)
        self.assert_clean()

    def assembly(self, operation, contents, validator, suffix='.asm'):
        source = 'entry' + suffix
        (self.root / source).write_text(contents, encoding='ascii', newline='\n')
        self.check_context(operation, source, 'result.o', validator)

    def test_assemble_object_release_preserves_checked_elf_bytes(self):
        self.assembly('assemble-cupidasm-object', 'BITS 32\nsection .text\nret\n',
                      validate_i386_relocatable_bytes)

    def test_assemble_bootloader_release_preserves_raw_image(self):
        def validate(data):
            self.assertEqual(len(data), 2560)
            self.assertEqual(data[:4], b'\xeb\x01\x90\xc3')
        self.assembly('assemble-bootloader', _build._CUPIDBUILD_BOOTLOADER_BEHAVIOR_SOURCE, validate)

    def test_assemble_smp_release_preserves_raw_image(self):
        self.assembly('assemble-smp-trampoline', _build._CUPIDBUILD_SMP_BEHAVIOR_SOURCE,
                      lambda data: self.assertEqual(len(data), 4096), '.S')

    def test_assemble_iso_release_preserves_raw_image(self):
        self.assembly('assemble-iso-pattern', _build._CUPIDBUILD_ISO_PATTERN_BEHAVIOR_SOURCE,
                      lambda data: self.assertEqual(len(data), 4096))

    def test_embed_jpeg_release_preserves_original_image_identity(self):
        (self.root / 'image.jpeg').write_bytes(_build.BASELINE_JPEG)
        def validate(data):
            validate_i386_relocatable_bytes(data)
            self.assertIn(_build.BASELINE_JPEG, data)
            self.assertIn(b'_binary_image_jpeg_start', data)
        self.check_context('embed-jpeg', 'image.jpeg', 'image.jpeg.o', validate)

    def build_elf(self):
        source = self.root / 'entry.asm'
        source.write_text('BITS 32\nglobal _start:function\nsection .text\n'
                          '_start:\nmov eax, 0x12345678\nret\n', encoding='ascii')
        commands = ((self.tool('cupidasm'), '-f', 'elf32', 'entry.asm', '-o', 'entry.o'),
                    (self.tool('cupidld'), '-m', 'elf_i386', '--text-address', '0x01C00000',
                     '--entry', '_start', '-o', 'kernel.elf.pass1', 'entry.o'))
        for command in commands:
            result = subprocess.run(list(map(str, command)), cwd=self.root,
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stderr)
        return self.root / 'kernel.elf.pass1'

    def test_generate_ksyms_release_preserves_valid_symbol_blob(self):
        self.build_elf()
        def validate(data):
            self.assertIn(b'ksym_blob_size', data)
            self.assertIn(b'0x4d59534bu', data)
        self.check_context('generate-ksyms', 'kernel.elf.pass1', 'ksyms_data.cc', validate)

    def test_flatten_kernel_release_preserves_checked_raw_bytes(self):
        elf = self.build_elf()
        (self.root / 'kernel').mkdir()
        names = ['kernel/kernel.elf.pass1', 'kernel/kernel.elf',
                 *(f'kernel/cohort-{index:02d}.elf' for index in range(25))]
        for logical in names:
            shutil.copy2(elf, self.root / logical)
        (self.root / 'code-inputs.txt').write_text('\n'.join([*names, '']), encoding='utf-8', newline='\n')
        self.check_context('flatten-kernel', 'code-inputs.txt', 'kernel/kernel.bin',
                           lambda data: self.assertIn(b'\xb8\x78\x56\x34\x12\xc3', data))

    def test_generate_profile_release_preserves_complete_source_inventory(self):
        self.copy_profile()
        def validate(data):
            document = json.loads(data)
            self.assertGreater(len(document['inputs']), 0)
            self.assertEqual(document['sources'], _profile_input_manifest(ROOT)['sources'])
        self.check_context('generate-profile-manifest', None, 'profile.json', validate)

    def test_compile_kernel_release_preserves_active_source_object(self):
        source = 'kernel/cpu/ksyms_data.cc'
        self.copy_inputs((source, *FROZEN_KERNEL_INPUT_CLOSURES[source]))
        self.check_context('compile-kernel', source, source[:-2] + 'o',
                           validate_i386_relocatable_bytes)

    def test_compile_doom_release_preserves_active_vendor_object(self):
        self.copy_profile()
        self.check_context('compile-doom', 'kernel/doom/src/d_items.cc',
                           'kernel/doom/src/d_items.o', validate_i386_relocatable_bytes)

    def test_compile_production_release_preserves_generated_install_object(self):
        self.copy_inputs(GENERATED_INCLUDE_CLOSURE)
        source = 'kernel/util/demos_programs_gen.cc'
        (self.root / source).parent.mkdir(parents=True, exist_ok=True)
        demos = sorted(path.relative_to(ROOT).as_posix() for path in (ROOT / 'demos').glob('*.asm'))
        self.assertGreater(len(demos), 0)
        result = subprocess.run([str(self.tool('cupidobj')), 'install-source', 'demos',
                                 '--demos', *demos, '-o', str(self.root / source)],
                                cwd=ROOT, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check_context('compile-production', source, source[:-2] + 'o',
                           validate_i386_relocatable_bytes)

    def test_compile_user_release_preserves_active_user_object(self):
        self.copy_inputs((*USER_SOURCES, 'user/cupid.h'))
        self.check_context('compile-user', 'user/examples/hello.cc', 'user/build/hello.o',
                           validate_i386_relocatable_bytes)

    def test_link_user_release_preserves_loader_approved_executable(self):
        self.copy_inputs((*USER_SOURCES, 'user/cupid.h'))
        result = self.run_operation('compile-user', 'user/examples/hello.cc', 'user/build/hello.o',
                                    manifest=self.historical)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check_context('link-user', 'user/build/hello.o', 'user/build/hello',
                           validate_user_executable_bytes)

    def test_physical_link_api_release_and_null_context_keep_original_contract(self):
        self.copy_inputs((*USER_SOURCES, 'user/cupid.h'))
        source, output = 'user/build/hello.o', 'user/build/hello'
        result = self.run_operation('compile-user', 'user/examples/hello.cc', source,
                                    manifest=self.historical)
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / output
        path.write_bytes(b'previous executable')
        timestamp = path.stat().st_mtime_ns
        def run(context, manifest=None):
            return subprocess.run(list(map(str, [self.physical, self.root,
                manifest or self.manifest, source, output, context])),
                cwd=self.root, capture_output=True, text=True, timeout=90)
        for context in ('--strict', '--null'):
            result = run(context)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn('fixed-point provenance differs', result.stderr)
            self.assertEqual(path.read_bytes(), b'previous executable')
            self.assertEqual(path.stat().st_mtime_ns, timestamp)
        result = run(self.release)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''))
        expected = path.read_bytes()
        validate_user_executable_bytes(expected)
        timestamp = path.stat().st_mtime_ns
        for context in ('--strict', '--null'):
            result = run(context)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(path.read_bytes(), expected)
            self.assertEqual(path.stat().st_mtime_ns, timestamp)
        result = run('--strict', self.historical)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''))
        self.assertEqual(path.read_bytes(), expected)
        self.assert_clean()

    def test_all_typed_commands_reject_duplicate_empty_and_missing_release_options(self):
        operations = ('assemble-cupidasm-object', 'assemble-bootloader', 'assemble-smp-trampoline',
                      'assemble-iso-pattern', 'embed-jpeg', 'generate-ksyms', 'flatten-kernel',
                      'generate-profile-manifest', 'compile-kernel', 'compile-doom',
                      'compile-production', 'compile-user', 'link-user')
        for operation in operations:
            for options in (('--seed-release', self.release, '--seed-release', self.release),
                            ('--seed-release', ''), ('--seed-release',)):
                with self.subTest(operation=operation, options=options):
                    result = self.run_operation(operation,
                        None if operation == 'generate-profile-manifest' else 'input.txt',
                        'result.o', options=options)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn('usage: cupidbuild', result.stderr)
                    self.assertEqual((self.root / 'result.o').read_bytes(), b'previous result')
        self.assert_clean()

    def test_release_drift_after_assembly_launch_preserves_previous_object(self):
        (self.root / 'entry.asm').write_text('BITS 32\nsection .text\nret\n', encoding='ascii')
        output = self.root / 'result.o'
        timestamp = output.stat().st_mtime_ns
        ready, resume = self.root / 'ready', self.root / 'resume'
        errors = []
        changed = threading.Event()
        def mutate():
            try:
                deadline = time.monotonic() + 20
                while not ready.is_file():
                    if time.monotonic() >= deadline:
                        raise AssertionError('assembly launch checkpoint was not reached')
                    time.sleep(.001)
                self.release.write_bytes(self.release.read_bytes() + b' \n')
                changed.set()
            except Exception as error:
                errors.append(repr(error))
            finally:
                resume.write_bytes(b'continue')
        environment = dict(os.environ)
        environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE='after-tool-launch',
            CUPIDBUILD_PUBLICATION_TEST_READY=str(ready), CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        worker = threading.Thread(target=mutate, daemon=True)
        worker.start()
        try:
            result = subprocess.run(self.command('assemble-cupidasm-object', 'entry.asm', 'result.o',
                cli=self.race_cli, options=('--seed-release', self.release)),
                cwd=self.root, env=environment, capture_output=True, text=True, timeout=30)
        finally:
            worker.join(timeout=20)
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertTrue(changed.is_set())
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertIn('checked seed inputs changed', result.stderr)
        self.assertEqual(output.read_bytes(), b'previous result')
        self.assertEqual(output.stat().st_mtime_ns, timestamp)
        self.assert_clean()


if __name__ == '__main__':
    unittest.main()

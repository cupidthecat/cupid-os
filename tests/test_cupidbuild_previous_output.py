"""Check retained previous outputs and their publication lifetime."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BLOCK = bytes((index * 29 + 17) & 255 for index in range(65536))
IMAGE_BYTES = 200 * 1024 * 1024
ORDINARY_LIMIT = 64 * 1024 * 1024


def fact(path):
    digest, size = hashlib.sha256(), 0
    with path.open('rb') as stream:
        while block := stream.read(1048576):
            digest.update(block)
            size += len(block)
    return {'size': size, 'sha256': digest.hexdigest()}


def expected_pattern(size, seed):
    block = bytes((index * 29 + seed) & 255 for index in range(65536))
    digest, remaining = hashlib.sha256(), size
    while remaining:
        part = block[:min(remaining, len(block))]
        digest.update(part)
        remaining -= len(part)
    return {'size': size, 'sha256': digest.hexdigest()}


class CupidBuildPreviousOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = None
        provided = os.environ.get('CUPIDBUILD_PREVIOUS_OUTPUT_PROGRAM')
        if provided:
            cls.program = Path(provided)
            return
        compiler = shutil.which('clang')
        if not compiler:
            raise unittest.SkipTest('requires clang or a retained checked caller')
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-previous-output-build-')
        cls.program = Path(cls.build.name) / ('contract.exe' if os.name == 'nt' else 'contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-DCUPIDBUILD_PREVIOUS_OUTPUT_TEST_NEW_API=1',
                   '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   str(ROOT / 'toolchain/tests/cupidbuild_previous_output_contract.cc'),
                   str(ROOT / 'toolchain/cupidbuild_host.cc'), str(ROOT / 'toolchain/path_encoding.cc'),
                   *(['-lntdll'] if os.name == 'nt' else []), '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode:
            cls.build.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        if cls.build:
            cls.build.cleanup()

    def setUp(self):
        self.temporary = None
        products = os.environ.get('CUPIDBUILD_PREVIOUS_OUTPUT_PRODUCTS')
        if products:
            self.case = Path(products) / self._testMethodName
            self.case.mkdir()
        else:
            self.temporary = tempfile.TemporaryDirectory(prefix='cupid-previous-output-')
            self.case = Path(self.temporary.name)
        self.invocations = []

    def tearDown(self):
        (self.case / 'invocations.json').write_text(
            json.dumps(self.invocations, indent=2) + '\n', encoding='utf-8')
        if self.temporary:
            self.temporary.cleanup()

    def call(self, mode, size=65, capacity=None, name='root', memory=False):
        capacity = max(1, size) if capacity is None else capacity
        root = self.case / name
        (root / 'nested').mkdir(parents=True)
        source = root / 'source.cc'
        source.write_bytes(b'original source\n')
        output = root / 'nested/result.bin'
        if mode != 'absent':
            with output.open('wb') as stream:
                left = size
                while left:
                    part = BLOCK[:min(left, len(BLOCK))]
                    stream.write(part)
                    left -= len(part)
        if mode == 'hardlink-alias':
            os.link(output, root / 'alias.bin')
        if mode in ('changed', 'equal', 'absent'):
            shutil.copyfile(self.program, root / 'writer')
            (root / 'writer').chmod(0o755)
        before = fact(output) if output.exists() else None
        before_stat = output.stat() if output.exists() else None
        export = self.case / (name + '-complete-copy.bin')
        command = [str(self.program), str(root).encode().hex(), mode,
                   str(size), str(capacity), str(export)]
        kwargs = {}
        if memory:
            import resource

            def limit_memory():
                resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024,) * 2)

            kwargs['preexec_fn'] = limit_memory
        started = time.perf_counter()
        timed_out = False
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=600, **kwargs)
        except subprocess.TimeoutExpired as error:
            from types import SimpleNamespace

            def decoded(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''

            timed_out = True
            result = SimpleNamespace(returncode=None, stdout=decoded(error.stdout), stderr=decoded(error.stderr))
        after = fact(output) if output.exists() else None
        copied = fact(export) if export.exists() else None
        row = {'command': command, 'exit_code': result.returncode, 'stdout': result.stdout,
               'stderr': result.stderr, 'timeout_seconds': 600, 'timed_out': timed_out,
               'elapsed_seconds': time.perf_counter() - started,
               'address_space_limit': 32 * 1024 * 1024 if memory else None,
               'output_before': before, 'output_after': after, 'complete_copy': copied}
        self.invocations.append(row)
        self.assertFalse(timed_out, result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, '')
        self.assertTrue(result.stdout.endswith('close 1\n'), result.stdout)
        names = ['nested', 'source.cc'] + (['alias.bin'] if mode == 'hardlink-alias' else [])
        if mode in ('changed', 'equal', 'absent'):
            names.append('writer')
        self.assertEqual(sorted(path.name for path in root.iterdir()), sorted(names))
        self.assertEqual(sorted(path.name for path in (root / 'nested').iterdir()), ['result.bin'])
        if mode == 'source-drift':
            self.assertEqual(source.stat().st_size, len(b'original source\n'))
            self.assertIn('live drift ', result.stdout)
        else:
            self.assertEqual(source.read_bytes(), b'original source\n')
        if mode in ('runner', 'unsafe', 'capacity-excess', 'absent'):
            self.assertIsNone(copied)
        else:
            self.assertEqual(copied, before)
            self.assertIn(f'previous {size} {before["sha256"]}\n', result.stdout)
        if mode in ('changed', 'absent'):
            self.assertEqual(after, expected_pattern(size, 55))
            self.assertIn('published 1 committed 1\n', result.stdout)
        elif mode == 'live-drift':
            self.assertEqual(after['size'], size)
            self.assertIn('live drift rejected\n' if after != before else 'live drift blocked\n', result.stdout)
        else:
            self.assertEqual(after, before)
        if mode == 'equal':
            after_stat = output.stat()
            self.assertEqual((after_stat.st_dev, after_stat.st_ino, after_stat.st_mtime_ns),
                             (before_stat.st_dev, before_stat.st_ino, before_stat.st_mtime_ns))
            self.assertIn('published 0 committed 0\n', result.stdout)
        return result.stdout

    def test_empty_previous_output(self):
        self.call('copy', 0)

    def test_padding_and_copy_block_boundaries(self):
        for size in (1, 55, 56, 63, 64, 65, 65535, 65536, 65537):
            with self.subTest(size=size):
                self.call('copy', size, name=str(size))

    def test_maximum_capacity_keeps_small_extent(self):
        self.call('copy', capacity=2147483647)

    def test_small_changed_output(self):
        self.call('changed')

    def test_small_equal_output_keeps_identity_and_time(self):
        self.call('equal')

    def test_absent_previous_output_clears_results_and_publishes(self):
        self.assertIn('previous absent\n', self.call('absent'))

    def test_second_capture_poisons_publication(self):
        self.call('duplicate')

    def test_unsafe_private_name_clears_results(self):
        self.call('unsafe')

    def test_runner_transaction_cannot_capture_previous_output(self):
        self.call('runner')

    def test_ordinary_freezing_still_rejects_output_and_hardlink_aliases(self):
        for mode in ('ordinary-alias', 'hardlink-alias'):
            with self.subTest(mode=mode):
                self.call(mode, name=mode)

    def test_public_source_and_frozen_drift_keep_their_guards(self):
        for mode in ('live-drift', 'source-drift', 'frozen-drift'):
            with self.subTest(mode=mode):
                self.call(mode, name=mode)

    def test_invalid_and_exceeded_capacities_preserve_the_output(self):
        for capacity in (0, 64, 2147483648, 4294967296, 18446744073709551615):
            with self.subTest(capacity=capacity):
                self.call('capacity-excess', capacity=capacity, name=str(capacity))

    def test_previous_output_above_ordinary_limit(self):
        self.call('copy', ORDINARY_LIMIT + 1, IMAGE_BYTES)

    def test_payload_request_retains_ordinary_limit(self):
        self.call('payload-limit', ORDINARY_LIMIT + 1, IMAGE_BYTES)

    def test_complete_200_mib_copy(self):
        self.call('copy', IMAGE_BYTES)

    def test_complete_200_mib_changed_publication(self):
        self.call('changed', IMAGE_BYTES)

    def test_complete_200_mib_equal_publication_keeps_identity_and_time(self):
        self.call('equal', IMAGE_BYTES)

    @unittest.skipIf(os.name == 'nt', 'POSIX address-space bound')
    def test_complete_200_mib_changed_publication_under_32_mib(self):
        self.call('changed', IMAGE_BYTES, memory=True)


if __name__ == '__main__':
    unittest.main()

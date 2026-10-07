"""Check complete bounded frozen copies through the public transaction API."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
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


class CupidBuildFrozenStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = None
        provided = os.environ.get('CUPIDBUILD_FROZEN_STREAM_PROGRAM')
        if provided:
            cls.program = Path(provided)
            return
        compiler = shutil.which('clang')
        if not compiler:
            raise unittest.SkipTest('requires clang or a retained checked caller')
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-frozen-stream-build-')
        cls.program = Path(cls.build.name) / ('contract.exe' if os.name == 'nt' else 'contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-DCUPIDBUILD_STREAM_FROZEN_INPUT_TEST_NEW_API=1',
                   '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   str(ROOT / 'toolchain/tests/cupidbuild_frozen_stream_contract.cc'),
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
        products = os.environ.get('CUPIDBUILD_FROZEN_STREAM_PRODUCTS')
        if products:
            self.case = Path(products) / self._testMethodName
            self.case.mkdir()
        else:
            self.temporary = tempfile.TemporaryDirectory(prefix='cupid-frozen-stream-')
            self.case = Path(self.temporary.name)
        self.invocations = []

    def tearDown(self):
        (self.case / 'invocations.json').write_text(json.dumps(self.invocations, indent=2) + '\n')
        if self.temporary:
            self.temporary.cleanup()

    def call(self, mode, size, capacity, name='root', memory=False, expected=0):
        root = self.case / name
        (root / 'nested').mkdir(parents=True)
        (root / 'source.cc').write_bytes(b'original source\n')
        (root / 'nested/result.bin').write_bytes(b'previous output')
        with (root / 'input.bin').open('wb') as stream:
            left = size
            while left:
                part = BLOCK[:min(left, len(BLOCK))]
                stream.write(part)
                left -= len(part)
        before = fact(root / 'input.bin')
        export = self.case / (name + '-complete-copy.bin')
        command = [str(self.program), str(root).encode().hex(), mode, str(size), str(capacity), str(export)]
        kwargs = {}
        if memory:
            import resource

            def limit_memory():
                resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024,) * 2)

            kwargs['preexec_fn'] = limit_memory
        result = subprocess.run(command, capture_output=True, text=True, timeout=600, **kwargs)
        row = {'command': command, 'exit_code': result.returncode, 'expected_exit_code': expected,
               'stdout': result.stdout, 'stderr': result.stderr, 'timeout_seconds': 600,
               'address_space_limit': 32 * 1024 * 1024 if memory else None,
               'input_before': before, 'input_after': fact(root / 'input.bin'),
               'complete_copy': fact(export) if export.is_file() else None}
        self.invocations.append(row)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(result.stderr, '')
        self.assertEqual((root / 'source.cc').read_bytes(), b'original source\n')
        self.assertEqual((root / 'nested/result.bin').read_bytes(), b'previous output')
        self.assertEqual(sorted(path.name for path in root.iterdir()), ['input.bin', 'nested', 'source.cc'])
        self.assertEqual(sorted(path.name for path in (root / 'nested').iterdir()), ['result.bin'])
        if mode == 'live-drift':
            self.assertNotEqual(row['input_after'], before)
            self.assertEqual(row['input_after']['size'], size)
        else:
            self.assertEqual(row['input_after'], before)
        if expected == 0:
            self.assertEqual(row['complete_copy'], before)
            self.assertTrue(result.stdout.startswith(f'captured {size} {before["sha256"]}\n'))
            self.assertTrue(result.stdout.endswith('close 1\n'))
        else:
            self.assertIsNone(row['complete_copy'])
            self.assertTrue(result.stdout.startswith('rejected '))
            self.assertTrue(result.stdout.endswith('\nclose 1\n'))
        return result.stdout

    def test_empty_input(self):
        self.call('bounded', 0, 1)

    def test_padding_and_copy_block_boundaries(self):
        for size in (1, 55, 56, 63, 64, 65, 65535, 65536, 65537):
            with self.subTest(size=size):
                self.call('bounded', size, size, str(size))

    def test_maximum_capacity_keeps_small_extent(self):
        self.call('bounded', 65, 2147483647)

    def test_input_above_ordinary_limit(self):
        self.call('bounded', ORDINARY_LIMIT + 1, IMAGE_BYTES)

    def test_complete_200_mib_copy(self):
        self.call('bounded', IMAGE_BYTES, IMAGE_BYTES)

    def test_payload_request_retains_ordinary_limit(self):
        self.assertIn('payload bounded\n', self.call('payload', ORDINARY_LIMIT + 1, IMAGE_BYTES))

    def test_small_payload_remains_available(self):
        self.assertIn('payload accepted\n', self.call('payload', 65, 65))

    def test_live_drift_rejects_source_without_changing_copy(self):
        self.assertIn('live drift rejected\n', self.call('live-drift', 65, 65))

    def test_frozen_drift_keeps_bytes_or_rejects_mutation(self):
        text = self.call('frozen-drift', 65, 65)
        self.assertTrue(any(line in text for line in (
            'frozen drift rejected\n', 'frozen drift blocked\n',
            'frozen drift blocked, metadata rejected\n')))

    def test_capacity_excess_preserves_previous_output(self):
        self.call('bounded', 65, 64, expected=7)

    def test_ordinary_freezing_keeps_64_mib_limit(self):
        self.call('ordinary', ORDINARY_LIMIT + 1, IMAGE_BYTES, expected=7)

    def test_invalid_wide_capacities_clear_results_and_cleanup(self):
        for capacity in (0, 2147483648, 4294967296, 18446744073709551615):
            with self.subTest(capacity=capacity):
                self.call('bounded', 65, capacity, str(capacity), expected=7)

    @unittest.skipIf(os.name == 'nt', 'POSIX address-space bound')
    def test_complete_200_mib_copy_under_32_mib(self):
        self.call('bounded', IMAGE_BYTES, IMAGE_BYTES, memory=True)

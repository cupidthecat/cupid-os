"""Check explicit large candidate extents through guarded publication."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BLOCK = bytes((index * 29 + 17) % 256 for index in range(65536))
IMAGE_BYTES = 200 * 1024 * 1024
ORDINARY_LIMIT = 64 * 1024 * 1024


def pattern_hash(size):
    digest = hashlib.sha256()
    while size:
        chunk = min(size, len(BLOCK))
        digest.update(BLOCK[:chunk])
        size -= chunk
    return digest.hexdigest()


class CupidBuildCandidateExtentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = None
        provided = os.environ.get('CUPIDBUILD_CANDIDATE_EXTENT_PROGRAM')
        if provided:
            cls.program = Path(provided)
            return
        compiler = shutil.which('clang')
        if not compiler:
            raise unittest.SkipTest('requires clang or a retained checked caller')
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-candidate-extent-build-')
        cls.program = Path(cls.build.name) / ('contract.exe' if os.name == 'nt' else 'contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   str(ROOT / 'toolchain/tests/cupidbuild_candidate_extent_contract.cc'),
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
        products = os.environ.get('CUPIDBUILD_CANDIDATE_EXTENT_PRODUCTS')
        if products:
            self.case = Path(products) / self._testMethodName
            self.case.mkdir()
        else:
            self.temporary = tempfile.TemporaryDirectory(prefix='cupid-candidate-extent-')
            self.case = Path(self.temporary.name)
        self.invocations = []

    def tearDown(self):
        (self.case / 'invocations.json').write_text(json.dumps(self.invocations, indent=2), encoding='utf-8')
        if self.temporary:
            self.temporary.cleanup()

    def fixture(self, name='root', existing=None):
        root = self.case / name
        (root / 'nested').mkdir(parents=True)
        (root / 'source.cc').write_bytes(b'original source\n')
        shutil.copyfile(self.program, root / 'writer')
        if existing is not None:
            with (root / 'nested/file.o').open('wb') as output:
                if isinstance(existing, bytes):
                    output.write(existing)
                else:
                    remaining = existing
                    while remaining:
                        chunk = min(remaining, len(BLOCK))
                        output.write(BLOCK[:chunk])
                        remaining -= chunk
        return root

    def call(self, mode, size, capacity, root=None, memory=False, expected=0):
        root = root or self.fixture()
        command = [str(self.program), str(root).encode().hex(), mode, str(size), str(capacity)]
        kwargs = {}
        if memory:
            import resource

            def limit_memory():
                resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024,) * 2)

            kwargs['preexec_fn'] = limit_memory
        result = subprocess.run(command, capture_output=True, text=True, timeout=600, **kwargs)
        self.invocations.append({'command': command, 'exit_code': result.returncode,
                                 'stdout': result.stdout, 'stderr': result.stderr,
                                 'address_space_limit': 32 * 1024 * 1024 if memory else None})
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result, root

    def check_output(self, root, size):
        output = root / 'nested/file.o'
        digest = hashlib.sha256()
        with output.open('rb') as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        self.assertEqual(output.stat().st_size, size)
        self.assertEqual(digest.hexdigest(), pattern_hash(size))
        self.assertEqual(sorted(path.name for path in (root / 'nested').iterdir()), ['file.o'])

    def test_invalid_wide_capacities_create_no_transaction(self):
        for capacity in ('zero', 'high', 'maximum', 'overflow'):
            with self.subTest(capacity=capacity):
                root = self.fixture(capacity, existing=b'previous output')
                result, _ = self.call('invalid-capacity', 0, capacity, root)
                self.assertEqual(result.stdout, 'invalid 1\n')
                self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')
                self.assertEqual(sorted(path.name for path in root.iterdir()), ['nested', 'source.cc', 'writer'])

    def test_empty_and_maximum_capacity(self):
        for size, capacity in ((0, 1), (65, 2147483647)):
            with self.subTest(size=size, capacity=capacity):
                result, root = self.call('candidate', size, capacity, self.fixture(str(size)))
                self.assertIn(f'captured {size} {pattern_hash(size)}\nchanged 1\n', result.stdout)
                self.check_output(root, size)

    def test_complete_200_mib_candidate_publishes(self):
        result, root = self.call('candidate', IMAGE_BYTES, IMAGE_BYTES)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, IMAGE_BYTES)

    def test_complete_200_mib_replaces_large_previous_output(self):
        root = self.fixture(existing=IMAGE_BYTES - 1)
        result, _ = self.call('candidate', IMAGE_BYTES, IMAGE_BYTES, root)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, IMAGE_BYTES)

    def test_large_candidate_passes_a_second_checked_tool(self):
        for size in (65, ORDINARY_LIMIT + 1):
            with self.subTest(size=size):
                root = self.fixture(str(size))
                result, _ = self.call('candidate-checker', size, IMAGE_BYTES, root)
                self.assertIn('checker 0\n', result.stdout)
                self.assertIn('changed 1\n', result.stdout)
                self.check_output(root, size)

    def test_rejecting_second_checked_tool_preserves_previous_output(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.call('candidate-checker-wrong-size', ORDINARY_LIMIT + 1,
                              IMAGE_BYTES, root)
        self.assertEqual(result.stdout, 'checker 94\n')
        self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')
        self.assertEqual(sorted(path.name for path in (root / 'nested').iterdir()), ['file.o'])

    @unittest.skipIf(os.name == 'nt', 'POSIX address-space bound')
    def test_large_candidate_second_checked_tool_under_32_mib(self):
        size = ORDINARY_LIMIT + 1
        root = self.fixture(existing=b'previous output')
        result, _ = self.call('candidate-checker', size, IMAGE_BYTES, root, memory=True)
        self.assertIn('checker 0\n', result.stdout)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, size)

    def test_equal_200_mib_output_keeps_identity_and_time(self):
        root = self.fixture(existing=IMAGE_BYTES)
        output = root / 'nested/file.o'
        before = output.stat()
        result, _ = self.call('candidate', IMAGE_BYTES, IMAGE_BYTES, root)
        after = output.stat()
        self.assertIn('changed 0\n', result.stdout)
        self.assertEqual((after.st_dev, after.st_ino, after.st_mtime_ns),
                         (before.st_dev, before.st_ino, before.st_mtime_ns))
        self.check_output(root, IMAGE_BYTES)

    @unittest.skipIf(os.name == 'nt', 'POSIX address-space bound')
    def test_200_mib_candidate_and_previous_output_under_32_mib(self):
        root = self.fixture(existing=IMAGE_BYTES - 1)
        result, _ = self.call('candidate', IMAGE_BYTES, IMAGE_BYTES, root, memory=True)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, IMAGE_BYTES)

    def test_candidate_above_explicit_capacity_preserves_previous(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.call('candidate-limit', 65537, 65536, root)
        self.assertIn('limit 0\n', result.stdout)
        self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')

    def test_previous_output_above_capacity_fails_before_candidate(self):
        root = self.fixture(existing=65537)
        result, _ = self.call('candidate', 65, 65536, root, expected=2)
        self.assertIn('open:', result.stderr)
        self.check_output(root, 65537)

    def test_ordinary_candidate_limit_is_unchanged(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.call('ordinary-limit', ORDINARY_LIMIT + 1, IMAGE_BYTES, root)
        self.assertIn('limit 0\n', result.stdout)
        self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')

    def test_payload_limit_does_not_disable_digest_capture(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.call('candidate-bytes-limit', ORDINARY_LIMIT + 1, IMAGE_BYTES, root)
        self.assertIn('limit 0\ndigest-after-limit 1\n', result.stdout)
        self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')

    def test_forged_digest_preserves_previous_output(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.call('candidate-wrong', 65537, IMAGE_BYTES, root)
        self.assertIn('wrong 0\n', result.stdout)
        self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')


if __name__ == '__main__':
    unittest.main()

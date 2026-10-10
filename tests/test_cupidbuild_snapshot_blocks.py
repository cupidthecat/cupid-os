"""Exercise bounded snapshot checks through the public transaction interface."""
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
BLOCK = bytes((index * 29 + 17) % 256 for index in range(65536))
LARGE = 48 * 1024 * 1024 + 65


def pattern_hash(size):
    digest = hashlib.sha256()
    while size:
        chunk = min(size, len(BLOCK))
        digest.update(BLOCK[:chunk])
        size -= chunk
    return digest.hexdigest()


class CupidBuildSnapshotBlockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = None
        provided = os.environ.get('CUPIDBUILD_SNAPSHOT_BLOCK_PROGRAM')
        if provided:
            cls.program = Path(provided)
            return
        compiler = shutil.which('clang')
        if not compiler:
            raise unittest.SkipTest('requires clang or a retained checked caller')
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-snapshot-block-build-')
        cls.program = Path(cls.build.name) / ('contract.exe' if os.name == 'nt' else 'contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   str(ROOT / 'toolchain/tests/cupidbuild_snapshot_blocks_contract.cc'),
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
        products = os.environ.get('CUPIDBUILD_SNAPSHOT_BLOCK_PRODUCTS')
        if products:
            self.case = Path(products) / self._testMethodName
            self.case.mkdir()
        else:
            self.temporary = tempfile.TemporaryDirectory(prefix='cupid-snapshot-block-')
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

    def run_case(self, mode, size, root=None, memory=False):
        root = root or self.fixture()
        command = [str(self.program), str(root).encode().hex(), mode, str(size)]
        kwargs = {}
        if memory:
            import resource

            def limit_memory():
                resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024,) * 2)

            kwargs['preexec_fn'] = limit_memory
        result = subprocess.run(command, capture_output=True, text=True, timeout=240, **kwargs)
        self.invocations.append({'command': command, 'exit_code': result.returncode,
                                 'stdout': result.stdout, 'stderr': result.stderr,
                                 'address_space_limit': 32 * 1024 * 1024 if memory else None})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        if 'limit' not in mode:
            self.assertEqual(sorted(path.name for path in (root / 'nested').iterdir()),
                             ['file.o'] if (root / 'nested/file.o').exists() else [])
        if 'limit' not in mode:
            self.assertTrue(result.stdout.startswith(f'captured {size} {pattern_hash(size)}\n'), result.stdout)
        return result, root

    def check_output(self, root, size):
        output = root / 'nested/file.o'
        digest = hashlib.sha256()
        with output.open('rb') as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        self.assertEqual(output.stat().st_size, size)
        self.assertEqual(digest.hexdigest(), pattern_hash(size))

    def test_empty_candidate(self):
        result, root = self.run_case('candidate', 0)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, 0)

    def test_padding_and_block_boundaries(self):
        for size in (1, 55, 56, 63, 64, 65, 65535, 65536, 65537):
            with self.subTest(size=size):
                result, root = self.run_case('candidate', size, self.fixture(str(size)))
                self.assertIn('changed 1\n', result.stdout)
                self.check_output(root, size)

    def test_returned_candidate_payload(self):
        _, root = self.run_case('candidate-bytes', 65537)
        self.check_output(root, 65537)

    def test_large_absent_candidate(self):
        result, root = self.run_case('candidate', LARGE)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, LARGE)

    def test_large_changed_candidate(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.run_case('candidate', LARGE, root)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, LARGE)

    def test_large_equal_candidate_keeps_identity_and_time(self):
        root = self.fixture(existing=LARGE)
        output = root / 'nested/file.o'
        before = output.stat()
        result, _ = self.run_case('candidate', LARGE, root)
        after = output.stat()
        self.assertIn('changed 0\n', result.stdout)
        self.assertEqual((after.st_dev, after.st_ino, after.st_mtime_ns),
                         (before.st_dev, before.st_ino, before.st_mtime_ns))
        self.check_output(root, LARGE)

    def test_large_private_file(self):
        _, root = self.run_case('private', LARGE)
        self.assertFalse((root / 'nested/file.o').exists())

    def test_returned_private_payload(self):
        _, root = self.run_case('private-bytes', 65537)
        self.assertFalse((root / 'nested/file.o').exists())

    def test_forged_candidate_digest_preserves_output(self):
        root = self.fixture(existing=b'previous output')
        result, _ = self.run_case('candidate-wrong', 65537, root)
        self.assertIn('wrong 0\n', result.stdout)
        self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')

    def test_forged_private_digest_is_rejected(self):
        result, root = self.run_case('private-wrong', 65537)
        self.assertIn('wrong 0\n', result.stdout)
        self.assertFalse((root / 'nested/file.o').exists())

    def test_candidate_keeps_legacy_limit(self):
        result, root = self.run_case('candidate-limit', 64 * 1024 * 1024 + 1)
        self.assertEqual(result.stdout, f'limit 0\nclosed {0 if os.name == "nt" else 1}\n')
        if os.name == 'nt':
            retained = list(root.glob('.cupidbuild-object-*/candidate.o'))
            self.assertEqual(len(retained), 1)
            self.assertEqual(retained[0].stat().st_size, 64 * 1024 * 1024 + 1)
        self.assertFalse((root / 'nested/file.o').exists())

    def test_private_file_keeps_legacy_limit(self):
        result, root = self.run_case('private-limit', 64 * 1024 * 1024 + 1)
        self.assertEqual(result.stdout, f'limit 0\nclosed {0 if os.name == "nt" else 1}\n')
        if os.name == 'nt':
            retained = list(root.glob('.cupidbuild-object-*/candidate.map'))
            self.assertEqual(len(retained), 1)
            self.assertEqual(retained[0].stat().st_size, 64 * 1024 * 1024 + 1)
        self.assertFalse((root / 'nested/file.o').exists())

    def test_restored_time_source_drift_prevents_publication(self):
        root = self.fixture(existing=b'previous output')
        command = [str(self.program), str(root).encode().hex(), 'candidate-pause', '65537']
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        lines = queue.Queue()

        def read_ready():
            lines.put(process.stdout.readline())
            lines.put(process.stdout.readline())

        reader = threading.Thread(target=read_ready, daemon=True)
        reader.start()
        prefix = b''
        try:
            prefix = lines.get(timeout=240)
            self.assertEqual(prefix.decode().strip(), f'captured 65537 {pattern_hash(65537)}')
            ready = lines.get(timeout=240)
            prefix += ready
            self.assertEqual(ready.strip(), b'ready')
            source = root / 'source.cc'
            before = source.stat()
            source.write_bytes(b'modified source\n')
            os.utime(source, ns=(before.st_atime_ns, before.st_mtime_ns))
            output, error = process.communicate(b'x', timeout=240)
            self.invocations.append({'command': command, 'exit_code': process.returncode,
                                     'stdout': (prefix + output).decode(), 'stderr': error.decode()})
            self.assertEqual(process.returncode, 0, (prefix + output, error))
            self.assertEqual(output.splitlines(), [b'drift 0 0'])
            self.assertEqual((root / 'nested/file.o').read_bytes(), b'previous output')
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
            reader.join(timeout=5)

    @unittest.skipIf(os.name == 'nt', 'requires POSIX address-space limits')
    def test_large_candidate_under_fixed_memory_limit(self):
        result, root = self.run_case('candidate', LARGE, memory=True)
        self.assertIn('changed 1\n', result.stdout)
        self.check_output(root, LARGE)

    @unittest.skipIf(os.name == 'nt', 'requires POSIX address-space limits')
    def test_large_existing_output_under_fixed_memory_limit(self):
        root = self.fixture(existing=LARGE)
        result, _ = self.run_case('candidate', LARGE, root, memory=True)
        self.assertIn('changed 0\n', result.stdout)
        self.check_output(root, LARGE)

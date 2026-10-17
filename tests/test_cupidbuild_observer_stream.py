"""Bounded retained file capture, whole-file digests and live drift checks."""
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
SOURCE = ROOT / 'toolchain/tests/cupidbuild_stream_contract.cc'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024): value.update(block)
    return value.hexdigest()


class CupidBuildObserverStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.retained = bool(os.environ.get('CUPIDBUILD_STREAM_PRODUCTS'))
        if cls.retained:
            cls.products = Path(os.environ['CUPIDBUILD_STREAM_PRODUCTS']).resolve()
            cls.products.mkdir(parents=True, exist_ok=True)
        else:
            cls.temporary = tempfile.TemporaryDirectory(prefix='cupid-stream-')
            cls.addClassCleanup(cls.temporary.cleanup)
            cls.products = Path(cls.temporary.name)
        if configured := os.environ.get('CUPIDBUILD_STREAM_PROGRAM'):
            cls.program = Path(configured).resolve(strict=True)
            return
        compiler = shutil.which('clang')
        if not compiler: raise RuntimeError('Clang is required for the native observer contract')
        cls.program = cls.products / ('stream-contract.exe' if os.name == 'nt' else 'stream-contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   str(SOURCE), str(ROOT / 'toolchain/cupidbuild_host.cc'),
                   str(ROOT / 'toolchain/path_encoding.cc'),
                   *(['-lntdll'] if os.name == 'nt' else []), '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        (cls.products / 'compile.stdout').write_bytes(result.stdout)
        (cls.products / 'compile.stderr').write_bytes(result.stderr)
        (cls.products / 'compile-command.json').write_text(json.dumps(command, indent=2))
        if result.returncode: raise AssertionError(result.stderr.decode(errors='replace'))

    def setUp(self):
        self.case = self.products / self._testMethodName
        self.case.mkdir()
        self.root = self.case / 'root'
        self.root.mkdir()
        (self.root / 'nested').mkdir()
        self.source = self.root / 'nested/payload.bin'
        self.copy = self.case / 'copied.bin'
        self.invocations = []

    def fixture(self, size):
        with self.source.open('xb') as stream:
            # Deterministic bytes cross both SHA-256 and stream block boundaries.
            block = bytes((index * 37 + index // 7) & 255 for index in range(65536))
            complete, remainder = divmod(size, len(block))
            for _ in range(complete): stream.write(block)
            stream.write(block[:remainder])
        return digest(self.source)

    def invoke(self, mode='copy', limit=None, logical='nested/payload.bin', mutate=None,
               expected=0, barrier='ready'):
        if limit is None: limit = self.source.stat().st_size
        output = self.copy if not self.invocations else self.case / f'copied-{len(self.invocations)}.bin'
        command = [str(self.program), str(self.root).encode().hex(), logical.encode().hex(),
                   mode, str(limit), str(output).encode().hex()]
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding='utf-8')
        first = ''
        try:
            if mutate:
                waiting = queue.Queue()
                reader = threading.Thread(target=lambda: waiting.put(process.stdout.readline()), daemon=True)
                reader.start()
                first = waiting.get(timeout=240)
                reader.join(timeout=1)
                self.assertTrue(first.startswith(barrier), first)
                mutate()
                stdout, stderr = process.communicate('x', timeout=240)
            else:
                stdout, stderr = process.communicate(timeout=240)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
        stdout = first + stdout
        row = {'command': command, 'exit_code': process.returncode, 'stdout': stdout,
               'stderr': stderr, 'output': str(output), 'size': output.stat().st_size,
               'sha256': digest(output)}
        self.invocations.append(row)
        (self.case / 'invocations.json').write_text(json.dumps(self.invocations, indent=2), encoding='utf-8')
        self.assertEqual(process.returncode, expected, (stdout, stderr))
        if expected == 2:
            self.assertTrue(any(line.startswith('reject 1 1 ') for line in stdout.splitlines()), stdout)
            return row
        report = next(line for line in stdout.splitlines() if line.startswith('ready ')).split()
        self.assertEqual((int(report[1]) << 32) | int(report[2]), limit)
        self.assertLessEqual(int(report[4]), 65536)
        if mode == 'hash':
            self.assertEqual(int(report[3]), 0)
            self.assertEqual(output.stat().st_size, 0)
        else:
            self.assertEqual(output.stat().st_size, limit)
            self.assertEqual(row['sha256'], report[5])
        row['reported_sha256'] = report[5]
        row['calls'] = int(report[3])
        (self.case / 'invocations.json').write_text(json.dumps(self.invocations, indent=2), encoding='utf-8')
        return row

    def test_empty_file(self):
        expected = self.fixture(0)
        row = self.invoke()
        self.assertEqual(row['reported_sha256'], expected)
        self.assertEqual(row['calls'], 0)

    def test_hash_and_stream_block_boundaries(self):
        for size in (1, 55, 56, 63, 64, 65, 65535, 65536, 65537):
            with self.subTest(size=size):
                if self.source.exists(): self.source.unlink()
                expected = self.fixture(size)
                row = self.invoke('buffer')
                self.assertEqual(row['reported_sha256'], expected)
                self.assertGreater(row['calls'], 0)

    def test_hash_without_sink(self):
        expected = self.fixture(65537)
        self.assertEqual(self.invoke('hash')['reported_sha256'], expected)

    def test_capture_exceeds_old_whole_file_limit(self):
        expected = self.fixture(64 * 1024 * 1024 + 17)
        self.assertEqual(self.invoke()['reported_sha256'], expected)

    def test_complete_active_disk_extent(self):
        expected = self.fixture(200 * 1024 * 1024)
        self.assertEqual(self.invoke()['reported_sha256'], expected)

    def test_limit_rejects_before_callback(self):
        self.fixture(65537)
        row = self.invoke(limit=65536, expected=2)
        self.assertEqual(row['size'], 0)
        self.assertIn('reject 1 1 0 0 0', row['stdout'])

    def test_high_word_size_and_limit_are_preserved(self):
        from tests import test_hosted_wide_file_io as wide
        wide.sparse_file(self.source, (1 << 32) + 65)
        for limit in (65537, (1 << 32) + 64):
            with self.subTest(limit=limit):
                row = self.invoke(limit=limit, expected=2)
                self.assertEqual(row['size'], 0)
                self.assertIn('reject 1 1 0 0 0', row['stdout'])

    def test_sink_failure_discards_result_and_poison_is_sticky(self):
        self.fixture(65537)
        for mode, size in (('fail-first', 0), ('fail-second', 65536)):
            with self.subTest(mode=mode):
                row = self.invoke(mode, expected=2)
                self.assertEqual(row['size'], size)

    def test_invalid_arguments_clear_results(self):
        self.fixture(1)
        for mode in ('null-observer', 'null-path', 'null-result'):
            with self.subTest(mode=mode): self.invoke(mode, expected=2)

    def test_unsafe_missing_and_directory_paths(self):
        self.fixture(1)
        for logical in ('', 'nested/../payload.bin', '/nested/payload.bin',
                        'nested//payload.bin', 'missing', 'nested'):
            with self.subTest(logical=logical): self.invoke(logical=logical, expected=2)

    def test_same_size_restored_timestamp_drift(self):
        self.fixture(65537)
        original = self.source.stat()
        def mutate():
            with self.source.open('r+b') as stream: stream.write(b'!')
            os.utime(self.source, ns=(original.st_atime_ns, original.st_mtime_ns))
        self.invoke('pause-after', mutate=mutate, expected=3)

    def test_content_drift_during_capture_detected_by_final_digest_check(self):
        self.fixture(65537)
        original = self.source.stat()
        def mutate():
            with self.source.open('r+b') as stream: stream.write(b'!')
            os.utime(self.source, ns=(original.st_atime_ns, original.st_mtime_ns))
        self.invoke('pause-stream', mutate=mutate, expected=3, barrier='chunk')

    def test_truncation_during_capture_clears_result(self):
        self.fixture(65537)
        def mutate():
            with self.source.open('r+b') as stream: stream.truncate(1)
        self.invoke('pause-stream', mutate=mutate, expected=2, barrier='chunk')


if __name__ == '__main__': unittest.main()

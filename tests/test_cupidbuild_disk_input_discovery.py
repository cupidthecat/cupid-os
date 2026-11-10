"""Exercise owned image inputs through native or supplied checked callers."""
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULES = ('cupidbuild_source_path', 'cupidbuild_source_file', 'cupidbuild_source_stage',
           'cupidbuild_disk_integer', 'cupidbuild_disk_options', 'cupidbuild_disk_input_discovery')


class CupidBuildDiskInputDiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-input-owner-build-')
        cls.addClassCleanup(cls.build.cleanup)
        selected = os.environ.get('CUPIDBUILD_INPUT_OWNER_PROGRAM')
        if selected:
            cls.program = Path(selected).resolve(strict=True)
            return
        compiler = shutil.which(os.environ.get('CC', 'clang' if os.name == 'nt' else 'cc'))
        if not compiler:
            raise RuntimeError('native input-owner compiler unavailable')
        cls.program = Path(cls.build.name) / ('contract.exe' if os.name == 'nt' else 'contract')
        command = [compiler, '-std=c11', '-O2', '-pedantic', '-Wall', '-Wextra', '-Werror',
                   '-Wcast-qual', '-Wconversion', '-Wsign-conversion',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', str(ROOT / 'toolchain'), '-x', 'c',
                   *(['-DCUPID_NATIVE_UTF8_ENABLE=1'] if os.name == 'nt' else []),
                   str(ROOT / 'toolchain/tests/cupidbuild_disk_input_discovery_contract.cc'),
                   *[str(ROOT / 'toolchain' / (name + '.cc')) for name in
                     (*MODULES, 'cupidbuild_host', 'path_encoding')],
                   *([str(ROOT / 'toolchain/native_utf8.cc'), '-lntdll'] if os.name == 'nt' else []),
                   '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        if (result.returncode, result.stdout, result.stderr) != (0, b'', b''):
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cupid-input-owner-case-')
        self.addCleanup(self.temporary.cleanup)
        self.top = Path(self.temporary.name)
        self.root = self.top / 'root'
        self.root.mkdir()
        for name in ('manifest.json', 'boot.bin', 'kernel.bin', 'payload.bin'):
            (self.root / name).write_bytes(b'original payload')

    @staticmethod
    def bounds():
        import resource
        resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
        resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

    def run_case(self, mode='ordinary', source='payload.bin', kind=1, roots=0,
                 expected=0, mutate=None, restore=None):
        arguments = [str(value).encode('utf-8').hex() for value in
                     (self.root, 'manifest.json', 'boot.bin', 'kernel.bin', source, '../image.bin')]
        process = subprocess.Popen([str(self.program), mode, *arguments, str(kind), str(roots)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            preexec_fn=self.bounds if os.name != 'nt' else None)
        started = time.monotonic()
        prefix = b''

        def line():
            rows = queue.Queue()
            reader = threading.Thread(target=lambda: rows.put(process.stdout.readline()), daemon=True)
            reader.start()
            return rows.get(timeout=max(0.001, 60 - (time.monotonic() - started))).replace(b'\r\n', b'\n')

        try:
            if mutate:
                prefix += line()
                self.assertEqual(prefix, b'ready\n')
                mutate()
                process.stdin.write(b'\n')
                process.stdin.flush()
                if restore:
                    received = line()
                    self.assertEqual(received, b'first-reject\n')
                    prefix += received
                    restore()
                    process.stdin.write(b'\n')
                    process.stdin.flush()
            stdout, stderr = process.communicate(timeout=max(0.001, 60 - (time.monotonic() - started)))
            stdout = stdout.replace(b'\r\n', b'\n')
            self.assertEqual((process.returncode, stdout, stderr),
                             (expected, b'accept\n' if expected == 0 else b'reject\n', b''))
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    def test_present_input_survives_argument_clobber_and_option_owner_close(self):
        self.run_case()

    def test_missing_nested_stage_retains_request_without_file_authority(self):
        self.run_case(source='missing/deeper/payload.bin', kind=0)

    def test_required_missing_file_rejects_and_clears_owner(self):
        (self.root / 'manifest.json').unlink()
        self.run_case(expected=1)

    def test_directory_stage_is_rejected(self):
        (self.root / 'folder').mkdir()
        self.run_case(source='folder', expected=1)

    def test_invalid_constructor_bounds_and_pointers_clear_owner(self):
        for mode in ('zero-quota', 'max-quota', 'quota-one', 'root-overflow',
                     'stage-overflow', 'null-stages', 'null-options', 'null-owner'):
            with self.subTest(mode=mode):
                self.run_case(mode, expected=1)

    def test_zero_external_budget_accepts_primary_descendants(self):
        self.run_case('no-external')

    def test_external_file_retains_its_own_root_and_enforces_budget(self):
        external = self.top / 'external'
        external.mkdir()
        (external / 'payload.bin').write_bytes(b'external payload')
        self.run_case(source=external / 'payload.bin', roots=1)
        self.run_case('no-external', source=external / 'payload.bin', expected=1)

    def test_null_view_permanently_poisons_owner_and_clears_next_view(self):
        self.run_case('null-view')

    def test_missing_stage_appearance_is_rejected(self):
        self.run_case('hold', source='missing.bin', kind=0, expected=1,
                      mutate=lambda: (self.root / 'missing.bin').write_bytes(b'appeared'))

    def test_streamed_same_size_edit_with_restored_timestamp_is_rejected(self):
        path = self.root / 'payload.bin'
        original = path.stat()

        def mutate():
            path.write_bytes(b'modified payload')
            os.utime(path, ns=(original.st_atime_ns, original.st_mtime_ns))

        self.run_case('hold-stream', expected=1, mutate=mutate)

    def test_metadata_drift_rejects_and_remains_poisoned_after_restore(self):
        path = self.root / 'payload.bin'
        original = path.stat()
        self.run_case('hold-recover', expected=1,
            mutate=lambda: os.utime(path, ns=(original.st_atime_ns, original.st_mtime_ns + 1000000000)),
            restore=lambda: os.utime(path, ns=(original.st_atime_ns, original.st_mtime_ns)))


if __name__ == '__main__':
    unittest.main()

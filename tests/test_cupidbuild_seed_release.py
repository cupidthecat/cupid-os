"""Explicit release authority for the real retained checked-tool runner."""
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

ROOT = Path(__file__).resolve().parents[1]
HOST = 'windows' if os.name == 'nt' else 'linux'


class CupidBuildSeedReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='.release-cli-', dir=ROOT / 'toolchain')
        cls.addClassCleanup(cls.directory.cleanup)
        build = Path(cls.directory.name)
        relative = build.relative_to(ROOT / 'toolchain').as_posix()
        cls.cli = build / ('cupidbuild.exe' if os.name == 'nt' else 'cupidbuild')
        result = subprocess.run(['make', '-C', str(ROOT / 'toolchain'), 'BUILD_DIR=' + relative,
            relative + '/' + cls.cli.name], cwd=ROOT, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.race_cli = build / 'race' / cls.cli.name
        result = subprocess.run(['make', '-C', str(ROOT / 'toolchain'), 'BUILD_DIR=' + relative + '/race',
            'CPPFLAGS=-DCUPIDBUILD_PUBLICATION_RACE_TEST', relative + '/race/' + cls.cli.name],
            cwd=ROOT, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def fixture(self, directory):
        original = ROOT / ('bootstrap/seeds/i386-' + HOST)
        seed = directory / 'seed'
        seed.mkdir()
        manifest = json.loads((original / 'manifest.json').read_bytes())
        record = json.loads((ROOT / 'bootstrap/seeds/release.json').read_bytes())
        record.update(parent_source_revision='1234567890abcdef1234567890abcdef12345678',
                      parent_linux_manifest_sha256='0123456789abcdef' * 4,
                      parent_windows_manifest_sha256='fedcba9876543210' * 4)
        provenance = manifest['provenance']
        if HOST == 'windows':
            provenance.update(
                parent_execution_seed_source_revision=record['parent_source_revision'],
                parent_execution_seed_manifest_sha256=record['parent_windows_manifest_sha256'],
                parent_plan_seed_source_revision=record['parent_source_revision'],
                parent_plan_seed_manifest_sha256=record['parent_linux_manifest_sha256'])
        else:
            provenance.update(parent_seed_source_revision=record['parent_source_revision'],
                              parent_seed_manifest_sha256=record['parent_linux_manifest_sha256'])
        for artifact in manifest['artifacts']:
            source = original / artifact['file']
            payload = source.read_bytes()
            self.assertEqual((len(payload), hashlib.sha256(payload).hexdigest()),
                             (artifact['size'], artifact['sha256']))
            shutil.copy2(source, seed / artifact['file'])
            if os.name != 'nt':
                (seed / artifact['file']).chmod(0o755)
        manifest_path = seed / 'manifest.json'
        manifest_path.write_text(json.dumps(manifest, sort_keys=True) + '\n', encoding='utf-8')
        release_path = directory / 'release.json'
        release_path.write_text(json.dumps(record, sort_keys=True) + '\n', encoding='utf-8')
        (directory / 'input.txt').write_bytes(b'first\r\nsecond\r\n')
        (directory / 'result.o').write_bytes(b'previous result')
        return manifest_path, release_path, record

    def run_tool(self, directory, manifest, options=()):
        return subprocess.run([str(self.cli), 'run', '--seed-manifest', str(manifest),
            '--root', str(directory), '--tool', 'cupidobj', *map(str, options), '--',
            'wrap-text', 'input.txt', '--identity', 'release-runner.txt', '-o', 'result.o'],
            cwd=directory, capture_output=True, text=True, timeout=30)

    def test_matching_release_authorizes_real_tool_without_changing_strict_runner(self):
        with tempfile.TemporaryDirectory(prefix='.release-run-', dir=ROOT if os.name == 'nt' else None) as temporary:
            directory = Path(temporary)
            manifest, release, _ = self.fixture(directory)
            original = {path: path.read_bytes() for path in (manifest, release)}
            result = self.run_tool(directory, manifest)
            self.assertEqual(result.returncode, 1)
            self.assertIn('fixed-point provenance differs', result.stderr)
            self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')
            result = self.run_tool(directory, manifest, ('--seed-release', release))
            self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''))
            payload = (directory / 'result.o').read_bytes()
            self.assertEqual(payload[:4], b'\x7fELF')
            self.assertIn(b'first\nsecond\n', payload)
            for path, expected in original.items():
                self.assertEqual(path.read_bytes(), expected)
            # Supplying context does not change subsequent historical calls.
            (directory / 'result.o').write_bytes(b'previous result')
            result = self.run_tool(directory, manifest)
            self.assertEqual(result.returncode, 1)
            self.assertIn('fixed-point provenance differs', result.stderr)
            self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')

    def test_changed_release_claims_and_malformed_records_preserve_prior_output(self):
        with tempfile.TemporaryDirectory(prefix='.release-run-', dir=ROOT if os.name == 'nt' else None) as temporary:
            directory = Path(temporary)
            manifest, release, record = self.fixture(directory)
            cases = [b'', b'bad', b'x' * 65537]
            for key in ('source_revision', 'source_snapshot_sha256', 'source_input_count',
                        'parent_source_revision', 'parent_linux_manifest_sha256',
                        *(['parent_windows_manifest_sha256'] if HOST == 'windows' else []),
                        'linux_plan_sha256', *(['windows_plan_sha256'] if HOST == 'windows' else [])):
                altered = copy.deepcopy(record)
                altered[key] = altered[key] + 1 if isinstance(altered[key], int) else 'a' * len(altered[key])
                cases.append((json.dumps(altered) + '\n').encode())
            changed = copy.deepcopy(record)
            row = next(row for row in changed['artifacts'] if row['format'] == ('pe32' if HOST == 'windows' else 'elf32'))
            row['size'] += 1
            cases.append(json.dumps(changed).encode())
            for payload in cases:
                with self.subTest(payload_size=len(payload)):
                    release.write_bytes(payload)
                    result = self.run_tool(directory, manifest, ('--seed-release', release))
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')
            release.unlink()
            result = self.run_tool(directory, manifest, ('--seed-release', release))
            self.assertEqual(result.returncode, 1)
            self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')

    def test_release_option_is_unique_nonempty_and_requires_a_value(self):
        with tempfile.TemporaryDirectory(prefix='.release-run-', dir=ROOT if os.name == 'nt' else None) as temporary:
            directory = Path(temporary)
            manifest, release, _ = self.fixture(directory)
            for options in (('--seed-release', release, '--seed-release', release),
                            ('--seed-release', ''), ('--seed-release',)):
                with self.subTest(options=options):
                    result = self.run_tool(directory, manifest, options)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn('usage: cupidbuild run', result.stderr)
                    self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')

    def test_release_drift_after_real_launch_suppresses_success_output(self):
        with tempfile.TemporaryDirectory(prefix='.release-run-', dir=ROOT if os.name == 'nt' else None) as temporary:
            directory = Path(temporary)
            manifest, release, _ = self.fixture(directory)
            ready, resume = directory / 'ready', directory / 'resume'
            changed = threading.Event()
            errors = []

            def mutate():
                try:
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            release.write_bytes(release.read_bytes() + b' \n')
                            changed.set()
                            return
                        time.sleep(0.001)
                    errors.append('real tool launch was not observed')
                except Exception as error:
                    errors.append(repr(error))
                finally:
                    try:
                        resume.write_bytes(b'continue')
                    except Exception as error:
                        errors.append(repr(error))

            environment = dict(os.environ)
            environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE='after-tool-launch',
                CUPIDBUILD_PUBLICATION_TEST_READY=str(ready), CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
            worker = threading.Thread(target=mutate, daemon=True)
            worker.start()
            try:
                result = subprocess.run([str(self.race_cli), 'run', '--seed-manifest', str(manifest),
                    '--seed-release', str(release), '--root', str(directory), '--tool', 'cupidobj', '--', '--help'],
                    cwd=directory, env=environment, capture_output=True, text=True, timeout=30)
            finally:
                worker.join(timeout=20)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors, [])
            self.assertTrue(changed.is_set())
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(result.stdout, '')
            self.assertIn('checked seed inputs changed', result.stderr)
            self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')
            self.assertEqual(list(directory.glob('.cupidbuild-run-*')), [])

    @unittest.skipIf(os.name == 'nt', 'POSIX link/FIFO coverage')
    def test_release_links_and_nonregular_files_fail_before_launch(self):
        with tempfile.TemporaryDirectory(prefix='.release-run-', dir=ROOT if os.name == 'nt' else None) as temporary:
            directory = Path(temporary)
            manifest, release, _ = self.fixture(directory)
            linked = directory / 'linked.json'
            linked.symlink_to(release.name)
            fifo = directory / 'fifo.json'
            os.mkfifo(fifo)
            for path in (linked, fifo, directory):
                result = self.run_tool(directory, manifest, ('--seed-release', path))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual((directory / 'result.o').read_bytes(), b'previous result')


if __name__ == '__main__':
    unittest.main()

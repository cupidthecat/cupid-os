"""Compose, project and stage in one caller; compare complete candidate images."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unicodedata
import unittest

from tools import hostbuild

ROOT = Path(__file__).resolve().parents[1]

def identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1048576): digest.update(data)
    return {'size': path.stat().st_size, 'sha256': digest.hexdigest()}

class DiskImageStageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cupid-disk-stage-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.base = Path(cls.temporary.name)
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if compiler is None: raise AssertionError('Native contract compiler unavailable')
        native = cls.base / ('disk-contract.exe' if os.name == 'nt' else 'disk-contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-D_CRT_SECURE_NO_WARNINGS', '-x', 'c', '-I', str(ROOT / 'toolchain'),
            *(str(ROOT / ('toolchain/' + name + '.cc')) for name in
                ('disk_image', 'fat16_stage', 'fat16_names')),
            str(ROOT / 'toolchain/tests/disk_stage_contract.cc'), '-o', str(native)]
        result = subprocess.run(command, capture_output=True, timeout=120)
        if result.returncode: raise AssertionError(result.stderr.decode(errors='replace'))
        cls.programs = [native]
        if value := os.environ.get('CUPID_DISK_STAGE_PROGRAM'): cls.programs.append(Path(value))
        cls.retained = Path(value) if (value := os.environ.get('CUPID_DISK_STAGE_RETAIN')) else None
        if cls.retained:
            cls.retained.mkdir(parents=True)
            shutil.copyfile(native, cls.retained / native.name)
        cls.profile = int(unicodedata.unidata_version.split('.')[0])
        if cls.profile not in (15, 16): raise AssertionError('The host oracle needs an explicit Unicode profile')

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='case-', dir=self.base)
        self.addCleanup(self.temporary.cleanup)
        self.case = Path(self.temporary.name)
        self.sectors, self.start = 16384, 32
        self.boot, self.kernel = self.case / 'boot.bin', self.case / 'kernel.bin'
        self.template, self.previous = self.case / 'template.bin', self.case / 'previous.img'
        self.boot.write_bytes(bytes(range(256)) * 12)
        self.kernel.write_bytes(b'kernel prefix\0\xff' * 117)
        hostbuild._write_pristine_disk_template(self.template, self.boot, self.kernel,
            self.sectors, self.start)
        self.inputs, self.reports = [], []

    def fresh(self, path):
        shutil.copyfile(self.template, path)
        with path.open('r+b') as stream: stream.truncate(self.sectors * 512)

    def persisted(self):
        self.fresh(self.previous)
        with hostbuild.Fat16Image(self.previous, self.start) as volume:
            volume.write_file('/KEEP/NESTED.TXT', b'persistent content\0\xff' * 73)
        with self.previous.open('r+b') as stream:
            stream.seek(-517, 2)
            stream.write(b'untouched tail bytes')

    def prepare_entries(self, entries):
        self.inputs = []
        for index, (guest, data) in enumerate(entries):
            payload, destination = self.case / ('payload-%d.bin' % index), self.case / ('destination-%d.utf8' % index)
            payload.write_bytes(data)
            destination.write_bytes(guest.encode('utf-8') if isinstance(guest, str) else guest)
            self.inputs += [payload, destination]

    def call(self, program, index, *, previous=False, force=False):
        candidate = self.case / ('actual-%d.img' % index)
        with candidate.open('wb') as stream:
            stream.truncate(self.sectors * 512)
            stream.write(b'private candidate before composition'.ljust(512, b'!'))
        before = identity(candidate)
        sources = [self.boot, self.kernel, self.template, *self.inputs]
        if previous: sources.append(self.previous)
        frozen = {path: identity(path) for path in sources if path.is_file()}
        command = [str(program), str(candidate), str(self.boot), str(self.kernel),
            str(self.template), str(self.previous) if previous else '-', str(self.sectors),
            str(self.start), str(int(force)), str(self.profile), *map(str, self.inputs)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        self.assertEqual(result.stderr, b'')
        report = {key.decode(): int(value) for key, value in re.findall(rb'(\w+)=(\d+)', result.stdout)}
        self.assertEqual(report['live'], 0)
        self.assertEqual({path: identity(path) for path in frozen}, frozen)
        self.reports.append(report)
        return candidate, before, report

    def retain(self):
        if self.retained:
            target = self.retained / self._testMethodName
            target.mkdir()
            for path in self.case.iterdir():
                if path.is_file(): shutil.copyfile(path, target / path.name)
            (target / 'reports.json').write_text(json.dumps(self.reports, indent=2) + '\n', encoding='utf-8')

    def parity(self, entries, *, previous=False, force=False):
        self.prepare_entries(entries)
        expected = self.case / 'expected.img'
        if previous and not force:
            shutil.copyfile(self.previous, expected)
            with self.template.open('rb') as source, expected.open('r+b') as destination:
                hostbuild._copy_disk_prefix(source, destination, self.start * 512)
        else: self.fresh(expected)
        with hostbuild.Fat16Image(expected, self.start) as volume:
            for guest, data in entries: volume.write_file(guest, data)
        for index, program in enumerate(self.programs):
            candidate, _, report = self.call(program, index, previous=previous, force=force)
            self.assertEqual((report['status'], report['ready'], report['reused'], report['staged']),
                (0, 1, int(previous and not force), len(entries)), report)
            self.assertEqual(identity(candidate), identity(expected))
        self.retain()

    def test_fresh_composition_stages_utf8_names_empty_files_and_parents(self):
        self.parity([('/stra\u00dfe/\ufb03le.txt', b'uppercase expansion'),
            ('/EMPTY.TXT', b''), ('//SUB///leaf.bin/', bytes(range(256)) * 5)])

    def test_reused_image_preserves_files_and_complete_tail(self):
        self.persisted()
        self.parity([('/NEW/USER.BIN', b'captured payload\0\xff' * 177)], previous=True)

    def test_repeated_replacement_grows_shrinks_and_reuses_freed_space(self):
        self.persisted()
        self.parity([('/KEEP/NESTED.TXT', b'g' * 5001), ('/OTHER.BIN', b'x' * 1500),
            ('/KEEP/NESTED.TXT', b'short'), ('/LAST.BIN', b'z' * 2500)], previous=True)

    def test_short_name_collisions_preserve_sequential_replacement(self):
        self.parity([('/abcdefghij.txt', b'first'), ('/abcdefghzz.txt', b'last')])

    def test_force_format_discards_previous_files_before_staging(self):
        self.persisted()
        self.parity([('/FRESH.BIN', b'new filesystem')], previous=True, force=True)

    def test_destination_components_have_no_fixed_sixty_four_entry_array(self):
        self.parity([('/' + '/'.join('D%02d' % index for index in range(70)) + '/F.TXT', b'deep file')])

    def test_invalid_later_destination_fails_before_any_candidate_write(self):
        self.prepare_entries([('/VALID.TXT', b'first'), ('/BAD/../LEAF', b'second')])
        for index, program in enumerate(self.programs):
            candidate, before, report = self.call(program, index)
            self.assertNotEqual(report['status'], 0)
            self.assertEqual((report['ready'], report['staged'], report['writes']), (0, 0, 0))
            self.assertEqual(identity(candidate), before)
        self.retain()

    def test_missing_later_payload_fails_before_any_candidate_write(self):
        self.prepare_entries([('/VALID.TXT', b'first'), ('/SECOND.TXT', b'second')])
        self.inputs[2].unlink()
        for index, program in enumerate(self.programs):
            candidate, before, report = self.call(program, index)
            self.assertEqual((report['status'], report['ready'], report['staged'], report['writes']), (4, 0, 0, 0))
            self.assertEqual(identity(candidate), before)
        self.retain()

    def test_template_failure_prevents_staging_and_candidate_writes(self):
        self.prepare_entries([('/VALID.TXT', b'payload')])
        with self.template.open('r+b') as stream:
            stream.seek(510); stream.write(b'\0\0')
        for index, program in enumerate(self.programs):
            candidate, before, report = self.call(program, index)
            self.assertNotEqual(report['status'], 0)
            self.assertEqual((report['ready'], report['staged'], report['writes']), (0, 0, 0))
            self.assertEqual(identity(candidate), before)
        self.retain()

    def test_stage_failure_marks_the_composed_candidate_unready(self):
        self.persisted()
        self.prepare_entries([('/FIRST.TXT', b'staged'), ('/FIRST.TXT/CHILD', b'invalid parent')])
        for index, program in enumerate(self.programs):
            _, _, report = self.call(program, index, previous=True)
            self.assertNotEqual(report['status'], 0)
            self.assertEqual((report['ready'], report['reused'], report['staged']), (0, 1, 1))
            self.assertGreater(report['writes'], self.sectors)
        self.retain()

if __name__ == '__main__': unittest.main()

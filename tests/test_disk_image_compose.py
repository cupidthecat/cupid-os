"""Compare bounded private-image composition with the production layout oracle."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

from tools import hostbuild

ROOT = Path(__file__).resolve().parents[1]
OK, INVALID, INPUT, IO = 0, 1, 2, 4


def identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1024 * 1024): digest.update(data)
    return path.stat().st_size, digest.hexdigest()


class DiskImageComposeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cupid-disk-compose-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.base = Path(cls.temporary.name)
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if compiler is None: raise AssertionError('Native compiler unavailable')
        native = cls.base / ('disk-compose.exe' if os.name == 'nt' else 'disk-compose')
        result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-D_CRT_SECURE_NO_WARNINGS', '-x', 'c', '-I', str(ROOT / 'toolchain'),
            str(ROOT / 'toolchain/disk_image.cc'),
            str(ROOT / 'toolchain/tests/disk_image_contract.cc'), '-o', str(native)],
            capture_output=True, timeout=120)
        if result.returncode: raise AssertionError(result.stderr.decode(errors='replace'))
        cls.programs = [native]
        if checked := os.environ.get('CUPID_DISK_COMPOSE_PROGRAM'): cls.programs.append(Path(checked))
        cls.retained = Path(value) if (value := os.environ.get('CUPID_DISK_COMPOSE_RETAIN')) else None
        if cls.retained:
            cls.retained.mkdir(parents=True)
            shutil.copyfile(native, cls.retained / native.name)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='case-', dir=self.base)
        self.addCleanup(self.temporary.cleanup)
        self.case = Path(self.temporary.name)
        self.sectors, self.start = 16384, 32
        self.retention_index = 0
        self.boot, self.kernel = self.case / 'boot.bin', self.case / 'kernel.bin'
        self.template, self.previous = self.case / 'template.bin', self.case / 'previous.img'
        self.boot.write_bytes(bytes(range(256)) * 12)
        self.kernel.write_bytes(b'kernel bytes\x00\xff' * 103)
        self.make_template()

    def make_template(self):
        if self.template.exists(): self.template.unlink()
        hostbuild._write_pristine_disk_template(self.template, self.boot, self.kernel,
                                               self.sectors, self.start)

    def fresh(self, output):
        shutil.copyfile(self.template, output)
        with output.open('r+b') as stream: stream.truncate(self.sectors * 512)

    def persisted(self):
        self.fresh(self.previous)
        with hostbuild.Fat16Image(self.previous, self.start) as volume:
            volume.write_file('/KEEP/NESTED.TXT', b'persisted data\0\xff' * 177)
        with self.previous.open('r+b') as stream:
            stream.seek(-513, 2)
            stream.write(b'tail survives both copies')

    def call(self, program, candidate, *, previous=False, force=False, mode='none', fail=0):
        result = subprocess.run([str(program), str(candidate), str(self.boot), str(self.kernel),
            str(self.template), str(self.previous) if previous else '-', str(self.sectors),
            str(self.start), str(int(force)), mode, str(fail)], capture_output=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual(result.stderr, b'')
        return {key.decode(): int(value) for key, value in re.findall(rb'(\w+)=(\d+)', result.stdout)}

    def candidate(self, index):
        output = self.case / ('actual-' + str(index) + '.img')
        with output.open('wb') as stream:
            stream.truncate(self.sectors * 512)
            stream.write(b'unchanged private candidate'.ljust(512, b'!'))
        return output

    def parity(self, *, previous=False, reuse=False, force=False):
        expected = self.case / 'expected.img'
        if reuse:
            shutil.copyfile(self.previous, expected)
            with self.template.open('rb') as source, expected.open('r+b') as destination:
                hostbuild._copy_disk_prefix(source, destination, self.start * 512)
        else: self.fresh(expected)
        before = identity(self.previous) if previous else None
        for index, program in enumerate(self.programs):
            output = self.candidate(index)
            report = self.call(program, output, previous=previous, force=force)
            self.assertEqual((report['status'], report['ready'], report['reused'], report['dirty']),
                             (OK, 1, int(reuse), 1), report)
            self.assertEqual(report['writes'], self.sectors)
            self.assertEqual(identity(output), identity(expected))
            if previous: self.assertEqual(identity(self.previous), before)
        if self.retained:
            directory = self.retained / self._testMethodName / ('scenario-%02d' % self.retention_index)
            self.retention_index += 1
            directory.mkdir(parents=True)
            for path in [expected, *(self.case / ('actual-' + str(i) + '.img') for i in range(len(self.programs)))]:
                shutil.copyfile(path, directory / path.name)

    def reject(self, status, *, previous=False, mode='none', fail=0, dirty=False):
        for index, program in enumerate(self.programs):
            output = self.candidate(index)
            before = identity(output)
            report = self.call(program, output, previous=previous, mode=mode, fail=fail)
            self.assertEqual((report['status'], report['ready'], report['dirty']), (status, 0, int(dirty)), report)
            if not dirty:
                self.assertEqual(report['writes'], 0)
                self.assertEqual(identity(output), before)
        return report

    def test_fresh_image_matches_every_byte(self):
        self.parity()

    def test_reuse_preserves_files_fat_and_tail(self):
        self.persisted()
        self.parity(previous=True, reuse=True)

    def test_shorter_kernel_zeroes_previous_prefix(self):
        self.kernel.write_bytes(b'long kernel' * 401)
        self.make_template()
        self.persisted()
        self.kernel.write_bytes(b'short')
        self.make_template()
        self.parity(previous=True, reuse=True)

    def test_force_format_discards_previous_files(self):
        self.persisted()
        self.parity(previous=True, force=True)

    def test_invalid_previous_image_reformats(self):
        self.persisted()
        with self.previous.open('r+b') as stream:
            stream.seek(450)
            stream.write(b'\x83')
        self.parity(previous=True)

    def test_wrong_previous_extent_reformats(self):
        self.previous.write_bytes(b'invalid short image')
        self.parity(previous=True)

    def test_accepted_alternate_fat_types_preserve_previous(self):
        self.persisted()
        for kind in (4, 14):
            with self.previous.open('r+b') as stream:
                stream.seek(450); stream.write(bytes([kind]))
            self.assertTrue(hostbuild._valid_existing_image(self.previous, 8, self.start))
            self.parity(previous=True, reuse=True)

    def test_all_template_regions_are_checked_before_writes(self):
        original = self.template.read_bytes()
        layout = hostbuild._choose_layout(self.sectors - self.start)
        offsets = [0, 445, 446, 450, 454, 458, 462, 510, 512, 5 * 512,
                   5 * 512 + self.kernel.stat().st_size, self.start * 512,
                   (self.start + 1) * 512, (self.start + 1 + layout.sectors_per_fat) * 512,
                   len(original) - 1]
        for offset in offsets:
            with self.subTest(offset=offset):
                changed = bytearray(original); changed[offset] ^= 1
                self.template.write_bytes(changed)
                self.reject(INPUT)
        self.template.write_bytes(original)

    def test_short_and_extra_templates_fail_before_writes(self):
        original = self.template.read_bytes()
        for data in (original[:-1], original + b'x', original[:-512], original + bytes(512)):
            self.template.write_bytes(data)
            self.reject(INPUT)

    def test_invalid_previous_geometry_reformats(self):
        self.persisted()
        original = self.previous.read_bytes()
        changes = [(11, b'\0\x04'), (13, b'\0'), (13, b'\3'), (13, b'\x80'),
                   (14, b'\0\0'), (16, b'\3'), (17, b'\0\0'), (19, b'\0\0'),
                   (22, b'\0\0'), (22, b'\1\0'), (28, bytes(4)), (510, b'\0\0')]
        for offset, data in changes:
            with self.subTest(offset=offset, data=data):
                self.previous.write_bytes(original)
                with self.previous.open('r+b') as stream:
                    stream.seek(self.start * 512 + offset); stream.write(data)
                self.assertFalse(hostbuild._valid_existing_image(self.previous, 8, self.start))
                self.parity(previous=True)

    def test_read_failures_before_and_after_writes(self):
        self.persisted()
        for mode, fail, dirty in [('boot', 1, False), ('kernel', 1, False),
                                   ('template', 1, False), ('previous', 1, False),
                                   ('previous', 2, False), ('previous', 3, True),
                                   ('template', self.template.stat().st_size // 512 + 1, False),
                                   ('template', self.template.stat().st_size // 512 + 2, True)]:
            with self.subTest(mode=mode, fail=fail):
                self.reject(IO, previous=True, mode=mode, fail=fail, dirty=dirty)

    def test_failed_and_partial_writes_require_discard(self):
        for mode in ('write', 'partial'):
            for fail in (1, 2, self.start, self.start + 1, self.sectors):
                with self.subTest(mode=mode, fail=fail):
                    report = self.reject(IO, mode=mode, fail=fail, dirty=True)
                    self.assertEqual(report['writes'], fail)

    def test_argument_failures_do_not_truncate_wide_sizes(self):
        for mode in ('wide-kernel', 'wrong-capacity', 'no-writer', 'null-request', 'null-result', 'no-template-reader'):
            with self.subTest(mode=mode): self.reject(INVALID, mode=mode)
        self.boot.write_bytes(b'boot too small')
        self.reject(INVALID)

    def test_kernel_overlap_and_partition_bounds_fail(self):
        self.kernel.write_bytes(b'x' * ((self.start - 5) * 512 + 1))
        self.reject(INVALID)
        self.kernel.write_bytes(b'')
        for self.start in (0, 5, self.sectors, 0xffffffff): self.reject(INVALID)

    def test_empty_kernel_and_longer_boot_input_preserve_layout(self):
        self.kernel.write_bytes(b'')
        self.make_template()
        self.parity()

    def test_checked_cupidobj_template_and_complete_200_mib_reuse(self):
        self.sectors, self.start = 200 * 1024 * 1024 // 512, 20480
        self.make_template()
        host = 'i386-windows' if os.name == 'nt' else 'i386-linux'
        suffix = '.exe' if os.name == 'nt' else '.elf'
        tool = ROOT / 'bootstrap/seeds' / host / ('cupidobj' + suffix)
        checked = self.case / 'checked-template.bin'
        result = subprocess.run([str(tool), 'disk-template', str(self.boot), '--kernel', str(self.kernel),
            '--image-sectors', str(self.sectors), '--fat-start-lba', str(self.start), '-o', str(checked)],
            capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual(identity(checked), identity(self.template))
        self.template.write_bytes(checked.read_bytes())
        self.persisted()
        self.parity(previous=True, reuse=True)


if __name__ == '__main__': unittest.main()

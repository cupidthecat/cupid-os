"""Compare bounded native FAT16 staging with the retained image writer."""
import hashlib
from dataclasses import replace
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
OK, INVALID, INPUT, IO, MEMORY, LIMIT, PATH = 0, 1, 2, 4, 5, 6, 8


def identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while data := stream.read(1024 * 1024):
            digest.update(data)
    return path.stat().st_size, digest.hexdigest()


class Fat16StageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='cupid-fat16-stage-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.base = Path(cls.directory.name)
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if compiler is None:
            raise AssertionError('Native contract compiler is unavailable')
        cls.native = cls.base / ('contract.exe' if os.name == 'nt' else 'contract')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-x', 'c', '-I', str(ROOT / 'toolchain'),
                   str(ROOT / 'toolchain/fat16_stage.cc'),
                   str(ROOT / 'toolchain/tests/fat16_stage_contract.cc'), '-o', str(cls.native)]
        result = subprocess.run(command, capture_output=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        cls.programs = [cls.native]
        retained = os.environ.get('CUPID_FAT16_STAGE_RETAIN')
        cls.retained = Path(retained) if retained else None
        if cls.retained:
            cls.retained.mkdir(parents=True)
            shutil.copyfile(cls.native, cls.retained / cls.native.name)
        checked = os.environ.get('CUPID_FAT16_STAGE_PROGRAM')
        if checked:
            cls.programs.append(Path(checked))

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='case-', dir=self.base)
        self.addCleanup(self.directory.cleanup)
        self.case = Path(self.directory.name)
        self.start = 32
        self.sectors = 16384
        self.image = self.case / 'input.img'
        self.make_image()

    def make_image(self):
        self.layout = hostbuild._choose_layout(self.sectors - self.start)
        with self.image.open('wb') as stream:
            stream.truncate(self.sectors * 512)
            stream.write(b'prefix survives staging'.ljust(512, b'!'))
            hostbuild._write_fat16_filesystem(stream, self.start, self.layout)
        self.root_offset = (self.start + self.layout.reserved_sectors +
                            self.layout.num_fats * self.layout.sectors_per_fat) * 512
        self.fat_offset = (self.start + self.layout.reserved_sectors) * 512

    def edit(self, offset, data):
        with self.image.open('r+b') as stream:
            stream.seek(offset)
            stream.write(data)

    def fat(self, cluster, value, copies=None):
        for copy in range(self.layout.num_fats) if copies is None else copies:
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + cluster * 2,
                      struct.pack('<H', value))

    def call(self, program, image, names, payload=b'', mode='none', fail=0):
        source = self.case / 'payload.bin'
        source.write_bytes(payload)
        command = [str(program), str(image), str(self.start), str(source), mode,
                   str(fail), str(len(names)), *(name.hex() for name in names)]
        result = subprocess.run(command, capture_output=True, timeout=40)
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        self.assertEqual(result.stderr, b'')
        report = {name.decode(): int(value) for name, value in re.findall(rb'(\w+)=(\d+)', result.stdout)}
        self.assertEqual(report['live'], 0)
        return report

    def names(self, path):
        return [hostbuild.Fat16Image._short_name(part) for part in path.replace('\\', '/').split('/') if part]

    def parity(self, operations):
        expected = self.case / 'expected.img'
        shutil.copyfile(self.image, expected)
        with hostbuild.Fat16Image(expected, self.start) as volume:
            for path, payload in operations:
                volume.write_file(path, payload)
        for index, program in enumerate(self.programs):
            actual = self.case / ('actual-' + str(index) + '.img')
            shutil.copyfile(self.image, actual)
            for path, payload in operations:
                report = self.call(program, actual, self.names(path), payload)
                self.assertEqual((report['open'], report['stage']), (OK, OK), report)
            self.assertEqual(identity(actual), identity(expected))
        if self.retained:
            directory = self.retained / self._testMethodName
            directory.mkdir()
            shutil.copyfile(expected, directory / 'expected.img')
            for index in range(len(self.programs)):
                shutil.copyfile(self.case / ('actual-' + str(index) + '.img'),
                                directory / ('actual-' + str(index) + '.img'))
        return expected

    def rejection(self, expected, names=None, payload=b'new bytes', mode='none', fail=0,
                  opening=False, preserved=True, poisoned=False):
        names = self.names('/NEW.TXT') if names is None else names
        before = identity(self.image)
        for index, program in enumerate(self.programs):
            actual = self.case / ('rejected-' + str(index) + '.img')
            shutil.copyfile(self.image, actual)
            report = self.call(program, actual, names, payload, mode, fail)
            self.assertEqual(report['open' if opening else 'stage'], expected, report)
            if not opening:
                self.assertEqual(report['open'], OK, report)
            if poisoned:
                self.assertEqual(report['repeat'], INPUT, report)
            if preserved:
                self.assertEqual(identity(actual), before)
        return report

    def test_empty_file_uses_no_cluster(self):
        expected = self.parity([('/EMPTY.TXT', b'')])
        with hostbuild.Fat16Image(expected, self.start) as volume:
            index, data, _ = volume._find_entry(None, b'EMPTY   TXT')
            self.assertEqual(struct.unpack_from('<H', data, index + 26)[0], 0)

    def test_partial_sector_and_cluster_padding(self):
        self.parity([('/ONE.BIN', bytes(range(256)) * 3 + b'last')])

    def test_exact_cluster_and_multi_cluster_files(self):
        size = self.layout.sectors_per_cluster * 512
        self.parity([('/EXACT.BIN', b'x' * size), ('/CHAIN.BIN', b'abc' * (size + 1))])

    def test_nested_parents_and_dot_entries(self):
        self.parity([('/ONE/TWO/THREE/PAYLOAD.BIN', b'nested payload')])

    def test_repeated_parent_names(self):
        self.parity([('/ONE/ONE/ONE/PAYLOAD.BIN', b'repeated')])

    def test_replacements_grow_shrink_and_empty(self):
        self.parity([('/DATA.BIN', b'a' * 4000), ('/DATA.BIN', b'b' * 9000),
                     ('/DATA.BIN', b'c'), ('/DATA.BIN', b'')])

    def test_lowest_free_holes_and_fat_sector_boundary(self):
        for cluster in range(2, 270):
            self.fat(cluster, 0xffff)
        for cluster in (7, 255, 256, 269):
            self.fat(cluster, 0)
        self.parity([('/HOLES.BIN', b'x' * 1800)])

    def test_deleted_directory_slot_and_long_name_projection(self):
        self.edit(self.root_offset, b'\xe5' + b'deleted'.ljust(31, b'!'))
        self.parity([('/very-long-source-name.extension', b'projected')])

    def test_existing_long_filename_slots_are_preserved(self):
        entry = bytearray(b'Q' * 32)
        entry[11] = 0x0f
        self.edit(self.root_offset, entry)
        self.parity([('/NEW.TXT', b'keep lfn')])

    def test_dirty_reserved_entry_flags_are_preserved(self):
        self.fat(1, 0x3fff)
        self.parity([('/DIRTY.TXT', b'stage')])

    def test_complete_200_mib_image_and_large_payload(self):
        self.start = 20480
        self.sectors = 200 * 1024 * 1024 // 512
        self.make_image()
        self.parity([('/EXISTING/KEEP.TXT', b'persistent'),
                     ('/DOOM/FREEDOOM.WAD', bytes(range(256)) * 32769)])

    def test_invalid_geometry_is_rejected_without_writes(self):
        original = self.image.read_bytes()
        mutations = [(510, b'\0\0'), (11, struct.pack('<H', 1024)), (13, b'\0'),
                     (13, b'\3'), (14, b'\0\0'), (16, b'\0'), (17, b'\0\0'),
                     (22, b'\0\0'), (22, b'\1\0'), (19, b'\1\0'),
                     (19, struct.pack('<H', 65535))]
        for offset, data in mutations:
            with self.subTest(offset=offset, data=data):
                self.image.write_bytes(original)
                self.edit(self.start * 512 + offset, data)
                self.rejection(INPUT, opening=True)

    def test_mismatched_reserved_fat_copies_are_rejected(self):
        self.fat(1, 0x3fff, copies=[1])
        self.rejection(INPUT, opening=True)

    def test_mismatched_allocation_fat_sector_is_rejected(self):
        for cluster in range(2, 256):
            self.fat(cluster, 0xffff)
        self.fat(260, 0xffff, copies=[1])
        self.rejection(INPUT)

    def test_full_partition_preserves_both_fats(self):
        used = b'\xff\xff' * self.layout.cluster_count
        for copy in range(self.layout.num_fats):
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + 4, used)
        self.rejection(LIMIT)

    def test_full_root_directory_preserves_image(self):
        entries = bytearray(self.layout.root_entries * 32)
        for index in range(self.layout.root_entries):
            entries[index * 32:index * 32 + 11] = ('%08dTXT' % index).encode()
            entries[index * 32 + 11] = 0x20
        self.edit(self.root_offset, entries)
        self.rejection(LIMIT)

    def test_parent_file_is_rejected(self):
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.write_file('/PARENT', b'file')
        self.rejection(INPUT, self.names('/PARENT/CHILD.TXT'))

    def test_replacing_directory_with_file_is_rejected(self):
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.mkdir('/DIR')
        self.rejection(INPUT, self.names('/DIR'))

    def test_invalid_short_names_fail_before_parent_creation(self):
        for bad in (b'.          ', b'..         ', b'BAD/NAME   ', b'BAD   X TXT',
                    b'lower   TXT', b'        TXT', b'\0BAD    TXT', b'BAD     T.X'):
            with self.subTest(name=bad):
                self.rejection(PATH, [b'NEW        ', bad])

    def test_corrupt_file_chains_fail_before_freeing(self):
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.write_file('/OLD.TXT', b'original')
        original = self.image.read_bytes()
        for value in (0, 1, self.layout.cluster_count + 2, 0xfff0, 0xfff7, 2):
            with self.subTest(next=value):
                self.image.write_bytes(original)
                self.fat(2, value)
                self.rejection(INPUT, self.names('/OLD.TXT'))

    def test_two_cluster_cycle_is_rejected(self):
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.write_file('/OLD.TXT', b'original' * 150)
        self.fat(3, 2)
        self.rejection(INPUT, self.names('/OLD.TXT'))

    def test_bad_directory_cluster_is_rejected(self):
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.mkdir('/DIR')
            index, _, _ = volume._find_entry(None, b'DIR        ')
        self.edit(self.root_offset + index + 26, b'\1\0')
        self.rejection(INPUT, self.names('/DIR/NEW.TXT'))

    def test_allocation_failure_leaves_image_unchanged(self):
        self.rejection(MEMORY, opening=True, mode='allocation')

    def test_mount_read_failures_leave_image_unchanged(self):
        for fail in (1, 2, 3):
            with self.subTest(read=fail):
                self.rejection(IO, opening=True, mode='read', fail=fail)

    def test_every_write_failure_poisons_candidate(self):
        names = self.names('/NEW.TXT')
        trial = self.case / 'trial.img'
        shutil.copyfile(self.image, trial)
        report = self.call(self.native, trial, names, b'new bytes')
        for mode in ('write', 'partial'):
            for fail in range(1, report['writes'] + 1):
                with self.subTest(mode=mode, write=fail):
                    self.rejection(IO, names, mode=mode, fail=fail,
                                   preserved=False, poisoned=True)

    def test_payload_failure_poisons_candidate(self):
        self.rejection(IO, payload=b'x' * 1200, mode='payload', fail=2,
                       preserved=False, poisoned=True)

    def test_last_data_cluster_is_used_before_fat_padding(self):
        used = b'\xff\xff' * (self.layout.cluster_count - 1)
        for copy in range(self.layout.num_fats):
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + 4, used)
        self.parity([('/LAST.TXT', b'last data cluster')])

    def test_full_child_directory_is_rejected_without_allocation(self):
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.mkdir('/DIR')
            for index in range(self.layout.sectors_per_cluster * 16 - 2):
                volume.write_file('/DIR/%08d.TXT' % index, b'')
        self.rejection(LIMIT, self.names('/DIR/NEW.TXT'))

    def test_exhaustion_after_partial_allocation_poisons_candidate(self):
        used = b'\xff\xff' * (self.layout.cluster_count - 1)
        for copy in range(self.layout.num_fats):
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + 6, used)
        self.rejection(LIMIT, payload=b'x' * (self.layout.sectors_per_cluster * 512 + 1),
                       preserved=False, poisoned=True)

    def test_volume_label_cannot_be_replaced_by_file(self):
        entry = bytearray(32)
        entry[:11] = b'VOLUME     '
        entry[11] = 8
        self.edit(self.root_offset, entry)
        self.rejection(INPUT, self.names('/VOLUME'))

    def test_old_chain_copy_mismatch_is_rejected_before_freeing(self):
        used = b'\xff\xff' * 254
        for copy in range(self.layout.num_fats):
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + 4, used)
        with hostbuild.Fat16Image(self.image, self.start) as volume:
            volume.write_file('/OLD.TXT', b'original')
        self.fat(256, 0, copies=[1])
        self.rejection(INPUT, self.names('/OLD.TXT'))

    def test_every_stage_read_failure_has_correct_recovery_state(self):
        names = self.names('/NEW.TXT')
        trial = self.case / 'trial.img'
        shutil.copyfile(self.image, trial)
        report = self.call(self.native, trial, names, b'new bytes')
        expected = identity(trial)
        for program_index, program in enumerate(self.programs):
            for fail in range(4, report['reads'] + 1):
                with self.subTest(program=str(program), read=fail):
                    actual = self.case / ('read-failure-%d-%d.img' % (program_index, fail))
                    shutil.copyfile(self.image, actual)
                    failed = self.call(program, actual, names, b'new bytes', 'read', fail)
                    self.assertEqual((failed['open'], failed['stage']), (OK, IO), failed)
                    if failed['repeat'] == OK:
                        self.assertEqual(identity(actual), expected)
                    else:
                        self.assertEqual(failed['repeat'], INPUT)
                        self.assertGreater(failed['writes'], 0)

    def make_high_cluster_image(self):
        count = 65524
        total = count + 1 + 2 * 256 + 32
        self.sectors = self.start + total
        self.layout = replace(self.layout, partition_sectors=total,
            sectors_per_cluster=1, reserved_sectors=1, num_fats=2, root_entries=512,
            root_dir_sectors=32, sectors_per_fat=256, data_sectors=count, cluster_count=count)
        with self.image.open('wb') as stream:
            stream.truncate(self.sectors * 512)
            hostbuild._write_fat16_filesystem(stream, self.start, self.layout)
        self.fat_offset = (self.start + 1) * 512
        self.root_offset = (self.start + 1 + 2 * 256) * 512

    def test_last_nonreserved_cluster_can_be_allocated(self):
        self.make_high_cluster_image()
        used = b'\xff\xff' * (0xffef - 2)
        for copy in range(self.layout.num_fats):
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + 4, used)
        expected = self.parity([('/LAST.TXT', b'last usable cluster')])
        with hostbuild.Fat16Image(expected, self.start) as volume:
            index, data, _ = volume._find_entry(None, b'LAST    TXT')
            self.assertEqual(struct.unpack_from('<H', data, index + 26)[0], 0xffef)

    def test_reserved_cluster_numbers_are_not_free_space(self):
        self.make_high_cluster_image()
        used = b'\xff\xff' * (0xfff0 - 2)
        for copy in range(self.layout.num_fats):
            self.edit(self.fat_offset + copy * self.layout.sectors_per_fat * 512 + 4, used)
        self.rejection(LIMIT)
        oracle = self.case / 'old-writer-reserved.img'
        shutil.copyfile(self.image, oracle)
        with hostbuild.Fat16Image(oracle, self.start) as volume:
            volume.write_file('/NEW.TXT', b'new bytes')
            index, data, _ = volume._find_entry(None, b'NEW     TXT')
            self.assertEqual(struct.unpack_from('<H', data, index + 26)[0], 0xfff0)


if __name__ == '__main__':
    unittest.main()

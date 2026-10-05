"""Run the active static FAT allocator against controlled sector callbacks."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ('fat16_read_fat_entry_checked', 'fat16_read_fat_entry',
             'fat16_write_fat_entry', 'fat16_alloc_cluster')
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)

def capture_allocator(root, destination):
    source = (root / 'kernel/fs/fat16.cc').read_text(encoding='utf-8')
    captured = []
    for name in FUNCTIONS:
        match = re.search(r'^static [^\n]*\b' + re.escape(name) + r'\(', source, re.M)
        if match is None:
            raise AssertionError('Active FAT helper is missing: ' + name)
        depth = 0
        entered = False
        for token in TOKENS.finditer(source, match.start()):
            if token.group() == '{':
                depth += 1
                entered = True
            elif token.group() == '}':
                depth -= 1
                if entered and depth == 0:
                    captured.append(source[match.start():token.end()])
                    break
        else:
            raise AssertionError('Active FAT helper body is incomplete: ' + name)
    destination.mkdir(parents=True)
    (destination / 'fat16_allocation_source.inc').write_text('\n'.join(captured) + '\n', encoding='utf-8')
    for name, source_path in (
        ('contract.cc', 'toolchain/tests/fat16_allocation_contract.cc'),
        ('fat16.h', 'kernel/fs/fat16.h'), ('blockdev.h', 'kernel/fs/blockdev.h'),
        ('types.h', 'kernel/core/types.h')):
        shutil.copyfile(root / source_path, destination / name)
    return hashlib.sha256(source.encode('utf-8')).hexdigest()

class Fat16AllocationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='cupid-fat-allocation-')
        cls.addClassCleanup(cls.directory.cleanup)
        root = Path(cls.directory.name)
        capture_allocator(ROOT, root / 'source')
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if compiler is None:
            raise AssertionError('Native contract compiler is unavailable')
        native = root / ('contract.exe' if os.name == 'nt' else 'contract')
        result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-x', 'c', '-I', str(root / 'source'),
            str(root / 'source/contract.cc'), '-o', str(native)], capture_output=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        cls.programs = [native]
        checked = os.environ.get('CUPID_FAT_ALLOCATION_PROGRAM')
        if checked:
            cls.programs.append(Path(checked))

    def check(self, mode):
        for program in self.programs:
            with self.subTest(program=str(program)):
                result = subprocess.run([str(program), mode], capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
                self.assertEqual(result.stderr, b'')
                if mode == 'a':
                    self.assertIn(b'table_fnv=3810730861', result.stdout)

    def test_complete_container_allocation_has_bounded_sector_reads(self): self.check('a')
    def test_lowest_free_holes_are_selected_in_order(self): self.check('b')
    def test_reserved_entries_are_never_selected(self): self.check('c')
    def test_sector_boundary_entries_are_allocated(self): self.check('d')
    def test_last_data_cluster_and_tail_padding(self): self.check('e')
    def test_full_volume_preserves_both_fats(self): self.check('f')
    def test_scan_read_failure_stops_before_any_write(self): self.check('g')
    def test_mark_read_failure_preserves_free_entry(self): self.check('h')
    def test_first_fat_write_failure_preserves_both_copies(self): self.check('i')
    def test_second_fat_write_failure_returns_failure(self): self.check('j')
    def test_invalid_geometry_fails_before_sector_access(self): self.check('k')
    def test_bad_and_end_of_chain_entries_are_preserved(self): self.check('l')
    def test_freed_earlier_cluster_is_reused(self): self.check('m')

if __name__ == '__main__':
    unittest.main()

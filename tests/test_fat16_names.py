"""Exercise guest-name projection through native and Cupid-built callers."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unicodedata
import unittest

from tools import hostbuild

ROOT = Path(__file__).resolve().parents[1]
OK, INVALID, LIMIT, PATH = 0, 1, 6, 8
SENTINEL = b'\xa5' * 11


class Fat16NamesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cupid-fat16-names-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.base = Path(cls.temporary.name)
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if compiler is None:
            raise AssertionError('Native contract compiler is unavailable')
        native = cls.base / ('fat16-names.exe' if os.name == 'nt' else 'fat16-names')
        result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-D_CRT_SECURE_NO_WARNINGS', '-x', 'c', '-I', str(ROOT / 'toolchain'),
            str(ROOT / 'toolchain/fat16_names.cc'),
            str(ROOT / 'toolchain/tests/fat16_names_contract.cc'), '-o', str(native)],
            capture_output=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace'))
        cls.programs = [native]
        if checked := os.environ.get('CUPID_FAT16_NAMES_PROGRAM'):
            cls.programs.append(Path(checked))
        cls.profile = int(unicodedata.unidata_version.split('.')[0])
        if cls.profile not in (15, 16):
            raise AssertionError('The actual oracle must carry Unicode 15 or 16')
        cls.retained = Path(value) if (value := os.environ.get('CUPID_FAT16_NAMES_RETAIN')) else None
        if cls.retained:
            cls.retained.mkdir(parents=True)
            shutil.copyfile(native, cls.retained / native.name)

    def check(self, cases):
        directory = self.base / self._testMethodName
        directory.mkdir()
        request = bytearray()
        expected = bytearray()
        for mode, profile, flags, payload, capacity, status, count, names in cases:
            request += struct.pack('<5I', mode, profile, flags, len(payload), capacity) + payload
            stored = 1 if mode == 0 or capacity > 4096 else capacity
            result = b''.join(names) + SENTINEL * (stored - len(names))
            self.assertEqual(len(result), stored * 11)
            unchanged = int(result == SENTINEL * stored)
            expected += struct.pack('<3I', status, count, unchanged) + result
        source = directory / 'request.bin'
        source.write_bytes(request)
        (directory / 'expected.bin').write_bytes(expected)
        for index, program in enumerate(self.programs):
            output = directory / ('actual-' + str(index) + '.bin')
            result = subprocess.run([str(program), str(source), str(output)],
                                    capture_output=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertEqual(result.stdout, b'')
            self.assertEqual(result.stderr, b'')
            self.assertEqual(output.read_bytes(), expected)
        if self.retained:
            shutil.copytree(directory, self.retained / self._testMethodName)

    def component(self, text, profile=None):
        try:
            names, status = [hostbuild.Fat16Image._short_name(text)], OK
        except (UnicodeError, ValueError):
            names, status = [], PATH
        return 0, profile or self.profile, 0, text.encode('utf-8'), 1, status, 0, names

    def destination(self, text, capacity=64, profile=None):
        names = []
        status, count = OK, 0
        try:
            if not text.startswith('/'):
                raise ValueError('relative guest path')
            parts = [part for part in text.replace('\\', '/').split('/') if part]
            if not parts:
                raise ValueError('empty guest path')
            names = [hostbuild.Fat16Image._short_name(part) for part in parts]
            count = len(names)
            if count > capacity:
                names, status = [], LIMIT
        except (UnicodeError, ValueError):
            names, status, count = [], PATH, 0
        return 1, profile or self.profile, 0, text.encode('utf-8'), capacity, status, count, names

    def test_ascii_character_filter_and_both_profiles(self):
        cases = []
        for profile in (15, 16):
            for codepoint in range(1, 128):
                if chr(codepoint) not in '/\\':
                    cases.append(self.component(chr(codepoint) + 'x.bin', profile))
        self.check(cases)

    def test_stem_extension_and_final_dot_boundaries(self):
        cases = []
        for profile in (15, 16):
            for text in ('a', 'abc.', 'a.b.c', '.hidden', '..file', 'a..b', '...x',
                         'abcdefgh', 'abcdefghi', 'abcdefghij', 'abcdefghi.long',
                         'x.abc', 'x.abcd', 'ab c+?d-e_f.$%!', '$%\'_-@~`!(){}^#&.txt'):
                cases.append(self.component(text, profile))
        self.check(cases)

    def test_empty_and_filtered_names_fail_without_output(self):
        self.check([self.component(text) for text in ('', '.', '..', '...', ' ', '+?', '.abc', '?.txt')])

    def test_unicode_expansion_filtering_and_truncation(self):
        cases = []
        for text in ('straße.bin', 'ıſ.txt', 'ﬃﬄ.dat', 'x😀.txt', 'e\u0301.txt',
                     'café.txt', '日本.txt', 'x.é', 'abcdeféyz', 'abcdefghi.abcé',
                     'abcdef😀yz', 'abcdefßyz', 'abcdefé', 'abcdefgéh',
                     'abcdefégh', 'abcdefégh.abc', 'x.ﬃ', 'x.aﬃ', 'x.abc日本'):
            cases.append(self.component(text))
        self.check(cases)

    def test_actual_unicode_transform_range_frontier(self):
        # Inspect every scalar through the actual independent Python transform.
        # Exercise each transform range's endpoints, midpoint and outside neighbors.
        points = set()
        previous, start = None, 128
        allowed = "$%'-_@~`!(){}^#&"
        for codepoint in range(128, 0x110001):
            mapping = None
            if codepoint < 0x110000 and not 0xd800 <= codepoint <= 0xdfff:
                mapping = tuple(ord(ch) if ord(ch) < 128 else 128
                    for ch in chr(codepoint).upper() if ch.isalnum() or ch in allowed)
            if mapping != previous:
                if codepoint > start:
                    points.update((start, codepoint - 1, (start + codepoint - 1) // 2))
                    if start > 128: points.add(start - 1)
                    if codepoint < 0x110000: points.add(codepoint)
                start, previous = codepoint, mapping
        cases = []
        for point in sorted(points):
            if 0xd800 <= point <= 0xdfff:
                continue
            character = chr(point)
            for text in (character + 'x.txt', 'abcdef' + character + 'yz',
                         'x.ab' + character + 'z'):
                cases.append(self.component(text))
        self.assertGreater(len(cases), 7000)
        self.check(cases)

    def test_explicit_unicode_version_difference(self):
        # U+1C89 was unassigned in Unicode 15 and is a Cyrillic letter in 16.
        old, new = 15, 16
        character = '\u1c89'
        cases = [
            (0, old, 0, (character + 'x').encode(), 1, OK, 0, [b'X          ']),
            (0, new, 0, (character + 'x').encode(), 1, PATH, 0, []),
            (0, old, 0, ('abcdef' + character + 'yz').encode(), 1, OK, 0, [b'ABCDEFYZ   ']),
            (0, new, 0, ('abcdef' + character + 'yz').encode(), 1, OK, 0, [b'ABCDEF~1   ']),
        ]
        self.check(cases)

    def test_separator_normalization_and_repeated_parents(self):
        self.check([self.destination(text) for text in (
            '/file.bin', '//a///b//file.bin///', '/a\\b\\file.bin\\',
            '/a/a/a/file.bin', '/long directory/very long file name.payload',
            '/😀a/straße.bin', '/a/ﬃ/file.txt')])

    def test_invalid_final_component_preserves_all_output(self):
        self.check([self.destination(text) for text in (
            '', '/', '///', 'a/file.bin', '\\a\\file.bin', '/a/../file.bin',
            '/a/./file.bin', '/a/b/.hidden', '/a/b/café.bin', '/a/b/+?')])

    def test_malformed_utf8_and_embedded_nul(self):
        cases = []
        for payload in (b'\x00x', b'x\x00', b'\x80x', b'\xc0\xafx', b'\xc1\xbfx',
                        b'\xc2', b'\xe0\x80\x80x', b'\xed\xa0\x80x', b'\xf0\x80\x80\x80x',
                        b'\xf4\x90\x80\x80x', b'\xf5\x80\x80\x80x', b'\xe2\x82', b'\xc2Ax'):
            cases += [(0, self.profile, 0, payload, 1, PATH, 0, []),
                      (1, self.profile, 0, b'/good/' + payload, 4, PATH, 0, [])]
        self.check(cases)

    def test_capacity_query_and_exact_output_extent(self):
        cases = [self.destination('/a/b/file.bin', capacity) for capacity in (0, 1, 2, 3, 4)]
        cases.append((1, self.profile, 2, b'/a/b/file.bin', 0, OK, 3, []))
        self.check(cases)

    def test_invalid_arguments_profiles_and_capacity_overflow(self):
        cases = [(0, self.profile, flag, b'name.txt', 1, INVALID, 0, []) for flag in (1, 2, 3)]
        cases += [(1, self.profile, flag, b'/name.txt', 2, INVALID, 91 if flag & 4 else 0, [])
                  for flag in (1, 2, 4, 5, 6, 7)]
        for profile in (0, 14, 17, 0xffffffff):
            cases += [(0, profile, 0, b'name.txt', 1, INVALID, 0, []),
                      (1, profile, 0, b'/name.txt', 1, INVALID, 0, [])]
        cases.append((1, self.profile, 0, b'/name.txt', 0xffffffff, INVALID, 0, []))
        self.check(cases)

    def test_large_guest_destination_has_no_fixed_component_limit(self):
        self.check([self.destination('/' + '/'.join(['parent'] * 1024 + ['file.bin']), 1025)])

    def test_raw_components_reject_separators(self):
        self.check([(0, self.profile, 0, text, 1, PATH, 0, [])
                    for text in (b'a/b.txt', b'a\\b.txt', b'/a', b'\\a')])


if __name__ == '__main__':
    unittest.main()

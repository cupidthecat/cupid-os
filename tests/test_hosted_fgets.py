"""Exercise line bounds, complete binary bytes, EOF state and read errors."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostedFgetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        selected = os.environ.get('CUPID_HOSTED_FGETS_PROGRAM')
        retained = os.environ.get('CUPID_HOSTED_FGETS_PRODUCTS')
        if retained:
            cls.products = Path(retained)
            cls.products.mkdir(parents=True, exist_ok=True)
        else:
            temporary = tempfile.TemporaryDirectory(prefix='hosted-fgets-')
            cls.addClassCleanup(temporary.cleanup)
            cls.products = Path(temporary.name)
        if selected:
            cls.program = Path(selected).resolve(strict=True)
            return
        runtime = (ROOT / 'toolchain/hosted/i386-linux/runtime.cc').read_text()
        body = re.search(r'^char \*fgets\(.*?^}', runtime, re.M | re.S)
        if body is None:
            raise AssertionError('The actual hosted fgets body is absent')
        header = (ROOT / 'toolchain/hosted/i386-linux/include/stdio.h').read_text()
        for declaration in ['char *fgets(char *destination, int capacity, FILE *stream);',
                            'int feof(FILE *stream);', 'void clearerr(FILE *stream);']:
            if declaration not in header:
                raise AssertionError(declaration)
        fixture = (ROOT / 'toolchain/tests/hosted_fgets_contract.cc').read_text()
        source = cls.products / 'native-contract.cc'
        source.write_text('''#include <errno.h>
#include <stdio.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#define fgets cupid_contract_fgets
#define main cupid_fgets_contract_main
''' + body.group() + '\n' + fixture + '''
#undef main
int main(int argc, char **argv) {
#if defined(_WIN32)
  if (_setmode(_fileno(stdout), _O_BINARY) == -1) return 23;
#endif
  return cupid_fgets_contract_main(argc, argv);
}
''', encoding='utf-8', newline='\n')
        cls.program = cls.products / ('fgets-contract.exe' if os.name == 'nt' else 'fgets-contract')
        command = [shutil.which('clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-Wconversion', '-Wsign-conversion', '-D_CRT_SECURE_NO_WARNINGS',
                   '-D', 'CUPID_FGETS_NATIVE=1', '-x', 'c', str(source), '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        (cls.products / 'native-build.json').write_text(json.dumps({
            'command': command, 'timeout_seconds': 180, 'exit_code': result.returncode,
            'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'stdout': result.stdout.decode(errors='replace'),
            'stderr': result.stderr.decode(errors='replace')}, indent=2))
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace'))

    def check(self, data, capacity=64, mode='read', expected=None):
        label = self._testMethodName
        path = self.products / (label + '.bin')
        path.write_bytes(data)
        before = path.stat()
        if expected is None:
            chunks, offset = [], 0
            while offset < len(data):
                stop = min(offset + capacity - 1, len(data))
                newline = data.find(b'\n', offset, stop)
                if newline != -1:
                    stop = newline + 1
                eof = stop == len(data) and stop - offset < capacity - 1 and newline == -1
                chunks.append(b'line ' + data[offset:stop].hex().encode() + (' eof=%d\n' % eof).encode())
                offset = stop
            expected = b''.join(chunks) + b'end eof=1 error=0 errno=1\n'
            suffixes = {'tell': b'tell kept-eof=1\n', 'seek': b'seek cleared-eof=1 reread=1\n',
                        'seek64': b'seek cleared-eof=1 reread=1\n', 'clear': b'clear kept-error-free=1\n',
                        'fread': b'fread set-eof=1\n'}
            expected += suffixes.get(mode, b'')
        command = [str(self.program), str(path), str(capacity), mode]
        result = subprocess.run(command, capture_output=True, timeout=20)
        (self.products / (label + '.stdout')).write_bytes(result.stdout)
        (self.products / (label + '.stderr')).write_bytes(result.stderr)
        (self.products / (label + '.expected')).write_bytes(expected)
        (self.products / (label + '.json')).write_text(json.dumps({
            'command': command, 'timeout_seconds': 20, 'exit_code': result.returncode,
            'source': {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mtime_ns': before.st_mtime_ns}, 'capacity': capacity, 'mode': mode}, indent=2))
        self.assertEqual((result.returncode, result.stderr), (0, b''), result.stdout)
        self.assertEqual(result.stdout, expected)
        self.assertEqual(path.read_bytes(), data)
        self.assertEqual(path.stat().st_mtime_ns, before.st_mtime_ns)

    def test_newline_is_retained_and_following_line_remains(self):
        self.check(b'first\nsecond\n')

    def test_partial_final_line_sets_eof_after_returning_data(self):
        self.check(b'last line')

    def test_empty_file_preserves_destination_and_sets_eof(self):
        self.check(b'')

    def test_small_capacity_splits_lines_without_losing_bytes(self):
        self.check(b'abcdef\nghij', capacity=3)

    def test_one_byte_capacity_terminates_without_reading(self):
        self.check(b'untouched\n', capacity=1, expected=b'unit preserved=1\n')

    def test_zero_capacity_reports_einval_without_writing(self):
        self.check(b'untouched\n', capacity=0, expected=b'invalid preserved=1\n')

    def test_negative_capacity_reports_einval_without_writing(self):
        self.check(b'untouched\n', capacity=-2, expected=b'invalid preserved=1\n')

    def test_binary_zero_and_high_bytes_survive_complete_reads(self):
        self.check(b'\x00\x80\xff\nnext\x00last')

    def test_exact_capacity_does_not_claim_eof_before_next_read(self):
        self.check(b'x' * 63)

    def test_tell_keeps_the_eof_indicator(self):
        self.check(b'last', mode='tell')

    def test_standard_seek_clears_eof_and_allows_rereading(self):
        self.check(b'first\nlast', mode='seek')

    def test_wide_seek_clears_eof_and_allows_rereading(self):
        self.check(b'first\nlast', mode='seek64')

    def test_clearerr_clears_eof(self):
        self.check(b'last', mode='clear')

    def test_fread_sets_eof_after_indicator_reset(self):
        self.check(b'last', mode='fread')

    def test_write_only_read_sets_error_and_clearerr_preserves_errno(self):
        self.check(b'unchanged\x00\xff', mode='error', expected=b'error cleared=1 errno=1\n')


if __name__ == '__main__':
    unittest.main()

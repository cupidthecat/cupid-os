"""Check hosted update modes through real reads, seeks and writes."""
import os
from pathlib import Path
import shutil
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostedUpdateModeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='cupid-update-modes-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.base = Path(cls.directory.name)
        compiler = shutil.which('clang' if os.name == 'nt' else 'cc')
        if compiler is None:
            raise AssertionError('Native contract compiler is unavailable')
        native = cls.base / ('contract.exe' if os.name == 'nt' else 'contract')
        runtime = (ROOT / 'toolchain/hosted/i386-linux/runtime.cc').read_text()
        parser = re.search(r'^FILE \*fopen\(.*?^}', runtime, re.M | re.S)
        if parser is None:
            raise AssertionError('The active hosted fopen body is unavailable')
        source = cls.base / 'contract.cc'
        source.write_text('''#include <errno.h>
#include <stdio.h>
#define CUPID_LINUX_EINVAL 22
#define CUPID_LINUX_O_RDONLY 0
#define CUPID_LINUX_O_WRONLY 1
#define CUPID_LINUX_O_RDWR 2
#define CUPID_LINUX_O_CREAT 64
#define CUPID_LINUX_O_TRUNC 512
#define CUPID_LINUX_O_APPEND 1024
static FILE *cupid_stdio_open(const char *path, int flags) {
  const char *mode;
  int update = (flags & 3) == CUPID_LINUX_O_RDWR;
  if (flags & CUPID_LINUX_O_APPEND) mode = update ? "a+b" : "ab";
  else if (flags & CUPID_LINUX_O_TRUNC) mode = update ? "w+b" : "wb";
  else mode = update ? "r+b" : "rb";
  return fopen(path, mode);
}
#define fopen cupid_contract_fopen
''' + parser.group() + '\n' +
            (ROOT / 'toolchain/tests/hosted_update_modes_contract.cc').read_text(),
            encoding='utf-8')
        result = subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-D_CRT_SECURE_NO_WARNINGS', '-x', 'c',
            str(source), '-o', str(native)],
            capture_output=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        cls.programs = [native]
        checked = os.environ.get('CUPID_UPDATE_MODES_PROGRAM')
        if checked:
            cls.programs.append(Path(checked))

    def check(self, modes, action, expected, present=True):
        for program in self.programs:
            for mode in modes:
                with self.subTest(program=str(program), mode=mode, present=present):
                    with tempfile.TemporaryDirectory(dir=self.base) as temporary:
                        path = Path(temporary) / 'file.bin'
                        if present:
                            path.write_bytes(b'abcdefgh')
                        before = path.stat().st_mtime_ns if present else None
                        result = subprocess.run([str(program), mode, str(path), action],
                                                capture_output=True, timeout=20)
                        self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
                        self.assertEqual(result.stdout, b'')
                        self.assertEqual(result.stderr, b'')
                        if expected is None:
                            self.assertFalse(path.exists())
                        else:
                            self.assertEqual(path.read_bytes(), expected)
                            if action in ('invalid', 'read'):
                                self.assertEqual(path.stat().st_mtime_ns, before)

    def test_read_update_preserves_length_and_other_bytes(self):
        self.check(('r+', 'rb+', 'r+b'), 'update', b'abXYZfgh')

    def test_read_update_requires_existing_file(self):
        self.check(('r+', 'rb+', 'r+b'), 'missing', None, present=False)

    def test_write_update_truncates_existing_file(self):
        self.check(('w+', 'wb+', 'w+b'), 'truncate', b'XYZ')

    def test_write_update_creates_missing_file(self):
        self.check(('w+', 'wb+', 'w+b'), 'truncate', b'XYZ', present=False)

    def test_append_update_writes_at_end_after_seek(self):
        self.check(('a+', 'ab+', 'a+b'), 'append', b'abcdefghXYZ')

    def test_append_update_creates_missing_file(self):
        self.check(('a+', 'ab+', 'a+b'), 'create-append', b'XYZ', present=False)

    def test_existing_read_modes_keep_their_behavior(self):
        self.check(('r', 'rb'), 'read', b'abcdefgh')

    def test_invalid_modes_preserve_existing_file(self):
        self.check(('', '+', 'x', 'r++', 'rbb', 'rb+b', 'r+b+', 'rr', 'w++', 'ab+b',
                    'R+', 'br+', 'rbx', 'r+garbage'), 'invalid', b'abcdefgh')

    def test_invalid_modes_do_not_create_file(self):
        self.check(('', '+', 'x', 'w++', 'wb+b', 'a+b+', 'ab+b'), 'invalid', None, present=False)

    def test_null_arguments_preserve_existing_file(self):
        self.check(('nullpath', 'nullmode'), 'invalid', b'abcdefgh')


if __name__ == '__main__':
    unittest.main()

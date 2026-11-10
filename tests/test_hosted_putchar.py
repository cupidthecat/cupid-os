"""Exercise the hosted byte conversion, stdout selection and write errors."""
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


class HostedPutcharTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        selected = os.environ.get('CUPID_HOSTED_PUTCHAR_PROGRAM')
        retained = os.environ.get('CUPID_HOSTED_PUTCHAR_PRODUCTS')
        if retained:
            cls.products = Path(retained)
            cls.products.mkdir(parents=True, exist_ok=True)
        else:
            temporary = tempfile.TemporaryDirectory(prefix='hosted-putchar-')
            cls.addClassCleanup(temporary.cleanup)
            cls.products = Path(temporary.name)
        if selected:
            cls.program = Path(selected).resolve(strict=True)
            return
        runtime = (ROOT / 'toolchain/hosted/i386-linux/runtime.cc').read_text()
        body = re.search(r'^int putchar\(int character\) \{.*?^}', runtime, re.M | re.S)
        if body is None:
            raise AssertionError('The hosted putchar implementation is absent')
        declaration = (ROOT / 'toolchain/hosted/i386-linux/include/stdio.h').read_text()
        if 'int putchar(int character);' not in declaration:
            raise AssertionError('The hosted stdio declaration is absent')
        source = cls.products / 'native-contract.cc'
        fixture = (ROOT / 'toolchain/tests/hosted_putchar_contract.cc').read_text()
        source.write_text('''#include <errno.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
static FILE *contract_original_stdout(void) { return stdout; }
#undef putchar
#undef stdout
static FILE *contract_stdout;
#define stdout contract_stdout
#define putchar cupid_contract_putchar
''' + body.group() + '\n#define main cupid_putchar_contract_main\n' + fixture + '''
#undef main
#undef stdout
int main(int argc, char **argv) {
#if defined(_WIN32)
  if (_setmode(_fileno(contract_original_stdout()), _O_BINARY) == -1) return 9;
#endif
  contract_stdout = contract_original_stdout();
  return cupid_putchar_contract_main(argc, argv);
}
''', encoding='utf-8', newline='\n')
        cls.program = cls.products / ('putchar-contract.exe' if os.name == 'nt' else 'putchar-contract')
        command = [shutil.which('clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-Wconversion', '-Wsign-conversion', '-D_CRT_SECURE_NO_WARNINGS',
                   '-x', 'c', str(source), '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        (cls.products / 'native-build.json').write_text(json.dumps({
            'command': command, 'timeout_seconds': 180, 'exit_code': result.returncode,
            'stdout': result.stdout.decode(errors='replace'), 'stderr': result.stderr.decode(errors='replace'),
            'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest()}, indent=2))
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace'))

    def invoke(self, *arguments):
        result = subprocess.run([str(self.program), *map(str, arguments)], capture_output=True, timeout=20)
        (self.products / (self._testMethodName + '.stdout')).write_bytes(result.stdout)
        (self.products / (self._testMethodName + '.stderr')).write_bytes(result.stderr)
        (self.products / (self._testMethodName + '.json')).write_text(json.dumps({
            'command': [str(self.program), *map(str, arguments)], 'timeout_seconds': 20,
            'exit_code': result.returncode}, indent=2))
        self.assertEqual((result.returncode, result.stderr), (0, b''), result.stdout + result.stderr)
        return result.stdout

    def test_every_byte_and_signed_int_conversion_return_exact_value(self):
        self.assertEqual(self.invoke('bytes'), bytes(range(256)) * 3)

    def test_read_only_stdout_reports_error_and_preserves_complete_file(self):
        path = self.products / 'read-only.bin'
        contents = b'unchanged read-only stdout\x00\xff'
        path.write_bytes(contents)
        before = path.stat()
        self.assertEqual(self.invoke('blocked', path), b'R blocked=1 error=1 errno=1\n')
        self.assertEqual(path.read_bytes(), contents)
        self.assertEqual(path.stat().st_mtime_ns, before.st_mtime_ns)


if __name__ == '__main__':
    unittest.main()

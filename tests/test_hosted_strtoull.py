"""Check hosted unsigned conversion against explicit values and the native CRT."""
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MAXIMUM = (1 << 64) - 1
DIGITS = '0123456789abcdefghijklmnopqrstuvwxyz'


def encoded_integer(value, base):
    if not value:
        return '0'
    digits = []
    while value:
        value, digit = divmod(value, base)
        digits.append(DIGITS[digit])
    return ''.join(reversed(digits))


class HostedStrtoullTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        retained = os.environ.get('CUPID_HOSTED_STRTOULL_PRODUCTS')
        if retained:
            cls.products = Path(retained)
            cls.products.mkdir(parents=True, exist_ok=True)
        else:
            temporary = tempfile.TemporaryDirectory(prefix='hosted-strtoull-')
            cls.addClassCleanup(temporary.cleanup)
            cls.products = Path(temporary.name)
        cls.invocations = 0
        cls.reference = None
        selected = os.environ.get('CUPID_HOSTED_STRTOULL_PROGRAM')
        if selected:
            cls.program = Path(selected).resolve(strict=True)
            return
        runtime = (ROOT / 'toolchain/hosted/i386-linux/runtime.cc').read_text()
        body = re.search(r'^static int cupid_runtime_integer_digit\(.*?(?=^char \*strchr\()',
                         runtime, re.M | re.S)
        if body is None:
            raise AssertionError('The actual hosted conversion implementation is absent')
        header = (ROOT / 'toolchain/hosted/i386-linux/include/stdlib.h').read_text()
        if 'unsigned long long strtoull(const char *text, char **end, int base);' not in header:
            raise AssertionError('The hosted declaration is absent')
        fixture = (ROOT / 'toolchain/tests/hosted_strtoull_contract.cc').read_text()
        suffix = '.exe' if os.name == 'nt' else ''
        cls.program = cls.products / ('strtoull-contract' + suffix)
        cls.reference = cls.products / ('strtoull-reference' + suffix)
        prefix = '''#include <stdio.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
'''
        wrapper = '''
#undef main
int main(int argc, char **argv) {
#if defined(_WIN32)
  if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
      _setmode(_fileno(stdout), _O_BINARY) == -1) return 9;
#endif
  return cupid_strtoull_contract_main(argc, argv);
}
'''
        for label, source, program in (
            ('native', '#include <errno.h>\n#include <stdlib.h>\n'
             '#define strtoull cupid_contract_strtoull\n' + body.group() + fixture, cls.program),
            ('reference', fixture, cls.reference),
        ):
            path = cls.products / (label + '-contract.cc')
            path.write_text(prefix + '#define main cupid_strtoull_contract_main\n' + source + wrapper,
                            encoding='utf-8', newline='\n')
            command = [shutil.which('clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-Wconversion', '-Wsign-conversion', '-D_CRT_SECURE_NO_WARNINGS',
                       '-x', 'c', str(path), '-o', str(program)]
            result = subprocess.run(command, capture_output=True, timeout=180)
            (cls.products / (label + '-build.json')).write_text(json.dumps({
                'command': command, 'timeout_seconds': 180, 'exit_code': result.returncode,
                'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'stdout': result.stdout.decode(errors='replace'),
                'stderr': result.stderr.decode(errors='replace')}, indent=2))
            if result.returncode:
                raise AssertionError(result.stderr.decode(errors='replace'))

    def check(self, base, cases, null_end=False, reference_cases=None):
        type(self).invocations += 1
        label = self._testMethodName + '-' + str(self.invocations)
        payload = b''.join(text.encode('latin1').hex().encode() + b'\n' for text, *_ in cases)
        expected = b''.join(('%d %d %d %d\n' % (
            value >> 32, value & 0xffffffff, 0xffffffff if null_end else end, error)).encode()
            for text, value, end, error in cases)
        (self.products / (label + '.stdin')).write_bytes(payload)
        (self.products / (label + '.expected')).write_bytes(expected)
        reference_expected = expected if reference_cases is None else b''.join(
            ('%d %d %d %d\n' % (value >> 32, value & 0xffffffff,
                               0xffffffff if null_end else end, error)).encode()
            for text, value, end, error in reference_cases)
        (self.products / (label + '.reference.expected')).write_bytes(reference_expected)
        for kind, program in [('candidate', self.program), ('reference', self.reference)]:
            if program is None or (kind == 'reference' and base != 0 and not 2 <= base <= 36):
                continue
            command = [str(program), str(base), 'null' if null_end else 'end']
            result = subprocess.run(command, input=payload, capture_output=True, timeout=20)
            (self.products / (label + '-' + kind + '.stdout')).write_bytes(result.stdout)
            (self.products / (label + '-' + kind + '.stderr')).write_bytes(result.stderr)
            (self.products / (label + '-' + kind + '.json')).write_text(json.dumps({
                'command': command, 'timeout_seconds': 20, 'exit_code': result.returncode,
                'case_count': len(cases), 'base': base, 'null_end': null_end,
                'documented_crt_difference': reference_cases is not None}, indent=2))
            self.assertEqual((result.returncode, result.stderr), (0, b''))
            self.assertEqual(result.stdout, reference_expected if kind == 'reference' else expected,
                             (label, kind))

    def test_decimal_crosses_both_machine_words(self):
        values = [0, 1, 255, (1 << 31) - 1, 1 << 31, (1 << 32) - 1,
                  1 << 32, (1 << 63) - 1, 1 << 63, MAXIMUM - 1, MAXIMUM]
        self.check(10, [(str(value), value, len(str(value)), 0) for value in values])

    def test_all_c_locale_white_space_and_optional_sign(self):
        cases = [(space + sign + '42!', 42, len(space + sign + '42'), 0)
                 for space in [' ', '\t', '\n', '\r', '\f', '\v', ' \t\n\r\f\v']
                 for sign in ['', '+']]
        self.check(10, cases)

    def test_negative_values_use_unsigned_negation(self):
        values = [0, 1, 42, (1 << 32) + 1, 1 << 63, MAXIMUM]
        self.check(10, [('-' + str(value), (-value) & MAXIMUM,
                         len(str(value)) + 1, 0) for value in values])

    def test_auto_base_selects_decimal_octal_and_hexadecimal(self):
        self.check(0, [('42!', 42, 2, 0), ('077!', 63, 3, 0), ('0x2a!', 42, 4, 0),
                       ('+0XFF!', 255, 5, 0), ('-010!', (-8) & MAXIMUM, 4, 0),
                       ('09', 0, 1, 0), ('00009', 0, 4, 0)])

    def test_explicit_hex_accepts_both_prefix_cases(self):
        self.check(16, [('0xFf!', 255, 4, 0), ('0XfF!', 255, 4, 0),
                        ('ff!', 255, 2, 0), ('  -0xFf!', (-255) & MAXIMUM, 7, 0)])

    def test_prefix_without_hex_digits_preserves_first_invalid_position(self):
        for base in [0, 16]:
            cases = [('0x', 0, 1, 0), ('0X!', 0, 1, 0), ('0xg', 0, 1, 0),
                     ('-0x', 0, 2, 0), ('+0X', 0, 2, 0)]
            reference = [(text, value, 0, error) for text, value, end, error in cases] if os.name == 'nt' else None
            self.check(base, cases, reference_cases=reference)

    def test_no_conversion_returns_original_pointer_and_keeps_errno(self):
        self.check(10, [(text, 0, 0, 0) for text in ['', ' ', '\t\n', '+', '-', ' +',
                                                           'abc', '+-1', '-+1', '\xff1']])

    def test_invalid_base_reports_einval_and_original_pointer(self):
        for base in [-2, -1, 1, 37, 100]:
            self.check(base, [('42!', 0, 0, 2), ('', 0, 0, 2)], null_end=False)

    def test_suffix_consumption_stops_at_first_invalid_byte(self):
        self.check(10, [('123abc', 123, 3, 0), ('123 4', 123, 3, 0),
                        ('+123-4', 123, 4, 0), ('1\xff2', 1, 1, 0), ('0x10', 0, 1, 0)])

    def test_explicit_octal_and_binary_do_not_consume_other_prefixes(self):
        self.check(8, [('0779', 63, 3, 0), ('0x10', 0, 1, 0)])
        self.check(2, [('10102', 10, 4, 0), ('0b10', 0, 1, 0)])

    def test_all_supported_bases_accept_lower_and_upper_digits(self):
        for base in range(2, 37):
            text = encoded_integer((1 << 63) + 137, base)
            self.check(base, [(text + '!', (1 << 63) + 137, len(text), 0),
                              (text.upper() + '!', (1 << 63) + 137, len(text), 0)])

    def test_each_base_checks_exact_maximum_and_both_overflow_signs(self):
        for base in range(2, 37):
            boundary = encoded_integer(MAXIMUM, base)
            excess = encoded_integer(MAXIMUM + 1, base)
            self.check(base, [(boundary + '!', MAXIMUM, len(boundary), 0),
                              (excess + '!', MAXIMUM, len(excess), 1),
                              ('-' + excess + '!', MAXIMUM, len(excess) + 1, 1)])

    def test_overflow_consumes_all_valid_digits_before_suffix(self):
        text = '9' * 8192
        self.check(10, [(text + '!', MAXIMUM, len(text), 1),
                        ('-' + text + '!', MAXIMUM, len(text) + 1, 1)])

    def test_leading_zeroes_do_not_create_false_overflow(self):
        text = '0' * 8192 + str(MAXIMUM)
        self.check(10, [(text + '!', MAXIMUM, len(text), 0)])

    def test_null_end_pointer_preserves_values_and_errors(self):
        self.check(10, [('42!', 42, 2, 0), ('-1', MAXIMUM, 2, 0), ('', 0, 0, 0),
                        (str(MAXIMUM + 1), MAXIMUM, 20, 1)], null_end=True)
        self.check(1, [('42!', 0, 0, 2)], null_end=True)

    def test_repeated_calls_preserve_errno_after_success(self):
        self.check(10, [('42', 42, 2, 0), (str(MAXIMUM + 1), MAXIMUM, 20, 1),
                        ('7', 7, 1, 0), ('no digits', 0, 0, 0)] * 20)

    def test_deterministic_values_and_range_failures_in_every_base(self):
        randomizer = random.Random(0x4355504944)
        for base in range(2, 37):
            cases = []
            for index in range(32):
                value = randomizer.randrange(1 << 68)
                digits = encoded_integer(value, base)
                prefix = ' \t-' if index & 1 else ' +'
                expected = MAXIMUM if value > MAXIMUM else ((-value) & MAXIMUM if index & 1 else value)
                cases.append((prefix + digits + '!', expected, len(prefix + digits), int(value > MAXIMUM)))
            self.check(base, cases)


if __name__ == '__main__':
    unittest.main()

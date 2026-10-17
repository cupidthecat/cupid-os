"""Wide integer offsets keep target stride and evaluate operands once."""
import subprocess

from tests.test_cupidc_frame_load import FrameToolCase, ROOT


class CupidCWidePointerTests(FrameToolCase):
    fixture_source = '/toolchain/tests/cupidc_wide_pointer_runtime.cc'

    def test_signed_unsigned_subscripts_strides_and_mutations_execute(self):
        self.execute_runtime()

    def test_wide_offsets_load_the_snapshot_value_before_address_arithmetic(self):
        for name in ('unsigned_index', 'signed_index', 'computed_index', 'signed_add',
                     'signed_subtract', 'record_add'):
            with self.subTest(function=name):
                self.assertIn(bytes.fromhex('8b8900000000'), self.code[name])
        self.assertIn(bytes.fromhex('8b8000000000'), self.code['reversed_add'])

    def test_invalid_pointer_offsets_preserve_existing_output(self):
        for name, source, diagnostic in (
            ('floating-index', 'int f(int *p, double n) { return p[n]; }', 'integer operand'),
            ('floating-add', 'int *f(int *p, double n) { return p + n; }', 'operand'),
            ('incomplete', 'struct Missing; struct Missing *f(struct Missing *p, long long n) { return p + n; }', 'complete'),
            ('function', 'void (*f(void (*p)(void), long long n))(void) { return p + n; }', 'complete object'),
        ):
            path = self.output / (name + '.cc')
            path.write_bytes((source + '\n').encode())
            output = self.output / (name + '.o')
            output.write_bytes(b'preserved compiler output')
            result = subprocess.run([str(self.tools['cupidc']), '--root', str(ROOT), '-c',
                '/' + path.relative_to(ROOT).as_posix(), '-o',
                '/' + output.relative_to(ROOT).as_posix()], capture_output=True, text=True, timeout=60)
            with self.subTest(case=name):
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr.lower())
                self.assertEqual(output.read_bytes(), b'preserved compiler output')

    def test_windows_seek_accepts_long_and_rejects_int_high_word(self):
        for name, type_name, succeeds in (
            ('long-high-word', 'LONG', True),
            ('int-high-word', 'int', False),
        ):
            path = self.output / (name + '.cc')
            path.write_bytes(('#include <windows.h>\n'
                '_Static_assert(sizeof(LONG) == 4u, "LONG has target word width");\n'
                'int seek_high(void) { ' + type_name + ' high = 1; '
                'return (int)SetFilePointer(0u, 0L, &high, FILE_BEGIN); }\n').encode())
            output = self.output / (name + '.o')
            output.write_bytes(b'preserved compiler output')
            result = subprocess.run([str(self.tools['cupidc']), '--root', str(ROOT),
                '-D', '_WIN32=1', '-c', '/' + path.relative_to(ROOT).as_posix(),
                '--include-angle', '/toolchain/hosted/i386-linux/include',
                '-o', '/' + output.relative_to(ROOT).as_posix()],
                capture_output=True, text=True, timeout=60)
            with self.subTest(case=name):
                if succeeds:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertNotEqual(output.read_bytes(), b'preserved compiler output')
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('ctb000010', result.stderr.lower())
                    self.assertIn('not convertible to parameter type', result.stderr.lower())
                    self.assertEqual(output.read_bytes(), b'preserved compiler output')

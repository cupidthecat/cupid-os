"""Keep flexible members addressable without inventing a complete array size."""
import subprocess

from tests.test_cupidc_frame_load import FrameToolCase, ROOT


class CupidCFlexibleArrayTests(FrameToolCase):
    fixture_source = '/toolchain/tests/cupidc_flexible_array_runtime.cc'

    def test_member_addresses_qualifiers_alignment_and_tail_padding_execute(self):
        self.execute_runtime()

    def test_flexible_elements_keep_byte_halfword_and_record_accesses(self):
        self.assertIn(bytes.fromhex('0fbe00'), self.code['byte_at'])
        self.assertIn(bytes.fromhex('0fb700'), self.code['short_at'])
        self.assertGreater(len(self.code['cell_at']), len(self.code['byte_at']))

    def test_invalid_incomplete_array_uses_preserve_existing_output(self):
        cases = (
            ('size', 'struct S { int n; char a[]; }; unsigned f(struct S *p) { return sizeof(p->a); }'),
            ('nonfinal', 'struct S { int n; char a[]; int after; }; int f(void) { return 0; }'),
            ('union', 'union S { int n; char a[]; }; int f(void) { return 0; }'),
            ('element', 'struct E; struct S { int n; struct E a[]; }; int f(void) { return 0; }'),
            ('array-stride', 'struct S { int n; char a[]; }; void *f(struct S *p) { return &p->a + 1; }'),
        )
        for name, source in cases:
            with self.subTest(case=name):
                path = self.output / (name + '.cc')
                path.write_text(source + '\n', encoding='utf-8')
                output = path.with_suffix('.o')
                output.write_bytes(b'preserved compiler output')
                before = output.stat().st_mtime_ns
                result = subprocess.run([str(self.tools['cupidc']), '--root', str(ROOT), '-c',
                    '/' + path.relative_to(ROOT).as_posix(), '-o',
                    '/' + output.relative_to(ROOT).as_posix()], capture_output=True, text=True, timeout=60)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('error CTB', result.stderr)
                self.assertEqual(output.read_bytes(), b'preserved compiler output')
                self.assertEqual(output.stat().st_mtime_ns, before)

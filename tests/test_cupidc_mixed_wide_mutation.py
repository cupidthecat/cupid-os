"""Narrow stores follow complete wide integer compound calculations."""
import os
from pathlib import Path
import shutil
import subprocess

from tests.test_cupidc_frame_load import FrameToolCase, ROOT


class CupidCMixedWideMutationTests(FrameToolCase):
    fixture_source = '/toolchain/tests/cupidc_mixed_wide_mutation_runtime.cc'

    def tearDown(self):
        products = os.environ.get('CUPIDC_MIXED_WIDE_PRODUCTS')
        if products:
            destination = Path(products) / self._testMethodName
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copytree(self.output, destination)

    def test_full_width_calculation_narrow_stores_and_single_evaluation_execute(self):
        self.execute_runtime()

    def test_unsupported_and_invalid_mutations_preserve_output(self):
        cases = (
            ('atomic', '_Atomic unsigned int x; unsigned int f(void) { return x += 1ULL; }', 'CTD000003'),
            ('boolean', '_Bool f(_Bool x) { return x += 1ULL; }', 'CTD000003'),
            ('wide-count', 'unsigned int f(unsigned int x, unsigned long long n) { return x <<= n; }', 'CTD000003'),
            ('field-count', 'struct S { unsigned int x : 9; }; unsigned int f(struct S *p, unsigned long long n) { return p->x >>= n; }', 'CTD000003'),
            ('volatile-field', 'struct S { volatile unsigned int x : 9; }; unsigned int f(struct S *p) { return p->x += 1ULL; }', 'CTD000003'),
            ('invalid-bitwise', 'unsigned int f(unsigned int x) { return x &= 0.5; }', 'CTB000010'),
        )
        for name, source, diagnostic in cases:
            path = self.output / (name + '.cc')
            path.write_bytes((source + '\n').encode())
            output = self.output / (name + '.o')
            output.write_bytes(b'preserved compiler output')
            before = output.stat().st_mtime_ns
            result = subprocess.run([str(self.tools['cupidc']), '--root', str(ROOT), '-c',
                '/' + path.relative_to(ROOT).as_posix(), '-o',
                '/' + output.relative_to(ROOT).as_posix()], capture_output=True, text=True, timeout=60)
            (self.output / (name + '.stdout')).write_text(result.stdout, encoding='utf-8')
            (self.output / (name + '.stderr')).write_text(result.stderr, encoding='utf-8')
            with self.subTest(case=name):
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)
                self.assertEqual(output.read_bytes(), b'preserved compiler output')
                self.assertEqual(output.stat().st_mtime_ns, before)

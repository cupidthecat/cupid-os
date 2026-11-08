"""Private compiler integers execution cases."""

from .harness import (Path, REPO_ROOT, tempfile)

class Integers1Cases:
    def test_indirect_integer_indexed_updates_keep_old_and_new_values(self):
        self._assert_halfword_runtime(r'''
            int main() {
                int a[3];
                U16 h[3];
                I16 s[3];
                char b[3];
                unsigned int u[2];
                int i;
                a[0] = 41;
                a[1] = 99;
                h[0] = 65535;
                h[1] = 54321;
                s[0] = -32768;
                s[1] = 123;
                b[0] = 255;
                b[1] = 123;
                u[0] = 4294967295u;
                i = 0;
                if (a[i++]++ != 41 || i != 1 || a[0] != 42 || a[1] != 99)
                    return 1;
                if (++h[0] != 0 || h[0] != 0 || h[1] != 54321)
                    return 2;
                if (s[0]-- != -32768 || s[0] != 32767 || s[1] != 123)
                    return 3;
                if (++b[0] != 0 || b[0] != 0 || b[1] != 123)
                    return 4;
                if (u[0]++ != 4294967295u || u[0] != 0)
                    return 5;
                if (--a[0] != 41 || a[0]-- != 41 || a[0] != 40)
                    return 6;
                return 0;
            }        ''')

    def test_indirect_integer_pointer_designators_execute_once(self):
        self._assert_halfword_runtime(r'''
            I16 data[3];
            int calls;
            I16 *pick() {
                calls++;
                return data + 1;
            }
            int main() {
                I16 *p;
                int old;
                data[0] = 123;
                data[1] = 12;
                data[2] = 456;
                p = data + 1;
                if (++*p != 13 || (*p)++ != 13 || *p != 14)
                    return 1;
                old = (*pick())--;
                if (old != 14 || data[1] != 13 || calls != 1)
                    return 2;
                if (--*pick() != 12 || calls != 2 || data[0] != 123 || data[2] != 456)
                    return 3;
                return 0;
            }        ''')

    def test_indirect_integer_record_and_row_updates(self):
        self._assert_halfword_runtime(r'''
            struct R {
                int a;
                U16 b;
                I16 c;
            };
            int calls;
            struct R rows[2];
            struct R *pick() {
                calls++;
                return rows;
            }
            int main() {
                int grid[2][3];
                int i;
                rows[0].a = 10;
                rows[0].b = 65535;
                rows[0].c = -1;
                if (rows[0].a++ != 10 || rows[0].a != 11)
                    return 1;
                if (++pick()->b != 0 || rows[0].b != 0 || calls != 1)
                    return 2;
                if (rows[0].c-- != -1 || rows[0].c != -2)
                    return 3;
                grid[1][2] = 17;
                i = 1;
                if (grid[i++][2]++ != 17 || grid[1][2] != 18 || i != 2)
                    return 4;
                return 0;
            }        ''')

    def test_indirect_integer_statement_and_for_updates(self):
        self._assert_halfword_runtime(r'''
            struct R {
                int a;
            };
            int main() {
                int a[1];
                I16 s[1];
                int i;
                struct R r;
                a[0] = 5;
                s[0] = -3;
                r.a = 9;
                a[0]++;
                s[0]--;
                r.a++;
                r.a--;
                if (a[0] != 6 || s[0] != -4 || r.a != 9)
                    return 1;
                for (i = 0; i < 3; a[0]++) {
                    i++;
                }
                if (a[0] != 9)
                    return 2;
                return 0;
            }        ''')

    def test_indirect_integer_nonmodifiable_results_reject_and_recover(self):
        for body in ('int a[2];return (a[0]+1)++;', 'int a[2];return ++&a[0];', 'int a[2];return ((int)a[0])++;'):
            with self.subTest(body=body), tempfile.TemporaryDirectory() as temporary:
                p = Path(temporary)
                r, _, _ = self._compile_after_failure(p, 'int main(){' + body + '}', 'int main(){int a[1];a[0]=4;return ++a[0]-5;}', same_state=True)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn('increment or decrement requires', r.stderr)
                executed = self._run_i386(p, int(r.stdout.strip()))
                self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_indirect_integer_const_rows_and_pointer_slots_are_not_integer_updates(self):
        cases = (('int main(){const int a[2];return ++a[0];}', 'modifiable scalar lvalue'), ('int main(){int a[2][3];return ++a[0];}', 'requires a scalar variable'), ('int main(){int a[2];int *slots[1];slots[0]=a;slots[0]++;return 0;}', 'indirect increment or decrement'), ('int main(){int a[2];int *slots[1];slots[0]=a;return ++slots[0];}', 'indirect increment or decrement'))
        for source, diagnostic in cases:
            for aot in (False, True):
                with self.subTest(source=source, aot=aot), tempfile.TemporaryDirectory() as temporary:
                    result, _, _ = self._compile(Path(temporary), source, aot=aot)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stdout + result.stderr)

    def test_indirect_integer_member_update_uses_destination_qualification(self):
        self._assert_halfword_runtime('struct R{int a;};int main(){const int enabled=1;struct R r;r.a=4;if(enabled)r.a++;return r.a-5;}')
        for source in ('struct R{int a;};int main(){const struct R r;r.a++;return 0;}',):
            for aot in (False, True):
                with self.subTest(source=source, aot=aot), tempfile.TemporaryDirectory() as temporary:
                    result, _, _ = self._compile(Path(temporary), source, aot=aot)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('modifiable scalar lvalue', result.stdout + result.stderr)

    def test_installed_integer_update_demo_runs_in_jit_and_aot(self):
        source = (REPO_ROOT / "bin/feature23_full_access.cc").read_text(
            encoding="utf-8")
        start = source.index("I32 feature23_integer_update_probe() {")
        end = source.index("I32 feature23_pointer_difference_probe()", start)
        self._assert_halfword_runtime(
            source[start:end] +
            "\nint main() { return feature23_integer_update_probe(); }")

    def test_halfword_scalar_addresses_use_declared_strides(self):
        self._assert_halfword_runtime('''
            U16 global_unsigned; I16 global_signed;
            int check(U16 parameter_unsigned, I16 parameter_signed) {
                U16 local_unsigned; I16 local_signed;
                static U16 static_unsigned; static I16 static_signed;
                if ((U32)(&local_unsigned + 1) - (U32)&local_unsigned != 2) return 1;
                if ((U32)(1 + &local_signed) - (U32)&local_signed != 2) return 2;
                if ((U32)(&parameter_unsigned + 1) - (U32)&parameter_unsigned != 2) return 3;
                if ((U32)(&parameter_signed - 1) - (U32)&parameter_signed != 4294967294u) return 4;
                if ((U32)(&static_unsigned + 1) - (U32)&static_unsigned != 2) return 5;
                if ((U32)(&static_signed + 1) - (U32)&static_signed != 2) return 6;
                if ((U32)(&global_unsigned + 1) - (U32)&global_unsigned != 2) return 7;
                if ((U32)(&global_signed + 1) - (U32)&global_signed != 2) return 8;
                return 0;
            }
            int main() { return check(1, -1); }
        ''')

    def test_halfword_array_sizeof_descends_through_rows(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 grid[2][3]; I16 cube[2][3][4];
                if (sizeof(*grid) != 6 || sizeof(**grid) != 2) return 1;
                if (sizeof(*cube) != 24 || sizeof(**cube) != 8 || sizeof(***cube) != 2) return 2;
                return 0;
            }
        ''')

    def test_halfword_cast_failure_recovers_and_keeps_explicit_address_casts(self):
        for target in ('U16', 'I16'):
            with self.subTest(target=target):
                with tempfile.TemporaryDirectory(prefix='private-halfword-cast-recovery-') as temporary:
                    root = Path(temporary)
                    result, _code, _data = self._compile_after_failure(
                        root,
                        'int main() { float4 value = {1.0f, 2.0f, 3.0f, 4.0f}; return (' + target + ')value; }',
                        '''U16 global;
                           int main() {
                               if ((U16)&global != ((U32)&global & 65535u)) return 1;
                               if ((I16)&global != (I16)((U32)&global & 65535u)) return 2;
                               if ((U16)65537 != 1 || (I16)65535 != -1) return 3;
                               return 0;
                           }''',
                        same_state=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    executed = self._run_i386(root, int(result.stdout.strip()))
                    self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_halfword_native_class_methods_normalize_signed_and_unsigned_results(self):
        self._assert_halfword_runtime('''
            class Device { I32 marker; };
            int main() {
                Device device;
                device.marker = 4951;
                if (device.ReadPort() != 65000) return 1;
                if (device.ReadSigned() != -1) return 2;
                return 0;
            }
        ''')

    def test_halfword_native_failures_recover_same_state(self):
        with tempfile.TemporaryDirectory(prefix='private-halfword-native-recovery-') as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_two_failures(root,
                'int main() { return bind(31); }',
                'int main() { return accept(31, 0, 1.0); }',
                'int main() { return htons(70196.75) == 13330 ? 0 : 1; }')
            self.assertEqual(result.returncode, 0, result.stderr)
            executed = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_halfword_global_integer_initializers_accept_character_constants(self):
        self._assert_halfword_runtime(r'''
            U16 values[4] = {'A', '\n', 'B' + 1, 65537};
            I16 signed_values[3] = {-'A', '\t', 65535};
            char bytes[3] = {'C', 0, 'D' - 1};
            int words[2] = {'E' * 2, ('F')};
            int main() {
                if (values[0] != 65 || values[1] != 10 || values[2] != 67 || values[3] != 1) return 1;
                if (signed_values[0] != -65 || signed_values[1] != 9 || signed_values[2] != -1) return 2;
                if (bytes[0] != 67 || bytes[1] != 0 || bytes[2] != 67) return 3;
                if (words[0] != 138 || words[1] != 70) return 4;
                return 0;
            }
        ''')

    def test_static_integer_arrays_keep_typed_constant_initializers(self):
        self._assert_halfword_runtime(r'''
            int step() {
                static U16 ports[3] = {65537, 54321,};
                static I16 signed_words[2] = {65535, -32768};
                static char bytes[3] = {'A', '\n'};
                static unsigned int words[2] = {2147483648u, 4294967295u};
                if (ports[1] != 54321 || ports[2] != 0) return 1;
                if (signed_words[0] != -1 || signed_words[1] != -32768) return 2;
                if (bytes[0] != 65 || bytes[1] != 10 || bytes[2] != 0) return 3;
                if (words[0] != 2147483648u || words[1] != 4294967295u) return 4;
                int previous = ports[0];
                ports[0] += 1;
                return previous;
            }
            int main() {
                if (step() != 1 || step() != 2 || step() != 3) return 5;
                return 0;
            }
        ''')

    def test_static_integer_array_typedefs_keep_constant_data(self):
        self._assert_halfword_runtime('''
            typedef U16 Ports[3];
            int main() {
                static Ports ports = {65537, 65535};
                if (sizeof(ports) != 6 || ports[0] != 1 || ports[1] != 65535 || ports[2] != 0) return 1;
                return 0;
            }
        ''')

    def test_repl_integer_arrays_keep_typed_initializers_across_units(self):
        result = self._compile_repl_and_run((
            r"U16 ports[3] = {65537, 54321,}; I16 signed_words[2] = {65535, -32768};",
            r"char bytes[3] = {'A', '\n'}; unsigned int words[2] = {2147483648u, 4294967295u};",
            '''int main() {
                if (ports[0] != 1 || ports[1] != 54321 || ports[2] != 0) return 1;
                if (signed_words[0] != -1 || signed_words[1] != -32768) return 2;
                if (bytes[0] != 65 || bytes[1] != 10 || bytes[2] != 0) return 3;
                if (words[0] != 2147483648u || words[1] != 4294967295u) return 4;
                return 0;
            }''',
        ))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_integer_array_typedefs_keep_constant_data(self):
        result = self._compile_repl_and_run((
            "typedef I16 Samples[3];",
            "Samples values = {65535, -32768,};",
            "int main() { return values[0] != -1 || values[1] != -32768 || values[2] != 0; }",
        ))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_static_and_repl_integer_lists_reject_bad_constants(self):
        failures = (
            ("U16 values[1] = {1, 2};", "too many integer array initializers"),
            ("I16 values[1] = {1 / 0};", "division by zero"),
            ("U16 values[1] = {native_mixed(1, 2, 3, 4)};", "constant"),
        )
        for declaration, diagnostic in failures:
            for context, repl, aot in (
                ("static JIT", False, False),
                ("static AOT", False, True),
                ("persistent REPL", True, False),
            ):
                source = declaration if repl else (
                    "int main() { static " + declaration + " return 0; }")
                with self.subTest(declaration=declaration, context=context):
                    with tempfile.TemporaryDirectory(prefix="private-integer-list-negative-") as temporary:
                        result, _code, _data = self._compile(Path(temporary), source, repl=repl, aot=aot)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stderr)

    def test_repl_integer_list_failure_restores_data_and_names(self):
        with tempfile.TemporaryDirectory(prefix="private-integer-list-recovery-") as temporary:
            root = Path(temporary)
            result, _code, data = self._compile_repl_after_struct_failure(root, (
                "U16 baseline[2] = {31, 53};",
                "U16 poisoned[2] = {1, 2, 3};",
                "U16 poisoned[2] = {65537, 65535};",
                "int main() { return baseline[0] != 31 || baseline[1] != 53 || poisoned[0] != 1 || poisoned[1] != 65535; }",
            ))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(data.stat().st_size, 8)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_halfword_sizes_and_fixed_array_typedefs(self):
        self._assert_halfword_runtime('''
            typedef unsigned short Ports[3];
            int main() {
                Ports ports;
                if (sizeof(U16) != 2 || sizeof(I16) != 2 || sizeof(short) != 2) return 1;
                if (sizeof(unsigned short) != 2 || sizeof(Ports) != 6 || sizeof(ports) != 6) return 2;
                return 0;
            }
        ''')

    def test_halfword_casts_and_function_results_normalize(self):
        self._assert_halfword_runtime('''
            U16 unsigned_result() { return 65537; }
            I16 signed_result() { return 65535; }
            int main() {
                if ((U16)65537 != 1 || (I16)65535 != -1) return 1;
                if (unsigned_result() != 1 || signed_result() != -1) return 2;
                if ((unsigned short)70000 != 4464 || (short)32768 != -32768) return 3;
                if ((U16)65535.75 != 65535 || (I16)-32768.75 != -32768) return 4;
                return 0;
            }
        ''')

    def test_halfword_fixed_calls_preserve_order_and_complete_word_slots(self):
        self._assert_halfword_runtime('''
            int check(U16 u, double d, I16 s, int tail) {
                if (u != 65535 || d != 3.5 || s != -2 || tail != 31) return 1;
                return 0;
            }
            int main() {
                int i = 0;
                while (i < 3) {
                    if (check(65535.0, 3.5, 65534.0, 31.0) != 0) return 2;
                    i++;
                }
                return 0;
            }
        ''')

    def test_halfword_integer_promotions_remain_signed_word_operations(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 u = 65535; I16 s = -1;
                if (u + 1 != 65536 || u + s != 65534 || ~u != -65536) return 1;
                if (u < -1 || u / 2 != 32767 || s * 3 != -3) return 2;
                if ((u >> 1) != 32767 || ((I16)-32768 >> 1) != -16384) return 3;
                return 0;
            }
        ''')

    def test_halfword_prefix_postfix_and_compound_stores_normalize(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 u = 65535; I16 s = 32767;
                int old = u++;
                if (old != 65535 || u != 0) return 1;
                int before = --u;
                if (before != 65535 || u != 65535) return 2;
                int signed_before = ++s;
                if (signed_before != -32768 || s != -32768) return 3;
                u = 2; u /= 65536;
                if (u != 0) return 4;
                s = 32767; s += 1;
                if (s != -32768) return 5;
                return 0;
            }
        ''')

    def test_halfword_array_writes_preserve_neighboring_elements(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 words[3];
                words[1] = 12345; words[2] = 54321; words[0] = 65537;
                if (words[0] != 1 || words[1] != 12345 || words[2] != 54321) return 1;
                words[0] += 65535;
                if (words[0] != 0 || words[1] != 12345) return 2;
                words[1] = 2; words[1] /= 65536;
                if (words[1] != 0 || words[2] != 54321) return 3;
                return 0;
            }
        ''')

    def test_halfword_pointer_stride_and_dereference_compounds(self):
        self._assert_halfword_runtime('''
            int main() {
                I16 words[3]; words[0] = -2; words[1] = 31; words[2] = -32768;
                I16 *p = words;
                if (sizeof(*p) != 2 || *p != -2) return 1;
                p++;
                if (*p != 31) return 2;
                p = p + 1;
                if (*p != -32768) return 3;
                p--; *p = 2; *p /= 65536;
                if (words[1] != 0 || words[0] != -2 || words[2] != -32768) return 4;
                return 0;
            }
        ''')

    def test_halfword_records_have_two_byte_fields_and_alignment(self):
        self._assert_halfword_runtime('''
            struct Pair { U16 a; I16 b; int tail; };
            int main() {
                struct Pair p;
                p.b = -2; p.tail = 31; p.a = 65537;
                if (sizeof(struct Pair) != 8 || p.a != 1 || p.b != -2 || p.tail != 31) return 1;
                p.a += 65535;
                if (p.a != 0 || p.b != -2 || p.tail != 31) return 2;
                return 0;
            }
        ''')

    def test_halfword_global_and_static_array_initializers(self):
        self._assert_halfword_runtime('''
            U16 global_words[2] = {65537, 65535};
            I16 global_value = 65535;
            int main() {
                static U16 local_words[2];
                local_words[0] = 65537; local_words[1] = 54321;
                if (sizeof(global_words) != 4 || global_words[0] != 1 || global_words[1] != 65535) return 1;
                if (global_value != -1 || sizeof(local_words) != 4 || local_words[0] != 1 || local_words[1] != 54321) return 2;
                return 0;
            }
        ''')

    def test_halfword_two_and_three_dimensional_arrays(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 grid[2][3]; I16 cube[2][2][2];
                grid[0][2] = 65537; grid[1][0] = 54321;
                cube[0][1][1] = 65535; cube[1][0][0] = 32768;
                if (sizeof(grid) != 12 || sizeof(cube) != 16) return 1;
                if (grid[0][2] != 1 || grid[1][0] != 54321) return 2;
                if (cube[0][1][1] != -1 || cube[1][0][0] != -32768) return 3;
                return 0;
            }
        ''')

    def test_halfword_rejects_invalid_short_declarations(self):
        for declaration in ('short float value;', 'short double value;', 'short char value;',
                            'long short value;', 'signed unsigned short value;',
                            'short short int value;', 'int short short value;'):
            for aot in (False, True):
                with self.subTest(declaration=declaration, aot=aot):
                    with tempfile.TemporaryDirectory(prefix='private-halfword-negative-') as temporary:
                        result, _code, _data = self._compile(Path(temporary), declaration, aot=aot)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('short' if 'signed unsigned' not in declaration else 'both signed and unsigned',
                                  result.stdout + result.stderr)

    def test_halfword_pointer_addition_matches_other_object_widths(self):
        self._assert_halfword_runtime('''
            struct Pair { int a; int b; };
            int main() {
                int integers[3]; double doubles[3]; struct Pair records[2];
                integers[1] = 31; doubles[1] = 3.5; records[1].a = 43;
                int *ip = integers; double *dp = doubles; struct Pair *rp = records;
                ip = 1 + ip; dp = dp + 1; rp = rp + 1;
                if (*ip != 31 || *dp != 3.5 || rp->a != 43) return 1;
                U16 halves[3]; halves[1] = 54321;
                U16 *hp = halves; hp = 1 + hp;
                if (*hp != 54321) return 2;
                hp = hp - 1;
                if (hp != halves) return 3;
                return 0;
            }
        ''')

    def test_halfword_pointer_compound_addition_scales_indices(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 halves[3]; halves[1] = 54321;
                int words[3]; words[1] = 31;
                U16 *hp = halves; int *ip = words;
                hp += 1; ip += 1;
                if (*hp != 54321 || *ip != 31) return 1;
                hp -= 1; ip -= 1;
                if (hp != halves || ip != words) return 2;
                return 0;
            }
        ''')

    def test_halfword_global_integer_initializer_list_bounds(self):
        self._assert_halfword_runtime('''
            U16 halves[3] = {65537, 65535,};
            int words[3] = {31, -2, 43};
            char bytes[3] = {65, 66, 0};
            int main() {
                if (halves[0] != 1 || halves[1] != 65535 || halves[2] != 0) return 1;
                if (words[0] != 31 || words[1] != -2 || words[2] != 43) return 2;
                if (bytes[0] != 65 || bytes[1] != 66 || bytes[2] != 0) return 3;
                return 0;
            }
        ''')

    def test_halfword_global_initializer_rejects_excess_and_nonconstants(self):
        cases = (
            ('U16 values[1] = {1, 2};', 'too many integer array initializers'),
            ('U16 values[2] = {1 / 0};', 'division by zero'),
            ('int value() { return 1; } U16 values[2] = {value()};', 'constant'),
        )
        for source, diagnostic in cases:
            for aot in (False, True):
                with self.subTest(source=source, aot=aot):
                    with tempfile.TemporaryDirectory(prefix='private-halfword-initializer-negative-') as temporary:
                        result, _code, _data = self._compile(Path(temporary), source, aot=aot)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(diagnostic, result.stdout + result.stderr)

    def test_halfword_trailing_width_qualifiers_match_prefix_spelling(self):
        self._assert_halfword_runtime('''
            int main() {
                int short signed a = 65535;
                unsigned int short b = 65537;
                if (sizeof(a) != 2 || sizeof(b) != 2 || a != -1 || b != 1) return 1;
                return 0;
            }
        ''')

    def test_halfword_explicit_unsigned_word_widening(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 u = 65535; I16 s = -1;
                if ((U32)u != 65535u || (unsigned int)u != 65535u) return 1;
                if ((U32)s != 4294967295u || (unsigned int)s != 4294967295u) return 2;
                return 0;
            }
        ''')

    def test_halfword_constant_metadata_matches_typed_null_values(self):
        self._assert_halfword_runtime('''
            typedef int (*Transform)(int);
            int main() {
                Transform first = (U16)65536;
                Transform second = (I16)65535 + 1;
                if (first != 0 || second != 0) return 1;
                return 0;
            }
        ''')
        for expression in ('(U16)65537', '(I16)65535'):
            with self.subTest(expression=expression):
                with tempfile.TemporaryDirectory(prefix='private-halfword-null-negative-') as temporary:
                    source = 'typedef int (*Transform)(int); int main() { Transform value = ' + expression + '; return 0; }'
                    result, _code, _data = self._compile(Path(temporary), source)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('function-pointer', result.stdout + result.stderr)

    def test_halfword_pointer_arithmetic_preserves_multidimensional_shape(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 grid[3][3]; double lattice[3][2];
                grid[1][2] = 31; grid[2][1] = 54321; lattice[2][1] = 3.5;
                if ((grid + 1)[0][2] != 31) return 1;
                if (((grid + 1) + 1)[0][1] != 54321) return 2;
                if ((1 + grid)[0][2] != 31) return 3;
                if (((lattice + 1) + 1)[0][1] != 3.5) return 4;
                return 0;
            }
        ''')

    def test_fixed_integer_parameters_preserve_object_pointer_bits(self):
        result = self._compile_and_run(
            """
            int write_outputs(int value_out_address, int text_out_address) {
              int *value_out = value_out_address;
              char *text_out = text_out_address;
              *value_out = 37;
              text_out[0] = 'O';
              text_out[1] = 'S';
              text_out[2] = 0;
              return 0;
            }

            int main() {
              int value = 0;
              char text[3];
              if (write_outputs(&value, text) != 0) return 1;
              if (value != 37) return 2;
              if (text[0] != 'O' || text[1] != 'S' || text[2] != 0) {
                return 3;
              }
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_unsigned_parameter_preserves_object_pointer_bits(self):
        result = self._compile_and_run(
            """
            int write_output(unsigned int output_address) {
              int *output = output_address;
              *output = 41;
              return 0;
            }

            int main() {
              int value = 0;
              if (write_output(&value) != 0) return 1;
              return value == 41 ? 0 : 2;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pointer_to_narrow_parameter_is_rejected_before_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-pointer-word-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume_byte(char value) {
                }

                int invalid_call() {
                  int value = 0;
                  consume_byte(&value);
                  return 0;
                }
                """,
                """
                int read_word(int address) {
                  int *pointer = address;
                  return *pointer;
                }

                int main() {
                  int value = 43;
                  return read_word(&value) == 43 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_constant_integer_expression_checks_every_arithmetic_operator(self):
        cases = (
            ("addition", "2147483647+1", "overflow"),
            ("subtraction", "(-2147483647-1)-1", "overflow"),
            ("multiplication", "65536*65536", "overflow"),
            ("division", "(-2147483647-1)/-1", "overflow"),
            ("negation", "-(-2147483647-1)", "overflow"),
            ("division by zero", "4/0", "division by zero"),
            ("decimal literal", "4294967297", "integer literal overflow"),
            (
                "hexadecimal literal",
                "0x100000001",
                "integer literal overflow",
            ),
        )
        for label, expression, diagnostic in cases:
            with self.subTest(operator=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-expression-operator-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), f"char values[{expression}];"
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)

    def test_uint32_maximum_integer_literals_remain_available(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-uint32-literal-edge-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile(
                Path(temporary),
                "int decimal = 4294967295u; int hexadecimal = 0xffffffffu;",
            )
            data = data_path.read_bytes()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(len(data), 8)
        self.assertEqual(data[:8], b"\xff" * 8)

    def test_hexadecimal_integer_literals_require_digits_and_recover(self):
        for literal in ("0x", "0xu"):
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(
                prefix="private-cupidc-hex-digits-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), f"int value={literal};"
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("expected hexadecimal digits", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-hex-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile_after_failure(
                Path(temporary),
                "int broken=0x;",
                "int recovered=7;",
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertEqual(
                data_path.read_bytes()[:4],
                bytes.fromhex("07000000"),
            )

    def test_unsigned_constant_expressions_use_uint32_wraparound(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-unsigned-constant-expression-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile(
                Path(temporary),
                "enum Edge {"
                " BelowSign=0x80000000u-1,"
                " AboveSign=0x7fffffffu+1u,"
                " Wrapped=0x80000000u*2u,"
                " Base=0x80000000u,"
                " BelowBase=Base-1"
                "};",
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            data = data_path.read_bytes()
        self.assertGreaterEqual(len(data), 20)
        self.assertEqual(
            data[:20],
            bytes.fromhex(
                "ffffff7f 00000080 00000000 00000080 ffffff7f"
            ),
        )

    def test_unsigned_enumerators_and_unary_results_keep_their_type(self):
        result = self._compile_and_run(
            """
            enum RuntimeEdge { HighEnumerator = 0x80000000u };

            int main() {
              uint32_t high = 0x80000000u;
              if (!(HighEnumerator > 1u)) return 1;
              if ((double)HighEnumerator != 2147483648.0) return 2;
              if (!(~0u > 1u)) return 3;
              if (!(+high > 1u)) return 4;
              if (!(-1u > 1u)) return 5;
              if ((double)~0u != 4294967295.0) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_relations_survive_local_and_parameter_loads(self):
        result = self._compile_and_run(
            """
            typedef unsigned int RuntimeU32;

            int compare_edges(RuntimeU32 high, RuntimeU32 low) {
              if (!(high >= low)) return 1;
              if (!(high > low)) return 2;
              if (low >= high) return 3;
              if (low > high) return 4;
              if (!(low <= high)) return 5;
              if (!(low < high)) return 6;
              return 0;
            }

            int main() {
              RuntimeU32 high = 0x80000000u;
              uint32_t low = 1u;
              return compare_edges(high, low);
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_relations_survive_array_field_and_pointer_loads(self):
        result = self._compile_and_run(
            """
            typedef unsigned int RuntimeU32;
            typedef struct EdgeBox {
              RuntimeU32 value;
            } EdgeBox;

            RuntimeU32 edge_values[2];

            int pointer_is_above(RuntimeU32 *value, RuntimeU32 floor) {
              return *value > floor;
            }

            int main() {
              EdgeBox box;
              edge_values[0] = 0x80000000u;
              edge_values[1] = 0xffffffffu;
              box.value = edge_values[1];
              if (!(edge_values[0] >= 1u)) return 1;
              if (!(box.value > edge_values[0])) return 2;
              if (!pointer_is_above(&edge_values[0], 1u)) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_division_remainder_and_right_shift_execute(self):
        result = self._compile_and_run(
            """
            int main() {
              uint32_t maximum = 0xffffffffu;
              uint32_t high_bit = 0x80000000u;
              uint32_t quotient = maximum;
              uint32_t shifted = high_bit;

              if (maximum / 2u != 2147483647u) return 1;
              if (maximum % 2u != 1u) return 2;
              if (high_bit >> 31u != 1u) return 3;

              quotient /= 2u;
              shifted >>= 31u;
              if (quotient != 2147483647u) return 4;
              if (shifted != 1u) return 5;

              if (-9 / 2 != -4) return 6;
              if (-9 >> 1 != -5) return 7;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_compound_right_shift_uses_the_promoted_left_operand_type(self):
        result = self._compile_and_run(
            """
            int main() {
              int negative = -8;
              unsigned int unsigned_count = 1u;
              unsigned int high_bit = 0x80000000u;
              int signed_count = 31;

              negative >>= unsigned_count;
              if (negative != -4) return 1;

              high_bit >>= signed_count;
              if (high_bit != 1u) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_conditional_type_does_not_depend_on_arm_order(self):
        result = self._compile_and_run(
            """
            int main() {
              int choose_true = 1;
              if (!((choose_true ? 0xffffffffu : 1) > 2)) return 1;
              if (!((choose_true ? 1 : 0xffffffffu) < -1)) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sizeof_produces_unsigned_size_t_arithmetic(self):
        result = self._compile_and_run(
            """
            int main() {
              return sizeof(int) - 5 > 1 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_unsigned_type_specifiers_report_and_recover(self):
        cases = (
            (
                "floating type",
                "unsigned double broken;",
                "unsigned requires an integer type",
            ),
            (
                "conflicting signs",
                "signed unsigned int broken;",
                "type cannot be both signed and unsigned",
            ),
        )
        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-unsigned-type-{name}-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-unsigned-type-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                "unsigned double broken;",
                """
                int main() {
                  uint32_t high = 0x80000000u;
                  return high > 1u ? 0 : 1;
                }
                """,
            )
            self.assertEqual(
                recovery.returncode,
                0,
                recovery.stdout + recovery.stderr,
            )
            run_result = self._run_i386(root, int(recovery.stdout.strip()))
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_integer_suffix_counts_toward_the_numeric_literal_limit(self):
        accepted_literals = (
            "0" * 94 + "u",
            "0x" + "0" * 92 + "U",
        )
        rejected_literals = (
            "0" * 95 + "u",
            "0x" + "0" * 93 + "U",
        )
        for literal in accepted_literals:
            self.assertEqual(len(literal), 95)
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(
                prefix="private-cupidc-integer-suffix-limit-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, data_path = self._compile(
                    Path(temporary),
                    f"unsigned int value = {literal};\n",
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertEqual(data_path.read_bytes()[:4], b"\0" * 4)

        for literal in rejected_literals:
            self.assertEqual(len(literal), 96)
            with self.subTest(literal=literal), tempfile.TemporaryDirectory(
                prefix="private-cupidc-integer-suffix-overflow-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary),
                    f"unsigned int value = {literal};\n",
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "numeric literal exceeds 95 characters",
                    result.stderr,
                )
                self.assertNotIn("unexpected token", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-integer-suffix-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile_after_failure(
                Path(temporary),
                f"unsigned int broken = {rejected_literals[0]};",
                "int recovered = 7;",
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            self.assertEqual(
                data_path.read_bytes()[:4],
                bytes.fromhex("07000000"),
            )

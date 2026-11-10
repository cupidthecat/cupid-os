"""Private compiler simd execution cases."""

from .harness import (Path, REPO_ROOT, tempfile)

class Simd1Cases:
    def test_halfword_simd_broadcasts_convert_signed_and_unsigned_values(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 u = 65535; I16 s = -2;
                float4 uf; float4 sf; double2 ud; double2 sd;
                uf = _mm_set1_ps(u); sf = _mm_set1_ps(s);
                ud = _mm_set1_pd(u); sd = _mm_set1_pd(s);
                if (uf.x != 65535.0f || uf.w != 65535.0f) return 1;
                if (sf.x != -2.0f || sf.w != -2.0f) return 2;
                if (ud.x != 65535.0 || ud.y != 65535.0) return 3;
                if (sd.x != -2.0 || sd.y != -2.0) return 4;
                return 0;
            }
        ''')

    def test_halfword_casts_reject_vectors_records_and_void(self):
        operands = (
            ('float4 value = {1.0f, 2.0f, 3.0f, 4.0f};', 'value'),
            ('double2 value = {1.0, 2.0};', 'value'),
            ('struct Pair value;', 'value'),
            ('', 'nothing()'),
        )
        for target in ('U16', 'I16'):
            for declaration, expression in operands:
                for aot in (False, True):
                    with self.subTest(target=target, expression=expression, declaration=declaration, aot=aot):
                        source = ('struct Pair { int x; }; void nothing() {} int main() { ' + declaration
                                  + ' return (' + target + ')' + expression + '; }')
                        with tempfile.TemporaryDirectory(prefix='private-halfword-cast-negative-') as temporary:
                            result, _code, _data = self._compile(Path(temporary), source, aot=aot)
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn('halfword cast requires', result.stdout + result.stderr)

    def test_halfword_fixed_calls_reject_pointer_and_simd_arguments(self):
        for expression in ('"address"', '(int *)31', '(float4){1.0, 2.0, 3.0, 4.0}'):
            for aot in (False, True):
                with self.subTest(expression=expression, aot=aot):
                    source = 'int check(U16 value) { return value; } int main() { return check(' + expression + '); }'
                    with tempfile.TemporaryDirectory(prefix='private-halfword-call-negative-') as temporary:
                        result, _code, _data = self._compile(Path(temporary), source, aot=aot)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('argument type', result.stdout + result.stderr)

    def test_simd_parameters_and_returns_survive_nested_calls(self):
        result = self._compile_and_run(
            """
            int float_marker;
            int double_marker;

            float4 merge_float4(float4 left, int marker, float4 right) {
              float_marker = marker;
              return left + right;
            }

            float4 merge_float4_three(float4 first, float4 second,
                                      float4 third) {
              return merge_float4(
                  merge_float4(first, 7, second), 11, third);
            }

            double2 merge_double2(double2 left, int marker, double2 right) {
              double_marker = marker;
              return left + right;
            }

            int main() {
              float4 first = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 second = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 third = {9.0f, 10.0f, 11.0f, 12.0f};
              double2 wide_first = {1.5, 2.5};
              double2 wide_second = {3.0, 4.0};
              float4 floats;
              double2 doubles;

              floats = merge_float4_three(first, second, third);
              doubles = merge_double2(wide_first, 13, wide_second);

              if (float_marker != 11 || double_marker != 13) return 1;
              if (floats.x != 15.0f || floats.y != 18.0f) return 2;
              if (floats.z != 21.0f || floats.w != 24.0f) return 3;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mixed_scalar_and_simd_call_keeps_order_and_raw_words(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-mixed-simd-call-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, code_path, _data = self._compile(
                root,
                """
                int sequence;
                int order_error;

                void advance(int expected) {
                  if (sequence != expected) order_error = 1;
                  sequence += 1;
                }

                float4 next_float4_first() {
                  float4 value = {1.0f, -2.0f, 3.0f, -4.0f};
                  advance(0);
                  return value;
                }

                double next_double() {
                  advance(1);
                  return 1.5;
                }

                double2 next_double2() {
                  double2 value = {3.25, -6.5};
                  advance(2);
                  return value;
                }

                int next_int() {
                  advance(3);
                  return 0x13579bdf;
                }

                float4 next_float4_last() {
                  float4 value = {9.0f, 10.0f, 11.0f, 12.0f};
                  advance(4);
                  return value;
                }

                int inspect(float4 first, double scalar, double2 packed,
                            int word, float4 last) {
                  float lane = first.x;
                  if (*(int *)&lane != 0x3f800000) return 1;
                  lane = first.y;
                  if (*(int *)&lane != (int)0xc0000000) return 2;
                  lane = first.z;
                  if (*(int *)&lane != 0x40400000) return 3;
                  lane = first.w;
                  if (*(int *)&lane != (int)0xc0800000) return 4;

                  int *bits = (int *)&scalar;
                  if (bits[0] != 0 || bits[1] != 0x3ff80000) return 5;
                  double wide_lane = packed.x;
                  bits = (int *)&wide_lane;
                  if (bits[0] != 0 || bits[1] != 0x400a0000) return 6;
                  wide_lane = packed.y;
                  bits = (int *)&wide_lane;
                  if (bits[0] != 0 ||
                      bits[1] != (int)0xc01a0000) return 7;
                  if (word != 0x13579bdf) return 8;

                  lane = last.x;
                  if (*(int *)&lane != 0x41100000) return 9;
                  lane = last.w;
                  if (*(int *)&lane != 0x41400000) return 10;
                  if (sequence != 5 || order_error != 0) return 11;
                  return 0;
                }

                int main() {
                  sequence = 0;
                  order_error = 0;
                  return inspect(
                      next_float4_first(), next_double(), next_double2(),
                      next_int(), next_float4_last());
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            entry_offset = int(result.stdout.strip())
            main_code = code_path.read_bytes()[entry_offset:]
            self.assertIn(
                b"\xe8",
                main_code,
                "expected a direct mixed-width call",
            )
            cleanup_sites = [
                offset
                for offset in range(len(main_code) - 7)
                if main_code[offset] == 0xE8
                and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x3c"
            ]
            self.assertEqual(
                len(cleanup_sites),
                1,
                "expected the mixed call to release 60 argument bytes",
            )
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_simd_calls_clean_the_complete_outgoing_area(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-call-cleanup-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                float4 merge_float4(float4 left, int marker, float4 right) {
                  if (marker == 0) return left;
                  return left + right;
                }

                double2 merge_double2(double2 left, int marker,
                                      double2 right) {
                  if (marker == 0) return left;
                  return left + right;
                }

                int main() {
                  float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
                  double2 wide_left = {1.0, 2.0};
                  double2 wide_right = {3.0, 4.0};
                  float4 floats;
                  double2 doubles;
                  floats = merge_float4(left, 7, right);
                  doubles = merge_double2(wide_left, 9, wide_right);
                  return (int)floats.x + (int)doubles.x;
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            code = code_path.read_bytes()
            main_code = code[entry_offset:]

        self.assertIn(
            b"\x0f\x10\x85\x08\x00\x00\x00",
            code,
            "expected the first vector at EBP + 8",
        )
        self.assertIn(
            b"\x8b\x85\x18\x00\x00\x00",
            code,
            "expected the scalar marker after the first vector",
        )
        self.assertIn(
            b"\x0f\x10\x85\x1c\x00\x00\x00",
            code,
            "expected the second vector after the scalar marker",
        )

        cleanup_sites = [
            offset
            for offset in range(len(main_code) - 7)
            if main_code[offset] == 0xE8
            and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x24"
        ]
        self.assertEqual(
            len(cleanup_sites),
            2,
            "expected both vector calls to release 36 argument bytes",
        )

    def test_active_simd_header_declarations_accept_vector_parameters(self):
        header = (REPO_ROOT / "kernel" / "cpu" / "simd_intrin.h").read_text(
            encoding="utf-8"
        )
        declarations = "\n".join(
            line for line in header.splitlines() if not line.startswith("#")
        )
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-header-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary), declarations + "\nint main() { return 0; }\n"
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_methods_keep_self_before_complete_vector_slots(self):
        result = self._compile_and_run(
            """
            int seen_direct;
            int seen_pointer;

            class Mixer {
              float4 Blend(float4 left, int marker, float4 right) {
                seen_direct = marker;
                return left + right;
              }

              double2 BlendWide(double2 left, int marker, double2 right) {
                seen_pointer = marker;
                return left + right;
              }
            };

            int main() {
              Mixer mixer;
              Mixer *pointer = &mixer;
              float4 float_left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 float_right = {5.0f, 6.0f, 7.0f, 8.0f};
              double2 double_left = {1.5, 2.5};
              double2 double_right = {3.0, 4.0};
              float4 floats;
              double2 doubles;

              floats = mixer.Blend(float_left, 17, float_right);
              doubles = pointer->BlendWide(
                  double_left, 19, double_right);

              if (seen_direct != 17 || seen_pointer != 19) return 1;
              if (floats.x != 6.0f || floats.w != 12.0f) return 2;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_void_simd_method_statements_use_the_shared_layout(self):
        result = self._compile_and_run(
            """
            int observations;

            class Observer {
              void Record(float4 floats, int marker, double2 doubles) {
                if (floats.x == 1.0f && floats.w == 4.0f &&
                    doubles.x == 5.0 && doubles.y == 7.0) {
                  observations += marker;
                }
              }
            };

            int main() {
              Observer observer;
              Observer *pointer = &observer;
              float4 floats = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 doubles = {5.0, 7.0};
              observer.Record(floats, 11, doubles);
              pointer->Record(floats, 13, doubles);
              return observations == 24 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_prototypes_preserve_results_before_definitions(self):
        result = self._compile_and_run(
            """
            float4 add_float4(float4 left, float4 right);
            double2 add_double2(double2 left, double2 right);

            int main() {
              float4 float_left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 float_right = {5.0f, 6.0f, 7.0f, 8.0f};
              double2 double_left = {1.5, 2.5};
              double2 double_right = {3.0, 4.0};
              float4 floats;
              double2 doubles;
              floats = add_float4(float_left, float_right);
              doubles = add_double2(double_left, double_right);
              if (floats.x != 6.0f || floats.w != 12.0f) return 1;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 2;
              return 0;
            }

            float4 add_float4(float4 left, float4 right) {
              return left + right;
            }

            double2 add_double2(double2 left, double2 right) {
              return left + right;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_parameter_updates_change_only_the_callee_copy(self):
        result = self._compile_and_run(
            """
            float4 increment_copy(float4 value) {
              value++;
              return value;
            }

            double2 decrement_copy(double2 value) {
              --value;
              return value;
            }

            int main() {
              float4 original = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 wide_original = {5.0, 7.0};
              float4 changed;
              double2 wide_changed;
              changed = increment_copy(original);
              wide_changed = decrement_copy(wide_original);
              if (original.x != 1.0f || original.w != 4.0f) return 1;
              if (wide_original.x != 5.0 || wide_original.y != 7.0) return 2;
              if (changed.x != 2.0f || changed.w != 5.0f) return 3;
              if (wide_changed.x != 4.0 || wide_changed.y != 6.0) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_const_simd_parameters_reject_mutation_before_recovery(self):
        cases = (
            (
                "float4 assignment",
                """
                int invalid(const float4 value, float4 other) {
                  value = other;
                  return 0;
                }
                """,
                "SIMD assignment requires a modifiable whole-vector lvalue",
            ),
            (
                "double2 update",
                """
                int invalid(const double2 value) {
                  value++;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
        )
        retry_source = """
            float4 identity(float4 value) {
              return value;
            }

            int main() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 result;
              result = identity(value);
              return result.y == 2.0f ? 0 : 1;
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-simd-parameter-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    0,
                    runtime.stdout + runtime.stderr,
                )

    def test_simd_calls_execute_through_private_aot(self):
        result = self._compile_and_run(
            """
            float4 add_float4(float4 left, int marker, float4 right) {
              if (marker != 7) return left;
              return left + right;
            }

            double2 add_double2(double2 left, int marker, double2 right) {
              if (marker != 9) return left;
              return left + right;
            }

            int main() {
              float4 float_left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 float_right = {5.0f, 6.0f, 7.0f, 8.0f};
              double2 double_left = {1.5, 2.5};
              double2 double_right = {3.0, 4.0};
              float4 floats;
              double2 doubles;
              floats = add_float4(float_left, 7, float_right);
              doubles = add_double2(double_left, 9, double_right);
              if (floats.z != 10.0f || floats.w != 12.0f) return 1;
              if (doubles.x != 4.5 || doubles.y != 6.5) return 2;
              return 0;
            }
            """,
            aot=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_feature14_source_executes_through_the_private_simd_path(self):
        result = self._compile_and_run(
            (REPO_ROOT / "bin" / "feature14_simd.cc").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for marker in (
            "[feature14-operator] PASS float=4 double=4",
            "[feature14-array] PASS global=2 local=2 static=2 "
            "sizeof=16 index=1",
            "[feature14-matrix] PASS global=2 local=2 static=2 "
            "sizes=8 index=6 unevaluated=2 canary=4",
            "[feature14-update] PASS direct=6 leaves=3 once=6 payload=8",
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6",
            "[feature14-callback] PASS float4=4 double2=2 calls=2",
            "[feature14-callback-typedef] PASS float4=4 calls=1",
            "[feature14-callback-automatic] PASS local=4 method=4 calls=2",
            "[feature14-callback-field] PASS stored=1 copied=1 cleared=1 "
            "float4=4 calls=1",
            "[feature14-callback-field-call] PASS typedef=1 raw=1 "
            "float4=4 once=1 calls=2",
            "[feature14-callback-raw-array] PASS modes=2 phases=3 "
            "calls=12 stored=1 persistent=1",
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43",
            "[feature14-callback-binding] PASS call=1 ignored=1 callback=0",
            "[feature14-minmax] PASS nan=4 signed_zero=4",
            "[feature14-nan] PASS float_left=",
            "PASS feature14_simd",
        ):
            self.assertIn(marker, result.stderr)

    def test_direct_simd_arithmetic_preserves_lane_values_and_operand_order(self):
        result = self._compile_and_run(
            """
            int main() {
              float4 float_left = {8.0f, 12.0f, 18.0f, 24.0f};
              float4 float_right = {2.0f, 3.0f, 6.0f, 8.0f};
              float4 float_result;
              double2 double_left = {20.0, 30.0};
              double2 double_right = {4.0, 5.0};
              double2 double_result;

              float_result = float_left + float_right;
              if (float_result.x != 10.0f || float_result.w != 32.0f)
                return 1;
              float_result = float_left - float_right;
              if (float_result.y != 9.0f || float_result.z != 12.0f)
                return 2;
              float_result = float_left * float_right;
              if (float_result.x != 16.0f || float_result.w != 192.0f)
                return 3;
              float_result = float_left / float_right;
              if (float_result.y != 4.0f || float_result.z != 3.0f)
                return 4;

              double_result = double_left + double_right;
              if (double_result.x != 24.0 || double_result.y != 35.0)
                return 5;
              double_result = double_left - double_right;
              if (double_result.x != 16.0 || double_result.y != 25.0)
                return 6;
              double_result = double_left * double_right;
              if (double_result.x != 80.0 || double_result.y != 150.0)
                return 7;
              double_result = double_left / double_right;
              if (double_result.x != 5.0 || double_result.y != 6.0)
                return 8;
              double2 fractional_left = {1.5, 2.5};
              double2 fractional_right = {0.5, 2.0};
              double_result = fractional_left * fractional_right;
              double fractional_x = double_result.x;
              int *fractional_bits = (int *)&fractional_x;
              if (fractional_bits[0] != 0) return 9;
              if (fractional_bits[1] != 0x3fe80000) return 10;
              if (double_result.y != 5.0) return 11;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_whole_simd_updates_cover_direct_storage_and_postfix_payloads(self):
        result = self._compile_and_run(
            """
            float4 global_float;
            double2 global_double;

            int update_static_vectors() {
              static float4 saved_float;
              static double2 saved_double;
              static float4 saved_float_array[1];
              static double2 saved_double_matrix[1][1];
              float4 float_seed = {3.0f, 4.0f, 5.0f, 6.0f};
              double2 double_seed = {7.0, 8.0};
              saved_float = float_seed;
              saved_double = double_seed;
              saved_float_array[0] = float_seed;
              saved_double_matrix[0][0] = double_seed;
              ++saved_float;
              saved_double--;
              saved_float_array[0]--;
              ++saved_double_matrix[0][0];
              if (saved_float.x != 4.0f || saved_float.w != 7.0f)
                return 1;
              if (saved_double.x != 6.0 || saved_double.y != 7.0)
                return 2;
              if (saved_float_array[0].x != 2.0f ||
                  saved_float_array[0].w != 5.0f)
                return 3;
              if (saved_double_matrix[0][0].x != 8.0 ||
                  saved_double_matrix[0][0].y != 9.0)
                return 4;
              return 0;
            }

            int main() {
              int nan_bits = 0x7fc12345;
              float nan_value = *(float *)&nan_bits;
              float zero = 0.0f;
              float negative_zero = -zero;
              float4 float_seed = {
                nan_value, negative_zero, 10.0f, -4.0f
              };
              double double_nan;
              int *double_nan_bits = (int *)&double_nan;
              double_nan_bits[0] = 0x89abcdef;
              double_nan_bits[1] = 0x7ff81234;
              double2 double_seed = {double_nan, -0.0};
              float4 old_float;
              double2 old_double;
              float old_float_nan;
              float old_float_zero;
              double old_double_nan;
              double old_double_zero;
              int *old_double_bits;
              float4 local_float;
              double2 local_double;

              local_float = float_seed;
              local_double = double_seed;
              ++local_float;
              local_double--;
              if (local_float.z != 11.0f || local_float.w != -3.0f)
                return 1;
              if (local_double.y != -1.0) return 2;

              global_float = float_seed;
              global_double = double_seed;
              old_float = global_float++;
              old_double = global_double--;

              old_float_nan = old_float.x;
              old_float_zero = old_float.y;
              if (*(int *)&old_float_nan != nan_bits) return 3;
              if (*(int *)&old_float_zero != (int)0x80000000) return 4;
              if (global_float.z != 11.0f || global_float.w != -3.0f)
                return 5;

              old_double_nan = old_double.x;
              old_double_zero = old_double.y;
              old_double_bits = (int *)&old_double_nan;
              if (old_double_bits[0] != 0x89abcdef ||
                  old_double_bits[1] != 0x7ff81234)
                return 6;
              old_double_bits = (int *)&old_double_zero;
              if (old_double_bits[0] != 0 ||
                  old_double_bits[1] != (int)0x80000000)
                return 7;
              if (global_double.y != -1.0) return 8;
              if (update_static_vectors() != 0) return 9;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_whole_simd_updates_cover_indexed_array_leaves_once(self):
        result = self._compile_and_run(
            """
            float4 global_line[2];
            float4 global_payload[1];
            double2 global_matrix[2][2];
            float4 global_cube[2][2][2];
            int outer_calls;
            int middle_calls;
            int inner_calls;

            int next_outer() { outer_calls += 1; return 1; }
            int next_middle() { middle_calls += 1; return 0; }
            int next_inner() { inner_calls += 1; return 1; }

            int main() {
              float4 float_seed = {2.0f, 4.0f, 6.0f, 8.0f};
              double2 double_seed = {10.0, 20.0};
              float4 old_float;
              float4 old_payload;
              double2 old_double;
              int nan_bits = 0x7fc54321;
              float nan_value = *(float *)&nan_bits;
              float zero = 0.0f;
              float negative_zero = -zero;
              float4 payload_seed = {
                nan_value, negative_zero, 12.0f, -8.0f
              };
              float old_payload_nan;
              float old_payload_zero;

              global_line[1] = float_seed;
              global_payload[0] = payload_seed;
              global_matrix[1][0] = double_seed;
              global_cube[1][0][1] = float_seed;

              old_float = global_line[next_outer()]++;
              old_payload = global_payload[0]++;
              old_double = --global_matrix[next_outer()][next_middle()];
              ++global_cube[next_outer()][next_middle()][next_inner()];

              if (old_float.x != 2.0f || old_float.w != 8.0f) return 1;
              old_payload_nan = old_payload.x;
              old_payload_zero = old_payload.y;
              if (*(int *)&old_payload_nan != nan_bits) return 2;
              if (*(int *)&old_payload_zero != (int)0x80000000) return 3;
              if (global_line[1].x != 3.0f ||
                  global_line[1].w != 9.0f)
                return 4;
              if (old_double.x != 9.0 || old_double.y != 19.0) return 5;
              if (global_matrix[1][0].x != 9.0 ||
                  global_matrix[1][0].y != 19.0)
                return 6;
              if (global_cube[1][0][1].x != 3.0f ||
                  global_cube[1][0][1].w != 9.0f)
                return 7;
              if (outer_calls != 3 || middle_calls != 2 || inner_calls != 1)
                return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_whole_simd_updates_execute_through_private_aot(self):
        result = self._compile_and_run(
            """
            float4 values[1];
            int main() {
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 old;
              values[0] = seed;
              old = values[0]++;
              if (old.x != 1.0f || old.w != 4.0f) return 1;
              if (values[0].x != 2.0f || values[0].w != 5.0f) return 2;
              return 0;
            }
            """,
            aot=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_whole_simd_updates_keep_persistent_state(self):
        result = self._compile_repl_and_run(
            (
                "float4 repl_float;",
                "double2 repl_double[1];",
                """
                int update_repl_vectors() {
                  float4 float_seed = {4.0f, 5.0f, 6.0f, 7.0f};
                  double2 double_seed = {8.0, 9.0};
                  float4 old_float;
                  repl_float = float_seed;
                  repl_double[0] = double_seed;
                  old_float = repl_float++;
                  --repl_double[0];
                  if (old_float.x != 4.0f || old_float.w != 7.0f)
                    return 1;
                  return 0;
                }
                """,
                """
                int verify_repl_vectors() {
                  if (update_repl_vectors() != 0) return 1;
                  if (repl_float.x != 5.0f || repl_float.w != 8.0f)
                    return 2;
                  if (repl_double[0].x != 7.0 ||
                      repl_double[0].y != 8.0)
                    return 3;
                  return 0;
                }
                verify_repl_vectors;
                """,
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_const_simd_update_is_rejected_before_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-const-simd-update-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    Path(temporary),
                    (
                        "typedef const float4 ReplConstVector; "
                        "ReplConstVector repl_value;",
                        "repl_value++;",
                        "ReplConstVector preserved_value; preserved_value.x;",
                        "float4 mutable_value; ++mutable_value;",
                    ),
                )
            )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "SIMD increment or decrement requires a modifiable "
            "whole-vector lvalue",
            result.stderr,
        )

    def test_const_simd_assignments_recover_in_the_same_state(self):
        cases = (
            (
                "direct assignment",
                """
                int main() {
                  const float4 destination;
                  float4 source;
                  destination = source;
                  return 0;
                }
                """,
            ),
            (
                "indexed compound assignment",
                """
                int main() {
                  const double2 destination[1][1];
                  double2 source;
                  destination[0][0] += source;
                  return 0;
                }
                """,
            ),
        )
        retry_source = "int main() { float4 value; value += value; return 0; }"
        for label, failing_source in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-simd-assignment-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    failing_source,
                    retry_source,
                    same_state=True,
                )
            self.assertEqual(
                result.returncode,
                0,
                f"{label}: {result.stdout}{result.stderr}",
            )
            self.assertIn(
                "SIMD assignment requires a modifiable whole-vector lvalue",
                result.stderr,
                label,
            )

    def test_typedef_const_simd_values_remain_readable(self):
        result = self._compile_and_run(
            """
            typedef const float4 ConstVector;
            typedef ConstVector ConstVectorAlias;

            int main() {
              ConstVectorAlias value = {1.0f, 2.0f, 3.0f, 4.0f};
              ConstVectorAlias values[1];
              float4 copy;
              copy = value;
              if (copy.x != 1.0f || copy.w != 4.0f) return 1;
              copy = values[0];
              if (copy.x != 0.0f || copy.w != 0.0f) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_typedef_const_simd_mutations_recover_in_the_same_state(self):
        assignment_message = (
            "SIMD assignment requires a modifiable whole-vector lvalue"
        )
        update_message = (
            "SIMD increment or decrement requires a modifiable "
            "whole-vector lvalue"
        )
        cases = (
            (
                "alias-chain direct assignment",
                """
                typedef const float4 ConstVector;
                typedef ConstVector ConstVectorAlias;
                int main() {
                  ConstVectorAlias destination;
                  float4 source;
                  destination = source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "direct compound assignment",
                """
                typedef const double2 ConstVector;
                int main() {
                  ConstVector destination;
                  double2 source;
                  destination += source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "direct prefix update",
                """
                typedef const float4 ConstVector;
                int main() {
                  ConstVector value;
                  ++value;
                  return 0;
                }
                """,
                update_message,
            ),
            (
                "indexed assignment",
                """
                typedef const float4 ConstVector;
                int main() {
                  ConstVector values[1];
                  float4 source;
                  values[0] = source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "alias-chain indexed compound assignment",
                """
                typedef const double2 ConstVector;
                typedef ConstVector ConstVectorAlias;
                int main() {
                  ConstVectorAlias values[1][1];
                  double2 source;
                  values[0][0] += source;
                  return 0;
                }
                """,
                assignment_message,
            ),
            (
                "typedef-array indexed postfix update",
                """
                typedef const float4 ConstVector;
                typedef ConstVector ConstVectorArray[1];
                int main() {
                  ConstVectorArray values;
                  values[0]++;
                  return 0;
                }
                """,
                update_message,
            ),
        )
        retry_source = "int main() { float4 value; value++; return 0; }"
        for label, failing_source, message in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-typedef-const-simd-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    failing_source,
                    retry_source,
                    same_state=True,
                )
            self.assertEqual(
                result.returncode,
                0,
                f"{label}: {result.stdout}{result.stderr}",
            )
            self.assertIn(message, result.stderr, label)

    def test_simd_update_rejections_recover_in_the_same_state(self):
        cases = (
            (
                "const automatic vector",
                """
                int main() {
                  const float4 value = {1, 2, 3, 4};
                  ++value;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "trailing-const global vector",
                """
                double2 const value;
                int main() {
                  value--;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "const block-static vector",
                """
                int main() {
                  static const double2 value;
                  value++;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "const one-dimensional vector leaf",
                """
                int main() {
                  const float4 values[1];
                  values[0]--;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "const three-dimensional vector leaf",
                """
                int main() {
                  const float4 values[1][1][1];
                  --values[0][0][0];
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "computed vector",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 right = {5, 6, 7, 8};
                  (left + right)++;
                  return 0;
                }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
            (
                "vector lane",
                """
                int main() {
                  float4 value = {1, 2, 3, 4};
                  value.x++;
                  return 0;
                }
                """,
                "SIMD lane increment or decrement is not supported",
            ),
            (
                "incomplete row",
                """
                int main() {
                  double2 values[2][2];
                  values[0]++;
                  return 0;
                }
                """,
                "SIMD array row values are not supported",
            ),
            (
                "prefix incomplete row",
                """
                int main() {
                  float4 values[2][2];
                  ++values[0];
                  return 0;
                }
                """,
                "SIMD array row values are not supported",
            ),
            (
                "record field",
                """
                struct Sample { float4 value; };
                int main() {
                  struct Sample sample;
                  sample.value++;
                  return 0;
                }
                """,
                "SIMD record-field increment or decrement is not supported",
            ),
            (
                "SIMD pointer",
                "float4 *values;",
                "SIMD pointer types are not supported",
            ),
            (
                "call result",
                """
                float4 make_value() {
                  float4 value = {1, 2, 3, 4};
                  return value;
                }
                int main() { return make_value()++; }
                """,
                "SIMD increment or decrement requires a modifiable "
                "whole-vector lvalue",
            ),
        )
        retry_source = "int main() { float4 value = {1, 2, 3, 4}; ++value; return value.x != 2.0f; }"
        for label, failing_source, message in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-update-recovery-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    failing_source,
                    retry_source,
                    same_state=True,
                )
            self.assertEqual(
                result.returncode,
                0,
                f"{label}: {result.stdout}{result.stderr}",
            )
            self.assertIn(message, result.stderr, label)

    def test_simd_add_and_multiply_keep_source_order_in_machine_code(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-order-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                int main() {
                  float4 float_left;
                  float4 float_right;
                  float4 float_result;
                  double2 double_left;
                  double2 double_right;
                  double2 double_result;
                  float_result = float_left + float_right;
                  float_result = float_left * float_right;
                  float_result = _mm_add_ps(float_left, float_right);
                  float_result = _mm_mul_ps(float_left, float_right);
                  double_result = double_left + double_right;
                  double_result = double_left * double_right;
                  double_result = _mm_add_pd(double_left, double_right);
                  double_result = _mm_mul_pd(double_left, double_right);
                  return 0;
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            code = code_path.read_bytes()

        add_left = b"\x0f\x58\xc8\x0f\x28\xc1"
        mul_left = b"\x0f\x59\xc8\x0f\x28\xc1"
        self.assertEqual(code.count(add_left), 4)
        self.assertEqual(code.count(mul_left), 4)
        self.assertEqual(code.count(b"\x66" + add_left), 2)
        self.assertEqual(code.count(b"\x66" + mul_left), 2)
        self.assertNotIn(b"\x0f\x58\xc1", code)
        self.assertNotIn(b"\x0f\x59\xc1", code)

    def test_simd_array_parameters_have_a_useful_diagnostic(self):
        cases = (
            "void probe(float4 values[]) {}",
            "class Probe { void Check(double2 values[]) {} };",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-parameter-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "SIMD array parameters are not supported", result.stderr
            )

    def test_fixed_simd_arrays_execute_across_supported_storage(self):
        result = self._compile_and_run(
            """
            float4 global_floats[3];
            int global_sentinel;
            double2 global_doubles[2];

            int touch_static_array() {
              static float4 saved[2];
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              if (saved[0].w == 0.0f) {
                saved[0] = seed;
                return 1;
              }
              saved[1] = saved[0] * seed;
              if (saved[1].x != 1.0f || saved[1].w != 16.0f)
                return 2;
              return 0;
            }

            int main() {
              float4 local_floats[2];
              double2 local_doubles[2];
              float4 float_seed = {8.0f, 12.0f, 18.0f, 24.0f};
              float4 float_step = {2.0f, 3.0f, 6.0f, 8.0f};
              double2 double_seed = {20.0, 30.0};
              double2 double_step = {4.0, 5.0};

              global_sentinel = 73;
              global_floats[0] = float_seed;
              global_floats[1] = float_step;
              global_floats[1] += float_seed;
              global_floats[1] -= float_step;
              global_floats[1] *= float_step;
              global_floats[1] /= float_step;
              global_doubles[0] = double_seed;
              global_doubles[1] = double_step;
              global_doubles[1] += double_seed;
              global_doubles[1] -= double_step;
              global_doubles[1] *= double_step;
              global_doubles[1] /= double_step;

              local_floats[0] = global_floats[0];
              local_floats[1] = global_floats[1];
              local_doubles[0] = global_doubles[0];
              local_doubles[1] = global_doubles[1];
              int selected = 0;
              local_floats[selected++] += float_step;

              if (selected != 1) return 1;
              if (local_floats[0].x != 10.0f ||
                  local_floats[1].w != 24.0f)
                return 2;
              if (local_doubles[0].x != 20.0 ||
                  local_doubles[1].y != 30.0)
                return 3;
              if (global_floats[2].z != 0.0f) return 4;
              if (sizeof(*global_floats) != 16) return 5;
              if (sizeof(*local_doubles) != 16) return 6;
              if (global_sentinel != 73) return 7;
              if (touch_static_array() != 1) return 8;
              if (touch_static_array() != 0) return 9;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_persistent_repl_simd_arrays_keep_values_between_lines(self):
        result = self._compile_and_run(
            """
            float4 persistent_floats[2];
            double2 persistent_doubles[2];
            int verify_persistent_vectors() {
              float4 float_seed = {2.0f, 4.0f, 6.0f, 8.0f};
              double2 double_seed = {3.0, 9.0};
              persistent_floats[1] = float_seed;
              persistent_doubles[1] = double_seed;
              persistent_floats[1] *= float_seed;
              persistent_doubles[1] += double_seed;
              if (persistent_floats[1].z != 36.0f) return 1;
              if (persistent_doubles[1].y != 18.0) return 2;
              if (sizeof(*persistent_floats) != 16) return 3;
              return 0;
            }
            verify_persistent_vectors();
            """,
            repl=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_simd_matrices_keep_row_metadata_between_units(self):
        result = self._compile_repl_and_run(
            (
                "float4 repl_matrix[2][3];",
                "double2 repl_cube[2][2][1];",
                "int repl_canary = 53;",
                """
                int verify_repl_vectors() {
                  float4 float_seed = {2.0f, 4.0f, 6.0f, 8.0f};
                  double2 double_seed = {3.0, 9.0};
                  repl_matrix[1][2] = float_seed;
                  repl_matrix[1][2] *= float_seed;
                  repl_cube[1][0][0] = double_seed;
                  repl_cube[1][0][0] += double_seed;
                  if (repl_matrix[1][2].z != 36.0f) return 1;
                  if (repl_cube[1][0][0].y != 18.0) return 2;
                  if (sizeof(*repl_matrix) != 48) return 3;
                  if (sizeof(**repl_matrix) != 16) return 4;
                  if (sizeof(*repl_cube) != 32) return 5;
                  if (sizeof(**repl_cube) != 16) return 6;
                  if (sizeof(***repl_cube) != 16) return 7;
                  if (repl_canary != 53) return 8;
                  return 0;
                }
                """,
                "verify_repl_vectors;",
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_min_max_keep_second_operand_nan_and_zero_rules(self):
        result = self._compile_and_run(
            """
            int main() {
              float float_nan = 0.0f / 0.0f;
              float float_positive_zero = 0.0f;
              float float_negative_zero = -float_positive_zero;
              float4 float_first = {
                float_nan, float_positive_zero,
                float_nan, float_positive_zero
              };
              float4 float_second = {
                5.0f, float_negative_zero,
                -7.0f, float_negative_zero
              };
              float4 float_min;
              float4 float_max;
              float float_lane;

              double double_nan = 0.0 / 0.0;
              double double_positive_zero = 0.0;
              double double_negative_zero = -double_positive_zero;
              double2 double_first = {double_nan, double_positive_zero};
              double2 double_second = {9.0, double_negative_zero};
              double2 double_min;
              double2 double_max;
              double double_lane;
              int *double_bits;

              float_min = _mm_min_ps(float_first, float_second);
              float_max = _mm_max_ps(float_first, float_second);
              if (float_min.x != 5.0f || float_max.z != -7.0f) return 1;
              float_lane = float_min.y;
              if (*(int *)&float_lane != (int)0x80000000) return 2;
              float_lane = float_max.w;
              if (*(int *)&float_lane != (int)0x80000000) return 3;

              double_min = _mm_min_pd(double_first, double_second);
              double_max = _mm_max_pd(double_first, double_second);
              if (double_min.x != 9.0 || double_max.x != 9.0) return 4;
              double_lane = double_min.y;
              double_bits = (int *)&double_lane;
              if (double_bits[1] != (int)0x80000000) return 5;
              double_lane = double_max.y;
              double_bits = (int *)&double_lane;
              if (double_bits[1] != (int)0x80000000) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_two_dimensional_simd_arrays_keep_vector_rows(self):
        result = self._compile_and_run(
            """
            int leading_canary = 31;
            float4 vectors[2][2];
            int trailing_canary = 37;
            int sizeof_index_calls;

            int next_sizeof_index() {
              sizeof_index_calls += 1;
              return 1;
            }

            int main() {
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              vectors[1][0] = seed;
              vectors[1][0] += seed;
              if (vectors[1][0].x != 2.0f) return 1;
              if (vectors[1][0].w != 8.0f) return 2;
              if (vectors[0][1].z != 0.0f) return 3;
              if (sizeof(*vectors) != 32) return 4;
              if (sizeof(**vectors) != 16) return 5;
              if (sizeof(vectors[next_sizeof_index()]) != 32) return 6;
              if (sizeof(vectors[0][next_sizeof_index()]) != 16) return 7;
              if (sizeof_index_calls != 0) return 8;
              if (leading_canary != 31) return 9;
              if (trailing_canary != 37) return 10;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_matrices_cover_local_static_and_three_dimensional_storage(self):
        result = self._compile_and_run(
            """
            float4 global_cube[2][2][2];
            int global_canary = 41;
            int outer_calls;
            int middle_calls;
            int inner_calls;

            int next_outer() {
              outer_calls += 1;
              return 1;
            }

            int next_middle() {
              middle_calls += 1;
              return 0;
            }

            int next_inner() {
              inner_calls += 1;
              return 1;
            }

            int touch_static_cube() {
              static double2 saved_cube[2][2][2];
              double2 seed = {3.0, 9.0};
              saved_cube[1][1][0] += seed;
              if (saved_cube[1][1][0].x != 3.0) return 1;
              if (saved_cube[1][1][0].y != 9.0) return 2;
              if (sizeof(*saved_cube) != 64) return 3;
              if (sizeof(**saved_cube) != 32) return 4;
              if (sizeof(***saved_cube) != 16) return 5;
              return 0;
            }

            int main() {
              int leading_canary = 43;
              double2 local_matrix[2][3];
              int trailing_canary = 47;
              float4 seed = {8.0f, 12.0f, 18.0f, 24.0f};
              float4 step = {2.0f, 3.0f, 6.0f, 8.0f};
              double2 local_seed = {20.0, 30.0};
              double2 local_step = {4.0, 5.0};

              global_cube[1][0][1] = seed;
              global_cube[next_outer()][next_middle()][next_inner()] += step;
              global_cube[1][0][1] -= step;
              global_cube[1][0][1] *= step;
              global_cube[1][0][1] /= step;
              local_matrix[1][2] = local_seed;
              local_matrix[1][2] += local_step;
              local_matrix[1][2] -= local_step;
              local_matrix[1][2] *= local_step;
              local_matrix[1][2] /= local_step;

              if (global_cube[next_outer()][next_middle()][next_inner()].x !=
                  8.0f)
                return 1;
              if (outer_calls != 2 || middle_calls != 2 || inner_calls != 2)
                return 2;
              if (global_cube[1][0][1].w != 24.0f) return 3;
              if (global_cube[0][1][1].z != 0.0f) return 4;
              if (local_matrix[1][2].x != 20.0) return 5;
              if (local_matrix[1][2].y != 30.0) return 6;
              if (sizeof(*global_cube) != 64) return 7;
              if (sizeof(**global_cube) != 32) return 8;
              if (sizeof(***global_cube) != 16) return 9;
              if (sizeof(*local_matrix) != 48) return 10;
              if (sizeof(**local_matrix) != 16) return 11;
              if (touch_static_cube() != 0) return 12;
              if (leading_canary != 43 || trailing_canary != 47) return 13;
              if (global_canary != 41) return 14;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unit_inner_simd_dimensions_retain_array_rank(self):
        result = self._compile_and_run(
            """
            float4 global_matrix[2][1];

            int main() {
              static float4 saved[2][1];
              double2 local_cube[2][2][1];
              float4 seed = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 wide = {5.0, 7.0};

              global_matrix[1][0] = seed;
              global_matrix[1][0] += seed;
              saved[1][0] = seed;
              local_cube[1][0][0] = wide;
              local_cube[1][0][0] *= wide;

              if (global_matrix[1][0].x != 2.0f) return 1;
              if (global_matrix[1][0].w != 8.0f) return 2;
              if (local_cube[1][0][0].x != 25.0) return 3;
              if (local_cube[1][0][0].y != 49.0) return 4;
              if (sizeof(*global_matrix) != 16) return 5;
              if (sizeof(**global_matrix) != 16) return 6;
              if (sizeof(global_matrix[0]) != 16) return 7;
              if (sizeof((global_matrix[0])) != 16) return 8;
              if (sizeof(*local_cube) != 32) return 9;
              if (sizeof(**local_cube) != 16) return 10;
              if (sizeof(***local_cube) != 16) return 11;
              if (saved[1][0].x != 1.0f) return 12;
              if (saved[1][0].w != 4.0f) return 13;
              if (sizeof(*saved) != 16) return 14;
              if (sizeof(**saved) != 16) return 15;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_array_row_assignment_requires_every_subscript(self):
        cases = (
            "int main() { float4 values[2][2]; float4 value; "
            "values[1] = value; return 0; }",
            "int main() { float4 values[2][1]; float4 value; "
            "values[1] = value; return 0; }",
            "int main() { double2 values[2][2][2]; double2 value; "
            "values[1][0] = value; return 0; }",
            "int main() { double2 values[2][2][1]; double2 value; "
            "values[1][0] = value; return 0; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-row-assignment-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "SIMD array assignment requires every subscript",
                result.stderr,
            )

    def test_parenthesized_simd_rows_continue_subscript_chains(self):
        result = self._compile_and_run(
            """
            float4 matrix[2][2];
            double2 cube[2][2][2];

            int main() {
              float4 float_seed = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 double_seed = {5.0, 7.0};

              matrix[1][0] = float_seed;
              cube[1][0][1] = double_seed;

              if ((matrix[1])[0].z != 3.0f) return 1;
              if (((cube[1])[0])[1].y != 7.0) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_simd_array_rows_do_not_escape_as_untyped_pointers(self):
        cases = (
            "int main() { float4 values[2][2]; return values[0] + 2 != "
            "values[1]; }",
            "int main() { double2 values[2][2][2]; "
            "return values[0][0] + 2 != values[0][1]; }",
            "int main() { float4 values[2][2]; if (values[0]) return 1; "
            "return 0; }",
            "int main() { float4 values[2][2]; return !values[0]; }",
            "int main() { float4 values[2][2]; return (int*)values[0] "
            "!= 0; }",
            "int main() { float4 values[2][2]; return (values[0]); }",
            "int main() { float4 values[2][2]; return (values[0]) + 2 "
            "!= values[1]; }",
            "int consume_row(int value) { return value; } "
            "int main() { float4 values[2][2]; "
            "return consume_row((values[0])); }",
            "int main() { float4 values[2][2]; return (int*)(values[0]) "
            "!= 0; }",
            "int main() { float4 values[2][2]; if ((values[0])) return 1; "
            "return 0; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-row-escape-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "SIMD array row values are not supported",
                result.stderr,
            )

    def test_simd_operator_diagnostics_name_the_supported_boundary(self):
        cases = (
            (
                "different vector widths",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  double2 right = {1, 2};
                  float4 result;
                  result = left + right;
                  return 0;
                }
                """,
                "SIMD operator requires matching float4 or double2 operands",
            ),
            (
                "vector and scalar",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 result;
                  result = left + 1;
                  return 0;
                }
                """,
                "SIMD operator requires matching float4 or double2 operands",
            ),
            (
                "unsupported remainder",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 right = {1, 2, 3, 4};
                  float4 result;
                  result = left % right;
                  return 0;
                }
                """,
                "SIMD operator supports only +, -, *, and /",
            ),
            (
                "unsupported comparison",
                """
                int main() {
                  float4 left = {1, 2, 3, 4};
                  float4 right = {1, 2, 3, 4};
                  if (left == right) return 1;
                  return 0;
                }
                """,
                "SIMD operator supports only +, -, *, and /",
            ),
        )

        for label, source, message in cases:
            with self.subTest(operation=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-operator-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(message, result.stderr)

    def test_simd_array_assignment_diagnostics_name_the_required_type(self):
        cases = (
            (
                "float4 from double2",
                """
                float4 values[1];
                int main() {
                  double2 value = {1, 2};
                  values[0] = value;
                  return 0;
                }
                """,
                "float4 array assignment requires a float4 value",
            ),
            (
                "unsupported bitwise compound",
                """
                double2 values[1];
                int main() {
                  double2 value = {1, 2};
                  values[0] &= value;
                  return 0;
                }
                """,
                "SIMD array compound assignment supports only +=, -=, *=, and /=",
            ),
        )

        for label, source, message in cases:
            with self.subTest(assignment=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-assignment-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(message, result.stderr)

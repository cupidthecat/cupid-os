"""Private compiler floating execution cases."""

from .harness import (Path, tempfile)

class Floating1Cases:
    def test_halfword_initializers_and_float_assignments_convert(self):
        self._assert_halfword_runtime('''
            int main() {
                U16 u = 65537; I16 s = 65535;
                if (u != 1 || s != -1) return 1;
                u = 65535.75; s = -32768.75;
                if (u != 65535 || s != -32768) return 2;
                unsigned short second = 12345.75;
                short third = -12345.75;
                if (second != 12345 || third != -12345) return 3;
                return 0;
            }
        ''')

    def test_halfword_floating_compounds_compute_before_narrow_store(self):
        self._assert_halfword_runtime('''
            struct Pair { I16 a; U16 b; };
            int main() {
                I16 scalar = 1;
                scalar += -0.75;
                if (scalar != 0) return 1;
                U16 values[2]; values[0] = 3; values[1] = 54321;
                values[0] *= 0.5;
                if (values[0] != 1 || values[1] != 54321) return 2;
                struct Pair p; p.a = -3; p.b = 31;
                p.a /= 2.0;
                if (p.a != -1 || p.b != 31) return 3;
                return 0;
            }
        ''')

    def test_two_double_parameters_keep_their_full_cdecl_width(self):
        result = self._compile_and_run(
            """
            double combine(double first, double second) {
              return first * 10.0 + second;
            }

            int main() {
              return (int)combine(3.0, 4.0);
            }
            """
        )
        self.assertEqual(result.returncode, 34, result.stdout + result.stderr)

    def test_double_then_integer_arguments_use_cdecl_source_order(self):
        result = self._compile_and_run(
            """
            double combine(double first, int second) {
              return first * 10.0 + (double)second;
            }

            int main() {
              return (int)combine(3.0, 4);
            }
            """
        )
        self.assertEqual(result.returncode, 34, result.stdout + result.stderr)

    def test_pointer_to_floating_parameter_is_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-pointer-float-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void consume_float(float value) {
                }

                int main() {
                  int value = 0;
                  consume_float(&value);
                  return 0;
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "cdecl argument type does not match fixed parameter",
            result.stderr,
        )

    def test_method_expressions_and_statements_convert_integer_zero_to_double(self):
        result = self._compile_and_run(
            """
            int recorded_kind;
            double recorded_number;
            int recorded_offset;
            int recorded_length;

            class Probe {
              int Inspect(int kind, double number, int offset, int length) {
                if (kind != 17) return 1;
                if (number != 0.0) return 2;
                if (offset != -31) return 3;
                if (length != 37) return 4;
                return 0;
              }

              void Record(int kind, double number,
                          int offset, int length) {
                recorded_kind = kind;
                recorded_number = number;
                recorded_offset = offset;
                recorded_length = length;
              }
            };

            int main() {
              Probe probe;
              int expression_status = probe.Inspect(17, 0, -31, 37);
              if (expression_status != 0) return expression_status;
              probe.Record(23, 0, -41, 47);
              if (recorded_kind != 23) return 5;
              if (recorded_number != 0.0) return 6;
              if (recorded_offset != -41) return 7;
              if (recorded_length != 47) return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_variadic_float_tail_widens_and_cleans_its_double_slot(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-variadic-float-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                void consume(int marker, ...) {
                }

                int main() {
                  float fraction = 2.5f;
                  consume(7, fraction, 13);
                  return 0;
                }
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            main_code = code_path.read_bytes()[entry_offset:]

        self.assertIn(
            b"\xf3\x0f\x5a\xc0",
            main_code,
            "expected CVTSS2SD for the variadic float argument",
        )
        cleanup_sites = [
            offset
            for offset in range(len(main_code) - 7)
            if main_code[offset] == 0xE8
            and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x10"
        ]
        self.assertEqual(
            len(cleanup_sites),
            1,
            "expected one variadic call followed by ADD ESP, 16",
        )

    def test_browser_token_call_converts_integer_zero_to_double(self):
        result = self._compile_and_run(
            """
            int seen_kind;
            double seen_number;
            int seen_offset;
            int seen_length;
            int seen_line;

            void emit_number_token(int kind, double number,
                                   int string_offset, int string_length,
                                   int line) {
              seen_kind = kind;
              seen_number = number;
              seen_offset = string_offset;
              seen_length = string_length;
              seen_line = line;
            }

            int main() {
              emit_number_token(17, 0, -31, 37, 43);
              if (seen_kind != 17) return 1;
              if (seen_number != 0.0) return 2;
              if (seen_offset != -31) return 3;
              if (seen_length != 37) return 4;
              if (seen_line != 43) return 5;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsigned_values_convert_exactly_to_float_and_double(self):
        result = self._compile_and_run(
            """
            double global_maximum_double = 0xffffffffu;
            float global_high_float = 0x80000000u;

            double take_double(double value) { return value; }
            float take_float(float value) { return value; }
            uint32_t return_maximum() { return 0xffffffffu; }

            int main() {
              uint32_t zero = 0u;
              uint32_t below_sign = 0x7fffffffu;
              uint32_t high_bit = 0x80000000u;
              uint32_t maximum = return_maximum();
              double zero_double = (double)zero;
              double below_double = below_sign;
              double high_double = (double)high_bit;
              double maximum_double = take_double(maximum);
              float high_float = (float)high_bit;
              float maximum_float = take_float(maximum);

              if (zero_double != 0.0) return 1;
              if (below_double != 2147483647.0) return 2;
              if (high_double != 2147483648.0) return 3;
              if (maximum_double != 4294967295.0) return 4;
              if (high_float != 2147483648.0f) return 5;
              if (maximum_float != 4294967296.0f) return 6;
              if (maximum + 0.25 != 4294967295.25) return 7;
              if (0.25 + maximum != 4294967295.25) return 8;
              if ((double)(maximum & 0x80000000u) != 2147483648.0)
                return 9;
              if ((double)(high_bit >> 1u) != 1073741824.0) return 10;
              if ((double)-3 != -3.0) return 11;
              if (global_maximum_double != 4294967295.0) return 12;
              if (global_high_float != 2147483648.0f) return 13;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_remainder_assignment_reports_and_recovers(self):
        failing_source = """
            int main() {
              float value = 5.0f;
              value %= 2;
              return 0;
            }
        """
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-remainder-assignment-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            failure, _code_path, _data_path = self._compile(
                root, failing_source
            )
            self.assertEqual(
                failure.returncode,
                2,
                failure.stdout + failure.stderr,
            )
            self.assertIn(
                "remainder compound assignment requires an integer lvalue",
                failure.stderr,
            )

            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                failing_source,
                """
                int main() {
                  uint32_t value = 0xffffffffu;
                  value %= 10u;
                  return value == 5u ? 0 : 1;
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

    def test_unsigned_function_and_method_returns_convert_to_float_lanes(self):
        result = self._compile_and_run(
            """
            double maximum_as_double() { return 0xffffffffu; }
            float high_as_float() { return 0x80000000u; }

            class Converter {
              double Maximum() { return 0xffffffffu; }
              float High() { return 0x80000000u; }
            };

            int main() {
              Converter converter;
              Converter *pointer = &converter;
              if (maximum_as_double() != 4294967295.0) return 1;
              if (high_as_float() != 2147483648.0f) return 2;
              if (converter.Maximum() != 4294967295.0) return 3;
              if (pointer->High() != 2147483648.0f) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_values_convert_to_unsigned_words_across_runtime_forms(self):
        result = self._compile_and_run(
            """
            uint32_t global_from_float = 4294967040.0f;
            uint32_t global_from_double = 4294967295.9999995;

            uint32_t take_unsigned(uint32_t value) { return value; }
            uint32_t return_unsigned() { return 2147483648.75; }
            uint32_t block_static_unsigned() {
              static uint32_t value = 4294967295.75;
              return value;
            }

            struct Box { uint32_t value; };

            int main() {
              uint32_t cast_negative = (uint32_t)-0.9999999999999999;
              uint32_t cast_low = (uint32_t)2147483647.9999998;
              uint32_t initialized_high = 2147483648.75;
              uint32_t assigned_maximum;
              uint32_t pointed_value = 1u;
              uint32_t *pointer = &pointed_value;
              uint32_t values[3];
              struct Box box;

              assigned_maximum = 4294967295.9999995;
              *pointer = -0.75f;
              values[0] = 0.99999994f;
              values[1] = 2147483904.0f;
              values[2] = 4294967040.0f;
              box.value = 2147483649.75;

              if (cast_negative != 0u) return 1;
              if (cast_low != 0x7fffffffu) return 2;
              if (initialized_high != 0x80000000u) return 3;
              if (assigned_maximum != 0xffffffffu) return 4;
              if (pointed_value != 0u) return 5;
              if (values[0] != 0u) return 6;
              if (values[1] != 0x80000100u) return 7;
              if (values[2] != 0xffffff00u) return 8;
              if (box.value != 0x80000001u) return 9;
              if (take_unsigned(4294967295.75) != 0xffffffffu) return 10;
              if (return_unsigned() != 0x80000000u) return 11;
              if (block_static_unsigned() != 0xffffffffu) return 12;
              if (global_from_float != 0xffffff00u) return 13;
              if (global_from_double != 0xffffffffu) return 14;
              if ((uint32_t)-0.0 != 0u) return 15;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unsupported_floating_to_word_forms_report_and_recover(self):
        cases = (
            (
                "compound assignment",
                "int main() { uint32_t value = 3u; value *= 0.5; return 0; }",
                "floating compound assignment to unsigned is not supported",
            ),
            (
                "pointer target",
                "int main() { uint32_t *value = (uint32_t *)1.5; return 0; }",
                "floating to pointer conversion is not supported",
            ),
            (
                "vector source",
                "int main() { double2 value; return (uint32_t)value; }",
                "conversion to unsigned requires a scalar word or floating value",
            ),
        )
        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix="private-cupidc-float-to-word-form-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-float-to-unsigned-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                "int main() { uint32_t value = 3u; value *= 0.5; return 0; }",
                """
                int main() {
                  uint32_t maximum = 4294967295.9999995;
                  return maximum == 0xffffffffu ? 0 : 1;
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

    def test_character_values_cast_to_both_floating_widths(self):
        result = self._compile_and_run(
            """
            int main() {
              char *digits = "79";
              char seven = digits[0];
              float as_float = (float)seven;
              double as_double = (double)seven;
              float arithmetic_float =
                  (float)(digits[1] - digits[0]);
              double arithmetic_double =
                  (double)(digits[1] - digits[0]);
              if (as_float != 55.0f) return 1;
              if (as_double != 55.0) return 2;
              if (arithmetic_float != 2.0f) return 3;
              if (arithmetic_double != 2.0) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_character_assignment_reaches_scalar_and_array_fp_targets(self):
        result = self._compile_and_run(
            """
            float scalar_float;
            double scalar_double;
            float float_values[1];
            double double_values[1];

            int main() {
              char code = 'A';
              scalar_float = code;
              scalar_double = code;
              float_values[0] = code;
              double_values[0] = code;
              if (scalar_float != 65.0f) return 1;
              if (scalar_double != 65.0) return 2;
              if (float_values[0] != 65.0f) return 3;
              if (double_values[0] != 65.0) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_fixed_float_and_double_arrays_keep_width_and_stride(self):
        result = self._compile_and_run(
            """
            float singles[3];
            double doubles[3];

            int main() {
              singles[0] = 1;
              singles[1] = 2.25;
              doubles[0] = singles[1];
              singles[2] = doubles[0];
              doubles[1] = 100.5;
              doubles[2] = doubles[0] + doubles[1];
              if (singles[0] != 1.0) return 1;
              if (singles[1] != 2.25) return 2;
              if (singles[2] != 2.25) return 3;
              if (doubles[0] != 2.25) return 4;
              if (doubles[1] != 100.5) return 5;
              if (doubles[2] != 102.75) return 6;
              if (sizeof(*singles) != 4) return 7;
              if (sizeof(*doubles) != 8) return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_floating_literals_keep_adjacent_double_storage_independent(self):
        result = self._compile_and_run(
            """
            float leading_float = 1.25f;
            double first_double = 12.5;
            double second_double = -3.25;
            float trailing_float = -4.5f;
            int sentinel = 73;

            int main() {
              if (leading_float != 1.25f) return 1;
              if (first_double != 12.5) return 2;
              if (second_double != -3.25) return 3;
              if (trailing_float != -4.5f) return 4;
              if (sentinel != 73) return 5;
              first_double = 9.5;
              if (second_double != -3.25) return 6;
              second_double = 7.75;
              if (first_double != 9.5) return 7;
              if (leading_float != 1.25f) return 8;
              if (trailing_float != -4.5f) return 9;
              if (sentinel != 73) return 10;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_decimal_literal_uses_the_nearest_binary64_payload(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-payload-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double value = 0.75;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:8]

        self.assertEqual(payload, bytes.fromhex("000000000000e83f"))

    def test_equivalent_exponent_literal_keeps_the_exact_binary64_payload(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-exponent-payload-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double value = 75e-2;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:8]

        self.assertEqual(payload, bytes.fromhex("000000000000e83f"))

    def test_long_decimal_significands_resolve_binary64_halfway_cases(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-long-decimal-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double halfway =
                    1.00000000000000011102230246251565404236316680908203125;
                double above_halfway =
                    1.00000000000000011102230246251565404236316680908203126;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:16]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "000000000000f03f"
                "010000000000f03f"
            ),
        )

    def test_local_and_static_fixed_floating_arrays_execute(self):
        result = self._compile_and_run(
            """
            int main() {
              float local_singles[2];
              double local_doubles[2];
              static float saved_singles[2];
              static double saved_doubles[2];
              local_singles[0] = 1.5;
              local_singles[1] = 2.5;
              local_doubles[0] = local_singles[0] + local_singles[1];
              local_doubles[1] = 8;
              saved_singles[1] = local_doubles[0];
              saved_doubles[1] = local_doubles[1] + saved_singles[1];
              if (local_doubles[0] != 4.0) return 1;
              if (saved_singles[1] != 4.0) return 2;
              if (saved_doubles[1] != 12.0) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_array_arithmetic_compound_assignments_execute(self):
        result = self._compile_and_run(
            """
            float singles[1];
            double doubles[1];

            int main() {
              singles[0] = 5.0;
              singles[0] += 3.0;
              singles[0] *= 2.0;
              singles[0] -= 4.0;
              singles[0] /= 3.0;
              doubles[0] = 20.0;
              doubles[0] += 2.0;
              doubles[0] *= 3.0;
              doubles[0] -= 6.0;
              doubles[0] /= 4.0;
              if (singles[0] != 4.0) return 1;
              if (doubles[0] != 15.0) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_pointers_keep_typed_indirect_lvalues(self):
        result = self._compile_and_run(
            """
            float singles[3];
            double doubles[3];
            float addressed_float;
            double addressed_double;
            int index_calls;
            int pointer_calls;

            int next_index() {
              index_calls += 1;
              return 1;
            }

            double *next_double_pointer() {
              pointer_calls += 1;
              return doubles;
            }

            void update(float *single_pointer, double *double_pointer) {
              *single_pointer = -0.0f;
              single_pointer[next_index()] = 3.0f;
              single_pointer[1] += 5.0;
              single_pointer[1] -= 2.0f;
              single_pointer[1] *= 4;
              single_pointer[1] /= 3.0;
              *double_pointer = 6.0;
              *double_pointer += 2.0f;
              *double_pointer -= 1;
              *double_pointer *= 3.0;
              *double_pointer /= 7.0;
              *next_double_pointer() += 4.0;
            }

            int main() {
              float *single_pointer = singles;
              double *double_pointer = doubles;
              float *addressed_float_pointer = &addressed_float;
              double *addressed_double_pointer = &addressed_double;
              update(single_pointer, double_pointer);
              *addressed_float_pointer = 2.75f;
              *addressed_double_pointer = 8.25;
              if (index_calls != 1) return 1;
              if (pointer_calls != 1) return 2;
              if (single_pointer[1] != 8.0f) return 3;
              if (double_pointer[0] != 7.0) return 4;
              if (sizeof(*single_pointer) != 4) return 5;
              if (sizeof(*double_pointer) != 8) return 6;
              if (*(int *)single_pointer != (-2147483647 - 1)) return 7;
              if (next_double_pointer()[0] != 7.0) return 8;
              if (pointer_calls != 2) return 9;
              if ((&addressed_float)[0] != 2.75f) return 10;
              if ((&addressed_double)[0] != 8.25) return 11;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_deeper_floating_pointers_have_a_useful_diagnostic(self):
        for declaration in ("float **values;", "double **values;"):
            with self.subTest(declaration=declaration), tempfile.TemporaryDirectory(
                prefix="private-cupidc-deep-floating-pointer-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(
                    Path(temporary), declaration
                )
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "floating pointer depth greater than one is not supported",
                result.stderr,
            )

    def test_indirect_floating_updates_preserve_results_and_evaluate_once(self):
        result = self._compile_and_run(
            """
            float pointed_float = -0.0f;
            double pointed_double = 8.5;
            int pointer_calls;

            float *next_float_pointer() {
              pointer_calls += 1;
              return &pointed_float;
            }

            double *next_double_pointer() {
              pointer_calls += 1;
              return &pointed_double;
            }

            int main() {
              float old_float = (*next_float_pointer())++;
              double new_double = --*next_double_pointer();

              if (pointer_calls != 2) return 1;
              if (*(int *)&old_float != (int)0x80000000) return 2;
              if (pointed_float != 1.0f) return 3;
              if (new_double != 7.5) return 4;
              if (pointed_double != 7.5) return 5;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_indexed_floating_updates_keep_the_selected_element(self):
        result = self._compile_and_run(
            """
            float singles[2];
            double doubles[2];
            int index_calls;

            int next_index() {
              int result = index_calls;
              index_calls += 1;
              return result;
            }

            int main() {
              singles[0] = 2.25f;
              doubles[1] = 4.5;

              float new_single = ++singles[next_index()];
              double old_double = doubles[next_index()]--;

              if (index_calls != 2) return 1;
              if (new_single != 3.25f || singles[0] != 3.25f) return 2;
              if (old_double != 4.5 || doubles[1] != 3.5) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_member_floating_updates_keep_the_selected_storage(self):
        result = self._compile_and_run(
            """
            struct Sample {
              float single;
              double wide;
              float taps[2];
              double nan_value;
            };

            struct Sample sample;
            int record_calls;
            int index_calls;

            struct Sample *next_sample() {
              record_calls += 1;
              return &sample;
            }

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              int nan_words[2];
              int *old_nan_words;
              sample.single = 1.5f;
              sample.wide = 4.25;
              sample.taps[1] = -0.0f;
              nan_words[0] = 0x12345678;
              nan_words[1] = 0x7ff81234;
              sample.nan_value = *(double *)nan_words;

              float new_single = ++next_sample()->single;
              double old_wide = sample.wide--;
              float old_tap = sample.taps[next_index()]++;
              double old_nan = sample.nan_value++;
              old_nan_words = (int *)&old_nan;

              if (record_calls != 1 || index_calls != 1) return 1;
              if (new_single != 2.5f || sample.single != 2.5f) return 2;
              if (old_wide != 4.25 || sample.wide != 3.25) return 3;
              if (*(int *)&old_tap != (int)0x80000000) return 4;
              if (sample.taps[1] != 1.0f) return 5;
              if (old_nan_words[0] != 0x12345678 ||
                  old_nan_words[1] != 0x7ff81234) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_derived_floating_update_statements_use_the_same_lvalue_path(self):
        result = self._compile_and_run(
            """
            struct Sample { double value; };

            float values[1];
            struct Sample sample;
            int index_calls;
            int initializer_calls;

            int next_index() {
              index_calls += 1;
              return 0;
            }

            int begin_loop() {
              initializer_calls += 1;
              return 0;
            }

            int main() {
              int loop_count = 0;
              values[0] = 1.25f;
              sample.value = 6.5;
              values[next_index()]++;
              --values[0];
              values[0]--;
              ++values[0];
              sample.value--;
              ++sample.value;
              sample.value++;
              --sample.value;
              for (begin_loop(); loop_count < 2; values[0]++)
                loop_count += 1;
              if (initializer_calls != 1 || index_calls != 1 ||
                  values[0] != 3.25f) return 1;
              return sample.value == 6.5 ? 0 : 2;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_grouped_direct_floating_updates_keep_lvalue_identity(self):
        result = self._compile_and_run(
            """
            int main() {
              float single = 2.5f;
              double wide = 6.5;
              float old_single = (single)++;
              double old_wide = ((wide))--;
              float new_single = ++(single);
              double new_wide = --((wide));

              if (old_single != 2.5f || single != 4.5f) return 1;
              if (old_wide != 6.5 || wide != 4.5) return 2;
              if (new_single != 4.5f || new_wide != 4.5) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_lvalue_failure_allows_same_process_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-floating-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            compile_result, _code, _data = self._compile_after_failure(
                root,
                "float **unsupported_pointer;",
                """
                double values[2];

                int main() {
                  double *pointer = values;
                  pointer[1] = 3.5;
                  *pointer = 1.25;
                  return pointer[1] == 3.5 && *pointer == 1.25 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            entry_offset = int(compile_result.stdout.strip())
            run_result = self._run_i386(root, entry_offset)
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_typed_floating_pointers_keep_direct_update_results(self):
        result = self._compile_and_run(
            """
            struct Pair {
              int first;
              int second;
            };

            int main() {
              char char_values[2];
              int int_values[2];
              float single_values[2];
              double double_values[2];
              struct Pair pair_values[2];
              char *char_pointer = char_values;
              int *int_pointer = int_values;
              float *single_pointer = single_values;
              double *double_pointer = double_values;
              struct Pair *pair_pointer = pair_values;
              char_values[1] = 5;
              int_values[1] = 7;
              single_values[0] = 1.0f;
              single_values[1] = 2.0f;
              double_values[0] = 3.0;
              double_values[1] = 4.0;
              pair_values[1].second = 11;
              char *old_char = char_pointer++;
              int *old_int = int_pointer++;
              float *old_single = single_pointer++;
              double *old_double = double_pointer++;
              struct Pair *old_pair = pair_pointer++;

              if (old_char != char_values) return 1;
              if (old_int != int_values) return 2;
              if (old_single != single_values) return 3;
              if (old_double != double_values) return 4;
              if (old_pair != pair_values) return 5;
              if (*char_pointer != 5) return 6;
              if (*int_pointer != 7) return 7;
              if (*single_pointer != 2.0f) return 8;
              if (*double_pointer != 4.0) return 9;
              if (pair_pointer->second != 11) return 10;
              if (--char_pointer != char_values) return 11;
              if (--int_pointer != int_values) return 12;
              if (--single_pointer != single_values) return 13;
              if (--double_pointer != double_values) return 14;
              if (--pair_pointer != pair_values) return 15;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_array_parameters_decay_to_typed_pointers(self):
        result = self._compile_and_run(
            """
            int check_function(float singles[], double doubles[]) {
              singles[1] = 3.25f;
              doubles[1] = 7.5;
              return singles[1] == 3.25f && doubles[1] == 7.5 ? 0 : 1;
            }

            class Probe {
              int Check(float singles[], double doubles[]) {
                singles[0] = 1.5f;
                doubles[0] = 9.25;
                return singles[0] == 1.5f && doubles[0] == 9.25 ? 0 : 2;
              }
            };

            int main() {
              float singles[2];
              double doubles[2];
              Probe probe;
              int function_status = check_function(singles, doubles);
              int method_status = probe.Check(singles, doubles);
              if (function_status != 0) return function_status;
              if (method_status != 0) return method_status;
              if (singles[1] != 3.25f || doubles[1] != 7.5) return 3;
              if (singles[0] != 1.5f || doubles[0] != 9.25) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_string_subscript_drops_stale_floating_array_metadata(self):
        result = self._compile_and_run(
            """
            double floating_values[1];

            int main() {
              floating_values[0] = 2.5;
              floating_values;
              if ("AZ"[1] != 'Z') return 1;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_floating_matrices_keep_row_and_scalar_widths(self):
        result = self._compile_and_run(
            """
            int leading_canary = 17;
            float singles[2][3];
            double doubles[2][2];
            int trailing_canary = 29;

            int main() {
              singles[0][0] = 1.25f;
              singles[1][2] = -3.5f;
              doubles[0][1] = 12.5;
              doubles[1][0] = singles[1][2];
              if (singles[0][0] != 1.25f) return 1;
              if (singles[1][2] != -3.5f) return 2;
              if (doubles[0][1] != 12.5) return 3;
              if (doubles[1][0] != -3.5) return 4;
              if (sizeof(*singles) != 12) return 5;
              if (sizeof(**singles) != 4) return 6;
              if (sizeof(*doubles) != 16) return 7;
              if (sizeof(**doubles) != 8) return 8;
              if (leading_canary != 17) return 9;
              if (trailing_canary != 29) return 10;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_matrices_cover_local_static_and_three_dimensional_storage(self):
        result = self._compile_and_run(
            """
            float global_cube[2][2][2];
            int global_canary = 31;

            int main() {
              int leading_canary = 37;
              float local_matrix[2][3];
              double local_cube[2][2][2];
              int trailing_canary = 41;
              static double saved_matrix[2][2];
              static float saved_cube[2][2][2];

              global_cube[1][0][1] = 2.5f;
              global_cube[1][0][1] *= 2;
              local_matrix[1][2] = 7.25f;
              local_cube[0][1][1] = 9.5;
              saved_matrix[1][0] = local_matrix[1][2];
              saved_cube[1][1][0] = local_cube[0][1][1];
              saved_cube[1][1][0] += 0.5f;

              if (global_cube[1][0][1] != 5.0f) return 1;
              if (local_matrix[1][2] != 7.25f) return 2;
              if (local_cube[0][1][1] != 9.5) return 3;
              if (saved_matrix[1][0] != 7.25) return 4;
              if (saved_cube[1][1][0] != 10.0f) return 5;
              if (sizeof(*global_cube) != 16) return 6;
              if (sizeof(**global_cube) != 8) return 7;
              if (sizeof(***global_cube) != 4) return 8;
              if (sizeof(*local_cube) != 32) return 9;
              if (sizeof(**local_cube) != 16) return 10;
              if (sizeof(***local_cube) != 8) return 11;
              if (leading_canary != 37) return 12;
              if (trailing_canary != 41) return 13;
              if (global_canary != 31) return 14;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_floating_matrices_remain_typed_across_lines(self):
        result = self._compile_repl_and_run(
            (
                "double repl_matrix[2][3];",
                "float repl_cube[2][2][2];",
                "int repl_canary = 43;",
                """
                int probe() {
                  repl_matrix[1][2] = 6.5;
                  repl_matrix[1][2] += 1.0f;
                  repl_cube[1][0][1] = 4.25f;
                  if (repl_matrix[1][2] != 7.5) return 1;
                  if (repl_cube[1][0][1] != 4.25f) return 2;
                  if (sizeof(*repl_matrix) != 24) return 3;
                  if (sizeof(**repl_matrix) != 8) return 4;
                  if (sizeof(*repl_cube) != 16) return 5;
                  if (sizeof(**repl_cube) != 8) return 6;
                  if (sizeof(***repl_cube) != 4) return 7;
                  if (repl_canary != 43) return 8;
                  return 0;
                }
                """,
                "probe;",
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_floating_array_bitwise_compound_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-float-array-compound-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                float values[1];

                int main() {
                  values[0] &= 1;
                  return 0;
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "bitwise/shift compound assignment not valid on FP arrays",
            result.stderr,
        )

    def test_floating_record_fields_keep_scalar_and_array_storage_typed(self):
        result = self._compile_and_run(
            """
            struct Sample {
              int leading;
              float reading;
              double values[2];
              float *reading_pointer;
              int trailing;
            };

            class Meter {
              int leading;
              double reading;
              float values[2];
              double *reading_pointer;
              int trailing;
            };

            struct Sample samples[2];
            Meter meter;
            int index_calls;

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              float pointer_singles[2];
              double pointer_doubles[2];
              struct Sample *sample_pointer = samples;
              Meter *meter_pointer = &meter;
              pointer_singles[1] = 6.25f;
              pointer_doubles[1] = 12.5;
              samples[0].leading = 11;
              samples[0].reading = -0.0f;
              samples[0].values[0] = 2.0;
              samples[0].trailing = 13;
              samples[next_index()].reading = 3.0f;
              samples[1].reading += 5.0;
              samples[1].reading -= 2;
              samples[1].reading *= 4.0f;
              samples[1].reading /= 3.0;
              samples[1].values[1] = 9.0;
              samples[1].values[1] += 3.0f;
              sample_pointer->values[1] = 7.5;
              sample_pointer->values[1] *= 2;
              sample_pointer->reading_pointer = pointer_singles;

              meter.leading = 17;
              meter.reading = 20.0;
              meter_pointer->reading /= 4.0f;
              meter.values[0] = 1.5f;
              meter_pointer->values[0] += 2;
              meter_pointer->reading_pointer = pointer_doubles;
              meter.trailing = 19;

              if (index_calls != 1) return 1;
              if (samples[1].reading != 8.0f) return 2;
              if (samples[1].values[1] != 12.0) return 3;
              if (sample_pointer->values[1] != 15.0) return 4;
              if ((1.0f / samples[0].reading) >= 0.0f) return 5;
              if (samples[0].leading != 11) return 6;
              if (samples[0].trailing != 13) return 7;
              if (meter.reading != 5.0) return 8;
              if (meter.values[0] != 3.5f) return 9;
              if (meter.leading != 17) return 10;
              if (meter.trailing != 19) return 11;
              if (sample_pointer->reading_pointer[1] != 6.25f) return 12;
              if (meter_pointer->reading_pointer[1] != 12.5) return 13;
              if (sizeof(samples[next_index()].values[1]) != 8) return 14;
              if (sizeof(meter.values[0]) != 4) return 15;
              if (index_calls != 1) return 16;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

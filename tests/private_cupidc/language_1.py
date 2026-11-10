"""Private compiler language execution cases."""

from .harness import (
    KERNEL_LANG,
    PARSER_SOURCE,
    Path,
    REPO_ROOT,
    struct,
    subprocess,
    tempfile,
)

class Language1Cases:
    def test_parser_error_helper_stays_private(self):
        header = (KERNEL_LANG / "cupidc.h").read_text(encoding="utf-8")
        parser = PARSER_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("void cc_error(cc_state_t *cc, const char *msg);", header)
        self.assertIn(
            "static void cc_error(cc_state_t *cc, const char *msg)",
            parser,
        )

    def test_aot_zero_data_program_keeps_advertised_code_offset(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-zero-data-aot-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, elf_path, _data = self._compile(
                root,
                """
                int main() {
                  return 17;
                }
                """,
                aot=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            image = elf_path.read_bytes()
            header = struct.unpack_from("<16sHHIIIIIHHHHHH", image)
            self.assertEqual(header[5], 52)
            self.assertEqual(header[9], 32)
            self.assertEqual(header[10], 1)
            program_header = struct.unpack_from("<IIIIIIII", image, 52)
            self.assertEqual(program_header[0], 1)
            self.assertEqual(program_header[1], 0x80)
            self.assertEqual(program_header[2], 0x01100000)
            self._extract_aot_segments(root, elf_path, entry_offset)
            runtime = self._run_i386(root, entry_offset)
            self.assertEqual(
                runtime.returncode,
                17,
                runtime.stdout + runtime.stderr,
            )

    def test_aot_initialized_data_program_keeps_two_load_segments(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-data-aot-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, elf_path, _data = self._compile(
                root,
                """
                int value = 17;

                int main() {
                  return value;
                }
                """,
                aot=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            image = elf_path.read_bytes()
            header = struct.unpack_from("<16sHHIIIIIHHHHHH", image)
            self.assertEqual(header[5], 52)
            self.assertEqual(header[9], 32)
            self.assertEqual(header[10], 2)
            self._extract_aot_segments(root, elf_path, entry_offset)
            runtime = self._run_i386(root, entry_offset)
            self.assertEqual(
                runtime.returncode,
                17,
                runtime.stdout + runtime.stderr,
            )

    def test_grouped_runtime_function_addresses_run_in_jit_and_aot(self):
        source = """
            int ready_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int later_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int main() {
              int marker = 7;
              int *marker_address = &(marker);
              int (*callback)(double, int) = &(ready_target);
              if (*marker_address != 7) return 1;
              int result = callback(3, 4.75f);
              callback = &((later_target));
              return result + callback(2, 5.5f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 59, result.stdout + result.stderr
                )

    def test_explicit_function_address_initializer_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-addressed-callback-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                double wrong(int value) { return value; }
                int invalid(void) {
                  int (*callback)(int) = &((wrong));
                  return callback(1);
                }
                int main() { return invalid(); }
                """,
                """
                int right(double value) { return (int)value; }
                int main() {
                  int (*callback)(double) = &(right);
                  return callback(9);
                }
                """,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "function-pointer initializer result does not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_conditional_explicit_casts_erase_only_the_selected_value(self):
        source = """
            int choose_second;
            int first(int value) { return value + 1; }
            int second(int value) { return value + 2; }

            int main() {
              choose_second = 1;
              int (*callback)(int) =
                  choose_second ? (void *)second : (void *)first;
              int (*left_nullable)(int) =
                  choose_second ? (void *)first : 0;
              choose_second = 0;
              int (*right_nullable)(int) =
                  choose_second ? 0 : (void *)second;
              if (callback(8) != 10) return 1;
              if (left_nullable(8) != 9) return 2;
              return right_nullable(8) == 10 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-conditional-cast-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int choose_object;
                int object;
                int identity(int value) { return value; }
                int invalid(void) {
                  int (*callback)(int) =
                      choose_object ? (void *)identity : &object;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(6) == 6 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_rejected_forward_initializer_rolls_back_its_address_patch(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-patch-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int later(double value);

                int invalid_initializer() {
                  int (*callback)(int) = later;
                  return 0;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(4) == 4 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer parameters do not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_function_restores_inferred_target_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-inference-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invalid_initializer(void) {
                  int (*callback)(int) = later;
                  return missing_value;
                }

                int later(int value);
                """,
                """
                double later(double value) {
                  return value;
                }

                int main() {
                  double (*callback)(double) = later;
                  return callback(14) == 14.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_program_restores_a_committed_function_definition(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-program-symbol-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                double later(double value) { return value + 1.0; }
                """,
                """
                double later(double value);
                int broken(void) { return missing_value; }
                """,
                """
                int main() {
                  double (*callback)(double) = later;
                  return callback(4) == 5.0 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_program_restores_a_preexisting_function_prototype(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-prototype-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                double later(double value);
                """,
                """
                double later(double value) { return value + 1.0; }
                int broken(void) { return missing_value; }
                """,
                """
                double later(double value) { return value + 3.0; }
                int main(void) {
                  double (*callback)(double) = later;
                  return callback(4) == 7.0 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_program_restores_the_committed_start_thunk(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-start-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_intermediate_failure(
                root,
                """
                int marker;
                marker = 1;
                """,
                """
                marker = 2;
                missing();
                """,
                """
                int main(void) {
                  marker = 0;
                  __start();
                  return marker == 1 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("[cupidc] Unresolved symbol: missing", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_method_restores_control_context_before_recovery(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-method-control-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_two_failures(
                root,
                """
                class Broken {
                  int Fail(void) {
                    while (missing_condition) {}
                    return 0;
                  }
                };
                """,
                """
                int main(void) {
                  break;
                  return 0;
                }
                """,
                """
                int main(void) { return 0; }
                """,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("undefined variable", result.stderr)
        self.assertIn("break outside loop or switch", result.stderr)

    def test_adjacent_string_literals_can_exceed_one_token(self):
        first = "a" * 700
        second = "b" * 700
        result = self._compile_and_run(
            f"""
            int main() {{
              char *text =
                  "{first}"
                  "{second}";
              if (text[0] != 'a') return 1;
              if (text[699] != 'a') return 2;
              if (text[700] != 'b') return 3;
              if (text[1399] != 'b') return 4;
              if (text[1400] != 0) return 5;
              return 0;
            }}
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_adjacent_string_initializer_uses_the_full_literal(self):
        first = "c" * 700
        second = "d" * 700
        result = self._compile_and_run(
            f"""
            char *text =
                "{first}"
                "{second}";

            int main() {{
              if (text[699] != 'c') return 1;
              if (text[700] != 'd') return 2;
              if (text[1399] != 'd') return 3;
              if (text[1400] != 0) return 4;
              return 0;
            }}
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_multiple_typedef_aliases_report_capacity_and_allow_recovery(self):
        aliases = ", ".join(f"Alias{index}" for index in range(17))
        failing_source = f"typedef int {aliases};"
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-typedef-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            failure, _code_path, _data_path = self._compile(
                root, failing_source
            )
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                failing_source,
                """
                typedef struct Recovered { int value; }
                    Recovered, *RecoveredPointer;
                int main() {
                  Recovered value;
                  RecoveredPointer pointer = &value;
                  pointer->value = 23;
                  return value.value == 23 ? 0 : 1;
                }
                """,
            )

        self.assertEqual(failure.returncode, 2, failure.stdout + failure.stderr)
        self.assertIn("too many typedef aliases", failure.stderr)
        self.assertEqual(
            recovery.returncode,
            0,
            recovery.stdout + recovery.stderr,
        )

    def test_multiple_typedef_aliases_reject_a_trailing_comma_and_recover(self):
        failing_source = (
            "typedef struct Broken { int value; } Broken,;"
        )
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-typedef-trailing-comma-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            failure, _code_path, _data_path = self._compile(
                root, failing_source
            )
            recovery, _code_path, _data_path = self._compile_after_failure(
                root,
                failing_source,
                """
                typedef struct { int value; } Value, *ValuePointer;
                int main() {
                  Value value;
                  ValuePointer pointer = &value;
                  pointer->value = 179;
                  return value.value == 179 ? 0 : 1;
                }
                """,
            )

        self.assertEqual(failure.returncode, 2, failure.stdout + failure.stderr)
        self.assertIn("expected typedef alias name", failure.stderr)
        self.assertNotIn("unexpected token", failure.stderr)
        self.assertEqual(
            recovery.returncode,
            0,
            recovery.stdout + recovery.stderr,
        )

    def test_cumulative_local_frame_size_never_wraps(self):
        cases = (
            (
                "arrays",
                "int main() {"
                " char a[1073741820]; char b[1073741820];"
                " char c[1073741820]; return 0; }",
            ),
            (
                "records",
                "struct Wide { char bytes[1073741820]; };"
                " int main() { struct Wide a; struct Wide b;"
                " struct Wide c; return 0; }",
            ),
            (
                "scalar after exact edge",
                "int main() { char a[1073741816]; char b[1073741816];"
                " int scalar; return 0; }",
            ),
            (
                "SIMD alignment",
                "int main() { char bytes[2147483620];"
                " float4 lanes; return 0; }",
            ),
        )
        for label, source in cases:
            with self.subTest(lane=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-local-frame-overflow-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("local frame size overflow", result.stderr)

    def test_remainder_assignment_preserves_signedness_and_lvalue_identity(self):
        result = self._compile_and_run(
            """
            int global_signed = -31;
            uint32_t global_unsigned = 0xfffffffeu;
            int pointer_target = -23;
            int pointer_calls;
            int index_calls;
            int divisor_calls;

            struct Pair {
              int signed_value;
              uint32_t unsigned_value;
            };

            struct Pair primary_record;
            struct Pair secondary_record;
            struct Pair *selected_record;

            int *select_pointer_target() {
              pointer_calls += 1;
              return &pointer_target;
            }

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int next_divisor() {
              divisor_calls += 1;
              return 4;
            }

            int retarget_record() {
              selected_record = &secondary_record;
              return 6;
            }

            int main() {
              int signed_local = -17;
              uint32_t unsigned_local = 0xffffffffu;
              uint32_t unsigned_pointer_target = 0x80000005u;
              uint32_t *unsigned_pointer = &unsigned_pointer_target;
              int signed_values[2];
              uint32_t unsigned_values[2];

              signed_values[1] = -19;
              unsigned_values[0] = 0xfffffffeu;
              primary_record.signed_value = -29;
              primary_record.unsigned_value = 0x80000001u;
              secondary_record.signed_value = 88;
              selected_record = &primary_record;

              global_signed %= 7;
              global_unsigned %= 7;
              signed_local %= 5;
              unsigned_local %= 10;
              *select_pointer_target() %= 6;
              *unsigned_pointer %= 4;
              signed_values[next_index()] %= next_divisor();
              unsigned_values[0] %= 16;
              primary_record.unsigned_value %= 7;
              selected_record->signed_value %= retarget_record();

              if (global_signed != -3) return 1;
              if (global_unsigned != 2u) return 2;
              if (signed_local != -2) return 3;
              if (unsigned_local != 5u) return 4;
              if (pointer_target != -5 || pointer_calls != 1) return 5;
              if (unsigned_pointer_target != 1u) return 6;
              if (signed_values[1] != -3) return 7;
              if (index_calls != 1 || divisor_calls != 1) return 8;
              if (unsigned_values[0] != 14u) return 9;
              if (primary_record.unsigned_value != 3u) return 10;
              if (primary_record.signed_value != -5) return 11;
              if (secondary_record.signed_value != 88) return 12;
              if (selected_record != &secondary_record) return 13;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_enum_maximum_only_overflows_for_an_implicit_successor(self):
        accepted = (
            "enum Edge { Maximum=2147483647, };",
            "enum Edge { Maximum=2147483647, Reset=0 };",
        )
        for repl in (False, True):
            for source in accepted:
                with self.subTest(repl=repl, source=source), \
                     tempfile.TemporaryDirectory(
                         prefix="private-cupidc-enum-maximum-",
                         ignore_cleanup_errors=True,
                     ) as temporary:
                    result, _code_path, _data_path = self._compile(
                        Path(temporary), source, repl=repl
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )

            with self.subTest(repl=repl, source="implicit overflow"), \
                 tempfile.TemporaryDirectory(
                     prefix="private-cupidc-enum-overflow-",
                     ignore_cleanup_errors=True,
                 ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary),
                    "enum Edge { Maximum=2147483647, Overflow };",
                    repl=repl,
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn("enum value overflow", result.stderr)

    def test_single_string_token_overflow_has_a_useful_diagnostic(self):
        oversized = "x" * 1024
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-string-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                f'int main() {{ char *text = "{oversized}"; return text[0]; }}',
            )
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "string literal is too long; split it into adjacent literals",
            result.stderr,
        )
        diagnostic = (
            "string literal is too long; split it into adjacent literals"
        )
        self.assertEqual(
            result.stderr.count(f"[cupidc] error (line 1): {diagnostic}"),
            1,
            result.stderr,
        )
        self.assertEqual(
            result.stderr.count(f"CupidC Error (line 1): {diagnostic}"),
            1,
            result.stderr,
        )
        self.assertNotIn("x" * 128, result.stderr)

    def test_adjacent_string_reports_data_section_overflow(self):
        piece = '"' + ("z" * 1023) + '"\n'
        source = "int main() { char *text =\n" + (piece * 8201) + "; return 0; }"
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-string-data-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(Path(temporary), source)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("data section overflow", result.stderr)

    def test_feature13_derived_aot_source_executes_through_the_elf_path(self):
        result = self._compile_and_run(
            (
                REPO_ROOT / "bin" / "feature13_derived_aot.cc"
            ).read_text(encoding="utf-8"),
            aot=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "[feature13-derived-aot] PASS score=%d once=%d zero=%x",
            result.stderr,
        )

    def test_decimal_literal_ties_round_to_even_at_each_width(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-ties-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                float float_down = 16777217.0f;
                float float_up = 16777219.0F;
                double double_down = 9007199254740993.0;
                double double_up = 9007199254740995.0;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:24]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "0000804b"
                "0200804b"
                "0000000000004043"
                "0200000000004043"
            ),
        )

    def test_decimal_literals_cover_subnormal_maximum_and_overflow_edges(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-edges-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                float float_minimum = 1.401298464324817e-45f;
                float float_maximum = 3.4028234663852886e38f;
                float float_overflow = 3.4028236e38f;
                double double_minimum = 5e-324;
                double double_maximum = 1.7976931348623157e308;
                double double_overflow = 1.7976931348623159e308;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:36]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "01000000"
                "ffff7f7f"
                "0000807f"
                "0100000000000000"
                "ffffffffffffef7f"
                "000000000000f07f"
            ),
        )

    def test_extreme_exponents_preserve_zero_sign_and_ieee_limits(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-extreme-decimal-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                float negative_zero = -0.0f;
                double negative_underflow = -1e-4000;
                double positive_underflow = 1e-4000;
                double positive_overflow = 1e4000;
                double zero_with_large_exponent =
                    0e999999999999999999999999999999999999;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:36]

        self.assertEqual(
            payload,
            bytes.fromhex(
                "00000080"
                "0000000000000080"
                "0000000000000000"
                "000000000000f07f"
                "0000000000000000"
            ),
        )

    def test_decimal_literal_requires_exponent_digits(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-exponent-error-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                double value = 1e+;
                """,
            )

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "decimal floating literal exponent requires a digit",
            result.stderr,
        )

    def test_function_body_decimal_error_keeps_the_lexer_diagnostic(self):
        cases = (
            (
                "double value = 1e+;",
                "decimal floating literal exponent requires a digit",
            ),
            (
                f"double value = {'1.' + '0' * 94};",
                "numeric literal exceeds 95 characters",
            ),
        )
        for declaration, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                with tempfile.TemporaryDirectory(
                    prefix="private-cupidc-decimal-body-error-",
                    ignore_cleanup_errors=True,
                ) as temporary:
                    result, _code, _data = self._compile(
                        Path(temporary),
                        f"""
                        int main() {{
                          {declaration}
                          return 0;
                        }}
                        """,
                    )

                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                public_errors = [
                    line
                    for line in result.stderr.splitlines()
                    if line.startswith("CupidC Error")
                ]
                self.assertEqual(len(public_errors), 1, result.stderr)
                self.assertIn(diagnostic, public_errors[0])
                self.assertNotIn("expected expression", public_errors[0])

    def test_decimal_literal_limit_accepts_95_characters_and_rejects_96(self):
        accepted_literal = "1." + "0" * 93
        rejected_literal = accepted_literal + "0"
        self.assertEqual(len(accepted_literal), 95)
        self.assertEqual(len(rejected_literal), 96)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-length-limit-",
            ignore_cleanup_errors=True,
        ) as temporary:
            accepted_result, _code, accepted_data = self._compile(
                Path(temporary),
                f"double value = {accepted_literal};\n",
            )
            self.assertEqual(
                accepted_result.returncode,
                0,
                accepted_result.stdout + accepted_result.stderr,
            )
            self.assertEqual(
                accepted_data.read_bytes()[:8],
                bytes.fromhex("000000000000f03f"),
            )
            result, _code, _data = self._compile(
                Path(temporary),
                f"double value = {rejected_literal};\n",
            )
            suffix_result, _code, _data = self._compile(
                Path(temporary),
                f"float value = {accepted_literal}f;\n",
            )

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "numeric literal exceeds 95 characters",
            result.stderr,
        )
        self.assertNotIn("unexpected token", result.stderr)
        self.assertNotIn("additional error", result.stderr)
        self.assertEqual(
            suffix_result.returncode,
            2,
            suffix_result.stdout + suffix_result.stderr,
        )
        self.assertIn(
            "numeric literal exceeds 95 characters",
            suffix_result.stderr,
        )

    def test_overlong_decimal_literal_keeps_the_following_token_available(self):
        literal = "1." + "0" * 94
        result = subprocess.run(
            [
                str(self.driver),
                "--check-number-boundary",
                f"{literal}; 0.75",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
            timeout=30,
        )

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(
            result.stdout.splitlines(),
            ["numeric literal exceeds 95 characters", "0.75"],
        )

    def test_decimal_literal_unbounded_exponent_reaches_ieee_overflow(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                """
                double value = 1e4097;
                """,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            payload = data_path.read_bytes()[:8]

        self.assertEqual(payload, bytes.fromhex("000000000000f07f"))

    def test_runtime_decimal_literals_keep_their_rounded_payloads(self):
        result = self._compile_and_run(
            """
            int main() {
              double wide;
              float narrow;
              wide = 0.75;
              narrow = 0.1f;
              if (*(int *)&wide != 0) return 1;
              if (*(int *)&narrow != 0x3dcccccd) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_derived_updates_report_useful_diagnostics(self):
        cases = (
            (
                "void probe(int *value) { ++(*value + 1); }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(float value) { ++(1.0f + value); }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(float value) { ++sizeof(value); }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(int index) { float values[2]; ++&values[index]; }",
                "increment or decrement requires a scalar variable",
            ),
            (
                "void probe(int index) { float values[2][2]; ++values[index]; }",
                "increment or decrement requires a scalar variable",
            ),
        )
        for source, diagnostic in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-derived-update-error-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(diagnostic, result.stderr)

    def test_rejected_derived_update_allows_recovery_in_the_same_compiler_state(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-derived-update-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                "void probe(int *value) { ++(*value + 1); }",
                """
                float value;

                int main() {
                  float *pointer = &value;
                  value = 2.0f;
                  return ++*pointer == 3.0f && value == 3.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            run_result = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

    def test_unevaluated_sizeof_keeps_diagnostics_and_recovers(self):
        failing_source = "int main() { return sizeof(missing_value); }"
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-sizeof-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary), failing_source
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("undefined variable", result.stderr)
        self.assertNotIn("operand has no object size", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-sizeof-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            retry_result, _code, _data = self._compile_after_failure(
                root,
                failing_source,
                "int main() { double value = 4.5; return value == 4.5 ? 0 : 1; }",
            )
            self.assertEqual(
                retry_result.returncode,
                0,
                retry_result.stdout + retry_result.stderr,
            )
            entry_offset = int(retry_result.stdout.strip())
            run_result = self._run_i386(root, entry_offset)
        self.assertEqual(
            run_result.returncode,
            0,
            run_result.stdout + run_result.stderr,
        )

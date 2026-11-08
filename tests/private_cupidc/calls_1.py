"""Private compiler calls execution cases."""

from .harness import (Path, REPO_ROOT, tempfile)

class Calls1Cases:
    def test_four_word_call_keeps_the_popup_menu_argument_order(self):
        result = self._compile_and_run(
            """
            int observe_popup(int x, int y, void *items, int count) {
              if (x != 220) return 1;
              if (y != 110) return 2;
              if (items == 0) return 3;
              if (count != 3) return 4;
              return 0;
            }

            int main() {
              int items[3];
              return observe_popup(220, 110, (void *)items, 3);
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_method_calls_layout_self_and_mixed_width_arguments(self):
        result = self._compile_and_run(
            """
            int recorded;

            class Probe {
              int Encode(double first, int second, double third) {
                return (int)first + second * 2 + (int)third * 4;
              }

              void Record(double first, int second, double third) {
                recorded =
                    (int)first + second * 2 + (int)third * 4;
              }
            };

            int main() {
              Probe probe;
              int expression_result = probe.Encode(1.0, 2, 3.0);
              probe.Record(2.0, 3, 4.0);
              return expression_result + recorded;
            }
            """
        )
        self.assertEqual(result.returncode, 41, result.stdout + result.stderr)

    def test_callable_provenance_does_not_survive_logical_not(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-unary-provenance-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int target(int value) { return value; }
                int invalid_initializer() {
                  int (*callback)(int) = !target;
                  return 0;
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

    def test_alternating_widths_keep_source_evaluation_and_parameter_order(self):
        result = self._compile_and_run(
            """
            int sequence;

            int next_int() {
              sequence = sequence + 1;
              return sequence;
            }

            double next_double() {
              sequence = sequence + 1;
              return (double)sequence;
            }

            int capture(int first, double second, int third, double fourth) {
              return first + (int)second * 2 +
                  third * 4 + (int)fourth * 8;
            }

            int main() {
              sequence = 0;
              return capture(
                  next_int(), next_double(), next_int(), next_double()) +
                  sequence;
            }
            """
        )
        self.assertEqual(result.returncode, 53, result.stdout + result.stderr)

    def test_alternating_width_call_cleans_every_argument_byte(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-cleanup-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, code_path, _data = self._compile(
                Path(temporary),
                """
                int capture(int first, double second,
                            int third, double fourth) {
                  return first + (int)second + third + (int)fourth;
                }

                int main() {
                  return capture(1, 2.0, 3, 4.0);
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

        cleanup_sites = [
            offset
            for offset in range(len(main_code) - 7)
            if main_code[offset] == 0xE8
            and main_code[offset + 5 : offset + 8] == b"\x83\xc4\x18"
        ]
        self.assertEqual(
            len(cleanup_sites),
            1,
            "expected one call followed by ADD ESP, 24",
        )

    def test_prototype_before_use_preserves_mixed_width_call_metadata(self):
        result = self._compile_and_run(
            """
            int seen_kind;
            double seen_number;
            int seen_offset;
            int seen_length;
            int seen_line;

            void capture_token(int kind, double number,
                               int string_offset, int string_length,
                               int line);

            int main() {
              capture_token(23, 0, -41, 47, 53);
              if (seen_kind != 23) return 1;
              if (seen_number != 0.0) return 2;
              if (seen_offset != -41) return 3;
              if (seen_length != 47) return 4;
              if (seen_line != 53) return 5;
              return 0;
            }

            void capture_token(int kind, double number,
                               int string_offset, int string_length,
                               int line) {
              seen_kind = kind;
              seen_number = number;
              seen_offset = string_offset;
              seen_length = string_length;
              seen_line = line;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_feature13_source_compiles_through_the_private_call_path(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-feature13-compile-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                (REPO_ROOT / "bin" / "feature13_double.cc").read_text(
                    encoding="utf-8"
                ),
            )
        self.assertEqual(
            result.returncode,
            0,
            result.stdout + result.stderr,
        )

    def test_unsupported_call_argument_has_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-call-diagnostic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void nothing() {
                }

                void consume(int value) {
                }

                void probe() {
                  consume(nothing());
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn(
            "cdecl call argument type is not supported", result.stderr
        )

    def test_aggregate_parameter_keeps_a_useful_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-aggregate-parameter-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                struct Pair {
                  int first;
                  int second;
                };

                void consume(struct Pair value) {
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("cdecl parameter type is not supported", result.stderr)

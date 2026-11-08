"""Private compiler simd execution cases."""

from .harness import (Path, tempfile)

class Simd2Cases:
    def test_simd_pointer_and_new_forms_have_useful_diagnostics(self):
        cases = (
            ("spelled pointer", "float4 *values;", "SIMD pointer types"),
            (
                "address of vector",
                """
                int main() {
                  float4 value = {1, 2, 3, 4};
                  &value;
                  return 0;
                }
                """,
                "SIMD pointer expressions",
            ),
            (
                "vector allocation",
                """
                int main() {
                  new double2[2];
                  return 0;
                }
                """,
                "SIMD allocation with new",
            ),
        )

        for label, source, message in cases:
            with self.subTest(form=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-pointer-",
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

    def test_simd_struct_field_arrays_have_a_useful_diagnostic(self):
        cases = (
            ("struct SIMD field", "struct Samples { float4 values[2]; };"),
            ("class SIMD field", "class Samples { double2 values[2]; };"),
        )
        for label, source in cases:
            with self.subTest(declaration=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-simd-array-diagnostic-",
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
                self.assertIn(
                    "SIMD struct field arrays are not supported",
                    result.stderr,
                )

    def test_simd_argument_mismatch_reports_the_fixed_type(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-argument-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                void consume(float4 value) {
                }

                int invalid_call() {
                  double2 value = {1.0, 2.0};
                  consume(value);
                  return 0;
                }
                """,
                """
                float4 identity(float4 value) {
                  return value;
                }

                int main() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 result;
                  result = identity(value);
                  return result.w == 4.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(Path(temporary), entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_variadic_simd_rejection_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-variadic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume(float4 fixed, ...);
                float4 fixed_value;
                float4 invalid_value;
                consume(fixed_value, invalid_value);
                """,
                """
                float4 identity(float4 value) {
                  return value;
                }

                int main() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 result;
                  result = identity(value);
                  return result.z == 3.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD call arguments require a fixed parameter type",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_variadic_call_accepts_fixed_simd_and_scalar_tail_values(self):
        result = self._compile_and_run(
            """
            int inspect(float4 narrow, double2 wide, int marker, ...) {
              if (narrow.x != 1.0f || narrow.w != 4.0f) return 1;
              if (wide.x != 5.0 || wide.y != 7.0) return 2;
              return marker == 9 ? 0 : 3;
            }

            int main() {
              float4 narrow = {1.0f, 2.0f, 3.0f, 4.0f};
              double2 wide = {5.0, 7.0};
              return inspect(narrow, wide, 9, 11, 2.5f);
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unprototyped_simd_rejection_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-unprototyped-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume();
                double2 invalid_value;
                consume(invalid_value);
                """,
                """
                double2 identity(double2 value) {
                  return value;
                }

                int main() {
                  double2 value = {3.0, 5.0};
                  double2 result;
                  result = identity(value);
                  return result.y == 5.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD call arguments require a fixed parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_erased_pointer_simd_argument_is_rejected_without_metadata(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-function-pointer-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                void consume(float4 value) {
                }

                int invalid_call() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  void *callback = consume;
                  callback(value);
                  return 0;
                }
                """,
                """
                float4 identity(float4 value) {
                  return value;
                }

                int main() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  float4 result;
                  result = identity(value);
                  return result.w == 4.0f ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD call arguments require a fixed parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_bare_simd_return_recovers_with_a_focused_diagnostic(self):
        cases = (
            ("float4", "float4"),
            ("double2", "double2"),
        )
        retry_source = """
            float4 identity(float4 value) {
              return value;
            }

            int main() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 result;
              result = identity(value);
              return result.z == 3.0f ? 0 : 1;
            }
        """
        for label, return_type in cases:
            with self.subTest(return_type=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-bare-simd-return-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    f"""
                    {return_type} invalid_return() {{
                      return;
                    }}
                    """,
                    retry_source,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "SIMD return requires a matching float4 or double2 value",
                    result.stderr,
                )
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    0,
                    runtime.stdout + runtime.stderr,
                )

    def test_mismatched_simd_return_recovers_with_a_focused_diagnostic(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-return-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                float4 invalid_return() {
                  double2 value = {1.0, 2.0};
                  return value;
                }
                """,
                """
                double2 identity(double2 value) {
                  return value;
                }

                int main() {
                  double2 value = {3.0, 5.0};
                  double2 result;
                  result = identity(value);
                  return result.y == 5.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "SIMD return requires a matching float4 or double2 value",
                result.stderr,
            )
            entry_offset = int(result.stdout.strip())
            runtime = self._run_i386(root, entry_offset)
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

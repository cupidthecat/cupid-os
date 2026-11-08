"""Private compiler callbacks execution cases."""

from .harness import (Path, tempfile)

class Callbacks4Cases:
    def test_named_function_pointer_array_result_is_rejected_and_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-array-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int Row[2];

                int invalid_call() {
                  Row (*callback)(void);
                  return 0;
                }
                """,
                """
                int main() {
                  int (*callback)(void);
                  return callback == 0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer array result is not supported", result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_repl_keeps_function_pointer_typedef_for_later_parameter_unit(self):
        result = self._compile_repl_and_run(
            (
                "typedef int (*entry_cb)(const uint8_t *, uint32_t, void *);",
                """
                int inspect(const uint8_t *entry, uint32_t elen, void *ctx) {
                  if (entry == 0 || ctx != 0) return 1;
                  return elen;
                }
                """,
                """
                int invoke(entry_cb cb) {
                  uint8_t entry = 1;
                  return cb(&entry, 7, 0);
                }
                """,
                "int main() { return invoke(inspect); }",
            )
        )
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_typed_function_pointer_simd_argument_uses_declared_slot(self):
        result = self._compile_and_run(
            """
            float observed;

            void consume(float4 value) {
              observed = value.w;
            }

            int main() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              void (*callback)(float4 value) = consume;
              callback(value);
              return observed == 4.0f ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_prototyped_function_pointer_simd_result_uses_xmm0(self):
        result = self._compile_and_run(
            """
            float4 make_value() {
              float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
              return value;
            }

            int main() {
              float4 (*callback)(void) = make_value;
              float4 result;
              result = callback();
              return result.z == 3.0f ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_function_pointer_simd_result_is_rejected_without_metadata(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-simd-function-pointer-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                float4 make_value() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  return value;
                }

                int invalid_call() {
                  float4 (*callback)() = make_value;
                  float4 result;
                  result = callback();
                  return result.x == 1.0f ? 0 : 1;
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
                "SIMD function-pointer returns are not supported",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

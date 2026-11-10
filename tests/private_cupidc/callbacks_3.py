"""Private compiler callbacks execution cases."""

from .harness import (Path, REPO_ROOT, tempfile)

class Callbacks3Cases:
    def test_function_pointer_typedef_parameter_matches_active_iso_callback_widths(self):
        iso_source = (REPO_ROOT / "kernel" / "fs" / "iso9660.cc").read_text(
            encoding="utf-8"
        )
        for fragment in (
            "typedef int (*susp_entry_cb)(const uint8_t *entry, "
            "uint32_t elen, void *ctx);",
            "static int susp_walk(block_device_t *bdev, const uint8_t *su, uint32_t su_len, "
            "susp_entry_cb cb,\n                     void *ctx)",
            "uint8_t elen = cur[i + 2];",
            "int rc = cb(cur + i, elen, ctx);",
            "static int nm_entry_cb(const uint8_t *e, uint32_t elen, "
            "void *ctx)",
            "(void)susp_walk(bdev, su, su_len, nm_entry_cb, &nc);",
        ):
            with self.subTest(active_iso_fragment=fragment):
                self.assertIn(fragment, iso_source)

        source = """
            typedef int (*susp_entry_cb)(const uint8_t *entry,
                                         uint32_t elen, void *ctx);

            int inspect(const uint8_t *entry, uint32_t elen, void *ctx) {
              if (entry == 0 || ctx != 0) return 1;
              return elen;
            }

            int susp_walk(susp_entry_cb cb);

            int susp_walk(susp_entry_cb cb) {
              uint8_t entry = 1;
              uint8_t elen = 7;
              return cb(&entry, elen, 0);
            }

            int forward_walk(susp_entry_cb cb) {
              return susp_walk(cb);
            }

            int main() {
              return forward_walk(inspect);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 7, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_global_matches_active_doom_callback(self):
        doom_header = (
            REPO_ROOT / "kernel" / "doom" / "src" / "v_video.h"
        ).read_text(encoding="utf-8")
        doom_source = (
            REPO_ROOT / "kernel" / "doom" / "src" / "v_video.cc"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "typedef boolean (*vpatchclipfunc_t)(patch_t *, int, int);",
            doom_header,
        )
        for fragment in (
            "static vpatchclipfunc_t patchclip_callback = NULL;",
            "void V_SetPatchClipCallback(vpatchclipfunc_t func)",
            "patchclip_callback = func;",
            "if (!patchclip_callback(patch, x, y))",
        ):
            with self.subTest(active_doom_fragment=fragment):
                self.assertIn(fragment, doom_source)

        source = """
            typedef int boolean;
            typedef boolean (*vpatchclipfunc_t)(int *patch, int x, int y);

            static vpatchclipfunc_t patchclip_callback = ((void *)0);

            boolean allow_patch(int *patch, int x, int y) {
              return *patch + x + y == 12;
            }

            void V_SetPatchClipCallback(vpatchclipfunc_t func) {
              patchclip_callback = func;
            }

            int main() {
              int patch = 3;
              if (patchclip_callback != 0) return 1;
              V_SetPatchClipCallback(allow_patch);
              if (!patchclip_callback(&patch, 4, 5)) return 2;
              V_SetPatchClipCallback(0);
              return patchclip_callback == 0 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_global_assignment_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-global-assignment-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*callback_t)(double value);
                callback_t callback = 0;

                int wrong(int value) { return value; }

                int main() {
                  callback = wrong;
                  return 1;
                }
                """,
                """
                typedef int (*callback_t)(double value);
                callback_t callback = 0;

                int right(double value) { return (int)value; }

                int main() {
                  callback = right;
                  return callback(9);
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer assignment parameters do not match destination",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_global_callback_initializes_from_defined_function_in_jit_and_aot(self):
        source = """
            typedef int (*callback_t)(int value);

            int target(int value) { return value + 4; }
            callback_t callback = ((target));

            int main() { return callback(7); }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 11, result.stdout + result.stderr
                )

    def test_global_callback_initializes_from_later_function_in_jit_and_aot(self):
        source = """
            typedef int (*callback_t)(int value);

            callback_t callback = target;
            int target(int value) { return value + 5; }

            int main() { return callback(8); }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 13, result.stdout + result.stderr
                )

    def test_global_callback_initializer_errors_recover_without_leaking_state(self):
        cases = (
            (
                "parameter mismatch",
                """
                typedef int (*callback_t)(double value);
                int wrong(int value) { return value; }
                callback_t callback = wrong;
                int main() { return callback(1); }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "unresolved function",
                """
                typedef int (*callback_t)(int value);
                int missing(int value);
                callback_t callback = missing;
                int main() { return callback(1); }
                """,
                "Unresolved symbol: missing",
            ),
            (
                "computed expression",
                """
                typedef int (*callback_t)(int value);
                int left(int value) { return value; }
                int right(int value) { return value + 1; }
                int choose;
                callback_t callback = choose ? left : right;
                int main() { return callback(1); }
                """,
                "global function-pointer initializer requires a function or null",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            callback_t callback = target;
            int target(int value) { return value + 1; }
            int main() { return callback(8); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-global-initializer-error-",
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
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_global_callback_later_definition_mismatch_restores_data_fixup(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-global-later-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*callback_t)(double value);
                callback_t callback = later;
                int later(int value) { return value; }
                int main() { return callback(1); }
                """,
                """
                typedef int (*callback_t)(int value);
                callback_t callback = later;
                int later(int value) { return value; }
                int main() { return callback(9); }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior "
                "function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_global_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*callback_t)(float4 left, int marker,
                                         float4 right);
            callback_t callback = ((void *)0);
            int calls;

            float4 merge(float4 left, int marker, float4 right) {
              calls += 1;
              if (marker != 7) return left;
              return left + right;
            }

            void set_callback(callback_t value) {
              callback = value;
            }

            int main() {
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              set_callback(merge);
              result = callback(left, 7, right);
              set_callback(0);
              if (callback != 0 || calls != 1) return 1;
              if (result.x != 6.0f || result.y != 8.0f) return 2;
              if (result.z != 10.0f || result.w != 12.0f) return 3;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*blend_cb)(float4 left, double marker,
                                       float4 right);

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            int invoke_blend(blend_cb cb) {
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              result = cb(left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }

            int main() {
              return invoke_blend(blend);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_keeps_variadic_promotions(self):
        source = """
            typedef int (*inspect_cb)(double fixed, ...);

            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int invoke_inspect(inspect_cb cb) {
              return cb(6, 2.5f);
            }

            int main() {
              return invoke_inspect(inspect);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 6, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_mismatch_recovers_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-parameter-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*susp_entry_cb)(const uint8_t *entry,
                                             uint32_t elen, void *ctx);

                int wrong(double value) {
                  return (int)value;
                }

                int susp_walk(susp_entry_cb cb) {
                  uint8_t entry = 1;
                  return cb(&entry, 7, 0);
                }

                int main() {
                  return susp_walk(wrong);
                }
                """,
                """
                typedef int (*susp_entry_cb)(const uint8_t *entry,
                                             uint32_t elen, void *ctx);

                int inspect(const uint8_t *entry, uint32_t elen,
                            void *ctx) {
                  if (entry == 0 || ctx != 0) return 1;
                  return elen;
                }

                int susp_walk(susp_entry_cb cb) {
                  uint8_t entry = 1;
                  return cb(&entry, 7, 0);
                }

                int main() {
                  return susp_walk(inspect);
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer argument parameters do not match parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 7, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_parameter_checks_full_signature(self):
        cases = (
            (
                "result",
                """
                typedef int (*callback_t)(int value);
                double wrong(int value) { return value; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "variadic",
                """
                typedef int (*callback_t)(double fixed, ...);
                int wrong(double fixed) { return (int)fixed; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                typedef struct One *(*callback_t)(struct One *value);
                struct Two *wrong(struct Two *value) { return value; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "nonzero integer",
                """
                typedef int (*callback_t)(int value);
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(1); }
                """,
                "function-pointer argument requires a function, zero, or "
                "explicit pointer cast",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            int right(int value) { return value; }
            int invoke(callback_t cb) { return cb(9); }
            int main() { return invoke(right); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-full-signature-",
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
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_function_pointer_typedef_parameter_keeps_null_and_erased_forms(self):
        source = """
            typedef int (*callback_t)(int value);

            double wrong(double value) {
              return value;
            }

            int inspect(callback_t cb) {
              return cb == 0 ? 3 : 5;
            }

            int main() {
              if (inspect(1 - 1) != 3) return 1;
              return inspect((void *)wrong) == 5 ? 0 : 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_parameter_prototype_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-prototype-mismatch-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*integer_cb)(int value);
                typedef int (*double_cb)(double value);
                int invoke(integer_cb cb);
                int invoke(double_cb cb) { return 0; }
                int main() { return 0; }
                """,
                """
                typedef int (*callback_t)(int value);
                int right(int value) { return value; }
                int invoke(callback_t cb);
                int invoke(callback_t cb) { return cb(4); }
                int main() { return invoke(right); }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function declaration does not match prior declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 4, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_argument_constrains_later_definition(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-later-definition-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*callback_t)(double value);
                int invoke(callback_t cb) { return cb(6); }
                int main() { return invoke(later); }
                int later(int value) { return value; }
                """,
                """
                typedef int (*callback_t)(double value);
                int invoke(callback_t cb) { return cb(6); }
                int main() { return invoke(later); }
                int later(double value) { return (int)value; }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior "
                "function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 6, runtime.stdout + runtime.stderr
            )

    def test_function_pointer_typedef_parameter_call_arity_recovers_same_state(self):
        cases = (
            (
                "too few",
                """
                typedef int (*callback_t)(int left, int right);
                int invoke(callback_t cb) { return cb(1); }
                int main() { return 0; }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many",
                """
                typedef int (*callback_t)(int value);
                int invoke(callback_t cb) { return cb(1, 2); }
                int main() { return 0; }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            int invoke(callback_t cb) { return cb(4, 5); }
            int main() { return invoke(add); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-arity-",
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
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode,
                    9,
                    runtime.stdout + runtime.stderr,
                )

    def test_function_pointer_typedef_parameter_capacity_accepts_32_and_recovers(self):
        accepted_parameters = ", ".join(
            f"int value{index}" for index in range(32)
        )
        accepted_arguments = ", ".join(
            str(index + 1) for index in range(32)
        )
        accepted_source = f"""
            typedef int (*callback_t)({accepted_parameters});

            int target({accepted_parameters}) {{
              return value0 + value31;
            }}

            int invoke(callback_t cb) {{
              return cb({accepted_arguments});
            }}

            int main() {{
              return invoke(target);
            }}
        """

        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(accepted_source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    33,
                    result.stdout + result.stderr,
                )

        rejected_parameters = ", ".join(
            f"int value{index}" for index in range(33)
        )
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-parameter-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            recovery, _code, _data = self._compile_after_failure(
                root,
                f"typedef int (*callback_t)({rejected_parameters});",
                accepted_source,
                same_state=True,
            )
            self.assertEqual(
                recovery.returncode,
                0,
                recovery.stdout + recovery.stderr,
            )
            self.assertIn(
                "too many function-pointer parameters",
                recovery.stderr,
            )
            runtime = self._run_i386(root, int(recovery.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                33,
                runtime.stdout + runtime.stderr,
            )

    def test_function_pointer_typedef_parameter_keeps_record_parameter_identity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-record-parameter-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                struct One { int value; };
                struct Two { int value; };
                typedef int (*callback_t)(struct One *value);
                int wrong(struct Two *value) { return value->value; }
                int invoke(callback_t cb) { return 0; }
                int main() { return invoke(wrong); }
                """,
                """
                struct One { int value; };
                typedef int (*callback_t)(struct One *value);
                int right(struct One *value) { return value->value; }
                int invoke(callback_t cb) {
                  struct One value;
                  value.value = 11;
                  return cb(&value);
                }
                int main() { return invoke(right); }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer argument parameters do not match parameter type",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 11, runtime.stdout + runtime.stderr
            )

    def test_named_variadic_function_pointer_converts_fixed_and_tail_slots(self):
        source = """
            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int main() {
              int (*callback)(double, ...) = inspect;
              return callback(6, 2.5f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 6, result.stdout + result.stderr
                )

    def test_named_function_pointer_keeps_nested_active_pointer_slots(self):
        result = self._compile_and_run(
            """
            int inspect(void *drawer, char *rows, int marker) {
              if (drawer == 0 || rows == 0) return 1;
              return marker;
            }

            void draw(int x, int y) {
              (void)x;
              (void)y;
            }

            int main() {
              char rows[6];
              int (*callback)(void (*)(int, int), uint8_t (*)[6], int) =
                  (void *)inspect;
              return callback(draw, rows, 9);
            }
            """
        )
        self.assertEqual(result.returncode, 9, result.stdout + result.stderr)

    def test_named_function_pointer_initializer_parameter_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-param-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int transform(double value) {
                  return (int)value;
                }

                int invalid_initializer() {
                  int (*callback)(int) = transform;
                  return callback(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(7) == 7 ? 0 : 1;
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

    def test_named_function_pointer_initializer_result_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                double transform(int value) {
                  return (double)value;
                }

                int invalid_initializer() {
                  int (*callback)(int) = transform;
                  return callback(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(9) == 9 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer result does not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_initializer_keeps_record_result_identity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-record-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                struct First { int value; };
                struct Second { int value; };
                struct First first;

                struct First *select_first(void) {
                  return &first;
                }

                int invalid_initializer() {
                  struct Second *(*callback)(void) = select_first;
                  return callback()->value;
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
            "function-pointer initializer result does not match declaration",
            result.stderr,
        )

    def test_named_function_pointer_initializer_variadic_mismatch_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-initializer-variadic-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int inspect(int value, ...) {
                  return value;
                }

                int invalid_initializer() {
                  int (*callback)(int) = inspect;
                  return callback(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(5) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer parameters do not match declaration",
            result.stderr,
        )

    def test_explicit_cast_erases_function_pointer_initializer_signature(self):
        result = self._compile_and_run(
            """
            double transform(double value) {
              return value;
            }

            int main() {
              int (*callback)(int) = (void *)transform;
              return callback != 0 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_grouped_null_and_cast_function_pointer_initializers_run(self):
        source = """
            int observed;

            int identity(int value) {
              observed = value;
              return value;
            }

            int main() {
              int (*empty)(int) = ((void *)0);
              int (*callback)(int) = ((void *)identity);
              if (empty != 0) return 1;
              if (callback(12) != 12) return 2;
              return observed == 12 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_integer_constant_expression_zero_is_a_null_callback(self):
        source = r"""
            int choose_target;
            int identity(int value) { return value; }

            int main() {
              int (*negative_zero)(int) = -0;
              int (*positive_zero)(int) = +0;
              int (*character_zero)(int) = '\0';
              int (*cast_zero)(int) = (int)0;
              int (*difference_zero)(int) = 1 - 1;
              int (*sizeof_zero)(int) = sizeof(int) - 4;
              choose_target = 1;
              int (*left_target)(int) =
                  choose_target ? identity : -0;
              choose_target = 0;
              int (*right_target)(int) =
                  choose_target ? 1 - 1 : identity;
              if (negative_zero != 0 || positive_zero != 0 ||
                  character_zero != 0 || cast_zero != 0 ||
                  difference_zero != 0 || sizeof_zero != 0) return 1;
              if (left_target(9) != 9) return 2;
              return right_target(11) == 11 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-nonconstant-zero-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int value;
                int invalid(void) {
                  int (*callback)(int) = 1 ? 0 : value;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main(void) {
                  int (*callback)(int) = identity;
                  return callback(8) == 8 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_function_pointer_initializer_patches_a_later_function_address(self):
        source = """
            double later(double value);

            int main() {
              double (*callback)(double) = later;
              return callback(6) == 6.0 ? 0 : 1;
            }

            double later(double value) {
              return value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_callback_target_definition_must_match_its_prior_prototype(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-prototype-definition-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                double later(double value);

                int invalid_initializer() {
                  double (*callback)(double) = later;
                  return callback != 0;
                }

                int later(int value) {
                  return value;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(1) == 1 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function declaration does not match prior declaration",
            result.stderr,
        )

    def test_prescanned_callback_signature_is_checked_at_later_definition(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-later-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invalid_initializer() {
                  double (*callback)(double) = later;
                  return callback != 0;
                }

                int later(int value) {
                  return value;
                }
                """,
                """
                double later(double value) {
                  return value;
                }

                int main() {
                  double (*callback)(double) = later;
                  return callback(3) == 3.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_malformed_later_callback_declaration_restores_provisional_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-malformed-later-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invalid_initializer() {
                  double (*callback)(double) = later;
                  return callback != 0;
                }

                double later(double) {
                  return 0.0;
                }
                """,
                """
                double later(double value) {
                  return value;
                }

                int main() {
                  double (*callback)(double) = later;
                  return callback(11) == 11.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("expected parameter name", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_defined_start_thunk_rejects_an_incompatible_callback_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-start-signature-",
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
                int capture(void) {
                  void (*wrong)(int) = __start;
                  return wrong != 0;
                }
                """,
                """
                int main(void) {
                  void (*right)(void) = __start;
                  marker = 0;
                  right();
                  return marker == 1 ? 0 : 1;
                }
                """,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer initializer parameters do not match declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_implicit_start_thunk_has_a_complete_void_callback_signature(self):
        source = """
            int marker;
            marker = 1;

            int capture(void) {
              void (*callback)(void) = __start;
              return callback != 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                with tempfile.TemporaryDirectory(
                    prefix="private-cupidc-start-signature-output-",
                    ignore_cleanup_errors=True,
                ) as temporary:
                    result, code_path, _data = self._compile(
                        Path(temporary), source, aot=aot
                    )
                    self.assertEqual(
                        result.returncode,
                        0,
                        result.stdout + result.stderr,
                    )
                    self.assertGreater(code_path.stat().st_size, 0)

    def test_failed_method_restores_inferred_callback_signatures(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-method-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                class Broken {
                  int Fail(void) {
                    int (*callback)(int) = later;
                    return missing_value;
                  }
                };

                int later(int value);
                """,
                """
                double later(double value) { return value + 2.0; }

                int main() {
                  double (*callback)(double) = later;
                  return callback(5) == 7.0 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_failed_callback_unit_discards_every_new_forward_patch(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-program-patches-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int pending(void);

                int invalid_initializer() {
                  double (*callback)(double) = later;
                  pending();
                  return callback != 0;
                }

                int later(int value) {
                  return value;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(13) == 13 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function definition does not match prior function-pointer initializer",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_prescanned_matching_callback_runs_in_jit_and_aot(self):
        source = """
            int observed;

            int main() {
              int (*callback)(int) = later;
              if (callback(8) != 8) return 1;
              return observed == 8 ? 0 : 2;
            }

            int later(int value) {
              observed = value;
              return value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_provisional_callback_signature_refines_from_empty_to_typed(self):
        source = """
            int observed;

            int main() {
              int (*loose)() = later;
              int (*typed)(int) = later;
              if (typed(10) != 10) return 1;
              return loose != 0 && observed == 10 ? 0 : 2;
            }

            int later(int value) {
              observed = value;
              return value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_conditional_function_pointer_keeps_compatible_signatures(self):
        source = """
            int choose_second;

            int main() {
              int (*callback)(int) =
                  choose_second ? later_second : later_first;
              return callback(7) == 8 ? 0 : 1;
            }

            int later_first(int value) {
              return value + 1;
            }

            int later_second(int value) {
              return value + 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_conditional_function_pointer_accepts_null_in_either_arm(self):
        source = """
            int choose_null;
            int observed;

            int target(int value) {
              observed = value;
              return value;
            }

            int main() {
              choose_null = 0;
              int (*empty)(int) = choose_null ? target : 0;
              int (*callback)(int) = choose_null ? 0 : target;
              if (empty != 0) return 1;
              if (callback(15) != 15) return 2;
              return observed == 15 ? 0 : 3;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_conditional_function_pointer_checks_every_later_arm(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-conditional-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int flag;

                int invalid_initializer() {
                  double (*callback)(double) = flag ? later_a : later_b;
                  return callback != 0;
                }

                double later_a(double value) { return value; }
                int later_b(int value) { return value; }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(8) == 8 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function definition does not match prior function-pointer initializer",
            result.stderr,
        )

    def test_named_function_pointer_copy_checks_retained_signature(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-copy-signature-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int inspect(double value) {
                  return (int)value;
                }

                int invalid_copy() {
                  int (*first)(double) = inspect;
                  int (*second)(int) = first;
                  return second(1);
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*first)(int) = identity;
                  int (*second)(int) = first;
                  return second(7) == 7 ? 0 : 1;
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

    def test_function_pointer_initializer_rejects_non_callable_values(self):
        cases = (
            ("floating value", "1.0", "floating"),
            ("nonzero integer", "7", "integer"),
            ("object pointer", "&value", "pointer"),
        )
        for label, initializer, declaration in cases:
            with self.subTest(initializer=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-function-pointer-value-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile_after_failure(
                    Path(temporary),
                    f"""
                    int invalid_initializer() {{
                      int value = 1;
                      int (*callback)(int) = {initializer};
                      return value;
                    }}
                    """,
                    """
                    int identity(int value) {
                      return value;
                    }

                    int main() {
                      int (*callback)(int) = identity;
                      return callback(2) == 2 ? 0 : 1;
                    }
                    """,
                    same_state=True,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(
                    "function-pointer initializer requires a function, zero, or explicit pointer cast",
                    result.stderr,
                )
                self.assertNotIn(declaration + " conversion", result.stderr)

    def test_function_pointer_initializer_accepts_zero(self):
        result = self._compile_and_run(
            """
            int main() {
              int (*callback)(int) = 0;
              return callback == 0 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mutable_enum_storage_is_not_proven_as_a_null_callback(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-enum-zero-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                enum { NIL = 0 };

                int invalid_initializer() {
                  NIL = 7;
                  int (*callback)(int) = NIL;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(3) == 3 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_function_pointer_initializer_checks_record_parameter_identity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-record-parameter-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                struct First { int value; };
                struct Second { int value; };

                int inspect(struct First *value) {
                  return value->value;
                }

                int invalid_initializer() {
                  int (*callback)(struct Second *) = inspect;
                  return 0;
                }
                """,
                """
                int identity(int value) {
                  return value;
                }

                int main() {
                  int (*callback)(int) = identity;
                  return callback(6) == 6 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer parameters do not match declaration",
            result.stderr,
        )

    def test_named_function_pointer_type_failure_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int inspect(double value) {
                  return (int)value;
                }

                int invalid_call() {
                  float4 value = {1.0f, 2.0f, 3.0f, 4.0f};
                  int (*callback)(double) = inspect;
                  return callback(value);
                }
                """,
                """
                int inspect(double value) {
                  return (int)value;
                }

                int main() {
                  int (*callback)(double) = inspect;
                  return callback(7) == 7 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_arity_failure_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-arity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int invalid_call() {
                  int (*callback)(double, int) = combine;
                  return callback(1);
                }
                """,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int main() {
                  int (*callback)(double, int) = combine;
                  return callback(2, 3) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer call has too few arguments", result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_excess_arity_recovers_in_the_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-excess-arity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int invalid_call() {
                  int (*callback)(double, int) = combine;
                  return callback(1, 2, 3);
                }
                """,
                """
                int combine(double first, int second) {
                  return (int)first + second;
                }

                int main() {
                  int (*callback)(double, int) = combine;
                  return callback(2, 3) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "function-pointer call has too many arguments", result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_named_function_pointer_struct_result_is_rejected_and_recovers(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-struct-result-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                struct Pair {
                  int value;
                };

                int invalid_call() {
                  struct Pair (*callback)(void);
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
                "function-pointer struct result is not supported; use pointer result",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
        self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

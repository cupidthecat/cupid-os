"""Private compiler callbacks execution cases."""

from .harness import (KERNEL_LANG, Path, REPO_ROOT, tempfile)

class Callbacks1Cases:
    def test_halfword_native_results_through_named_and_record_callbacks(self):
        self._assert_halfword_runtime('''
            typedef U16 (*Convert)(U16);
            struct Slot { Convert convert; };
            int main() {
                Convert direct = htons;
                Slot slot;
                slot.convert = ntohs;
                if (direct(70196.75) != 13330) return 1;
                if (slot.convert(70196.75) != 13330) return 2;
                return 0;
            }
        ''')

    def test_halfword_typedef_callback_parameter_and_return_types(self):
        self._assert_halfword_runtime('''
            typedef I16 (*Adjust)(U16, I16, int);
            I16 adjust(U16 u, I16 s, int tail) { return u + s + tail; }
            int main() {
                Adjust callback = adjust;
                if (callback(65535.0, -2.0, 31.0) != 28) return 1;
                return 0;
            }
        ''')

    def test_named_function_pointer_coercion_and_result_run_in_jit_and_aot(self):
        source = """
            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            double echo_double(double value) {
              return value;
            }

            int main() {
              int (*combine_callback)(double, int) = combine;
              double (*echo_callback)(double) = echo_double;
              int combined = combine_callback(3, 4.75f);
              double echoed = echo_callback(5);
              if (combined != 34) return 1;
              return echoed == 5.0 ? 0 : 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_raw_global_callbacks_match_active_spelling_in_jit_and_aot(self):
        panic_source = (
            REPO_ROOT / "kernel" / "core" / "panic.cc"
        ).read_text(encoding="utf-8")
        panic_header = (
            REPO_ROOT / "kernel" / "core" / "panic.h"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "static void (*panic_print)(const char *) = print;",
            panic_source,
        )
        self.assertIn(
            "void panic_set_output(void (*print_fn)(const char*), "
            "void (*putchar_fn)(char));",
            panic_header,
        )

        source = """
            int ready_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int observed_character;
            void observe(const char *text) {
              observed_character = *text;
            }

            static int (*empty_callback)(double, int) = 0;
            static int (*ready_callback)(double, int) = ready_target;
            static int (*later_callback)(double, int) = later_target;
            static void (*panic_style_output)(const char*) = observe;

            int later_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int main() {
              int result;
              if (empty_callback != 0) return 1;
              empty_callback = ready_target;
              result = empty_callback(3, 4.75f);
              empty_callback = 0;
              if (empty_callback != 0) return 2;
              panic_style_output("A");
              if (observed_character != 65) return 3;
              result += ready_callback(2, 5.5f);
              result += later_callback(1, 6.25f);
              return result;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    75,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_initializers_accept_explicit_function_addresses(self):
        source = """
            int ready_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int later_target(double value, int marker);
            static int (*ready_callback)(double, int) = (&ready_target);
            static int (*later_callback)(double, int) = &(later_target);

            int later_target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int main() {
              int (*local_callback)(double, int) = &ready_target;
              local_callback = &later_target;
              return local_callback(3, 4.75f) +
                     ready_callback(2, 5.5f) +
                     later_callback(1, 6.25f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 75, result.stdout + result.stderr
                )

    def test_raw_callback_parameter_keeps_mixed_width_slots_in_jit_and_aot(self):
        source = """
            int inspect(const uint8_t *entry, double scale, int marker) {
              if (entry == 0 || *entry != 7) return 1;
              return (int)(scale * 10.0) + marker;
            }

            int invoke(int (*callback)(const uint8_t *, double, int),
                       const uint8_t *entry) {
              return callback(entry, 4, 5.75f);
            }

            int main() {
              uint8_t entry = 7;
              return invoke(inspect, &entry);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    45,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_keeps_variadic_promotions_in_jit_and_aot(self):
        source = """
            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int invoke(int (*callback)(double, ...)) {
              return callback(6, 2.5f);
            }

            int main() {
              return invoke(inspect);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    6,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_keeps_record_identity_in_jit_and_aot(self):
        source = """
            struct Entry { int value; };

            int inspect(struct Entry *entry) {
              return entry->value;
            }

            int invoke(int (*callback)(struct Entry *),
                       struct Entry *entry) {
              return callback(entry);
            }

            int main() {
              struct Entry entry;
              entry.value = 13;
              return invoke(inspect, &entry);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    13,
                    result.stdout + result.stderr,
                )

    def test_nested_raw_callback_parameters_match_active_cupidc_shape(self):
        compiler_source = (KERNEL_LANG / "cupidc.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "void (*p_icon_set_drawer)(int, void (*)(int, int)) =",
            compiler_source,
        )
        icon_header = (
            REPO_ROOT / "kernel" / "gfx" / "gfx2d_icons.h"
        ).read_text(encoding="utf-8")
        icon_source = (
            REPO_ROOT / "kernel" / "gfx" / "gfx2d_icons.cc"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "void gfx2d_icon_set_custom_drawer(int handle, "
            "void (*drawer)(int, int));",
            icon_header,
        )
        self.assertIn(
            "void gfx2d_icon_set_custom_drawer(int handle, "
            "void (*drawer)(int, int)) {",
            icon_source,
        )
        self.assertIn(
            "cc_retain_kernel_binding_callback_signature",
            compiler_source,
        )

        source = """
            int observed;

            void draw(int x, int y) {
              observed = x * 10 + y;
            }

            void install(int slot, void (*drawer)(int, int)) {
              drawer(slot, 7);
            }

            int main() {
              void (*binding)(int, void (*)(int, int)) = install;
              binding(4, draw);
              return observed;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    47,
                    result.stdout + result.stderr,
                )

    def test_nested_callback_signatures_match_across_raw_and_typedef_forms(self):
        source = """
            typedef void (*drawer_t)(int, int);
            int observed;

            void draw(int x, int y) {
              observed = x * 10 + y;
            }

            void install_named(int slot, drawer_t drawer) {
              drawer(slot, 2);
            }

            void install_raw(int slot, void (*drawer)(int, int)) {
              drawer(slot, 3);
            }

            int main() {
              void (*raw_binding)(int, void (*)(int, int)) = install_named;
              void (*named_binding)(int, drawer_t) = install_raw;
              raw_binding(4, draw);
              if (observed != 42) return 1;
              named_binding(5, draw);
              return observed;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    53,
                    result.stdout + result.stderr,
                )

    def test_recursive_callback_signatures_execute_in_jit_and_aot(self):
        source = """
            int observed;

            void leaf(int value) {
              observed = value;
            }

            void middle(int value, void (*callback)(int)) {
              callback(value + 1);
            }

            void outer(int value,
                       void (*callback)(int, void (*)(int))) {
              callback(value, leaf);
            }

            int main() {
              void (*binding)(
                  int, void (*)(int, void (*)(int))) = outer;
              binding(8, middle);
              return observed;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    9,
                    result.stdout + result.stderr,
                )

    def test_nested_record_and_variadic_callbacks_execute_in_jit_and_aot(self):
        sources = (
            (
                "record",
                """
                struct Entry { int value; };
                struct Entry *select(struct Entry *entry) { return entry; }
                struct Entry *install(
                    struct Entry *(*picker)(struct Entry *),
                    struct Entry *entry) {
                  return picker(entry);
                }
                int main() {
                  struct Entry entry;
                  struct Entry *(*setter)(
                      struct Entry *(*)(struct Entry *),
                      struct Entry *) = install;
                  entry.value = 47;
                  return setter(select, &entry)->value;
                }
                """,
                47,
            ),
            (
                "variadic",
                """
                int inspect(double fixed, ...) { return (int)fixed; }
                int invoke(int (*callback)(double, ...)) {
                  return callback(6, 2.5f);
                }
                int main() {
                  int (*outer)(int (*)(double, ...)) = invoke;
                  return outer(inspect);
                }
                """,
                6,
            ),
        )
        for label, source, expected in sources:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode,
                        expected,
                        result.stdout + result.stderr,
                    )

    def test_nested_callback_mismatches_recover_same_state(self):
        cases = (
            (
                "initializer parameter",
                """
                void wrong(int slot, void (*drawer)(double)) { (void)slot; }
                int main() {
                  void (*binding)(int, void (*)(int, int)) = wrong;
                  return binding != 0;
                }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "assignment result",
                """
                void right(int slot, void (*drawer)(int, int)) { (void)slot; }
                void wrong(int slot, int (*drawer)(int, int)) { (void)slot; }
                int main() {
                  void (*binding)(int, void (*)(int, int)) = right;
                  binding = wrong;
                  return binding != 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "higher-order fixed argument",
                """
                void wrong(int slot, void (*drawer)(double)) { (void)slot; }
                void accept_callback(void (*binding)(int, void (*)(int, int))) {
                  (void)binding;
                }
                int main() {
                  accept_callback(wrong);
                  return 0;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                void wrong(int slot, void (*drawer)(struct Two *)) {
                  (void)slot;
                }
                void accept_callback(
                    void (*binding)(int, void (*)(struct One *))) {
                  (void)binding;
                }
                int main() {
                  accept_callback(wrong);
                  return 0;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "variadic boundary",
                """
                void wrong(int slot, void (*drawer)(int)) { (void)slot; }
                void accept_callback(void (*binding)(int, void (*)(int, ...))) {
                  (void)binding;
                }
                int main() {
                  accept_callback(wrong);
                  return 0;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "prototype definition",
                """
                void accept_callback(
                    void (*setter)(int, void (*)(int, int)));
                void accept_callback(
                    void (*setter)(int, void (*)(double))) {
                  (void)setter;
                }
                int main() { return 0; }
                """,
                "function declaration does not match prior declaration",
            ),
            (
                "nested prototype state",
                """
                void accept_callback(void (*setter)(int (*)()));
                void accept_callback(void (*setter)(int (*)(void))) {
                  (void)setter;
                }
                int main() { return 0; }
                """,
                "function declaration does not match prior declaration",
            ),
            (
                "indirect nested result",
                """
                struct Entry { int value; };
                struct Wrong { int value; };
                struct Wrong *wrong(struct Entry *entry) { return 0; }
                void install(
                    struct Entry *(*callback)(struct Entry *)) {
                  (void)callback;
                }
                int main() {
                  void (*setter)(
                      struct Entry *(*)(struct Entry *)) = install;
                  setter(wrong);
                  return 0;
                }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "indirect nested parameter",
                """
                struct Entry { int value; };
                struct Wrong { int value; };
                struct Entry *wrong(struct Wrong *entry) { return 0; }
                void install(
                    struct Entry *(*callback)(struct Entry *)) {
                  (void)callback;
                }
                int main() {
                  void (*setter)(
                      struct Entry *(*)(struct Entry *)) = install;
                  setter(wrong);
                  return 0;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "indirect nested variadic boundary",
                """
                int wrong(double value) { return (int)value; }
                void install(int (*callback)(double, ...)) {
                  (void)callback;
                }
                int main() {
                  void (*setter)(int (*)(double, ...)) = install;
                  setter(wrong);
                  return 0;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "conditional",
                """
                void left(int slot, void (*drawer)(int, int)) { (void)slot; }
                void right(int slot, void (*drawer)(double)) { (void)slot; }
                int main() {
                  void (*binding)(int, void (*)(int, int)) =
                      1 ? left : right;
                  return binding != 0;
                }
                """,
                "conditional function-pointer signatures do not match",
            ),
            (
                "later definition",
                """
                void (*binding)(int, void (*)(int, int)) = later;
                void later(int slot, void (*drawer)(double)) { (void)slot; }
                int main() { return binding != 0; }
                """,
                "function definition does not match prior function-pointer initializer",
            ),
            (
                "pointer to function pointer",
                """
                int main() {
                  void (*binding)(int (**)(int)) = 0;
                  return binding != 0;
                }
                """,
                "function-pointer parameter declarator requires exactly one '*'",
            ),
        )
        retry_source = """
            int observed;
            void draw(int x, int y) { observed = x * 10 + y; }
            void install(int slot, void (*drawer)(int, int)) {
              drawer(slot, 9);
            }
            int main() {
              void (*binding)(int, void (*)(int, int)) = install;
              binding(6, draw);
              return observed;
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-nested-callback-",
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
                    69,
                    runtime.stdout + runtime.stderr,
                )

    def test_nested_callback_signature_depth_failure_recovers_same_state(self):
        nested_parameter = "int"
        for _depth in range(18):
            nested_parameter = f"int (*)({nested_parameter})"
        failing_source = (
            f"int (*binding)({nested_parameter});\n"
            "int main() { return binding != 0; }"
        )
        retry_source = """
            int identity(int value) { return value; }
            int invoke(int (*callback)(int)) { return callback(9); }
            int main() { return invoke(identity); }
        """

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-nested-callback-depth-",
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
            self.assertIn(
                "function-pointer signature nesting is too deep",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_typedef_backed_nested_callback_depth_is_bounded(self):
        typedefs = ["typedef int (*layer0)(int);"]
        for depth in range(1, 16):
            typedefs.append(
                f"typedef int (*layer{depth})(layer{depth - 1});"
            )
        accepted_source = "\n".join(typedefs)
        accepted_source += "\nint (*binding)(layer15) = 0;"
        accepted_source += "\nint main() { return binding != 0; }"

        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(accepted_source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )

        failing_source = "\n".join(typedefs)
        failing_source += "\nint (*binding)(int (*)(layer15)) = 0;"
        failing_source += "\nint main() { return binding != 0; }"
        retry_source = """
            int identity(int value) { return value; }
            int invoke(int (*callback)(int)) { return callback(9); }
            int main() { return invoke(identity); }
        """
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-typedef-callback-depth-",
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
            self.assertIn(
                "function-pointer signature nesting is too deep",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_nested_callback_signature_capacity_recovers_same_state(self):
        declarations = []
        for arity in range(33):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            declarations.append(
                f"int (*holder{arity})(int (*)({parameters})) = 0;"
            )
        failing_source = "\n".join(declarations)
        failing_source += "\nint main() { return 0; }"
        retry_source = """
            int identity(int value) { return value; }
            int invoke(int (*callback)(int)) { return callback(9); }
            int main() { return invoke(identity); }
        """

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-nested-callback-capacity-",
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
            self.assertIn(
                "too many raw function-pointer signatures",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_raw_callback_parameter_prototype_matches_definition(self):
        source = """
            int invoke(int (*callback)(double, int));

            int target(double value, int marker) {
              return (int)(value * 10.0) + marker;
            }

            int invoke(int (*callback)(double, int)) {
              return callback(3, 4.75f);
            }

            int main() {
              return invoke(target);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    34,
                    result.stdout + result.stderr,
                )

    def test_raw_callback_parameter_prototype_mismatch_recovers_same_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-raw-callback-prototype-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                int invoke(int (*callback)(int));
                int invoke(int (*callback)(double)) {
                  return callback(9);
                }
                int main() { return 0; }
                """,
                """
                int identity(int value) { return value; }
                int invoke(int (*callback)(int));
                int invoke(int (*callback)(int)) {
                  return callback(9);
                }
                int main() { return invoke(identity); }
                """,
                same_state=True,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "function declaration does not match prior declaration",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_raw_callback_parameter_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                double wrong(int value) { return value; }
                int invoke(int (*callback)(int)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "parameters",
                """
                int wrong(int value) { return value; }
                int invoke(int (*callback)(double)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                int wrong(struct Two *value) { return value->value; }
                int invoke(int (*callback)(struct One *)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "variadic boundary",
                """
                int wrong(double fixed) { return (int)fixed; }
                int invoke(int (*callback)(double, ...)) { return 0; }
                int main() { return invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "too few arguments",
                """
                int invoke(int (*callback)(int, int)) {
                  return callback(1);
                }
                int main() { return 0; }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many arguments",
                """
                int invoke(int (*callback)(int)) {
                  return callback(1, 2);
                }
                int main() { return 0; }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            int add(int left, int right) { return left + right; }
            int invoke(int (*callback)(int, int)) {
              return callback(4, 5);
            }
            int main() { return invoke(add); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-parameter-",
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

    def test_raw_callback_signature_capacity_recovers_same_state(self):
        declarations = []
        for arity in range(33):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )
        failing_source = "\n".join(declarations) + "\nint main() { return 0; }"
        retry_source = """
            int identity(int value) { return value; }
            int invoke(int (*callback)(int)) { return callback(9); }
            int main() { return invoke(identity); }
        """

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-raw-callback-capacity-",
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
            self.assertIn(
                "too many raw function-pointer signatures",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_repl_rolls_back_a_failed_raw_callback_signature(self):
        capacity_declarations = []
        for arity in range(32):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            capacity_declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-raw-callback-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "int poisoned(int (*callback)(double)) { "
                        "return missing_value; }",
                        "\n".join(capacity_declarations),
                        "int identity(int value) { return value; } "
                        "int invoke(int (*callback)(int)) { "
                        "return callback(9); } "
                        "int main() { return invoke(identity); }",
                    ),
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )
            runtime_source = (KERNEL_LANG / "cupidc.cc").read_text(
                encoding="utf-8"
            )
            self.assertIn(
                "repl_state.cc->raw_function_pointer_signature_count =",
                runtime_source,
            )
            self.assertIn(
                "repl_state.raw_function_pointer_signature_committed",
                runtime_source,
            )

    def test_repl_keeps_raw_callback_globals_for_later_units(self):
        result = self._compile_repl_and_run(
            (
                """
                int ready_target(double value, int marker) {
                  return (int)(value * 10.0) + marker;
                }
                """,
                "static int (*empty_callback)(double, int) = 0;",
                "static int (*ready_callback)(double, int) = ready_target;",
                "int later_target(double value, int marker);",
                "static int (*later_callback)(double, int) = later_target;",
                """
                int later_target(double value, int marker) {
                  return (int)(value * 10.0) + marker;
                }
                """,
                """
                int main() {
                  int result;
                  if (empty_callback != 0) return 1;
                  empty_callback = ready_target;
                  result = empty_callback(3, 4.75f);
                  empty_callback = 0;
                  if (empty_callback != 0) return 2;
                  result += ready_callback(2, 5.5f);
                  result += later_callback(1, 6.25f);
                  return result;
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 75, result.stdout + result.stderr)

    def test_raw_global_callback_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                double wrong(int value) { return value; }
                static int (*callback)(int) = wrong;
                int main() { return callback(1); }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "parameters",
                """
                int wrong(int value) { return value; }
                static int (*callback)(double) = wrong;
                int main() { return callback(1); }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
        )
        retry_source = """
            int right(double value) { return (int)value; }
            static int (*callback)(double) = right;
            int main() { return callback(9); }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-global-",
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

    def test_named_function_pointer_record_pointer_result_keeps_identity(self):
        source = """
            struct Pair {
              int left;
              int right;
            };

            struct Pair values[2];

            struct Pair *select_pair(int index) {
              return &values[index];
            }

            int main() {
              struct Pair *(*callback)(int) = select_pair;
              values[0].left = 11;
              values[1].left = 33;
              values[1].right = 44;
              if (callback(1)->right != 44) return 1;
              return callback(0)[1].left == 33 ? 0 : 2;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_named_function_pointer_uses_typed_cdecl_slot_widths(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-layout-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, code_path, _data = self._compile(
                root,
                """
                int combine(double first, int second) {
                  return (int)first * 10 + second;
                }

                int main() {
                  int (*callback)(double, int) = combine;
                  return callback(3, 4.75f);
                }
                """,
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            code = code_path.read_bytes()
            self.assertIn(b"\xff\xd0\x83\xc4\x0c", code)

    def test_named_function_pointer_matches_active_callback_widths(self):
        result = self._compile_and_run(
            """
            int inspect(const uint8_t *entry, uint32_t length, void *context) {
              if (entry == 0 || context != 0) return 1;
              return length;
            }

            int main() {
              uint8_t entry = 1;
              uint8_t length = 7;
              int (*callback)(const uint8_t *, uint32_t, void *) = inspect;
              return callback(&entry, length, 0);
            }
            """
        )
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)

    def test_function_pointer_typedef_automatic_local_keeps_mixed_width_slots(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int main() {
              callback_t callback = combine;
              return callback(3, 4.75f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 34, result.stdout + result.stderr
                )

    def test_each_function_pointer_typedef_automatic_declarator_keeps_its_signature(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int subtract(double first, int second) {
              return (int)first * 10 - second;
            }

            int main() {
              callback_t first = combine, second = subtract;
              return first(3, 4.75f) + second(8, 2.25f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 112, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_method_parameter_keeps_mixed_width_slots(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            class Invoker {
              int Invoke(callback_t callback) {
                return callback(3, 4.75f);
              }
            };

            int main() {
              Invoker invoker;
              return invoker.Invoke(combine);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 34, result.stdout + result.stderr
                )

    def test_each_function_pointer_typedef_method_parameter_keeps_its_signature(self):
        source = """
            typedef int (*integer_cb)(int value);
            typedef int (*mixed_cb)(double first, int second);

            int identity(int value) {
              return value;
            }

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            class Invoker {
              int Invoke(integer_cb first, mixed_cb second) {
                return first(3) + second(4, 5.75f);
              }
            };

            int main() {
              Invoker invoker;
              return invoker.Invoke(identity, combine);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 48, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_automatic_and_method_calls_clean_exact_slots(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(double first, int second);
                int combine(double first, int second) { return second; }
                int main() {
                  callback_t callback = combine;
                  return callback(3, 4.75f);
                }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(double first, int second);
                int combine(double first, int second) { return second; }
                class Invoker {
                  int Invoke(callback_t callback) {
                    return callback(3, 4.75f);
                  }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(combine);
                }
                """,
            ),
        )
        for label, source in cases:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot), tempfile.TemporaryDirectory(
                    prefix="private-cupidc-callback-typedef-cleanup-",
                    ignore_cleanup_errors=True,
                ) as temporary:
                    result, code_path, _data = self._compile(
                        Path(temporary), source, aot=aot
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    self.assertEqual(
                        code_path.read_bytes().count(b"\xff\xd0\x83\xc4\x0c"),
                        1,
                    )

    def test_function_pointer_typedef_automatic_local_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*blend_cb)(float4 left, double marker,
                                       float4 right);

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            int main() {
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              blend_cb callback = blend;
              result = callback(left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_method_parameter_keeps_simd_slots_and_result(self):
        source = """
            typedef float4 (*blend_cb)(float4 left, double marker,
                                       float4 right);

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            class Invoker {
              float4 Invoke(blend_cb callback, float4 left,
                            double marker, float4 right) {
                return callback(left, marker, right);
              }
            };

            int main() {
              Invoker invoker;
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              result = invoker.Invoke(blend, left, 9, right);
              if (result.x != 6.0f || result.y != 8.0f) return 1;
              if (result.z != 10.0f || result.w != 12.0f) return 2;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_function_pointer_typedef_automatic_and_method_forms_keep_null_and_erasure(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(int value);
                double wrong(double value) { return value; }
                int main() {
                  callback_t empty = sizeof(int) - 4;
                  callback_t erased = (void *)wrong;
                  if (empty != 0) return 1;
                  return erased != 0 ? 0 : 2;
                }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(int value);
                double wrong(double value) { return value; }
                class Inspector {
                  int Check(callback_t callback) {
                    return callback == 0 ? 3 : 5;
                  }
                };
                int main() {
                  Inspector inspector;
                  if (inspector.Check(sizeof(int) - 4) != 3) return 1;
                  return inspector.Check((void *)wrong) == 5 ? 0 : 2;
                }
                """,
            ),
        )
        for label, source in cases:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )

    def test_function_pointer_typedef_automatic_and_method_forms_fix_later_targets(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(double value);
                int main() {
                  callback_t callback = later;
                  return callback(6);
                }
                int later(double value) { return (int)value; }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(double value);
                class Invoker {
                  int Invoke(callback_t callback) {
                    return callback(6);
                  }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(later);
                }
                int later(double value) { return (int)value; }
                """,
            ),
        )
        for label, source in cases:
            for aot in (False, True):
                with self.subTest(case=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode, 6, result.stdout + result.stderr
                    )

    def test_function_pointer_typedef_automatic_initializer_mismatches_recover(self):
        cases = (
            (
                "result",
                """
                typedef int (*callback_t)(int value);
                double wrong(int value) { return value; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "parameter",
                """
                typedef int (*callback_t)(double value);
                int wrong(int value) { return value; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                typedef int (*callback_t)(struct One *value);
                int wrong(struct Two *value) { return value->value; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "variadic boundary",
                """
                typedef int (*callback_t)(double fixed, ...);
                int wrong(double fixed) { return (int)fixed; }
                int main() { callback_t callback = wrong; return 0; }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            int main() {
              callback_t callback = add;
              return callback(4, 5);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-local-mismatch-",
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

    def test_function_pointer_typedef_automatic_call_arity_recovers(self):
        cases = (
            (
                "too few",
                """
                typedef int (*callback_t)(int left, int right);
                int add(int left, int right) { return left + right; }
                int main() {
                  callback_t callback = add;
                  return callback(1);
                }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many",
                """
                typedef int (*callback_t)(int value);
                int identity(int value) { return value; }
                int main() {
                  callback_t callback = identity;
                  return callback(1, 2);
                }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            int main() {
              callback_t callback = add;
              return callback(4, 5);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-local-arity-",
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

    def test_function_pointer_typedef_method_argument_mismatches_recover(self):
        cases = (
            (
                "result",
                """
                typedef int (*callback_t)(int value);
                double wrong(int value) { return value; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "parameter",
                """
                typedef int (*callback_t)(double value);
                int wrong(int value) { return value; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "record identity",
                """
                struct One { int value; };
                struct Two { int value; };
                typedef int (*callback_t)(struct One *value);
                int wrong(struct Two *value) { return value->value; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "variadic boundary",
                """
                typedef int (*callback_t)(double fixed, ...);
                int wrong(double fixed) { return (int)fixed; }
                class Invoker { int Invoke(callback_t callback) { return 0; } };
                int main() { Invoker invoker; return invoker.Invoke(wrong); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            int identity(int value) { return value; }
            class Invoker {
              int Invoke(callback_t callback) { return callback(9); }
            };
            int main() {
              Invoker invoker;
              return invoker.Invoke(identity);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-method-mismatch-",
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

    def test_function_pointer_typedef_method_call_arity_recovers(self):
        cases = (
            (
                "too few",
                """
                typedef int (*callback_t)(int left, int right);
                class Invoker {
                  int Invoke(callback_t callback) { return callback(1); }
                };
                int main() { return 0; }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too many",
                """
                typedef int (*callback_t)(int value);
                class Invoker {
                  int Invoke(callback_t callback) { return callback(1, 2); }
                };
                int main() { return 0; }
                """,
                "function-pointer call has too many arguments",
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int left, int right);
            int add(int left, int right) { return left + right; }
            class Invoker {
              int Invoke(callback_t callback) { return callback(4, 5); }
            };
            int main() {
              Invoker invoker;
              return invoker.Invoke(add);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-method-arity-",
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

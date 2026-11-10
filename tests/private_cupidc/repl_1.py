"""Private compiler repl execution cases."""

from .harness import (KERNEL_LANG, Path, tempfile)

class Repl1Cases:
    def test_repl_rolls_back_raw_signature_after_unresolved_patch(self):
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
            prefix="private-cupidc-repl-raw-callback-patch-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int missing(void);",
                        "{ int (*poisoned)(double) = 0; missing(); }",
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

    def test_repl_rolls_back_nested_signature_and_data_patch_together(self):
        capacity_declarations = []
        for arity in range(31):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            capacity_declarations.append(
                f"int hold{arity}(int (*callback)({parameters})) {{ "
                "return callback != 0; }"
            )
        capacity_declarations.append(
            "void hold_active(void (*callback)(int, int)) { "
            "(void)callback; }"
        )

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-nested-callback-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "int later(int value); "
                        "int (*pending)(int) = later; "
                        "int (*poisoned)(int (*)(double)) = 0; "
                        "int broken(void) { return missing_value; }",
                        "\n".join(capacity_declarations),
                        "int observed; "
                        "void draw(int x, int y) { "
                        "observed = x * 10 + y; } "
                        "void install(int slot, void (*drawer)(int, int)) { "
                        "drawer(slot, 3); } "
                        "int main() { "
                        "void (*binding)(int, void (*)(int, int)) = install; "
                        "binding(4, draw); return observed; }",
                    ),
                    require_patch_and_raw_signature=True,
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertEqual(data_path.stat().st_size, 8)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                43,
                runtime.stdout + runtime.stderr,
            )

    def test_repl_adjacent_string_initializer_uses_the_full_literal(self):
        first = "e" * 700
        second = "f" * 700
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-string-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, data_path = self._compile(
                Path(temporary),
                f'char *text = "{first}" "{second}";',
                repl=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                result.stdout + result.stderr,
            )
            data = data_path.read_bytes()
        string_offset = int.from_bytes(data[:4], "little") - 0x01200000
        self.assertEqual(data[string_offset : string_offset + 700], b"e" * 700)
        self.assertEqual(
            data[string_offset + 700 : string_offset + 1400],
            b"f" * 700,
        )
        self.assertEqual(data[string_offset + 1400], 0)

    def test_repl_rolls_back_a_failed_definition_of_an_existing_tag(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = (
                self._compile_repl_after_struct_failure(
                    Path(temporary),
                    (
                        "struct Node;",
                        "typedef struct Node { int poisoned; };",
                        "typedef struct Node { int value; } Node;",
                        "Node recovered;",
                    ),
                )
            )
            data = data_path.read_bytes() if data_path.exists() else b""

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(data), 4)

        runtime_source = (KERNEL_LANG / "cupidc.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "cc_repl_checkpoint_structs(&repl_state);",
            runtime_source,
        )
        self.assertIn(
            "cc_repl_restore_structs(&repl_state);",
            runtime_source,
        )

    def test_repl_enum_respects_remaining_data_capacity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-enum-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile_repl(
                Path(temporary),
                (
                    "char padding[8388608];",
                    "enum ReplEdge { Value=1 };",
                ),
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("data section overflow", result.stderr)

    def test_decimal_recovery_does_not_replace_the_first_diagnostic(self):
        overlong = "1." + "0" * 94
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-decimal-first-error-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                f"""
                int first() {{
                  double value = 1e+;
                  return 0;
                }}
                int second() {{
                  double value = {overlong};
                  return 0;
                }}
                """,
            )

        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        public_errors = [
            line
            for line in result.stderr.splitlines()
            if line.startswith("CupidC Error")
        ]
        self.assertEqual(len(public_errors), 1, result.stderr)
        self.assertIn(
            "decimal floating literal exponent requires a digit",
            public_errors[0],
        )
        self.assertNotIn(
            "numeric literal exceeds 95 characters",
            public_errors[0],
        )
        self.assertNotIn("expected expression", public_errors[0])

"""Private compiler storage execution cases."""

from .harness import (Path, REPO_ROOT, tempfile)

class Storage1Cases:
    def test_installed_pointer_difference_demo_runs_in_jit_and_aot(self):
        source = (REPO_ROOT / "bin/feature23_full_access.cc").read_text(
            encoding="utf-8")
        start = source.index("I32 feature23_pointer_difference_probe() {")
        end = source.index("\nI32 feature23_initializer_probe()", start)
        self._assert_halfword_runtime(
            source[start:end] +
            "\nint main() { return !feature23_pointer_difference_probe(); }")

    def test_pointer_difference_uses_signed_element_counts(self):
        self._assert_halfword_runtime(r'''
            struct Triple { int a; int b; int c; };
            int calls;
            U16 ports[6];
            U16 *select_port(int index) { calls = calls + 1; return ports + index; }
            int main() {
                char bytes[6]; I16 halves[6]; int words[6];
                float floats[6]; double doubles[6]; struct Triple records[6];
                if (&bytes[5] - &bytes[1] != 4) return 1;
                if (&halves[1] - &halves[5] != -4) return 2;
                if (&words[5] - &words[1] != 4) return 3;
                if (&floats[1] - &floats[5] != -4) return 4;
                if (&doubles[5] - &doubles[1] != 4) return 5;
                if (&records[1] - &records[5] != -4) return 6;
                if (select_port(5) - select_port(1) != 4 || calls != 2) return 7;
                if (ports - ports != 0 || sizeof(ports - ports) != 4) return 8;
                if ((ports + 6) - ports != 6) return 9;
                if ((ports + 5) - (ports + 1) + 3 != 7) return 10;
                U16 rows[3][5]; unsigned int unsigned_words[6];
                if ((rows + 2) - rows != 2 || rows - (rows + 2) != -2) return 11;
                if (&unsigned_words[1] - &unsigned_words[5] != -4) return 12;
                return 0;
            }
        ''')

    def test_pointer_difference_keeps_repl_and_static_storage_types(self):
        result = self._compile_repl_and_run((
            'U16 ports[6]; double samples[6];',
            'U16 *end_port() { return ports + 6; }',
            'int span() { static I16 history[6]; return &history[1] - &history[5]; }',
            '''int main() {
                if (end_port() - ports != 6 || span() != -4) return 1;
                if (&samples[1] - &samples[5] != -4) return 2;
                if (sizeof(end_port() - ports) != 4) return 3;
                return 0;
            }''',
        ))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pointer_difference_rejects_incompatible_referents_and_recovers(self):
        cases = (
            'int a[2]; char b[2]; return a - b;',
            'U16 a[2]; I16 b[2]; return a - b;',
            'struct A a[2]; struct B b[2]; return a - b;',
            'int a[2][3]; int b[2][4]; return a - b;',
            'void *a; void *b; return a - b;',
        )
        for body in cases:
            for aot in (False, True):
                with self.subTest(body=body, aot=aot):
                    with tempfile.TemporaryDirectory(prefix='private-pointer-difference-') as temporary:
                        result, _code, _data = self._compile(
                            Path(temporary), 'struct A { int x; }; struct B { int x; }; int main() { ' + body + ' }', aot=aot)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('pointer difference requires', result.stdout + result.stderr)
            with tempfile.TemporaryDirectory(prefix='private-pointer-difference-recovery-') as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root, 'struct A { int x; }; struct B { int x; }; int main() { ' + body + ' }',
                    'int main() { U16 a[3]; return (&a[2] - &a[0]) - 2; }',
                    same_state=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                executed = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)

    def test_indirect_and_pointer_method_calls_share_the_mixed_width_layout(self):
        result = self._compile_and_run(
            """
            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

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
              int (*callback)(double first, int second) = combine;
              Probe probe;
              Probe *pointer = &probe;
              int indirect_result = callback(3.0, 4);
              int expression_result = pointer->Encode(1.0, 2, 3.0);
              pointer->Record(2.0, 3, 4.0);
              return indirect_result + expression_result + recorded;
            }
            """
        )
        self.assertEqual(result.returncode, 75, result.stdout + result.stderr)

    def test_cast_in_condition_does_not_erase_selected_object_pointer(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-cast-provenance-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int value;
                int target(int input) { return input; }

                int invalid_initializer() {
                  int (*callback)(int) =
                      (void *)target ? &value : (void *)target;
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(5) == 5 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_cast_inside_subscript_does_not_erase_selected_object_pointer(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-function-pointer-child-cast-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile_after_failure(
                Path(temporary),
                """
                int target(int value) { return value; }

                int invalid_initializer(void) {
                  char bytes[8];
                  int (*callback)(int) =
                      &bytes[sizeof((void *)target)];
                  return callback != 0;
                }
                """,
                """
                int identity(int value) { return value; }
                int main() {
                  int (*callback)(int) = identity;
                  return callback(4) == 4 ? 0 : 1;
                }
                """,
                same_state=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "function-pointer initializer requires a function, zero, or explicit pointer cast",
            result.stderr,
        )

    def test_struct_typedefs_keep_their_tag_and_member_layout(self):
        result = self._compile_and_run(
            """
            typedef struct TaggedPair {
              int left;
              int right;
            } TaggedPair;
            typedef TaggedPair PairAlias;
            typedef TaggedPair *TaggedPairPointer;

            typedef struct {
              int value;
            } AnonymousValue;

            typedef struct Node {
              int value;
              struct Node *next;
            } Node;

            int pair_sum(TaggedPairPointer pair) {
              return pair->left + pair->right;
            }

            int main() {
              PairAlias alias_value;
              struct TaggedPair tagged_value;
              AnonymousValue anonymous_value;
              Node first;
              Node second;
              alias_value.left = 7;
              alias_value.right = 11;
              tagged_value.left = 13;
              tagged_value.right = 17;
              anonymous_value.value = 19;
              first.value = 23;
              first.next = &second;
              second.value = 29;
              second.next = 0;
              if (pair_sum(&alias_value) != 18) return 1;
              if (pair_sum(&tagged_value) != 30) return 2;
              if (anonymous_value.value != 19) return 3;
              if (first.next->value != 29) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_tagged_struct_typedef_declares_multiple_value_and_pointer_aliases(self):
        result = self._compile_and_run(
            """
            typedef struct TaggedPair {
              int left;
              int right;
            } TaggedPair, PairAlias, *TaggedPairPointer;

            int pair_sum(TaggedPairPointer pair) {
              return pair->left + pair->right;
            }

            int main() {
              TaggedPair tagged;
              PairAlias alias;
              tagged.left = 5;
              tagged.right = 7;
              alias.left = 11;
              alias.right = 13;
              if (pair_sum(&tagged) != 12) return 1;
              if (pair_sum(&alias) != 24) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_struct_typedef_keeps_each_declarator_pointer_depth(self):
        result = self._compile_and_run(
            """
            typedef struct ReversePair {
              int left;
              int right;
            } *ReversePairPointer, ReversePairValue;

            int main() {
              ReversePairValue value;
              ReversePairPointer pointer = &value;
              value.left = 17;
              value.right = 19;
              if (pointer->left != 17) return 1;
              if (pointer->right != 19) return 2;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_allocates_and_indexes_an_automatic_object(self):
        result = self._compile_and_run(
            """
            typedef int IntTriple[3];

            int main() {
              IntTriple values, others;
              values[0] = 29;
              values[1] = 31;
              values[2] = 37;
              others[0] = 41;
              others[1] = 43;
              others[2] = 47;
              if (sizeof(values) != 12) return 1;
              if (sizeof(others) != 12) return 2;
              if (values[0] + values[1] + values[2] != 97) return 3;
              return others[0] + others[1] + others[2] == 131 ? 0 : 4;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_sizeof_fixed_array_typedef_reports_the_complete_type_size(self):
        result = self._compile_and_run(
            """
            typedef char SevenBytes[7];
            typedef struct Word { int value; } TwoWords[2];

            int main() {
              SevenBytes bytes;
              if (sizeof(SevenBytes) != 7) return 1;
              if (sizeof(TwoWords) != 8) return 2;
              if (sizeof(bytes) != 7) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_struct_array_typedef_allocates_a_complete_global_object(self):
        result = self._compile_and_run(
            """
            typedef struct Cell {
              int value;
            } CellArray[2], Cell;

            CellArray cells;

            int main() {
              Cell scalar;
              cells[0].value = 41;
              cells[1].value = 43;
              scalar.value = 47;
              if (sizeof(cells) != 8) return 1;
              if (cells[0].value + cells[1].value != 84) return 2;
              return scalar.value == 47 ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_keeps_its_shape_in_a_struct_field(self):
        result = self._compile_and_run(
            """
            typedef int IntTriple[3];
            typedef struct Holder {
              int leading;
              IntTriple values;
              int trailing;
            } Holder;

            int main() {
              Holder holder;
              holder.leading = 53;
              holder.values[0] = 59;
              holder.values[1] = 61;
              holder.values[2] = 67;
              holder.trailing = 71;
              if (sizeof(holder) != 20) return 1;
              if (holder.leading != 53 || holder.trailing != 71) return 2;
              return holder.values[0] + holder.values[1] +
                         holder.values[2] == 187 ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_record_members_keep_complete_element_types(self):
        result = self._compile_and_run(
            """
            typedef int Pair[2];
            typedef struct Word {
              int value;
            } Words[2], Word;
            typedef struct Holder {
              Pair values;
              Words words;
            } Holder;

            Holder holders[2];

            int main() {
              Holder holder;
              Holder *pointer = &holder;
              Holder *array_pointer = &holders[0];

              holder.values[0] = 11;
              pointer->values[1] = 13;
              holder.words[0].value = 17;
              pointer->words[1].value = 19;
              holders[1].words[0].value = 23;
              array_pointer[1].words[1].value = 29;

              if (sizeof(holder.values) != 8) return 1;
              if (sizeof(pointer->values) != 8) return 2;
              if (sizeof(holder.words) != 8) return 3;
              if (sizeof(holder.words[0]) != 4) return 4;
              if (sizeof(pointer->words[1]) != 4) return 5;
              if (holder.values[0] != 11 || pointer->values[1] != 13)
                return 6;
              if (holder.words[0].value != 17 ||
                  pointer->words[1].value != 19)
                return 7;
              if (holders[1].words[0].value != 23 ||
                  array_pointer[1].words[1].value != 29)
                return 8;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_keeps_its_shape_in_a_standalone_struct(self):
        result = self._compile_and_run(
            """
            typedef char BytePair[2];
            struct Envelope {
              int leading;
              BytePair bytes;
              int trailing;
            };

            int main() {
              struct Envelope envelope;
              envelope.leading = 107;
              envelope.bytes[0] = 'O';
              envelope.bytes[1] = 'S';
              envelope.trailing = 109;
              if (sizeof(envelope) != 12) return 1;
              if (envelope.leading != 107 || envelope.trailing != 109) {
                return 2;
              }
              return envelope.bytes[0] == 'O' &&
                     envelope.bytes[1] == 'S' ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_keeps_its_shape_in_a_class_field(self):
        result = self._compile_and_run(
            """
            typedef int IntPair[2];
            class Packet {
              int leading;
              IntPair values;
              int trailing;
            };

            int main() {
              Packet packet;
              packet.leading = 127;
              packet.values[0] = 131;
              packet.values[1] = 137;
              packet.trailing = 139;
              if (sizeof(packet) != 16) return 1;
              if (packet.leading != 127 || packet.trailing != 139) return 2;
              return packet.values[0] + packet.values[1] == 268 ? 0 : 3;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_alias_chain_allocates_block_static_storage(self):
        result = self._compile_and_run(
            """
            typedef int IntPair[2];
            typedef IntPair PairAlias;

            int pair_sum() {
              static PairAlias values;
              values[0] = 73;
              values[1] = 79;
              if (sizeof(values) != 8) return -1;
              return values[0] + values[1];
            }

            int main() {
              return pair_sum() == 152 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_parameter_decays_to_an_element_pointer(self):
        result = self._compile_and_run(
            """
            typedef int IntTriple[3];

            int sum_triple(IntTriple values) {
              return values[0] + values[1] + values[2];
            }

            int main() {
              IntTriple values;
              values[0] = 83;
              values[1] = 89;
              values[2] = 97;
              return sum_triple(values) == 269 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_array_typedef_method_parameter_uses_the_same_decay(self):
        result = self._compile_and_run(
            """
            typedef int IntPair[2];

            class Accumulator {
              int Sum(IntPair values) {
                return values[0] + values[1];
              }
            };

            int main() {
              IntPair values;
              Accumulator accumulator;
              values[0] = 101;
              values[1] = 103;
              return accumulator.Sum(values) == 204 ? 0 : 1;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_record_fields_keeps_the_selected_storage(self):
        result = self._compile_and_run(
            """
            typedef struct TargetRef {
              int leading;
              int key_offset;
              int key_length;
              int trailing;
            } TargetRef;

            int write_key(int *offset, int *length) {
              *offset = 37;
              *length = 41;
              return 0;
            }

            int main() {
              TargetRef target;
              TargetRef *pointer = &target;
              target.leading = 11;
              target.trailing = 13;
              write_key(&target.key_offset, &target.key_length);
              if (target.key_offset != 37 || target.key_length != 41) return 1;
              write_key(&pointer->key_offset, &pointer->key_length);
              if (pointer->key_offset != 37 || pointer->key_length != 41) {
                return 2;
              }
              if (target.leading != 11 || target.trailing != 13) return 3;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_record_field_reports_an_unknown_member(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-record-field-address-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                """
                struct TargetRef { int key_offset; };
                int main() {
                  struct TargetRef target;
                  struct TargetRef *pointer = &target;
                  int *missing = &pointer->missing;
                  return missing != 0;
                }
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("unknown struct field", result.stderr)

    def test_repl_keeps_a_tagged_struct_typedef_for_later_lines(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-typedef-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile_repl(
                Path(temporary),
                (
                    """
                    typedef struct ReplPair {
                      int left;
                      int right;
                    } ReplPair;
                    """,
                    "ReplPair pair;",
                ),
            )
            data = data_path.read_bytes() if data_path.exists() else b""
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(len(data), 8)

    def test_repl_keeps_multiple_anonymous_struct_typedef_aliases(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-multiple-typedef-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, data_path = self._compile_repl(
                Path(temporary),
                (
                    """
                    typedef struct {
                      int left;
                      int right;
                    } ReplPair, PairAlias, *ReplPairPointer;
                    """,
                    "ReplPair first;",
                    "PairAlias second;",
                    "ReplPairPointer pointer;",
                ),
            )
            data = data_path.read_bytes() if data_path.exists() else b""

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(data), 20)

    def test_repl_keeps_fixed_array_typedef_shape_for_later_units(self):
        result = self._compile_repl_and_run(
            (
                "typedef char ByteBlock[5];",
                "ByteBlock bytes;",
                """
                int main() {
                  bytes[0] = 'C';
                  bytes[4] = 'd';
                  if (sizeof(bytes) != 5) return 1;
                  if (bytes[0] != 'C') return 2;
                  return bytes[4] == 'd' ? 0 : 3;
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_repl_struct_field_keeps_a_fixed_array_typedef_shape(self):
        result = self._compile_repl_and_run(
            (
                "typedef int ReplPair[2];",
                "struct ReplHolder { int leading; ReplPair values; "
                "int trailing; };",
                "struct ReplHolder holder;",
                """
                int main() {
                  holder.leading = 149;
                  holder.values[0] = 151;
                  holder.values[1] = 157;
                  holder.trailing = 163;
                  if (sizeof(holder) != 16) return 1;
                  if (holder.leading != 149 || holder.trailing != 163) {
                    return 2;
                  }
                  return holder.values[0] + holder.values[1] == 308 ? 0 : 3;
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_tagged_struct_typedef_requires_an_alias(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-struct-typedef-alias-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                "typedef struct MissingAlias { int value; };",
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("expected typedef alias name", result.stderr)

    def test_fixed_array_typedefs_report_invalid_declarators_and_recover(self):
        cases = (
            (
                "zero",
                "typedef int Empty[0];",
                "array size must be positive",
            ),
            (
                "unsized",
                "typedef int Pending[];",
                "typedef array size is required",
            ),
            (
                "overflow",
                "typedef int Huge[1073741824];",
                "array allocation size overflow",
            ),
            (
                "incomplete-struct",
                "struct Pending; typedef struct Pending PendingArray[2];",
                "array of incomplete struct type",
            ),
            (
                "multidimensional",
                "typedef int Matrix[2][3];",
                "multidimensional typedef arrays are not supported",
            ),
            (
                "pointer-to-array",
                "typedef int Pair[2]; typedef Pair *PairPointer;",
                "pointer to typedef array is not supported",
            ),
            (
                "additional-dimension",
                "typedef int Pair[2]; Pair matrix[3];",
                "array declarator after typedef array is not supported",
            ),
        )

        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-array-typedef-{name}-",
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
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn("unexpected token", result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-array-typedef-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            recovery, _code_path, _data_path = self._compile_after_failure(
                Path(temporary),
                "typedef int Broken[0];",
                """
                typedef int Pair[2];
                int main() {
                  Pair values;
                  values[0] = 167;
                  values[1] = 173;
                  return values[0] + values[1] == 340 ? 0 : 1;
                }
                """,
            )
        self.assertEqual(
            recovery.returncode,
            0,
            recovery.stdout + recovery.stderr,
        )

    def test_fixed_array_typedefs_reject_unrepresented_type_uses(self):
        cases = (
            (
                "function-return",
                "typedef int Pair[2]; Pair broken() { return 0; }",
                "function return type cannot be an array",
            ),
            (
                "struct-function-return",
                "typedef struct Word { int value; } Words[2]; "
                "Words broken() { return 0; }",
                "function return type cannot be an array",
            ),
            (
                "method-return",
                "typedef int Pair[2]; "
                "class Broken { Pair Value() { return 0; } };",
                "method return type cannot be an array",
            ),
            (
                "cast-target",
                "typedef int Pair[2]; int main() { return (Pair)7; }",
                "cast target cannot be an array type",
            ),
            (
                "new-target",
                "typedef int Pair[2]; int main() { return new Pair; }",
                "new does not support typedef array types",
            ),
        )

        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-array-typedef-use-{name}-",
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
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn("unexpected token", result.stderr)

    def test_tagged_struct_typedef_rejects_an_incomplete_value_field(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-struct-typedef-incomplete-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                """
                typedef struct Broken {
                  struct Pending value;
                } Broken;
                """,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("field has incomplete struct type", result.stderr)
        self.assertNotIn("expected typedef alias name", result.stderr)

    def test_struct_declarations_reject_invalid_field_array_layouts(self):
        cases = (
            (
                "tagged-zero",
                "typedef struct Bad { int values[0]; } Bad;",
                "array size must be positive",
            ),
            (
                "anonymous-negative",
                "typedef struct { int values[-1]; } Bad;",
                "array size must be positive",
            ),
            (
                "tagged-overflow",
                "typedef struct Bad { int values[1073741824]; } Bad;",
                "array allocation size overflow",
            ),
            (
                "tagged-total-overflow",
                "typedef struct Bad { char a[2147483644]; char b[4]; } Bad;",
                "record size overflow",
            ),
            (
                "tagged-allocation-alignment-overflow",
                "typedef struct Bad { char a[2147483644]; char b[3]; } Bad; "
                "Bad value;",
                "record size overflow",
            ),
            (
                "anonymous-allocation-alignment-overflow",
                "typedef struct { char a[2147483644]; char b[3]; } Bad; "
                "Bad value;",
                "record size overflow",
            ),
            (
                "standalone-total-overflow",
                "struct Bad { char a[2147483644]; char b[4]; };",
                "record size overflow",
            ),
            (
                "standalone-local-alignment-overflow",
                "struct Bad { char a[2147483644]; char b[3]; }; "
                "int main() { struct Bad value; return 0; }",
                "record size overflow",
            ),
            (
                "class-alignment-overflow",
                "class Bad { char a[2147483644]; char b[3]; };",
                "record size overflow",
            ),
        )
        for name, source, diagnostic in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory(
                prefix=f"private-cupidc-struct-array-{name}-",
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
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn("expected typedef alias name", result.stderr)
                self.assertNotIn("unexpected token", result.stderr)
                self.assertNotIn("[cupidc] Defined", result.stderr)

    def test_repl_struct_rejects_cumulative_field_size_overflow(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-overflow-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile(
                Path(temporary),
                "struct Bad { char a[2147483644]; char b[3]; }; "
                "struct Bad value;",
                repl=True,
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("record size overflow", result.stderr)

    def test_repl_struct_global_respects_remaining_data_capacity(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-repl-struct-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code_path, _data_path = self._compile_repl(
                Path(temporary),
                (
                    "char padding[8388600];",
                    "struct ReplWide { int words[4]; };",
                    "struct ReplWide value;",
                ),
            )
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("data section overflow", result.stderr)

    def test_array_bound_expression_overflow_is_rejected_in_every_lane(self):
        cases = (
            (
                "record field",
                "struct Bad { char values[65536*65536+1]; };",
                False,
            ),
            ("global", "char values[65536*65536+1];", False),
            (
                "local",
                "int main() { char values[65536*65536+1]; return 0; }",
                False,
            ),
            ("REPL global", "char values[65536*65536+1];", True),
        )
        for label, source, repl in cases:
            with self.subTest(lane=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-const-expression-lane-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code_path, _data_path = self._compile(
                    Path(temporary), source, repl=repl
                )
                self.assertEqual(
                    result.returncode,
                    2,
                    result.stdout + result.stderr,
                )
                self.assertIn(
                    "constant integer expression overflow", result.stderr
                )

    def test_indexed_record_updates_evaluate_each_index_once_in_both_modes(
        self,
    ):
        source = """
            struct Reading {
              float single;
              double wide;
            };

            struct Reading records[2];
            int index_calls;

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              records[1].single = -0.0f;
              records[1].wide = 6.25;

              float new_single = ++records[next_index()].single;
              if (index_calls != 1) return 1;
              index_calls = 0;
              double old_wide = records[next_index()].wide--;
              if (index_calls != 1) return 2;

              if (new_single != 1.0f) return 3;
              if (records[1].single != 1.0f) return 4;
              if (old_wide != 6.25 || records[1].wide != 5.25) return 5;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(mode="aot" if aot else "jit"):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )

    def test_incomplete_struct_pointer_update_has_a_useful_diagnostic(self):
        cases = (
            "int main() { struct Pending *pointer; pointer++; return 0; }",
            "int main() { struct Pending *pointer; --pointer; return 0; }",
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-incomplete-struct-pointer-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "struct pointer update requires a complete pointed-to type",
                result.stderr,
            )

    def test_function_returned_int_pointer_drops_stale_array_metadata(self):
        result = self._compile_and_run(
            """
            int numbers[3];
            double floating_values[1];

            int *numbers_view() {
              return numbers;
            }

            int main() {
              numbers[0] = 11;
              numbers[1] = 77;
              numbers[2] = 22;
              floating_values[0] = 3.5;
              floating_values;
              if (numbers_view()[1] != 77) return 1;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_program_array_bounds_reject_nonpositive_sizes(self):
        cases = (
            ("global zero", "double values[0];"),
            ("global negative", "float values[1 - 2];"),
            ("global SIMD zero", "float4 values[0];"),
            ("global SIMD zero inner bound", "float4 values[2][0];"),
            (
                "local zero inner bound",
                "int main() { double values[2][0]; return 0; }",
            ),
            (
                "local negative",
                "int main() { float values[1 - 2]; return 0; }",
            ),
            (
                "block static zero",
                "int main() { static double values[0]; return 0; }",
            ),
            (
                "block static negative inner bound",
                "int main() { static float values[2][1 - 2]; return 0; }",
            ),
            (
                "global zero third bound",
                "double values[2][2][0];",
            ),
            (
                "local negative third bound",
                "int main() { float values[2][2][1 - 2]; return 0; }",
            ),
            (
                "local SIMD negative third bound",
                "int main() { double2 values[2][2][1 - 2]; return 0; }",
            ),
        )

        for label, source in cases:
            with self.subTest(storage=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-array-bound-",
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
                self.assertIn("array size must be positive", result.stderr)

    def test_array_byte_calculation_rejects_signed_overflow(self):
        cases = (
            ("global double", "double values[268435456];"),
            ("global SIMD", "double2 values[134217728];"),
            (
                "global two dimensional SIMD",
                "float4 values[32768][4096];",
            ),
            (
                "block static three dimensional SIMD",
                "int main() { static double2 values[4096][4096][8]; return 0; }",
            ),
            (
                "local float",
                "int main() { float values[536870912]; return 0; }",
            ),
            (
                "block static char alignment",
                "int main() { static char values[2147483647]; return 0; }",
            ),
            ("global two dimensional int", "int values[32768][32768];"),
            (
                "global three dimensional double",
                "double values[32768][32768][2];",
            ),
            (
                "block static three dimensional float",
                "int main() { static float values[32768][32768][2]; return 0; }",
            ),
        )

        for label, source in cases:
            with self.subTest(storage=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-array-overflow-",
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
                    "array allocation size overflow", result.stderr
                )

    def test_unevaluated_subscript_sizeof_keeps_remaining_row_size(self):
        result = self._compile_and_run(
            """
            int index_calls;
            float matrix[2][3];
            double cube[2][3][4];

            int next_index() {
              index_calls += 1;
              return 1;
            }

            int main() {
              if (sizeof(matrix[next_index()]) != 12) return 1;
              if (sizeof(cube[next_index()]) != 96) return 2;
              if (sizeof(cube[0][next_index()]) != 32) return 3;
              if (sizeof(matrix[0] == matrix[1]) != 4) return 4;
              if (sizeof(1 ? matrix[0] : matrix[1]) != 4) return 5;
              if (index_calls != 0) return 6;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_record_array_element_keeps_the_selected_object(self):
        result = self._compile_and_run(
            """
            struct Reading {
              float gain;
              double bias;
              float taps[3];
            };

            struct Reading readings[2];

            int main() {
              struct Reading *selected = &readings[1];
              readings[1].gain = 1.5f;
              readings[1].bias = 2.25;
              readings[1].taps[2] = 3.25f;
              selected->gain += 0.5f;
              selected->bias *= 2.0;
              if (selected != &readings[1]) return 1;
              if (selected->gain != 2.0f) return 2;
              if (selected->bias != 4.5) return 3;
              if (selected->taps[2] != 3.25f) return 4;
              return 0;
            }
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_address_of_array_row_has_a_useful_diagnostic(self):
        cases = (
            "int main() { float values[2][3]; float *row = &values[1]; }",
            (
                "int main() { double values[2][3][4]; "
                "double *row = &values[1][2]; }"
            ),
        )
        for source in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory(
                prefix="private-cupidc-array-row-address-",
                ignore_cleanup_errors=True,
            ) as temporary:
                result, _code, _data = self._compile(Path(temporary), source)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn(
                "address of an array row is not supported", result.stderr
            )

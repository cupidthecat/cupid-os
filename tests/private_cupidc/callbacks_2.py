"""Private compiler callbacks execution cases."""

from .harness import (Path, REPO_ROOT, tempfile)

class Callbacks2Cases:
    def test_function_pointer_typedef_automatic_and_method_later_mismatches_recover(self):
        cases = (
            (
                "automatic local",
                """
                typedef int (*callback_t)(double value);
                int main() {
                  callback_t callback = later;
                  return callback(6);
                }
                int later(int value) { return value; }
                """,
            ),
            (
                "method parameter",
                """
                typedef int (*callback_t)(double value);
                class Invoker {
                  int Invoke(callback_t callback) { return callback(6); }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(later);
                }
                int later(int value) { return value; }
                """,
            ),
        )
        retry_source = """
            typedef int (*callback_t)(int value);
            int later(int value) { return value; }
            int main() {
              callback_t callback = later;
              return callback(9);
            }
        """
        for label, failing_source in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-typedef-later-mismatch-",
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
                    "function definition does not match prior "
                    "function-pointer initializer",
                    result.stderr,
                )
                runtime = self._run_i386(root, int(result.stdout.strip()))
                self.assertEqual(
                    runtime.returncode, 9, runtime.stdout + runtime.stderr
                )

    def test_failed_method_does_not_leak_callback_typedef_metadata(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-callback-typedef-method-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                """
                typedef int (*double_cb)(double value);
                class Invoker {
                  int Invoke(double_cb callback) {
                    return callback(4) + missing_value;
                  }
                };
                """,
                """
                typedef int (*integer_cb)(int value);
                int identity(int value) { return value; }
                class Invoker {
                  int Invoke(integer_cb callback) { return callback(9); }
                };
                int main() {
                  Invoker invoker;
                  return invoker.Invoke(identity);
                }
                """,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("undefined variable", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_signature_erased_callback_alias_chain_keeps_legacy_initializer(self):
        source = """
        typedef double (*base_callback_t)(double);
        typedef base_callback_t alias_callback_t;

        double identity(double value) { return value; }

        int main() {
          alias_callback_t callback = identity;
          return callback != 0;
        }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 1, result.stdout + result.stderr
                )

    def test_active_usb_callbacks_use_typedef_automatic_locals(self):
        header = (REPO_ROOT / "kernel" / "usb" / "usb_hc.h").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "typedef void (*usb_complete_cb_t)(int status, usb_transfer_t *);",
            header,
        )

        for relative_path, poll_name in (
            ("uhci.cc", "uhci_poll_interrupts"),
            ("ehci.cc", "ehci_poll_interrupts"),
        ):
            with self.subTest(source=relative_path):
                source = (
                    REPO_ROOT / "kernel" / "usb" / relative_path
                ).read_text(encoding="utf-8")
                self.assertIn(f"void {poll_name}(void) {{", source)
                self.assertIn("usb_complete_cb_t cb;", source)
                self.assertIn("c->interrupt_slots[i].cb = NULL;", source)
                self.assertIn("slot->cb = cb;", source)
                self.assertIn("usb_complete_cb_t cb = NULL;", source)
                self.assertIn("cb = slot->cb;", source)
                self.assertRegex(source, r"if \(deliver\)\s+cb\(0, &local\);")

    def test_active_callback_fields_use_direct_postfix_calls(self):
        timer_header = (REPO_ROOT / "drivers" / "timer.h").read_text(
            encoding="utf-8"
        )
        timer_source = (REPO_ROOT / "drivers" / "timer.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "typedef void (*timer_callback_t)(struct registers*, "
            "uint32_t channel);",
            timer_header,
        )
        self.assertIn(
            "timer_channels[i].callback(r, (uint32_t)i);",
            timer_source,
        )

        gui_header = (
            REPO_ROOT / "kernel" / "gui" / "gui_events.h"
        ).read_text(encoding="utf-8")
        gui_source = (
            REPO_ROOT / "kernel" / "gui" / "gui_events.cc"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "typedef void (*ui_event_callback_t)(ui_event_t *event, "
            "void *context);",
            gui_header,
        )
        self.assertIn("h->callback(ev, h->context);", gui_source)

    def test_typedef_callback_field_store_copy_and_call_run_in_jit_and_aot(self):
        source = """
            struct usb_transfer { int marker; };
            typedef struct usb_transfer usb_transfer_t;
            typedef void (*usb_complete_cb_t)(int status, usb_transfer_t *);

            typedef struct {
              usb_complete_cb_t cb;
            } interrupt_slot_t;

            typedef struct {
              interrupt_slot_t interrupt_slots[2];
            } controller_t;

            class completion_box_t {
              usb_complete_cb_t cb;
            };

            int observed;

            void complete(int status, usb_transfer_t *transfer) {
              observed = status + transfer->marker;
            }

            int deliver(controller_t *controller, int index,
                        usb_complete_cb_t supplied,
                        usb_transfer_t *transfer) {
              usb_complete_cb_t cb = 0;
              controller->interrupt_slots[index].cb = supplied;
              cb = controller->interrupt_slots[index].cb;
              controller->interrupt_slots[index].cb = 0;
              if (controller->interrupt_slots[index].cb != 0) return 1;
              cb(7, transfer);
              return observed;
            }

            int prepare_box(completion_box_t *box,
                            usb_complete_cb_t supplied) {
              usb_complete_cb_t copied = 0;
              box->cb = supplied;
              copied = box->cb;
              box->cb = 0;
              return copied != 0 && box->cb == 0;
            }

            int main() {
              controller_t controller;
              completion_box_t box;
              usb_transfer_t transfer;
              transfer.marker = 9;
              if (!prepare_box(&box, complete)) return 2;
              return deliver(&controller, 1, complete, &transfer);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_typedef_callback_field_postfix_call_runs_in_jit_and_aot(self):
        source = """
            struct Entry { int marker; };
            typedef int (*callback_t)(struct Entry *, double, int);

            struct Slot { callback_t callback; };
            struct Controller { struct Slot slots[2]; };

            int controller_calls;
            int index_calls;

            struct Controller *select_controller(
                    struct Controller *controller) {
              controller_calls++;
              return controller;
            }

            int select_slot(void) {
              index_calls++;
              return 1;
            }

            int target(struct Entry *entry, double value, int tag) {
              return entry->marker + (int)(value * 10.0) + tag;
            }

            int invoke(struct Controller *controller, struct Entry *entry) {
              return select_controller(controller)->slots[
                  select_slot()].callback(
                  entry, 3, 4.75f);
            }

            int main() {
              struct Controller controller;
              struct Entry entry;
              int result;
              entry.marker = 9;
              controller.slots[1].callback = target;
              result = invoke(&controller, &entry);
              if (controller_calls != 1 || index_calls != 1) return 1;
              return result;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 43, result.stdout + result.stderr
                )

    def test_callback_field_postfix_call_wins_over_same_named_method(self):
        source = """
            typedef int (*callback_t)(int);

            int add_one(int value) {
              return value + 1;
            }

            class Holder {
              callback_t Invoke;

              int Invoke(int value) {
                return value + 40;
              }
            };

            int main() {
              Holder holder;
              holder.Invoke = add_one;
              return holder.Invoke(8);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 9, result.stdout + result.stderr
                )

    def test_raw_callback_struct_field_runs_in_jit_and_aot(self):
        source = """
            struct Entry { int value; };

            struct Holder {
              int (*callback)(struct Entry *, int);
            };

            int add(struct Entry *entry, int amount) {
              return entry->value + amount;
            }

            int main() {
              struct Entry entry;
              struct Holder holder;
              int (*copied)(struct Entry *, int) = 0;
              entry.value = 9;
              holder.callback = add;
              copied = holder.callback;
              holder.callback = 0;
              if (holder.callback != 0) return 1;
              return copied(&entry, 7);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_raw_callback_class_field_runs_in_jit_and_aot(self):
        source = """
            class Holder {
              int (*callback)(int, double, int);
            };

            int add(int left, double middle, int right) {
              return left + (int)middle + right;
            }

            int invoke(Holder *holder,
                       int (*supplied)(int, double, int)) {
              int (*copied)(int, double, int) = 0;
              holder->callback = supplied;
              copied = holder->callback;
              holder->callback = 0;
              if (holder->callback != 0) return 1;
              return copied(7, 5.0, 4);
            }

            int main() {
              Holder holder;
              return invoke(&holder, add);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_raw_callback_field_postfix_call_keeps_simd_result(self):
        source = """
            struct Holder {
              float4 (*callback)(float4, double, float4);
            };

            float4 blend(float4 left, double marker, float4 right) {
              if (marker != 9.0) return left;
              return left + right;
            }

            int main() {
              struct Holder holder;
              float4 left = {1.0f, 2.0f, 3.0f, 4.0f};
              float4 right = {5.0f, 6.0f, 7.0f, 8.0f};
              float4 result;
              holder.callback = blend;
              result = holder.callback(left, 9, right);
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

    def test_raw_variadic_callback_field_postfix_call_promotes_tail(self):
        source = """
            struct Holder {
              int (*callback)(double, ...);
            };

            int inspect(double fixed, ...) {
              return (int)fixed;
            }

            int main() {
              struct Holder holder;
              holder.callback = inspect;
              return holder.callback(6, 2.5f);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 6, result.stdout + result.stderr
                )

    def test_raw_callback_typedef_struct_fields_run_in_jit_and_aot(self):
        source = """
            typedef struct {
              void *context;
              int (*allocate)(void *, int);
              void (*release)(void *, int);
            } allocator_t;

            int released;

            int allocate_value(void *context, int amount) {
              return *(int *)context + amount;
            }

            void release_value(void *context, int amount) {
              released = *(int *)context - amount;
            }

            int main() {
              int base = 11;
              allocator_t allocator;
              int (*allocate)(void *, int) = 0;
              void (*release)(void *, int) = 0;
              allocator.context = &base;
              allocator.allocate = allocate_value;
              allocator.release = release_value;
              allocate = allocator.allocate;
              release = allocator.release;
              release(allocator.context, 4);
              return allocate(allocator.context, 2) + released - 4;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 16, result.stdout + result.stderr
                )

    def test_typedef_callback_field_arrays_run_in_jit_and_aot(self):
        source = """
            typedef int (*callback_t)(double first, int second);

            struct Holder {
              callback_t callbacks[2];
            };

            int selected;
            int stored;
            int observed;

            int select_slot(void) {
              selected++;
              return 1;
            }

            int select_store(void) {
              stored++;
              return 0;
            }

            int combine(double first, int second) {
              observed = (int)first * 10 + second;
              return observed;
            }

            int subtract(double first, int second) {
              return (int)first * 10 - second;
            }

            int main(void) {
              struct Holder holder;
              callback_t copied = 0;
              holder.callbacks[select_store()] = combine;
              holder.callbacks[1] = subtract;
              if (stored != 1) return 1;
              holder.callbacks[0](2, 5);
              if (observed != 25) return 2;
              copied = holder.callbacks[0];
              if (copied(3, 4) != 34) return 3;
              if (holder.callbacks[select_slot()](5, 2) != 48) return 4;
              if (selected != 1) return 5;
              return 83;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 83, result.stdout + result.stderr
                )

    def test_typedef_callback_field_arrays_cover_other_record_paths(self):
        records = (
            (
                "class",
                "class Holder { callback_t callbacks[2]; };",
                "Holder",
            ),
            (
                "anonymous-typedef",
                "typedef struct { callback_t callbacks[2]; } Holder;",
                "Holder",
            ),
        )
        for label, declaration, holder_type in records:
            source = f"""
                typedef int (*callback_t)(int value);
                {declaration}

                int plus_one(int value) {{ return value + 1; }}
                int minus_two(int value) {{ return value - 2; }}

                int main(void) {{
                  {holder_type} holder;
                  holder.callbacks[0] = plus_one;
                  holder.callbacks[1] = minus_two;
                  return holder.callbacks[0](4) + holder.callbacks[1](9);
                }}
            """
            for aot in (False, True):
                with self.subTest(record=label, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual(
                        result.returncode, 12, result.stdout + result.stderr
                    )

        repl_result = self._compile_repl_and_run(
            (
                "typedef int (*callback_t)(int value);",
                "struct Holder { callback_t callbacks[2]; };",
                "int plus_one(int value) { return value + 1; }",
                "int minus_two(int value) { return value - 2; }",
                "struct Holder holder;",
                """
                int main(void) {
                  holder.callbacks[0] = plus_one;
                  holder.callbacks[1] = minus_two;
                  return holder.callbacks[0](4) + holder.callbacks[1](9);
                }
                """,
            )
        )
        self.assertEqual(
            repl_result.returncode,
            12,
            repl_result.stdout + repl_result.stderr,
        )

    def test_typedef_callback_field_arrays_retain_variadic_return_abi(self):
        source = """
            typedef double (*callback_t)(double, ...);

            struct Holder {
              callback_t callbacks[2];
            };

            double keep(double value, ...) {
              return value + 0.5;
            }

            int main(void) {
              struct Holder holder;
              holder.callbacks[0] = keep;
              return (int)holder.callbacks[0](8.5, 1.25f, 3);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 9, result.stdout + result.stderr
                )

    def test_doom_wipe_callback_table_keeps_active_raw_array_shape(self):
        source = (
            REPO_ROOT / "kernel" / "doom" / "src" / "f_wipe.cc"
        ).read_text(encoding="utf-8")

        for fragment in (
            "static int (*wipes[])(int, int, int)",
            "(*wipes[wipeno * 3])(width, height, ticks);",
            "(*wipes[wipeno * 3 + 1])(width, height, ticks);",
            "(*wipes[wipeno * 3 + 2])(width, height, ticks);",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, source)

    def test_block_static_raw_callback_arrays_run_in_jit_and_aot(self):
        source = """
            int wipe_zero_start(int width, int height, int ticks) {
              return 100 + width + height + ticks;
            }

            int wipe_zero_tick(int width, int height, int ticks) {
              return 200 + width + height + ticks;
            }

            int wipe_zero_end(int width, int height, int ticks) {
              return 300 + width + height + ticks;
            }

            int wipe_one_start(int width, int height, int ticks) {
              return 400 + width + height + ticks;
            }

            int wipe_one_tick(int width, int height, int ticks);

            int wipe_one_end(int width, int height, int ticks) {
              return 600 + width + height + ticks;
            }

            int replacement(int width, int height, int ticks) {
              return 900 + width + height + ticks;
            }

            int dispatch(int wipeno, int phase, int replace) {
              static int (*wipes[])(int, int, int) = {
                wipe_zero_start, wipe_zero_tick, wipe_zero_end,
                wipe_one_start, wipe_one_tick, wipe_one_end
              };
              if (replace) wipes[wipeno*3+1] = replacement;
              if (phase == 0)
                return (*wipes[wipeno*3])(4, 2, 1);
              if (phase == 1)
                return (*wipes[wipeno*3+1])(4, 2, 1);
              return (*wipes[wipeno*3+2])(4, 2, 1);
            }

            int wipe_one_tick(int width, int height, int ticks) {
              return 500 + width + height + ticks;
            }

            int main(void) {
              if (dispatch(0, 0, 0) != 107) return 1;
              if (dispatch(0, 1, 0) != 207) return 2;
              if (dispatch(0, 2, 0) != 307) return 3;
              if (dispatch(1, 0, 0) != 407) return 4;
              if (dispatch(1, 1, 0) != 507) return 5;
              if (dispatch(1, 2, 0) != 607) return 6;
              if (dispatch(1, 1, 1) != 907) return 7;
              if (dispatch(1, 1, 0) != 907) return 8;
              return 83;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 83, result.stdout + result.stderr
                )

    def test_block_static_raw_callback_scalar_runs_in_jit_and_aot(self):
        source = """
            int later_target(int value);
            int replacement(int value) { return value + 9; }

            int dispatch(int replace) {
              static int (*callback)(int) = later_target;
              if (replace) callback = replacement;
              return callback(9);
            }

            int later_target(int value) { return value + 1; }

            int main(void) {
              if (dispatch(0) != 10) return 1;
              if (dispatch(1) != 18) return 2;
              if (dispatch(0) != 18) return 3;
              return 85;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 85, result.stdout + result.stderr
                )

    def test_automatic_raw_callback_arrays_run_in_jit_and_aot(self):
        source = """
            int index_calls;

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int later(double first, int second);

            int replacement(double first, int second) {
              return (int)first * 20 + second;
            }

            int next_index(void) {
              index_calls += 1;
              return 1;
            }

            int dispatch(int replace) {
              int (*callbacks[3])(double, int) = { combine, later };
              int (*zeroed[1])(double, int);
              int (*copied)(double, int) = callbacks[0];
              if (callbacks[2] != 0) return 1;
              if (zeroed[0] != 0) return 6;
              if (replace) callbacks[next_index()] = replacement;
              return copied(2.5, 4) + callbacks[1](2.5, 4);
            }

            int later(double first, int second) {
              return (int)first * 10 + second + 10;
            }

            int main(void) {
              if (dispatch(0) != 58) return 2;
              if (dispatch(1) != 68) return 3;
              if (index_calls != 1) return 4;
              if (dispatch(0) != 58) return 5;
              return 86;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 86, result.stdout + result.stderr
                )

    def test_raw_callback_arrays_keep_mixed_width_calls_and_one_store_index(self):
        source = """
            int call_index_calls;
            int store_index_calls;

            int combine(double first, int second) {
              return (int)first * 10 + second;
            }

            int replacement(double first, int second) {
              return (int)first * 20 + second;
            }

            int next_store_index(void) {
              store_index_calls += 1;
              return 0;
            }

            int next_call_index(void) {
              call_index_calls += 1;
              return 0;
            }

            int main(void) {
              static int (*callbacks[])(double, int) = { combine };
              int initial = (*callbacks[next_call_index()])(2.5, 4);
              callbacks[next_store_index()] = replacement;
              if (call_index_calls != 1) return 1;
              if (store_index_calls != 1) return 2;
              if (initial != 24) return 3;
              return callbacks[0](2.5, 4);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 44, result.stdout + result.stderr
                )

    def test_file_scope_raw_callback_arrays_run_in_jit_and_aot(self):
        source = """
            int plus_one(int value) { return value + 1; }

            int (*inferred_callbacks[])(int) = {
              (&plus_one), later_global,
            };
            int (*fixed_callbacks[3])(int) = { (&plus_one), 0 };
            int (*zeroed_callbacks[1])(int);

            int later_global(int value) { return value + 4; }

            int main(void) {
              int assigned_result;
              int (*copied)(int) = inferred_callbacks[1];
              if (fixed_callbacks[1] != 0) return 1;
              if (fixed_callbacks[2] != 0) return 2;
              if (zeroed_callbacks[0] != 0) return 3;
              if (sizeof(inferred_callbacks) != 8) return 4;
              if (sizeof(fixed_callbacks) != 12) return 5;
              if (sizeof(zeroed_callbacks) != 4) return 6;
              fixed_callbacks[2] = later_global;
              assigned_result = fixed_callbacks[2](5);
              fixed_callbacks[2] = 0;
              if (inferred_callbacks[0](3) != 4) return 7;
              if ((*inferred_callbacks[1])(3) != 7) return 8;
              if (copied(3) != 7) return 9;
              if (assigned_result != 9) return 10;
              if (fixed_callbacks[2] != 0) return 11;
              return 84;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 84, result.stdout + result.stderr
                )

    def test_raw_callback_arrays_keep_variadic_double_results(self):
        source = """
            double keep(double value, ...) { return value + 0.5; }

            int main(void) {
              static double (*callbacks[])(double, ...) = { keep };
              return (int)(*callbacks[0])(8.5, 1.25f, 3);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 9, result.stdout + result.stderr
                )

    def test_raw_callback_array_elements_flow_through_typed_values(self):
        source = """
            int add_one(int value) { return value + 1; }

            int invoke(int (*callback)(int), int value) {
              return callback(value);
            }

            int main(void) {
              static int (*callbacks[])(int) = { add_one };
              int (*copied)(int) = callbacks[0];
              return invoke(callbacks[0], 8) + copied(8);
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 18, result.stdout + result.stderr
                )

    def test_raw_callback_arrays_keep_record_pointer_identity(self):
        source = """
            struct Pair { int value; };

            struct Pair *choose(struct Pair *value) { return value; }

            struct Pair *invoke(
                struct Pair *(*callback)(struct Pair *),
                struct Pair *value) {
              return callback(value);
            }

            int main(void) {
              static struct Pair *(*callbacks[])(struct Pair *) = {
                choose
              };
              struct Pair *(*copied)(struct Pair *) = callbacks[0];
              struct Pair pair;
              pair.value = 23;
              return invoke(callbacks[0], &pair)->value +
                     copied(&pair)->value;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 46, result.stdout + result.stderr
                )

    def test_repl_keeps_raw_callback_array_signature(self):
        result = self._compile_repl_and_run(
            (
                "int later_repl(int value); "
                "int (*callbacks[])(int) = { later_repl, 0 };",
                "int later_repl(int value) { return value + 4; }",
                "int main(void) { "
                "if (callbacks[1] != 0) return 1; "
                "return (*callbacks[0])(8); }",
            )
        )
        self.assertEqual(result.returncode, 12, result.stdout + result.stderr)

    def test_repl_accepts_raw_callback_array_without_a_semicolon(self):
        result = self._compile_repl_and_run(
            (
                "int plus_one(int value) { return value + 1; }",
                "int (*callbacks[])(int) = { plus_one }",
                "int main(void) { return callbacks[0](8); }",
            )
        )
        self.assertEqual(result.returncode, 9, result.stdout + result.stderr)

    def test_raw_callback_array_failures_recover_same_state(self):
        cases = (
            (
                "unsized-without-initializer",
                "int (*callbacks[])(int); int main(void) { return 0; }",
                "unsized raw function-pointer array requires an initializer",
            ),
            (
                "empty-unsized-initializer",
                "int (*callbacks[])(int) = {}; int main(void) { return 0; }",
                "unsized raw function-pointer array requires at least one initializer",
            ),
            (
                "excess-fixed-initializers",
                """
                int first(int value) { return value; }
                int second(int value) { return value + 1; }
                int (*callbacks[1])(int) = { first, second };
                int main(void) { return 0; }
                """,
                "too many initializers for raw function-pointer array",
            ),
            (
                "initializer-without-braces",
                """
                int right(int value) { return value; }
                int (*callbacks[1])(int) = right;
                int main(void) { return 0; }
                """,
                "raw function-pointer array initializer requires braces",
            ),
            (
                "incompatible-initializer",
                """
                int later_compatible(int value);
                double wrong_result(int value) { return value; }
                int (*callbacks[])(int) = {
                  later_compatible, wrong_result
                };
                int later_compatible(int value) { return value; }
                int main(void) { return 0; }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "automatic-unsized",
                """
                int identity(int value) { return value; }
                int main(void) {
                  int (*callbacks[])(int) = { identity };
                  return 0;
                }
                """,
                "automatic raw function-pointer array size is required",
            ),
            (
                "automatic-incompatible-initializer",
                """
                int right(int value) { return value; }
                int wrong(double value) { return (int)value; }
                int main(void) {
                  int (*callbacks[2])(int) = { right, wrong };
                  return 0;
                }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "automatic-frame-capacity",
                """
                int main(void) {
                  int (*callbacks[536870909])(int);
                  return 0;
                }
                """,
                "local frame size overflow",
            ),
            (
                "parameter-storage",
                """
                int invoke(int (*callbacks[2])(int)) { return 0; }
                int main(void) { return 0; }
                """,
                "raw function-pointer array parameters are not supported",
            ),
            (
                "nonpositive-size",
                "int (*callbacks[0])(int); int main(void) { return 0; }",
                "raw function-pointer array size must be positive",
            ),
            (
                "multiple-dimensions",
                "int (*callbacks[2][2])(int); int main(void) { return 0; }",
                "raw function-pointer arrays support one dimension",
            ),
            (
                "indexed-call-too-few-arguments",
                """
                int combine(int first, int second) {
                  return first + second;
                }
                int (*callbacks[])(int, int) = { combine };
                int main(void) { return callbacks[0](1); }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "indexed-call-too-many-arguments",
                """
                int identity(int value) { return value; }
                int (*callbacks[])(int) = { identity };
                int main(void) { return callbacks[0](1, 2); }
                """,
                "function-pointer call has too many arguments",
            ),
            (
                "incompatible-indexed-store",
                """
                int right(int value) { return value; }
                int wrong(double value) { return (int)value; }
                int (*callbacks[])(int) = { right };
                int main(void) {
                  callbacks[0] = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "incompatible-indexed-store-result",
                """
                int right(int value) { return value; }
                double wrong(int value) { return value; }
                int (*callbacks[])(int) = { right };
                int main(void) {
                  callbacks[0] = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment result does not match destination",
            ),
            (
                "incompatible-indexed-argument",
                """
                int target(double value) { return (int)value; }
                int invoke(int (*callback)(int), int value) {
                  return callback(value);
                }
                int (*callbacks[])(double) = { target };
                int main(void) { return invoke(callbacks[0], 9); }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "incompatible-indexed-declaration-initializer",
                """
                int target(double value) { return (int)value; }
                int (*callbacks[])(double) = { target };
                int main(void) {
                  int (*copied)(int) = callbacks[0];
                  return copied(9);
                }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
            (
                "incompatible-indexed-argument-result",
                """
                double target(int value) { return value; }
                int invoke(int (*callback)(int), int value) {
                  return callback(value);
                }
                double (*callbacks[])(int) = { target };
                int main(void) { return invoke(callbacks[0], 9); }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "incompatible-indexed-declaration-initializer-result",
                """
                double target(int value) { return value; }
                double (*callbacks[])(int) = { target };
                int main(void) {
                  int (*copied)(int) = callbacks[0];
                  return copied(9);
                }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "incompatible-indexed-argument-record-identity",
                """
                struct Pair { int value; };
                struct Other { int value; };
                struct Pair *choose(struct Pair *value) { return value; }
                struct Other *invoke(
                    struct Other *(*callback)(struct Other *),
                    struct Other *value) {
                  return callback(value);
                }
                struct Pair *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Other value;
                  return invoke(callbacks[0], &value)->value;
                }
                """,
                "function-pointer argument result does not match parameter type",
            ),
            (
                "incompatible-indexed-initializer-record-identity",
                """
                struct Pair { int value; };
                struct Other { int value; };
                struct Pair *choose(struct Pair *value) { return value; }
                struct Pair *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Other *(*copied)(struct Other *) = callbacks[0];
                  struct Other value;
                  return copied(&value)->value;
                }
                """,
                "function-pointer initializer result does not match declaration",
            ),
            (
                "incompatible-indexed-argument-parameter-record-identity",
                """
                struct Result { int value; };
                struct Pair { int value; };
                struct Other { int value; };
                struct Result output;
                struct Result *choose(struct Pair *value) { return &output; }
                struct Result *invoke(
                    struct Result *(*callback)(struct Other *),
                    struct Other *value) {
                  return callback(value);
                }
                struct Result *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Other value;
                  return invoke(callbacks[0], &value)->value;
                }
                """,
                "function-pointer argument parameters do not match parameter type",
            ),
            (
                "incompatible-indexed-initializer-parameter-record-identity",
                """
                struct Result { int value; };
                struct Pair { int value; };
                struct Other { int value; };
                struct Result output;
                struct Result *choose(struct Pair *value) { return &output; }
                struct Result *(*callbacks[])(struct Pair *) = { choose };
                int main(void) {
                  struct Result *(*copied)(struct Other *) = callbacks[0];
                  struct Other value;
                  return copied(&value)->value;
                }
                """,
                "function-pointer initializer parameters do not match declaration",
            ),
        )
        retry_source = """
            int right(int value) { return value; }
            int main(void) {
              int (*callbacks[1])(int) = { right };
              return callbacks[0](9);
            }
        """

        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-array-recovery-",
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

    def test_repl_rolls_back_a_partial_raw_callback_array_initializer(self):
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
            prefix="private-cupidc-repl-raw-array-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "int later_poisoned(double value); "
                        "double wrong_result(double value) { return value; } "
                        "int (*poisoned[])(double) = { "
                        "later_poisoned, wrong_result };",
                        "int sentinel = 77; "
                        "int later_poisoned(double value) { "
                        "return (int)value; } "
                        + "\n".join(capacity_declarations),
                        "int recovered(int value) { return value; } "
                        "int (*callbacks[])(int) = { recovered }; "
                        "int main(void) { "
                        "if (baseline != 0 || sentinel != 77) return 1; "
                        "return callbacks[0](9); }",
                    ),
                    require_patch=True,
                )
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "function-pointer initializer result does not match declaration",
                result.stderr,
            )
            self.assertEqual(data_path.stat().st_size, 12)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode,
                9,
                runtime.stdout + runtime.stderr,
            )

    def test_repl_keeps_raw_callback_struct_field_signature(self):
        result = self._compile_repl_and_run(
            (
                "struct Holder { int (*callback)(int); };",
                "int add_seven(int value) { return value + 7; }",
                """
                int main() {
                  struct Holder holder;
                  int (*copied)(int) = 0;
                  holder.callback = add_seven;
                  copied = holder.callback;
                  return copied(9);
                }
                """,
            )
        )
        self.assertEqual(result.returncode, 16, result.stdout + result.stderr)

    def test_repl_rolls_back_failed_raw_callback_struct_field(self):
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
            prefix="private-cupidc-repl-raw-field-rollback-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code_path, _data_path = (
                self._compile_repl_after_struct_failure(
                    root,
                    (
                        "int baseline;",
                        "struct Poisoned { int (*callback)(double); }; "
                        "int broken(void) { return missing_value; }",
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

    def test_raw_callback_field_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                struct Holder { int (*callback)(int); };
                double wrong(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.callback = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment result does not match destination",
            ),
            (
                "parameters",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                class Holder {
                  int (*callback)(struct Expected *);
                };
                int wrong(struct Wrong *value) { return value->value; }
                int main() {
                  Holder holder;
                  holder.callback = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "missing-name",
                """
                struct Holder { int (*)(int); };
                int main() { return 0; }
                """,
                "expected function pointer name",
            ),
            (
                "raw-array",
                """
                struct Holder { int (*callbacks[2])(int); };
                int main() { return 0; }
                """,
                "raw function-pointer arrays are not supported; use a callback typedef",
            ),
        )
        retry_source = """
            struct Holder { int (*callback)(int); };
            int right(int value) { return value; }
            int main() {
              struct Holder holder;
              int (*copied)(int) = 0;
              holder.callback = right;
              copied = holder.callback;
              return copied(9);
            }
        """

        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-raw-callback-field-recovery-",
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

    def test_raw_callback_field_capacity_recovers_same_state(self):
        declarations = []
        for arity in range(33):
            parameters = "void" if arity == 0 else ", ".join(
                "int" for _ in range(arity)
            )
            declarations.append(
                f"struct Holder{arity} {{ "
                f"int (*callback)({parameters}); }};"
            )
        failing_source = (
            "\n".join(declarations) + "\nint main() { return 0; }"
        )
        retry_source = """
            struct Holder { int (*callback)(int); };
            int right(int value) { return value; }
            int main() {
              struct Holder holder;
              int (*copied)(int) = 0;
              holder.callback = right;
              copied = holder.callback;
              return copied(9);
            }
        """

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-raw-callback-field-capacity-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_failure(
                root,
                failing_source,
                retry_source,
                same_state=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "too many raw function-pointer signatures",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 9, runtime.stdout + runtime.stderr
            )

    def test_callback_field_postfix_call_failures_recover_same_state(self):
        cases = (
            (
                "too-few",
                """
                typedef int (*callback_t)(int, int);
                struct Holder { callback_t callback; };
                int add(int left, int right) { return left + right; }
                int main() {
                  struct Holder holder;
                  holder.callback = add;
                  return holder.callback(1);
                }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "too-many",
                """
                struct Holder { int (*callback)(int); };
                int identity(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.callback = identity;
                  return holder.callback(1, 2);
                }
                """,
                "function-pointer call has too many arguments",
            ),
            (
                "fixed-type",
                """
                struct Holder { int (*callback)(double); };
                int inspect(double value) { return (int)value; }
                int main() {
                  struct Holder holder;
                  int value = 7;
                  holder.callback = inspect;
                  return holder.callback(&value);
                }
                """,
                "cdecl argument type does not match fixed parameter",
            ),
            (
                "array-too-few",
                """
                typedef int (*callback_t)(int, int);
                struct Holder { callback_t callbacks[2]; };
                int add(int first, int second) { return first + second; }
                int main() {
                  struct Holder holder;
                  holder.callbacks[0] = add;
                  return holder.callbacks[0](1);
                }
                """,
                "function-pointer call has too few arguments",
            ),
            (
                "array-too-many",
                """
                typedef int (*callback_t)(int);
                struct Holder { callback_t callbacks[2]; };
                int identity(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.callbacks[0] = identity;
                  return holder.callbacks[0](1, 2);
                }
                """,
                "function-pointer call has too many arguments",
            ),
            (
                "array-fixed-type",
                """
                typedef int (*callback_t)(double);
                struct Holder { callback_t callbacks[2]; };
                int inspect(double value) { return (int)value; }
                int main() {
                  struct Holder holder;
                  int value = 7;
                  holder.callbacks[0] = inspect;
                  return holder.callbacks[0](&value);
                }
                """,
                "cdecl argument type does not match fixed parameter",
            ),
        )
        retry_source = """
            struct Holder { int (*callback)(int); };
            int identity(int value) { return value; }
            int main() {
              struct Holder holder;
              holder.callback = identity;
              return holder.callback(9);
            }
        """

        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-field-call-recovery-",
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

    def test_typedef_callback_field_mismatches_recover_same_state(self):
        cases = (
            (
                "field-result",
                """
                typedef int (*callback_t)(int value);
                struct Holder { callback_t cb; };

                double wrong(int value) { return value; }
                int main() {
                  struct Holder holder;
                  holder.cb = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment result does not match destination",
            ),
            (
                "field-store",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                typedef int (*callback_t)(struct Expected *);
                struct Holder { callback_t cb; };

                int wrong(struct Wrong *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  holder.cb = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "field-copy",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                typedef int (*expected_callback_t)(struct Expected *);
                typedef int (*wrong_callback_t)(struct Wrong *);
                struct Holder { expected_callback_t cb; };

                int right(struct Expected *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  wrong_callback_t callback = 0;
                  holder.cb = right;
                  callback = holder.cb;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "compound-store",
                """
                struct Entry { int value; };
                typedef int (*callback_t)(struct Entry *);
                struct Holder { callback_t cb; };

                int right(struct Entry *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  holder.cb += right;
                  return 0;
                }
                """,
                "function-pointer assignment requires plain =",
            ),
            (
                "callback-array-field-store",
                """
                typedef int (*callback_t)(int value);
                struct Holder { callback_t callbacks[2]; };

                int wrong(double value) { return (int)value; }
                int main() {
                  struct Holder holder;
                  holder.callbacks[0] = wrong;
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
            (
                "callback-array-field-copy",
                """
                struct Expected { int value; };
                struct Wrong { int value; };
                typedef int (*expected_callback_t)(struct Expected *value);
                typedef int (*wrong_callback_t)(struct Wrong *value);
                struct Holder { expected_callback_t callbacks[2]; };

                int right(struct Expected *value) { return value->value; }
                int main() {
                  struct Holder holder;
                  wrong_callback_t callback = 0;
                  holder.callbacks[0] = right;
                  callback = holder.callbacks[0];
                  return 0;
                }
                """,
                "function-pointer assignment parameters do not match destination",
            ),
        )
        retry_source = """
            struct Entry { int value; };
            typedef int (*callback_t)(struct Entry *);
            struct Holder { callback_t cb; };

            int right(struct Entry *value) { return value->value; }
            int main() {
              struct Holder holder;
              struct Entry entry;
              callback_t callback = 0;
              entry.value = 9;
              holder.cb = right;
              callback = holder.cb;
              return callback(&entry);
            }
        """
        for label, failing_source, diagnostic in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-callback-field-",
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

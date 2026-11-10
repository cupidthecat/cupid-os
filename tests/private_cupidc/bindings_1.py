"""Private compiler bindings execution cases."""

from .harness import (Path, REPO_ROOT, subprocess, tempfile)

class Bindings1Cases:
    def test_halfword_native_network_slots_and_results(self):
        cases = {
            "htons": "if (htons(70196.75) != 13330) return 1;",
            "ntohs": "if (ntohs(70196.75) != 13330) return 2;",
            "bind": "if (bind(31.0, 2147483649.0, 70196.75) != -7) return 3;",
            "connect": "if (connect(31.0, 2147483649.0, 70196.75) != -8) return 4;",
            "accept": "if (accept(31.0, &ip, &ports[0]) != 43 || ip != 2147483650u || ports[0] != 65000 || ports[1] != 1234) return 5;",
            "sendto": "if (sendto(31.0, bytes, 2147483651.0, 2147483649.0, 70196.75) != 47) return 6;",
            "recvfrom": "if (recvfrom(31.0, bytes, 2147483652.0, &ip, &ports[0]) != -9 || ip != 2147483651u || ports[0] != 65001 || ports[1] != 1234 || bytes[0] != 67) return 7;",
            "udp_send_raw": "if (udp_send_raw(2147483649.0, 70196.75, -1.0, bytes, 2147483653.0) != -10) return 8;",
        }
        for name, body in cases.items():
            with self.subTest(name=name):
                self._assert_halfword_runtime(
                    "int main() { U32 ip=0; U16 ports[2]; char bytes[2]; int i=0; "
                    "while (i < 1000) { ports[0]=0; ports[1]=1234; bytes[0]=53; "
                    + body + " i+=1; } return 0; }")

    def test_halfword_native_bindings_reject_arity_and_bad_types(self):
        calls = {
            "htons": ["4660"], "ntohs": ["4660"],
            "bind": ["31", "1", "2"], "connect": ["31", "1", "2"],
            "accept": ["31", "0", "0"], "sendto": ["31", "0", "8", "1", "2"],
            "recvfrom": ["31", "0", "8", "0", "0"], "udp_send_raw": ["1", "2", "3", "0", "8"],
        }
        failures = []
        for name, arguments in calls.items():
            for selected, detail in ((arguments[:-1], "too few"), ([*arguments, "0"], "too many")):
                failures.append((f"int main() {{ return {name}({', '.join(selected)}); }}",
                                 f"kernel binding call has {detail} arguments"))
        for call in ('accept(31, 1.0, 0)', 'accept(31, 0, 1.0)',
                     'sendto(31, 1.0, 8, 1, 2)', 'recvfrom(31, 1.0, 8, 0, 0)',
                     'recvfrom(31, 0, 8, 0, 1.0)', 'udp_send_raw(1, 2, 3, 1.0, 8)'):
            failures.append((f"int main() {{ return {call}; }}",
                             "cdecl argument type does not match fixed parameter"))
        failures.extend((f"int main() {{ float4 v={{1,2,3,4}}; return {name}(v); }}",
                         "cdecl argument type does not match fixed parameter") for name in ('htons', 'ntohs'))
        for source, diagnostic in failures:
            for aot in (False, True):
                with self.subTest(source=source, aot=aot):
                    with tempfile.TemporaryDirectory(prefix='private-halfword-native-negative-') as temporary:
                        result, _code, _data = self._compile(Path(temporary), source, aot=aot)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(diagnostic, result.stderr)

    def test_network_bindings_convert_native_slots_in_jit_and_aot(self):
        cases = {
            "socket": "if (socket(2.0) != 31) return 1;",
            "listen": "if (listen(31.0, 3.0) != 0) return 2;",
            "send": "if (send(31.0, bytes, 2147483651.0) != 43) return 3;",
            "recv": "if (recv(31.0, bytes, 2147483650.0) != -7 || bytes[0] != 67) return 4;",
            "setsockopt": "if (setsockopt(31.0, -1.0, 2.0, bytes, 2147483652.0) != -9) return 5;",
            "sock_avail": "if (sock_avail(31.0) != 2147483647) return 6;",
            "sock_state": "if (sock_state(31.0) != -3) return 7;",
            "close": "if (close(31.0) != 0) return 8;",
            "htonl": "if (htonl(2147483649.0) != 16777344u || htonl(16777344.0) != 2147483649u) return 9;",
            "ntohl": "if (ntohl(2147483649.0) != 16777344u || ntohl(16777344.0) != 2147483649u) return 10;",
            "dns_resolve": 'if (dns_resolve("example", &ip) != 0 || ip != 2147483650u) return 11;',
            "ip_parse": 'if (ip_parse("10.0.2.15", &ip) != 0 || ip != 2147483651u) return 12;',
        }
        for name, body in cases.items():
            source = ("int main(void) { unsigned int ip = 0; char bytes[8]; int i = 0; "
                      "while (i < 1000) { bytes[0] = 53; " + body + " i += 1; } return 0; }")
            for aot in (False, True):
                with self.subTest(name=name, aot=aot):
                    result = self._compile_and_run(source, aot=aot)
                    self.assertEqual((result.returncode, result.stdout, result.stderr), (0, "", ""))

    def test_network_bindings_preserve_signed_peer_ip_pointers(self):
        source = """
            int main(void) {
              int ip = 0;
              if (dns_resolve("example", &ip) != 0 || ip != -2147483646) return 1;
              if (ip_parse("10.0.2.15", &ip) != 0 || ip != -2147483645) return 2;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual((result.returncode, result.stdout, result.stderr), (0, "", ""))

    def test_network_bindings_reject_arity_and_incompatible_arguments(self):
        calls = {
            "socket": ["2"], "listen": ["31", "3"],
            "send": ["31", '"bytes"', "8"], "recv": ["31", '"bytes"', "8"],
            "close": ["31"], "setsockopt": ["31", "1", "1", '"bytes"', "8"],
            "sock_avail": ["31"], "sock_state": ["31"],
            "htonl": ["1"], "ntohl": ["1"],
            "dns_resolve": ['"example"', "0"], "ip_parse": ['"10.0.2.15"', "0"],
        }
        failures = []
        for name, arguments in calls.items():
            for selected, detail in ((arguments[:-1], "too few"), ([*arguments, "0"], "too many")):
                failures.append((f"int main(void) {{ return {name}({', '.join(selected)}); }}",
                                 f"kernel binding call has {detail} arguments"))
        for call in ('send(31, 1.0, 8)', 'recv(31, 1.0, 8)',
                     'dns_resolve("example", 1.0)', 'ip_parse("10.0.2.15", 1.0)'):
            failures.append((f"int main(void) {{ return {call}; }}",
                             "cdecl argument type does not match fixed parameter"))
        failures.append(("int main(void) { float4 v = {1, 2, 3, 4}; return socket(v); }",
                         "cdecl argument type does not match fixed parameter"))
        for source, diagnostic in failures:
            for aot in (False, True):
                with self.subTest(source=source, aot=aot):
                    with tempfile.TemporaryDirectory(prefix="private-cupidc-network-negative-",
                                                     ignore_cleanup_errors=True) as temporary:
                        result, _code, _data = self._compile(Path(temporary), source, aot=aot)
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(diagnostic, result.stderr)

    def test_network_binding_failures_recover_in_the_same_compiler_state(self):
        failures = (
            ("int main(void) { return socket(); }", "int main(void) { return socket(2, 3); }"),
            ("int main(void) { return send(31, 1.0, 8); }",
             "int main(void) { float4 v = {1, 2, 3, 4}; return socket(v); }"),
        )
        for first, second in failures:
            with self.subTest(first=first):
                with tempfile.TemporaryDirectory(prefix="private-cupidc-network-recovery-",
                                                 ignore_cleanup_errors=True) as temporary:
                    root = Path(temporary)
                    result, _code, _data = self._compile_after_two_failures(
                        root, first, second, "int main(void) { return socket(2.0) - 31; }")
                    self.assertEqual(result.returncode, 0, result.stderr)
                    executed = self._run_i386(root, int(result.stdout.strip()))
                    self.assertEqual((executed.returncode, executed.stdout, executed.stderr), (0, "", ""))

    def test_vfs_bindings_convert_arguments_to_native_slots_in_jit_and_aot(self):
        source = """
            int main(void) {
              double flags = 2147483649.0;
              double count = 2147483650.0;
              double descriptor = 31.0;
              double offset = -7.0;
              float origin = 2.0f;
              char bytes[2];
              int index = 0;
              bytes[0] = 53;
              bytes[1] = 0;
              if (vfs_open("/file", flags) != 31) return 1;
              while (index < 1000) {
                if (vfs_read(descriptor, bytes, count) != 43) return 2;
                if (vfs_write(descriptor, bytes, 1.0) != 1) return 3;
                index += 1;
              }
              if (vfs_seek(descriptor, offset, origin) != 42) return 4;
              if (vfs_mkdir("/directory") != 0) return 5;
              if (vfs_unlink("/file") != 0) return 6;
              if (vfs_rename("/before", "/after") != 0) return 7;
              if (vfs_close(descriptor) != 0) return 8;
              if (bytes[0] != 53 || bytes[1] != 0) return 9;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_vfs_bindings_reject_arity_and_incompatible_arguments(self):
        calls = {
            "vfs_open": ['"/file"', "0"],
            "vfs_close": ["31"],
            "vfs_read": ["31", '"bytes"', "5"],
            "vfs_write": ["31", '"bytes"', "5"],
            "vfs_seek": ["31", "-7", "2"],
            "vfs_mkdir": ['"/directory"'],
            "vfs_unlink": ['"/file"'],
            "vfs_rename": ['"/before"', '"/after"'],
        }
        failures = []
        for name, arguments in calls.items():
            for args, detail in (
                (arguments[:-1], "too few"),
                ([*arguments, "0"], "too many"),
            ):
                failures.append((
                    f"int main(void) {{ return {name}({', '.join(args)}); }}",
                    f"kernel binding call has {detail} arguments",
                ))
        failures.extend((
            ('int main(void) { return vfs_open(1.0, 0); }',
             "cdecl argument type does not match fixed parameter"),
            ('int main(void) { return vfs_read(31, 1.0, 5); }',
             "cdecl argument type does not match fixed parameter"),
            ('int main(void) { float4 value = {1, 2, 3, 4}; '
             'return vfs_close(value); }',
             "cdecl argument type does not match fixed parameter"),
        ))
        for source, diagnostic in failures:
            for aot in (False, True):
                with self.subTest(source=source, aot=aot):
                    with tempfile.TemporaryDirectory(
                        prefix="private-cupidc-vfs-negative-",
                        ignore_cleanup_errors=True,
                    ) as temporary:
                        result, _code, _data = self._compile(
                            Path(temporary), source, aot=aot
                        )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(diagnostic, result.stderr)

    def test_vfs_binding_failure_recovers_in_the_same_compiler_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-vfs-recovery-", ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_two_failures(
                root,
                "int main(void) { return vfs_read(31); }",
                "int main(void) { return vfs_read(31, 1.0, 5); }",
                "int main(void) { return vfs_seek(31.0, -7.0, 2.0) - 42; }",
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("kernel binding call has too few arguments", result.stderr)
            self.assertIn("cdecl argument type does not match fixed parameter", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_swap_bindings_convert_sizes_and_handles_in_jit_and_aot(self):
        source = """
            int main(void) {
              double pool = 2147483651.0;
              double size = 500.0;
              double handle_argument = 2147483649.0;
              U32 handle;
              char *bytes;
              if (swap_init("/swap", pool) != 0) return 1;
              handle = swap_kmalloc(size);
              if (handle != (U32)0x80000001) return 2;
              if (handle < 0) return 3;
              bytes = (char *)swap_pin(handle_argument);
              if (!bytes) return 4;
              bytes[0] = 53;
              if (bytes[0] != 53) return 5;
              swap_unpin(handle_argument);
              swap_free(handle_argument);
              if (native_free_count() != 3) return 6;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_swap_bindings_reject_arity_and_incompatible_arguments(self):
        calls = {
            "swap_init": ['"/swap"', "500"],
            "swap_kmalloc": ["500"],
            "swap_pin": ["1"],
            "swap_unpin": ["1"],
            "swap_free": ["1"],
        }
        failures = []
        for name, arguments in calls.items():
            for args, detail in (
                (arguments[:-1], "too few"),
                ([*arguments, "0"], "too many"),
            ):
                failures.append((
                    f"int main(void) {{ {name}({', '.join(args)}); return 0; }}",
                    f"kernel binding call has {detail} arguments",
                ))
        failures.extend((
            ('int main(void) { swap_init(1.0, 500); return 0; }',
             "cdecl argument type does not match fixed parameter"),
            ('int main(void) { float4 value = {1, 2, 3, 4}; '
             'swap_pin(value); return 0; }',
             "cdecl argument type does not match fixed parameter"),
        ))
        for source, diagnostic in failures:
            for aot in (False, True):
                with self.subTest(source=source, aot=aot):
                    with tempfile.TemporaryDirectory(
                        prefix="private-cupidc-swap-negative-",
                        ignore_cleanup_errors=True,
                    ) as temporary:
                        result, _code, _data = self._compile(
                            Path(temporary), source, aot=aot
                        )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(diagnostic, result.stderr)

    def test_swap_binding_failure_recovers_in_the_same_compiler_state(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-swap-recovery-", ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_two_failures(
                root,
                "int main(void) { swap_pin(1, 2); return 0; }",
                "int main(void) { float4 value = {1, 2, 3, 4}; "
                "swap_pin(value); return 0; }",
                "int main(void) { return swap_pin(2147483649.0) == 0; }",
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("kernel binding call has too many arguments", result.stderr)
            self.assertIn("cdecl argument type does not match fixed parameter", result.stderr)
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(runtime.returncode, 0, runtime.stdout + runtime.stderr)

    def test_typed_kernel_bindings_convert_and_lay_out_fixed_arguments(self):
        source = """
            int main(void) {
              float small = 5.0f;
              if (sqrt(7) != 7.0) return 1;
              if (native_float(6) != 6.0f) return 2;
              if (sqrt(small) != 5.0) return 3;
              if (native_mixed(1, 2, 3, 4) != 10) return 4;
              if (native_variadic(1, (char)2, 3.0f) != 6) return 5;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_new_and_del_use_typed_allocator_bindings_in_jit_and_aot(self):
        source = """
            class Pair {
              int a;
              int b;
            };

            int main(void) {
              Pair *pair = new Pair;
              U8 *bytes = new U8[8];
              int index = 0;
              if (!pair || !bytes) return 1;
              if (pair->a != 0 || pair->b != 0) return 2;
              while (index < 8) {
                if (bytes[index] != 0) return 3;
                index += 1;
              }
              pair->a = 20;
              pair->b = 22;
              bytes[0] = 42;
              if (pair->a + pair->b != 42 || bytes[0] != 42) return 4;
              del pair;
              del bytes;
              if (pair != 0 || bytes != 0) return 5;
              if (native_free_count() != 2) return 6;
              return 0;
            }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_typed_kernel_binding_arity_and_type_failures_recover(self):
        failures = (
            (
                "too few",
                "int main(void) { return sqrt(); }",
                "kernel binding call has too few arguments",
            ),
            (
                "too many",
                "int main(void) { return sqrt(1, 2); }",
                "kernel binding call has too many arguments",
            ),
            (
                "wrong type",
                'int main(void) { return sqrt("not a number"); }',
                "cdecl argument type does not match fixed parameter",
            ),
        )
        for label, source, diagnostic in failures:
            for aot in (False, True):
                with self.subTest(label=label, aot=aot):
                    with tempfile.TemporaryDirectory(
                        prefix="private-cupidc-kernel-binding-negative-",
                        ignore_cleanup_errors=True,
                    ) as temporary:
                        result, _code, _data = self._compile(
                            Path(temporary), source, aot=aot
                        )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(diagnostic, result.stderr)

        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-kernel-binding-recovery-",
            ignore_cleanup_errors=True,
        ) as temporary:
            root = Path(temporary)
            result, _code, _data = self._compile_after_two_failures(
                root,
                "int main(void) { return sqrt(); }",
                'int main(void) { return sqrt("bad"); }',
                "int main(void) { return native_mixed(1, 2, 3, 4) - 10; }",
            )
            self.assertEqual(
                result.returncode, 0, result.stdout + result.stderr
            )
            self.assertIn(
                "kernel binding call has too few arguments", result.stderr
            )
            self.assertIn(
                "cdecl argument type does not match fixed parameter",
                result.stderr,
            )
            runtime = self._run_i386(root, int(result.stdout.strip()))
            self.assertEqual(
                runtime.returncode, 0, runtime.stdout + runtime.stderr
            )

    def test_typed_kernel_binding_descriptor_failures_roll_back(self):
        for aot in (False, True):
            with self.subTest(aot=aot):
                command = [str(self.driver), "--binding-registration-rollback"]
                if aot:
                    command.append("--aot")
                result = subprocess.run(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                    timeout=30,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )

    def test_nested_kernel_binding_descriptor_limits_recover_same_state(self):
        for mode in ("jit", "aot"):
            with self.subTest(mode=mode):
                command = [
                    str(self.driver),
                    "--nested-binding-signature-limits",
                ]
                if mode == "aot":
                    command.append("--aot")
                result = subprocess.run(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                    timeout=30,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )

    def test_nested_kernel_binding_descriptor_capacity_recovers_same_state(self):
        for mode in ("jit", "aot"):
            with self.subTest(mode=mode):
                command = [
                    str(self.driver),
                    "--nested-binding-signature-capacity",
                ]
                if mode == "aot":
                    command.append("--aot")
                result = subprocess.run(
                    command,
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                    timeout=30,
                )
                self.assertEqual(
                    result.returncode,
                    0,
                    result.stdout + result.stderr,
                )

    def test_nested_kernel_binding_executes_in_jit_and_aot(self):
        source = """
            int observed;

            void draw(int x, int y) {
              observed = x * 10 + y;
            }

            int main() {
              set_icon_drawer(4, draw);
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

    def test_nested_kernel_binding_mismatches_recover_same_state(self):
        cases = (
            (
                "result",
                """
                int wrong(int x, int y) { return x + y; }
                int main() { set_icon_drawer(4, wrong); return 0; }
                """,
                "function-pointer argument result does not match parameter type",
                False,
            ),
            (
                "parameters",
                """
                void wrong(int x) { (void)x; }
                int main() { set_icon_drawer(4, wrong); return 0; }
                """,
                "function-pointer argument parameters do not match parameter type",
                False,
            ),
            (
                "parameter type",
                """
                struct Wrong { int value; };
                void wrong(struct Wrong *value, int marker) {
                  (void)value;
                  (void)marker;
                }
                int main() { set_icon_drawer(4, wrong); return 0; }
                """,
                "function-pointer argument parameters do not match parameter type",
                False,
            ),
            (
                "record identity",
                """
                struct BindingRecord { int value; };
                struct OtherRecord { int value; };
                void wrong(struct OtherRecord *value) { (void)value; }
                int main() { native_record_drawer(4, wrong); return 0; }
                """,
                "function-pointer argument parameters do not match parameter type",
                True,
            ),
            (
                "variadic boundary",
                """
                void wrong(int x, ...) { (void)x; }
                int main() { set_icon_drawer(4, wrong); return 0; }
                """,
                "function-pointer argument parameters do not match parameter type",
                False,
            ),
        )
        retry_source = """
            int observed;
            void draw(int x, int y) { observed = x * 10 + y; }
            int main() {
              set_icon_drawer(6, draw);
              return observed;
            }
        """
        record_retry_source = """
            struct BindingRecord { int value; };
            int observed;
            void draw_record(struct BindingRecord *value) {
              observed = value == 0 ? 1 : 0;
            }
            int main() {
              native_record_drawer(6, draw_record);
              return observed;
            }
        """
        for label, failing_source, diagnostic, record_binding in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory(
                prefix="private-cupidc-nested-kernel-binding-",
                ignore_cleanup_errors=True,
            ) as temporary:
                root = Path(temporary)
                result, _code, _data = self._compile_after_failure(
                    root,
                    failing_source,
                    record_retry_source if record_binding else retry_source,
                    same_state=True,
                    record_binding=record_binding,
                )
                self.assertEqual(
                    result.returncode, 0, result.stdout + result.stderr
                )
                self.assertIn(diagnostic, result.stderr)
                self.assertGreaterEqual(int(result.stdout.strip()), 0)

    def test_global_callback_initializes_from_kernel_binding_in_jit_and_aot(self):
        source = """
            typedef void (*printer_t)(char *text);

            void serial_printf(char *text);
            printer_t callback = serial_printf;

            int main() { return callback == serial_printf; }
        """
        for aot in (False, True):
            with self.subTest(aot=aot):
                result = self._compile_and_run(source, aot=aot)
                self.assertEqual(
                    result.returncode, 1, result.stdout + result.stderr
                )

    def test_matching_prototype_preserves_a_kernel_binding(self):
        with tempfile.TemporaryDirectory(
            prefix="private-cupidc-kernel-prototype-",
            ignore_cleanup_errors=True,
        ) as temporary:
            result, _code, _data = self._compile(
                Path(temporary),
                """
                void serial_printf(char *text);
                int main(void) {
                  serial_printf("callback prototype");
                  return 0;
                }
                """,
                aot=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

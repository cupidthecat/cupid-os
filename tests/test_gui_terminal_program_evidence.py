## @file
#  @brief Verify serial evidence for the programs exercised by the GUI smoke test.
import hashlib
import re
import unittest

from tests.c_source_spelling import _c_spelling_ranges, c_block, c_literal_text
from tests.gui_terminal_fixtures import (
    FEATURE7_SOURCE,
    GFXGUI_SOURCE,
    JPEG_FIXTURE,
    REPO_ROOT,
    _frontier_command,
    _frontier_command_output,
    _frontier_command_outputs,
    _frontier_runtime_log,
)
from tools import gui_terminal_contracts, gui_terminal_smoke


class FrontierProgramEvidenceTests(unittest.TestCase):
    def test_command_sequence_uses_source_backed_success_markers(self):
        commands = gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS

        self.assertEqual(
            [command.text for command in commands],
            [
                "ls",
                "/bin/kbdsub_test.cc",
                "/bin/date.cc +epoch",
                "as /demos/syscall_vfs_extended_demo.asm",
                "dis /bin/test_fpaug.cc",
                "/bin/test_fpaug.cc",
                "/bin/feature7_new_del.cc",
                "/bin/feature13_double.cc",
                "ccc /bin/feature13_derived_aot.cc -o /feature13_derived_aot",
                "exec /feature13_derived_aot",
                "/bin/feature14_simd.cc",
                "/bin/feature15_libm.cc",
                "/bin/feature17_iso.cc",
                "/bin/feature18_swap.cc",
                "/bin/feature23_full_access.cc",
                "ccc /bin/feature23_full_access.cc -o /feature23_binding_aot",
                "exec /feature23_binding_aot",
                "ccc /bin/gfxgui_test.cc -o /gfxgui_test",
                "/bin/gfxgui_test.cc",
                "ccc /bin/gfxhandoff_exit.cc -o /gfxhandoff_exit",
                "ccc /bin/gfxhandoff_kill.cc -o /gfxhandoff_kill",
                "exec /gfxhandoff_exit",
                "exec /gfxhandoff_kill {pid}",
                "exec /gfxgui_test",
                "dglibc_test",
                "doom",
                "doom -iwad /disk/missing.wad",
                "ls",
                "browser --selftest",
                "audiotest all",
                "godsong 1 200",
            ],
        )
        self.assertEqual(
            commands[-1].followup_keys,
            ("esc",) * 8,
        )
        self.assertGreater(len(commands[-1].followup_keys), 5)
        self.assertEqual(commands[1].followup_keys, ("shift",))
        self.assertEqual(commands[1].followup_settle_seconds, 0.0)
        feature7_command = _frontier_command("/bin/feature7_new_del.cc")
        self.assertIn(
            r"\[cupidc\] JIT compile: /bin/feature7_new_del\.cc",
            feature7_command.expected_pattern,
        )
        self.assertIn(
            "feature7_new_del: PASS",
            feature7_command.expected_pattern,
        )
        self.assertIn(
            gui_terminal_contracts.CUPIDC_COMPLETION_PATTERN,
            feature7_command.expected_pattern,
        )
        feature7_source = FEATURE7_SOURCE.read_text(encoding="utf-8")
        self.assertIn(
            'serial_printf("feature7_new_del: PASS\\n");',
            feature7_source,
        )
        self.assertIn(
            'serial_printf("feature7_new_del: FAIL\\n");',
            feature7_source,
        )
        self.assertIn(
            "waiting for USB Shift",
            commands[1].interaction_pattern,
        )
        self.assertEqual(
            commands[-1].interaction_pattern,
            gui_terminal_contracts.GODSONG_SETTINGS_READY_PATTERN,
        )
        self.assertNotIn("flip frame=2", commands[-1].interaction_pattern)
        self.assertEqual(commands[-1].followup_settle_seconds, 0.0)
        exit_command = _frontier_command("exec /gfxhandoff_exit")
        self.assertEqual(exit_command.capture_name, "gfx_owner_pid")
        for text in (
            "exec /gfxhandoff_kill {pid}",
            "exec /gfxgui_test",
        ):
            self.assertEqual(
                _frontier_command(text).pid_from_capture,
                "gfx_owner_pid",
            )

        resolved = gui_terminal_smoke.resolve_frontier_command(
            _frontier_command("exec /gfxhandoff_kill {pid}"),
            {"gfx_owner_pid": "17"},
        )
        self.assertEqual(resolved.text, "exec /gfxhandoff_kill 17")
        self.assertIn("targeting PID 17", resolved.expected_pattern)
        stale_first_output = (
            '[elf] Loaded /gfxhandoff_kill as PID 17 '
            '(ELF32, 2048 bytes at 0x01100000)\n'
            '[gfxhandoff_kill] nested owner waiting for remote kill\n'
            '[PROCESS] Delayed kill skipped stale PID 17\n'
            '[PROCESS] Delayed killer PID 19 targeting PID 17 after 7000 ms\n'
            '[PROCESS] Killing PID 17 "/gfxhandoff_kill"\n'
        )
        self.assertIsNotNone(
            re.search(
                resolved.expected_pattern,
                stale_first_output,
                re.S | re.M,
            )
        )
        stale_before_owner_output = (
            '[elf] Loaded /gfxhandoff_kill as PID 17 '
            '(ELF32, 2048 bytes at 0x01100000)\n'
            '[PROCESS] Delayed kill skipped stale PID 17\n'
            '[gfxhandoff_kill] nested owner waiting for remote kill\n'
            '[PROCESS] Delayed killer PID 19 targeting PID 17 after 7000 ms\n'
            '[PROCESS] Killing PID 17 "/gfxhandoff_kill"\n'
        )
        self.assertIsNotNone(
            re.search(
                resolved.expected_pattern,
                stale_before_owner_output,
                re.S | re.M,
            )
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "needs missing capture",
        ):
            gui_terminal_smoke.resolve_frontier_command(
                _frontier_command("exec /gfxhandoff_kill {pid}"),
                {},
            )

        captures = {"gfx_owner_pid": "17"}
        for command, sample in zip(commands, _frontier_command_outputs()):
            with self.subTest(command=command.text):
                resolved_command = gui_terminal_smoke.resolve_frontier_command(
                    command,
                    captures,
                )
                self.assertIsNotNone(
                    re.search(
                        resolved_command.expected_pattern,
                        sample,
                        re.S | re.M,
                    )
                )

    def test_gui_disassembly_mirrors_its_listing_to_serial(self):
        source = (
            REPO_ROOT / "kernel" / "lang" / "shell.cc"
        ).read_text(encoding="utf-8")

        router = re.search(
            r"static void shell_route_print\(const char \*s,\s*"
            r"int mirror_gui_to_serial\) \{(?P<body>.*?)\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(router)
        body = router.group("body")
        self.assertRegex(
            body,
            re.compile(
                r"if \(shell_output_write_current\(s, \(uint32_t\)strlen\(s\)\)\)"
                r"\s+return;\s+if \(redir_active && redir_buf\) \{.*?"
                r"\s+return;\s+\}\s+if \(output_mode == SHELL_OUTPUT_GUI\)"
                r" \{\s+shell_gui_print\(s\);\s+"
                r"if \(mirror_gui_to_serial\)\s+serial_write_string\(s\);"
                r"\s+\} else \{\s+print\(s\);\s+\}",
                re.S,
            ),
        )
        self.assertTrue(_c_spelling_ranges(source,
            "static void shell_print(const char *s) {\n"
            "  shell_route_print(s, 0);\n"
            "}"))
        self.assertTrue(_c_spelling_ranges(source,
            "static void shell_dis_print(const char *s) {\n"
            "  shell_route_print(s, 1);\n"
            "}"))
        self.assertEqual(
            source.count("cupidc_dis(rpath, shell_dis_print);"),
            3,
        )
        self.assertEqual(
            source.count("dis_elf(rpath, shell_dis_print);"),
            2,
        )
        self.assertNotIn("cupidc_dis(rpath, shell_print);", source)
        self.assertNotIn("dis_elf(rpath, shell_print);", source)

    def test_fpaug_disassembly_requires_canonical_parity_setcc(self):
        command = _frontier_command("dis /bin/test_fpaug.cc")
        sample = _frontier_command_output("dis /bin/test_fpaug.cc")

        self.assertIsNotNone(
            re.search(command.expected_pattern, sample, re.S | re.M)
        )
        for spelling in ("setnp dl", "and al, dl", "setp dl", "or al, dl"):
            with self.subTest(spelling=spelling):
                self.assertIsNone(
                    re.search(
                        command.expected_pattern,
                        sample.replace(spelling, "db 0x0F"),
                        re.S | re.M,
                    )
                )

        source = (REPO_ROOT / "bin" / "test_fpaug.cc").read_text(
            encoding="utf-8"
        )
        self.assertLess(len(source.encode("utf-8")), 4096)
        for spelling in (
            "int fpaug_equal(double left, double right)",
            "int fpaug_not_equal(double left, double right)",
            "int fpaug_truth(double value)",
        ):
            with self.subTest(spelling=spelling):
                self.assertIn(spelling, source)

    def test_fpaug_runtime_requires_parity_semantics(self):
        command = _frontier_command("/bin/test_fpaug.cc")
        sample = _frontier_command_output("/bin/test_fpaug.cc")

        self.assertIsNotNone(
            re.search(command.expected_pattern, sample, re.S | re.M)
        )
        self.assertIn(
            "[test_fpaug-parity] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        self.assertIn(
            "FAIL test_fpaug",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_godsong_publishes_readiness_at_the_first_popup_boundary(self):
        source = (REPO_ROOT / "bin" / "godsong.cc").read_text(
            encoding="utf-8"
        )

        self.assertTrue(_c_spelling_ranges(source,
            'serial_printf("[godsong] settings ready\\n");\n'
            "  c = popup_menu(220, 110, (void*)items, 3);"))
        popup = (REPO_ROOT / "kernel" / "gfx" / "gfx2d.cc").read_text(
            encoding="utf-8"
        ).split("int gfx2d_popup_menu(", 1)[1]
        self.assertLess(
            popup.index("gfx2d_shared_writer_begin()"),
            popup.index('[gfx2d] popup input ready'),
        )

    def test_gfxgui_frontier_requires_aot_and_runtime_evidence(self):
        aot = _frontier_command(
            "ccc /bin/gfxgui_test.cc -o /gfxgui_test"
        )
        aot_output = _frontier_command_output(aot.text)
        self.assertIsNotNone(
            re.search(aot.expected_pattern, aot_output, re.S | re.M)
        )
        self.assertIsNotNone(
            re.search(
                aot.expected_pattern,
                aot_output.replace("entry=0x", "entry=0x0x"),
                re.S | re.M,
            )
        )
        for marker in (
            "[cupidc] AOT compile: /bin/gfxgui_test.cc",
            "[cupidc] Wrote ELF: /gfxgui_test",
        ):
            with self.subTest(command="AOT", marker=marker):
                self.assertIsNone(
                    re.search(
                        aot.expected_pattern,
                        aot_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        runtime = _frontier_command("/bin/gfxgui_test.cc")
        runtime_output = _frontier_command_output(runtime.text)
        self.assertIsNotNone(
            re.search(
                runtime.expected_pattern,
                runtime_output,
                re.S | re.M,
            )
        )
        for marker in (
            "[gfxgui_test] init",
            "[gfxgui_test] assets ready",
            "[gfxgui_test] fullscreen",
            "[gfxgui_test] font ready",
            "[gfxgui_test] surface ready",
            "[gfxgui_test] transform ready",
            "[gfxgui_test] frame 0 done",
            "[gfxgui_test] frame 240 done",
            "[gfxgui_test] done",
            "[cupidc] JIT execution complete",
        ):
            with self.subTest(command="runtime", marker=marker):
                self.assertIsNone(
                    re.search(
                        runtime.expected_pattern,
                        runtime_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        external = gui_terminal_smoke.resolve_frontier_command(
            _frontier_command("exec /gfxgui_test"),
            {"gfx_owner_pid": "17"},
        )
        external_output = _frontier_command_output(external.text)
        self.assertIsNotNone(
            re.search(
                external.expected_pattern,
                external_output,
                re.S | re.M,
            )
        )
        self.assertIn("[elf] Loaded /gfxgui_test as PID", external_output)

    def test_gfxgui_frontier_uses_a_workload_specific_timeout(self):
        aot = _frontier_command(
            "ccc /bin/gfxgui_test.cc -o /gfxgui_test"
        )
        runtime = _frontier_command("/bin/gfxgui_test.cc")
        external = _frontier_command("exec /gfxgui_test")
        self.assertEqual(aot.timeout_seconds, 180.0)
        self.assertEqual(runtime.timeout_seconds, 300.0)
        self.assertEqual(external.timeout_seconds, 300.0)

    def test_gfxgui_program_publishes_runtime_markers_to_serial(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        for marker in ("init", "done"):
            with self.subTest(marker=marker):
                self.assertIn(
                    f'serial_printf("[gfxgui_test] {marker}\\n");',
                    source,
                )
        for marker in (
            "assets ready",
            "fullscreen",
            "font ready",
            "surface ready",
            "transform ready",
            "frame %d begin",
            "frame %d done",
        ):
            with self.subTest(marker=marker):
                self.assertIn(f"[gfxgui_test] {marker}", source)
        for path in (
            "/gfxgui_test.theme",
            "/gfxgui_test.bmp",
            "/gfxgui_test.fnt",
        ):
            with self.subTest(path=path):
                self.assertIn(path, source)
        self.assertNotIn("/home/gfxgui_test", source)

    def test_gfxgui_program_requires_each_runtime_asset(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        for operation in (
            'ui_theme_save("/gfxgui_test.theme")',
            'ui_theme_load("/gfxgui_test.theme")',
            'make_test_bmp("/gfxgui_test.bmp")',
            'gfx2d_image_load("/gfxgui_test.bmp")',
            'make_test_font("/gfxgui_test.fnt")',
            'gfx2d_font_load("/gfxgui_test.fnt")',
            "gfx2d_surface_alloc(96, 96)",
        ):
            with self.subTest(operation=operation):
                self.assertIn(operation, source)
        self.assertIn("[gfxgui_test] FAIL", source)
        self.assertIn("if (surf < 0)", source)

    def test_gfxgui_program_checks_font_and_filtered_surface_pixels(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        self.assertIn("gfx2d_blend_mode(0);", source)
        self.assertIn('gfx2d_text_ex(16, 16, "A",', source)
        self.assertIn("glyph[0] = 0xFF;", source)
        self.assertIn("gfx2d_getpixel(16, 16)", source)
        self.assertIn("[gfxgui_test] FAIL font pixel", source)
        self.assertIn("gfx2d_rect_fill(3, 3, 3, 3, 0x000000)", source)
        self.assertIn("gfx2d_pixel(4, 4, 0xFFFFFF)", source)
        self.assertIn("gfx2d_getpixel(4, 4)", source)
        self.assertIn("0x001C1C1C", source)
        self.assertIn("[gfxgui_test] FAIL surface blur pixel", source)
        self.assertIn("gfx2d_pixel(4, 4, 0x123456)", source)
        self.assertIn("0x00123456", source)
        self.assertIn("[gfxgui_test] FAIL surface isolation", source)

    def test_gfxgui_program_checks_linear_transform_and_stack_restore(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        self.assertIn("gfx2d_rotate(90);", source)
        self.assertIn("gfx2d_transform_point(2, 3, &ox, &oy);", source)
        self.assertIn("ox != 91 || oy != 104", source)
        self.assertIn("[gfxgui_test] FAIL transform linear", source)
        self.assertIn("[gfxgui_test] FAIL transform restore", source)
        self.assertIn("gfx2d_getpixel(484, 150)", source)
        self.assertIn("0x00BC809E", source)
        self.assertIn("[gfxgui_test] FAIL transformed scale pixel", source)

    def test_gfxgui_non_gui_mode_is_an_immediate_serial_failure(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")
        gui_guard, next_block = c_block(source, "if (!is_gui_mode())")
        self.assertIn(
            "[gfxgui_test] FAIL requires GUI mode",
            source[gui_guard:next_block],
        )

    def test_doom_recovery_requires_both_failures_before_a_fresh_ls(self):
        commands = gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS
        command_texts = [command.text for command in commands]
        no_wad_index = command_texts.index("doom")
        missing_iwad_index = command_texts.index(
            "doom -iwad /disk/missing.wad"
        )
        ls_indexes = [
            index
            for index, text in enumerate(command_texts)
            if text == "ls"
        ]

        self.assertEqual(len(ls_indexes), 2)
        self.assertEqual(
            [no_wad_index, missing_iwad_index, ls_indexes[1]],
            [ls_indexes[1] - 2, ls_indexes[1] - 1, ls_indexes[1]],
        )

        no_wad = _frontier_command("doom")
        no_wad_output = _frontier_command_output("doom")
        for marker in (
            "doom: no WAD found in /disk/wads/ or /home/doom/.",
            "try: doom -iwad /path/to/your.wad",
        ):
            with self.subTest(command="doom", marker=marker):
                self.assertIsNone(
                    re.search(
                        no_wad.expected_pattern,
                        no_wad_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        missing_iwad = _frontier_command(
            "doom -iwad /disk/missing.wad"
        )
        missing_iwad_output = _frontier_command_output(
            "doom -iwad /disk/missing.wad"
        )
        for marker in (
            "IWAD file '/disk/missing.wad' not found!",
            "[doom] returned to shell",
        ):
            with self.subTest(command="missing IWAD", marker=marker):
                self.assertIsNone(
                    re.search(
                        missing_iwad.expected_pattern,
                        missing_iwad_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )

        post_doom_ls = _frontier_command("ls", occurrence=1)
        post_doom_ls_output = _frontier_command_output(
            "ls",
            occurrence=1,
        )
        self.assertIsNotNone(
            re.search(
                post_doom_ls.expected_pattern,
                post_doom_ls_output,
                re.S | re.M,
            )
        )

    def test_dglibc_command_requires_every_filesystem_boundary(self):
        command = _frontier_command("dglibc_test")
        expected = command.expected_pattern
        sample = _frontier_command_output("dglibc_test")

        self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
        for fragment in (
            "[PASS] dglibc snprintf",
            "[PASS] dglibc malloc/free",
            "[PASS] dglibc setjmp/longjmp and exit envelope",
            "[PASS] dglibc checked integer parsing",
            "[PASS] dglibc Doom exit callback lifecycle",
            "[PASS] dglibc Doom path resolution",
            "[PASS] dglibc shared errno bridge",
            "[PASS] dglibc Doom config round trip",
            "[PASS] dglibc synthetic config filesystem bridge",
            "[PASS] dglibc synthetic save filesystem bridge",
            "[PASS] dglibc VFS rename boundaries",
            "[PASS] dglibc VFS copy boundaries",
            "[PASS] dglibc block cache failure boundary",
            "[PASS] dglibc RamFS size boundary",
            "[PASS] dglibc FAT directory collision",
            "[PASS] dglibc FAT read boundary",
            "[PASS] dglibc FAT handle exhaustion",
            "[PASS] dglibc FAT busy replacement",
            "[PASS] dglibc FAT 8.3 path boundary",
            "[PASS] dglibc HomeFS mount boundary",
            "[PASS] dglibc HomeFS depth boundary",
            "[PASS] dglibc HomeFS batch boundary",
            "[PASS] dglibc_test",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )

        self.assertIn(
            "[FAIL] dglibc",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_browser_number_selftest_requires_every_boundary(self):
        command = _frontier_command("browser --selftest")
        expected = command.expected_pattern
        sample = _frontier_command_output("browser --selftest")

        self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
        for fragment in (
            "[js] parse error: js: expected exponent digits",
            "[js] parse error: js: expected hexadecimal digits",
            "[js] parse error: js: invalid binary digit",
            "[js] parse error: js: invalid octal digit",
            "[js] parse error: js: invalid numeric separator",
            "[js] parse error: js: identifier follows numeric literal",
            "close=1",
            "large=1",
            "negzero=1",
            "nan=1",
            "truth=1",
            "nanformat=1",
            "posinfformat=1",
            "neginfformat=1",
            "literal=1",
            "signedexp=1",
            "upperexp=1",
            "order=1",
            "divide=1",
            "divideassign=1",
            "remainder=1",
            "expcap=1",
            "radix=1",
            "separators=1",
            "tonumber=1",
            "looseeq=1",
            "stringrel=1",
            "largefmod=1",
            "modassign=1",
            "strplusassign=1",
            "reject=1",
            "recovery=1",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )

        self.assertIn(
            "[browser-js-number] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_unary_command_requires_value_type_error_and_recovery_evidence(
        self,
    ):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        for fragment in (
            (
                "[cupidc] error (line 1): "
                "unary sign requires an arithmetic scalar operand\n"
            ),
            "float=-15",
            "double=-9",
            "zero=0x80000000",
            "plus=9",
            "reject=1",
            "recovery=1",
            "PASS feature13_double\n",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )
        self.assertIn(
            "[feature13-unary] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        self.assertEqual(
            command.allowed_failure_pattern,
            r"^" + re.escape(command.allowed_failure_literal) + r"\r?\n",
        )
        self.assertEqual(
            command.allowed_failure_literal,
            (
                "[cupidc] error (line 1): "
                "unary sign requires an arithmetic scalar operand"
            ),
        )
        self.assertEqual(
            command.allowed_failure_context_pattern,
            (
                r"^"
                + re.escape(
                    "[cupidc] JIT compile: /bin/feature13_double.cc"
                )
                + r"\r?$"
            ),
        )

    def test_feature14_requires_operator_array_matrix_minmax_and_nan_evidence(self):
        command = _frontier_command("/bin/feature14_simd.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature14_simd.cc")

        for fragment in (
            "[feature14-operator] PASS float=4 double=4\n",
            (
                "[feature14-array] PASS global=2 local=2 static=2 "
                "sizeof=16 index=1\n"
            ),
            (
                "[feature14-matrix] PASS global=2 local=2 static=2 "
                "sizes=8 index=6 unevaluated=2 canary=4\n"
            ),
            (
                "[feature14-update] PASS direct=6 leaves=3 once=6 "
                "payload=8\n"
            ),
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6\n",
            "[feature14-callback] PASS float4=4 double2=2 calls=2\n",
            "[feature14-callback-typedef] PASS float4=4 calls=1\n",
            (
                "[feature14-callback-global] PASS float4=4 initialized=1 "
                "assigned=1 cleared=1 calls=2\n"
            ),
            (
                "[feature14-callback-raw] PASS initialized=1 parameter=1 "
                "cleared=1 reassigned=1 calls=3\n"
            ),
            (
                "[feature14-callback-raw-array] PASS modes=2 phases=3 "
                "calls=12 stored=1 persistent=1\n"
            ),
            (
                "[feature14-callback-raw-automatic-array] PASS zeroed=4 "
                "initialized=2 assigned=1 copied=2 later=1 calls=4\n"
            ),
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43\n",
            "[feature14-callback-binding] PASS call=1 ignored=1 callback=0\n",
            (
                "[feature14-callback-automatic] PASS local=4 method=4 "
                "calls=2\n"
            ),
            (
                "[feature14-callback-field] PASS stored=1 copied=1 "
                "cleared=1 float4=4 calls=1\n"
            ),
            (
                "[feature14-callback-field-call] PASS typedef=1 raw=1 "
                "float4=4 once=1 calls=2\n"
            ),
            "[feature14-minmax] PASS nan=4 signed_zero=4\n",
            (
                "[feature14-nan] PASS float_left=4 float_right=0 "
                "double_left=4 double_right=0\n"
            ),
            "PASS feature14_simd\n",
        ):
            with self.subTest(fragment=fragment):
                self.assertIsNone(
                    re.search(
                        expected,
                        sample.replace(fragment, ""),
                        re.S | re.M,
                    )
                )

        for failure in (
            "[feature14-operator] FAIL",
            "[feature14-array] FAIL",
            "[feature14-matrix] FAIL",
            "[feature14-update] FAIL",
            "[feature14-call] FAIL",
            "[feature14-callback] FAIL",
            "[feature14-callback-typedef] FAIL",
            "[feature14-callback-global] FAIL",
            "[feature14-callback-raw] FAIL",
            "[feature14-callback-automatic] FAIL",
            "[feature14-callback-field] FAIL",
            "[feature14-callback-field-call] FAIL",
            "[feature14-callback-raw-array] FAIL",
            "[feature14-callback-raw-automatic-array] FAIL",
            "[feature14-callback-nested] FAIL",
            "[feature14-callback-binding] FAIL",
            "[feature14-minmax] FAIL",
            "[feature14-nan] FAIL",
            "FAIL feature14_simd",
        ):
            self.assertIn(
                failure,
                gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
            )

        source = (
            REPO_ROOT / "bin" / "feature14_simd.cc"
        ).read_text(encoding="utf-8")
        for spelling in (
            "float4 feature14_global_floats[3]",
            "double2 feature14_global_doubles[2]",
            "float4 feature14_global_matrix[2][2]",
            "double2 feature14_global_cube[2][2][2]",
            "direct_float = a + b",
            "direct_double = dv / dpos",
            "feature14_global_floats[1] += a",
            "saved_doubles[1].y",
            "local_matrix[1][0] += b",
            "saved_cube[1][1][0] /= local_step",
            "feature14_global_cube[feature14_next_outer()]",
            "feature14_global_cube[feature14_next_outer()][feature14_next_middle()]",
            "old_float = feature14_update_float++",
            "new_matrix = --feature14_global_matrix[matrix_outer++]",
            "old_cube = feature14_global_cube[cube_outer++][cube_middle++]",
            "return feature14_merge_float4(",
            "feature14_merge_float4(first, 7, second)",
            "return feature14_merge_double2(",
            "feature14_merge_double2(first, 13, second)",
            "float4 (*float_callback)(float4 left, int marker, float4 right)",
            "double2 (*double_callback)(double2 left, int marker, double2 right)",
            "typedef float4 (*feature14_float_callback_t)(float4 left, int marker,",
            "feature14_float_callback_t feature14_global_callback = "
            "feature14_merge_float4",
            "int (*feature14_raw_callback)(int) = feature14_raw_target;",
            "int feature14_invoke_raw_callback(int (*callback)(int), int value)",
            "initialized_result = feature14_raw_callback(1);",
            "parameter_result = feature14_invoke_raw_callback(",
            "feature14_raw_target, 2);",
            "feature14_raw_callback = 0;",
            "if (feature14_raw_callback != 0) return 2;",
            "feature14_raw_callback = feature14_raw_target;",
            "reassigned_result = feature14_raw_callback(3);",
            "float4 feature14_invoke_float_callback(",
            "feature14_float_callback_t callback, float4 left, int marker,",
            "result = feature14_invoke_float_callback(",
            "feature14_merge_float4, first, 7, second);",
            "feature14_set_global_callback(feature14_merge_float4);",
            "initialized_result = feature14_global_callback(first, 7, second);",
            "assigned_result = feature14_global_callback(first, 7, second);",
            "feature14_set_global_callback(0);",
            "class Feature14CallbackInvoker",
            "feature14_float_callback_t callback = feature14_merge_float4;",
            "method_result = invoker.Invoke(",
            "sizeof(feature14_global_matrix[feature14_sizeof_index()])",
            "_mm_min_ps(edge_float_first, edge_float_second)",
            "_mm_max_pd(edge_double_first, edge_double_second)",
            "float_result = float_left + float_right",
            "float_result = _mm_mul_ps(float_left, float_right)",
            "double_result = double_left + double_right",
            "double_result = _mm_mul_pd(double_left, double_right)",
            "[feature14-nan] PASS float_left=%d float_right=%d "
            "double_left=%d double_right=%d",
            "[feature14-matrix] PASS global=2 local=2 static=2 "
            "sizes=8 index=6 unevaluated=2 canary=4",
            "[feature14-update] PASS direct=6 leaves=3 once=6 payload=8",
            "[feature14-call] PASS float4=4 double2=2 nested=2 calls=6",
            "[feature14-callback] PASS float4=4 double2=2 calls=2",
            "[feature14-callback-typedef] PASS float4=4 calls=1",
            "[feature14-callback-global] PASS float4=4 initialized=1 "
            "assigned=1 cleared=1 calls=2",
            "[feature14-callback-raw] PASS initialized=1 parameter=1 "
            "cleared=1 reassigned=1 calls=3",
            "[feature14-callback-raw-array] PASS modes=2 phases=3 "
            "calls=12 stored=1 persistent=1",
            "int (*automatic_callbacks[4])(int, int, int) =",
            "int (*automatic_zeroed[2])(int, int, int);",
            "int (*automatic_copied)(int, int, int) =",
            "automatic_callbacks[2] = feature14_wipe_replacement;",
            "[feature14-callback-raw-automatic-array] PASS zeroed=4 "
            "initialized=2 assigned=1 copied=2 later=1 calls=4",
            "void (*set_drawer)(int, void (*)(int, int)) =",
            "void feature14_nested_install(int handle, "
            "void (*drawer)(int, int))",
            "set_drawer(4, feature14_nested_draw);",
            "set_icon_drawer(-1, feature14_nested_draw);",
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43",
            "[feature14-callback-binding] PASS call=1 ignored=1 callback=0",
            "static int (*wipes[])(int, int, int)",
            "(*wipes[wipeno*3])(4, 2, 1)",
            "(*wipes[wipeno*3+1])(4, 2, 1)",
            "(*wipes[wipeno*3+2])(4, 2, 1)",
            "[feature14-callback-automatic] PASS local=4 method=4 calls=2",
            "[feature14-callback-field] PASS stored=1 copied=1 cleared=1 "
            "float4=4 calls=1",
            "[feature14-callback-field-call] PASS typedef=1 raw=1 "
            "float4=4 once=1 calls=2",
        ):
            if spelling.startswith("["):
                self.assertIn(spelling, c_literal_text(source))
            else:
                self.assertTrue(_c_spelling_ranges(source, spelling), spelling)

    def test_feature14_raw_callback_failure_cannot_hide_behind_pass_evidence(self):
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        poisoned = sample.replace(
            "[feature14-callback-raw] PASS",
            "[feature14-callback-raw] FAIL\n[feature14-callback-raw] PASS",
            1,
        )

        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(poisoned),
            "[feature14-callback-raw] FAIL",
        )

    def test_feature14_nested_callback_failure_cannot_hide_behind_pass_evidence(self):
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        poisoned = sample.replace(
            "[feature14-callback-nested] PASS",
            "[feature14-callback-nested] FAIL\n"
            "[feature14-callback-nested] PASS",
            1,
        )

        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(poisoned),
            "[feature14-callback-nested] FAIL",
        )

    def test_feature14_callback_binding_failure_cannot_hide_behind_pass_evidence(self):
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        poisoned = sample.replace(
            "[feature14-callback-binding] PASS",
            "[feature14-callback-binding] FAIL\n"
            "[feature14-callback-binding] PASS",
            1,
        )

        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(poisoned),
            "[feature14-callback-binding] FAIL",
        )

    def test_feature14_nested_callback_evidence_keeps_callback_order(self):
        command = _frontier_command("/bin/feature14_simd.cc")
        sample = _frontier_command_output("/bin/feature14_simd.cc")
        automatic_array = (
            "[feature14-callback-raw-automatic-array] PASS zeroed=4 "
            "initialized=2 assigned=1 copied=2 later=1 calls=4\n"
        )
        nested = (
            "[feature14-callback-nested] PASS outer=1 inner=1 value=43\n"
        )
        reordered = sample.replace(
            automatic_array + nested,
            nested + automatic_array,
            1,
        )

        self.assertIsNone(
            re.search(command.expected_pattern, reordered, re.S | re.M)
        )

    def test_feature13_requires_all_scalar_comparison_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-compare] PASS ordered=6 mixed=4 "
            "zero=2 unordered=6\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-compare] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "1.0 == 1.0",
            "1.0 != 2.0",
            "1.0 < 2.0",
            "2.0 > 1.0",
            "1.0 <= 1.0",
            "2.0 >= 2.0",
            "1.0f < 2.0",
            "negative_zero == 0.0",
            "compare_nan != compare_nan",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_all_scalar_truth_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-truth] PASS zero=2 nonzero=3 "
            "control=255 nan=1\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-truth] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "!truth_zero",
            "!truth_negative_zero",
            "!!truth_nan",
            "if (truth_nonzero)",
            "truth_nonzero ? 4 : 1000",
            "while (truth_while)",
            "for (; truth_for; truth_for = 0.0)",
            "} while (truth_do);",
            "if (truth_nan)",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_all_scalar_update_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-update] PASS local=48 global=40 "
            "for=3 zero=0x80000000 nan=2\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-update] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "update_float_old = (update_float)++",
            "update_float_new = ++(update_float)",
            "update_float--;",
            "update_double_old = ((update_double))--",
            "update_double_new = --((update_double))",
            "update_double++;",
            "feature13_update_global_float++;",
            "--feature13_update_global_double;",
            "update_global_old = feature13_update_global_double--",
            "update_global_new = ++feature13_update_global_float",
            "for (; update_iterations < 3; update_for++)",
            "update_zero_old = update_negative_zero++",
            "update_nan_old = update_nan++",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_typed_floating_lvalue_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-lvalue] PASS array=42 pointer=13 "
            "record=26 sizes=56 unevaluated=1\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-lvalue] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "float feature13_lvalue_global[2][3]",
            "float *row = &matrix[1][0]",
            "double *cube_cell = &cube[1][0][0]",
            "float *returned_row = feature13_lvalue_row()",
            "returned_row[2] *= 2.0f",
            "record->gain += 0.5f",
            "record->bias *= 2.0",
            "sizeof(matrix[index++])",
            "sizeof(cube[0])",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_indirect_floating_update_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")

        self.assertIn(r"\[feature13-indirect-update\] PASS", expected)
        self.assertIn(
            "[feature13-indirect-update] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        for expression in (
            "(*feature13_indirect_float_pointer(&pointer_value))++",
            "++indirect_values[feature13_indirect_index()]",
            "feature13_indirect_record(&indirect_record)->bias--",
            "*(int*)&pointer_old",
        ):
            with self.subTest(expression=expression):
                self.assertTrue(_c_spelling_ranges(source, expression), expression)

    def test_feature13_requires_aot_and_external_update_evidence(self):
        aot = _frontier_command(
            "ccc /bin/feature13_derived_aot.cc -o /feature13_derived_aot"
        )
        aot_output = _frontier_command_output(aot.text)
        external = _frontier_command("exec /feature13_derived_aot")
        external_output = _frontier_command_output(external.text)
        self.assertIn(
            "[feature13-derived-aot] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        self.assertIsNotNone(
            re.search(aot.expected_pattern, aot_output, re.S | re.M)
        )
        self.assertIsNotNone(
            re.search(
                external.expected_pattern,
                external_output,
                re.S | re.M,
            )
        )
        for marker in (
            "[elf] Loaded /feature13_derived_aot as PID",
            "[feature13-derived-aot] PASS",
            '[PROCESS] PID 11 "/feature13_derived_aot" exiting',
        ):
            with self.subTest(marker=marker):
                self.assertIsNone(
                    re.search(
                        external.expected_pattern,
                        external_output.replace(marker, ""),
                        re.S | re.M,
                    )
                )
        self.assertIsNone(
            re.search(
                external.expected_pattern,
                external_output.replace(
                    '[PROCESS] PID 11 "/feature13_derived_aot" exiting',
                    '[PROCESS] PID 12 "/feature13_derived_aot" exiting',
                ),
                re.S | re.M,
            )
        )

    def test_feature13_requires_unsigned_word_runtime_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = (
            "[feature13-unsigned] PASS conversions=4 "
            "remainders=2 once=1\n"
        )

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        for failure in (
            "[feature13-unsigned-convert] FAIL",
            "[feature13-unsigned-remainder] FAIL",
        ):
            self.assertIn(
                failure,
                gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
            )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for expression in (
            "float unsigned_float_below = 2147483520.0f",
            "float unsigned_float_above = 2147483904.0f",
            "double unsigned_double_below = 2147483647.75",
            "double unsigned_double_above = 4294967295.75",
            "(uint32_t)unsigned_float_below",
            "(uint32_t)unsigned_double_below",
            "signed_remainder %= 6",
            (
                "feature13_unsigned_values["
                "feature13_unsigned_next_index()] %= 7"
            ),
            "feature13_unsigned_index_calls == 1",
            "[feature13-unsigned] PASS conversions=4 remainders=2 once=1",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, source)

    def test_feature13_requires_mixed_width_user_call_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = "[feature13-call] PASS checks=10\n"

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-call] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r"int feature13_within\(\s*double actual,\s*double expected,\s*"
            r"double scale,\s*int max_scaled_error\)",
        )
        self.assertEqual(source.count("feature13_within("), 11)
        self.assertIn("double ex = exp(1.0);", source)
        self.assertIn(
            "return scaled >= 0 && scaled <= max_scaled_error;",
            source,
        )
        self.assertNotIn("known bug", source)
        self.assertNotIn("calling-convention edge cases", source)

    def test_feature13_requires_exact_decimal_literal_evidence(self):
        command = _frontier_command("/bin/feature13_double.cc")
        expected = command.expected_pattern
        sample = _frontier_command_output("/bin/feature13_double.cc")
        marker = "[feature13-literal] PASS double=2 float=2 edge=3\n"

        self.assertIsNone(
            re.search(
                expected,
                sample.replace(marker, ""),
                re.S | re.M,
            )
        )
        self.assertIn(
            "[feature13-literal] FAIL",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

        source = (
            REPO_ROOT / "bin" / "feature13_double.cc"
        ).read_text(encoding="utf-8")
        for literal in (
            "double literal_double = 0.75",
            "float literal_float = 0.75f",
            "1.00000000000000011102230246251565404236316680908203125",
            "1.000000059604644775390625f",
            "double literal_subnormal = 5e-324",
            "double literal_overflow = 1e400",
            "double literal_negative_zero = -0e-9999",
        ):
            with self.subTest(literal=literal):
                self.assertIn(literal, source)

    def test_libm_command_requires_total_and_pass_markers(self):
        expected = _frontier_command(
            "/bin/feature15_libm.cc"
        ).expected_pattern
        for marker in (
            "[feature15-x87] 7 range checks, 0 failed\n",
            "[feature15] 29 checks total, 0 failed\n",
            "PASS feature15_libm\n",
        ):
            with self.subTest(marker=marker):
                output = _frontier_command_output(
                    "/bin/feature15_libm.cc"
                ).replace(marker, "")
                self.assertIsNone(
                    re.search(expected, output, re.S | re.M)
                )
        self.assertIn(
            "FAIL feature15_libm",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_libm_guest_source_calls_the_production_math_path(self):
        source = (
            REPO_ROOT / "bin" / "feature15_libm.cc"
        ).read_text(encoding="utf-8")

        for function in (
            "sin(",
            "cos(",
            "tan(",
            "atan(",
            "sqrt(",
            "exp(",
            "expf(",
            "exp2(",
            "exp2f(",
            "log(",
            "pow(",
            "powf(",
            "sinh(",
            "fabs(",
        ):
            with self.subTest(function=function):
                self.assertIn(function, source)
        self.assertIn(
            '"[feature15] %d checks total, %d failed\\n"',
            source,
        )
        self.assertIn(
            '"[feature15-x87] 7 range checks, %d failed\\n"',
            source,
        )
        self.assertIn('"PASS feature15_libm\\n"', source)
        self.assertEqual(source.count("scaled != 0"), 29)
        self.assertNotIn("scaled > 0", source)
        self.assertIn("double neg_x = -1.5;", source)
        self.assertNotIn("0.0 - 1.5", source)

        libm_source = (
            REPO_ROOT / "kernel" / "cpu" / "libm.cc"
        ).read_text(encoding="utf-8")
        self.assertEqual(libm_source.count("fsubr  %st, %st(1)"), 4)
        self.assertEqual(libm_source.count("fsubr  %%st, %%st(1)"), 3)
        self.assertNotIn("fsub   %st, %st(1)", libm_source)
        self.assertNotIn("fsub   %%st, %%st(1)", libm_source)

    def test_iso_jpeg_fixture_is_a_byte_fixed_baseline_image(self):
        data = JPEG_FIXTURE.read_bytes()
        self.assertEqual(len(data), 331)
        self.assertEqual(
            hashlib.sha256(data).hexdigest(),
            "76aac1d6ee61f230d47cd6fef3ba1ea5"
            "0fe55f1a32634c109489cb3b8d931957",
        )
        sof0 = data.index(b"\xff\xc0")
        self.assertEqual(
            data[sof0:sof0 + 13],
            b"\xff\xc0\x00\x0b\x08\x00\x08\x00\x08\x01\x01\x11\x00",
        )
        self.assertNotIn(b"\xff\xc2", data)

    def test_system_image_tracks_the_iso_runtime_fixture(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        logical_makefile = re.sub(r"\\\r?\n[ \t]*", " ", makefile)
        rule = re.search(
            r"^\$\(OS_IMAGE\): ([^\n]+)$",
            logical_makefile,
            re.MULTILINE,
        )
        self.assertIsNotNone(rule)
        self.assertIn("test_iso/hello.iso", rule.group(1).split())

    def test_network_jit_and_aot_commands_require_the_native_binding_probe(self):
        marker = "PASS feature23_binding bindings=20 slots=4 unsigned=4 peer=1 halfwords=8 guards=2"
        for command in ("/bin/feature23_full_access.cc", "exec /feature23_binding_aot"):
            with self.subTest(command=command):
                expected = _frontier_command(command).expected_pattern
                sample = _frontier_command_output(command)
                self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
                self.assertIsNone(re.search(expected, sample.replace(marker, ""), re.S | re.M))
                self.assertIsNone(re.search(expected, sample.replace("bindings=20", "bindings=19"), re.S | re.M))

    def test_network_guest_requires_halfword_and_neighbor_guard_counts(self):
        for command in ("/bin/feature23_full_access.cc", "exec /feature23_binding_aot"):
            with self.subTest(command=command):
                expected = _frontier_command(command).expected_pattern
                sample = _frontier_command_output(command)
                self.assertIsNone(re.search(expected, sample.replace("halfwords=8", "halfwords=7"), re.S | re.M))
                self.assertIsNone(re.search(expected, sample.replace("guards=2", "guards=1"), re.S | re.M))

    def test_network_guest_requires_static_integer_initializer_evidence(self):
        marker = "PASS feature23_initializers arrays=5 calls=2"
        for command in ("/bin/feature23_full_access.cc", "exec /feature23_binding_aot"):
            with self.subTest(command=command):
                expected = _frontier_command(command).expected_pattern
                sample = _frontier_command_output(command)
                self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
                for replacement in ("", "PASS feature23_initializers arrays=4 calls=2", "PASS feature23_initializers arrays=5 calls=1"):
                    self.assertIsNone(re.search(expected, sample.replace(marker, replacement), re.S | re.M))

    def test_network_guest_requires_each_halfword_type_probe_count(self):
        marker = "PASS feature23_halfword_types strides=2 rows=3 broadcasts=4 casts=2"
        replacements = (
            "",
            marker.replace("strides=2", "strides=1"),
            marker.replace("rows=3", "rows=2"),
            marker.replace("broadcasts=4", "broadcasts=3"),
            marker.replace("casts=2", "casts=1"),
        )
        for command in ("/bin/feature23_full_access.cc", "exec /feature23_binding_aot"):
            with self.subTest(command=command):
                expected = _frontier_command(command).expected_pattern
                sample = _frontier_command_output(command)
                self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
                for replacement in replacements:
                    self.assertIsNone(re.search(expected, sample.replace(marker, replacement), re.S | re.M))

    def test_storage_commands_require_each_native_binding_probe(self):
        cases = (
            ("/bin/feature17_iso.cc", "PASS feature17_binding calls=9 bytes=8"),
            ("/bin/feature18_swap.cc", "PASS feature18_binding sizes=1 handles=5 bytes=8"),
        )
        for command, marker in cases:
            with self.subTest(command=command):
                expected = _frontier_command(command).expected_pattern
                sample = _frontier_command_output(command)
                if marker not in sample:
                    sample = sample.replace(
                        "PASS feature17_iso" if "feature17" in command else "PASS feature18_swap",
                        marker + "\n" + ("PASS feature17_iso" if "feature17" in command else "PASS feature18_swap"),
                    )
                self.assertIsNotNone(re.search(expected, sample, re.S | re.M))
                self.assertIsNone(re.search(expected, sample.replace(marker + "\n", ""), re.S | re.M))
                if "feature17" in command:
                    stale_count = sample.replace("calls=9 bytes=8", "calls=8 bytes=8")
                    self.assertIsNone(re.search(expected, stale_count, re.S | re.M))

    def test_iso_command_requires_jpeg_glyph_and_ramfs_markers(self):
        expected = _frontier_command(
            "/bin/feature17_iso.cc"
        ).expected_pattern
        for marker in (
            "PASS jpeg_decode_mem baseline 8x8 gray128\n",
            (
                "PASS glyph_rasterize Liberation Mono Q size37 "
                "width=22 cache=22\n"
            ),
            "PASS feature17_ramfs limit=1048576\n",
        ):
            with self.subTest(marker=marker):
                output = _frontier_command_output(
                    "/bin/feature17_iso.cc"
                ).replace(marker, "")
                self.assertIsNone(
                    re.search(expected, output, re.S | re.M)
                )
        mismatched_cache = _frontier_command_output(
            "/bin/feature17_iso.cc"
        ).replace(
            "width=22 cache=22",
            "width=22 cache=23",
        )
        self.assertIsNone(
            re.search(expected, mismatched_cache, re.S | re.M)
        )
        self.assertIn(
            "FAIL jpeg_decode_mem",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )
        self.assertIn(
            "FAIL glyph_rasterize",
            gui_terminal_contracts.FRONTIER_RUNTIME_REJECTED_MARKERS,
        )

    def test_iso_guest_source_calls_the_production_glyph_path(self):
        source = (
            REPO_ROOT / "bin" / "feature17_iso.cc"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'fontsys_match("Liberation Mono", 3, 400, 0)',
            source,
        )
        self.assertEqual(
            source.count('fontsys_run_width(face, 37, "Q", 1)'),
            2,
        )
        self.assertIn(
            "PASS glyph_rasterize Liberation Mono Q size37",
            source,
        )

    def test_iso_guest_source_checks_readdir_rock_ridge_names(self):
        source = (
            REPO_ROOT / "bin" / "feature17_iso.cc"
        ).read_text(encoding="utf-8")

        self.assertIn("int check_directory_names()", source)
        self.assertIn('vfs_open("/iso", 0)', source)
        self.assertIn("vfs_readdir(fd, ent)", source)
        self.assertIn(
            'strcmp(ent, "long_named_file.txt")',
            source,
        )
        self.assertIn(
            "PASS feature17_readdir names=6 "
            "long=long_named_file.txt",
            source,
        )
        command = next(
            entry
            for entry in gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS
            if entry.text == "/bin/feature17_iso.cc"
        )
        self.assertIn(
            "PASS feature17_readdir names=6",
            command.expected_pattern,
        )

if __name__ == "__main__":
    unittest.main()

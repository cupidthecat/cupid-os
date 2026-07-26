import re
import shutil
import unittest
from pathlib import Path

from tools import build_graph_audit


REPO_ROOT = Path(__file__).resolve().parents[1]


class ExternalUserRuntimeContractTests(unittest.TestCase):
    def test_external_print_marker_never_interpolates_caller_text(self):
        source = (REPO_ROOT / "kernel/core/syscall.cc").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("[elf-print]", source)
        self.assertRegex(
            source,
            re.compile(
                r'serial_printf\('
                r'"\[elf-syscall\] pid=%u op=print '
                r'bytes=%u fnv1a=0x%08x\\n",'
            ),
        )
        self.assertIsNone(
            re.search(
                r"serial_printf\([^;]*%s[^;]*\bstr\b",
                source,
                re.S,
            )
        )

    def test_integer_and_exit_markers_are_bound_to_the_running_pid(self):
        source = (REPO_ROOT / "kernel/core/syscall.cc").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            '"[elf-syscall] pid=%u op=print_int value=%u\\n"',
            source,
        )
        self.assertIn(
            '"[elf-syscall] pid=%u op=exit\\n"',
            source,
        )
        self.assertIn("syscall_table.print_int = syscall_print_int;", source)
        self.assertIn("syscall_table.exit = syscall_exit;", source)

    def test_normal_test_runs_both_production_frontiers(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")

        model = build_graph_audit._collect_build_model(
            REPO_ROOT, make, "test", "."
        )
        prerequisites = model.rules["test"].prerequisites
        self.assertIn("test-generated-cupidc-frontier", prerequisites)
        self.assertIn("test-user-cupidc-frontier", prerequisites)
        self.assertNotIn("test-user-cupidc-runtime", prerequisites)

    def test_runtime_gate_executes_all_three_programs_with_a_fixed_fixture(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")

        model = build_graph_audit._collect_build_model(
            REPO_ROOT, make, "test-user-cupidc-runtime", "."
        )
        runtime_rule = model.rules["test-user-cupidc-runtime"]
        self.assertIn("sync-user-runtime", runtime_rule.prerequisites)
        recipe = " ".join(runtime_rule.recipe)
        self.assertEqual(recipe.count("tools/gui_terminal_smoke.py"), 3)
        for name in ("hello", "ls", "cat"):
            self.assertIn(f'--command "exec /disk/{name}"', recipe)
            self.assertIn(
                f"$(USER_CUPIDC_RUNTIME_{name.upper()}_SUCCESS)",
                recipe,
            )
        self.assertIn(
            '--setup-command "cp /disk/catfix.txt /home/readme.txt"',
            recipe,
        )
        self.assertIn("--private-image", recipe)
        self.assertIn(
            "$(USER_CUPIDC_RUNTIME_CAT_SETUP_SUCCESS)",
            recipe,
        )

        fixture_recipe = " ".join(
            model.rules["sync-user-runtime"].recipe
        )
        self.assertIn(
            "$(USER_CUPIDC_RUNTIME_FIXTURE):/catfix.txt",
            fixture_recipe,
        )

    def test_runtime_patterns_require_vfs_output_and_pid_bound_completion(self):
        make = shutil.which("make")
        if make is None:
            self.skipTest("GNU Make is unavailable")

        names = (
            "USER_CUPIDC_RUNTIME_HELLO_SUCCESS",
            "USER_CUPIDC_RUNTIME_LS_SUCCESS",
            "USER_CUPIDC_RUNTIME_CAT_SETUP_SUCCESS",
            "USER_CUPIDC_RUNTIME_CAT_SUCCESS",
        )
        patterns = build_graph_audit._read_evaluated_make_variables(
            REPO_ROOT, make, names
        )

        hello_log = (
            "[shell_exec_cmd] prog='/disk/hello' "
            "rpath='/disk/hello' args=''\n"
            "[elf] Loaded /disk/hello as PID 4 "
            "(ELF32, 8196 bytes at 0x0x00f00000)\n"
            "[elf-syscall] pid=4 op=print bytes=27 "
            "fnv1a=0x6d2edfa6\n"
            "[elf-syscall] pid=4 op=print_int value=4\n"
            "[elf-syscall] pid=4 op=print_int value=13540\n"
            "[elf-syscall] pid=4 op=exit\n"
            '[PROCESS] PID 4 "/disk/hello" exiting\n'
        )
        hello_pattern = patterns["USER_CUPIDC_RUNTIME_HELLO_SUCCESS"]
        self.assertIsNotNone(re.search(hello_pattern, hello_log, re.S))
        self.assertIsNone(
            re.search(
                hello_pattern,
                hello_log.replace(
                    "pid=4 op=exit", "pid=5 op=exit"
                ),
                re.S,
            )
        )

        ls_log = (
            "[shell_exec_cmd] prog='/disk/ls' rpath='/disk/ls' args=''\n"
            "[elf] Loaded /disk/ls as PID 5 "
            "(ELF32, 12288 bytes at 0x0x00f00000)\n"
            "[elf-syscall] pid=5 op=print bytes=5 "
            "fnv1a=0xbd9adb9f\n"
            "[elf-syscall] pid=5 op=print bytes=4 "
            "fnv1a=0xd2c8c28e\n"
            "[elf-syscall] pid=5 op=print bytes=4 "
            "fnv1a=0x28eb34d2\n"
            "[elf-syscall] pid=5 op=print bytes=3 "
            "fnv1a=0x5acad8be\n"
            "[elf-syscall] pid=5 op=print bytes=4 "
            "fnv1a=0x456040a4\n"
            "[elf-syscall] pid=5 op=exit\n"
            '[PROCESS] PID 5 "/disk/ls" exiting\n'
        )
        ls_pattern = patterns["USER_CUPIDC_RUNTIME_LS_SUCCESS"]
        self.assertIsNotNone(re.search(ls_pattern, ls_log, re.S))
        self.assertIsNone(
            re.search(
                ls_pattern,
                ls_log.replace(
                    "[elf-syscall] pid=5 op=print bytes=4 "
                    "fnv1a=0x28eb34d2\n",
                    "",
                ),
                re.S,
            )
        )

        cat_log = (
            "[shell_exec_cmd] prog='/disk/cat' "
            "rpath='/disk/cat' args=''\n"
            "[elf] Loaded /disk/cat as PID 6 "
            "(ELF32, 10240 bytes at 0x0x00f00000)\n"
            "[elf-syscall] pid=6 op=print bytes=62 "
            "fnv1a=0xc12ed628\n"
            "[elf-syscall] pid=6 op=exit\n"
            '[PROCESS] PID 6 "/disk/cat" exiting\n'
        )
        cat_pattern = patterns["USER_CUPIDC_RUNTIME_CAT_SUCCESS"]
        self.assertIsNotNone(re.search(cat_pattern, cat_log, re.S))
        self.assertIsNone(
            re.search(
                cat_pattern,
                cat_log.replace(
                    "[elf-syscall] pid=6 op=exit\n",
                    "[elf-syscall] pid=999 op=exit\n"
                    "[elf-syscall] pid=6 op=exit\n",
                ),
                re.S,
            )
        )
        cat_setup_log = (
            "[cupidc] JIT compile: /bin/cp.cc\n"
            "[cupidc] JIT execution complete\n"
        )
        self.assertIsNotNone(
            re.search(
                patterns["USER_CUPIDC_RUNTIME_CAT_SETUP_SUCCESS"],
                cat_setup_log,
                re.S,
            )
        )

        ls_source = (REPO_ROOT / "user/examples/ls.cc").read_text(
            encoding="utf-8"
        )
        cat_source = (REPO_ROOT / "user/examples/cat.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn("const char *path = shell_get_cwd();", ls_source)
        self.assertNotIn('const char *path = "/disk";', ls_source)
        self.assertIn(
            'const char *path = "/home/readme.txt";',
            cat_source,
        )


if __name__ == "__main__":
    unittest.main()

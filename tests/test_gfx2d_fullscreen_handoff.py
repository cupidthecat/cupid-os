import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HANDOFF_HEADER = REPO_ROOT / "kernel" / "gfx" / "gfx2d_handoff.h"
GFX2D_SOURCE = REPO_ROOT / "kernel" / "gfx" / "gfx2d.cc"
ASSETS_SOURCE = REPO_ROOT / "kernel" / "gfx" / "gfx2d_assets.cc"
ICONS_SOURCE = REPO_ROOT / "kernel" / "gfx" / "gfx2d_icons.cc"
FONTSYS_SOURCE = REPO_ROOT / "kernel" / "gfx" / "fontsys.cc"
TRANSFORM_SOURCE = REPO_ROOT / "kernel" / "gfx" / "gfx2d_transform.cc"
THEMES_SOURCE = REPO_ROOT / "kernel" / "gui" / "gui_themes.cc"
THEMES_HEADER = REPO_ROOT / "kernel" / "gui" / "gui_themes.h"
EVENTS_SOURCE = REPO_ROOT / "kernel" / "gui" / "gui_events.cc"
DESKTOP_SOURCE = REPO_ROOT / "kernel" / "gui" / "desktop.cc"
GUI_SOURCE = REPO_ROOT / "kernel" / "gui" / "gui.cc"
SHELL_SOURCE = REPO_ROOT / "kernel" / "lang" / "shell.cc"
PROCESS_SOURCE = REPO_ROOT / "kernel" / "core" / "process.cc"
KERNEL_SOURCE = REPO_ROOT / "kernel" / "core" / "kernel.cc"
CUPIDC_SOURCE = REPO_ROOT / "kernel" / "lang" / "cupidc.cc"
ASM_SOURCE = REPO_ROOT / "kernel" / "lang" / "as.cc"
GFXGUI_SOURCE = REPO_ROOT / "bin" / "gfxgui_test.cc"
EXIT_SOURCE = REPO_ROOT / "bin" / "gfxhandoff_exit.cc"
KILL_SOURCE = REPO_ROOT / "bin" / "gfxhandoff_kill.cc"
BMPTEST_SOURCE = REPO_ROOT / "bin" / "bmptest.cc"
DOOM_SOURCE = REPO_ROOT / "kernel" / "doom" / "doomgeneric_cupidos.cc"
PAINT_SOURCE = REPO_ROOT / "bin" / "paint.cc"
PAINT_DEMO_SOURCE = REPO_ROOT / "demos" / "paint.cc"
PARITY_GFX2D_SOURCE = REPO_ROOT / "demos" / "parity_gfx2d.asm"
SMOKE_SOURCE = REPO_ROOT / "tools" / "gui_terminal_smoke.py"


def _function_source(source: str, name: str) -> str:
    signature = -1
    search_from = 0
    while True:
        candidate = source.index(f"{name}(", search_from)
        line_start = source.rfind("\n", 0, candidate) + 1
        prefix = source[line_start:candidate].strip()
        if not prefix or prefix.startswith(("if", "while", "for", "return")):
            search_from = candidate + len(name) + 1
            continue
        opening = source.index("{", candidate)
        semicolon = source.find(";", candidate, opening)
        if semicolon < 0:
            signature = candidate
            break
        search_from = candidate + len(name) + 1
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[signature : offset + 1]
    raise AssertionError(f"unterminated function: {name}")


class Gfx2DFullscreenHandoffContractTests(unittest.TestCase):
    def test_ownership_primitive_serializes_desktop_and_fullscreen_writers(
        self,
    ):
        compiler_names = (
            ("g++", "clang++")
            if os.name == "nt"
            else ("clang++", "g++")
        )
        compiler = next(
            (path for name in compiler_names if (path := shutil.which(name))),
            None,
        )
        if compiler is None:
            self.skipTest("a hosted C++ compiler is required")

        harness = textwrap.dedent(
            """
            #include <atomic>
            #include <thread>
            #include "gfx2d_handoff.h"

            static gfx2d_handoff_t race_handoff = {};
            static std::atomic<int> race_start(0);
            static std::atomic<int> race_writers(0);
            static std::atomic<int> race_failed(0);

            static void race_enter_writer(void) {
              if (race_writers.fetch_add(1) != 0) race_failed.store(1);
              for (volatile int spin = 0; spin < 64; spin++) {}
            }

            static void race_leave_writer(void) {
              if (race_writers.fetch_sub(1) != 1) race_failed.store(1);
            }

            static void race_desktop(void) {
              while (!race_start.load()) std::this_thread::yield();
              for (int i = 0; i < 4000; i++) {
                while (!gfx2d_handoff_desktop_begin(&race_handoff, 10))
                  std::this_thread::yield();
                race_enter_writer();
                race_leave_writer();
                while (gfx2d_handoff_desktop_end(&race_handoff, 10) == 0)
                  std::this_thread::yield();
              }
            }

            static void race_fullscreen(void) {
              race_start.store(1);
              for (int i = 0; i < 4000; i++) {
                int prepared;
                int finished;
                while (!gfx2d_handoff_try_request_fullscreen(&race_handoff, 20))
                  std::this_thread::yield();
                while (!gfx2d_handoff_desktop_quiescent(&race_handoff))
                  std::this_thread::yield();
                while (!gfx2d_handoff_try_mark_fullscreen_entered(
                           &race_handoff, 20))
                  std::this_thread::yield();
                race_enter_writer();
                race_leave_writer();
                do {
                  prepared = gfx2d_handoff_try_prepare_fullscreen_release(
                      &race_handoff, 20
                  );
                  if (prepared == 0) std::this_thread::yield();
                } while (prepared == 0);
                if (prepared != 2) race_failed.store(1);
                do {
                  finished = gfx2d_handoff_try_finish_fullscreen_release(
                      &race_handoff, 20
                  );
                  if (finished == 0) std::this_thread::yield();
                } while (finished == 0);
                if (finished != 2) race_failed.store(1);
              }
            }

            int main(void) {
              gfx2d_handoff_t handoff = {};
              gfx2d_handoff_t pending = {};

              if (!gfx2d_handoff_desktop_begin(&pending, 10)) return 1;
              if (!gfx2d_handoff_try_request_fullscreen(&pending, 1)) return 1;
              if (gfx2d_handoff_fullscreen_entered(&pending)) return 1;
              if (gfx2d_handoff_writer_begin(&pending, 1)
                  != GFX2D_HANDOFF_WRITER_BUSY) return 1;
              if (gfx2d_handoff_try_prepare_fullscreen_release(
                      &pending, 1) != 3) return 1;
              if (gfx2d_handoff_try_finish_fullscreen_release(
                      &pending, 1) != 2) return 1;
              if (gfx2d_handoff_fullscreen_active(&pending)) return 1;
              if (gfx2d_handoff_desktop_end(&pending, 10) != 2) return 1;

              if (gfx2d_handoff_fullscreen_active(&handoff)) return 1;
              if (!gfx2d_handoff_desktop_begin(&handoff, 10)) return 2;
              if (gfx2d_handoff_owner_has_state(&handoff, 10) != 1) return 2;
              if (gfx2d_handoff_owner_has_desktop(&handoff, 10) != 1) return 2;
              if (gfx2d_handoff_owner_has_state(&handoff, 11) != 0) return 2;
              if (gfx2d_handoff_owner_has_desktop(&handoff, 11) != 0) return 2;
              if (gfx2d_handoff_desktop_owned_by_other(&handoff, 10)) return 2;
              if (!gfx2d_handoff_desktop_owned_by_other(&handoff, 11)) return 2;
              if (gfx2d_handoff_desktop_begin(&handoff, 11)) return 3;
              if (gfx2d_handoff_desktop_quiescent(&handoff)) return 4;

              if (!gfx2d_handoff_try_request_fullscreen(&handoff, 1)) return 5;
              if (!gfx2d_handoff_fullscreen_active(&handoff)) return 5;
              if (!gfx2d_handoff_desktop_begin(&handoff, 10)) return 6;
              if (gfx2d_handoff_writer_begin(&handoff, 10)
                  != GFX2D_HANDOFF_WRITER_DESKTOP) return 7;
              if (gfx2d_handoff_writer_begin(&handoff, 1)
                  != GFX2D_HANDOFF_WRITER_BUSY) return 8;
              if (gfx2d_handoff_desktop_begin(&handoff, 11)) return 6;
              if (gfx2d_handoff_writer_begin(&handoff, 11)
                  != GFX2D_HANDOFF_WRITER_BUSY) return 9;
              if (gfx2d_handoff_desktop_quiescent(&handoff)) return 10;
              if (gfx2d_handoff_try_request_fullscreen(&handoff, 2)) return 11;
              if (!gfx2d_handoff_try_request_fullscreen(&handoff, 1)) return 12;
              if (gfx2d_handoff_try_prepare_fullscreen_release(
                      &handoff, 2) != -1)
                return 13;
              if (gfx2d_handoff_try_prepare_fullscreen_release(
                      &handoff, 1) != 1)
                return 14;
              if (!gfx2d_handoff_fullscreen_active(&handoff)) return 15;

              if (gfx2d_handoff_desktop_end(&handoff, 11) != -1) return 16;
              if (gfx2d_handoff_desktop_end(&handoff, 10) != 1) return 17;
              if (gfx2d_handoff_desktop_end(&handoff, 10) != 1) return 18;
              if (gfx2d_handoff_desktop_quiescent(&handoff)) return 19;
              if (gfx2d_handoff_desktop_end(&handoff, 10) != 2) return 20;
              if (!gfx2d_handoff_desktop_quiescent(&handoff)) return 21;
              if (gfx2d_handoff_desktop_begin(&handoff, 10)) return 22;
              if (!gfx2d_handoff_try_mark_fullscreen_entered(
                       &handoff, 1)) return 22;
              if (!gfx2d_handoff_fullscreen_entered(&handoff)) return 22;
              if (gfx2d_handoff_writer_begin(&handoff, 1)
                  != GFX2D_HANDOFF_WRITER_FULLSCREEN) return 22;

              if (gfx2d_handoff_try_prepare_fullscreen_release(
                      &handoff, 1) != 2)
                return 23;
              if (gfx2d_handoff_try_prepare_fullscreen_release(
                      &handoff, 1) != 2)
                return 24;
              if (!gfx2d_handoff_fullscreen_active(&handoff)) return 24;
              if (gfx2d_handoff_writer_begin(&handoff, 1)
                  != GFX2D_HANDOFF_WRITER_BUSY) return 25;
              if (gfx2d_handoff_try_request_fullscreen(&handoff, 1)) return 26;
              if (gfx2d_handoff_try_finish_fullscreen_release(
                      &handoff, 2) != -1) return 27;
              if (gfx2d_handoff_try_finish_fullscreen_release(
                      &handoff, 1) != 2) return 28;
              if (gfx2d_handoff_fullscreen_active(&handoff)) return 29;
              if (!gfx2d_handoff_desktop_begin(&handoff, 10)) return 30;
              if (gfx2d_handoff_desktop_end(&handoff, 10) != 2) return 31;
              if (!gfx2d_handoff_desktop_quiescent(&handoff)) return 32;
              if (gfx2d_handoff_owner_has_state(&handoff, 10) != 0) return 32;

              if (!gfx2d_handoff_desktop_begin(&handoff, 10)) return 33;
              if (gfx2d_handoff_writer_begin(&handoff, 10)
                  != GFX2D_HANDOFF_WRITER_DESKTOP) return 34;
              if (gfx2d_handoff_desktop_end_owned(&handoff, 10) != 1)
                return 35;
              if (gfx2d_handoff_desktop_end_owned(&handoff, 10) != 2)
                return 36;
              if (!gfx2d_handoff_desktop_quiescent(&handoff)) return 37;

              std::thread desktop(race_desktop);
              std::thread fullscreen(race_fullscreen);
              desktop.join();
              fullscreen.join();
              if (race_failed.load() || race_writers.load() != 0) return 33;
              if (gfx2d_handoff_fullscreen_active(&race_handoff)) return 34;
              if (!gfx2d_handoff_desktop_quiescent(&race_handoff)) return 35;
              return 0;
            }
            """
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "gfx2d-handoff-probe.cc"
            hosted_types = root / "types.h"
            executable = root / (
                "gfx2d-handoff-probe.exe"
                if os.name == "nt"
                else "gfx2d-handoff-probe"
            )
            source.write_text(harness, encoding="utf-8")
            hosted_types.write_text(
                "#include <cstdint>\nusing uint32_t = std::uint32_t;\n",
                encoding="utf-8",
            )
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-fno-builtin",
                    "-pthread",
                    str(source),
                    "-I",
                    str(root),
                    "-I",
                    str(REPO_ROOT / "kernel" / "gfx"),
                    "-o",
                    str(executable),
                ],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            self.assertEqual(
                built.returncode,
                0,
                msg=built.stdout + built.stderr,
            )
            executed = subprocess.run(
                [str(executable)],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertEqual(
                executed.returncode,
                0,
                msg=executed.stdout + executed.stderr,
            )

    def test_production_paths_use_the_quiescent_handoff(self):
        gfx2d = GFX2D_SOURCE.read_text(encoding="utf-8")
        desktop = DESKTOP_SOURCE.read_text(encoding="utf-8")
        gui = GUI_SOURCE.read_text(encoding="utf-8")
        shell = SHELL_SOURCE.read_text(encoding="utf-8")
        process = PROCESS_SOURCE.read_text(encoding="utf-8")
        bmptest = BMPTEST_SOURCE.read_text(encoding="utf-8")
        doom = DOOM_SOURCE.read_text(encoding="utf-8")
        parity_gfx2d = PARITY_GFX2D_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "gfx2d_handoff.h"', gfx2d)
        self.assertIn("gfx2d_handoff_try_request_fullscreen", gfx2d)
        self.assertIn("gfx2d_handoff_desktop_quiescent", gfx2d)
        self.assertIn("gfx2d_handoff_try_prepare_fullscreen_release", gfx2d)
        self.assertIn("gfx2d_handoff_try_finish_fullscreen_release", gfx2d)
        release = _function_source(gfx2d, "gfx2d_fullscreen_release_token")
        self.assertLess(
            release.index("gfx2d_surface_unset_active();"),
            release.index("gfx2d_handoff_try_finish_fullscreen_release"),
        )
        self.assertIn("process_get_current_pid() + 1u", gfx2d)
        self.assertIn("g2d_handoff_save_and_cli", gfx2d)
        self.assertIn("g2d_handoff_restore_if", gfx2d)
        self.assertGreaterEqual(
            gfx2d.count('__asm__ volatile("" ::: "memory");'),
            4,
        )
        self.assertIn("while (suspended_depth != 0u)", gfx2d)
        self.assertIn("GFX2D_HANDOFF_RELEASE_ENTERED_FINAL", gfx2d)
        self.assertIn("uint32_t gfx2d_fullscreen_release_all(void)", gfx2d)
        self.assertIn("void gfx2d_release_process_ownership(uint32_t pid)", gfx2d)
        self.assertIn(
            "int gfx2d_try_release_process_ownership(uint32_t pid)",
            gfx2d,
        )
        self.assertIn("GFX2D_PROCESS_RELEASE_RETRIES", gfx2d)
        force_release = _function_source(
            gfx2d,
            "gfx2d_release_process_ownership_with_budget",
        )
        self.assertIn("gfx2d_handoff_owner_has_desktop", force_release)
        self.assertIn("gfx2d_release_budget_pause", force_release)
        self.assertLess(
            force_release.index("gfx2d_surface_unset_active();"),
            force_release.index("gfx2d_handoff_desktop_end"),
        )
        self.assertIn("int gfx2d_process_owns_render_state(uint32_t pid)", gfx2d)
        self.assertIn("int gfx2d_shared_writer_begin(void)", gfx2d)
        desktop_end = _function_source(gfx2d, "gfx2d_desktop_render_end")
        self.assertIn("gfx2d_handoff_desktop_end_owned", desktop_end)
        self.assertNotIn("while", desktop_end)
        self.assertNotIn("do {", desktop_end)
        self.assertIn("int gfx2d_desktop_writer_owned_by_other(void)", gfx2d)
        init = _function_source(gfx2d, "gfx2d_init")
        self.assertIn("gfx2d_shared_writer_begin()", init)
        self.assertIn("gfx2d_shared_writer_end(", init)
        self.assertIn('__asm__ volatile("pause")', gfx2d)
        self.assertIn("if (warn_non_owner)", gfx2d)
        self.assertNotIn("static int g2d_fullscreen_mode", gfx2d)
        self.assertIn(
            "gui_release_process_paint(process_get_current_pid());",
            shell,
        )
        self.assertIn(
            "gfx2d_release_process_ownership(process_get_current_pid());",
            shell,
        )
        self.assertIn("shell_jit_discard_by_owner(dead_pid);", process)
        self.assertIn("gui_release_process_paint(dead_pid);", process)
        cleanup = _function_source(process, "process_cleanup_resources_locked")
        self.assertIn(
            "if (!gfx2d_try_release_process_ownership(dead_pid))",
            cleanup,
        )
        self.assertLess(
            cleanup.index("gui_release_process_paint(dead_pid);"),
            cleanup.index("gfx2d_try_release_process_ownership(dead_pid)"),
        )
        self.assertLess(
            cleanup.index("gfx2d_try_release_process_ownership(dead_pid)"),
            cleanup.index("shell_jit_discard_by_owner(dead_pid);"),
        )
        self.assertLess(
            cleanup.index("shell_jit_discard_by_owner(dead_pid);"),
            cleanup.index("gui_destroy_windows_by_owner(dead_pid);"),
        )
        self.assertIn("if (gui_cleanup_result == GUI_ERR_BUSY)", cleanup)
        schedule = process.split("void schedule(void)", 1)[1].split(
            "void process_reschedule_if_pending", 1
        )[0]
        self.assertLess(
            schedule.index("process_reap_terminated_impl();"),
            schedule.index("schedule_locked()"),
        )
        self.assertNotIn(
            "if (gfx2d_fullscreen_active()) {\n"
            "    gfx2d_fullscreen_exit();\n"
            "  }",
            shell,
        )
        jit_start = _function_source(shell, "shell_jit_program_start_regions")
        self.assertIn("jit_owner_pid == current_pid", jit_start)
        self.assertIn("gfx2d_process_owns_render_state(current_pid)", jit_start)
        self.assertIn("nested graphics owner", jit_start)

        self.assertEqual(desktop.count("gfx2d_desktop_render_begin()"), 6)
        self.assertEqual(desktop.count("gfx2d_desktop_render_end();"), 8)
        self.assertEqual(desktop.count("gfx2d_popup_menu("), 1)
        self.assertEqual(desktop.count("desktop_popup_menu_owned("), 4)
        self.assertIn("desktop_open_bg_settings_dialog_owned();", desktop)
        desktop_key_read = _function_source(desktop, "desktop_read_key_event")
        self.assertIn("gfx2d_shared_writer_try_begin()", desktop_key_read)
        self.assertIn("keyboard_read_event(event)", desktop_key_read)
        self.assertIn("gfx2d_shared_writer_end(writer_lease)", desktop_key_read)
        present = gui.split("int gui_present_windows(void)", 1)[1].split(
            "int gui_cache_window_content", 1
        )[0]
        self.assertIn("gfx2d_shared_writer_try_begin()", present)
        self.assertIn("return GUI_ERR_BUSY;", present)
        self.assertEqual(present.count("gfx2d_shared_writer_end("), 3)
        for function_name in (
            "desktop_redraw_cycle",
            "desktop_run_minimized_loop",
            "desktop_run",
        ):
            with self.subTest(input_owner=function_name):
                input_loop = _function_source(desktop, function_name)
                mouse_boundary = input_loop.index("/* Process mouse */")
                keyboard_boundary = input_loop.index("/* Process keyboard")
                first_input_guard = input_loop.index(
                    "gfx2d_desktop_writer_owned_by_other()"
                )
                second_input_guard = input_loop.index(
                    "gfx2d_desktop_writer_owned_by_other()",
                    first_input_guard + 1,
                )
                self.assertLess(first_input_guard, mouse_boundary)
                self.assertGreater(second_input_guard, mouse_boundary)
                self.assertLess(second_input_guard, keyboard_boundary)
                self.assertIn("desktop_read_key_event(&event)", input_loop)
                self.assertNotIn("while (keyboard_read_event(&event))", input_loop)
        main_loop = _function_source(desktop, "desktop_run")
        self.assertGreater(
            main_loop.index("gfx2d_desktop_render_begin()"),
            main_loop.index("/* Redraw */"),
        )

        for function_name in (
            "fdlg_run_screen",
            "gfx2d_confirm_dialog",
            "gfx2d_input_dialog",
            "gfx2d_message_dialog",
            "gfx2d_popup_menu",
        ):
            with self.subTest(function=function_name):
                body = _function_source(gfx2d, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        popup = _function_source(gfx2d, "gfx2d_popup_menu")
        self.assertLess(
            popup.index("gfx2d_shared_writer_begin()"),
            popup.index('[gfx2d] popup input ready'),
        )
        confirm = _function_source(gfx2d, "gfx2d_confirm_dialog")
        terminal_key = confirm.index("if (evt.scancode == FDLG_SC_ENTER)")
        self.assertGreater(confirm.index("if (result >= 0) break;"), terminal_key)

        modal_target = _function_source(gfx2d, "fdlg_window_modal_target")
        modal_window = _function_source(gfx2d, "fdlg_run_window")
        self.assertNotIn("window_t **out_win", modal_target)
        self.assertIn("int *out_wid", modal_target)
        self.assertNotIn("window_t *win", modal_window.split("{", 1)[0])
        self.assertIn("uint32_t expected_owner", modal_window)
        self.assertGreaterEqual(modal_window.count("gui_get_window(wid)"), 3)
        self.assertGreaterEqual(
            modal_window.count("win->owner_pid != expected_owner"),
            3,
        )
        self.assertGreaterEqual(
            modal_window.count("(int)win->width - 2 != content_w"),
            3,
        )
        self.assertGreaterEqual(
            modal_window.count("paint_result == GUI_ERR_BUSY"),
            3,
        )

        for function_name in ("gui_create_window", "gui_set_focus"):
            with self.subTest(registry_mutation=function_name):
                body = _function_source(gui, function_name)
                self.assertIn("gui_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        for function_name in (
            "gui_handle_mouse",
            "gui_minimize_window",
            "gui_restore_window",
        ):
            with self.subTest(mouse_mutation=function_name):
                body = _function_source(gui, function_name)
                self.assertIn("gui_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        themes = THEMES_SOURCE.read_text(encoding="utf-8")
        themes_header = THEMES_HEADER.read_text(encoding="utf-8")
        self.assertIn("const ui_theme_t *ui_theme_get(void);", themes_header)
        self.assertIn("const ui_style_t *ui_style_get(void);", themes_header)
        self.assertEqual(
            themes_header.count(
                "hold the shared graphics writer lease while using it"
            ),
            2,
        )
        for function_name in (
            "gui_themes_init",
            "ui_theme_set",
            "ui_theme_reset_default",
            "ui_style_set",
            "ui_theme_load",
            "ui_theme_save",
        ):
            with self.subTest(theme=function_name):
                body = _function_source(themes, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        events = EVENTS_SOURCE.read_text(encoding="utf-8")
        event_init = _function_source(events, "gui_events_init")
        self.assertIn("g_events_initialized", event_init)
        self.assertIn("if (g_events_initialized)", event_init)

        self.assertIn("gfx2d_fullscreen_enter();", bmptest)
        self.assertIn("gfx2d_fullscreen_exit();", bmptest)
        self.assertIn('#include "gfx2d.h"', doom)
        self.assertIn("gfx2d_fullscreen_enter();", doom)
        self.assertGreaterEqual(doom.count("gfx2d_fullscreen_exit();"), 3)
        parity_main = parity_gfx2d.split("main:", 1)[1]
        self.assertLess(
            parity_main.index("call gfx2d_fullscreen_enter"),
            parity_main.index("call gfx2d_init"),
        )
        self.assertGreaterEqual(
            parity_main.count("call gfx2d_fullscreen_exit"),
            3,
        )

    def test_retained_paint_scopes_serialize_the_global_render_context(self):
        gui = GUI_SOURCE.read_text(encoding="utf-8")
        process = PROCESS_SOURCE.read_text(encoding="utf-8")
        paint = PAINT_SOURCE.read_text(encoding="utf-8")
        paint_demo = PAINT_DEMO_SOURCE.read_text(encoding="utf-8")

        begin = _function_source(gui, "gui_begin_window_paint")
        end = _function_source(gui, "gui_end_window_paint")
        abandon = _function_source(gui, "gui_release_process_paint")
        destroy = _function_source(gui, "gui_destroy_window")
        reap_destroy = _function_source(gui, "gui_destroy_windows_by_owner")
        cache = _function_source(gui, "gui_cache_window_content")

        self.assertLess(
            begin.index("gfx2d_shared_writer_try_begin()"),
            begin.index("ensure_window_surface"),
        )
        self.assertIn("gui_paint_owner_token", begin)
        self.assertIn("gui_legacy_owner_token", begin)
        self.assertIn("gui_paint_window_id", begin)
        self.assertIn("gfx2d_surface_set_active", begin)
        self.assertIn("gui_paint_writer_lease", begin)
        self.assertLess(
            begin.index("gui_paint_writer_lease = writer_lease;"),
            begin.index("gfx2d_surface_set_active"),
        )
        self.assertIn("gui_paint_owner_token", end)
        self.assertIn("gui_paint_window_id", end)
        abandon_helper = _function_source(gui, "gui_abandon_active_paint")
        self.assertIn("gfx2d_surface_unset_active()", abandon_helper)
        self.assertIn("gfx2d_clip_clear()", abandon_helper)
        self.assertIn("gfx2d_blend_mode(GFX2D_BLEND_NORMAL)", abandon_helper)
        self.assertNotIn("gfx2d_shared_writer_end", abandon)
        self.assertLess(
            end.index("gui_abandon_active_paint"),
            end.index("gfx2d_shared_writer_end"),
        )
        self.assertIn("gui_shared_writer_begin", destroy)
        self.assertIn("gfx2d_shared_writer_end", destroy)
        self.assertIn("gfx2d_shared_writer_try_begin", reap_destroy)
        self.assertIn("return GUI_ERR_BUSY;", reap_destroy)
        self.assertIn("gfx2d_shared_writer_end", reap_destroy)
        self.assertIn("gui_shared_writer_begin", cache)
        self.assertIn("gfx2d_shared_writer_end", cache)
        self.assertIn("gui_release_process_paint(dead_pid);", process)

        for source in (paint, paint_demo):
            with self.subTest(source="paint" if source is paint else "demo"):
                main = _function_source(source, "main")
                self.assertLess(
                    main.index("gfx2d_fullscreen_enter();"),
                    main.index("gfx2d_init();"),
                )
                self.assertLess(
                    main.rindex("gfx2d_surface_free(canvas_surf);"),
                    main.rindex("gfx2d_fullscreen_exit();"),
                )

        cupidc = CUPIDC_SOURCE.read_text(encoding="utf-8")
        create = _function_source(cupidc, "cc_gui_win_create")
        self.assertNotIn("gui_draw_window", create)

        legacy_begin = _function_source(gui, "gui_begin_legacy_frame")
        legacy_end = _function_source(gui, "gui_end_legacy_frame")
        self.assertIn("gfx2d_shared_writer_try_begin()", legacy_begin)
        self.assertIn("gui_legacy_owner_token", legacy_begin)
        self.assertIn("gui_draw_window_locked", legacy_begin)
        self.assertIn("gui_cache_window_content", legacy_end)
        self.assertIn("gui_present_windows", legacy_end)
        self.assertIn("gfx2d_shared_writer_end", legacy_end)
        self.assertIn("gui_abandon_legacy_frame", _function_source(
            gui, "gui_release_process_paint"
        ))
        legacy_abandon = _function_source(gui, "gui_abandon_legacy_frame")
        self.assertIn("gfx2d_surface_unset_active()", legacy_abandon)
        self.assertIn("gfx2d_clip_clear()", legacy_abandon)
        self.assertIn("gfx2d_blend_mode(GFX2D_BLEND_NORMAL)", legacy_abandon)
        legacy_end = _function_source(gui, "gui_end_legacy_frame")
        self.assertLess(
            legacy_end.index("gui_restore_legacy_render_state"),
            legacy_end.index("gui_cache_window_content"),
        )

        asm = ASM_SOURCE.read_text(encoding="utf-8")
        for source, begin_name, end_name in (
            (cupidc, "cc_gui_win_draw_frame", "cc_gui_win_flip"),
            (asm, "as_gui_win_draw_frame", "as_gui_win_flip"),
        ):
            self.assertIn(
                "gui_begin_legacy_frame",
                _function_source(source, begin_name),
            )
            self.assertIn(
                "gui_end_legacy_frame",
                _function_source(source, end_name),
            )

    def test_asset_registries_share_the_render_ownership_lease(self):
        assets = ASSETS_SOURCE.read_text(encoding="utf-8")
        fontsys = FONTSYS_SOURCE.read_text(encoding="utf-8")
        transform = TRANSFORM_SOURCE.read_text(encoding="utf-8")
        desktop = DESKTOP_SOURCE.read_text(encoding="utf-8")

        for function_name in (
            "gfx2d_assets_init",
            "gfx2d_image_load",
            "gfx2d_image_load_mem",
            "gfx2d_image_free",
            "gfx2d_image_draw",
            "gfx2d_image_draw_scaled",
            "gfx2d_image_draw_region",
            "gfx2d_image_width",
            "gfx2d_image_height",
            "gfx2d_image_get_pixel",
            "gfx2d_font_load",
            "gfx2d_font_free",
            "gfx2d_font_set_default",
            "gfx2d_font_text_width",
            "gfx2d_font_text_height",
            "gfx2d_text_ex",
        ):
            with self.subTest(asset=function_name):
                body = _function_source(assets, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        icons = ICONS_SOURCE.read_text(encoding="utf-8")
        icons_header = (REPO_ROOT / "kernel" / "gfx" / "gfx2d_icons.h").read_text(
            encoding="utf-8"
        )
        for function_name in (
            "gfx2d_icons_init",
            "gfx2d_icon_register",
            "gfx2d_icon_set_desc",
            "gfx2d_icon_set_type",
            "gfx2d_icon_set_color",
            "gfx2d_icon_set_custom_drawer",
            "gfx2d_icon_set_launch",
            "gfx2d_icon_invoke_launch",
            "gfx2d_icon_set_pos",
            "gfx2d_icon_snap_to_grid",
            "gfx2d_icon_get_label",
            "gfx2d_icon_get_path",
            "gfx2d_icon_get_desc",
            "gfx2d_icon_get_x",
            "gfx2d_icon_get_y",
            "gfx2d_icon_select",
            "gfx2d_icon_deselect_all",
            "gfx2d_icon_find_by_path",
            "gfx2d_icon_unregister",
            "gfx2d_icon_count",
            "gfx2d_icon_at_pos",
            "gfx2d_icons_handle_click",
            "gfx2d_icon_draw_named",
            "gfx2d_icons_draw_all",
            "gfx2d_icons_scan_bin",
            "gfx2d_icons_save",
            "gfx2d_icons_load",
        ):
            with self.subTest(icon_registry=function_name):
                body = _function_source(icons, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        for function_name, view_name in (
            ("gfx2d_icon_get_label", "icon_label_views"),
            ("gfx2d_icon_get_path", "icon_path_views"),
            ("gfx2d_icon_get_desc", "icon_desc_views"),
        ):
            with self.subTest(icon_snapshot=function_name):
                body = _function_source(icons, function_name)
                self.assertIn("copy_icon_view(", body)
                self.assertIn(view_name, body)
                self.assertLess(
                    body.index("copy_icon_view("),
                    body.index("gfx2d_shared_writer_end("),
                )
        self.assertIn(
            "icon_label_views[MAX_PROCESSES + 1]",
            icons,
        )
        self.assertIn("pid <= MAX_PROCESSES ? pid : 0", icons)
        self.assertIn(
            "Valid until that process asks again",
            icons_header,
        )

        for function_name in (
            "gfx2d_icons_init",
            "gfx2d_icon_register",
            "gfx2d_icon_set_type",
            "gfx2d_icon_set_color",
            "gfx2d_icon_set_custom_drawer",
            "gfx2d_icon_set_pos",
            "gfx2d_icon_snap_to_grid",
            "gfx2d_icon_select",
            "gfx2d_icon_deselect_all",
            "gfx2d_icon_unregister",
            "gfx2d_icons_scan_bin",
            "gfx2d_icons_load",
        ):
            with self.subTest(icon_generation=function_name):
                body = _function_source(icons, function_name)
                self.assertIn("gfx2d_icons_touch_locked()", body)
                self.assertLess(
                    body.index("gfx2d_icons_touch_locked()"),
                    body.index(f"{function_name}_locked("),
                )

        self.assertIn("uint32_t gfx2d_icons_generation(void)", icons)
        self.assertIn("gfx2d_icons_generation()", desktop)
        self.assertIn("workspace_cache_registry_generation", desktop)

        self.assertIn("uint32_t custom_draw_owner", icons_header)
        self.assertIn("uint32_t launch_owner", icons_header)
        self.assertIn("gfx2d_icons_release_process_callbacks", icons_header)
        self.assertIn("gfx2d_icons_try_release_process_callbacks", icons_header)
        self.assertIn("gfx2d_icons_process_owns_callbacks", icons_header)
        callback_set = _function_source(
            icons,
            "gfx2d_icon_set_custom_drawer_locked",
        )
        self.assertIn("process_get_current_pid() + 1u", callback_set)
        self.assertLess(
            callback_set.index("icons[handle].custom_draw_owner ="),
            callback_set.index("icons[handle].custom_draw = drawer"),
        )
        launch_set = _function_source(icons, "gfx2d_icon_set_launch_locked")
        self.assertLess(
            launch_set.index("icons[handle].launch_owner ="),
            launch_set.index("icons[handle].launch = launch_fn"),
        )
        callback_release = _function_source(
            icons,
            "gfx2d_icons_release_process_callbacks_locked",
        )
        self.assertLess(
            callback_release.index("gfx2d_icons_touch_locked()"),
            callback_release.index("icons[i].custom_draw = 0"),
        )
        self.assertIn("icons[i].custom_draw = 0", callback_release)
        self.assertIn("icons[i].launch = 0", callback_release)
        launch_invoke = _function_source(icons, "gfx2d_icon_invoke_launch")
        self.assertLess(
            launch_invoke.index("gfx2d_icon_invoke_launch_locked"),
            launch_invoke.index("gfx2d_shared_writer_end"),
        )
        self.assertNotIn("gfx2d_icon_get_launch", desktop)

        shell = SHELL_SOURCE.read_text(encoding="utf-8")
        process = PROCESS_SOURCE.read_text(encoding="utf-8")
        jit_end = _function_source(shell, "shell_jit_program_end")
        self.assertLess(
            jit_end.index("gfx2d_icons_release_process_callbacks"),
            jit_end.index("memcpy((void *)jit_stack[d].code_address"),
        )
        jit_start = _function_source(shell, "shell_jit_program_start_regions")
        self.assertIn("gfx2d_icons_process_owns_callbacks", jit_start)
        cleanup = _function_source(process, "process_cleanup_resources_locked")
        self.assertIn("gfx2d_icons_try_release_process_callbacks", cleanup)
        self.assertLess(
            cleanup.index("gfx2d_icons_try_release_process_callbacks"),
            cleanup.index("shell_jit_discard_by_owner"),
        )

        desktop = DESKTOP_SOURCE.read_text(encoding="utf-8")
        for function_name in (
            "desktop_bg_set_mode_anim",
            "desktop_bg_set_mode_solid",
            "desktop_bg_set_mode_gradient",
            "desktop_bg_set_mode_tiled_pattern",
            "desktop_bg_set_mode_tiled_bmp",
            "desktop_bg_set_mode_bmp",
            "desktop_bg_get_mode",
            "desktop_bg_get_solid_color",
            "desktop_bg_set_anim_theme",
            "desktop_bg_get_anim_theme",
            "desktop_bg_get_tiled_pattern",
            "desktop_bg_get_tiled_use_bmp",
        ):
            with self.subTest(background_registry=function_name):
                body = _function_source(desktop, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        release_tile = _function_source(desktop, "desktop_release_tile_bmp_locked")
        self.assertLess(
            release_tile.index("desktop_tile_bmp_data = NULL"),
            release_tile.index("kfree(old_pixels)"),
        )
        apply_bmp = _function_source(desktop, "desktop_bg_apply_bmp")
        self.assertIn("uint32_t *replacement", apply_bmp)
        self.assertLess(
            apply_bmp.index("desktop_invalidate_workspace_cache()"),
            apply_bmp.index("desktop_bg_bmp_scaled = replacement"),
        )
        self.assertLess(
            apply_bmp.index("desktop_bg_bmp_scaled = replacement"),
            apply_bmp.index("kfree(old_pixels)"),
        )
        apply_tile = _function_source(desktop, "desktop_bg_apply_tile_bmp")
        self.assertLess(
            apply_tile.index("desktop_invalidate_workspace_cache()"),
            apply_tile.index("desktop_tile_bmp_data = NULL"),
        )
        self.assertLess(
            apply_tile.index("desktop_tile_bmp_w = (int)tile_w"),
            apply_tile.index("desktop_tile_bmp_data = pixels"),
        )
        self.assertLess(
            apply_tile.index("desktop_tile_bmp_data = pixels"),
            apply_tile.index("kfree(old_pixels)"),
        )

        themes = THEMES_SOURCE.read_text(encoding="utf-8")
        for function_name in (
            "ui_theme_set",
            "ui_theme_reset_default",
            "ui_style_set",
            "ui_theme_load",
        ):
            with self.subTest(theme_invalidation=function_name):
                body = _function_source(themes, function_name)
                self.assertLess(
                    body.index("desktop_theme_changed()"),
                    body.index(f"{function_name}_locked("),
                )
        theme_changed = _function_source(desktop, "desktop_theme_changed")
        self.assertIn("desktop_invalidate_taskbar_cache()", theme_changed)
        self.assertIn("gui_mark_all_dirty()", theme_changed)

        for function_name in (
            "fontsys_init",
            "fontsys_register_blob",
            "fontsys_register_file",
            "fontsys_set_generic",
            "fontsys_match",
            "fontsys_synth_flags",
            "fontsys_ascent",
            "fontsys_descent",
            "fontsys_line_height",
            "fontsys_glyph",
            "fontsys_run_width",
            "fontsys_draw_run_styled",
            "fontsys_unregister",
            "fontsys_face_has_cp",
            "fontsys_find_face_with_cp",
            "fontsys_face_count",
            "fontsys_face_family",
            "fontsys_face_weight",
            "fontsys_face_italic",
            "fontsys_set_os_default",
            "fontsys_get_os_default_face",
            "fontsys_get_os_default_size",
            "fontsys_advance",
        ):
            with self.subTest(fontsys=function_name):
                body = _function_source(fontsys, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

        asset_init = _function_source(assets, "gfx2d_assets_init_locked")
        self.assertIn("if (g2d_img_used[i])", asset_init)
        self.assertIn("if (g2d_fonts[i].used)", asset_init)
        kernel = KERNEL_SOURCE.read_text(encoding="utf-8")
        self.assertLess(
            kernel.index("gfx2d_init();"),
            kernel.index("gfx2d_assets_init();"),
        )
        self.assertLess(
            kernel.index("gfx2d_assets_init();"),
            kernel.index("fontsys_init();"),
        )

        for function_name in (
            "gfx2d_transform_init",
            "gfx2d_push_transform",
            "gfx2d_pop_transform",
            "gfx2d_reset_transform",
            "gfx2d_translate",
            "gfx2d_rotate",
            "gfx2d_scale",
            "gfx2d_rotate_around",
            "gfx2d_set_matrix",
            "gfx2d_get_matrix",
            "gfx2d_transform_point",
            "gfx2d_image_draw_transformed",
            "gfx2d_sprite_draw_transformed",
            "gfx2d_text_transformed",
        ):
            with self.subTest(transform=function_name):
                body = _function_source(transform, function_name)
                self.assertIn("gfx2d_shared_writer_begin()", body)
                self.assertIn("gfx2d_shared_writer_end(", body)

    def test_resource_release_invalidates_handles_before_free(self):
        assets = ASSETS_SOURCE.read_text(encoding="utf-8")
        fontsys = FONTSYS_SOURCE.read_text(encoding="utf-8")
        gfx2d = GFX2D_SOURCE.read_text(encoding="utf-8")

        sprite_free = _function_source(gfx2d, "gfx2d_sprite_free")
        self.assertLess(
            sprite_free.index("__atomic_store_n(&g2d_sprite_used[handle], 0"),
            sprite_free.index("kfree(old_data)"),
        )
        surface_free = _function_source(gfx2d, "gfx2d_surface_free")
        self.assertLess(
            surface_free.index("__atomic_store_n(&g2d_surf_used[handle], 0"),
            surface_free.index("kfree(old_data)"),
        )

        image_free = _function_source(assets, "gfx2d_image_free_locked")
        self.assertLess(
            image_free.index("__atomic_store_n(&g2d_img_used[handle], 0"),
            image_free.index("kfree(old_data)"),
        )
        font_free = _function_source(assets, "gfx2d_font_free_locked")
        self.assertLess(
            font_free.index("__atomic_store_n(&g2d_fonts[handle].used, 0"),
            font_free.index("kfree(old_glyph_data)"),
        )

        unregister = _function_source(fontsys, "fontsys_unregister_locked")
        self.assertLess(
            unregister.index("__atomic_store_n(&face_used[face_id], 0"),
            unregister.index("kfree(old_blob)"),
        )
        self.assertEqual(unregister.count("gc_n_used--;"), 1)
        self.assertIn(
            "while (gc_n_used > 0 && !gc_used[gc_n_used - 1])",
            unregister,
        )
        evict = _function_source(fontsys, "gc_evict_one")
        self.assertLess(
            evict.index("__atomic_store_n(&gc_used[oldest], 0"),
            evict.index("kfree(old_alpha)"),
        )
        raster = _function_source(fontsys, "rasterize_into_cache")
        self.assertLess(
            raster.index("gc_bytes_total +="),
            raster.index("__atomic_store_n(&gc_used[slot], 1"),
        )

    def test_guest_repeats_the_fullscreen_font_handoff(self):
        source = GFXGUI_SOURCE.read_text(encoding="utf-8")

        self.assertIn("for (handoff = 0; handoff < 32; handoff++)", source)
        self.assertIn("gfx2d_fullscreen_exit();\n    yield();", source)
        self.assertIn("FAIL font pixel handoff=%d value=%x", source)
        loop = source.split(
            "for (handoff = 0; handoff < 32; handoff++) {",
            1,
        )[1].split("\n  }", 1)[0]
        self.assertLess(
            loop.index("gfx2d_fullscreen_enter();"),
            loop.index("gfx2d_blend_mode(0);"),
        )
        self.assertIn(
            'gfx2d_text_ex(16, 16, "A", 0xFFFFFF, fnt, 0);',
            loop,
        )
        final_enter = source.index(
            "gfx2d_fullscreen_enter();",
            source.index("gfx2d_fullscreen_exit();\n    yield();"),
        )
        self.assertGreater(source.index("gfx2d_transform_init();"), final_enter)

    def test_guest_exit_leaves_nested_cleanup_to_the_process_reaper(self):
        source = EXIT_SOURCE.read_text(encoding="utf-8")
        kill_source = KILL_SOURCE.read_text(encoding="utf-8")
        smoke = SMOKE_SOURCE.read_text(encoding="utf-8")
        process = PROCESS_SOURCE.read_text(encoding="utf-8")

        self.assertEqual(source.count("gfx2d_fullscreen_enter();"), 2)
        self.assertIn("[gfxhandoff_exit] nested owner exiting", source)
        self.assertIn("process_kill_after_ms(0, 0)", source)
        self.assertIn("exit();", source)
        self.assertNotIn("gfx2d_fullscreen_exit();", source)
        self.assertIn(
            '"ccc /bin/gfxhandoff_exit.cc -o /gfxhandoff_exit",', smoke
        )
        self.assertIn('"exec /gfxhandoff_exit",', smoke)
        self.assertNotIn('        "/gfxhandoff_exit",', smoke)
        self.assertNotIn('"/bin/gfxhandoff_exit.cc",', smoke)
        self.assertIn(r"\[elf\] Loaded /gfxhandoff_exit as PID", smoke)
        self.assertIn(r"\[gfxhandoff_exit\] nested owner exiting", smoke)
        self.assertEqual(kill_source.count("gfx2d_fullscreen_enter();"), 2)
        self.assertIn("[gfxhandoff_kill] nested owner waiting", kill_source)
        self.assertIn("while (1)", kill_source)
        self.assertIn("yield();", kill_source)
        self.assertIn("process_kill_after_ms(pid, 7000)", kill_source)
        self.assertIn("get_args()", kill_source)
        self.assertLess(
            kill_source.rindex("gfx2d_fullscreen_enter();"),
            kill_source.index("process_kill_after_ms(pid, 7000)"),
        )
        self.assertLess(
            kill_source.index("[gfxhandoff_kill] nested owner waiting"),
            kill_source.index("process_kill_after_ms(pid, 7000)"),
        )
        delayed_kill = _function_source(process, "process_kill_after_ms")
        self.assertIn("if (pid == 0u)", delayed_kill)
        self.assertIn("pid = process_get_current_pid();", delayed_kill)
        self.assertIn("wait_for_reuse", delayed_kill)
        self.assertIn("target_generation", delayed_kill)
        delayed_entry = _function_source(process, "process_delayed_kill_entry")
        self.assertIn("if (wait_for_reuse)", delayed_entry)
        self.assertIn("process_pid_generation_changed", delayed_entry)
        expected_kill = _function_source(process, "process_kill_expected")
        self.assertIn("p->lifetime_generation != expected_generation", expected_kill)
        self.assertIn("Delayed kill skipped stale PID", expected_kill)
        self.assertNotIn("gfx2d_fullscreen_exit();", kill_source)
        self.assertIn(
            '"ccc /bin/gfxhandoff_kill.cc -o /gfxhandoff_kill",', smoke
        )
        self.assertIn('"exec /gfxhandoff_kill {pid}",', smoke)
        self.assertNotIn('"kill {pid}",', smoke)
        self.assertIn('capture_name="gfx_owner_pid"', smoke)
        self.assertGreaterEqual(smoke.count('pid_from_capture="gfx_owner_pid"'), 2)
        self.assertIn("Delayed kill skipped stale PID {pid}", smoke)
        self.assertIn("waiting for PID (?P=pid) reuse", smoke)
        self.assertIn("targeting PID {pid} after 7000 ms", smoke)
        self.assertIn('"exec /gfxgui_test",', smoke)
        self.assertNotIn('        "/gfxgui_test",', smoke)
        self.assertLess(
            smoke.index('"ccc /bin/gfxhandoff_kill.cc -o /gfxhandoff_kill",'),
            smoke.index('"exec /gfxhandoff_exit",'),
        )
        self.assertLess(
            smoke.index('"exec /gfxhandoff_exit",'),
            smoke.index('"exec /gfxhandoff_kill {pid}",'),
        )
        self.assertLess(
            smoke.index('"exec /gfxhandoff_kill {pid}",'),
            smoke.index('"exec /gfxgui_test",'),
        )

    def test_cupidasm_selftest_count_matches_registered_definitions(self):
        source = ASM_SOURCE.read_text(encoding="utf-8")
        runtime = _function_source(source, "as_register_kernel_bindings")
        definitions = _function_source(source, "as_register_definitions")

        expected = int(
            source.split("#define AS_EXPECTED_DEFINITIONS ", 1)[1]
            .split("u", 1)[0]
        )
        runtime_bindings = runtime.count("AS_BIND(as,") - 1
        runtime_constants = runtime.count('as_bind_equ(as, "')
        syscall_bindings = definitions.count("AS_BIND_SYS(") - 1
        definition_constants = definitions.count('as_bind_equ(as, "')
        registered = (
            runtime_bindings
            + runtime_constants
            + syscall_bindings
            + definition_constants
        )

        # The JIT/AOT exit branches contribute one definition at runtime, and
        # the AS_BIND_SYS macro declaration is not itself a definition.
        self.assertEqual(registered, expected)


if __name__ == "__main__":
    unittest.main()

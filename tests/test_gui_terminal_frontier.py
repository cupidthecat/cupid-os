## @file
#  @brief Verify frontier command sequencing, USB hotplug, and runtime log checks.
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.gui_terminal_fixtures import (
    FakeMonitorSocket,
    ReplugMonitorSocket,
    SequencedMonitorSocket,
    _frontier_command,
    _frontier_command_output,
    _frontier_command_outputs,
    _frontier_runtime_log,
    _write_frontier_usb_image,
)
from tools import gui_terminal_contracts, gui_terminal_smoke


class FrontierRuntimeContractTests(unittest.TestCase):
    def run_replug_contract(self, log, monitor, process=None):
        if process is None:
            process = mock.Mock()
            process.poll.return_value = None
        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            return gui_terminal_smoke.run_frontier_usb_replug_contract(
                process,
                monitor,
                log,
                timeout=0.1,
                key_pause=0.01,
            )

    def test_replug_contract_orders_recovery_proofs_after_each_device_add(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(_frontier_runtime_log(), encoding="utf-8")
            monitor = ReplugMonitorSocket(log)

            self.run_replug_contract(log, monitor)

        commands = [
            command.decode("ascii").strip()
            for command in monitor.sent
        ]
        self.assertEqual(
            commands[:14],
            [
                "device_del frontier_keyboard",
                "info qtree",
                (
                    "device_add usb-kbd,id=frontier_keyboard_replug,"
                    "bus=frontier_uhci.0,port=1"
                ),
                "sendkey l 300",
                "sendkey s 300",
                "sendkey ret 300",
                "device_del frontier_mouse",
                "info qtree",
                (
                    "device_add usb-mouse,id=frontier_mouse_replug,"
                    "bus=frontier_uhci.0,port=2"
                ),
                "mouse_move 32 24",
                "mouse_button 1",
                "mouse_button 0",
                "mouse_move -16 8 1",
                "mouse_move 0 0 -1",
            ],
        )
        first_storage_delete = commands.index(
            "device_del frontier_mass_storage"
        )
        self.assertEqual(first_storage_delete, 14)
        expected_storage_commands = []
        storage_id = "frontier_mass_storage"
        for cycle in range(1, 6):
            expected_storage_commands.extend(
                [
                    f"device_del {storage_id}",
                    "info qtree",
                    (
                        "device_add usb-storage,"
                        f"id=frontier_mass_storage_replug_{cycle},"
                        "bus=frontier_ehci.0,port=1,"
                        "drive=frontier_usb_storage"
                    ),
                ]
            )
            storage_id = f"frontier_mass_storage_replug_{cycle}"
        self.assertEqual(commands[14:], expected_storage_commands)

    def test_replug_contract_accepts_the_expected_unary_diagnostic(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_runtime_log()
                + _frontier_command_output("/bin/feature13_double.cc"),
                encoding="utf-8",
            )
            monitor = ReplugMonitorSocket(log)

            self.run_replug_contract(log, monitor)

        self.assertGreater(len(monitor.sent), 0)

    def test_replug_contract_exercises_six_storage_lifetimes(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(_frontier_runtime_log(), encoding="utf-8")
            monitor = ReplugMonitorSocket(log)

            self.run_replug_contract(log, monitor)

        commands = [
            command.decode("ascii").strip()
            for command in monitor.sent
        ]
        storage_adds = [
            command
            for command in commands
            if command.startswith("device_add usb-storage,")
        ]
        storage_deletes = [
            command
            for command in commands
            if command.startswith("device_del frontier_mass_storage")
        ]
        self.assertGreaterEqual(
            gui_terminal_contracts.FRONTIER_STORAGE_REATTACHMENTS,
            5,
        )
        self.assertEqual(len(storage_adds), 5)
        self.assertEqual(
            storage_adds,
            [
                (
                    f"device_add usb-storage,"
                    f"id=frontier_mass_storage_replug_{cycle},"
                    "bus=frontier_ehci.0,port=1,"
                    "drive=frontier_usb_storage"
                )
                for cycle in range(1, 6)
            ],
        )
        self.assertEqual(
            storage_deletes,
            [
                "device_del frontier_mass_storage",
                *[
                    f"device_del frontier_mass_storage_replug_{cycle}"
                    for cycle in range(1, 5)
                ],
            ],
        )

    def test_replug_contract_requires_the_initial_storage_lifetime(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_runtime_log().replace(
                    (
                        "usb_msc: usb0 has 1 FAT16 partition(s); "
                        "auto-mount NYI, use raw /dev/usb0\n"
                    ),
                    "",
                ),
                encoding="utf-8",
            )
            monitor = ReplugMonitorSocket(log)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "initial USB storage did not reach its FAT16 marker",
            ):
                self.run_replug_contract(log, monitor)

        self.assertEqual(monitor.sent, [])

    def test_replug_contract_rejects_stale_keyboard_attach_history(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_runtime_log()
                + "usb_hid: keyboard attached addr=77\n",
                encoding="utf-8",
            )
            monitor = ReplugMonitorSocket(
                log,
                omit_prefix="device_add usb-kbd,",
            )
            process = mock.Mock()
            process.poll.return_value = 7

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                (
                    "USB keyboard reattach did not reach its guest marker"
                    ".*QEMU exited with status 7"
                ),
            ):
                self.run_replug_contract(log, monitor, process)

    def test_replug_contract_rejects_a_storage_failure_even_with_fat16_log(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(_frontier_runtime_log(), encoding="utf-8")
            monitor = ReplugMonitorSocket(
                log,
                failure_storage_cycle=5,
            )

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                (
                    "USB storage reattach 5 saw failure marker: "
                    "usb_msc: block device registry is full"
                ),
            ):
                self.run_replug_contract(log, monitor)

    def test_gfxgui_failure_marker_stops_the_command_gate_immediately(self):
        marker = "[gfxgui_test] FAIL image load"
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(marker + "\n", encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*gfxgui_test",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("/bin/gfxgui_test.cc"),
                    start_offset=0,
                    timeout=0.1,
                )

    def test_kernel_panic_gate_waits_for_the_serial_reason(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("KERNEL PANIC\n", encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            def publish_reason(_delay):
                log.write_text(
                    "KERNEL PANIC\n"
                    "[PANIC] uhci: DMA revocation failed\n",
                    encoding="utf-8",
                )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.time.sleep",
                    side_effect=publish_reason,
                ),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "KERNEL PANIC.*uhci: DMA revocation failed",
                ),
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("/bin/gfxgui_test.cc"),
                    start_offset=0,
                    timeout=0.1,
                )

    def test_command_sequence_applies_only_explicit_timeout_overrides(self):
        commands = (
            gui_terminal_contracts.TerminalCommand("a", "A"),
            gui_terminal_contracts.TerminalCommand(
                "b",
                "B",
                timeout_seconds=7.5,
            ),
        )
        process = mock.Mock()
        monitor = FakeMonitorSocket()

        with (
            tempfile.TemporaryDirectory() as temporary,
            mock.patch.object(
                gui_terminal_contracts,
                "FRONTIER_RUNTIME_COMMANDS",
                commands,
            ),
            mock.patch(
                "tools.gui_terminal_smoke.wait_frontier_command",
                side_effect=[(1, "A"), (2, "B")],
            ) as wait_command,
            mock.patch("tools.gui_terminal_smoke.send_key"),
            mock.patch("tools.gui_terminal_smoke.time.sleep"),
        ):
            gui_terminal_smoke.run_frontier_commands(
                process,
                monitor,
                Path(temporary) / "serial.log",
                start_offset=0,
                timeout=1.25,
                key_pause=0.01,
            )

        self.assertEqual(
            [call.args[4] for call in wait_command.call_args_list],
            [1.25, 7.5],
        )

    def test_unary_command_allows_only_its_expected_compiler_error(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                output,
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            cursor, _data = gui_terminal_smoke.wait_frontier_command(
                process,
                log,
                command,
                start_offset=0,
                timeout=1.0,
            )
            self.assertGreater(cursor, 0)

            log.write_text(
                output.replace(
                    "unary sign requires an arithmetic scalar operand",
                    "undefined variable",
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*\\[cupidc\\] error",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=1.0,
                )

    def test_unary_command_rejects_its_diagnostic_before_compile_context(
        self,
    ):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                diagnostic + output.replace(diagnostic, ""),
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "outside.*feature13_double",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=0.1,
                )

    def test_unary_command_rejects_a_second_copy_of_its_diagnostic(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(output + diagnostic, encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "exactly once.*feature13_double",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=0.1,
                )

    def test_unary_command_accepts_each_trailing_diagnostic_prefix(self):
        command = _frontier_command("/bin/feature13_double.cc")
        context = (
            "[cupidc] JIT compile: /bin/feature13_double.cc\n"
        )
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )

        for prefix_length in range(1, len(diagnostic)):
            with self.subTest(prefix_length=prefix_length):
                checked = gui_terminal_smoke.mask_frontier_command_failures(
                    context + diagnostic[:prefix_length],
                    command,
                )
                self.assertIsNone(
                    gui_terminal_smoke.frontier_failure_marker(checked)
                )

        checked = gui_terminal_smoke.mask_frontier_command_failures(
            context + diagnostic[:-1] + "\r",
            command,
        )
        self.assertIsNone(
            gui_terminal_smoke.frontier_failure_marker(checked)
        )

    def test_unary_command_does_not_mask_an_embedded_partial_diagnostic(
        self,
    ):
        command = _frontier_command("/bin/feature13_double.cc")
        data = (
            "[cupidc] JIT compile: /bin/feature13_double.cc\n"
            "junk[cupidc] error (line 1): unary sign requires"
        )

        checked = gui_terminal_smoke.mask_frontier_command_failures(
            data,
            command,
        )
        self.assertEqual(
            gui_terminal_smoke.frontier_failure_marker(checked),
            "[cupidc] error",
        )

    def test_unary_command_rejects_an_embedded_complete_diagnostic(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                output.replace(diagnostic, "junk" + diagnostic),
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*\\[cupidc\\] error",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    command,
                    start_offset=0,
                    timeout=0.1,
                )

    def test_unary_command_rejects_an_embedded_compile_context(self):
        command = _frontier_command("/bin/feature13_double.cc")
        output = _frontier_command_output("/bin/feature13_double.cc")
        context = "[cupidc] JIT compile: /bin/feature13_double.cc"
        malformed_contexts = (
            ("leading text", "junk" + context),
            ("trailing text", context + "junk"),
        )
        for name, malformed in malformed_contexts:
            with (
                self.subTest(name=name),
                tempfile.TemporaryDirectory() as temporary,
            ):
                log = Path(temporary) / "serial.log"
                log.write_text(
                    output.replace(context, malformed),
                    encoding="utf-8",
                )
                process = mock.Mock()
                process.poll.return_value = None

                with self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "outside.*feature13_double",
                ):
                    gui_terminal_smoke.wait_frontier_command(
                        process,
                        log,
                        command,
                        start_offset=0,
                        timeout=0.1,
                    )

    def test_command_sequence_waits_for_each_marker_before_continuing(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("", encoding="utf-8")
            monitor = SequencedMonitorSocket(
                log,
                _frontier_command_outputs(),
                delayed_marker=(
                    "[godsong] settings ready\n"
                    "[gfx2d] popup input ready\n"
                ),
            )
            process = mock.Mock()
            process.poll.return_value = None
            marker_release_sent_counts = []

            def release_interaction_marker(_seconds):
                if monitor.pending_marker is None:
                    return
                self.assertEqual(
                    len(monitor.sent),
                    monitor.marker_wait_sent_count,
                )
                marker_release_sent_counts.append(len(monitor.sent))
                monitor.release_delayed_marker()

            with mock.patch(
                "tools.gui_terminal_smoke.time.sleep",
                side_effect=release_interaction_marker,
            ) as sleep:
                data = gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=0,
                    timeout=1.0,
                    key_pause=0.01,
                )

        self.assertEqual(
            monitor.completed,
            len(gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS),
        )
        self.assertEqual(
            marker_release_sent_counts,
            [monitor.marker_wait_sent_count],
        )
        self.assertIsNone(monitor.pending_tail)
        self.assertIn("[cupidc] JIT compile: /bin/godsong.cc", data)
        self.assertIn("[gfx2d] flip frame=2", data)
        self.assertEqual(
            sleep.call_args_list.count(mock.call(1.0)),
            len(gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS) - 1,
        )
        self.assertEqual(sleep.call_args_list.count(mock.call(2.0)), 0)
        sent = b"".join(monitor.sent)
        self.assertIn(b"sendkey shift-minus 300\n", sent)
        self.assertIn(b"sendkey dot 300\n", sent)
        self.assertEqual(
            monitor.sent[-8:],
            [b"sendkey esc 300\n"] * 8,
        )

    def test_godsong_interaction_rejects_an_earlier_graphics_marker(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                "[gfx2d] flip frame=2\n"
                "[cupidc] JIT compile: /bin/godsong.cc\n",
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None
            command = _frontier_command("godsong 1 200")
            interaction = gui_terminal_contracts.TerminalCommand(
                command.text,
                command.interaction_pattern,
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.time.time",
                    side_effect=(0.0, 0.0, 1.0),
                ),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "timed out waiting",
                ),
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    interaction,
                    start_offset=0,
                    timeout=0.5,
                )

    def test_doom_recovery_does_not_reuse_the_first_ls_completion(self):
        command_texts = [
            command.text
            for command in gui_terminal_contracts.FRONTIER_RUNTIME_COMMANDS
        ]
        post_doom_ls_index = [
            index
            for index, text in enumerate(command_texts)
            if text == "ls"
        ][1]
        command_outputs = _frontier_command_outputs()
        command_outputs[post_doom_ls_index] = ""

        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, command_outputs)
            process = mock.Mock()
            process.poll.return_value = 9

            with (
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "frontier command 'ls'.*QEMU exited with status 9",
                ),
            ):
                gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=0,
                    timeout=1.0,
                    key_pause=0.01,
                )

        self.assertEqual(monitor.completed, post_doom_ls_index + 1)

    def test_syscall_demo_failure_cannot_pass_on_jit_completion(self):
        failed = _frontier_command_output(
            "as /demos/syscall_vfs_extended_demo.asm"
        ).replace(
            "extended SYS VFS calls: OK",
            "extended SYS VFS calls: FAIL",
        )
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(failed, encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker: extended SYS VFS calls: FAIL",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command(
                        "as /demos/syscall_vfs_extended_demo.asm"
                    ),
                    0,
                    0.1,
                )

    def test_command_sequence_reports_qemu_exit_and_pending_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("", encoding="utf-8")
            monitor = FakeMonitorSocket()
            process = mock.Mock()
            process.poll.return_value = 7

            with (
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "frontier command 'ls'.*QEMU exited with status 7",
                ),
            ):
                gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=0,
                    timeout=1.0,
                    key_pause=0.01,
                )

    def test_command_sequence_ignores_a_stale_completion_before_its_cursor(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                _frontier_command_output("ls"),
                encoding="utf-8",
            )
            start_offset = len(log.read_text(encoding="utf-8"))
            monitor = FakeMonitorSocket()
            process = mock.Mock()
            process.poll.return_value = 9

            with (
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "frontier command 'ls'.*QEMU exited with status 9",
                ),
            ):
                gui_terminal_smoke.run_frontier_commands(
                    process,
                    monitor,
                    log,
                    start_offset=start_offset,
                    timeout=1.0,
                    key_pause=0.01,
                )

    def test_complete_frontier_runtime_log_passes(self):
        gui_terminal_smoke.validate_frontier_runtime_log(
            _frontier_runtime_log()
        )

    def test_complete_frontier_allows_only_the_expected_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        data = _frontier_runtime_log() + output
        gui_terminal_smoke.validate_frontier_runtime_log(data)

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*\\[cupidc\\] error",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                data.replace(
                    "unary sign requires an arithmetic scalar operand",
                    "undefined variable",
                )
            )

    def test_complete_frontier_rejects_a_stale_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        data = (
            _frontier_runtime_log()
            + diagnostic
            + output.replace(diagnostic, "")
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "outside.*feature13_double",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_complete_frontier_rejects_a_duplicate_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        data = _frontier_runtime_log() + output + diagnostic

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "exactly once.*feature13_double",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_complete_frontier_rejects_an_embedded_unary_diagnostic(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        diagnostic = (
            "[cupidc] error (line 1): "
            "unary sign requires an arithmetic scalar operand\n"
        )
        data = _frontier_runtime_log() + output.replace(
            diagnostic,
            "junk" + diagnostic,
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*\\[cupidc\\] error",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_complete_frontier_rejects_an_embedded_compile_context(
        self,
    ):
        output = _frontier_command_output("/bin/feature13_double.cc")
        context = "[cupidc] JIT compile: /bin/feature13_double.cc"
        malformed_contexts = (
            ("leading text", "junk" + context),
            ("trailing text", context + "junk"),
        )
        for name, malformed in malformed_contexts:
            data = _frontier_runtime_log() + output.replace(
                context,
                malformed,
            )
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    "outside.*feature13_double",
                ),
            ):
                gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_frontier_runtime_requires_traffic_from_the_selected_nic(self):
        rtl8139 = (
            _frontier_runtime_log()
            .replace("e1000: init OK", "rtl8139: init OK")
            .replace("net: if=e1000", "net: if=rtl8139")
        )
        gui_terminal_smoke.validate_frontier_runtime_log(
            rtl8139,
            "rtl8139",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTL8139 initialization marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                _frontier_runtime_log(),
                "rtl8139",
            )

        no_traffic = rtl8139.replace(
            "[0.990] [INFO]  net: if=rtl8139 ip=10.0.2.15\n",
            "",
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTL8139 packet traffic marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                no_traffic,
                "rtl8139",
            )

    def test_frontier_runtime_rejects_every_rtl8139_probe_failure(self):
        for marker in (
            "rtl8139: BAR0 not IO port",
            "rtl8139: rx alloc failed",
            "rtl8139: tx alloc failed",
        ):
            with (
                self.subTest(marker=marker),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    f"found failure marker: {re.escape(marker)}",
                ),
            ):
                gui_terminal_smoke.validate_frontier_runtime_log(
                    _frontier_runtime_log() + marker + "\n",
                )

    def test_missing_frontier_marker_names_the_unproved_subsystem(self):
        data = _frontier_runtime_log().replace(
            "[5.650] [INFO]  usb_hid: keyboard attached addr=1\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing USB keyboard marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_pass_without_dma_refills_does_not_prove_ac97_runtime(self):
        data = _frontier_runtime_log().replace(
            "[ac97] DMA refills during audiotest: 441\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing AC97 refill exercise marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_rtc_requires_real_fields_and_rejects_invalid_data(self):
        gui_terminal_smoke.validate_frontier_runtime_log(
            _frontier_runtime_log()
        )

        placeholder = _frontier_runtime_log().replace(
            "RTC: 2026-07-24 22:14:05",
            "RTC: 2026-%02u-%02u %02u:%02u:%02u",
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTC marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(placeholder)

        unpadded = _frontier_runtime_log().replace(
            "RTC: 2026-07-24 22:14:05",
            "RTC: 2026-7-24 22:14:5",
        )
        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "missing RTC marker",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(unpadded)

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*RTC: invalid data",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(
                _frontier_runtime_log()
                + "[WARN] RTC: invalid data (time or date out of range)\n"
            )

    def test_frontier_failures_are_rejected_case_insensitively(self):
        data = _frontier_runtime_log() + "USB_MSC: READ CAPACITY FAILED\n"

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "failure marker.*usb_msc: read capacity failed",
        ):
            gui_terminal_smoke.validate_frontier_runtime_log(data)

    def test_guest_compiler_errors_stop_the_command_gate_immediately(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(
                "[cupidc] error (line 1): undefined variable\n",
                encoding="utf-8",
            )
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*\\[cupidc\\] error",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("ls"),
                    start_offset=0,
                    timeout=1.0,
                )

    def test_unresolved_guest_symbol_stops_the_command_gate_immediately(self):
        marker = "[cupidc] Unresolved symbol: gfx2d_blur_box"
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text(marker + "\n", encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = None

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "saw failure marker.*Unresolved symbol",
            ):
                gui_terminal_smoke.wait_frontier_command(
                    process,
                    log,
                    _frontier_command("ls"),
                    start_offset=0,
                    timeout=0.1,
                )

    def test_missing_usb_image_stops_before_qemu_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            image.write_bytes(b"system image")
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(root / "missing-usb.img"),
                    "--verify-frontier-runtime",
                ]
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen"
                ) as launch,
                mock.patch("sys.stderr"),
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 2)
        launch.assert_not_called()

    def test_frontier_mode_runs_on_private_disk_copies_and_all_validators(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            usb_image = root / "frontier-usb.img"
            image_bytes = b"system image bytes"
            image.write_bytes(image_bytes)
            _write_frontier_usb_image(usb_image)
            usb_bytes = usb_image.read_bytes()
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(usb_image),
                    "--verify-frontier-runtime",
                ]
            )
            process = mock.Mock()
            process.poll.return_value = None
            monitor = FakeMonitorSocket()
            copied_paths = []
            stopped = {"value": False}
            event_order = []

            def launch_private_images(command, **_kwargs):
                drives = [
                    command[index + 1]
                    for index, item in enumerate(command)
                    if item == "-drive"
                ]
                blockdevs = [
                    command[index + 1]
                    for index, item in enumerate(command)
                    if item == "-blockdev"
                ]
                self.assertEqual(len(drives), 1)
                self.assertEqual(len(blockdevs), 2)
                system_path = Path(
                    drives[0].split(",", 1)[0].split("=", 1)[1]
                )
                usb_file = next(
                    option
                    for option in blockdevs
                    if option.startswith("driver=file,")
                )
                usb_path = Path(
                    next(
                        field.split("=", 1)[1]
                        for field in usb_file.split(",")
                        if field.startswith("filename=")
                    )
                )
                copied_paths.extend((system_path, usb_path))
                for path in copied_paths:
                    self.assertTrue(path.is_file())
                self.assertNotIn(image, copied_paths)
                self.assertNotIn(usb_image, copied_paths)
                self.assertEqual(copied_paths[0].read_bytes(), image_bytes)
                self.assertEqual(copied_paths[1].read_bytes(), usb_bytes)
                copied_paths[0].write_bytes(b"guest changed system disk")
                copied_paths[1].write_bytes(b"guest changed USB disk")
                return process

            def mark_stopped(_process, _monitor):
                stopped["value"] = True

            def check_audio_after_stop(_path):
                self.assertTrue(stopped["value"])
                return gui_terminal_smoke.AudioEvidence(
                    channels=2,
                    sample_rate=22050,
                    frames=44100,
                    peak=8000,
                )

            def run_commands(*_args, **_kwargs):
                event_order.append("commands")
                return _frontier_runtime_log()

            def capture_frame(_monitor, path, **_kwargs):
                event_order.append(f"capture:{path.name}")

            def inject_mouse(_monitor):
                event_order.append("mouse")

            def run_replug(*_args, **_kwargs):
                event_order.append("replug")
                return _frontier_runtime_log()

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen",
                    side_effect=launch_private_images,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.free_tcp_port",
                    return_value=43210,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log",
                    side_effect=[
                        (True, "Entering desktop environment"),
                        (True, "Terminal launched"),
                        (False, _frontier_runtime_log()),
                    ],
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log_success_count",
                    return_value=(True, _frontier_runtime_log()),
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.connect_monitor",
                    return_value=monitor,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.qemu_supports_wav_audio",
                    return_value=True,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.run_frontier_commands",
                    side_effect=run_commands,
                ) as commands,
                mock.patch(
                    "tools.gui_terminal_smoke.capture_screendump",
                    side_effect=capture_frame,
                ) as screendump,
                mock.patch(
                    "tools.gui_terminal_smoke.inject_mouse_activity",
                    side_effect=inject_mouse,
                ) as mouse,
                mock.patch(
                    "tools.gui_terminal_smoke.run_frontier_usb_replug_contract",
                    side_effect=run_replug,
                ) as replug,
                mock.patch(
                    "tools.gui_terminal_smoke.validate_framebuffer_change",
                    return_value=gui_terminal_smoke.FramebufferEvidence(
                        width=640,
                        height=480,
                        changed_pixels=32,
                    ),
                ) as framebuffer,
                mock.patch(
                    "tools.gui_terminal_smoke.validate_wav_audio",
                    side_effect=check_audio_after_stop,
                ) as audio,
                mock.patch(
                    "tools.gui_terminal_smoke.stop_qemu",
                    side_effect=mark_stopped,
                ),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
            ):
                status = gui_terminal_smoke.run(args)

            self.assertEqual(status, 0)
            self.assertEqual(image.read_bytes(), image_bytes)
            self.assertEqual(usb_image.read_bytes(), usb_bytes)
            commands.assert_called_once()
            self.assertEqual(screendump.call_count, 2)
            mouse.assert_called_once_with(monitor)
            self.assertEqual(
                monitor.sent,
                [
                    b"sendkey ctrl-alt-t 300\n",
                ],
            )
            self.assertEqual(
                event_order,
                [
                    "commands",
                    "capture:before.ppm",
                    "mouse",
                    "capture:after.ppm",
                    "replug",
                ],
            )
            replug.assert_called_once_with(
                process,
                monitor,
                mock.ANY,
                timeout=args.timeout,
                key_pause=args.key_pause,
            )
            framebuffer.assert_called_once()
            self.assertEqual(audio.call_count, 2)
            self.assertEqual(
                [call.args[0].name for call in audio.call_args_list],
                ["ac97.wav", "pcspk.wav"],
            )

    def test_frontier_mode_requires_qemu_wav_capture_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            usb_image = root / "frontier-usb.img"
            image.write_bytes(b"system image")
            _write_frontier_usb_image(usb_image)
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(usb_image),
                    "--verify-frontier-runtime",
                ]
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.qemu_supports_wav_audio",
                    return_value=False,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen"
                ) as launch,
                mock.patch("sys.stderr"),
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 2)
        launch.assert_not_called()

    def test_frontier_mode_rejects_a_usb_image_without_an_mbr(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            usb_image = root / "frontier-usb.img"
            image.write_bytes(b"system image")
            usb_image.write_bytes(b"\0" * 1024)
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--usb-image",
                    str(usb_image),
                    "--verify-frontier-runtime",
                ]
            )

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.qemu_supports_wav_audio"
                ) as audio_probe,
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen"
                ) as launch,
                mock.patch("sys.stderr"),
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 2)
        audio_probe.assert_not_called()
        launch.assert_not_called()

    def test_frontier_usb_partition_must_fit_inside_the_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            usb_image = Path(temporary) / "frontier-usb.img"
            _write_frontier_usb_image(usb_image)
            contents = bytearray(usb_image.read_bytes())
            partition = 0x1BE
            contents[partition + 12:partition + 16] = (6000).to_bytes(
                4,
                "little",
            )
            usb_image.write_bytes(contents)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "partition exceeds",
            ):
                gui_terminal_smoke.validate_frontier_usb_image(usb_image)

    def test_frontier_usb_partition_requires_a_fat16_boot_sector(self):
        with tempfile.TemporaryDirectory() as temporary:
            usb_image = Path(temporary) / "frontier-usb.img"
            _write_frontier_usb_image(usb_image)
            contents = bytearray(usb_image.read_bytes())
            contents[512:1024] = b"\0" * 512
            usb_image.write_bytes(contents)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "valid FAT16 boot sector",
            ):
                gui_terminal_smoke.validate_frontier_usb_image(usb_image)

if __name__ == "__main__":
    unittest.main()

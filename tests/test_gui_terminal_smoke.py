## @file
#  @brief Test QEMU terminal input, serial checks, and capture validation.
import io
import itertools
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.gui_terminal_fixtures import (
    FakeMonitorSocket,
    SequencedMonitorSocket,
    _smp_runtime_log,
    _write_ppm,
    _write_qemu_placeholder_wav,
    _write_wav,
)
from tools import gui_terminal_smoke


class GuiTerminalInputTests(unittest.TestCase):
    def _run_ordinary_runtime(self, exit_stage):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            image.write_bytes(b"system image")
            log = root / "serial.log"
            args = gui_terminal_smoke.parse_args(
                ["--image", str(image), "--log", str(log),
                 "--setup-command", "setup", "--setup-success-pattern", "ready"]
            )
            process = mock.Mock()
            process.poll.return_value = None
            monitor = mock.Mock()
            monitor.recv.return_value = b"(qemu)"
            command_count = 0
            capture = None
            output = io.StringIO()
            errors = io.StringIO()

            def launch(_command, **kwargs):
                nonlocal capture
                capture = kwargs["stdout"]
                capture.write(b"qemu-system-i386: host runtime failure\n")
                log.write_text("Entering desktop environment\n", encoding="utf-8")
                return process

            def send(data):
                nonlocal command_count
                if data.startswith(b"sendkey ctrl-alt-t "):
                    stage = "terminal"
                    marker = "Terminal launched\n"
                elif data.startswith(b"sendkey ret "):
                    command_count += 1
                    stage = "setup" if command_count == 1 else "command"
                    marker = "ready\n" if command_count == 1 else "JIT execution complete\n"
                elif data == b"quit\n":
                    process.poll.return_value = 0
                    return
                else:
                    return
                if exit_stage == stage:
                    process.poll.return_value = 9
                    return
                with log.open("a", encoding="utf-8") as serial:
                    serial.write(marker)
                    if stage == "command" and exit_stage == "survival":
                        process.poll.return_value = 9
                    if stage == "command" and exit_stage == "panic":
                        serial.write("KERNEL PANIC: test failure\n")

            monitor.sendall.side_effect = send
            with (
                mock.patch("tools.gui_terminal_smoke.subprocess.Popen",
                           side_effect=launch),
                mock.patch("tools.gui_terminal_smoke.socket.create_connection",
                           return_value=monitor),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                mock.patch("tools.gui_terminal_smoke.time.time",
                           side_effect=itertools.count()),
                mock.patch("sys.stdout", output),
                mock.patch("sys.stderr", errors),
            ):
                status = gui_terminal_smoke.run(args)

            self.assertTrue(capture.closed)
            self.assertEqual(image.read_bytes(), b"system image")
            return status, output.getvalue(), errors.getvalue()

    def test_ordinary_smoke_rejects_qemu_exit_after_command_completion(self):
        status, output, errors = self._run_ordinary_runtime("survival")

        self.assertEqual(status, 1)
        self.assertNotIn("smoke passed", output)
        self.assertIn("QEMU exited with status 9", errors)
        self.assertIn("host runtime failure", errors)

    def test_ordinary_smoke_reports_qemu_exit_during_terminal_commands(self):
        for stage, reason in (
            ("terminal", "Terminal did not launch"),
            ("setup", "setup command did not complete"),
            ("command", "command did not complete"),
        ):
            with self.subTest(stage=stage):
                status, output, errors = self._run_ordinary_runtime(stage)
                self.assertEqual(status, 1)
                self.assertNotIn("smoke passed", output)
                self.assertIn(reason, errors)
                self.assertIn("QEMU exited with status 9", errors)
                self.assertIn("host runtime failure", errors)

    def test_ordinary_smoke_keeps_success_and_panic_checks(self):
        status, output, errors = self._run_ordinary_runtime(None)
        self.assertEqual(status, 0)
        self.assertIn("GUI terminal smoke passed", output)
        self.assertEqual(errors, "")

        status, output, errors = self._run_ordinary_runtime("panic")
        self.assertEqual(status, 1)
        self.assertNotIn("smoke passed", output)
        self.assertIn("KERNEL PANIC: test failure", errors)

    def _run_ordinary_child(self, child_source, *, timeout="2", launch_error=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            image.write_bytes(b"system image")
            args = gui_terminal_smoke.parse_args(
                ["--image", str(image), "--log", str(root / "serial.log"),
                 "--private-image", "--timeout", timeout]
            )
            output = io.StringIO()
            launch = subprocess.Popen
            capture = None
            private_image = None

            def failed_qemu(command, **kwargs):
                nonlocal capture, private_image
                capture = kwargs["stdout"]
                drive = command[command.index("-drive") + 1]
                private_image = Path(drive.removeprefix("file=").split(",format=")[0])
                private_image.write_bytes(b"guest changed private image")
                if launch_error:
                    raise OSError("QEMU launch failed")
                return launch(
                    [sys.executable, "-c", child_source],
                    **kwargs,
                )

            with (
                mock.patch("tools.gui_terminal_smoke.subprocess.Popen",
                           side_effect=failed_qemu),
                mock.patch("sys.stderr", output),
            ):
                if launch_error:
                    with self.assertRaisesRegex(OSError, "QEMU launch failed"):
                        gui_terminal_smoke.run(args)
                    status = None
                else:
                    status = gui_terminal_smoke.run(args)

            self.assertTrue(capture.closed)
            self.assertFalse(private_image.exists())
            self.assertEqual(image.read_bytes(), b"system image")
            return status, output.getvalue()

    def test_ordinary_smoke_reports_qemu_startup_failure_without_a_timeout(self):
        status, output = self._run_ordinary_child(
            "import sys; sys.stderr.write("
            "'qemu-system-i386: cannot set up guest memory: "
            "Cannot allocate memory\\n'); sys.exit(7)"
        )

        self.assertEqual(status, 1)
        self.assertIn("QEMU exited with status 7", output)
        self.assertIn("cannot set up guest memory", output)
        self.assertNotIn("timeout", output)

    def test_ordinary_smoke_captures_large_qemu_output_without_blocking(self):
        status, output = self._run_ordinary_child(
            "import sys; sys.stdout.write('x' * 1048576); sys.stdout.flush(); "
            "sys.stderr.write('qemu: final startup error\\n'); sys.exit(3)"
        )

        self.assertEqual(status, 1)
        self.assertIn("QEMU exited with status 3", output)
        self.assertIn("qemu: final startup error", output)
        self.assertLess(len(output), 4200)

    def test_ordinary_smoke_keeps_a_live_qemu_timeout_distinct_from_exit(self):
        status, output = self._run_ordinary_child(
            "import time; time.sleep(30)", timeout="0.1"
        )

        self.assertEqual(status, 1)
        self.assertIn("GUI desktop did not boot before timeout", output)
        self.assertNotIn("QEMU exited", output)

    def test_ordinary_smoke_cleans_up_capture_and_private_image_when_launch_fails(self):
        self._run_ordinary_child("", launch_error=True)

    def test_read_log_retries_one_host_allocation_failure(self):
        log = Path("serial.log")
        with (
            mock.patch.object(
                Path,
                "read_bytes",
                side_effect=[MemoryError(), b"marker\n"],
            ) as read_bytes,
            mock.patch("tools.gui_terminal_smoke.gc.collect") as collect,
        ):
            data = gui_terminal_smoke.read_log(log)

        self.assertEqual(data, "marker\n")
        self.assertEqual(read_bytes.call_count, 2)
        collect.assert_called_once_with()

    def test_read_log_reports_a_persistent_host_allocation_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_bytes(b"marker")
            with (
                mock.patch.object(
                    Path,
                    "read_bytes",
                    side_effect=MemoryError(),
                ),
                mock.patch("tools.gui_terminal_smoke.gc.collect"),
                self.assertRaisesRegex(
                    gui_terminal_smoke.FrontierRuntimeContractError,
                    r"serial log allocation failed.*serial\.log.*6 bytes",
                ),
            ):
                gui_terminal_smoke.read_log(log)

    def test_qemu_launch_failure_reports_captured_output(self):
        process = mock.Mock()
        process.poll.return_value = 1
        output = io.BytesIO(
            b"qemu-system-i386: monitor socket address is already in use\n"
        )

        detail = gui_terminal_smoke.qemu_exit_diagnostic(process, output)

        self.assertIn("QEMU exited with status 1", detail)
        self.assertIn("monitor socket address is already in use", detail)

    def test_hmp_rejects_a_monitor_command_error(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            return_value=(
                b"Error: Drive 'frontier_usb_storage' is already in use\r\n"
                b"(qemu)"
            )
        )

        with (
            mock.patch("tools.gui_terminal_smoke.time.sleep"),
            self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "QEMU monitor rejected.*frontier_usb_storage",
            ),
        ):
            gui_terminal_smoke.hmp(
                monitor,
                (
                    "device_add usb-storage,id=replug,"
                    "drive=frontier_usb_storage"
                ),
            )

    def test_hmp_drains_readline_echo_before_a_monitor_error(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            side_effect=[
                b"x" * 4096,
                (
                    b"Error: Drive 'frontier_usb_storage' "
                    b"is already in use\r\n(qemu)"
                ),
            ]
        )

        with (
            mock.patch("tools.gui_terminal_smoke.time.sleep"),
            self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "QEMU monitor rejected.*frontier_usb_storage",
            ),
        ):
            gui_terminal_smoke.hmp(
                monitor,
                (
                    "device_add usb-storage,id=replug,"
                    "drive=frontier_usb_storage"
                ),
            )

        self.assertEqual(monitor.recv.call_count, 2)

    def test_wait_hmp_device_deleted_polls_until_qtree_forgets_id(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            side_effect=[
                (
                    b'dev: usb-storage, id "frontier_mass_storage"\r\n'
                    b"(qemu)"
                ),
                b"(qemu)",
            ]
        )

        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            gui_terminal_smoke.wait_hmp_device_deleted(
                monitor,
                "frontier_mass_storage",
                timeout=1.0,
            )

        self.assertEqual(
            monitor.sent,
            [b"info qtree\n", b"info qtree\n"],
        )

    def test_wait_hmp_device_deleted_rejects_a_stuck_device(self):
        monitor = FakeMonitorSocket()
        monitor.recv = mock.Mock(
            return_value=(
                b'dev: usb-storage, id "frontier_mass_storage"\r\n'
                b"(qemu)"
            )
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.FrontierRuntimeContractError,
            "QEMU did not delete device 'frontier_mass_storage'",
        ):
            gui_terminal_smoke.wait_hmp_device_deleted(
                monitor,
                "frontier_mass_storage",
                timeout=0.0,
            )

    def test_send_key_holds_usb_report_long_enough_for_guest_polling(self):
        monitor = FakeMonitorSocket()

        with mock.patch("tools.gui_terminal_smoke.time.sleep") as sleep:
            gui_terminal_smoke.send_key(monitor, "slash")

        self.assertEqual(monitor.sent, [b"sendkey slash 300\n"])
        sleep.assert_called_once_with(0.35)

    def test_key_names_cover_frontier_runtime_commands(self):
        self.assertEqual(gui_terminal_smoke.key_name("_"), "shift-minus")
        self.assertEqual(gui_terminal_smoke.key_name("+"), "shift-equal")
        self.assertEqual(gui_terminal_smoke.key_name("/"), "slash")
        self.assertEqual(gui_terminal_smoke.key_name("."), "dot")
        self.assertEqual(gui_terminal_smoke.key_name(" "), "spc")

    def test_terminal_command_types_a_pointer_difference_repl_expression(self):
        command = r'{short pd[6];serial_printf("PTRDIFF=%d\n",(pd+6)-pd);}'
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("boot complete\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["PTRDIFF=6\n"])
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, _ = gui_terminal_smoke.run_terminal_command(
                    mock.Mock(), monitor, log, command, r"PTRDIFF=6", 1.0, 0.35)
        self.assertTrue(ok)
        self.assertEqual(len(monitor.sent), len(command) + 1)
        for report in (b"sendkey bracket_left 300\n", b"sendkey semicolon 300\n",
                       b"sendkey shift-apostrophe 300\n", b"sendkey shift-p 300\n",
                       b"sendkey backslash 300\n", b"sendkey shift-9 300\n",
                       b"sendkey shift-bracket_left 300\n",
                       b"sendkey shift-bracket_right 300\n"):
            self.assertIn(report, monitor.sent)
        self.assertEqual(monitor.sent[-1], b"sendkey ret 300\n")

    def test_terminal_command_types_a_fat_short_name(self):
        command = "doom -iwad /disk/wads/freedo~1.wad"
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("boot complete\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["doom ready\n"])
            process = mock.Mock()
            process.poll.return_value = None
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, data = gui_terminal_smoke.run_terminal_command(
                    process, monitor, log, command, "doom ready", 1.0, 0.35
                )
        self.assertTrue(ok)
        self.assertIn("doom ready", data)
        self.assertEqual(
            monitor.sent[command.index("~")],
            b"sendkey shift-grave_accent 300\n",
        )
        self.assertEqual(monitor.sent[-1], b"sendkey ret 300\n")
        self.assertEqual(len(monitor.sent), len(command) + 1)

    def test_terminal_command_fails_immediately_on_a_new_worker_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("boot complete\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["build stopped at /home/test.job\n"])
            process = mock.Mock()
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, data = gui_terminal_smoke.run_terminal_command(
                    process, monitor, log, "exec /disk/b", "build complete", 7200, 0.35,
                    require_completion=True, failure_pattern="build stopped at ",
                )
        self.assertFalse(ok)
        self.assertIn("build stopped at /home/test.job", data)
        process.poll.assert_not_called()

    def test_terminal_command_ignores_an_earlier_worker_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("build stopped at /home/previous.job\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["build complete\n[terminal] command complete\n"])
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, _ = gui_terminal_smoke.run_terminal_command(
                    mock.Mock(), monitor, log, "exec /disk/b", "build complete", 1, 0.35,
                    require_completion=True, failure_pattern="build stopped at ",
                )
        self.assertTrue(ok)

    def test_terminal_completion_rejects_an_early_success_without_ready(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("[terminal] command complete\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["first ret\n"])
            process = mock.Mock()
            process.poll.return_value = None
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, _ = gui_terminal_smoke.run_terminal_command(
                    process, monitor, log, "dis", "first ret", 0.01, 0.35,
                    require_completion=True,
                )
        self.assertFalse(ok)

    def test_terminal_completion_waits_for_command_return_after_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("boot complete\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["first ret\n"])
            process = mock.Mock()
            polls = []

            def running():
                polls.append(True)
                with log.open("a", encoding="utf-8") as stream:
                    stream.write("last ret\n[terminal] command complete\n")
                return None

            process.poll.side_effect = running
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, data = gui_terminal_smoke.run_terminal_command(
                    process, monitor, log, "dis", "first ret", 1.0, 0.35,
                    require_completion=True,
                )
        self.assertTrue(ok)
        self.assertTrue(polls)
        self.assertIn("last ret", data)
        self.assertIn("[terminal] command complete", data)

    def test_terminal_completion_does_not_replace_expected_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("boot complete\n", encoding="utf-8")
            monitor = SequencedMonitorSocket(log, ["[terminal] command complete\n"])
            process = mock.Mock()
            process.poll.return_value = None
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                ok, _ = gui_terminal_smoke.run_terminal_command(
                    process, monitor, log, "dis", "first ret", 0.01, 0.35,
                    require_completion=True,
                )
        self.assertFalse(ok)

    def test_terminal_command_rejects_unsupported_text_before_typing(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("boot complete\n", encoding="utf-8")
            monitor = FakeMonitorSocket()
            with mock.patch("tools.gui_terminal_smoke.time.sleep"):
                with self.assertRaisesRegex(
                    ValueError, "unsupported smoke-test character"
                ):
                    gui_terminal_smoke.run_terminal_command(
                        mock.Mock(), monitor, log, "echo ?", "ready", 1.0, 0.35
                    )
            self.assertEqual(monitor.sent, [])
            self.assertEqual(log.read_text(encoding="utf-8"), "boot complete\n")

    def test_completion_pattern_accepts_a_caller_success_regex_and_panics(self):
        pattern = gui_terminal_smoke.completion_pattern(
            r"\[elf\] Loaded /home/hello as PID [0-9]+"
        )

        self.assertIsNotNone(pattern.search("[elf] Loaded /home/hello as PID 7"))
        self.assertIsNotNone(pattern.search("KERNEL PANIC: bad"))
        self.assertIsNone(pattern.search("unrelated serial output"))

    def test_success_count_supports_repeated_command_gates(self):
        pattern = r'\[PROCESS\] PID [0-9]+ "hello" exiting'
        data = (
            '[PROCESS] PID 4 "hello" exiting\n'
            '[PROCESS] PID 4 released external image lease 1\n'
            '[PROCESS] PID 5 "hello" exiting\n'
        )

        self.assertEqual(gui_terminal_smoke.success_count(data, pattern), 2)

    def test_command_matchers_support_anchored_multiline_patterns(self):
        pattern = r"^compile\r?$.*?^complete\r?$"
        data = "boot noise\ncompile\r\nprogram output\ncomplete\r\n"

        self.assertIsNotNone(
            gui_terminal_smoke.completion_pattern(pattern).search(data)
        )
        self.assertEqual(gui_terminal_smoke.success_count(data, pattern), 1)

    def test_positive_count_rejects_zero(self):
        self.assertEqual(gui_terminal_smoke.positive_count("2"), 2)
        with self.assertRaisesRegex(ValueError, "positive"):
            gui_terminal_smoke.positive_count("0")

    def test_qemu_args_request_the_configured_cpu_count(self):
        args = mock.Mock(
            qemu="qemu-system-i386",
            image="fresh.img",
            log="serial.log",
            nic="e1000",
            smp=4,
            cpu=None,
        )

        command = gui_terminal_smoke.qemu_args(args, 12345)

        smp_index = command.index("-smp")
        self.assertEqual(command[smp_index + 1], "cpus=4")

    def test_qemu_args_request_the_configured_cpu_model(self):
        args = mock.Mock(
            qemu="qemu-system-i386",
            image="fresh.img",
            log="serial.log",
            nic="e1000",
            smp=1,
            cpu="max",
        )

        command = gui_terminal_smoke.qemu_args(args, 12345)

        cpu_index = command.index("-cpu")
        self.assertEqual(command[cpu_index + 1], "max")

    def test_cli_accepts_a_cpu_model(self):
        args = gui_terminal_smoke.parse_args(["--cpu", "max"])

        self.assertEqual(args.cpu, "max")

    def test_cli_accepts_the_strong_smp_runtime_contract(self):
        args = gui_terminal_smoke.parse_args(["--verify-smp-runtime"])

        self.assertTrue(args.verify_smp_runtime)

    def test_cli_accepts_the_frontier_runtime_contract_and_usb_image(self):
        args = gui_terminal_smoke.parse_args(
            [
                "--verify-frontier-runtime",
                "--usb-image",
                "frontier-usb.img",
            ]
        )

        self.assertTrue(args.verify_frontier_runtime)
        self.assertEqual(args.usb_image, Path("frontier-usb.img"))

    def test_frontier_qemu_topology_pins_usb_devices_and_wav_capture(self):
        args = gui_terminal_smoke.parse_args(
            [
                "--verify-frontier-runtime",
                "--image",
                "fresh.img",
                "--log",
                "serial.log",
                "--usb-image",
                "frontier-usb.img",
            ]
        )

        command = gui_terminal_smoke.qemu_args(
            args,
            12345,
            Path("frontier-ac97.wav"),
            Path("frontier-pcspk.wav"),
        )

        self.assertIn(
            "wav,id=ac97_capture,path=frontier-ac97.wav",
            command,
        )
        self.assertIn(
            "wav,id=pcspk_capture,path=frontier-pcspk.wav",
            command,
        )
        self.assertIn(
            "pcspk-audiodev=pcspk_capture,i8042=off",
            command,
        )
        self.assertIn("AC97,audiodev=ac97_capture", command)
        self.assertIn("piix3-usb-uhci,id=frontier_uhci", command)
        self.assertIn("usb-ehci,id=frontier_ehci", command)
        self.assertIn(
            "usb-kbd,id=frontier_keyboard,bus=frontier_uhci.0,port=1",
            command,
        )
        self.assertIn(
            "usb-mouse,id=frontier_mouse,bus=frontier_uhci.0,port=2",
            command,
        )
        self.assertIn(
            (
                "driver=file,filename=frontier-usb.img,"
                "node-name=frontier_usb_file"
            ),
            command,
        )
        self.assertIn(
            (
                "driver=raw,file=frontier_usb_file,"
                "node-name=frontier_usb_storage"
            ),
            command,
        )
        self.assertNotIn(
            (
                "file=frontier-usb.img,format=raw,if=none,"
                "id=frontier_usb_storage"
            ),
            command,
        )
        self.assertIn(
            (
                "usb-storage,id=frontier_mass_storage,"
                "bus=frontier_ehci.0,port=1,"
                "drive=frontier_usb_storage"
            ),
            command,
        )

    def test_wait_log_after_ignores_matching_history(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            stale = "usb_hid: mouse detached\n"
            log.write_text(stale, encoding="utf-8")
            process = mock.Mock()
            process.poll.return_value = 9

            found, data = gui_terminal_smoke.wait_log_after(
                process,
                log,
                r"usb_hid: mouse detached",
                len(stale),
                0.1,
            )

        self.assertFalse(found)
        self.assertEqual(data.splitlines(), stale.splitlines())

    def test_cli_accepts_a_slower_inter_key_pause(self):
        args = gui_terminal_smoke.parse_args(["--key-pause", "0.60"])

        self.assertEqual(args.key_pause, 0.60)

        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            gui_terminal_smoke.parse_args(["--key-pause", "0"])

    def test_cli_pairs_setup_commands_with_completion_patterns(self):
        args = gui_terminal_smoke.parse_args(
            [
                "--setup-command",
                "cp /disk/input /home/output",
                "--setup-success-pattern",
                "copy complete",
            ]
        )

        self.assertEqual(
            args.setup_command,
            ["cp /disk/input /home/output"],
        )
        self.assertEqual(args.setup_success_pattern, ["copy complete"])

    def test_cli_setup_timeout_is_separate_from_the_command_timeout(self):
        args = gui_terminal_smoke.parse_args(["--timeout", "1800", "--setup-timeout", "60"])
        self.assertEqual(args.setup_timeout, 60)
        self.assertEqual(args.timeout, 1800)
        self.assertIsNone(gui_terminal_smoke.parse_args([]).setup_timeout)
        for invalid in ("0", "nan", "inf"):
            with self.subTest(invalid=invalid), mock.patch("sys.stderr"), self.assertRaises(SystemExit):
                gui_terminal_smoke.parse_args(["--setup-timeout", invalid])

    def test_cli_accepts_a_private_system_image(self):
        args = gui_terminal_smoke.parse_args(["--private-image"])

        self.assertTrue(args.private_image)

    def test_private_system_image_preserves_the_selected_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "cupidos.img"
            source.write_bytes(b"original image")
            private = root / "private"
            private.mkdir()
            args = gui_terminal_smoke.parse_args(
                ["--image", str(source)]
            )

            copied_args = gui_terminal_smoke.copy_terminal_image(
                args,
                private,
            )
            copied_args.image.write_bytes(b"guest mutation")

            self.assertEqual(source.read_bytes(), b"original image")
            self.assertEqual(
                copied_args.image,
                private / "cupidos.img",
            )

    def test_cli_rejects_an_unpaired_setup_command(self):
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            gui_terminal_smoke.parse_args(
                ["--setup-command", "cp /disk/input /home/output"]
            )

    def test_terminal_command_requires_a_new_completion_event(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("copy complete\n", encoding="utf-8")
            process = mock.Mock()
            monitor = FakeMonitorSocket()

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.send_key"
                ) as send_key,
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log_success_count",
                    return_value=(
                        True,
                        "copy complete\ncopy complete\n",
                    ),
                ) as wait,
            ):
                ok, _ = gui_terminal_smoke.run_terminal_command(
                    process,
                    monitor,
                    log,
                    "cp",
                    "copy complete",
                    12.0,
                    0.6,
                )

        self.assertTrue(ok)
        self.assertEqual(
            [call.args[1] for call in send_key.call_args_list],
            ["c", "p", "ret"],
        )
        wait.assert_called_once_with(
            process,
            log,
            "copy complete",
            2,
            12.0,
        )

    def test_shutdown_requests_qemu_quit_before_process_termination_fallback(self):
        monitor = FakeMonitorSocket()
        process = mock.Mock()
        process.poll.return_value = None
        process.wait.return_value = 0

        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            gui_terminal_smoke.stop_qemu(process, monitor)

        self.assertEqual(monitor.sent, [b"quit\n"])
        self.assertTrue(monitor.closed)
        process.wait.assert_called_once_with(timeout=3.0)
        process.terminate.assert_not_called()
        process.kill.assert_not_called()

    def test_mouse_activity_covers_motion_button_and_wheel_reports(self):
        monitor = FakeMonitorSocket()

        with mock.patch("tools.gui_terminal_smoke.time.sleep"):
            gui_terminal_smoke.inject_mouse_activity(monitor)

        self.assertEqual(
            monitor.sent,
            [
                b"mouse_move 32 24\n",
                b"mouse_button 1\n",
                b"mouse_button 0\n",
                b"mouse_move -16 8 1\n",
                b"mouse_move 0 0 -1\n",
            ],
        )


class SmpRuntimeContractTests(unittest.TestCase):
    def test_complete_runtime_log_passes(self):
        gui_terminal_smoke.validate_smp_runtime_log(_smp_runtime_log())

    def test_smp_runtime_accepts_the_selected_rtl8139_driver(self):
        data = _smp_runtime_log().replace(
            "e1000: init OK",
            "rtl8139: init OK",
        )
        gui_terminal_smoke.validate_smp_runtime_log(data, "rtl8139")

    def test_missing_required_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "cpu3: online apic=3\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "missing required marker.*cpu3: online",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_missing_fpu_boot_smoke_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "[fpu] boot smoke ok\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            r"missing required marker: \[fpu\] boot smoke ok",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_missing_fpu_enable_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "[fpu] SSE2 enabled\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            r"missing required marker: \[fpu\] SSE2 enabled",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_missing_fpu_boot_summary_marker_is_rejected(self):
        data = _smp_runtime_log().replace(
            "FPU boot smoke passed\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "missing required marker: FPU boot smoke passed",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_wrong_tls_success_count_is_rejected(self):
        data = _smp_runtime_log().replace(
            "[tls-selftest] ok: fixture 61\n",
            "",
        )

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "61 TLS self-test successes; expected 62",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_failure_marker_is_rejected_case_insensitively(self):
        data = _smp_runtime_log() + "block cache: FLUSH FAILED\n"

        with self.assertRaisesRegex(
            gui_terminal_smoke.SmpRuntimeContractError,
            "failure marker.*Block cache: flush failed",
        ):
            gui_terminal_smoke.validate_smp_runtime_log(data)

    def test_run_applies_the_strong_contract_after_command_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "cupidos.img"
            image.write_bytes(b"fixture")
            args = gui_terminal_smoke.parse_args(
                [
                    "--image",
                    str(image),
                    "--log",
                    str(root / "serial.log"),
                    "--verify-smp-runtime",
                ]
            )
            process = mock.Mock()
            process.poll.return_value = None
            monitor = FakeMonitorSocket()
            completed = "[cupidc] JIT execution complete\n"
            final_log = "captured strong runtime log"

            with (
                mock.patch(
                    "tools.gui_terminal_smoke.subprocess.Popen",
                    return_value=process,
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
                        (False, final_log),
                    ],
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.wait_log_success_count",
                    return_value=(True, completed),
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.connect_monitor",
                    return_value=monitor,
                ),
                mock.patch(
                    "tools.gui_terminal_smoke.read_log",
                    return_value="",
                ),
                mock.patch("tools.gui_terminal_smoke.send_key"),
                mock.patch("tools.gui_terminal_smoke.time.sleep"),
                mock.patch("tools.gui_terminal_smoke.stop_qemu"),
                mock.patch(
                    "tools.gui_terminal_smoke.validate_smp_runtime_log"
                ) as validate,
            ):
                status = gui_terminal_smoke.run(args)

        self.assertEqual(status, 0)
        validate.assert_called_once_with(final_log, "e1000")


class FrontierFramebufferTests(unittest.TestCase):
    def test_changed_nonuniform_screendumps_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            _write_ppm(
                before,
                4,
                4,
                [b"\0\0\0", b"\xff\0\0"] * 8,
            )
            _write_ppm(
                after,
                4,
                4,
                [b"\0\xff\0", b"\0\0\xff"] * 8,
            )

            evidence = gui_terminal_smoke.validate_framebuffer_change(
                before,
                after,
            )

        self.assertEqual(evidence.width, 4)
        self.assertEqual(evidence.height, 4)
        self.assertEqual(evidence.changed_pixels, 16)

    def test_single_pixel_change_is_not_meaningful_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            pixels = [b"\0\0\0", b"\xff\0\0"] * 8
            changed = list(pixels)
            changed[0] = b"\0\xff\0"
            _write_ppm(before, 4, 4, pixels)
            _write_ppm(after, 4, 4, changed)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "changed by only 1 pixel",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_uniform_screendump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            _write_ppm(before, 2, 1, [b"\x01\x01\x01", b"\x01\x01\x01"])
            _write_ppm(after, 2, 1, [b"\xff\0\0", b"\0\xff\0"])

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "before framebuffer is uniform",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_black_screendump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            _write_ppm(before, 2, 1, [b"\0\0\0", b"\0\0\0"])
            _write_ppm(after, 2, 1, [b"\xff\0\0", b"\0\xff\0"])

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "before framebuffer is black",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_unchanged_screendump_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            pixels = [b"\0\0\0", b"\xff\0\0"]
            _write_ppm(before, 2, 1, pixels)
            _write_ppm(after, 2, 1, pixels)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "changed by only 0 pixel",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_malformed_screendump_has_a_useful_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            before = root / "before.ppm"
            after = root / "after.ppm"
            before.write_bytes(b"P3\n1 1\n255\n0 0 0\n")
            _write_ppm(after, 1, 1, [b"\xff\0\0"])

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "before framebuffer.*P6",
            ):
                gui_terminal_smoke.validate_framebuffer_change(before, after)

    def test_screendump_capture_uses_a_quoted_temporary_path(self):
        monitor = FakeMonitorSocket()
        with tempfile.TemporaryDirectory(prefix="frontier artifacts ") as temporary:
            output = Path(temporary) / "before frame.ppm"

            def create_dump(_monitor, command, pause=0.25):
                self.assertEqual(
                    command,
                    f'screendump "{output.as_posix()}"',
                )
                output.write_bytes(b"P6\n1 1\n255\n\x01\x02\x03")

            with mock.patch(
                "tools.gui_terminal_smoke.hmp",
                side_effect=create_dump,
            ):
                gui_terminal_smoke.capture_screendump(
                    monitor,
                    output,
                    timeout=1.0,
                )

        self.assertTrue(output.name.endswith(".ppm"))


class FrontierAudioTests(unittest.TestCase):
    def test_non_silent_pcm_capture_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_wav(capture, [0, 0, -8000, 8000, 0])

            evidence = gui_terminal_smoke.validate_wav_audio(capture)

        self.assertEqual(evidence.channels, 1)
        self.assertEqual(evidence.sample_rate, 22050)
        self.assertEqual(evidence.frames, 5)
        self.assertEqual(evidence.peak, 8000)

    def test_qemu_placeholder_lengths_use_the_closed_file_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_qemu_placeholder_wav(capture, [0, -3200, 8000, 0])

            evidence = gui_terminal_smoke.validate_wav_audio(capture)

            self.assertEqual(evidence.frames, 4)
            self.assertEqual(evidence.peak, 8000)

    def test_qemu_placeholder_rejects_partial_pcm_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_qemu_placeholder_wav(capture, [0, 8000])
            with capture.open("ab") as output:
                output.write(b"\x7f")

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "misaligned PCM data",
            ):
                gui_terminal_smoke.validate_wav_audio(capture)

    def test_silent_pcm_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            _write_wav(capture, [0] * 32)

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "audio capture is silent",
            ):
                gui_terminal_smoke.validate_wav_audio(capture)

    def test_malformed_audio_capture_has_a_useful_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / "audio.wav"
            capture.write_bytes(b"not a wave")

            with self.assertRaisesRegex(
                gui_terminal_smoke.FrontierRuntimeContractError,
                "audio capture is not a readable PCM WAV",
            ):
                gui_terminal_smoke.validate_wav_audio(capture)

    def test_qemu_wav_backend_detection_reads_driver_inventory(self):
        available = mock.Mock(
            stdout="Available audio drivers:\nnone\ndsound\nwav\n"
        )
        unavailable = mock.Mock(
            stdout="Available audio drivers:\nnone\ndsound\n"
        )

        with mock.patch(
            "tools.gui_terminal_smoke.subprocess.run",
            side_effect=[available, unavailable],
        ):
            self.assertTrue(
                gui_terminal_smoke.qemu_supports_wav_audio("qemu")
            )
            self.assertFalse(
                gui_terminal_smoke.qemu_supports_wav_audio("qemu")
            )


if __name__ == "__main__":
    unittest.main()

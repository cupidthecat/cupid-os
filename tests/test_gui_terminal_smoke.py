import unittest
from unittest import mock

from tools import gui_terminal_smoke


class FakeMonitorSocket:
    def __init__(self):
        self.sent = []
        self.closed = False

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _size):
        return b"(qemu)"

    def close(self):
        self.closed = True


class GuiTerminalInputTests(unittest.TestCase):
    def test_send_key_holds_usb_report_long_enough_for_guest_polling(self):
        monitor = FakeMonitorSocket()

        with mock.patch("tools.gui_terminal_smoke.time.sleep") as sleep:
            gui_terminal_smoke.send_key(monitor, "slash")

        self.assertEqual(monitor.sent, [b"sendkey slash 300\n"])
        sleep.assert_called_once_with(0.35)

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
        )

        command = gui_terminal_smoke.qemu_args(args, 12345)

        smp_index = command.index("-smp")
        self.assertEqual(command[smp_index + 1], "cpus=4")

    def test_cli_accepts_a_slower_inter_key_pause(self):
        args = gui_terminal_smoke.parse_args(["--key-pause", "0.60"])

        self.assertEqual(args.key_pause, 0.60)

        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            gui_terminal_smoke.parse_args(["--key-pause", "0"])

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


if __name__ == "__main__":
    unittest.main()

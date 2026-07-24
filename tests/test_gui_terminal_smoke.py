import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import gui_terminal_smoke


def _smp_runtime_log():
    required = [
        "[csprng] seeded from RDRAND",
        "mp: discovered 1 CPUs, 1 IOAPIC(s)",
        "acpi: MADT: 4 CPUs, 1 IOAPIC(s)",
        "cpu1: online apic=1",
        "cpu2: online apic=2",
        "cpu3: online apic=3",
        "smp: 4 CPUs online (of 4 discovered)",
        "e1000: init OK",
        "Scheduler started",
        "Entering desktop environment",
        "Terminal launched",
        "[cupidc] JIT execution complete",
        (
            "[tls-selftest] all 62 crypto, ASN.1, and X.509 "
            "checks passed"
        ),
    ]
    checks = [
        f"[tls-selftest] ok: fixture {index}"
        for index in range(62)
    ]
    return "\n".join([*required, *checks]) + "\n"


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


class SmpRuntimeContractTests(unittest.TestCase):
    def test_complete_runtime_log_passes(self):
        gui_terminal_smoke.validate_smp_runtime_log(_smp_runtime_log())

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
        validate.assert_called_once_with(final_log)


if __name__ == "__main__":
    unittest.main()

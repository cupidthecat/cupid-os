import contextlib
import io
import re
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import net_test


class PipeSessionTests(unittest.TestCase):
    def start_process(self, source):
        process = subprocess.Popen(
            [sys.executable, "-u", "-c", source],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.addCleanup(self.stop_process, process)
        return process

    @staticmethod
    def stop_process(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)
        for stream in (process.stdin, process.stdout):
            if stream is not None:
                stream.close()

    def test_expect_consumes_through_match_and_keeps_later_bytes(self):
        process = self.start_process(
            "import sys\n"
            "sys.stdout.buffer.write(b'booted\\r\\n/> trailing')\n"
            "sys.stdout.buffer.flush()\n"
        )
        session = net_test.PipeSession(process)

        before, match = session.expect(re.compile(rb"/>\s*"), timeout=2)
        self.assertEqual(before, b"booted\r\n")
        self.assertEqual(match, b"/> ")
        before, match = session.expect(b"trailing", timeout=2)
        self.assertEqual(before, b"")
        self.assertEqual(match, b"trailing")

    def test_send_flushes_bytes_to_the_child(self):
        process = self.start_process(
            "import sys\n"
            "data = sys.stdin.buffer.read(7)\n"
            "sys.stdout.buffer.write(b'got:' + data)\n"
            "sys.stdout.buffer.flush()\n"
        )
        session = net_test.PipeSession(process)

        session.send(b"status\r")
        before, match = session.expect(b"got:status\r", timeout=2)
        self.assertEqual(before, b"")
        self.assertEqual(match, b"got:status\r")

    def test_timeout_is_distinct_from_end_of_stream(self):
        process = self.start_process(
            "import time\n"
            "time.sleep(2)\n"
        )
        session = net_test.PipeSession(process)

        with self.assertRaisesRegex(
            net_test.PipeExpectTimeout,
            "timed out waiting for serial output",
        ):
            session.expect(b"never", timeout=0.05)

    def test_end_of_stream_reports_unmatched_output(self):
        process = self.start_process(
            "import sys\n"
            "sys.stdout.buffer.write(b'partial output')\n"
            "sys.stdout.buffer.flush()\n"
        )
        session = net_test.PipeSession(process)

        with self.assertRaisesRegex(
            net_test.PipeExpectEOF,
            "partial output",
        ):
            session.expect(b"never", timeout=2)

    def test_socket_transport_uses_the_same_expect_contract(self):
        client, server = socket.socketpair()
        self.addCleanup(client.close)
        self.addCleanup(server.close)
        session = net_test.PipeSession.from_socket(client)
        self.addCleanup(session.close)

        server.sendall(b"serial ready")
        before, match = session.expect(b"ready", timeout=2)
        self.assertEqual(before, b"serial ")
        self.assertEqual(match, b"ready")
        session.send(b"command\r")
        self.assertEqual(server.recv(8), b"command\r")


class QemuArgumentTests(unittest.TestCase):
    def test_e1000_command_keeps_forwarding_and_capture(self):
        command = net_test._qemu_argv(
            "e1000",
            net_test.DEFAULT_IMAGE,
            net_test.PCAP_DIR / "fixture.pcap",
            True,
            serial_port=23456,
        )

        self.assertIn(
            "user,id=n0,hostfwd=tcp::18080-:80",
            command,
        )
        memory_index = command.index("-m")
        self.assertEqual(command[memory_index + 1], "512M")
        self.assertIn("e1000,netdev=n0", command)
        cpu_index = command.index("-cpu")
        self.assertEqual(command[cpu_index + 1], "max")
        self.assertIn(
            "tcp:127.0.0.1:23456,server=on,wait=off",
            command,
        )
        self.assertIn(
            "filter-dump,id=f0,netdev=n0,"
            f"file={net_test.PCAP_DIR / 'fixture.pcap'}",
            command,
        )

    def test_boot_event_matches_a_prompt_or_known_kernel_failure(self):
        for payload in (
            b"boot log\r\n/> ",
            b"boot log\r\nKERNEL PANIC",
            b"boot log\r\nHeap corruption detected",
            b"boot log\r\n[PANIC] CPU Exception",
        ):
            with self.subTest(payload=payload):
                self.assertIsNotNone(net_test.BOOT_EVENT_RE.search(payload))
        self.assertIsNone(net_test.BOOT_EVENT_RE.search(b"boot still running"))

    def test_boot_only_cli_stops_after_the_boot_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "image.bin"
            image.touch()
            with mock.patch.object(net_test, "run", return_value=True) as run:
                status = net_test.main(
                    [
                        "--nic",
                        "e1000",
                        "--image",
                        str(image),
                        "--boot-only",
                    ]
                )

        self.assertEqual(status, 0)
        run.assert_called_once_with(
            "e1000",
            image,
            False,
            boot_only=True,
        )

    def test_boot_only_run_does_not_reserve_the_server_port(self):
        qemu = mock.Mock()
        qemu.boot.return_value = "booted"
        qemu.process = None
        qemu.pcap = Path("tests/boot-only.pcap")
        with mock.patch.object(
            net_test,
            "QemuNet",
            return_value=qemu,
        ) as qemu_type:
            with contextlib.redirect_stdout(io.StringIO()):
                ok = net_test.run(
                    "e1000",
                    net_test.DEFAULT_IMAGE,
                    False,
                    boot_only=True,
                )

        self.assertTrue(ok)
        qemu_type.assert_called_once_with(
            nic="e1000",
            image=net_test.DEFAULT_IMAGE,
            hostfwd=False,
        )

    def test_missing_image_names_the_supported_headless_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "missing.img"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                status = net_test.main(["--image", str(image)])

        self.assertEqual(status, 2)
        self.assertIn("make headless-net-image", stderr.getvalue())

    def test_guest_failure_interrupts_shell_and_marker_waits(self):
        for method, arguments in (
            ("shell", ("status",)),
            ("collect_until", (b"ready",)),
        ):
            with self.subTest(method=method):
                qemu = net_test.QemuNet()
                qemu.session = mock.Mock()
                qemu.session.expect.return_value = (
                    b"partial guest output\r\n",
                    b"KERNEL PANIC",
                )

                with self.assertRaisesRegex(
                    RuntimeError,
                    "guest failed while",
                ):
                    getattr(qemu, method)(*arguments)

    def test_server_requires_a_live_prompt_after_pass(self):
        for prompt_result, expected_detail in (
            (
                net_test.PipeExpectTimeout("prompt timeout"),
                "server did not return to the shell",
            ),
            (
                net_test.PipeExpectEOF("serial closed"),
                "server did not return to the shell",
            ),
        ):
            with self.subTest(error=type(prompt_result).__name__):
                qemu = mock.Mock()
                qemu._expect_guest.side_effect = prompt_result
                connection = mock.Mock()
                connection.recv.side_effect = [b"Hello CupidOS", b""]
                with mock.patch.object(
                    net_test.socket,
                    "create_connection",
                    return_value=connection,
                ):
                    result = net_test.test_tcp_server(qemu)

                self.assertFalse(result.ok)
                self.assertIn(expected_detail, result.detail)
                qemu._expect_guest.assert_called_once_with(
                    net_test.PROMPT,
                    timeout=5,
                    activity="waiting for the shell after feature22",
                )

    def test_server_propagates_a_panic_after_pass(self):
        qemu = mock.Mock()
        qemu._expect_guest.side_effect = RuntimeError(
            "guest failed while waiting for the shell after feature22"
        )
        connection = mock.Mock()
        connection.recv.side_effect = [b"Hello CupidOS", b""]
        with mock.patch.object(
            net_test.socket,
            "create_connection",
            return_value=connection,
        ):
            with self.assertRaisesRegex(
                RuntimeError,
                "guest failed while waiting for the shell",
            ):
                net_test.test_tcp_server(qemu)

    def test_server_closes_the_host_socket_after_failure(self):
        qemu = mock.Mock()
        connection = mock.Mock()
        connection.sendall.side_effect = OSError("send failed")
        with mock.patch.object(
            net_test.socket,
            "create_connection",
            return_value=connection,
        ):
            result = net_test.test_tcp_server(qemu)

        self.assertFalse(result.ok)
        self.assertIn("send failed", result.detail)
        connection.close.assert_called_once_with()

    def test_server_bounds_the_host_response(self):
        cases = (
            (
                "byte-limit",
                [b"x" * 40000, b"y" * 40000],
                None,
                "response exceeded",
            ),
            (
                "deadline",
                [b""],
                [0.0, 30.0],
                "response deadline",
            ),
        )
        for name, chunks, clock, message in cases:
            with self.subTest(case=name):
                qemu = mock.Mock()
                connection = mock.Mock()
                connection.recv.side_effect = chunks
                patches = [
                    mock.patch.object(
                        net_test.socket,
                        "create_connection",
                        return_value=connection,
                    )
                ]
                if clock is not None:
                    patches.append(
                        mock.patch.object(
                            net_test.time,
                            "monotonic",
                            side_effect=clock,
                        )
                    )
                with patches[0]:
                    if len(patches) == 2:
                        with patches[1]:
                            result = net_test.test_tcp_server(qemu)
                    else:
                        result = net_test.test_tcp_server(qemu)

                self.assertFalse(result.ok)
                self.assertIn(message, result.detail)
                connection.close.assert_called_once_with()

    def test_serial_connection_failure_includes_qemu_stderr(self):
        process = mock.Mock()
        process.poll.return_value = 1
        process.returncode = 1

        with self.assertRaisesRegex(
            RuntimeError,
            "QEMU stderr: 'bad serial option'",
        ):
            net_test._connect_qemu_serial(
                12345,
                process,
                1,
                diagnostics=lambda: "bad serial option",
            )

    def test_keep_retains_a_running_guest_only_after_failure(self):
        failed_qemu = mock.Mock()
        failed_qemu.boot.side_effect = RuntimeError("guest failed")
        failed_qemu.process.poll.return_value = None
        failed_qemu.process.pid = 1234
        with mock.patch.object(net_test, "QemuNet", return_value=failed_qemu):
            with contextlib.redirect_stdout(io.StringIO()):
                ok = net_test.run(
                    "rtl8139",
                    net_test.DEFAULT_IMAGE,
                    True,
                    boot_only=True,
                )

        self.assertFalse(ok)
        failed_qemu.stop.assert_not_called()

        successful_qemu = mock.Mock()
        successful_qemu.boot.return_value = "booted"
        successful_qemu.process.poll.return_value = None
        with mock.patch.object(net_test, "QemuNet", return_value=successful_qemu):
            with contextlib.redirect_stdout(io.StringIO()):
                ok = net_test.run(
                    "rtl8139",
                    net_test.DEFAULT_IMAGE,
                    True,
                    boot_only=True,
                )

        self.assertTrue(ok)
        successful_qemu.stop.assert_called_once_with()


class LiveCheckContractTests(unittest.TestCase):
    def test_dhcp_requires_the_default_slirp_lease(self):
        for output, expected in (
            ("rtl8139 ip=10.0.2.15", True),
            ("rtl8139 ip=10.0.2.99", False),
        ):
            with self.subTest(output=output):
                qemu = mock.Mock()
                qemu.shell.return_value = output

                result = net_test.test_dhcp(qemu, "")

                self.assertEqual(result.ok, expected)

    def test_ping_requires_both_requested_replies(self):
        for output, expected in (
            ("sent=2 recv=2", True),
            ("sent=2 recv=1 rtt_ms=4", False),
            ("seq=1 rtt_ms=4", False),
        ):
            with self.subTest(output=output):
                qemu = mock.Mock()
                qemu.shell.return_value = output

                result = net_test.test_ping_gw(qemu)

                self.assertEqual(result.ok, expected)

    def test_arp_requires_the_default_gateway_entry(self):
        for output, expected in (
            ("10.0.2.2 -> 52:55:0a:00:02:02", True),
            ("10.0.2.9 -> 52:55:0a:00:02:09", False),
        ):
            with self.subTest(output=output):
                qemu = mock.Mock()
                qemu.shell.return_value = output

                result = net_test.test_arp(qemu)

                self.assertEqual(result.ok, expected)


if __name__ == "__main__":
    unittest.main()

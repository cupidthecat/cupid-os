import unittest
from unittest import mock

from tools import gui_terminal_smoke


class FakeMonitorSocket:
    def __init__(self):
        self.sent = []

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _size):
        return b"(qemu)"


class GuiTerminalInputTests(unittest.TestCase):
    def test_send_key_holds_usb_report_long_enough_for_guest_polling(self):
        monitor = FakeMonitorSocket()

        with mock.patch("tools.gui_terminal_smoke.time.sleep") as sleep:
            gui_terminal_smoke.send_key(monitor, "slash")

        self.assertEqual(monitor.sent, [b"sendkey slash 300\n"])
        sleep.assert_called_once_with(0.35)


if __name__ == "__main__":
    unittest.main()

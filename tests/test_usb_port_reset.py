import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbPortResetContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "kernel" / "usb" / "usb_hc.h").read_text(
            encoding="utf-8"
        )
        cls.ehci = (ROOT / "kernel" / "usb" / "ehci.c").read_text(
            encoding="utf-8"
        )
        cls.uhci = (ROOT / "kernel" / "usb" / "uhci.c").read_text(
            encoding="utf-8"
        )

    @staticmethod
    def _function(source, signature):
        match = re.search(
            re.escape(signature) + r"[^;]*?\)\s*\{.*?\n\}",
            source,
            re.S,
        )
        if match is None:
            raise AssertionError(f"missing function {signature}")
        return match.group(0)

    def test_reset_results_distinguish_failure_success_and_handoff(self):
        for name, value in (
            ("USB_PORT_RESET_FAILED", "-1"),
            ("USB_PORT_RESET_OK", "0"),
            ("USB_PORT_RESET_HANDOFF", "1"),
        ):
            self.assertRegex(self.header, rf"{name}\s*=\s*{value}")
        self.assertIn(
            "usb_port_reset_result_t (*port_reset)",
            self.header,
        )
        self.assertIn("bool must_reset", self.header)

    def test_ehci_only_bypasses_reset_for_low_speed_without_quarantine(self):
        reset = self._function(
            self.ehci,
            "static usb_port_reset_result_t ehci_port_reset(",
        )
        pre_reset = reset[: reset.index("v &= ~EHCI_PORTSC_ENABLE;")]
        self.assertIn("if (ls == 0x1u && !must_reset)", pre_reset)
        self.assertNotIn("ls == 0x2u", pre_reset)
        handoff = pre_reset.index("return ehci_handoff_port(c, port, v);")
        self.assertLess(pre_reset.index("ls == 0x1u"), handoff)

    def test_ehci_waits_for_high_speed_enable_before_handoff(self):
        reset = self._function(
            self.ehci,
            "static usb_port_reset_result_t ehci_port_reset(",
        )
        wait = reset.index("for (int i = 0; i < 100; i++)")
        delay = reset.index("timer_delay_us(1000u);", wait)
        final_status = reset.index(
            "uint32_t final = ehci_op_read",
            delay,
        )
        handoff = reset.index(
            "return ehci_handoff_port(c, port, final);",
            final_status,
        )
        self.assertLess(wait, delay)
        self.assertLess(delay, final_status)
        self.assertLess(final_status, handoff)

    def test_ehci_only_completes_a_handoff_that_latched(self):
        handoff = self._function(
            self.ehci,
            "static usb_port_reset_result_t ehci_handoff_port(",
        )
        write = handoff.index("status | EHCI_PORTSC_OWNER")
        readback = handoff.index("& EHCI_PORTSC_OWNER", write)
        failed = handoff.index("return USB_PORT_RESET_FAILED;", readback)
        complete = handoff.index("return USB_PORT_RESET_HANDOFF;", failed)
        self.assertLess(write, readback)
        self.assertLess(readback, failed)
        self.assertLess(failed, complete)

    def test_uhci_rejects_an_incomplete_or_disconnected_reset(self):
        reset = self._function(
            self.uhci,
            "static usb_port_reset_result_t uhci_port_reset(",
        )
        final_status = reset.index(
            "uint16_t final = inw(",
        )
        for condition in (
            "(final & UHCI_PORT_CONNECT) == 0u",
            "(final & UHCI_PORT_RESET) != 0u",
            "(final & UHCI_PORT_ENABLE) == 0u",
        ):
            self.assertIn(condition, reset[final_status:])
        failed = reset.index(
            "return USB_PORT_RESET_FAILED;",
            final_status,
        )
        complete = reset.index(
            "return USB_PORT_RESET_OK;",
            failed,
        )
        self.assertLess(failed, complete)


if __name__ == "__main__":
    unittest.main()

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbPollSerializationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.usb = (ROOT / "kernel" / "usb" / "usb.c").read_text(
            encoding="utf-8"
        )
        cls.header = (ROOT / "kernel" / "usb" / "usb.h").read_text(
            encoding="utf-8"
        )
        cls.kernel = (ROOT / "kernel" / "core" / "kernel.c").read_text(
            encoding="utf-8"
        )
        cls.doom = (
            ROOT / "kernel" / "doom" / "doomgeneric_cupidos.c"
        ).read_text(encoding="utf-8")
        cls.hid = (ROOT / "kernel" / "usb" / "usb_hid.c").read_text(
            encoding="utf-8"
        )
        cls.ehci = (ROOT / "kernel" / "usb" / "ehci.c").read_text(
            encoding="utf-8"
        )

    def test_overlapping_pollers_are_folded_into_one_usb_cycle(self):
        poll = re.search(
            r"void usb_poll\(void\) \{.*?\n\}",
            self.usb,
            re.S,
        )
        self.assertIsNotNone(poll)
        body = poll.group(0)
        self.assertIn(
            "__atomic_exchange_n(\n"
            "            &usb_poll_active,\n"
            "            1u,\n"
            "            __ATOMIC_ACQUIRE",
            body,
        )
        self.assertIn(
            "__atomic_store_n(&usb_poll_active, 0u, __ATOMIC_RELEASE);",
            body,
        )
        for call in (
            "ehci_poll_ports();",
            "ehci_poll_interrupts();",
            "uhci_poll_ports();",
            "uhci_poll_interrupts();",
            "usb_process_pending();",
        ):
            self.assertIn(call, body)

    def test_kernel_and_doom_use_the_serialized_entry_point(self):
        self.assertIn("void usb_poll(void);", self.header)
        self.assertIn("usb_poll();", self.kernel)
        self.assertIn("usb_poll();", self.doom)
        for direct_call in (
            "ehci_poll_ports();",
            "ehci_poll_interrupts();",
            "uhci_poll_ports();",
            "uhci_poll_interrupts();",
            "usb_process_pending();",
        ):
            self.assertNotIn(direct_call, self.kernel)
            self.assertNotIn(direct_call, self.doom)

    def test_ehci_irq_defers_port_work_to_the_serialized_poll(self):
        irq = re.search(
            r"static void ehci_irq_handler_fn\(usb_hc_t \*hc\) \{"
            r".*?\n\}",
            self.ehci,
            re.S,
        )
        self.assertIsNotNone(irq)
        irq_body = irq.group(0)
        self.assertIn("&c->pending_ports", irq_body)
        self.assertIn("__atomic_fetch_or", irq_body)
        self.assertNotIn("usb_port_change(", irq_body)

        poll = re.search(
            r"void ehci_poll_ports\(void\) \{.*?\n\}",
            self.ehci,
            re.S,
        )
        self.assertIsNotNone(poll)
        poll_body = poll.group(0)
        self.assertIn("__atomic_exchange_n(", poll_body)
        self.assertIn("usb_port_change(&c->hc, port)", poll_body)

    def test_mouse_runtime_evidence_is_rate_limited(self):
        self.assertIn(
            "now_ms - hid_mouse_last_log_ms >= 250u",
            self.hid,
        )
        self.assertIn(
            "usb_hid: mouse activity report=%u",
            self.hid,
        )


if __name__ == "__main__":
    unittest.main()

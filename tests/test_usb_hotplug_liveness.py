import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsbHotplugLivenessContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "kernel" / "usb" / "usb.h").read_text(
            encoding="utf-8"
        )
        cls.usb = (ROOT / "kernel" / "usb" / "usb.c").read_text(
            encoding="utf-8"
        )
        cls.ehci = (ROOT / "kernel" / "usb" / "ehci.c").read_text(
            encoding="utf-8"
        )
        cls.uhci = (ROOT / "kernel" / "usb" / "uhci.c").read_text(
            encoding="utf-8"
        )
        cls.hub = (ROOT / "kernel" / "usb" / "usb_hub.c").read_text(
            encoding="utf-8"
        )

    @staticmethod
    def _slice(source, start, end):
        begin = source.index(start)
        finish = source.index(end, begin)
        return source[begin:finish]

    def test_hub_bitmap_includes_the_hub_status_bit(self):
        def bitmap_bytes(port_count):
            return (port_count + 8) // 8

        self.assertEqual(
            [bitmap_bytes(n) for n in (1, 7, 8, 15, 63)],
            [1, 1, 2, 2, 8],
        )
        probe = self._slice(
            self.hub,
            "static usb_probe_result_t hub_probe(",
            "static int hub_disconnect(",
        )
        self.assertIn(
            "((uint32_t)nports + 8u) / 8u",
            probe,
        )
        self.assertIn(
            "bitmap_bytes_wide > HUB_CHANGE_BUFFER_SIZE",
            probe,
        )
        self.assertIn("t.length = (uint32_t)bitmap_bytes;", probe)
        self.assertNotIn("(nports + 7) / 8", probe)

    def test_hub_uses_a_descriptor_backed_interrupt_endpoint(self):
        endpoint = self._slice(
            self.hub,
            "static usb_probe_result_t hub_find_change_endpoint(",
            "static void hub_handle_port_change(",
        )
        self.assertIn("type == USB_DESC_INTERFACE", endpoint)
        self.assertIn("type == USB_DESC_ENDPOINT", endpoint)
        self.assertIn("USB_ENDPOINT_INTERRUPT", endpoint)
        self.assertIn("packet_size &= 0x07FFu;", endpoint)
        self.assertIn(
            "packet_size < (uint16_t)bitmap_bytes",
            endpoint,
        )
        self.assertIn("packet_size > 0xFFu", endpoint)
        self.assertIn(
            "(address & USB_ENDPOINT_NUMBER) == 0u",
            endpoint,
        )

        probe = self._slice(
            self.hub,
            "static usb_probe_result_t hub_probe(",
            "static int hub_disconnect(",
        )
        self.assertIn("hub_find_change_endpoint(", probe)
        self.assertIn("t.max_packet = change_max_packet;", probe)
        self.assertNotIn("t.max_packet = 1;", probe)
        self.assertIn(
            'KWARN("usb_hub: no usable interrupt IN status endpoint");',
            probe,
        )

    def test_hub_acknowledges_changes_only_after_safe_handoff(self):
        handler = self._slice(
            self.hub,
            "static void hub_handle_port_change(",
            "static void hub_status_cb(",
        )
        self.assertIn(
            "if (!usb_hub_port_change(st->dev, port))",
            handler,
        )
        self.assertNotIn("HUB_CLEAR_FEATURE", handler)
        self.assertNotIn("hub_remove_port_devices", handler)

        reconcile = self._slice(
            self.usb,
            "static usb_work_result_t reconcile_hub_port(",
            "static usb_work_result_t enumerate_port(",
        )
        reconciled = reconcile.index("w->reconciled = true;")
        acknowledgement = reconcile.index(
            "USB_HUB_C_PORT_CONNECTION",
            reconciled,
        )
        self.assertLess(reconciled, acknowledgement)
        self.assertIn("return USB_WORK_RETRY;", reconcile[acknowledgement:])
        self.assertIn(
            "current_connected != w->reconciled_connected",
            reconcile,
        )

    def test_a_full_work_queue_keeps_root_port_work_pending(self):
        push = self._slice(
            self.usb,
            "static bool workq_push(",
            "bool usb_port_change(",
        )
        self.assertIn("return true;", push)
        self.assertIn("return false;", push)
        self.assertIn("usb: work queue full, deferring port event", push)
        self.assertLess(
            push.index("if (next == workq_tail)"),
            push.index("workq_head = next;"),
        )

        self.assertIn(
            "bool usb_port_change    (usb_hc_t *hc, int port);",
            self.header,
        )
        self.assertIn(
            "bool usb_hub_port_change(usb_device_t *hub, int port);",
            self.header,
        )

        ehci_poll = self._slice(
            self.ehci,
            "void ehci_poll_ports(void) {",
            "void ehci_poll_interrupts(void);",
        )
        self.assertIn("if (!usb_port_change(&c->hc, port))", ehci_poll)
        self.assertIn("retry |= 1u << (uint32_t)port;", ehci_poll)
        self.assertIn("&c->pending_ports", ehci_poll)
        self.assertIn("__atomic_fetch_or(", ehci_poll)

        uhci_poll = self._slice(
            self.uhci,
            "void uhci_poll_ports(void) {",
            "\n}",
        )
        accepted = uhci_poll.index("if (usb_port_change(&c->hc, p))")
        acknowledged = uhci_poll.index(
            "outw((uint16_t)(c->io_base + UHCI_PORTSC(p))",
            accepted,
        )
        self.assertLess(accepted, acknowledged)


if __name__ == "__main__":
    unittest.main()

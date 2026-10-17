import re
import unittest
from pathlib import Path


USB_SOURCE = (
    Path(__file__).resolve().parent.parent / "kernel" / "usb" / "usb.cc"
)
USB_HEADER = (
    Path(__file__).resolve().parent.parent / "kernel" / "usb" / "usb.h"
)
USB_HID_SOURCE = (
    Path(__file__).resolve().parent.parent / "kernel" / "usb" / "usb_hid.cc"
)
USB_HUB_SOURCE = (
    Path(__file__).resolve().parent.parent / "kernel" / "usb" / "usb_hub.cc"
)
USB_MSC_SOURCE = (
    Path(__file__).resolve().parent.parent / "kernel" / "usb" / "usb_msc.cc"
)


class UsbEnumerationRecoveryContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = USB_SOURCE.read_text(encoding="utf-8")
        cls.header = USB_HEADER.read_text(encoding="utf-8")
        cls.hid = USB_HID_SOURCE.read_text(encoding="utf-8")
        cls.hub = USB_HUB_SOURCE.read_text(encoding="utf-8")
        cls.msc = USB_MSC_SOURCE.read_text(encoding="utf-8")

    def test_enumeration_retries_control_requests_with_real_delays(self):
        self.assertIn("#define USB_CONTROL_ATTEMPTS 5u", self.source)
        self.assertIn("#define USB_CONTROL_RETRY_US 10000u", self.source)
        self.assertIn("int usb_control_retry(", self.source)
        self.assertNotIn("static int usb_control_retry(", self.source)
        self.assertIn("int usb_control_retry(", self.header)
        self.assertIn(
            "timer_delay_us(USB_CONTROL_RETRY_US);",
            self.source,
        )
        self.assertNotIn("timer_sleep_ms(", self.source)
        self.assertGreaterEqual(self.source.count("usb_control_retry("), 5)

    def test_matching_drivers_report_retryable_setup_failures(self):
        for source in (self.hid, self.hub, self.msc):
            self.assertIn("USB_PROBE_NOT_SUPPORTED", source)
            self.assertIn("USB_PROBE_BOUND", source)
            self.assertIn("USB_PROBE_RETRY", source)
            self.assertIn("usb_control_retry(", source)

        self.assertIn("USB_PROBE_REJECTED", self.hub)
        self.assertIn("USB_PROBE_REJECTED", self.msc)
        self.assertIn(
            "usb_probe_result_t endpoint_result",
            self.hub,
        )

    def test_probe_outcomes_have_distinct_public_values(self):
        expected = {
            "USB_PROBE_NOT_SUPPORTED": "-1",
            "USB_PROBE_BOUND": "0",
            "USB_PROBE_RETRY": "1",
            "USB_PROBE_REJECTED": "2",
        }
        for name, value in expected.items():
            self.assertRegex(
                self.header,
                rf"{name}\s*=\s*{value}",
            )
        self.assertIn(
            "usb_probe_result_t (*probe)(usb_device_t *dev);",
            self.header,
        )

    def test_invalid_probe_outcomes_retry_without_publishing_a_device(self):
        invalid = self.source.index(
            "returned invalid probe result %d"
        )
        unwind = self.source.index(
            "unwind_enumeration(w, dev);",
            invalid,
        )
        retry = self.source.index(
            "return USB_WORK_RETRY;",
            unwind,
        )
        self.assertLess(invalid, unwind)
        self.assertLess(unwind, retry)

    def test_ambiguous_set_address_probes_the_new_address(self):
        assign = re.search(
            r"static int usb_assign_address\(.*?\n\}",
            self.source,
            re.S,
        )
        self.assertIsNotNone(assign)
        body = assign.group(0)
        self.assertIn("dev->address = 0;", body)
        self.assertIn("dev->address = address;", body)
        self.assertIn("(uint16_t)(0x01 << 8)", body)
        self.assertIn("descriptor,\n                8", body)

    def test_configuration_failure_unwinds_before_retry(self):
        self.assertIn(
            'KWARN("usb: SET_CONFIGURATION failed");',
            self.source,
        )
        self.assertRegex(
            self.source,
            re.compile(
                r"SET_CONFIGURATION failed\"\);\s*"
                r"unwind_enumeration\(w, dev\);\s*"
                r"return USB_WORK_RETRY;",
                re.S,
            ),
        )

    def test_ambiguous_set_configuration_queries_active_state(self):
        configure = re.search(
            r"static int usb_set_configuration\(.*?\n\}",
            self.source,
            re.S,
        )
        self.assertIsNotNone(configure)
        body = configure.group(0)
        self.assertIn("0x09,", body)
        self.assertIn("0x08,", body)
        self.assertIn("&active,", body)
        self.assertIn("active == configuration", body)

    def test_root_reconnect_retires_the_previous_port_generation(self):
        reconcile_root = re.search(
            r"static usb_work_result_t reconcile_root_port\(.*?\n\}",
            self.source,
            re.S,
        )
        self.assertIsNotNone(reconcile_root)
        body = reconcile_root.group(0)
        connected_reconcile = body.index(
            "if (remove_root_device(hc, port) < 0)"
        )
        reset = body.index("hc->port_reset(")
        allocate = body.index("return enumerate_device(w, status);")
        self.assertLess(connected_reconcile, reset)
        self.assertLess(reset, allocate)

    def test_stale_hub_work_is_rejected_by_generation(self):
        self.assertIn("uint32_t      hub_generation;", self.source)
        self.assertIn(
            "hub ? hub->generation : 0u",
            self.source,
        )
        enumerate_port = re.search(
            r"static usb_work_result_t enumerate_port\(.*?\n\}",
            self.source,
            re.S,
        )
        self.assertIsNotNone(enumerate_port)
        body = enumerate_port.group(0)
        self.assertIn("!pHub->in_use", body)
        self.assertIn("pHub->generation != w->hub_generation", body)
        self.assertLess(
            body.index("pHub->generation != w->hub_generation"),
            body.index("reconcile_hub_port(w)"),
        )


if __name__ == "__main__":
    unittest.main()

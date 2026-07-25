import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
CONTROLLER_SOURCES = (
    "kernel/usb/ehci.c",
    "kernel/usb/uhci.c",
)


class UsbTransferTimeoutContractTests(unittest.TestCase):
    def test_synchronous_transfers_use_irq_independent_elapsed_time(self):
        for relative in CONTROLLER_SOURCES:
            with self.subTest(source=relative):
                source = (REPO_ROOT / relative).read_text(encoding="utf-8")
                self.assertIn('#include "timer.h"', source)
                self.assertIn("timeout_ms * 1000u", source)
                self.assertIn("timer_delay_us(delay_us);", source)
                self.assertIn("waited_us += delay_us;", source)
                self.assertNotIn("timer_get_uptime_ms()", source)


if __name__ == "__main__":
    unittest.main()

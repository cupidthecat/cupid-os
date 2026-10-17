import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class KeyboardSubscriberIsolationTests(unittest.TestCase):
    def test_usb_keyboard_uses_the_production_subscriber_path(self):
        hid = (ROOT / "kernel" / "usb" / "usb_hid.cc").read_text(
            encoding="utf-8"
        )
        driver = (ROOT / "drivers" / "keyboard.cc").read_text(
            encoding="utf-8"
        )

        self.assertIn("keyboard_inject_scancode(hid_to_ps2[k]);", hid)
        injected = re.search(
            r"void keyboard_inject_scancode\(uint8_t raw_scancode\) "
            r"\{.*?\n\}",
            driver,
            re.S,
        )
        self.assertIsNotNone(injected)
        self.assertIn("fire_subscriber(raw_scancode);", injected.group(0))

    def test_guest_smoke_waits_for_real_modifier_input(self):
        program = (ROOT / "bin" / "kbdsub_test.cc").read_text(
            encoding="utf-8"
        )
        driver = (ROOT / "drivers" / "keyboard.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "[kbdsub] waiting for USB Shift make/break",
            program,
        )
        self.assertIn("yield();", program)
        self.assertIn("last_sc != 0x2A", program)
        self.assertNotIn("keyboard_test_sub_inject", program)
        self.assertIn("if (sc != 0x2Au) return;", driver)

        adapter = (ROOT / "kernel" / "lang" / "cupidc.cc").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('BIND("keyboard_test_sub_inject"', adapter)


if __name__ == "__main__":
    unittest.main()

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class OptionalPs2InputTests(unittest.TestCase):
    def test_keyboard_controller_wait_is_bounded(self):
        source = (ROOT / "drivers" / "keyboard.cc").read_text(
            encoding="utf-8"
        )
        wait = re.search(
            r"static bool keyboard_wait_input_empty\(void\) \{.*?\n\}",
            source,
            re.S,
        )
        self.assertIsNotNone(wait)
        self.assertIn("PS2_INPUT_WAIT_ATTEMPTS", wait.group(0))
        self.assertIn("status == 0xFFu", wait.group(0))
        self.assertNotRegex(
            source,
            r"while \(inb\(KEYBOARD_STATUS_PORT\)",
        )

    def test_mouse_skips_an_absent_controller(self):
        source = (ROOT / "drivers" / "mouse.cc").read_text(
            encoding="utf-8"
        )
        self.assertIn("if (inb(0x64) == 0xFFu)", source)
        self.assertIn(
            "PS/2 mouse unavailable; USB input remains available",
            source,
        )


if __name__ == "__main__":
    unittest.main()

import unittest

from tests.raw_x86_oracle import canonicalize_size_prefix_order


def mapped(image, mode="code16", ranges=None):
    return ("cupid.raw-map.v2\nsize " + str(len(image)) + "\nbase 0x00007c00\nedges 0\n"
            + (ranges or "range 0 " + mode + "\n"))


def rendered(image, address=0x7C00, text="mov eax, dword [ds:esi]"):
    return f"{address:08X}:  " + " ".join(f"{byte:02X}" for byte in image) + "  " + text + "\n"


class RawX86OracleTests(unittest.TestCase):
    def test_only_adjacent_instruction_size_prefixes_commute(self):
        first = bytes.fromhex("3e66678b06")
        second = bytes.fromhex("3e67668b06")
        self.assertEqual(canonicalize_size_prefix_order(first, mapped(first), rendered(first)),
                         canonicalize_size_prefix_order(second, mapped(second), rendered(second)))

    def test_immediate_bytes_are_preserved(self):
        first = bytes.fromhex("b86667")
        second = bytes.fromhex("b86766")
        self.assertNotEqual(canonicalize_size_prefix_order(first, mapped(first), rendered(first)),
                            canonicalize_size_prefix_order(second, mapped(second), rendered(second)))

    def test_data_bytes_are_preserved(self):
        first = bytes.fromhex("906667")
        second = bytes.fromhex("906766")
        mapping = mapped(first, ranges="range 0 code16\nrange 1 data\n")
        self.assertNotEqual(canonicalize_size_prefix_order(first, mapping, rendered(first[:1], text="nop")),
                            canonicalize_size_prefix_order(second, mapping, rendered(second[:1], text="nop")))

    def test_code32_is_outside_the_boot_oracle_exception(self):
        first = bytes.fromhex("3e66678b06")
        second = bytes.fromhex("3e67668b06")
        self.assertNotEqual(canonicalize_size_prefix_order(first, mapped(first,"code32"), rendered(first)),
                            canonicalize_size_prefix_order(second, mapped(second,"code32"), rendered(second)))

    def test_opcode_and_operand_bytes_are_preserved(self):
        first = bytes.fromhex("3e66678b06")
        for changed in [bytes.fromhex("3e67668906"), bytes.fromhex("3e67668b07")]:
            with self.subTest(changed=changed.hex()):
                self.assertNotEqual(canonicalize_size_prefix_order(first, mapped(first), rendered(first)),
                                    canonicalize_size_prefix_order(changed, mapped(changed), rendered(changed)))

    def test_stale_rendering_duplicate_prefixes_and_bad_maps_fail_closed(self):
        image = bytes.fromhex("3e66678b06")
        duplicate = bytes.fromhex("6666678b06")
        probes = [(image,mapped(image),rendered(bytes.fromhex("3e67668b06"))),
                  (duplicate,mapped(duplicate),rendered(duplicate)),
                  (image,mapped(image),"[disassembly raw]\n"),
                  (image,mapped(image).replace("size 5","size 6"),rendered(image)),
                  (image,mapped(image,ranges="range 0 code16\nrange 2 data\n"),rendered(image))]
        for payload, mapping, listing in probes:
            with self.subTest(payload=payload.hex(), mapping=mapping, listing=listing):
                with self.assertRaises(ValueError):
                    canonicalize_size_prefix_order(payload,mapping,listing)


if __name__ == "__main__":
    unittest.main()

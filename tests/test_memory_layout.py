import re
import struct
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def _macro_value(source: str, name: str) -> int:
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+)u?"
        r"(?:\s*/\*.*\*/)?\s*$",
        source,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"missing fixed-address macro {name}")
    return int(match.group(1), 16)


class MemoryLayoutContractTests(unittest.TestCase):
    def test_fixed_regions_keep_size_alignment_and_adjacency(self):
        header = (REPO_ROOT / "kernel/mm/memory.h").read_text(
            encoding="utf-8"
        )
        stack_bottom = _macro_value(header, "STACK_BOTTOM")
        stack_top = _macro_value(header, "STACK_TOP")
        external_start = _macro_value(
            header, "EXTERNAL_EXEC_ARENA_START"
        )
        external_end = _macro_value(header, "EXTERNAL_EXEC_ARENA_END")
        cupidc_start = _macro_value(header, "CUPIDC_EXEC_ARENA_START")

        self.assertEqual(stack_bottom, 0x00C00000)
        self.assertEqual(stack_top, 0x00E00000)
        self.assertEqual(external_start, 0x00E00000)
        self.assertEqual(external_end, 0x01000000)
        self.assertEqual(cupidc_start, 0x01000000)
        self.assertEqual(stack_top - stack_bottom, 2 * 1024 * 1024)
        self.assertEqual(external_end - external_start, 2 * 1024 * 1024)
        self.assertEqual(stack_top, external_start)
        self.assertEqual(external_end, cupidc_start)
        for address in (
            stack_bottom,
            stack_top,
            external_start,
            external_end,
            cupidc_start,
        ):
            self.assertEqual(address % 4096, 0)

    def test_link_boot_kernel_and_user_build_use_the_same_boundaries(self):
        linker = (REPO_ROOT / "link.ld").read_text(encoding="utf-8")
        boot = (REPO_ROOT / "boot/boot.asm").read_text(encoding="utf-8")
        kernel = (REPO_ROOT / "kernel/core/kernel.c").read_text(
            encoding="utf-8"
        )
        root_makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        user_makefile = (REPO_ROOT / "user/Makefile").read_text(
            encoding="utf-8"
        )

        self.assertIn("ASSERT(_kernel_end <= 0xC00000,", linker)
        self.assertRegex(boot, r"(?m)^\s*mov esp, 0xE00000\s+;")
        self.assertIn('"mov $0xE00000, %%esp\\n"', kernel)
        self.assertIn("USER_TEXT_ADDRESS ?= 0x00E00000", user_makefile)
        self.assertRegex(
            user_makefile,
            r"(?m)^\$\(BUILD\)/%: \$\(BUILD\)/%\.o \$\(CUPIDLD\) Makefile$",
        )
        for target in (
            "kernel/core/kernel.o",
            "kernel/core/process.o",
            "kernel/lang/exec.o",
        ):
            rule = re.search(
                rf"(?m)^{re.escape(target)}: ([^\r\n]+)$",
                root_makefile,
            )
            self.assertIsNotNone(rule, target)
            self.assertIn("kernel/mm/memory.h", rule.group(1))

    def test_tracked_user_executables_use_the_current_external_arena(self):
        for name in ("hello", "ls", "cat"):
            with self.subTest(program=name):
                image = (REPO_ROOT / "user/build" / name).read_bytes()
                header = struct.unpack_from("<16sHHIIIIIHHHHHH", image, 0)
                ident = header[0]
                file_type = header[1]
                machine = header[2]
                entry = header[4]
                program_offset = header[5]
                program_entry_size = header[9]
                program_count = header[10]
                self.assertEqual(ident[:6], b"\x7fELF\x01\x01")
                self.assertEqual(file_type, 2)
                self.assertEqual(machine, 3)
                self.assertEqual(entry, 0x00E00000)

                entry_is_executable = False
                load_count = 0
                for index in range(program_count):
                    offset = program_offset + index * program_entry_size
                    program = struct.unpack_from("<IIIIIIII", image, offset)
                    (
                        segment_type,
                        file_offset,
                        virtual_address,
                        _physical_address,
                        file_size,
                        memory_size,
                        flags,
                        _alignment,
                    ) = program
                    if segment_type != 1:
                        continue
                    load_count += 1
                    self.assertGreaterEqual(virtual_address, 0x00E00000)
                    self.assertLessEqual(
                        virtual_address + memory_size, 0x01000000
                    )
                    self.assertLessEqual(file_offset + file_size, len(image))
                    if (
                        flags & 1
                        and virtual_address
                        <= entry
                        < virtual_address + file_size
                    ):
                        entry_is_executable = True
                self.assertGreater(load_count, 0)
                self.assertTrue(entry_is_executable)


if __name__ == "__main__":
    unittest.main()

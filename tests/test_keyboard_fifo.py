import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.c_source_spelling import _c_spelling_ranges, _c_spelling_tokens, c_block


ROOT = Path(__file__).resolve().parents[1]


def driver_function(source, signature):
    definitions = []
    for start, end in _c_spelling_ranges(source, signature):
        following = _c_spelling_tokens(source[end:])
        if following and following[0][0] == "{":
            left, right = c_block(source[start:], signature)
            definitions.append(source[start + left:start + right])
    if len(definitions) != 1:
        raise ValueError(f"expected one driver definition: {signature}")
    return definitions[0] + "\n"


CASES = r'''
static keyboard_state_t keyboard_state;
static uint32_t system_ticks;
static int serial_calls, serial_value = -1;
static int serial_read_char(void) { serial_calls++; return serial_value; }
'''

CHECKS = r'''
static void reset(unsigned int start) {
    memset(&keyboard_state, 0xa5, sizeof(keyboard_state));
    keyboard_state.buffer.head = (uint8_t)start;
    keyboard_state.buffer.tail = (uint8_t)start;
    keyboard_state.buffer.count = 0;
    serial_calls = 0; serial_value = -1;
}
static int physical_equal(const keyboard_state_t *before) {
    return !memcmp(before->key_states, keyboard_state.key_states, sizeof(before->key_states)) &&
           !memcmp(before->modifier_states, keyboard_state.modifier_states, sizeof(before->modifier_states)) &&
           !memcmp(before->last_keypress_time, keyboard_state.last_keypress_time, sizeof(before->last_keypress_time));
}
int main(int argc, char **argv) {
    if (argc != 2) return 90;
    for (unsigned int start = 0; start < 256; start++) {
        reset(start);
        keyboard_state_t before = keyboard_state;
        if (argv[1][0] == 'f') {
            for (unsigned int i = 0; i < 4; i++) {
                system_ticks = 100 + i; enqueue_event((uint8_t)(10 + i), (char)('a' + i));
            }
            unsigned int head = keyboard_state.buffer.head;
            for (unsigned int i = 0; i < 4; i++) {
                if (i % 2 == 0) {
                    if ((uint8_t)keyboard_get_scancode() != 10 + i) return 1;
                } else {
                    key_event_t event;
                    if (!keyboard_read_event(&event) || event.scancode != 10 + i ||
                        event.character != (char)('a' + i) || !event.pressed || event.timestamp != 100 + i) return 2;
                }
                if (keyboard_state.buffer.head != head || keyboard_state.buffer.tail != (start + i + 1) % 256 ||
                    keyboard_state.buffer.count != 3 - i || serial_calls) return 3;
            }
        } else if (argv[1][0] == 'o') {
            for (unsigned int i = 0; i < 400; i++) {
                system_ticks = i; enqueue_event((uint8_t)(i % 127 + 1), (char)('a' + i % 26));
            }
            unsigned int head = keyboard_state.buffer.head;
            if (keyboard_state.buffer.count != 255 || head != (start + 400) % 256 ||
                keyboard_state.buffer.tail != (start + 145) % 256) return 4;
            for (unsigned int i = 145; i < 400; i++) {
                if (i % 2 == 0) {
                    if ((uint8_t)keyboard_get_scancode() != i % 127 + 1) return 5;
                } else {
                    key_event_t event;
                    if (!keyboard_read_event(&event) || event.scancode != i % 127 + 1 ||
                        event.character != (char)('a' + i % 26) || !event.pressed || event.timestamp != i) return 6;
                }
                if (keyboard_state.buffer.head != head || keyboard_state.buffer.count != 399 - i || serial_calls) return 7;
            }
            if (keyboard_state.buffer.tail != head) return 8;
        } else if (argv[1][0] == 'e') {
            key_event_t event, previous;
            memset(&event, 0x5a, sizeof(event)); previous = event;
            serial_value = '\r';
            if (keyboard_get_scancode() || serial_calls || keyboard_read_event(NULL) || serial_calls) return 9;
            serial_value = -1;
            if (keyboard_read_event(&event) || serial_calls != 1 || memcmp(&event, &previous, sizeof(event))) return 10;
            serial_value = '\r';
            if (!keyboard_read_event(&event) || serial_calls != 2 || event.scancode ||
                event.character != '\n' || !event.pressed || event.timestamp) return 11;
            if (keyboard_state.buffer.head != start || keyboard_state.buffer.tail != start || keyboard_state.buffer.count) return 12;
        } else if (argv[1][0] == 'n') {
            enqueue_event(42, 'x');
            if (keyboard_read_event(NULL) || keyboard_state.buffer.count != 1 ||
                keyboard_state.buffer.head != (start + 1) % 256 || keyboard_state.buffer.tail != start || serial_calls) return 13;
            if ((uint8_t)keyboard_get_scancode() != 42 || keyboard_state.buffer.count || serial_calls) return 14;
        } else return 91;
        if (!physical_equal(&before)) return 15;
    }
    return 0;
}
'''


def contract_source():
    driver = (ROOT / "drivers/keyboard.cc").read_text(encoding="utf-8")
    types = (ROOT / "kernel/core/types.h").read_text(encoding="utf-8")
    types = types[types.index("#define KEYBOARD_BUFFER_SIZE"):types.index("// Timer calibration")]
    functions = "".join(driver_function(driver, signature) for signature in (
        "static void enqueue_event(uint8_t scancode, char ascii)",
        "char keyboard_get_scancode(void)",
        "bool keyboard_read_event(key_event_t *event)",
    ))
    return ("#include <stdint.h>\n#include <string.h>\n"
            "typedef enum { false = 0, true = 1 } bool;\n"
            + types + CASES + functions + CHECKS)


class KeyboardFifoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("native compiler unavailable")
        cls.directory = tempfile.TemporaryDirectory(prefix=".keyboard-fifo-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.directory.cleanup)
        source = Path(cls.directory.name) / "fifo.cc"
        source.write_text(contract_source(), encoding="utf-8")
        cls.program = source.with_suffix(".exe" if os.name == "nt" else ".out")
        result = subprocess.run([compiler, "-x", "c", "-std=c11", "-O2", "-Wall", "-Werror",
                                 str(source), "-o", str(cls.program)], capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run([str(self.program), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, f"FIFO case {name}: contract failure {result.returncode}\n{result.stderr}")
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")

    def test_mixed_readers_consume_fifo_at_every_wrap_position(self):
        self.run_case("fifo")

    def test_overflow_keeps_the_newest_255_events(self):
        self.run_case("overflow")

    def test_empty_scancode_read_preserves_serial_fallback(self):
        self.run_case("empty")

    def test_null_event_destination_preserves_queued_input(self):
        self.run_case("null")


class CheckedKeyboardFifoTests(KeyboardFifoTests):
    @classmethod
    def setUpClass(cls):
        if os.name == "nt" and shutil.which("wsl") is None:
            raise unittest.SkipTest("WSL is required to execute the checked i386 fixture")
        from tools.bootstrap_toolchain import ToolRunner, freeze_seed_inputs, run_seed_tool

        cls.directory = tempfile.TemporaryDirectory(prefix=".checked-keyboard-fifo-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.directory.cleanup)
        output = Path(cls.directory.name)
        source = output / "fifo.cc"
        source.write_text(contract_source(), encoding="utf-8")
        manifest = ROOT / "bootstrap/seeds" / ("i386-windows" if os.name == "nt" else "i386-linux") / "manifest.json"
        frozen = freeze_seed_inputs(manifest, output / "seed")
        cls.runner = ToolRunner(ROOT)

        def run(tool, arguments):
            result = run_seed_tool(manifest, ROOT, tool, arguments, timeout=60,
                                   frozen_seed=frozen, runner=cls.runner)
            if result.returncode or result.stdout or result.stderr:
                raise AssertionError(f"{tool}: {result.returncode}\n{result.stdout}{result.stderr}")

        logical = "/" + output.relative_to(ROOT).as_posix()
        includes = ["-I", "/toolchain", "--include-angle", "/toolchain/hosted/i386-linux/include"]
        run("cupidc", ["--root", ROOT, "--gnu", *includes,
                       "-c", logical + "/fifo.cc", "-o", logical + "/fifo.o"])
        run("cupidc", ["--root", ROOT, "--gnu", "--freestanding", *includes,
                       "-c", "/toolchain/hosted/i386-linux/runtime.cc", "-o", logical + "/runtime.o"])
        run("cupidasm", ["-f", "elf32", ROOT / "toolchain/hosted/i386-linux/start.asm", "-o", output / "start.o"])
        cls.program = output / "fifo.elf"
        run("cupidld", ["-m", "elf_i386", "--text-address", "0x08048000", "--entry", "_start",
                        "-o", cls.program, output / "start.o", output / "fifo.o", output / "runtime.o"])

    def run_case(self, name):
        result = self.runner.run(self.program, [name], 10)
        self.assertEqual(result.returncode, 0, f"checked FIFO case {name}: {result.returncode}\n{result.stderr}")
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()

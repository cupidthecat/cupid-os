import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* CONSTANTS */
static int s_music_inited = 1;
static volatile uint32_t s_mus_w;
static volatile uint32_t s_mus_r;
static int16_t s_mus_ring[MUS_RING_FRAMES * 2u];
static unsigned int calls;
static int draining;

static void require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(91);
    }
}

static int16_t sample_value(uint32_t frame) {
    return (int16_t)(frame & 0x7fffu);
}

static void check_frames(uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t frame = first + i;
        uint32_t index = (frame % MUS_RING_FRAMES) * 2u;
        int16_t value = sample_value(frame);
        require(s_mus_ring[index] == value, "left sample order changed");
        require(s_mus_ring[index + 1u] == (int16_t)-value,
                "right sample order changed");
    }
}

static void render_one_buffer(int16_t *destination) {
    if (++calls > 128u) {
        fprintf(stderr, "pump did not return while the consumer drained: "
                "w=%u r=%u\n", s_mus_w, s_mus_r);
        exit(90);
    }
    uint32_t write = s_mus_w;
    require(destination == &s_mus_ring[(write % MUS_RING_FRAMES) * 2u],
            "producer selected the wrong ring slot");
    require(write - s_mus_r + MUS_BUF_FRAMES <= MUS_RING_FRAMES,
            "producer overwrote unread audio");

    /* Model the observed IRQ consumer running while synthesis is busy. */
    if (draining && write - s_mus_r >= MUS_BUF_FRAMES) {
        check_frames(s_mus_r, MUS_BUF_FRAMES);
        s_mus_r += MUS_BUF_FRAMES;
    }
    for (uint32_t i = 0; i < MUS_BUF_FRAMES; ++i) {
        int16_t value = sample_value(write + i);
        destination[i * 2u] = value;
        destination[i * 2u + 1u] = (int16_t)-value;
    }
}

/* PRODUCER */

int main(int argc, char **argv) {
    require(argc == 2, "one case number is required");
    require(argv[1][0] >= '0' && argv[1][0] <= '9' && argv[1][1] == '\0',
            "case must be one digit from zero through nine");
    unsigned int mode = (unsigned int)(argv[1][0] - '0');
    uint32_t expected_calls = 0;
    switch (mode) {
        case 0: s_music_inited = 0; break;
        case 1: s_mus_w = MUS_RING_FRAMES; break;
        case 2: expected_calls = MUS_RING_BUFS; break;
        case 3:
            s_mus_w = MUS_BUF_FRAMES * 3u;
            expected_calls = MUS_RING_BUFS - 3u;
            break;
        case 4:
            s_mus_r = UINT32_MAX - (MUS_BUF_FRAMES * 2u - 1u);
            s_mus_w = s_mus_r + MUS_RING_FRAMES - MUS_BUF_FRAMES * 2u;
            expected_calls = 2u;
            break;
        case 5: draining = 1; expected_calls = MUS_RING_BUFS; break;
        case 6:
            draining = 1;
            s_mus_w = MUS_BUF_FRAMES * 2u;
            expected_calls = MUS_RING_BUFS - 2u;
            break;
        case 7: s_mus_w = MUS_RING_FRAMES - MUS_BUF_FRAMES + 1u; break;
        case 8: s_mus_w = MUS_RING_FRAMES + 1u; break;
        case 9:
            s_mus_r = UINT32_MAX - (MUS_BUF_FRAMES * 3u - 1u);
            s_mus_w = s_mus_r + MUS_BUF_FRAMES * 2u;
            expected_calls = MUS_RING_BUFS - 2u;
            break;
        default: require(0, "unknown case");
    }
    uint32_t initial_write = s_mus_w;
    uint32_t initial_read = s_mus_r;
    uint32_t initial_used = initial_write - initial_read;
    if (initial_used <= MUS_RING_FRAMES) {
        for (uint32_t i = 0; i < initial_used; ++i) {
            uint32_t frame = initial_read + i;
            uint32_t index = (frame % MUS_RING_FRAMES) * 2u;
            int16_t value = sample_value(frame);
            s_mus_ring[index] = value;
            s_mus_ring[index + 1u] = (int16_t)-value;
        }
    }
    cup_music_pump();
    require(calls == expected_calls, "entry-space work budget changed");
    require(s_mus_w == initial_write + expected_calls * MUS_BUF_FRAMES,
            "producer publication count changed");
    if (initial_used <= MUS_RING_FRAMES) {
        uint32_t remaining = s_mus_w - s_mus_r;
        require(remaining <= MUS_RING_FRAMES, "ring capacity exceeded");
        check_frames(s_mus_r, remaining);
        require((s_mus_r - initial_read) + remaining ==
                    initial_used + expected_calls * MUS_BUF_FRAMES,
                "audio frames were lost or duplicated");
    }
    puts("PASS: producer returned with ordered audio");
    return 0;
}
"""


class DoomMusicPumpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "kernel/doom/i_sound_cupidos.cc").read_text(
            encoding="utf-8"
        )
        start = source.index("void cup_music_pump(void)")
        end = source.index("\n}", start) + 2
        producer = source[start:end]
        constants = "\n".join(
            re.findall(r"^#define MUS_(?:BUF_FRAMES|RING_BUFS|RING_FRAMES)\s+.*$",
                       source, re.MULTILINE)
        )
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupid-music-pump-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        fixture = directory / "music_pump_contract.cc"
        fixture.write_text(
            HARNESS.replace("/* CONSTANTS */", constants).replace(
                "/* PRODUCER */", producer
            ), encoding="utf-8"
        )
        cls.executable = directory / ("pump.exe" if os.name == "nt" else "pump")
        compiler = shlex.split(os.environ.get("CC", "gcc" if os.name == "nt" else "cc"))
        built = subprocess.run(
            compiler + ["-x", "c", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        str(fixture), "-o", str(cls.executable)],
            capture_output=True, text=True, timeout=30,
        )
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, case):
        result = subprocess.run([str(self.executable), str(case)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_uninitialized_music_returns(self):
        self.run_case(0)

    def test_full_ring_returns(self):
        self.run_case(1)

    def test_empty_ring_prefills(self):
        self.run_case(2)

    def test_partial_ring_fills_remaining_space(self):
        self.run_case(3)

    def test_monotonic_counters_wrap(self):
        self.run_case(4)

    def test_draining_consumer_cannot_hold_caller(self):
        self.run_case(5)

    def test_draining_consumer_does_not_expand_entry_budget(self):
        self.run_case(6)

    def test_fractional_buffer_space_is_not_overwritten(self):
        self.run_case(7)

    def test_producer_publication_wraps_uint32(self):
        self.run_case(9)

    def test_invalid_occupancy_does_not_write(self):
        self.run_case(8)


if __name__ == "__main__":
    unittest.main()

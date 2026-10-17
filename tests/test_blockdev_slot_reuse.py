import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

CONTRACT_SOURCE = r"""
#include "blockdev.h"

static block_device_t *unregister_during_log;
static int registration_print_calls;
static int release_print_call;
static int released_devices;

void print(const char *text) {
    (void)text;
    if (unregister_during_log) {
        registration_print_calls++;
        if (registration_print_calls == 1) {
            if (blkdev_unregister(unregister_during_log) != 0) {
                release_print_call = -1;
            }
        }
    }
}

void print_int(uint32_t value) {
    (void)value;
}

static int contract_read(void *driver_data, uint32_t lba, uint32_t count,
                         void *buffer) {
    (void)lba;
    (void)count;
    (void)buffer;
    return *(int *)driver_data;
}

static int contract_write(void *driver_data, uint32_t lba, uint32_t count,
                          const void *buffer) {
    (void)lba;
    (void)count;
    (void)buffer;
    return *(int *)driver_data;
}

static void contract_release(void *driver_data) {
    if (driver_data) {
        released_devices++;
        if (unregister_during_log) {
            release_print_call = registration_print_calls;
        }
    }
}

static void prepare_device(block_device_t *dev, int *identity) {
    dev->name = "contract";
    dev->sector_count = 32u;
    dev->sector_size = 512u;
    dev->driver_data = identity;
    dev->read = contract_read;
    dev->write = contract_write;
    dev->release = contract_release;
    dev->registry_ref_count = 0u;
    dev->registry_registered = false;
}

int main(void) {
    block_device_t dev[12];
    int identity[12];
    uint8_t byte = 0u;

    for (int i = 0; i < 12; i++) {
        identity[i] = i + 10;
        prepare_device(&dev[i], &identity[i]);
    }

    blkdev_init();
    if (blkdev_count() != 0) return 1;
    if (blkdev_index_limit() != 0) return 30;
    if (blkdev_register(NULL) != -1) return 2;

    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (blkdev_register(&dev[i]) != 0) return 3;
        block_device_t *current = blkdev_get(i);
        if (current != &dev[i]) return 4;
        if (blkdev_put(current) != 0) return 31;
    }
    if (blkdev_count() != MAX_BLOCK_DEVICES) return 5;
    if (blkdev_index_limit() != MAX_BLOCK_DEVICES) return 32;
    if (blkdev_register(&dev[4]) != -1) return 6;
    if (blkdev_register(&dev[0]) != -1) return 7;
    dev[0].registry_ref_count = 0xFFFFFFFFu;
    if (blkdev_get(0) != NULL) return 55;
    if (dev[0].registry_ref_count != 0xFFFFFFFFu) return 56;
    dev[0].registry_ref_count = 1u;

    block_device_t *retired = blkdev_get(1);
    if (retired != &dev[1]) return 8;
    if (blkdev_unregister(retired) != 0) return 9;
    if (blkdev_get(1) != NULL) return 10;
    if (blkdev_count() != MAX_BLOCK_DEVICES - 1) return 11;
    if (blkdev_index_limit() != MAX_BLOCK_DEVICES) return 33;
    if (released_devices != 0) return 34;
    if (blkdev_register(retired) != -1) return 35;

    /*
     * Keep slots 0, 2, and 3 active while slot 1 passes through eight more
     * device lifetimes. A pointer from the first lifetime still names its
     * original object after every reuse.
     */
    for (int i = 4; i < 12; i++) {
        if (blkdev_register(&dev[i]) != 0) return 12;
        block_device_t *slot = blkdev_get(1);
        if (slot != &dev[i]) return 13;
        if (blkdev_put(slot) != 0) return 36;
        slot = blkdev_get(0);
        if (slot != &dev[0]) return 14;
        if (blkdev_put(slot) != 0) return 37;
        slot = blkdev_get(2);
        if (slot != &dev[2]) return 15;
        if (blkdev_put(slot) != 0) return 38;
        slot = blkdev_get(3);
        if (slot != &dev[3]) return 16;
        if (blkdev_put(slot) != 0) return 39;
        if (blkdev_read(retired, 0u, 1u, &byte) != identity[1]) return 17;
        if (blkdev_unregister(&dev[i]) != 0) return 18;
        if (blkdev_get(1) != NULL) return 19;
        if (blkdev_count() != MAX_BLOCK_DEVICES - 1) return 40;
        if (released_devices != i - 3) return 41;
    }

    if (blkdev_put(retired) != 0) return 42;
    if (released_devices != 9) return 43;
    if (blkdev_put(retired) != -1) return 44;
    if (blkdev_unregister(&dev[11]) != -1) return 20;
    if (blkdev_unregister(NULL) != -1) return 21;
    if (blkdev_unregister(&dev[3]) != 0) return 22;
    if (blkdev_count() != 2) return 23;
    if (blkdev_index_limit() != 3) return 45;
    if (blkdev_unregister(&dev[2]) != 0) return 24;
    if (blkdev_count() != 1) return 25;
    if (blkdev_index_limit() != 1) return 46;
    block_device_t *first = blkdev_get(0);
    if (first != &dev[0]) return 26;
    if (blkdev_put(first) != 0) return 47;
    if (blkdev_unregister(&dev[0]) != 0) return 27;
    if (blkdev_count() != 0) return 28;
    if (blkdev_index_limit() != 0) return 48;
    if (blkdev_get(MAX_BLOCK_DEVICES) != NULL) return 29;
    if (released_devices != 12) return 49;

    unregister_during_log = &dev[0];
    registration_print_calls = 0;
    release_print_call = 0;
    if (blkdev_register(&dev[0]) != 0) return 50;
    unregister_during_log = NULL;
    if (registration_print_calls != 5) return 51;
    if (release_print_call != 5) return 52;
    if (released_devices != 13) return 53;
    if (blkdev_count() != 0 || blkdev_index_limit() != 0) return 54;
    return 0;
}
"""


class BlockDeviceSlotReuseTests(unittest.TestCase):
    def test_actual_registry_reuses_slots_without_rebinding_cached_pointers(self):
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("a C compiler is required for the registry contract")

        with tempfile.TemporaryDirectory(prefix="blockdev-contract-") as tmp:
            tmp_path = Path(tmp)
            source = tmp_path / "contract.c"
            executable = tmp_path / (
                "contract.exe" if Path(compiler).name.lower().endswith(".exe")
                else "contract"
            )
            source.write_text(CONTRACT_SOURCE, encoding="utf-8")

            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-ffreestanding",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "kernel" / "fs"),
                    "-I",
                    str(ROOT / "kernel" / "core"),
                    "-I",
                    str(ROOT / "kernel" / "cpu"),
                    "-x",
                    "c",
                    str(ROOT / "kernel" / "fs" / "blockdev.cc"),
                    str(source),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )

            run_result = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )


if __name__ == "__main__":
    unittest.main()

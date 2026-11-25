"""Exercise the active HomeFS save code with a small allocator and FAT source sink."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from tests.c_source_spelling import c_block


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = (
    ("int", "homefs_reserved_component"),
    ("int", "homefs_valid_name_bytes"),
    ("int", "homefs_checked_add_u32"),
    ("int", "homefs_count_nodes"),
    ("int", "homefs_collect_nodes"),
    ("int", "homefs_prepare_snapshot"),
    ("bool", "homefs_next_snapshot_part"),
    ("int", "homefs_read_snapshot"),
    ("int", "homefs_flush"),
)
PREAMBLE = r"""
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VFS_MAX_NAME 128u
#define VFS_TYPE_FILE 0u
#define VFS_TYPE_DIR 1u
#define VFS_OK 0
#define VFS_EINVAL -22
#define VFS_ENOSPC -28
#define VFS_EIO -5
#define ALLOCATION_LIMIT 256u

static unsigned lock_depth, attempts;
static bool inject_write_failure;
static uint32_t pending_size, saved_size;
static uint8_t committed[32768], pending[32768];

static bool blockcache_guard_enter(void) { lock_depth++; return true; }
static void blockcache_guard_leave(bool locked) {
    assert(locked && lock_depth > 0u);
    lock_depth--;
}
static void *kmalloc(uint32_t size) {
    assert(lock_depth == 1u);
    return size <= ALLOCATION_LIMIT ? malloc(size) : NULL;
}
#define kfree free
#define serial_printf(...)

typedef int (*fat16_read_source_t)(void *context, void *data, uint32_t count);
static int fat16_write_reserved_file(const char *name, uint32_t size,
                                    fat16_read_source_t read_source, void *context);
"""
FIXTURE = r"""
static homefs_t *active_fs;

static int fat16_write_reserved_file(const char *name, uint32_t size,
                                    fat16_read_source_t read_source, void *context) {
    const uint32_t chunks[] = {1u, 14u, 503u, 512u, 75u};
    assert(strcmp(name, "HOMEFS.SYS") == 0);
    assert(size <= sizeof(pending) && size > ALLOCATION_LIMIT);
    assert(lock_depth == 1u && !active_fs->dirty);
    attempts++;
    bool locked = blockcache_guard_enter();
    assert(homefs_flush(active_fs) == VFS_OK);
    blockcache_guard_leave(locked);
    assert(lock_depth == 1u);

    pending_size = 0u;
    for (size_t i = 0u; pending_size < size; i++) {
        uint32_t count = chunks[i % (sizeof(chunks) / sizeof(chunks[0]))];
        if (count > size - pending_size)
            count = size - pending_size;
        assert(read_source(context, pending + pending_size, count) == (int)count);
        pending_size += count;
        if (inject_write_failure && pending_size >= 1024u)
            return -1;
    }
    uint8_t extra;
    assert(read_source(context, &extra, 1u) < 0);
    memcpy(committed, pending, size);
    saved_size = size;
    return (int)size;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    static uint8_t asset[8192], product[12288];
    memset(asset, 'A', sizeof(asset));
    memset(product, 'B', sizeof(product));
    bool sparse = strcmp(argv[1], "sparse") == 0;
    homefs_node_t root = {.type = VFS_TYPE_DIR};
    homefs_node_t dir = {.name = "project", .type = VFS_TYPE_DIR, .parent = &root};
    homefs_node_t empty = {.name = "empty", .parent = &dir};
    homefs_node_t input = {.name = "input", .parent = &dir, .data = asset,
                          .size = sizeof(asset), .stored_size = sizeof(asset)};
    homefs_node_t output = {.name = "product", .parent = &root, .data = product,
                            .size = sparse ? 7u * sizeof(product) : sizeof(product),
                            .stored_size = sizeof(product)};
    root.children = &dir;
    dir.children = &empty;
    empty.next = &input;
    dir.next = &output;
    homefs_t fs = {.root = &root, .dirty = true};
    active_fs = &fs;

    if (strcmp(argv[1], "invalid") == 0) {
        input.stored_size++;
        bool locked = blockcache_guard_enter();
        assert(homefs_flush(&fs) == VFS_ENOSPC);
        assert(lock_depth == 1u && attempts == 0u && fs.dirty);
        blockcache_guard_leave(locked);
        return 0;
    }

    if (strcmp(argv[1], "retry") == 0) {
        inject_write_failure = true;
        memcpy(committed, "old", 3u);
        saved_size = 3u;
        bool locked = blockcache_guard_enter();
        assert(homefs_flush(&fs) == VFS_EIO);
        assert(lock_depth == 1u && attempts == 1u && fs.dirty);
        assert(saved_size == 3u && memcmp(committed, "old", 3u) == 0);
        blockcache_guard_leave(locked);
        inject_write_failure = false;
    }

    bool locked = blockcache_guard_enter();
    assert(homefs_flush(&fs) == VFS_OK);
    assert(lock_depth == 1u && !fs.dirty);
    blockcache_guard_leave(locked);
    FILE *file = fopen(argv[2], "wb");
    assert(file != NULL);
    assert(fwrite(committed, 1u, saved_size, file) == saved_size);
    return fclose(file);
}
"""


def native_fixture(directory: Path) -> Path:
    source = (ROOT / "kernel/fs/homefs.cc").read_text(encoding="utf-8")
    first = source.index("#define HOMEFS_MAGIC")
    last = source.index("typedef struct {\n    homefs_t *fs;\n    int status;")
    parts = [PREAMBLE, source[first:last]]
    for result, name in FUNCTIONS:
        start = source.index(f"static {result} {name}(")
        signature = source[start:source.index("{", start)].rstrip()
        start, end = c_block(source, signature)
        parts.append(source[start:end])
    parts.append(FIXTURE)
    fixture = directory / "contract.c"
    fixture.write_text("\n".join(parts), encoding="utf-8")

    compiler = shutil.which("clang" if os.name == "nt" else "cc")
    if compiler is None:
        raise AssertionError("Native HomeFS contract compiler is unavailable")
    runner = directory / ("contract.exe" if os.name == "nt" else "contract")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    fixture, "-o", runner], check=True, timeout=30)
    return runner


def expected_snapshot(sparse: bool) -> bytes:
    nodes = (
        (0xFFFFFFFF, 1, b"", b"", 0),
        (0, 1, b"project", b"", 0),
        (1, 0, b"empty", b"", 0),
        (1, 0, b"input", b"A" * 8192, 8192),
        (0, 0, b"product", b"B" * 12288, 86016 if sparse else 12288),
    )
    parts = [struct.pack("<III", 0x31465348, 2 if sparse else 1, len(nodes))]
    for parent, kind, name, contents, size in nodes:
        parts.append(struct.pack("<IIII", parent, kind, size, len(name)))
        if sparse:
            parts.append(struct.pack("<I", len(contents)))
        parts.extend((name, contents))
    return b"".join(parts)


class HomefsPublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = tempfile.TemporaryDirectory(prefix="cupid-homefs-publication-")
        cls.addClassCleanup(directory.cleanup)
        cls.scratch = Path(directory.name)
        cls.runner = native_fixture(cls.scratch)

    def check_snapshot(self, mode: str, sparse: bool = False) -> None:
        output = self.scratch / (mode + ".sys")
        subprocess.run([self.runner, mode, output], check=True, timeout=15)
        self.assertEqual(output.read_bytes(), expected_snapshot(sparse))

    def test_streams_dense_records_with_small_allocations(self):
        self.check_snapshot("dense")

    def test_streams_sparse_records_and_retains_the_logical_size(self):
        self.check_snapshot("sparse", sparse=True)

    def test_keeps_the_previous_snapshot_if_the_writer_fails(self):
        self.check_snapshot("retry")

    def test_rejects_invalid_lengths_before_the_writer(self):
        subprocess.run([self.runner, "invalid", self.scratch / "invalid.sys"], check=True, timeout=15)
        self.assertFalse((self.scratch / "invalid.sys").exists())


if __name__ == "__main__":
    unittest.main()

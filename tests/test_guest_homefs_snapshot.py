"""Check readback of HomeFS snapshots with and without a sparse tail."""

from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch


GUEST_TOOLS = Path(__file__).resolve().parents[1] / "bootstrap" / "guest"
with patch.object(sys, "path", [str(GUEST_TOOLS), *sys.path]):
    from check_guest_runtime import HOMEFS_MAGIC, compare_homefs_files, write_homefs


def sparse_snapshot(size: int, stored_size: int) -> bytes:
    name = b"disk.img"
    return b"".join((
        struct.pack("<III", HOMEFS_MAGIC, 2, 2),
        struct.pack("<IIIII", 0xFFFFFFFF, 1, 0, 0, 0),
        struct.pack("<IIIII", 0, 0, size, len(name), stored_size),
        name,
        b"ABC",
    ))


class GuestHomefsSnapshotTests(unittest.TestCase):
    def test_dense_snapshot_round_trips(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / "homefs.sys"
            image = root / "disk.img"
            image.write_bytes(b"ABC\0")
            write_homefs(snapshot, ((b"disk.img", image.read_bytes()),))
            self.assertEqual(compare_homefs_files(snapshot.read_bytes(), {"disk.img": image}),
                             {"disk.img": b"ABC\0"})

    def test_sparse_snapshot_restores_implicit_zeroes(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "disk.img"
            expected = b"ABC" + bytes(13)
            image.write_bytes(expected)
            self.assertEqual(compare_homefs_files(sparse_snapshot(16, 3), {"disk.img": image}),
                             {"disk.img": expected})
            image.write_bytes(expected[:-1] + b"X")
            with self.assertRaisesRegex(ValueError, "differs from the checked file"):
                compare_homefs_files(sparse_snapshot(16, 3), {"disk.img": image})

    def test_rejects_stored_bytes_beyond_the_logical_size(self):
        with self.assertRaisesRegex(ValueError, "invalid HOMEFS.SYS file size"):
            compare_homefs_files(sparse_snapshot(2, 3), {})

    def test_rejects_a_truncated_backed_prefix(self):
        with self.assertRaisesRegex(ValueError, "truncated HOMEFS.SYS entry"):
            compare_homefs_files(sparse_snapshot(16, 3)[:-1], {})


if __name__ == "__main__":
    unittest.main()

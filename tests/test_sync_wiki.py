import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from tools import sync_wiki


class WikiSyncTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.destination = self.root / "published"
        self.source.mkdir()
        self.destination.mkdir()

    def test_updates_pages_assets_and_sidebar_and_keeps_unmanaged_pages(self):
        for name in ("Home.md", "_Sidebar.md", "images/screen.png"):
            path = self.source / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(b"new\x00content")
        (self.destination / "Home.md").write_text("old")
        (self.destination / "Web-only.md").write_text("keep")
        (self.destination / "Old.md").write_text("remove")
        current = ["Home.md", "_Sidebar.md", "images/screen.png"]
        self.assertEqual(sync_wiki.sync(self.source, self.destination, current, ["Old.md"]),
                         (3, 1))
        self.assertEqual((self.destination / "Web-only.md").read_text(), "keep")
        for name in current:
            self.assertEqual((self.destination / name).read_bytes(), b"new\x00content")
        self.assertFalse((self.destination / "Old.md").exists())
        timestamp = (self.destination / "Home.md").stat().st_mtime_ns
        self.assertEqual(sync_wiki.sync(self.source, self.destination, current, ["Old.md"]),
                         (0, 0))
        self.assertEqual((self.destination / "Home.md").stat().st_mtime_ns, timestamp)

    def test_invalid_paths_fail_before_any_write_or_deletion(self):
        (self.source / "Home.md").write_text("new")
        (self.destination / "Home.md").write_text("old")
        for invalid in ("../outside", "/absolute", ".git/config", "folder/.GIT/x",
                        "folder\\page", "a//b", "./Home.md", "C:/outside", ""):
            with self.subTest(path=invalid), self.assertRaises(ValueError):
                sync_wiki.sync(self.source, self.destination, ["Home.md"], [invalid])
            self.assertEqual((self.destination / "Home.md").read_text(), "old")

    def test_missing_source_and_directory_destinations_fail_before_writes(self):
        (self.source / "Home.md").write_text("new")
        (self.destination / "Home.md").write_text("old")
        with self.assertRaises(FileNotFoundError):
            sync_wiki.sync(self.source, self.destination, ["Home.md", "Missing.md"], [])
        (self.destination / "Folder.md").mkdir()
        with self.assertRaises(ValueError):
            sync_wiki.sync(self.source, self.destination, ["Home.md"], ["Folder.md"])
        self.assertEqual((self.destination / "Home.md").read_text(), "old")

    def test_rejects_conflicting_and_overlapping_paths(self):
        for current, deleted in ((["Home.md"], ["Home.md"]),
                                 (["Home.md", "home.md"], [])):
            with self.assertRaises(ValueError):
                sync_wiki.sync(self.source, self.destination, current, deleted)
        with self.assertRaises(ValueError):
            sync_wiki.sync(self.source, self.source / "nested", [], [])

    def test_source_and_destination_symlinks_are_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "Home.md").write_text("keep")
        for root in (self.source, self.destination):
            link = root / "alias"
            try:
                link.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlinks unavailable: {error}")
            if root == self.source:
                current, deleted = ["alias/Home.md"], []
            else:
                current, deleted = [], ["alias/Home.md"]
            with self.assertRaises(ValueError):
                sync_wiki.sync(self.source, self.destination, current, deleted)
            self.assertEqual((outside / "Home.md").read_text(), "keep")

    def test_git_inventory_uses_nul_paths_and_explicit_deletions(self):
        before = "a" * 40
        with mock.patch.object(sync_wiki, "_git", side_effect=[
                b"wiki/Home.md\0wiki/Space Page.md\0", b"", b"wiki/Removed.md\0"]) as git:
            self.assertEqual(sync_wiki.tracked_changes(self.root, before),
                             (["Home.md", "Space Page.md"], ["Removed.md"]))
        self.assertEqual(git.call_args_list[-1].args,
                         (self.root, "diff", "--no-renames", "--diff-filter=D",
                          "--name-only", "-z", before, "HEAD", "--", "wiki/"))
        for before in ("", "0" * 40):
            with mock.patch.object(sync_wiki, "_git", return_value=b"wiki/Home.md\0") as git:
                self.assertEqual(sync_wiki.tracked_changes(self.root, before), (["Home.md"], []))
                self.assertEqual(git.call_count, 1)

    def test_invalid_revision_and_unavailable_parent_fail_closed(self):
        with mock.patch.object(sync_wiki, "_git", return_value=b""):
            with self.assertRaises(ValueError):
                sync_wiki.tracked_changes(self.root, "--bad-revision")
        with mock.patch.object(sync_wiki, "_git", side_effect=[
                b"", subprocess.CalledProcessError(1, "git")]):
            with self.assertRaises(subprocess.CalledProcessError):
                sync_wiki.tracked_changes(self.root, "b" * 40)


if __name__ == "__main__":
    unittest.main()

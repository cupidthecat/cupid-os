import os
import json
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
                        "folder\\page", "a//b", "./Home.md", "C:/outside", "", "line\nbreak",
                        sync_wiki.STATE_FILE):
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
            for value in ('--bad-revision', '0', '0000'):
                with self.subTest(revision=value), self.assertRaises(ValueError):
                    sync_wiki.tracked_changes(self.root, value)
        with mock.patch.object(sync_wiki, "_git", side_effect=[
                b"", subprocess.CalledProcessError(1, "git")]):
            with self.assertRaises(subprocess.CalledProcessError):
                sync_wiki.tracked_changes(self.root, "b" * 40)

    def pages(self, home):
        for name in sync_wiki.REQUIRED_PAGES:
            (self.source / name).write_text(home if name == 'Home.md' else '# Page\n',
                                            encoding='utf-8')
        return sorted(sync_wiki.REQUIRED_PAGES)

    def test_validates_pages_assets_references_and_preserved_remote_pages(self):
        current = self.pages('[Guide](Getting-Started#build) ![Image](images/a.png)\n'
                             '[Website page][web]\n[web]: Web-only\n[[Home|Home]]\n')
        (self.source / 'images').mkdir()
        (self.source / 'images/a.png').write_bytes(b'\x00image')
        (self.destination / 'Web-only.md').write_text('# Website page')
        sync_wiki.validate(self.source, self.destination, current + ['images/a.png'], [])
        with self.assertRaisesRegex(ValueError, 'missing wiki link target'):
            sync_wiki.validate(self.source, self.destination, current + ['images/a.png'],
                               ['Web-only.md'])

    def test_rejects_missing_required_pages_bad_links_and_unsafe_schemes(self):
        current = self.pages('# Home\n')
        with self.assertRaisesRegex(ValueError, 'required wiki pages missing'):
            sync_wiki.validate(self.source, self.destination, ['Home.md'], [])
        for target in ('Missing', '../docs/file.md', '%2e%2e/escape', 'javascript:bad'):
            with self.subTest(target=target):
                (self.source / 'Home.md').write_text(f'[Bad]({target})', encoding='utf-8')
                with self.assertRaises(ValueError):
                    sync_wiki.validate(self.source, self.destination, current, [])
        (self.source / 'Home.md').write_text('[Broken][undefined]', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'undefined wiki link reference'):
            sync_wiki.validate(self.source, self.destination, current, [])

    def test_validation_ignores_code_examples_and_accepts_external_links(self):
        current = self.pages('```md\n[example](Missing)\n```\n'
                             '~~~\n[example](Missing)\n~~~\n'
                             '    [example](Missing)\n`[example](Missing)`\n'
                             '[GitHub](https://github.com/cupidthecat/cupid-os)\n')
        sync_wiki.validate(self.source, self.destination, current, [])

    def test_failed_validation_keeps_the_published_wiki_unchanged(self):
        current = self.pages('[Broken](Missing)')
        repository = self.root / 'repo'
        repository.mkdir()
        pages = repository / 'wiki'
        pages.mkdir()
        for path in self.source.iterdir():
            (pages / path.name).write_bytes(path.read_bytes())
        (self.destination / 'Home.md').write_text('keep')
        with mock.patch.object(sync_wiki, 'tracked_changes', return_value=(current, [])):
            with self.assertRaises(SystemExit) as error:
                sync_wiki.main(['--source', str(repository), '--destination', str(self.destination)])
        self.assertEqual(error.exception.code, 1)
        self.assertEqual((self.destination / 'Home.md').read_text(), 'keep')

    def test_previous_publication_carries_deletions_across_failed_or_skipped_pushes(self):
        state = {'schema': 1, 'source_revision': 'a' * 40,
                 'files': ['Home.md', 'Old-name.md']}
        (self.destination / sync_wiki.STATE_FILE).write_text(json.dumps(state))
        self.assertEqual(sync_wiki.pending_deletions(self.destination,
                         ['Home.md', 'New-name.md'], []), ['Old-name.md'])
        self.assertEqual(sync_wiki.pending_deletions(self.destination,
                         ['Home.md', 'Old-name.md'], ['Other.md']), ['Other.md'])
        state['files'] = ['../outside']
        (self.destination / sync_wiki.STATE_FILE).write_text(json.dumps(state))
        with self.assertRaises(ValueError):
            sync_wiki.pending_deletions(self.destination, ['Home.md'], [])

    def test_check_mode_is_read_only_and_success_records_managed_files(self):
        current = self.pages('# Home\n')
        repository = self.root / 'repo'
        wiki = repository / 'wiki'
        wiki.mkdir(parents=True)
        for path in self.source.iterdir():
            (wiki / path.name).write_bytes(path.read_bytes())
        args = ['--source', str(repository), '--destination', str(self.destination)]
        with mock.patch.object(sync_wiki, 'tracked_changes', return_value=(current, [])), \
                mock.patch.object(sync_wiki, '_git', return_value=('b' * 40).encode()):
            self.assertEqual(sync_wiki.main(args + ['--check']), 0)
            self.assertEqual(list(self.destination.iterdir()), [])
            self.assertEqual(sync_wiki.main(args), 0)
        state = json.loads((self.destination / sync_wiki.STATE_FILE).read_text())
        self.assertEqual(state, {'schema': 1, 'source_revision': 'b' * 40, 'files': current})


if __name__ == "__main__":
    unittest.main()

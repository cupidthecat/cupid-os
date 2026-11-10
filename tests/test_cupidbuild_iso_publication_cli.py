"""The direct CLI retains the complete publication API and required authority."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from . import test_cupidbuild_iso_publication as publication


class CupidBuildIsoPublicationCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        publication.CupidBuildIsoPublicationTests.setUpClass.__func__(cls)
        selected = os.environ.get("CUPIDBUILD_ISO_PUBLICATION_CLI_PROGRAM")
        if selected:
            cls.cli = Path(selected).resolve(strict=True)
        else:
            temporary = tempfile.TemporaryDirectory(prefix="iso-publication-cli-")
            cls.addClassCleanup(temporary.cleanup)
            directory = Path(temporary.name)
            cls.cli = directory / ("cupidbuild.exe" if os.name == "nt" else "cupidbuild")
            command = ["make", "-C", str(publication.ROOT / "toolchain"),
                       "BUILD_DIR=" + directory.as_posix(), cls.cli.as_posix()]
            result = subprocess.run(command, capture_output=True, timeout=180)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)

    setUp = publication.CupidBuildIsoPublicationTests.setUp
    arrange = publication.CupidBuildIsoPublicationTests.arrange

    def arguments(self):
        return [str(self.cli), "publish-iso-fixture", "--root", str(self.root),
                "--manifest", self.paths[0], "--fixtures", self.paths[1],
                "--output", self.paths[2], "--linux-manifest", self.paths[3],
                "--windows-manifest", self.paths[4], "--seed-release", self.paths[5]]

    def execute(self, arguments=None, expected=0):
        result = subprocess.run(arguments or self.arguments(), cwd=self.root,
                                capture_output=True, timeout=600)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(result.stdout, b"")
        if expected:
            self.assertTrue(result.stderr)
            self.assertEqual((self.output.read_bytes(), self.output.stat().st_mtime_ns), self.before)
        else:
            self.assertEqual(result.stderr, b"")
        self.assertFalse(list(self.root.rglob(".cupidbuild*")))
        return result

    def test_direct_authoring_and_equal_timestamp(self):
        rows = [("empty", None), ("payload", b"content")]
        self.arrange(rows)
        self.execute()
        self.assertEqual(self.output.read_bytes(), publication.bundle.image([(n.encode(), d) for n, d in rows]))
        stamp = self.output.stat().st_mtime_ns
        self.execute(); self.assertEqual(self.output.stat().st_mtime_ns, stamp)

    def test_relative_repository_root(self):
        self.arrange([("empty", None)])
        for spelling in (".", "./", ".//"):
            arguments = self.arguments(); arguments[3] = spelling
            with self.subTest(root=spelling): self.execute(arguments)

    def test_all_authority_and_source_flags_are_required(self):
        self.arrange([("payload", b"content")])
        arguments = self.arguments()
        for index in range(2, len(arguments), 2):
            with self.subTest(option=arguments[index]):
                self.execute(arguments[:index] + arguments[index + 2:], 2)

    def test_duplicates_missing_values_and_unknown_options(self):
        self.arrange([("payload", b"content")])
        arguments = self.arguments()
        for extra in (["--root", str(self.root)], ["--seed-release", self.paths[5]],
                      ["--output"], ["--timeout", "120"], ["--unknown", "value"]):
            with self.subTest(extra=extra): self.execute(arguments + extra, 2)

    def test_invalid_manifest_and_selected_release_preserve_output(self):
        self.arrange([("payload", b"content")], b"missing\n")
        self.execute(expected=1)
        self.arrange([("payload", b"content")])
        release = self.root / self.paths[5]; saved = release.read_bytes()
        release.write_bytes(b"invalid release"); self.execute(expected=1)
        release.write_bytes(saved); self.execute()

    def test_output_subtree_rejection(self):
        self.arrange([("payload", b"content")])
        for output in ("fixtures/image.iso", "seed/linux/image.iso", "seed/windows/image.iso"):
            arguments = self.arguments(); arguments[9] = output
            self.execute(arguments, 1)

    def test_unicode_and_long_fixture_spelling(self):
        self.paths[0] = "caf\u00e9 \U0001f600.manifest"
        self.paths[1] = "\u8cc7\u6599 \U0001f600/" + "a" * 100 + "/" + "b" * 100
        rows = [("payload", b"long utf8 path")]
        self.arrange(rows); self.execute()
        self.assertEqual(self.output.read_bytes(), publication.bundle.image([(n.encode(), d) for n, d in rows]))

    def test_help_describes_the_complete_pair_and_release(self):
        result = subprocess.run([str(self.cli), "--help"], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for text in (b"publish-iso-fixture", b"--linux-manifest", b"--windows-manifest", b"--seed-release"):
            self.assertIn(text, result.stdout)
        self.assertEqual(result.stderr, b"")
